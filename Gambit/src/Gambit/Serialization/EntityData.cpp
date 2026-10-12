#include "gbpch.h"

#include "Gambit/Serialization/EntityData.h"

#include <set>
#include <unordered_map>
#include <unordered_set>

namespace Gambit
{
	namespace
	{
		constexpr const char* UuidKey = "UUID";
		constexpr const char* ParentKey = "Parent";
		constexpr const char* ComponentsKey = "Components";

		// ---- Walking the references inside values ----

		template<typename V, typename Visit>
		void VisitUUIDs(const FieldInfo& field, V& value, FieldType kind, Visit& visit);

		// `S` is StructValue or const StructValue. Values that do not match their field are skipped: the data may come
		// from anywhere, and validating it is the job of the callers that can report it.
		template<typename S, typename Visit>
		void VisitStructUUIDs(const StructInfo& info, S& value, FieldType kind, Visit& visit)
		{
			const std::span<const FieldInfo> fields = info.GetFields();
			if (value.Values.size() != fields.size())
			{
				return;
			}
			for (size_t index = 0; index < fields.size(); ++index)
			{
				VisitUUIDs(fields[index], value.Values[index], kind, visit);
			}
		}

		// Calls visit(UUID&) for each UUID of `kind` (Entity or AssetHandle) in a value of `field`: the value itself,
		// the elements of arrays and everything inside structs.
		template<typename V, typename Visit>
		void VisitUUIDs(const FieldInfo& field, V& value, FieldType kind, Visit& visit)
		{
			if (field.Type == kind)
			{
				if (auto* id = std::get_if<UUID>(&value))
				{
					visit(*id);
				}
			}
			else if (field.Type == FieldType::Array && field.ElementType == kind)
			{
				if (auto* ids = std::get_if<std::vector<UUID>>(&value))
				{
					for (auto& id : *ids)
					{
						visit(id);
					}
				}
			}
			else if (field.Type == FieldType::Struct)
			{
				if (auto* nested = std::get_if<StructValue>(&value);
				    nested != nullptr && field.Attributes.Struct != nullptr)
				{
					VisitStructUUIDs(*field.Attributes.Struct, *nested, kind, visit);
				}
			}
			else if (field.Type == FieldType::Array && field.ElementType == FieldType::Struct)
			{
				if (auto* elements = std::get_if<std::vector<StructValue>>(&value);
				    elements != nullptr && field.Attributes.Struct != nullptr)
				{
					for (auto& element : *elements)
					{
						VisitStructUUIDs(*field.Attributes.Struct, element, kind, visit);
					}
				}
			}
		}

		// Calls visit(UUID&) for every UUID of `kind` in the field values of `entities`. Components and fields the
		// registry does not know are skipped.
		template<typename Entities, typename Visit>
		void ForEachUUID(const TypeRegistry& types, Entities& entities, FieldType kind, Visit&& visit)
		{
			for (auto& entity : entities)
			{
				for (auto& component : entity.Components)
				{
					const ComponentInfo* info = types.FindComponent(component.Name);
					if (info == nullptr)
					{
						continue;
					}
					for (auto& data : component.Fields)
					{
						if (const FieldInfo* field = info->FindField(data.Name); field != nullptr)
						{
							VisitUUIDs(*field, data.Value, kind, visit);
						}
					}
				}
			}
		}

		// ---- Capturing ----

		EntityData CaptureEntity(const World& world, Entity entity, bool includeParent)
		{
			const entt::entity handle = entity.GetHandle();
			EntityData data;
			data.Id = entity.GetUUID();
			if (includeParent)
			{
				data.Parent = world.GetRegistry().get<RelationshipComponent>(handle).Parent;
			}
			for (const ComponentInfo* info : world.GetTypeRegistry().GetComponents())
			{
				if (!IsStoredInFiles(*info))
				{
					continue;
				}
				const void* raw = info->GetRaw(world, handle);
				if (raw == nullptr)
				{
					continue;
				}
				ComponentData component;
				component.Name = info->GetName();
				if (!info->IsTag())
				{
					for (const FieldInfo& field : info->GetFields())
					{
						if (IsSerialized(field))
						{
							component.Fields.push_back({ field.Name, field.GetValue(raw) });
						}
					}
				}
				data.Components.push_back(std::move(component));
			}
			return data;
		}

		// ---- Spawning ----

		struct ResolvedComponent
		{
			const ComponentInfo* Info = nullptr;
			const ComponentData* Data = nullptr;
			std::vector<const FieldInfo*> Fields; // parallel to Data->Fields
		};

