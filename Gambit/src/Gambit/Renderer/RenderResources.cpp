#include "gbpch.h"

#include "Gambit/Renderer/RenderResources.h"

#include "Gambit/Asset/AssetManager.h"
#include "Gambit/RHI/GraphicsDevice.h"
#include "Gambit/RHI/PipelineUtils.h"

namespace Gambit
{
	namespace
	{
		// The fallback of each material texture slot when the material has none: white for factors that multiply,
		// the flat normal for normal maps.
		constexpr std::array<DefaultTexture, Gpu::MaterialTextureCount> MaterialTextureFallbacks = {
			DefaultTexture::White, DefaultTexture::White, DefaultTexture::FlatNormal, DefaultTexture::White,
			DefaultTexture::White
		};

		nvrhi::Format GetTextureFormat(const Texture2D& texture)
		{
			if (texture.IsHdr())
			{
				return nvrhi::Format::RGBA16_FLOAT;
			}
			return texture.IsSrgb() ? nvrhi::Format::SRGBA8_UNORM : nvrhi::Format::RGBA8_UNORM;
		}

		nvrhi::SamplerAddressMode ToAddressMode(TextureWrap wrap)
		{
			switch (wrap)
			{
				case TextureWrap::Repeat:
					return nvrhi::SamplerAddressMode::Repeat;
				case TextureWrap::ClampToEdge:
					return nvrhi::SamplerAddressMode::ClampToEdge;
				case TextureWrap::MirroredRepeat:
					return nvrhi::SamplerAddressMode::MirroredRepeat;
			}
			return nvrhi::SamplerAddressMode::Repeat;
		}

		std::array<AssetHandle, Gpu::MaterialTextureCount> GetTextureHandles(const MaterialData& data)
		{
			return { data.BaseColorTexture, data.MetallicRoughnessTexture, data.NormalTexture, data.OcclusionTexture,
				     data.EmissiveTexture };
		}

		Gpu::MaterialConstants MakeConstants(const MaterialData& data)
		{
			Gpu::MaterialConstants constants;
			constants.BaseColor = data.BaseColor;
			constants.Emissive = glm::vec4(data.EmissiveColor * data.EmissiveStrength, 0.0f);
			constants.UVTransform = glm::vec4(data.UVTiling, data.UVOffset);
			constants.Metallic = data.Metallic;
			constants.Roughness = data.Roughness;
			constants.NormalScale = data.NormalScale;
			constants.OcclusionStrength = data.OcclusionStrength;
			constants.AlphaCutoff = data.AlphaCutoff;
			constants.AlphaMode = static_cast<uint32_t>(data.Alpha);
			constants.Flags = data.NormalTexture.IsValid() ? Gpu::MaterialFlagNormalTexture : 0u;
			return constants;
		}
	}

	RenderResources::RenderResources(GraphicsDevice& device, AssetManager& assets)
		: m_Device(device), m_Assets(assets)
	{
		nvrhi::IDevice& nvrhiDevice = *device.GetNvrhiDevice();
		m_UploadCommandList = nvrhiDevice.createCommandList();
		GB_CORE_VERIFY(m_UploadCommandList != nullptr, "NVRHI could not create the upload command list");

		nvrhi::BindingLayoutDesc material = MakeBindingLayoutDesc(
			nvrhi::ShaderType::Vertex | nvrhi::ShaderType::Pixel, Gpu::MaterialSet,
			{ nvrhi::BindingLayoutItem::ConstantBuffer(Gpu::MaterialConstantsSlot) });
		for (uint32_t index = 0; index < Gpu::MaterialTextureCount; ++index)
		{
			material.addItem(nvrhi::BindingLayoutItem::Texture_SRV(Gpu::MaterialTextureSlot(index)));
			material.addItem(nvrhi::BindingLayoutItem::Sampler(Gpu::MaterialSamplerSlot(index)));
		}
		Result<nvrhi::BindingLayoutHandle> materialLayout = CreateBindingLayout(device, material);
		GB_CORE_VERIFY(materialLayout.HasValue(), "{}", materialLayout.GetError().Message);
		m_MaterialLayout = *materialLayout;

		Result<nvrhi::BindingLayoutHandle> mipLayout = CreateBindingLayout(
			device, MakeBindingLayoutDesc(nvrhi::ShaderType::Pixel, 0, { nvrhi::BindingLayoutItem::Texture_SRV(0) }));
		GB_CORE_VERIFY(mipLayout.HasValue(), "{}", mipLayout.GetError().Message);
		m_MipLayout = *mipLayout;
	}

