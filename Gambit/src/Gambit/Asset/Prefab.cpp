#include "gbpch.h"

#include "Gambit/Asset/Prefab.h"

#include "Gambit/Asset/Components/AssetComponents.h"

namespace Gambit
{
	namespace
	{
		constexpr const char* FormatName = "prefab";
		constexpr std::string_view PrefabInstanceName = "PrefabInstance";
		constexpr std::string_view TransformName = "Transform";

		FieldValue Uuid(UUID value)
		{
			return FieldValue(std::in_place_type<UUID>, value);
		}
	}

	Prefab::Prefab(
		ConstructionKey, std::string name, std::vector<EntityData> entities, std::vector<AssetHandle> dependencies)
		: m_Name(std::move(name)), m_Entities(std::move(entities)), m_Dependencies(std::move(dependencies))
	{}

	Result<Ref<Prefab>> Prefab::Create(const TypeRegistry& types, std::string name, std::vector<EntityData> entities)
	{
		if (entities.empty())
		{
			return Error("a prefab needs at least its root entity");
		}
		if (Result<void> valid = ValidateEntityHierarchy(entities); !valid)
		{
			return valid.GetError();
		}
		const auto roots =
			std::ranges::count_if(entities, [](const EntityData& entity) { return !entity.Parent.IsValid(); });
		if (roots != 1 || entities.front().Parent.IsValid())
		{
			return Error::Format(
				"a prefab has exactly one root, listed first (found {}{})", roots,
				roots == 1 ? ", but it is not the first entity" : " entities without a parent");
		}
		std::vector<AssetHandle> dependencies = CollectAssetReferences(types, entities);
		return CreateRef<Prefab>(ConstructionKey{}, std::move(name), std::move(entities), std::move(dependencies));
	}

	namespace PrefabUtils
	{
		const MigrationTable& GetMigrations()
		{
			return GetNoMigrations();
		}

		Result<Ref<Prefab>> CreateFromEntity(World& world, Entity root, std::string_view name)
		{
			if (root.GetWorld() != &world || !root.IsValid())
			{
				return Error("CreateFromEntity needs an entity of the world");
			}
			std::vector<EntityData> entities = CaptureSubtree(world, root);
			for (EntityData& entity : entities)
			{
				std::erase_if(entity.Components, [](const ComponentData& component) {
					return component.Name == PrefabInstanceName;
				});
			}
			const TypeRegistry& types = world.GetTypeRegistry();
			if (const size_t cleared = ClearExternalEntityReferences(types, entities); cleared > 0)
			{
				GB_CORE_WARN(
					"Prefab '{}': {} reference(s) to entities outside the prefab were cleared", root.GetName(),
					cleared);
			}
			return Prefab::Create(
				types, std::string(name.empty() ? std::string_view(root.GetName()) : name), std::move(entities));
		}

		Result<Entity> Instantiate(
			World& world, const Prefab& prefab, Entity parent, const std::optional<TransformComponent>& transform,
			const InstantiateOptions& options)
		{
			std::vector<EntityData> entities(prefab.GetEntities().begin(), prefab.GetEntities().end());
			for (EntityData& entity : entities)
			{
				std::erase_if(entity.Components, [](const ComponentData& component) {
					return component.Name == PrefabInstanceName;
				});
				entity.Components.push_back(
					{ std::string(PrefabInstanceName),
				      { { "Prefab", Uuid(prefab.GetHandle()) }, { "SourceEntity", Uuid(entity.Id) } } });
			}
			if (transform.has_value())
			{
				EntityData& root = entities.front();
				std::erase_if(
					root.Components, [](const ComponentData& component) { return component.Name == TransformName; });
				root.Components.push_back({ std::string(TransformName),
				                            { { "Translation", FieldValue(transform->Translation) },
				                              { "Rotation", FieldValue(transform->Rotation) },
				                              { "Scale", FieldValue(transform->Scale) } } });
			}

			SpawnOptions spawn;
			spawn.Ids = options.Ids;
			spawn.Parent = parent;
			spawn.References = ExternalReferences::Clear;
			Result<std::vector<Entity>> spawned = SpawnEntities(world, entities, spawn);
			if (!spawned)
			{
				return Error::Format(
					"cannot instantiate prefab '{}': {}", prefab.GetName(), spawned.GetError().Message);
			}
			return spawned->front();
		}

		Result<Json> ToJson(const TypeRegistry& types, const Prefab& prefab)
		{
			Result<Json> entities = EntitiesToJson(types, prefab.GetEntities(), "/Entities");
			if (!entities)
			{
				return entities;
			}
			Json document = Json::object();
			document["Version"] = FormatVersion;
			document["Name"] = prefab.GetName();
			document["Entities"] = std::move(*entities);
			return document;
		}

		Result<std::string> Serialize(const TypeRegistry& types, const Prefab& prefab)
		{
			Result<Json> document = ToJson(types, prefab);
			if (!document)
			{
				return document.GetError();
			}
			return DumpJson(*document);
		}

		Result<void> Save(const TypeRegistry& types, const Prefab& prefab, VFS& vfs, std::string_view path)
		{
			Result<std::string> text = Serialize(types, prefab);
			if (!text)
			{
				return AddSource(path, text.GetError());
			}
			return vfs.WriteText(path, *text);
		}

		Result<Ref<Prefab>> Parse(
			const TypeRegistry& types, std::string_view text, std::string_view source, LoadReport& report,
			const MigrationTable& migrations)
		{
			Result<Json> document = ParseJson(text, source);
			if (!document)
			{
				return document.GetError();
			}
			if (Result<void> upgraded = migrations.Upgrade(*document, FormatVersion, FormatName); !upgraded)
			{
				return AddSource(source, upgraded.GetError());
			}

			std::string name;
			std::vector<EntityData> entities;
			bool hasEntities = false;
			for (const auto& [key, member] : document->items())
			{
				const std::string path = JsonPathAppend({}, key);
				if (key == "Name")
				{
					if (!member.is_string())
					{
						return AddSource(source, JsonTypeError(path, "a string", member));
					}
					name = member.get<std::string>();
				}
				else if (key == "Entities")
				{
					Result<std::vector<EntityData>> decoded = EntitiesFromJson(types, member, path, report);
					if (!decoded)
					{
						return AddSource(source, decoded.GetError());
					}
					entities = std::move(*decoded);
					hasEntities = true;
				}
				else if (key != "Version")
				{
					report.Warn(path, "unknown member, ignored");
				}
			}
			if (!hasEntities)
			{
				return AddSource(source, Error("the prefab has no \"Entities\""));
			}
			Result<Ref<Prefab>> prefab = Prefab::Create(types, std::move(name), std::move(entities));
			if (!prefab)
			{
				return AddSource(source, MakePathError("/Entities", prefab.GetError().Message));
			}
			return prefab;
		}

		Result<Ref<Prefab>> Load(const TypeRegistry& types, const VFS& vfs, std::string_view path, LoadReport& report)
		{
			Result<std::string> text = vfs.ReadText(path);
			if (!text)
			{
				return AddSource(path, text.GetError());
			}
			Result<Ref<Prefab>> prefab = Parse(types, *text, path, report);
			if (prefab)
			{
				report.Log(path);
			}
			return prefab;
		}
	}
}
