#pragma once

#include "Gambit/Core/UUID.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace Gambit
{
	// An asset is referenced by its handle: a UUID kept in the asset's .meta file, so renaming or moving the file
	// never breaks a reference (D-014). The null handle refers to no asset.
	using AssetHandle = UUID;

	// The kinds of assets (extend when a module adds one). The names are used in .meta files and as the AssetType
	// attribute of AssetHandle fields.
	enum class AssetType : uint8_t
	{
		None = 0,
		Scene,
		Prefab,
		Texture,
		Mesh,
		Material,
		Environment,
		Script,
		Audio,
		Font,
		VectorGraphic,
		DataTable,
		AnimationClip,
		Skeleton,
		InputActions,
		Model // a glTF file: a node hierarchy whose meshes, materials and textures are sub-assets
	};

	// "Texture", "DataTable", ...; "None" for AssetType::None.
	[[nodiscard]] std::string_view ToString(AssetType type) noexcept;
	// The type with this name. AssetType::None is not a valid answer: unknown names and "None" give nullopt.
	[[nodiscard]] std::optional<AssetType> ParseAssetType(std::string_view name) noexcept;

	// The type of the asset file at `path` (a project-relative path or just a file name), from its name: the
	// extension, compared without regard to case (".gscene" Scene, ".gprefab" Prefab, ".png" ".jpg" ".jpeg" ".tga"
	// ".bmp" Texture, ".hdr" Environment, ".glb" ".gltf" Model, ".gmat" Material, ".lua" Script, ".wav" ".ogg" ".mp3"
	// ".flac" Audio, ".ttf" ".otf" Font, ".svg" VectorGraphic, ".csv" DataTable) and the file name "InputActions.json".
	// Meshes, AnimationClip and Skeleton assets are sub-assets of models (or built into the engine) and have no files
	// of their own. Everything else, including ".meta" files, is AssetType::None.
	[[nodiscard]] AssetType DetectAssetType(std::string_view path) noexcept;
}