	RenderResources::~RenderResources() = default;

	void RenderResources::BeginFrame()
	{
		++m_Frame;
		const auto idle = [this](const auto& entry) {
			return m_Frame - entry.second.CheckedFrame > IdleFramesBeforeRelease;
		};
		std::erase_if(m_Meshes, idle);
		std::erase_if(m_Textures, idle);
		std::erase_if(m_Materials, idle);
		std::erase_if(m_Models, idle);
	}

	void RenderResources::Clear()
	{
		m_Meshes.clear();
		m_Textures.clear();
		m_Materials.clear();
		m_Models.clear();
		m_Reported.clear();
	}

	void RenderResources::ReportOnce(AssetHandle handle, std::string_view message)
	{
		if (m_Reported.insert(handle).second)
		{
			GB_CORE_ERROR("Renderer: asset {}: {}", handle.ToString(), message);
		}
	}

	template<typename T>
	Ref<T> RenderResources::LoadTyped(AssetHandle handle)
	{
		const AssetType type = m_Assets.GetType(handle);
		if (type != T::StaticType)
		{
			ReportOnce(
				handle,
				type == AssetType::None ? std::string("there is no such asset")
										: std::format("it is a {}, not a {}", ToString(type), ToString(T::StaticType)));
			return nullptr;
		}
		Result<Ref<Asset>> asset = m_Assets.LoadAsset(handle);
		if (!asset)
		{
			ReportOnce(handle, asset.GetError().Message);
			return nullptr;
		}
		return std::static_pointer_cast<T>(*asset);
	}

	Result<void> RenderResources::Upload(const std::function<Result<void>(nvrhi::ICommandList&)>& record)
	{
		m_UploadCommandList->open();
		Result<void> recorded = record(*m_UploadCommandList);
		m_UploadCommandList->close();
		// A failed recording may have left commands half-way; executing them is harmless (they only touch the new
		// objects, which are dropped).
		m_Device.GetNvrhiDevice()->executeCommandList(m_UploadCommandList);
		return recorded;
	}

	nvrhi::ISampler* RenderResources::GetSampler(const TextureImportSettings& settings)
	{
		const auto key = std::make_tuple(settings.Filter, settings.WrapU, settings.WrapV, settings.GenerateMips);
		nvrhi::SamplerHandle& sampler = m_Samplers[key];
		if (!sampler)
		{
			const bool linear = settings.Filter == TextureFilter::Linear;
			nvrhi::SamplerDesc desc;
			desc.setAllFilters(linear)
				.setAddressU(ToAddressMode(settings.WrapU))
				.setAddressV(ToAddressMode(settings.WrapV))
				.setAddressW(nvrhi::SamplerAddressMode::ClampToEdge);
			if (linear && settings.GenerateMips && m_Device.GetInfo().Features.SamplerAnisotropy)
			{
				desc.setMaxAnisotropy(std::min(8.0f, m_Device.GetInfo().Limits.MaxSamplerAnisotropy));
			}
			sampler = m_Device.GetNvrhiDevice()->createSampler(desc);
			GB_CORE_VERIFY(sampler != nullptr, "NVRHI could not create a sampler");
		}
		return sampler;
	}

