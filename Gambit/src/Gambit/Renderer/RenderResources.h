#pragma once

#include "Gambit/Asset/AssetType.h"
#include "Gambit/Core/Base.h"
#include "Gambit/Core/Result.h"
#include "Gambit/Math/Bounds.h"
#include "Gambit/Renderer/GpuTypes.h"
#include "Gambit/Renderer/Material.h"
#include "Gambit/Renderer/MeshSource.h"
#include "Gambit/Renderer/Model.h"
#include "Gambit/Renderer/Texture.h"

#include <nvrhi/nvrhi.h>

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Gambit
{
	class AssetManager;
	class GraphicsDevice;

	// A texture on the GPU with the sampler its import settings ask for. The texture rests in the shader-resource
	// state.
	struct GpuTexture
	{
		nvrhi::TextureHandle Texture;
		nvrhi::SamplerHandle Sampler;
		uint32_t Width = 0;
		uint32_t Height = 0;
		uint32_t MipLevels = 1;
	};

	// A mesh on the GPU: positions in one vertex buffer, the other attributes interleaved in a second
	// (Gpu::VertexAttributes, missing streams filled with neutral values), 32-bit indices.
	struct GpuMesh
	{
		nvrhi::BufferHandle Positions;
		nvrhi::BufferHandle Attributes;
		nvrhi::BufferHandle Indices;
		std::vector<Submesh> Submeshes;
		std::vector<AssetHandle> Materials;
		AABB Bounds;
		uint32_t VertexCount = 0;
		uint32_t IndexCount = 0;
	};

	// A material on the GPU: its constant buffer and the binding set of descriptor set 1 (constants, five textures and
	// their samplers), rebuilt when the material's version or one of its textures changes.
	struct GpuMaterial
	{
		nvrhi::BufferHandle Constants;
		nvrhi::BindingSetHandle BindingSet;
		AlphaMode Alpha = AlphaMode::Opaque;
		bool DoubleSided = false;
	};

	// The GPU copies of the assets the renderer draws with (meshes, textures, materials) and the models it draws,
	// created when first used and rebuilt when the AssetManager hands out another object for a handle (a reload) or a
	// material's version changes. Each copy holds its asset, so AssetManager::UnloadUnused keeps what is drawn; copies
	// that no Get call asked for during IdleFramesBeforeRelease frames are released (with the assets they hold).
	// Uploads, including the generation of mip chains on the GPU (render passes that box-filter each level from the
	// one above, correct for sRGB and non-power-of-two sizes), are submitted right away. Assets that fail to load or
	// to upload are reported once and replaced: textures by a default texture, materials by the default material,
	// meshes by nothing; a failed upload is tried again only for another object or version of the asset. Not thread
	// safe; every object it creates must be released before the device.
	class RenderResources
	{
	public:
		// Frames (BeginFrame calls) after which a copy nobody asked for is released.
		static constexpr uint64_t IdleFramesBeforeRelease = 120;

		RenderResources(GraphicsDevice& device, AssetManager& assets);
		~RenderResources();
		RenderResources(const RenderResources&) = delete;
		RenderResources& operator=(const RenderResources&) = delete;

		[[nodiscard]] GraphicsDevice& GetDevice() const noexcept { return m_Device; }
		[[nodiscard]] AssetManager& GetAssets() const noexcept { return m_Assets; }
		// The layout of descriptor set 1 (Gpu::MaterialSet) that every material binding set uses.
		[[nodiscard]] nvrhi::IBindingLayout* GetMaterialBindingLayout() const noexcept { return m_MaterialLayout; }

		// Starts a frame: cached objects are checked against their assets again (once per frame), and those unused
		// for IdleFramesBeforeRelease frames are released.
		void BeginFrame();

		// The GPU mesh of a Mesh asset; null when the handle names no loadable mesh (reported once).
		[[nodiscard]] const GpuMesh* GetMesh(AssetHandle handle);
		// A Model asset (its meshes are drawn with GetMesh); null when the handle names no loadable model (reported
		// once).
		[[nodiscard]] Ref<Model> GetModel(AssetHandle handle);
		// The GPU texture of a Texture asset; the default texture `fallback` for the null handle or a texture that
		// cannot be loaded (reported once).
		[[nodiscard]] const GpuTexture& GetTexture(AssetHandle handle, DefaultTexture fallback);
		// The GPU material of a Material asset; the default material for the null handle or a material that cannot
		// be loaded (reported once).
		[[nodiscard]] const GpuMaterial& GetMaterial(AssetHandle handle);

		// Uploads a texture asset that is not managed (tests, tools): the texture with its mip chain and sampler.
		[[nodiscard]] Result<GpuTexture> CreateTexture(const Texture2D& texture, std::string_view debugName);
		// Uploads mesh data that is not managed.
		[[nodiscard]] Result<GpuMesh> CreateMesh(const MeshData& mesh, std::string_view debugName);

		// Drops every cached object and the assets they hold (the GPU frees them once it no longer uses them).
		void Clear();

	private:
		// A GPU copy and the asset object it was made from. Object is null when making it failed: the failure was
		// reported and is not retried while Source stays the same.
		template<typename T>
		struct Entry
		{
			Ref<const Asset> Source;
			uint64_t CheckedFrame = 0; // the latest frame that asked for it
			Scope<T> Object;
		};

		struct MaterialEntry
		{
			Ref<const Asset> Source;
			uint64_t Version = 0;
			uint64_t CheckedFrame = 0;
			// The textures the binding set was made with (held, so that a replaced texture is always noticed).
			std::array<nvrhi::TextureHandle, Gpu::MaterialTextureCount> Textures{};
			Scope<GpuMaterial> Object;
		};

		struct ModelEntry
		{
			Ref<Model> Source;
			uint64_t CheckedFrame = 0;
		};

		template<typename T>
		[[nodiscard]] Ref<T> LoadTyped(AssetHandle handle);
		void ReportOnce(AssetHandle handle, std::string_view message);

		[[nodiscard]] nvrhi::ISampler* GetSampler(const TextureImportSettings& settings);
		[[nodiscard]] Result<void> GenerateMips(nvrhi::ICommandList& commandList, nvrhi::ITexture* texture);
		[[nodiscard]] Result<Scope<GpuMaterial>> CreateMaterial(
			const MaterialData& data, const std::array<const GpuTexture*, Gpu::MaterialTextureCount>& textures);
		[[nodiscard]] std::array<const GpuTexture*, Gpu::MaterialTextureCount>
		GetMaterialTextures(const MaterialData& data);
		// Records with `record` into the upload command list and executes it.
		[[nodiscard]] Result<void> Upload(const std::function<Result<void>(nvrhi::ICommandList&)>& record);

		GraphicsDevice& m_Device;
		AssetManager& m_Assets;
		uint64_t m_Frame = 1;
		nvrhi::CommandListHandle m_UploadCommandList;
		nvrhi::BindingLayoutHandle m_MaterialLayout;
		nvrhi::BindingLayoutHandle m_MipLayout;
		std::map<nvrhi::Format, nvrhi::GraphicsPipelineHandle> m_MipPipelines;
		std::map<std::tuple<TextureFilter, TextureWrap, TextureWrap, bool>, nvrhi::SamplerHandle> m_Samplers;
		std::unordered_map<AssetHandle, Entry<GpuMesh>> m_Meshes;      // lookups only
		std::unordered_map<AssetHandle, Entry<GpuTexture>> m_Textures; // lookups only
		std::unordered_map<AssetHandle, MaterialEntry> m_Materials;    // lookups only
		std::unordered_map<AssetHandle, ModelEntry> m_Models;          // lookups only
		std::unordered_set<AssetHandle> m_Reported;                    // lookups only
	};
}
