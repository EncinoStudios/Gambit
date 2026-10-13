#include "gbpch.h"

#include "Gambit/Renderer/RendererTypes.h"

#include "Gambit/Asset/AssetManager.h"
#include "Gambit/Renderer/Components/RenderComponents.h"
#include "Gambit/Renderer/GltfImporter.h"
#include "Gambit/Renderer/Material.h"
#include "Gambit/Renderer/MeshFactory.h"
#include "Gambit/Renderer/Texture.h"

namespace Gambit
{
	void RegisterRendererTypes(TypeRegistry& registry)
	{
		RegisterTextureTypes(registry);
		RegisterMaterialTypes(registry);
		RegisterRenderComponents(registry);
	}

	void RegisterRendererAssets(AssetManager& manager)
	{
		manager.RegisterImporter(CreateScope<TextureImporter>());
		manager.RegisterImporter(CreateScope<MaterialImporter>());
		manager.RegisterImporter(CreateScope<GltfImporter>());
		MeshFactory::RegisterBuiltinAssets(manager);
		RegisterDefaultTextures(manager);
		RegisterDefaultMaterial(manager);
	}
}
