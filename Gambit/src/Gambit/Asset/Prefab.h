#pragma once

#include "Gambit/Asset/Asset.h"
#include "Gambit/Core/Result.h"
#include "Gambit/Core/VFS.h"
#include "Gambit/ECS/Components/CoreComponents.h"
#include "Gambit/ECS/Entity.h"
#include "Gambit/Serialization/EntityData.h"
#include "Gambit/Serialization/Migration.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Gambit
{
	// A prefab asset (D-026): a serialized entity subtree, loaded from a .gprefab file or captured from a world. It
	// has one root, which comes first, and the entities below it in hierarchy pre-order.
	class Prefab final : public Asset
	{
		struct ConstructionKey
		{
			explicit ConstructionKey() = default;
		};

	public:
		static constexpr AssetType StaticType = AssetType::Prefab;

		Prefab(
			ConstructionKey, std::string name, std::vector<EntityData> entities, std::vector<AssetHandle> dependencies);

		// Checks that `entities` have a single root, listed first, and a consistent hierarchy. `types` finds the asset
		// references that make up the dependencies.
		[[nodiscard]] static Result<Ref<Prefab>>
		Create(const TypeRegistry& types, std::string name, std::vector<EntityData> entities);

		[[nodiscard]] AssetType GetType() const noexcept override { return StaticType; }
		[[nodiscard]] std::vector<AssetHandle> GetDependencies() const override { return m_Dependencies; }

		[[nodiscard]] const std::string& GetName() const noexcept { return m_Name; }
		[[nodiscard]] std::span<const EntityData> GetEntities() const noexcept { return m_Entities; }
		[[nodiscard]] UUID GetRootId() const noexcept { return m_Entities.front().Id; }

	private:
		std::string m_Name;
		std::vector<EntityData> m_Entities;
		std::vector<AssetHandle> m_Dependencies;
	};

	// Making prefabs, spawning them, and the .gprefab format (D-025, D-026):
	//   { "Version": 1, "Name": "Crate", "Entities": [ <the entity format of scenes; the root first, without "Parent">
	//   ] }
	namespace PrefabUtils
	{
		inline constexpr uint32_t FormatVersion = 1;

		[[nodiscard]] const MigrationTable& GetMigrations();

		// Captures `root` and its descendants as a prefab named `name` (the name of the root when empty). A prefab
		// refers to no entity outside itself and to no other prefab: references to other entities are cleared (with a
		// warning in the log) and PrefabInstance components are dropped.
		[[nodiscard]] Result<Ref<Prefab>> CreateFromEntity(World& world, Entity root, std::string_view name = {});

		struct InstantiateOptions
		{
			// FromWorld draws the new UUIDs from the world's random generator, which keeps simulations deterministic
			// (D-011); Random is for editors and tools.
			UUIDMode Ids = UUIDMode::FromWorld;
		};

		// Creates a copy of the prefab as a child of `parent` (a root when null) and returns its root entity. Every
		// entity gets a new UUID, references between the prefab's entities follow, and each entity carries a
		// PrefabInstanceComponent. `transform` replaces the local transform of the root. Fails, creating nothing, when
		// the world cannot take the prefab's components.
		[[nodiscard]] Result<Entity> Instantiate(
			World& world, const Prefab& prefab, Entity parent = {},
			const std::optional<TransformComponent>& transform = std::nullopt, const InstantiateOptions& options = {});

		[[nodiscard]] Result<Json> ToJson(const TypeRegistry& types, const Prefab& prefab);
		[[nodiscard]] Result<std::string> Serialize(const TypeRegistry& types, const Prefab& prefab);
		[[nodiscard]] Result<void>
		Save(const TypeRegistry& types, const Prefab& prefab, VFS& vfs, std::string_view path);

		// Parses a prefab file. Unknown members, components and fields are warnings in `report`; `migrations`
		// replaces the format's own table (for tests).
		[[nodiscard]] Result<Ref<Prefab>> Parse(
			const TypeRegistry& types, std::string_view text, std::string_view source, LoadReport& report,
			const MigrationTable& migrations = GetMigrations());
		[[nodiscard]] Result<Ref<Prefab>>
		Load(const TypeRegistry& types, const VFS& vfs, std::string_view path, LoadReport& report);
	}
}
