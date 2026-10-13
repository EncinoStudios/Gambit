#include "gbpch.h"

#include "Gambit/Renderer/Passes/DepthPrepass.h"

namespace Gambit
{
	Result<uint64_t> DepthPrepass::Execute(const SceneFrame& frame)
	{
		frame.CommandList.clearDepthStencilTexture(frame.Targets.Depth, nvrhi::AllSubresources, true, 0.0f, false, 0);
		return DrawCommands(
			frame.CommandList, m_Pipelines, frame.Draws.Opaque, true, frame.Targets.DepthFramebuffer,
			frame.FrameBindingSet);
	}
}
