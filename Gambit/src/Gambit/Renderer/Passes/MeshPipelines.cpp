#include "gbpch.h"

#include "Gambit/Renderer/Passes/MeshPipelines.h"

#include "Gambit/RHI/GraphicsDevice.h"
#include "Gambit/RHI/ShaderCache.h"

namespace Gambit
{
	namespace
	{
		std::string_view ToString(MeshPass pass)
		{
			switch (pass)
			{
				case MeshPass::DepthOpaque:
					return "DepthOpaque";
				case MeshPass::DepthMasked:
					return "DepthMasked";
				case MeshPass::ForwardOpaque:
					return "ForwardOpaque";
				case MeshPass::ForwardBlend:
					return "ForwardBlend";
			}
			return "Unknown";
		}

		nvrhi::VertexAttributeDesc
		Attribute(const char* name, nvrhi::Format format, uint32_t buffer, uint32_t offset, uint32_t stride)
		{
			return nvrhi::VertexAttributeDesc()
				.setName(name)
				.setFormat(format)
				.setBufferIndex(buffer)
				.setOffset(offset)
				.setElementStride(stride);
		}
	}

	MeshPipelines::MeshPipelines(
		GraphicsDevice& device, nvrhi::IBindingLayout* frameLayout, nvrhi::IBindingLayout* materialLayout)
		: m_Device(device), m_FrameLayout(frameLayout), m_MaterialLayout(materialLayout)
	{
		// NVRHI numbers the locations in the order of the attributes. The forward pass reads every attribute: 0
		// position (stream 0), then those of stream 1 (Gpu::VertexAttributes). The depth prepass declares only what it
		// reads (position, UV0 and color at locations 0..2): attributes a shader does not read cost bandwidth, and the
		// validation layer warns about them.
		constexpr uint32_t Stride = sizeof(Gpu::VertexAttributes);
		const nvrhi::VertexAttributeDesc position =
			Attribute("POSITION", nvrhi::Format::RGB32_FLOAT, 0, 0, sizeof(glm::vec3));
		const nvrhi::VertexAttributeDesc uv0 =
			Attribute("TEXCOORD0", nvrhi::Format::RG32_FLOAT, 1, offsetof(Gpu::VertexAttributes, UV0), Stride);
		const nvrhi::VertexAttributeDesc color =
			Attribute("COLOR", nvrhi::Format::RGBA32_FLOAT, 1, offsetof(Gpu::VertexAttributes, Color), Stride);
		const std::array forwardAttributes = {
			position,
			Attribute("NORMAL", nvrhi::Format::RGB32_FLOAT, 1, offsetof(Gpu::VertexAttributes, Normal), Stride),
			Attribute("TANGENT", nvrhi::Format::RGBA32_FLOAT, 1, offsetof(Gpu::VertexAttributes, Tangent), Stride),
			uv0,
			Attribute("TEXCOORD1", nvrhi::Format::RG32_FLOAT, 1, offsetof(Gpu::VertexAttributes, UV1), Stride),
			color,
		};
		const std::array depthAttributes = { position, uv0, color };
		m_ForwardInputLayout = CreateInputLayout(forwardAttributes, "PBR.vert");
		m_DepthInputLayout = CreateInputLayout(depthAttributes, "DepthPrepass.vert");
	}

	nvrhi::InputLayoutHandle
	MeshPipelines::CreateInputLayout(std::span<const nvrhi::VertexAttributeDesc> attributes, std::string_view shader)
	{
		Result<nvrhi::ShaderHandle> vertexShader = m_Device.GetShaderCache().Get(shader);
		GB_CORE_VERIFY(vertexShader.HasValue(), "{}", vertexShader.GetError().Message);
		nvrhi::InputLayoutHandle layout = m_Device.GetNvrhiDevice()->createInputLayout(
			attributes.data(), static_cast<uint32_t>(attributes.size()), *vertexShader);
		GB_CORE_VERIFY(layout != nullptr, "NVRHI could not create the input layout of {}", shader);
		return layout;
	}

	MeshPass MeshPipelines::GetForwardPass(AlphaMode alpha) noexcept
	{
		return alpha == AlphaMode::Blend ? MeshPass::ForwardBlend : MeshPass::ForwardOpaque;
	}

	MeshPass MeshPipelines::GetDepthPass(AlphaMode alpha) noexcept
	{
		return alpha == AlphaMode::Mask ? MeshPass::DepthMasked : MeshPass::DepthOpaque;
	}

