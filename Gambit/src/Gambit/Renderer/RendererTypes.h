#pragma once

namespace Gambit
{
	class AssetManager;
	class TypeRegistry;

	// Registers the renderer's reflected types: the render components and the structs and enums of its assets
	// (textures' import settings, materials). RegisterEngineTypes calls it.
	void RegisterRendererTypes(TypeRegistry& registry);

	// Registers the renderer's importers (textures, materials, glTF models) and built-in assets (MeshFactory meshes,
	// default textures, the default material) in `manager`. RegisterEngineAssets calls it.
	void RegisterRendererAssets(AssetManager& manager);
}
