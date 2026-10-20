#pragma once

#include "Gambit/Core/Result.h"
#include "Gambit/Renderer/Passes/MeshPipelines.h"
#include "Gambit/Renderer/Passes/SceneDrawList.h"

namespace Gambit
{
	// Shades the scene into the HDR scene color (D-032): clears it to the view's clear color, draws the opaque and
	// alpha-masked draws against the prepass's depth, then the blended ones back to front. Inputs: the draw lists,
	// ViewTargets::Depth from the DepthPrepass, set 0 (frame constants and lights). Output: ViewTargets::SceneColor,
	// pre-exposed linear luminance.
	class ForwardPass
	{
	public:
		explicit ForwardPass(MeshPipelines& pipelines)
			: m_Pipelines(pipelines)
		{}

		// Returns the number of triangles drawn.
		[[nodiscard]] Result<uint64_t> Execute(const SceneFrame& frame);

	private:
		MeshPipelines& m_Pipelines;
	};
}
