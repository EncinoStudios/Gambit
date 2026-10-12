#pragma once

#include "Gambit/Core/Result.h"

#include <nvrhi/nvrhi.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string_view>

namespace Gambit
{
	class GraphicsDevice;

	// Helpers for the binding convention of D-031 and the pipelines most passes need. The functions that create objects
	// turn NVRHI's null results (whose reason NVRHI logs) into errors.

	// A binding layout for GLSL descriptor set `descriptorSet`: NVRHI's per-type Vulkan binding offsets are all zero,
	// so an item's slot is its GLSL `binding` number, which must therefore be unique within the set across resource
	// types (D-031). All layouts of a pipeline must use this convention.
	[[nodiscard]] nvrhi::BindingLayoutDesc MakeBindingLayoutDesc(
		nvrhi::ShaderType visibility, uint32_t descriptorSet, std::initializer_list<nvrhi::BindingLayoutItem> items);

	[[nodiscard]] Result<nvrhi::BindingLayoutHandle>
	CreateBindingLayout(GraphicsDevice& device, const nvrhi::BindingLayoutDesc& desc);

	[[nodiscard]] Result<nvrhi::BindingSetHandle> CreateBindingSet(
		GraphicsDevice& device, nvrhi::IBindingLayout* layout, std::initializer_list<nvrhi::BindingSetItem> items);

	// A compute pipeline from the embedded shader `shaderName` ("Blur.comp") and the layouts of its descriptor sets.
	[[nodiscard]] Result<nvrhi::ComputePipelineHandle> CreateComputePipeline(
		GraphicsDevice& device, std::string_view shaderName,
		std::span<const nvrhi::BindingLayoutHandle> bindingLayouts);

	// A pipeline for a fullscreen pass: the shared FullscreenTriangle.vert plus `pixelShaderName`, no depth test or
	// write, no culling, rendering to framebuffers like `framebuffer`. The pixel shader reads the UV at location 0.
	[[nodiscard]] Result<nvrhi::GraphicsPipelineHandle> CreateFullscreenPipeline(
		GraphicsDevice& device, std::string_view pixelShaderName, const nvrhi::FramebufferInfo& framebuffer,
		std::span<const nvrhi::BindingLayoutHandle> bindingLayouts,
		const nvrhi::BlendState::RenderTarget& blend = nvrhi::BlendState::RenderTarget());

	// A GraphicsState that renders with `pipeline` into the whole of `framebuffer` (one viewport and scissor
	// rectangle). Add binding sets and vertex buffers as needed.
	[[nodiscard]] nvrhi::GraphicsState
	MakeGraphicsState(nvrhi::IGraphicsPipeline* pipeline, nvrhi::IFramebuffer* framebuffer);

	// Sets `state`, then the push constants (when any) and draws the three vertices of FullscreenTriangle.vert.
	void DrawFullscreenTriangle(
		nvrhi::ICommandList& commandList, const nvrhi::GraphicsState& state,
		std::span<const std::byte> pushConstants = {});
}
