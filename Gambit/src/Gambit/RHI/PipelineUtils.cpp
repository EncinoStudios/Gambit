#include "gbpch.h"

#include "Gambit/RHI/PipelineUtils.h"

#include "Gambit/RHI/GraphicsDevice.h"
#include "Gambit/RHI/ShaderCache.h"

namespace Gambit
{
	nvrhi::BindingLayoutDesc MakeBindingLayoutDesc(
		nvrhi::ShaderType visibility, uint32_t descriptorSet, std::initializer_list<nvrhi::BindingLayoutItem> items)
	{
		nvrhi::BindingLayoutDesc desc;
		desc.setVisibility(visibility)
			.setRegisterSpaceAndDescriptorSet(descriptorSet)
			.setBindingOffsets(nvrhi::VulkanBindingOffsets()
		                           .setShaderResourceOffset(0)
		                           .setSamplerOffset(0)
		                           .setConstantBufferOffset(0)
		                           .setUnorderedAccessViewOffset(0));
		for (const nvrhi::BindingLayoutItem& item : items)
		{
			desc.addItem(item);
		}
		return desc;
	}

	Result<nvrhi::BindingLayoutHandle> CreateBindingLayout(GraphicsDevice& device, const nvrhi::BindingLayoutDesc& desc)
	{
		nvrhi::BindingLayoutHandle layout = device.GetNvrhiDevice()->createBindingLayout(desc);
		if (!layout)
		{
			return Error(
				std::format("NVRHI could not create a binding layout for set {} (see the log)", desc.registerSpace));
		}
		return layout;
	}

	Result<nvrhi::BindingSetHandle> CreateBindingSet(
		GraphicsDevice& device, nvrhi::IBindingLayout* layout, std::initializer_list<nvrhi::BindingSetItem> items)
	{
		if (layout == nullptr)
		{
			return Error("cannot create a binding set without a binding layout");
		}
		nvrhi::BindingSetDesc desc;
		for (const nvrhi::BindingSetItem& item : items)
		{
			desc.addItem(item);
		}
		nvrhi::BindingSetHandle set = device.GetNvrhiDevice()->createBindingSet(desc, layout);
		if (!set)
		{
			return Error("NVRHI could not create a binding set (see the log)");
		}
		return set;
	}

	Result<nvrhi::ComputePipelineHandle> CreateComputePipeline(
		GraphicsDevice& device, std::string_view shaderName, std::span<const nvrhi::BindingLayoutHandle> bindingLayouts)
	{
		Result<nvrhi::ShaderHandle> shader = device.GetShaderCache().Get(shaderName, nvrhi::ShaderType::Compute);
		if (!shader)
		{
			return shader.GetError();
		}
		nvrhi::ComputePipelineDesc desc;
		desc.setComputeShader(*shader);
		for (const nvrhi::BindingLayoutHandle& layout : bindingLayouts)
		{
			desc.addBindingLayout(layout);
		}
		nvrhi::ComputePipelineHandle pipeline = device.GetNvrhiDevice()->createComputePipeline(desc);
		if (!pipeline)
		{
			return Error(std::format("NVRHI could not create the compute pipeline of '{}' (see the log)", shaderName));
		}
		return pipeline;
	}

	Result<nvrhi::GraphicsPipelineHandle> CreateFullscreenPipeline(
		GraphicsDevice& device, std::string_view pixelShaderName, const nvrhi::FramebufferInfo& framebuffer,
		std::span<const nvrhi::BindingLayoutHandle> bindingLayouts, const nvrhi::BlendState::RenderTarget& blend)
	{
		ShaderCache& shaders = device.GetShaderCache();
		Result<nvrhi::ShaderHandle> vertexShader = shaders.Get("FullscreenTriangle.vert", nvrhi::ShaderType::Vertex);
		if (!vertexShader)
		{
			return vertexShader.GetError();
		}
		Result<nvrhi::ShaderHandle> pixelShader = shaders.Get(pixelShaderName, nvrhi::ShaderType::Pixel);
		if (!pixelShader)
		{
			return pixelShader.GetError();
		}

		nvrhi::RenderState renderState;
		renderState.rasterState.setCullNone();
		renderState.depthStencilState.disableDepthTest().disableDepthWrite().disableStencil();
		renderState.blendState.setRenderTarget(0, blend);
		nvrhi::GraphicsPipelineDesc desc;
		desc.setPrimType(nvrhi::PrimitiveType::TriangleList)
			.setVertexShader(*vertexShader)
			.setPixelShader(*pixelShader)
			.setRenderState(renderState);
		for (const nvrhi::BindingLayoutHandle& layout : bindingLayouts)
		{
			desc.addBindingLayout(layout);
		}
		nvrhi::GraphicsPipelineHandle pipeline = device.GetNvrhiDevice()->createGraphicsPipeline(desc, framebuffer);
		if (!pipeline)
		{
			return Error(
				std::format("NVRHI could not create the fullscreen pipeline of '{}' (see the log)", pixelShaderName));
		}
		return pipeline;
	}

	nvrhi::GraphicsState MakeGraphicsState(nvrhi::IGraphicsPipeline* pipeline, nvrhi::IFramebuffer* framebuffer)
	{
		GB_CORE_ASSERT(framebuffer != nullptr, "MakeGraphicsState needs a framebuffer");
		const nvrhi::FramebufferInfoEx& info = framebuffer->getFramebufferInfo();
		return nvrhi::GraphicsState()
			.setPipeline(pipeline)
			.setFramebuffer(framebuffer)
			.setViewport(nvrhi::ViewportState().addViewportAndScissorRect(
				nvrhi::Viewport(static_cast<float>(info.width), static_cast<float>(info.height))));
	}

	void DrawFullscreenTriangle(
		nvrhi::ICommandList& commandList, const nvrhi::GraphicsState& state, std::span<const std::byte> pushConstants)
	{
		commandList.setGraphicsState(state);
		if (!pushConstants.empty())
		{
			commandList.setPushConstants(pushConstants.data(), pushConstants.size());
		}
		commandList.draw(nvrhi::DrawArguments().setVertexCount(3));
	}
}
