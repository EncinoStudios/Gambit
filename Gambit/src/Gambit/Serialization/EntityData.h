#pragma once

#include "Gambit/Core/Result.h"
#include "Gambit/Core/UUID.h"
#include "Gambit/ECS/Entity.h"
#include "Gambit/Reflection/ComponentInfo.h"
#include "Gambit/Reflection/FieldType.h"
#include "Gambit/Reflection/TypeRegistry.h"
#include "Gambit/Serialization/FieldSerializer.h"
#include "Gambit/Serialization/Json.h"

#include <functional>
#include <span>
#include <string>
#include <vector>

// Entities as data: the common form of everything that stores or copies entities (scene and prefab files, prefab
// instantiation, duplicating, undo snapshots). Capturing reads a world through the TypeRegistry, spawning writes one,
// and the JSON conversions give the file format, so no component ever needs code of its own (D-022).

namespace Gambit
{
	// One field of a component with its value.
	struct FieldData
	{
		std::string Name;
		FieldValue Value;
	};

	// A component of an entity by name, with the fields that are stored. Applying it to an entity that lacks the
	// component adds it with its defaults first, so fields that are not listed keep their defaults; on an entity that
	// has it they stay as they are.
	struct ComponentData
	{
		std::string Name;
		std::vector<FieldData> Fields;
	};

	// An entity: its UUID, its parent (null for roots) and its components. Components appear in registry (name)
	// order when captured.
	struct EntityData
	{
		UUID Id;
		UUID Parent;
		std::vector<ComponentData> Components;
	};

	// Whether files store the component: serializable, and neither engine-managed (identity and hierarchy are stored
	// as the entity's UUID and parent) nor derived.
	[[nodiscard]] inline bool IsStoredInFiles(const ComponentInfo& info) noexcept
	{
		return info.HasFlag(ComponentFlags::Serializable) && !info.HasFlag(ComponentFlags::Internal)
			&& !info.HasFlag(ComponentFlags::Derived);
	}

	// ---- JSON ----

	// A live component as an object with a member for each serialized field in field order (tags give {}). `path`
	// is the JSON pointer of the result, for error messages.
	[[nodiscard]] Result<Json>
	ComponentToJson(const ComponentInfo& info, const void* component, std::string_view path = {});
	[[nodiscard]] Result<Json>
	ComponentDataToJson(const ComponentInfo& info, const ComponentData& data, std::string_view path = {});
	// Decodes the fields present in the object (a partial object is fine: see ComponentData). Unknown fields are
	// warnings, malformed values errors.
	[[nodiscard]] Result<ComponentData>
	ComponentDataFromJson(const ComponentInfo& info, const Json& json, std::string_view path, LoadReport& report);

	// { "UUID": "0x...", "Parent": "0x..." (left out for roots), "Components": { "<Name>": { fields } } }
	[[nodiscard]] Result<Json>
	EntityToJson(const TypeRegistry& types, const EntityData& entity, std::string_view path = {});
	// Components that are unknown to the registry or not stored in files are warnings and are left out.
	[[nodiscard]] Result<EntityData>
	EntityFromJson(const TypeRegistry& types, const Json& json, std::string_view path, LoadReport& report);

	// A JSON array of entities.
	[[nodiscard]] Result<Json>
	EntitiesToJson(const TypeRegistry& types, std::span<const EntityData> entities, std::string_view path = {});
	[[nodiscard]] Result<std::vector<EntityData>>
	EntitiesFromJson(const TypeRegistry& types, const Json& json, std::string_view path, LoadReport& report);

	// ---- World to data and back ----

	// `root` and its descendants in hierarchy pre-order (parents first, siblings in order). The root's parent is left
	// null unless keepRootParent is set (restoring a deleted subtree under its old parent). Reads the world only.
	[[nodiscard]] std::vector<EntityData> CaptureSubtree(World& world, Entity root, bool keepRootParent = false);
	// Every entity of the world in hierarchy pre-order, roots in World::GetRoots order.
	[[nodiscard]] std::vector<EntityData> CaptureWorld(World& world);

	// Where the UUIDs of spawned entities come from.
	enum class UUIDMode
	{
		Keep,      // the UUIDs of the data (loading a scene, restoring a snapshot): they must be free in the world
		FromWorld, // new ones from the world's random generator (World::GenerateUUID): deterministic, for simulation
		Random     // new random ones (UUID::Generate): for editors and tools, not for simulation
	};

	// What happens to entity references that point outside the data being spawned (new UUIDs only).
	enum class ExternalReferences
	{
		Keep, // they stay as they are: right when duplicating inside one world
		Clear // they become null: right when the data came from elsewhere, like a prefab
	};

	struct SpawnOptions
	{
		UUIDMode Ids = UUIDMode::Keep;
		// Entities of the data that have no parent become children of this entity (roots when null).
		Entity Parent;
		// Entity-typed fields that refer to spawned entities are changed to the new UUIDs whenever Ids is not Keep.
		ExternalReferences References = ExternalReferences::Keep;
	};

	// Creates the entities in the world, in hierarchy pre-order whatever the order of `entities` (so the storage order
	// that systems iterate depends on the structure only), and returns them in that order. The hierarchy is built with
	// World::SetParent; siblings are in the order of the data. Everything is validated first and nothing is changed
	// when the data is not valid: bad or taken UUIDs, unknown parents or cycles, unknown or engine-managed components,
	// unknown fields or values that do not fit them.
	[[nodiscard]] Result<std::vector<Entity>>
	SpawnEntities(World& world, std::span<const EntityData> entities, const SpawnOptions& options = {});

	// Checks that the list is self-contained: UUIDs are valid and unique, every parent is in the list and the parent
	// links have no cycle. (SpawnEntities checks this and more; this is for formats that must be well-formed on
	// their own.)
	[[nodiscard]] Result<void> ValidateEntityHierarchy(std::span<const EntityData> entities);

	// ---- References ----

	// The distinct non-null AssetHandle UUIDs the data refers to, ascending: the dependencies of a scene or prefab.
	[[nodiscard]] std::vector<UUID>
	CollectAssetReferences(const TypeRegistry& types, std::span<const EntityData> entities);

	// Sets every Entity-typed reference to an entity that is not in `entities` to null and returns how many it cleared.
	size_t ClearExternalEntityReferences(const TypeRegistry& types, std::span<EntityData> entities);
}
