#pragma once

#include "Gambit/Core/Result.h"
#include "Gambit/Math/Math.h"
#include "Gambit/Renderer/DebugDraw.h"

#include <nvrhi/nvrhi.h>

#include <span>
#include <tuple>
#include <vector>

namespace Gambit
{
	class GraphicsDevice;

	// Draws debug lines over the tonemapped image (D-032 puts overlays after tonemapping): depth-tested lines against
	// the view's depth (reversed), then the others on top. Inputs: the lines, the view-projection matrix, the view's
	// depth as the framebuffer's depth attachment. Output: the framebuffer's color attachment (colors are linear,
	// sRGB-encoded for UNORM targets).
	class DebugLinePass
	{
	public:
		explicit DebugLinePass(GraphicsDevice& device);

		[[nodiscard]] Result<void> Execute(
			nvrhi::ICommandList& commandList, std::span<const DebugLine> lines, const glm::mat4& viewProjection,
			nvrhi::IFramebuffer* output);

	private:
		[[nodiscard]] Result<nvrhi::IGraphicsPipeline*>
		GetPipeline(const nvrhi::FramebufferInfo& info, bool depthTested);

		GraphicsDevice& m_Device;
		nvrhi::BindingLayoutHandle m_Layout;
		nvrhi::BindingSetHandle m_BindingSet;
		nvrhi::InputLayoutHandle m_InputLayout;
		nvrhi::BufferHandle m_Vertices;
		size_t m_VertexCapacity = 0;
		// One pipeline per kind of framebuffer and depth testing.
		std::vector<std::tuple<nvrhi::FramebufferInfo, bool, nvrhi::GraphicsPipelineHandle>> m_Pipelines;
	};
}
