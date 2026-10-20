#pragma once

#include "Gambit/Math/Math.h"
#include "Gambit/Renderer/GpuTypes.h"
#include "Gambit/Renderer/RenderResources.h"

#include <nvrhi/nvrhi.h>

#include <cstdint>
#include <vector>

namespace Gambit
{
	// One submesh to draw with one material and transform: what the scene passes consume.
	struct DrawCommand
	{
		const GpuMesh* Mesh = nullptr;
		uint32_t SubmeshIndex = 0;
		const GpuMaterial* Material = nullptr;
		AssetHandle MeshHandle; // sort keys: the assets behind Mesh and Material
		AssetHandle MaterialHandle;
		Gpu::DrawConstants Constants;
		bool Mirrored = false; // the transform flips the winding (negative determinant)
		float Distance = 0.0f; // from the eye to the center of the world bounds
		uint32_t Order = 0;    // position in the scene, the last tie breaker of sorting
	};

	// The draws of one view, sorted by the SceneRenderer: opaque and alpha-masked ones grouped by pipeline, material
	// and mesh (fewest state changes), blended ones back to front.
	struct SceneDrawList
	{
		std::vector<DrawCommand> Opaque;
		std::vector<DrawCommand> Transparent;

		void Clear()
		{
			Opaque.clear();
			Transparent.clear();
		}
	};

	// The render targets of a view of one size: HDR scene color and reversed depth (0 is the far plane).
	struct ViewTargets
	{
		static constexpr nvrhi::Format SceneColorFormat = nvrhi::Format::RGBA16_FLOAT;
		static constexpr nvrhi::Format DepthFormat = nvrhi::Format::D32;

		glm::uvec2 Size{ 0 };
		nvrhi::TextureHandle SceneColor;
		nvrhi::TextureHandle Depth;
		nvrhi::FramebufferHandle DepthFramebuffer; // depth only
		nvrhi::FramebufferHandle SceneFramebuffer; // scene color and depth
	};

	// What every scene pass of a view reads: the command list being recorded, the view's targets and draws, and the
	// binding set of descriptor set 0 (frame constants, lights, draw push constants).
	struct SceneFrame
	{
		nvrhi::ICommandList& CommandList;
		const ViewTargets& Targets;
		const SceneDrawList& Draws;
		nvrhi::IBindingSet* FrameBindingSet = nullptr;
		glm::vec4 ClearColor{ 0.0f, 0.0f, 0.0f, 1.0f };
	};
}