		Error EntityError(UUID id, std::string_view message)
		{
			return Error::Format("entity {}: {}", id.ToString(), message);
		}

		// Looks up the components and fields of an entity and checks that the world can take them.
		Result<std::vector<ResolvedComponent>> ResolveComponents(const TypeRegistry& types, const EntityData& entity)
		{
			std::vector<ResolvedComponent> resolved;
			std::set<std::string_view> names;
			for (const ComponentData& component : entity.Components)
			{
				const ComponentInfo* info = types.FindComponent(component.Name);
				if (info == nullptr)
				{
					return EntityError(entity.Id, std::format("unknown component '{}'", component.Name));
				}
				if (!IsStoredInFiles(*info))
				{
					return EntityError(
						entity.Id,
						std::format("component '{}' is managed by the engine and cannot be spawned", component.Name));
				}
				if (!names.insert(component.Name).second)
				{
					return EntityError(entity.Id, std::format("component '{}' appears twice", component.Name));
				}
				ResolvedComponent entry{ info, &component, {} };
				for (const FieldData& data : component.Fields)
				{
					const FieldInfo* field = info->FindField(data.Name);
					if (field == nullptr)
					{
						return EntityError(
							entity.Id, std::format("component '{}' has no field '{}'", component.Name, data.Name));
					}
					if (!field->CanHold(data.Value))
					{
						return EntityError(
							entity.Id,
							std::format(
								"the value of '{}.{}' does not fit its type {}", component.Name, data.Name,
								ToString(field->Type)));
					}
					entry.Fields.push_back(field);
				}
				resolved.push_back(std::move(entry));
			}
			return resolved;
		}

		constexpr size_t NoParent = ~size_t{ 0 };

		// The structure of a list of entities: who is whose child, and the order of creation.
		struct Hierarchy
		{
			std::vector<size_t> Order;          // indices, hierarchy pre-order (parents first, siblings in list order)
			std::vector<size_t> ParentIndex;    // NoParent for roots
			std::vector<Entity> ExternalParent; // an existing entity of the world for roots that restore a subtree
		};

		// Checks UUIDs and parent links and works out the order of creation. With a world, UUIDs must be free in it
		// (`keepIds`) and a parent that is not in the list may be an entity of the world; without one the list must be
		// self-contained.
		Result<Hierarchy> ResolveHierarchy(World* world, std::span<const EntityData> entities, bool keepIds)
		{
			const size_t count = entities.size();
			std::unordered_map<UUID, size_t> indexOf; // lookups only
			indexOf.reserve(count);
			for (size_t index = 0; index < count; ++index)
			{
				const UUID id = entities[index].Id;
				if (!id.IsValid())
				{
					return Error::Format("entity {} has the null UUID", index);
				}
				if (!indexOf.emplace(id, index).second)
				{
					return EntityError(id, "the UUID appears twice");
				}
				if (keepIds && world != nullptr && world->ContainsEntity(id))
				{
					return EntityError(id, "the UUID is already used by an entity of the world");
				}
			}

			Hierarchy hierarchy;
			hierarchy.ParentIndex.assign(count, NoParent);
			hierarchy.ExternalParent.resize(count);
			std::vector<std::vector<size_t>> children(count);
			std::vector<size_t> roots;
			for (size_t index = 0; index < count; ++index)
			{
				const UUID parent = entities[index].Parent;
				if (!parent.IsValid())
				{
					roots.push_back(index);
				}
				else if (const auto found = indexOf.find(parent); found != indexOf.end())
				{
					if (found->second == index)
					{
						return EntityError(entities[index].Id, "the entity is its own parent");
					}
					hierarchy.ParentIndex[index] = found->second;
					children[found->second].push_back(index);
				}
				else if (keepIds && world != nullptr && world->ContainsEntity(parent))
				{
					hierarchy.ExternalParent[index] = world->TryGetEntity(parent); // restoring under the old parent
					roots.push_back(index);
				}
				else
				{
					return EntityError(
						entities[index].Id, std::format("its parent {} is not part of the data", parent.ToString()));
				}
			}

			// Hierarchy pre-order: the order of creation, hence of every storage, depends on the structure and not on
			// how the data happened to be sorted.
			hierarchy.Order.reserve(count);
			std::vector<size_t> pending(roots.rbegin(), roots.rend());
			while (!pending.empty())
			{
				const size_t index = pending.back();
				pending.pop_back();
				hierarchy.Order.push_back(index);
				pending.insert(pending.end(), children[index].rbegin(), children[index].rend());
			}
			if (hierarchy.Order.size() != count)
			{
				return Error("the parent links of the entities form a cycle");
			}
			return hierarchy;
		}

