#include "gbpch.h"

#include "Gambit/Renderer/Passes/ForwardPass.h"

namespace Gambit
{
	Result<uint64_t> ForwardPass::Execute(const SceneFrame& frame)
	{
		const glm::vec4& clear = frame.ClearColor;
		frame.CommandList.clearTextureFloat(
			frame.Targets.SceneColor, nvrhi::AllSubresources, nvrhi::Color(clear.r, clear.g, clear.b, clear.a));
		Result<uint64_t> opaque = DrawCommands(
			frame.CommandList, m_Pipelines, frame.Draws.Opaque, false, frame.Targets.SceneFramebuffer,
			frame.FrameBindingSet);
		if (!opaque)
		{
			return opaque;
		}
		Result<uint64_t> transparent = DrawCommands(
			frame.CommandList, m_Pipelines, frame.Draws.Transparent, false, frame.Targets.SceneFramebuffer,
			frame.FrameBindingSet);
		if (!transparent)
		{
			return transparent;
		}
		return *opaque + *transparent;
	}
}