	Result<void> RenderResources::GenerateMips(nvrhi::ICommandList& commandList, nvrhi::ITexture* texture)
	{
		const nvrhi::TextureDesc& desc = texture->getDesc();
		nvrhi::IDevice& device = *m_Device.GetNvrhiDevice();
		const std::array layouts = { m_MipLayout };
		for (uint32_t level = 1; level < desc.mipLevels; ++level)
		{
			const nvrhi::FramebufferHandle framebuffer =
				device.createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(
					nvrhi::FramebufferAttachment().setTexture(texture).setSubresources(
						nvrhi::TextureSubresourceSet(level, 1, 0, 1))));
			if (!framebuffer)
			{
				return Error("NVRHI could not create a framebuffer for a mip level (see the log)");
			}
			nvrhi::GraphicsPipelineHandle& pipeline = m_MipPipelines[desc.format];
			if (!pipeline)
			{
				Result<nvrhi::GraphicsPipelineHandle> created =
					CreateFullscreenPipeline(m_Device, "GenerateMips.frag", framebuffer->getFramebufferInfo(), layouts);
				if (!created)
				{
					return created.GetError();
				}
				pipeline = *created;
			}
			Result<nvrhi::BindingSetHandle> set = CreateBindingSet(
				m_Device, m_MipLayout,
				{ nvrhi::BindingSetItem::Texture_SRV(
					0, texture, desc.format, nvrhi::TextureSubresourceSet(level - 1, 1, 0, 1)) });
			if (!set)
			{
				return set.GetError();
			}
			DrawFullscreenTriangle(commandList, MakeGraphicsState(pipeline, framebuffer).addBindingSet(*set));
		}
		return {};
	}

	Result<GpuTexture> RenderResources::CreateTexture(const Texture2D& texture, std::string_view debugName)
	{
		const Image& source = texture.GetImage();
		const uint32_t maxSize = m_Device.GetInfo().Limits.MaxTextureDimension2D;
		if (source.GetWidth() > maxSize || source.GetHeight() > maxSize)
		{
			return Error::Format(
				"the texture is {}x{}, the GPU supports at most {} texels per side", source.GetWidth(),
				source.GetHeight(), maxSize);
		}
		const Image image = texture.IsHdr() ? ConvertImage(source, ImageFormat::RGBA16F) : source;
		const uint32_t mipLevels = texture.GetMipLevelCount();
		const nvrhi::TextureDesc desc = nvrhi::TextureDesc()
											.setWidth(image.GetWidth())
											.setHeight(image.GetHeight())
											.setMipLevels(mipLevels)
											.setFormat(GetTextureFormat(texture))
											.setIsRenderTarget(mipLevels > 1)
											.setInitialState(nvrhi::ResourceStates::ShaderResource)
											.setKeepInitialState(true)
											.setDebugName(std::string(debugName));
		GpuTexture result;
		result.Texture = m_Device.GetNvrhiDevice()->createTexture(desc);
		if (!result.Texture)
		{
			return Error::Format("NVRHI could not create the texture '{}' (see the log)", debugName);
		}
		result.Sampler = GetSampler(texture.GetSettings());
		result.Width = image.GetWidth();
		result.Height = image.GetHeight();
		result.MipLevels = mipLevels;
		Result<void> uploaded = Upload([&](nvrhi::ICommandList& commandList) -> Result<void> {
			commandList.writeTexture(result.Texture, 0, 0, image.GetPixels().data(), image.GetRowPitch());
			return GenerateMips(commandList, result.Texture);
		});
		if (!uploaded)
		{
			return AddSource(debugName, uploaded.GetError());
		}
		return result;
	}

	Result<GpuMesh> RenderResources::CreateMesh(const MeshData& mesh, std::string_view debugName)
	{
		if (Result<void> valid = mesh.Validate(); !valid)
		{
			return AddSource(debugName, valid.GetError());
		}
		const size_t vertexCount = mesh.GetVertexCount();
		std::vector<Gpu::VertexAttributes> attributes(vertexCount);
		for (size_t vertex = 0; vertex < vertexCount; ++vertex)
		{
			Gpu::VertexAttributes& target = attributes[vertex];
			if (!mesh.Normals.empty())
			{
				target.Normal = mesh.Normals[vertex];
			}
			if (!mesh.Tangents.empty())
			{
				target.Tangent = mesh.Tangents[vertex];
			}
			if (!mesh.UV0.empty())
			{
				target.UV0 = mesh.UV0[vertex];
			}
			if (!mesh.UV1.empty())
			{
				target.UV1 = mesh.UV1[vertex];
			}
			if (!mesh.Colors.empty())
			{
				target.Color = mesh.Colors[vertex];
			}
		}

		nvrhi::IDevice& device = *m_Device.GetNvrhiDevice();
		const auto makeBuffer = [&](size_t size, bool index, std::string_view stream) {
			return device.createBuffer(
				nvrhi::BufferDesc()
					.setByteSize(size)
					.setIsVertexBuffer(!index)
					.setIsIndexBuffer(index)
					.setInitialState(index ? nvrhi::ResourceStates::IndexBuffer : nvrhi::ResourceStates::VertexBuffer)
					.setKeepInitialState(true)
					.setDebugName(std::format("{} ({})", debugName, stream)));
		};
		GpuMesh result;
		result.Positions = makeBuffer(vertexCount * sizeof(glm::vec3), false, "positions");
		result.Attributes = makeBuffer(vertexCount * sizeof(Gpu::VertexAttributes), false, "attributes");
		result.Indices = makeBuffer(mesh.Indices.size() * sizeof(uint32_t), true, "indices");
		if (!result.Positions || !result.Attributes || !result.Indices)
		{
			return Error::Format("NVRHI could not create the buffers of '{}' (see the log)", debugName);
		}
		result.Submeshes = mesh.Submeshes;
		result.Materials = mesh.Materials;
		result.Bounds = mesh.Bounds;
		result.VertexCount = static_cast<uint32_t>(vertexCount);
		result.IndexCount = static_cast<uint32_t>(mesh.Indices.size());
		Result<void> uploaded = Upload([&](nvrhi::ICommandList& commandList) -> Result<void> {
			commandList.writeBuffer(result.Positions, mesh.Positions.data(), vertexCount * sizeof(glm::vec3));
			commandList.writeBuffer(result.Attributes, attributes.data(), vertexCount * sizeof(Gpu::VertexAttributes));
			commandList.writeBuffer(result.Indices, mesh.Indices.data(), mesh.Indices.size() * sizeof(uint32_t));
			return {};
		});
		if (!uploaded)
		{
			return AddSource(debugName, uploaded.GetError());
		}
		return result;
	}

	const GpuMesh* RenderResources::GetMesh(AssetHandle handle)
	{
		auto& entry = m_Meshes[handle];
		if (entry.CheckedFrame == m_Frame)
		{
			return entry.Object.get();
		}
		entry.CheckedFrame = m_Frame;
		const Ref<MeshSource> mesh = LoadTyped<MeshSource>(handle);
		if (mesh == nullptr)
		{
			entry.Object.reset();
			entry.Source.reset();
			return nullptr;
		}
		if (entry.Source != mesh)
		{
			// Another object for the handle: made (and, failing, reported) anew.
			m_Reported.erase(handle);
			entry.Source = mesh;
			entry.Object.reset();
			Result<GpuMesh> created = CreateMesh(mesh->GetData(), mesh->GetPath());
			if (!created)
			{
				ReportOnce(handle, created.GetError().Message);
				return nullptr;
			}
			entry.Object = CreateScope<GpuMesh>(std::move(*created));
		}
		return entry.Object.get();
	}

	Ref<Model> RenderResources::GetModel(AssetHandle handle)
	{
		ModelEntry& entry = m_Models[handle];
		if (entry.CheckedFrame != m_Frame)
		{
			entry.CheckedFrame = m_Frame;
			Ref<Model> model = LoadTyped<Model>(handle);
			if (model != nullptr && model != entry.Source)
			{
				// A model has no GPU copy that could fail: once it loads, a later failure is news again.
				m_Reported.erase(handle);
			}
			entry.Source = std::move(model);
		}
		return entry.Source;
	}

	const GpuTexture& RenderResources::GetTexture(AssetHandle handle, DefaultTexture fallback)
	{
		const AssetHandle fallbackHandle = GetDefaultTextureHandle(fallback);
		if (!handle.IsValid())
		{
			handle = fallbackHandle;
		}
		auto& entry = m_Textures[handle];
		if (entry.CheckedFrame != m_Frame)
		{
			entry.CheckedFrame = m_Frame;
			const Ref<Texture2D> texture = LoadTyped<Texture2D>(handle);
			if (texture == nullptr)
			{
				entry.Object.reset();
				entry.Source.reset();
			}
			else if (entry.Source != texture)
			{
				m_Reported.erase(handle);
				entry.Source = texture;
				entry.Object.reset();
				Result<GpuTexture> created = CreateTexture(*texture, texture->GetPath());
				if (created)
				{
					entry.Object = CreateScope<GpuTexture>(std::move(*created));
				}
				else
				{
					ReportOnce(handle, created.GetError().Message);
				}
			}
		}
		if (entry.Object != nullptr)
		{
			return *entry.Object;
		}
		GB_CORE_VERIFY(handle != fallbackHandle, "the default texture {} cannot be created", ToString(fallback));
		return GetTexture(fallbackHandle, fallback);
	}

	std::array<const GpuTexture*, Gpu::MaterialTextureCount>
	RenderResources::GetMaterialTextures(const MaterialData& data)
	{
		const std::array<AssetHandle, Gpu::MaterialTextureCount> handles = GetTextureHandles(data);
		std::array<const GpuTexture*, Gpu::MaterialTextureCount> textures{};
		for (size_t index = 0; index < handles.size(); ++index)
		{
			textures[index] = &GetTexture(handles[index], MaterialTextureFallbacks[index]);
		}
		return textures;
	}

	Result<Scope<GpuMaterial>> RenderResources::CreateMaterial(
		const MaterialData& data, const std::array<const GpuTexture*, Gpu::MaterialTextureCount>& textures)
	{
		nvrhi::IDevice& device = *m_Device.GetNvrhiDevice();
		auto material = CreateScope<GpuMaterial>();
		material->Alpha = data.Alpha;
		material->DoubleSided = data.DoubleSided;
		material->Constants = device.createBuffer(nvrhi::BufferDesc()
		                                              .setByteSize(sizeof(Gpu::MaterialConstants))
		                                              .setIsConstantBuffer(true)
		                                              .setInitialState(nvrhi::ResourceStates::ConstantBuffer)
		                                              .setKeepInitialState(true)
		                                              .setDebugName("Material constants"));
		if (!material->Constants)
		{
			return Error("NVRHI could not create a material constant buffer (see the log)");
		}
		nvrhi::BindingSetDesc desc;
		desc.addItem(nvrhi::BindingSetItem::ConstantBuffer(Gpu::MaterialConstantsSlot, material->Constants));
		for (uint32_t index = 0; index < Gpu::MaterialTextureCount; ++index)
		{
			desc.addItem(nvrhi::BindingSetItem::Texture_SRV(Gpu::MaterialTextureSlot(index), textures[index]->Texture));
			desc.addItem(nvrhi::BindingSetItem::Sampler(Gpu::MaterialSamplerSlot(index), textures[index]->Sampler));
		}
		material->BindingSet = device.createBindingSet(desc, m_MaterialLayout);
		if (!material->BindingSet)
		{
			return Error("NVRHI could not create a material binding set (see the log)");
		}
		const Gpu::MaterialConstants constants = MakeConstants(data);
		Result<void> uploaded = Upload([&](nvrhi::ICommandList& commandList) -> Result<void> {
			commandList.writeBuffer(material->Constants, &constants, sizeof(constants));
			return {};
		});
		if (!uploaded)
		{
			return uploaded.GetError();
		}
		return material;
	}

	const GpuMaterial& RenderResources::GetMaterial(AssetHandle handle)
	{
		if (!handle.IsValid())
		{
			handle = Material::DefaultHandle;
		}
		MaterialEntry& entry = m_Materials[handle];
		if (entry.CheckedFrame != m_Frame)
		{
			entry.CheckedFrame = m_Frame;
			const Ref<Material> material = LoadTyped<Material>(handle);
			if (material == nullptr)
			{
				entry.Object.reset();
				entry.Source.reset();
			}
			else
			{
				const auto textures = GetMaterialTextures(material->GetData());
				std::array<nvrhi::TextureHandle, Gpu::MaterialTextureCount> bound{};
				std::ranges::transform(
					textures, bound.begin(), [](const GpuTexture* texture) { return texture->Texture; });
				const bool changed = entry.Source != material || entry.Version != material->GetVersion();
				if (changed || entry.Textures != bound)
				{
					if (changed)
					{
						m_Reported.erase(handle);
					}
					entry.Source = material;
					entry.Version = material->GetVersion();
					entry.Textures = bound;
					entry.Object.reset();
					Result<Scope<GpuMaterial>> created = CreateMaterial(material->GetData(), textures);
					if (created)
					{
						entry.Object = std::move(*created);
					}
					else
					{
						ReportOnce(handle, created.GetError().Message);
					}
				}
			}
		}
		if (entry.Object != nullptr)
		{
			return *entry.Object;
		}
		GB_CORE_VERIFY(handle != Material::DefaultHandle, "the default material cannot be created");
		return GetMaterial(Material::DefaultHandle);
	}
}