		UUID NewUUID(World& world, UUIDMode mode, const std::unordered_set<UUID>& taken)
		{
			for (;;)
			{
				const UUID candidate = mode == UUIDMode::FromWorld ? world.GenerateUUID() : UUID::Generate();
				if (!taken.contains(candidate) && !world.ContainsEntity(candidate))
				{
					return candidate;
				}
			}
		}
	}

	// ---- JSON ----

	Result<Json> ComponentToJson(const ComponentInfo& info, const void* component, std::string_view path)
	{
		Json object = Json::object();
		if (info.IsTag())
		{
			return object;
		}
		for (const FieldInfo& field : info.GetFields())
		{
			if (!IsSerialized(field))
			{
				continue;
			}
			Result<Json> encoded = FieldValueToJson(field, field.GetValue(component), JsonPathAppend(path, field.Name));
			if (!encoded)
			{
				return encoded;
			}
			object[field.Name] = std::move(*encoded);
		}
		return object;
	}

	Result<Json> ComponentDataToJson(const ComponentInfo& info, const ComponentData& data, std::string_view path)
	{
		for (const FieldData& field : data.Fields)
		{
			const FieldInfo* known = info.FindField(field.Name);
			if (known == nullptr || !IsSerialized(*known))
			{
				return MakePathError(
					path, std::format("component '{}' has no stored field '{}'", info.GetName(), field.Name));
			}
		}
		Json object = Json::object();
		for (const FieldInfo& field : info.GetFields()) // field order, whatever the order of the data
		{
			const auto found = std::ranges::find(data.Fields, field.Name, &FieldData::Name);
			if (found == data.Fields.end())
			{
				continue;
			}
			Result<Json> encoded = FieldValueToJson(field, found->Value, JsonPathAppend(path, field.Name));
			if (!encoded)
			{
				return encoded;
			}
			object[field.Name] = std::move(*encoded);
		}
		return object;
	}

	Result<ComponentData>
	ComponentDataFromJson(const ComponentInfo& info, const Json& json, std::string_view path, LoadReport& report)
	{
		ComponentData data;
		data.Name = info.GetName();
		const std::span<const FieldInfo> fields = info.GetFields();
		Result<void> read = ReadFieldMembers(fields, json, path, report, [&](size_t index, FieldValue&& value) {
			data.Fields.push_back({ fields[index].Name, std::move(value) });
		});
		if (!read)
		{
			return read.GetError();
		}
		return data;
	}

	Result<Json> EntityToJson(const TypeRegistry& types, const EntityData& entity, std::string_view path)
	{
		Json object = Json::object();
		object[UuidKey] = entity.Id.ToString();
		if (entity.Parent.IsValid())
		{
			object[ParentKey] = entity.Parent.ToString();
		}
		const std::string componentsPath = JsonPathAppend(path, ComponentsKey);
		Json components = Json::object();
		for (const ComponentData& component : entity.Components)
		{
			const std::string componentPath = JsonPathAppend(componentsPath, component.Name);
			const ComponentInfo* info = types.FindComponent(component.Name);
			if (info == nullptr)
			{
				return MakePathError(componentPath, "unknown component");
			}
			Result<Json> encoded = ComponentDataToJson(*info, component, componentPath);
			if (!encoded)
			{
				return encoded;
			}
			components[component.Name] = std::move(*encoded);
		}
		object[ComponentsKey] = std::move(components);
		return object;
	}

