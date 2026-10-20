#pragma once

#include "Gambit/Core/Result.h"

#include <nvrhi/nvrhi.h>

#include <utility>
#include <vector>

namespace Gambit
{
	class GraphicsDevice;

	// The temporary output pass (until the HDR post chain of T09): maps the pre-exposed HDR scene color through the
	// fitted ACES curve into the output framebuffer, sRGB-encoding the values for UNORM outputs (sRGB formats encode in
	// hardware, float formats take linear values). Input: the scene color texture. Output: the framebuffer's only
	// color attachment, of the same size (a depth attachment is ignored).
	class TonemapPass
	{
	public:
		explicit TonemapPass(GraphicsDevice& device);

		[[nodiscard]] Result<void>
		Execute(nvrhi::ICommandList& commandList, nvrhi::ITexture* sceneColor, nvrhi::IFramebuffer* output);

		// Whether the shaders must sRGB-encode linear colors themselves for a target of this format.
		[[nodiscard]] static bool NeedsSrgbEncoding(nvrhi::Format format) noexcept;

	private:
		[[nodiscard]] Result<nvrhi::IGraphicsPipeline*> GetPipeline(const nvrhi::FramebufferInfo& info);

		GraphicsDevice& m_Device;
		nvrhi::BindingLayoutHandle m_Layout;
		// One pipeline per kind of framebuffer (formats of the color and depth attachments).
		std::vector<std::pair<nvrhi::FramebufferInfo, nvrhi::GraphicsPipelineHandle>> m_Pipelines;
		// The binding set of the scene color texture it was made for (which it keeps alive).
		nvrhi::BindingSetHandle m_BindingSet;
		nvrhi::ITexture* m_BoundSceneColor = nullptr;
	};
}