	Result<nvrhi::IGraphicsPipeline*>
	MeshPipelines::Get(MeshPass pass, bool doubleSided, bool mirrored, const nvrhi::FramebufferInfo& framebuffer)
	{
		CachedPipeline& cached = m_Pipelines[std::make_tuple(pass, doubleSided, mirrored)];
		if (cached.Pipeline)
		{
			if (cached.Framebuffer != framebuffer)
			{
				return Error::Format(
					"the {} mesh pipeline was made for the view targets' formats, not for this framebuffer",
					ToString(pass));
			}
			return cached.Pipeline.Get();
		}
		const bool depthOnly = pass == MeshPass::DepthOpaque || pass == MeshPass::DepthMasked;
		ShaderCache& shaders = m_Device.GetShaderCache();
		Result<nvrhi::ShaderHandle> vertexShader = shaders.Get(depthOnly ? "DepthPrepass.vert" : "PBR.vert");
		if (!vertexShader)
		{
			return vertexShader.GetError();
		}
		nvrhi::GraphicsPipelineDesc desc;
		desc.setPrimType(nvrhi::PrimitiveType::TriangleList)
			.setInputLayout(depthOnly ? m_DepthInputLayout : m_ForwardInputLayout)
			.setVertexShader(*vertexShader)
			.addBindingLayout(m_FrameLayout)
			.addBindingLayout(m_MaterialLayout);
		if (pass != MeshPass::DepthOpaque)
		{
			Result<nvrhi::ShaderHandle> pixelShader = shaders.Get(depthOnly ? "DepthPrepass.frag" : "PBR.frag");
			if (!pixelShader)
			{
				return pixelShader.GetError();
			}
			desc.setPixelShader(*pixelShader);
		}

		nvrhi::RenderState& state = desc.renderState;
		// glTF triangles are counter-clockwise; a mirrored transform turns them clockwise.
		state.rasterState.setCullMode(doubleSided ? nvrhi::RasterCullMode::None : nvrhi::RasterCullMode::Back)
			.setFrontCounterClockwise(!mirrored)
			.enableDepthClip();
		// Reversed depth: nearer is greater. The forward pass tests against the prepass's depth (equal passes).
		state.depthStencilState.enableDepthTest().disableStencil();
		switch (pass)
		{
			case MeshPass::DepthOpaque:
			case MeshPass::DepthMasked:
				state.depthStencilState.enableDepthWrite().setDepthFunc(nvrhi::ComparisonFunc::Greater);
				break;
			case MeshPass::ForwardOpaque:
				state.depthStencilState.disableDepthWrite().setDepthFunc(nvrhi::ComparisonFunc::GreaterOrEqual);
				break;
			case MeshPass::ForwardBlend:
				state.depthStencilState.disableDepthWrite().setDepthFunc(nvrhi::ComparisonFunc::Greater);
				state.blendState.targets[0]
					.enableBlend()
					.setSrcBlend(nvrhi::BlendFactor::SrcAlpha)
					.setDestBlend(nvrhi::BlendFactor::InvSrcAlpha)
					.setSrcBlendAlpha(nvrhi::BlendFactor::One)
					.setDestBlendAlpha(nvrhi::BlendFactor::InvSrcAlpha);
				break;
		}
		cached.Pipeline = m_Device.GetNvrhiDevice()->createGraphicsPipeline(desc, framebuffer);
		if (!cached.Pipeline)
		{
			return Error::Format("NVRHI could not create the {} mesh pipeline (see the log)", ToString(pass));
		}
		cached.Framebuffer = framebuffer;
		return cached.Pipeline.Get();
	}

	Result<void> MeshPipelines::MakeState(
		nvrhi::GraphicsState& state, MeshPass pass, const DrawCommand& draw, nvrhi::IFramebuffer* framebuffer,
		nvrhi::IBindingSet* frameBindingSet)
	{
		Result<nvrhi::IGraphicsPipeline*> pipeline =
			Get(pass, draw.Material->DoubleSided, draw.Mirrored, framebuffer->getFramebufferInfo());
		if (!pipeline)
		{
			return pipeline.GetError();
		}
		const nvrhi::FramebufferInfoEx& info = framebuffer->getFramebufferInfo();
		state =
			nvrhi::GraphicsState()
				.setPipeline(*pipeline)
				.setFramebuffer(framebuffer)
				.setViewport(nvrhi::ViewportState().addViewportAndScissorRect(
					nvrhi::Viewport(static_cast<float>(info.width), static_cast<float>(info.height))))
				.addBindingSet(frameBindingSet)
				.addBindingSet(draw.Material->BindingSet)
				.addVertexBuffer(nvrhi::VertexBufferBinding().setBuffer(draw.Mesh->Positions).setSlot(0).setOffset(0))
				.addVertexBuffer(nvrhi::VertexBufferBinding().setBuffer(draw.Mesh->Attributes).setSlot(1).setOffset(0))
				.setIndexBuffer(nvrhi::IndexBufferBinding()
		                            .setBuffer(draw.Mesh->Indices)
		                            .setFormat(nvrhi::Format::R32_UINT)
		                            .setOffset(0));
		return {};
	}

	Result<uint64_t> DrawCommands(
		nvrhi::ICommandList& commandList, MeshPipelines& pipelines, std::span<const DrawCommand> draws, bool depthPass,
		nvrhi::IFramebuffer* framebuffer, nvrhi::IBindingSet* frameBindingSet)
	{
		uint64_t triangles = 0;
		std::optional<std::tuple<MeshPass, bool, bool, const GpuMaterial*, const GpuMesh*>> current;
		for (const DrawCommand& draw : draws)
		{
			const MeshPass pass = depthPass ? MeshPipelines::GetDepthPass(draw.Material->Alpha)
											: MeshPipelines::GetForwardPass(draw.Material->Alpha);
			const auto key = std::make_tuple(pass, draw.Material->DoubleSided, draw.Mirrored, draw.Material, draw.Mesh);
			if (current != key)
			{
				nvrhi::GraphicsState state;
				if (Result<void> made = pipelines.MakeState(state, pass, draw, framebuffer, frameBindingSet); !made)
				{
					return made.GetError();
				}
				commandList.setGraphicsState(state);
				current = key;
			}
			const Submesh& submesh = draw.Mesh->Submeshes[draw.SubmeshIndex];
			commandList.setPushConstants(&draw.Constants, sizeof(draw.Constants));
			commandList.drawIndexed(
				nvrhi::DrawArguments().setVertexCount(submesh.IndexCount).setStartIndexLocation(submesh.BaseIndex));
			triangles += submesh.IndexCount / 3;
		}
		return triangles;
	}
}