	Result<EntityData>
	EntityFromJson(const TypeRegistry& types, const Json& json, std::string_view path, LoadReport& report)
	{
		if (!json.is_object())
		{
			return MakePathError(path, "expected an entity object");
		}

		const auto readUUID = [&](const char* key) -> Result<UUID> {
			const std::string keyPath = JsonPathAppend(path, key);
			const Json& member = json.at(key);
			if (!member.is_string())
			{
				return MakePathError(keyPath, "expected a UUID string like \"0x00000000DEADBEEF\"");
			}
			const std::optional<UUID> uuid = UUID::Parse(member.get_ref<const std::string&>());
			if (!uuid.has_value())
			{
				return MakePathError(
					keyPath,
					std::format(
						"'{}' is not a UUID: expected \"0x\" and up to 16 hexadecimal digits",
						member.get_ref<const std::string&>()));
			}
			return *uuid;
		};

		EntityData entity;
		if (!json.contains(UuidKey))
		{
			return MakePathError(path, "the entity has no \"UUID\"");
		}
		Result<UUID> id = readUUID(UuidKey);
		if (!id)
		{
			return id.GetError();
		}
		if (!id->IsValid())
		{
			return MakePathError(JsonPathAppend(path, UuidKey), "the UUID must not be null");
		}
		entity.Id = *id;

		if (json.contains(ParentKey))
		{
			Result<UUID> parent = readUUID(ParentKey);
			if (!parent)
			{
				return parent.GetError();
			}
			entity.Parent = *parent;
		}

		for (const auto& [key, member] : json.items())
		{
			if (key != UuidKey && key != ParentKey && key != ComponentsKey)
			{
				report.Warn(JsonPathAppend(path, key), "unknown member, ignored");
			}
		}

		if (json.contains(ComponentsKey))
		{
			const std::string componentsPath = JsonPathAppend(path, ComponentsKey);
			const Json& components = json.at(ComponentsKey);
			if (!components.is_object())
			{
				return MakePathError(componentsPath, "expected an object of components");
			}
			for (const auto& [name, member] : components.items())
			{
				const std::string componentPath = JsonPathAppend(componentsPath, name);
				const ComponentInfo* info = types.FindComponent(name);
				if (info == nullptr)
				{
					report.Warn(componentPath, "unknown component, ignored");
					continue;
				}
				if (!IsStoredInFiles(*info))
				{
					report.Warn(componentPath, "this component is not stored in files, ignored");
					continue;
				}
				Result<ComponentData> data = ComponentDataFromJson(*info, member, componentPath, report);
				if (!data)
				{
					return data.GetError();
				}
				entity.Components.push_back(std::move(*data));
			}
		}
		return entity;
	}

	Result<Json> EntitiesToJson(const TypeRegistry& types, std::span<const EntityData> entities, std::string_view path)
	{
		Json array = Json::array();
		for (const EntityData& entity : entities)
		{
			Result<Json> encoded = EntityToJson(types, entity, JsonPathAppend(path, array.size()));
			if (!encoded)
			{
				return encoded;
			}
			array.push_back(std::move(*encoded));
		}
		return array;
	}

	Result<std::vector<EntityData>>
	EntitiesFromJson(const TypeRegistry& types, const Json& json, std::string_view path, LoadReport& report)
	{
		if (!json.is_array())
		{
			return MakePathError(path, "expected an array of entities");
		}
		std::vector<EntityData> entities;
		entities.reserve(json.size());
		for (size_t index = 0; index < json.size(); ++index)
		{
			Result<EntityData> entity = EntityFromJson(types, json[index], JsonPathAppend(path, index), report);
			if (!entity)
			{
				return entity.GetError();
			}
			entities.push_back(std::move(*entity));
		}
		return entities;
	}

	// ---- World to data and back ----

	std::vector<EntityData> CaptureSubtree(World& world, Entity root, bool keepRootParent)
	{
		GB_CORE_ASSERT(root.GetWorld() == &world && root.IsValid(), "CaptureSubtree: invalid entity");
		std::vector<EntityData> captured;
		std::vector<Entity> pending{ root };
		while (!pending.empty())
		{
			const Entity entity = pending.back();
			pending.pop_back();
			captured.push_back(CaptureEntity(world, entity, entity != root || keepRootParent));
			const std::vector<Entity> children = world.GetChildren(entity);
			pending.insert(pending.end(), children.rbegin(), children.rend()); // popped in sibling order
		}
		return captured;
	}

	std::vector<EntityData> CaptureWorld(World& world)
	{
		std::vector<EntityData> captured;
		captured.reserve(world.GetEntityCount());
		for (const Entity root : world.GetRoots())
		{
			std::vector<EntityData> subtree = CaptureSubtree(world, root);
			std::ranges::move(subtree, std::back_inserter(captured));
		}
		return captured;
	}

