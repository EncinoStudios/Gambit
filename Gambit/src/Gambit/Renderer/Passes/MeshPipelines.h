#pragma once

#include "Gambit/Core/Result.h"
#include "Gambit/Renderer/Material.h"
#include "Gambit/Renderer/Passes/SceneDrawList.h"

#include <nvrhi/nvrhi.h>

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <tuple>

namespace Gambit
{
	class GraphicsDevice;

	// What a mesh pipeline does.
	enum class MeshPass : uint8_t
	{
		DepthOpaque,   // depth only, no pixel shader
		DepthMasked,   // depth with the alpha test
		ForwardOpaque, // shading of opaque and alpha-masked materials over the prepass's depth
		ForwardBlend   // shading of blended materials, alpha blended, depth tested but not written
	};

	// The graphics pipelines of the scene passes: every combination of pass, culling (double-sided materials draw back
	// faces) and winding (mirrored transforms) is made on first use, for the view targets' fixed formats (asking for
	// one with another kind of framebuffer is an error). Meshes are
	// counter-clockwise (glTF); every pipeline uses the binding layouts of set 0 (frame) and set 1 (material) and the
	// vertex streams of GpuMesh.
	class MeshPipelines
	{
	public:
		MeshPipelines(
			GraphicsDevice& device, nvrhi::IBindingLayout* frameLayout, nvrhi::IBindingLayout* materialLayout);

		[[nodiscard]] Result<nvrhi::IGraphicsPipeline*>
		Get(MeshPass pass, bool doubleSided, bool mirrored, const nvrhi::FramebufferInfo& framebuffer);

		// The pass of a material in the forward pass and in the depth prepass (blended materials have none).
		[[nodiscard]] static MeshPass GetForwardPass(AlphaMode alpha) noexcept;
		[[nodiscard]] static MeshPass GetDepthPass(AlphaMode alpha) noexcept;

		// Sets `state` up for one draw command: pipeline, vertex and index buffers, binding sets.
		[[nodiscard]] Result<void> MakeState(
			nvrhi::GraphicsState& state, MeshPass pass, const DrawCommand& draw, nvrhi::IFramebuffer* framebuffer,
			nvrhi::IBindingSet* frameBindingSet);

	private:
		[[nodiscard]] nvrhi::InputLayoutHandle
		CreateInputLayout(std::span<const nvrhi::VertexAttributeDesc> attributes, std::string_view shader);

		GraphicsDevice& m_Device;
		nvrhi::BindingLayoutHandle m_FrameLayout;
		nvrhi::BindingLayoutHandle m_MaterialLayout;
		nvrhi::InputLayoutHandle m_ForwardInputLayout;
		nvrhi::InputLayoutHandle m_DepthInputLayout;
		struct CachedPipeline
		{
			nvrhi::FramebufferInfo Framebuffer; // what it was made for
			nvrhi::GraphicsPipelineHandle Pipeline;
		};
		std::map<std::tuple<MeshPass, bool, bool>, CachedPipeline> m_Pipelines;
	};

	// Records draw commands: sets the state only when the pipeline, the material or the mesh changes, then the push
	// constants and the indexed draw. Returns the number of triangles drawn.
	[[nodiscard]] Result<uint64_t> DrawCommands(
		nvrhi::ICommandList& commandList, MeshPipelines& pipelines, std::span<const DrawCommand> draws, bool depthPass,
		nvrhi::IFramebuffer* framebuffer, nvrhi::IBindingSet* frameBindingSet);
}
