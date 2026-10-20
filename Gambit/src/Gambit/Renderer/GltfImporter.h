#pragma once

#include "Gambit/Asset/IAssetImporter.h"

namespace Gambit
{
	// Loads glTF 2.0 files (.gltf with embedded or external buffers and images, and .glb) as a Model whose meshes,
	// materials and textures are sub-assets with stable handles (D-038). Everything is read through the VFS.
	//
	// - Meshes: one Mesh sub-asset per glTF mesh ("Mesh/<name>"), one submesh per triangle primitive; strips and fans
	//   become triangle lists, points and lines are skipped with a warning. Missing normals become flat normals (glTF
	//   section 3.7.2.1), missing tangents are generated with MikkTSpace (D-037). Quantized attributes
	//   (KHR_mesh_quantization) and sparse accessors are read as floats.
	// - Materials: "Material/<name>", the metallic-roughness model with KHR_materials_emissive_strength;
	//   KHR_texture_transform (offset and scale) becomes the material's UV tiling and offset.
	// - Textures: "Texture/<name>", sRGB when a material uses them as base color or emissive, linear otherwise (a
	//   texture used both ways becomes two, the linear one keyed "<name> (Linear)"); filters and wrap modes come from
	//   the glTF sampler.
	// - Nodes: the default scene's hierarchy with transforms, meshes, cameras and KHR_lights_punctual lights.
	// Names that are missing or shared become "#<index>". Files that require other extensions are refused; skins,
	// animations, morph targets and GPU instancing are not imported (a warning says so).
	class GltfImporter final : public IAssetImporter
	{
	public:
		[[nodiscard]] AssetType GetType() const noexcept override { return AssetType::Model; }
		[[nodiscard]] Result<Ref<Asset>> Load(const AssetLoadContext& context) const override;
	};
}