	Result<std::vector<Entity>>
	SpawnEntities(World& world, std::span<const EntityData> entities, const SpawnOptions& options)
	{
		const TypeRegistry& types = world.GetTypeRegistry();
		const size_t count = entities.size();

		// ---- Validate: nothing below changes the world until all of this holds ----
		if (options.Parent.GetWorld() != nullptr && (options.Parent.GetWorld() != &world || !options.Parent.IsValid()))
		{
			return Error("SpawnEntities: the parent is not a valid entity of this world");
		}
		Result<Hierarchy> structure = ResolveHierarchy(&world, entities, options.Ids == UUIDMode::Keep);
		if (!structure)
		{
			return structure.GetError();
		}
		const std::vector<size_t>& order = structure->Order;
		const std::vector<size_t>& parentIndex = structure->ParentIndex;
		const std::vector<Entity>& externalParent = structure->ExternalParent;

		std::vector<std::vector<ResolvedComponent>> resolved(count);
		for (const size_t index : order)
		{
			Result<std::vector<ResolvedComponent>> components = ResolveComponents(types, entities[index]);
			if (!components)
			{
				return components.GetError();
			}
			resolved[index] = std::move(*components);
		}

		// ---- Identities ----
		std::vector<UUID> newIds(count);
		std::unordered_map<UUID, UUID> remap; // old UUID to new one; lookups only
		if (options.Ids == UUIDMode::Keep)
		{
			for (size_t index = 0; index < count; ++index)
			{
				newIds[index] = entities[index].Id;
			}
		}
		else
		{
			std::unordered_set<UUID> taken;
			taken.reserve(count);
			for (const size_t index : order)
			{
				newIds[index] = NewUUID(world, options.Ids, taken);
				taken.insert(newIds[index]);
				remap.emplace(entities[index].Id, newIds[index]);
			}
		}

		const auto remapReference = [&](UUID& id) {
			if (!id.IsValid())
			{
				return;
			}
			if (const auto found = remap.find(id); found != remap.end())
			{
				id = found->second;
			}
			else if (options.References == ExternalReferences::Clear)
			{
				id = UUID();
			}
		};

		// ---- Create ----
		std::vector<Entity> created(count);
		std::vector<Entity> result;
		result.reserve(count);
		for (const size_t index : order)
		{
			Entity entity = world.CreateEntityWithUUID(newIds[index]);
			GB_CORE_VERIFY(entity.IsValid(), "SpawnEntities: a validated UUID was refused");
			created[index] = entity;

			for (const ResolvedComponent& component : resolved[index])
			{
				void* raw = component.Info->GetRaw(entity); // the Tag exists already
				if (raw == nullptr)
				{
					raw = component.Info->Add(entity);
				}
				GB_CORE_VERIFY(
					raw != nullptr, "SpawnEntities: component '{}' could not be added", component.Info->GetName());
				for (size_t field = 0; field < component.Fields.size(); ++field)
				{
					FieldValue value = component.Data->Fields[field].Value;
					if (options.Ids != UUIDMode::Keep)
					{
						VisitUUIDs(*component.Fields[field], value, FieldType::Entity, remapReference);
					}
					[[maybe_unused]] const bool written = component.Fields[field]->SetValue(raw, value);
					GB_CORE_ASSERT(written, "SpawnEntities: a validated value was refused");
				}
			}

			Entity parent = options.Parent;
			if (parentIndex[index] != NoParent)
			{
				parent = created[parentIndex[index]];
			}
			else if (externalParent[index].IsValid())
			{
				parent = externalParent[index];
			}
			if (parent.GetWorld() != nullptr)
			{
				[[maybe_unused]] const Result<void> attached = world.SetParent(entity, parent);
				GB_CORE_ASSERT(attached.HasValue(), "SpawnEntities: new entities cannot form a cycle");
			}
			result.push_back(entity);
		}
		return result;
	}

	Result<void> ValidateEntityHierarchy(std::span<const EntityData> entities)
	{
		Result<Hierarchy> structure = ResolveHierarchy(nullptr, entities, false);
		if (!structure)
		{
			return structure.GetError();
		}
		return {};
	}

	// ---- References ----

	std::vector<UUID> CollectAssetReferences(const TypeRegistry& types, std::span<const EntityData> entities)
	{
		std::set<UUID> found; // sorted: the result does not depend on hash or storage order
		ForEachUUID(types, entities, FieldType::AssetHandle, [&](const UUID& id) {
			if (id.IsValid())
			{
				found.insert(id);
			}
		});
		return std::vector<UUID>(found.begin(), found.end());
	}

	size_t ClearExternalEntityReferences(const TypeRegistry& types, std::span<EntityData> entities)
	{
		std::unordered_set<UUID> ids;
		ids.reserve(entities.size());
		for (const EntityData& entity : entities)
		{
			ids.insert(entity.Id);
		}
		size_t cleared = 0;
		ForEachUUID(types, entities, FieldType::Entity, [&](UUID& id) {
			if (id.IsValid() && !ids.contains(id))
			{
				id = UUID();
				++cleared;
			}
		});
		return cleared;
	}
}
