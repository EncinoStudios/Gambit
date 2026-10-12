#include "gbpch.h"

#include "Gambit/Serialization/SceneSerializer.h"

namespace Gambit::SceneSerializer
{
	namespace
	{
		constexpr const char* FormatName = "scene";

		Result<SceneData> Decode(
			const TypeRegistry& types, Json document, std::string_view source, LoadReport& report,
			const MigrationTable& migrations)
		{
			if (Result<void> upgraded = migrations.Upgrade(document, FormatVersion, FormatName); !upgraded)
			{
				return AddSource(source, upgraded.GetError());
			}

			SceneData scene;
			for (const auto& [key, member] : document.items())
			{
				const std::string path = JsonPathAppend({}, key);
				if (key == "Name")
				{
					if (!member.is_string())
					{
						return AddSource(source, JsonTypeError(path, "a string", member));
					}
					scene.Metadata.Name = member.get<std::string>();
				}
				else if (key == "Seed")
				{
					Result<uint64_t> seed = ReadJsonInteger<uint64_t>(member, path);
					if (!seed)
					{
						return AddSource(source, seed.GetError());
					}
					scene.Seed = *seed;
				}
				else if (key == "Settings")
				{
					if (!member.is_object())
					{
						return AddSource(source, JsonTypeError(path, "an object", member));
					}
					scene.Metadata.Settings = member;
				}
				else if (key == "Entities")
				{
					Result<std::vector<EntityData>> entities = EntitiesFromJson(types, member, path, report);
					if (!entities)
					{
						return AddSource(source, entities.GetError());
					}
					scene.Entities = std::move(*entities);
					if (Result<void> valid = ValidateEntityHierarchy(scene.Entities); !valid)
					{
						return AddSource(source, MakePathError(path, valid.GetError().Message));
					}
				}
				else if (key != "Version")
				{
					report.Warn(path, "unknown member, ignored");
				}
			}
			return scene;
		}
	}

	const MigrationTable& GetMigrations()
	{
		return GetNoMigrations();
	}

	Result<Json> ToJson(World& world, const SceneMetadata& metadata)
	{
		const std::vector<EntityData> entities = CaptureWorld(world);
		Result<Json> encoded = EntitiesToJson(world.GetTypeRegistry(), entities, "/Entities");
		if (!encoded)
		{
			return encoded;
		}
		Json document = Json::object();
		document["Version"] = FormatVersion;
		document["Name"] = metadata.Name;
		document["Seed"] = world.GetSeed();
		document["Settings"] = metadata.Settings;
		document["Entities"] = std::move(*encoded);
		return document;
	}

	Result<std::string> Serialize(World& world, const SceneMetadata& metadata)
	{
		Result<Json> document = ToJson(world, metadata);
		if (!document)
		{
			return document.GetError();
		}
		return DumpJson(*document);
	}

	Result<void> Save(World& world, const SceneMetadata& metadata, VFS& vfs, std::string_view path)
	{
		Result<std::string> text = Serialize(world, metadata);
		if (!text)
		{
			return AddSource(path, text.GetError());
		}
		return vfs.WriteText(path, *text);
	}

	Result<SceneData> Parse(
		const TypeRegistry& types, std::string_view text, std::string_view source, LoadReport& report,
		const MigrationTable& migrations)
	{
		Result<Json> document = ParseJson(text, source);
		if (!document)
		{
			return document.GetError();
		}
		return Decode(types, std::move(*document), source, report, migrations);
	}

	Result<SceneData> Read(const TypeRegistry& types, const VFS& vfs, std::string_view path, LoadReport& report)
	{
		Result<std::string> text = vfs.ReadText(path);
		if (!text)
		{
			return AddSource(path, text.GetError());
		}
		return Parse(types, *text, path, report);
	}

	Result<void> Apply(World& world, const SceneData& scene)
	{
		if (world.GetEntityCount() != 0)
		{
			return Error("a scene can only be loaded into an empty world");
		}
		Result<std::vector<Entity>> spawned = SpawnEntities(world, scene.Entities);
		if (!spawned)
		{
			return spawned.GetError();
		}
		world.SetSeed(scene.Seed);
		return {};
	}

	Result<SceneMetadata> Deserialize(
		World& world, std::string_view text, std::string_view source, LoadReport& report,
		const MigrationTable& migrations)
	{
		Result<SceneData> scene = Parse(world.GetTypeRegistry(), text, source, report, migrations);
		if (!scene)
		{
			return scene.GetError();
		}
		if (Result<void> applied = Apply(world, *scene); !applied)
		{
			return AddSource(source, applied.GetError());
		}
		report.Log(source);
		return std::move(scene->Metadata);
	}

	Result<SceneMetadata> Load(World& world, const VFS& vfs, std::string_view path, LoadReport& report)
	{
		Result<std::string> text = vfs.ReadText(path);
		if (!text)
		{
			return AddSource(path, text.GetError());
		}
		return Deserialize(world, *text, path, report);
	}
}
