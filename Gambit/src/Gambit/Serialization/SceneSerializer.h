#pragma once

#include "Gambit/Core/Result.h"
#include "Gambit/Core/VFS.h"
#include "Gambit/ECS/World.h"
#include "Gambit/Serialization/EntityData.h"
#include "Gambit/Serialization/Json.h"
#include "Gambit/Serialization/Migration.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Gambit
{
	// What a scene file stores besides its entities and the seed.
	struct SceneMetadata
	{
		std::string Name;
		// Scene-wide settings of the modules (physics gravity, environment, ...), an object keyed by module. Opaque
		// here: the owning modules read and write their own members.
		Json Settings = Json::object();
	};

	// A scene file decoded and checked, but not yet loaded into a world.
	struct SceneData
	{
		SceneMetadata Metadata;
		uint64_t Seed = 0;
		std::vector<EntityData> Entities; // hierarchy pre-order
	};

	// The .gscene format (D-025):
	//   { "Version": 1, "Name": "...", "Seed": 1234, "Settings": { ... },
	//     "Entities": [ { "UUID": "0x...", "Parent": "0x...", "Components": { "<Name>": { fields } } } ] }
	// Output is stable: entities in hierarchy pre-order (roots in World::GetRoots order, children in sibling order),
	// components in registry name order, fields in registration order, 2-space indent, trailing newline. Saving,
	// loading and saving again gives the same bytes, and the loaded world has the same state hash as the saved one
	// (the file stores the seed, not the random generator position, tick or time: it holds initial states).
	namespace SceneSerializer
	{
		inline constexpr uint32_t FormatVersion = 1;

		// Documents of older versions are upgraded with these steps before they are read.
		[[nodiscard]] const MigrationTable& GetMigrations();

		// The document of the world's entities (World is not changed; it is not const because handles need it).
		[[nodiscard]] Result<Json> ToJson(World& world, const SceneMetadata& metadata);
		[[nodiscard]] Result<std::string> Serialize(World& world, const SceneMetadata& metadata);
		// Writes the file through the VFS, atomically on directory mounts.
		[[nodiscard]] Result<void> Save(World& world, const SceneMetadata& metadata, VFS& vfs, std::string_view path);

		// Parses and checks a scene without a world. `source` names the text in messages. Unknown members, components
		// and fields are warnings in `report`; malformed values, bad or duplicate UUIDs, dangling parents and cycles
		// are errors. `migrations` replaces the format's own table (for tests).
		[[nodiscard]] Result<SceneData> Parse(
			const TypeRegistry& types, std::string_view text, std::string_view source, LoadReport& report,
			const MigrationTable& migrations = GetMigrations());
		// The same for a file of the VFS (the source is its path).
		[[nodiscard]] Result<SceneData>
		Read(const TypeRegistry& types, const VFS& vfs, std::string_view path, LoadReport& report);

		// Creates the entities of `scene` in `world`, which must be empty, and takes its seed. The world is not
		// changed when this fails.
		[[nodiscard]] Result<void> Apply(World& world, const SceneData& scene);

		// Parse and Apply in one step; returns the metadata. Warnings are logged and also left in `report`.
		[[nodiscard]] Result<SceneMetadata> Deserialize(
			World& world, std::string_view text, std::string_view source, LoadReport& report,
			const MigrationTable& migrations = GetMigrations());
		[[nodiscard]] Result<SceneMetadata>
		Load(World& world, const VFS& vfs, std::string_view path, LoadReport& report);
	}
}
