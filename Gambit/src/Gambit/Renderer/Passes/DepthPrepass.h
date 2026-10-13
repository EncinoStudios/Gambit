#pragma once

#include "Gambit/Core/Result.h"
#include "Gambit/Renderer/Passes/MeshPipelines.h"
#include "Gambit/Renderer/Passes/SceneDrawList.h"

namespace Gambit
{
	// Fills the view's depth buffer with the opaque and alpha-masked draws (D-032), so that the forward pass shades
	// each pixel once and later passes (SSAO, T10) have depth. Input: SceneDrawList::Opaque. Output:
	// ViewTargets::Depth, cleared to 0 (reversed depth: the far plane) first.
	class DepthPrepass
	{
	public:
		explicit DepthPrepass(MeshPipelines& pipelines)
			: m_Pipelines(pipelines)
		{}

		// Returns the number of triangles drawn.
		[[nodiscard]] Result<uint64_t> Execute(const SceneFrame& frame);

	private:
		MeshPipelines& m_Pipelines;
	};
}
