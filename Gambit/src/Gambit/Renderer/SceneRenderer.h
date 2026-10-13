#pragma once

#include "Gambit/Core/Base.h"
#include "Gambit/Core/Result.h"
#include "Gambit/Math/Bounds.h"
#include "Gambit/Math/Math.h"
#include "Gambit/Renderer/DebugDraw.h"
#include "Gambit/Renderer/GpuTypes.h"
#include "Gambit/Renderer/Passes/SceneDrawList.h"
#include "Gambit/Renderer/RenderScene.h"
#include "Gambit/Renderer/RenderView.h"

#include <nvrhi/nvrhi.h>

#include <cstdint>
#include <span>
#include <unordered_set>
#include <vector>

namespace Gambit
{
	class AssetManager;
	class DebugLinePass;
	class DepthPrepass;
	class ForwardPass;
	class GraphicsDevice;
	class MeshPipelines;
	class RenderResources;
	class TonemapPass;

	// What one SceneRenderer::Render call drew.
	struct SceneRenderStats
	{
		uint32_t DrawItems = 0;   // the scene's draw items (a model counts once)
		uint32_t DrawCalls = 0;   // submeshes drawn by the forward pass
		uint32_t CulledDraws = 0; // submeshes outside the view frustum
		uint64_t Triangles = 0;   // triangles drawn by the forward pass
		uint32_t Lights = 0;      // lights in the light buffer
		uint32_t DebugLines = 0;  // debug lines drawn (the scene's and the engine's shapes)
	};

	// The lights of a scene as the shaders read them (Gpu::Light), with physical intensities converted to
	// pre-exposed luminous quantities: lux for directional lights, candela (lumens / 4 pi) for point and spot lights.
	struct PackedLights
	{
		std::vector<Gpu::Light> Lights;
		uint32_t Culled = 0;  // point and spot lights whose range is outside the view
		uint32_t Dropped = 0; // lights beyond SceneRenderer::MaxLights
	};

	// Renders views of a RenderScene (D-032): never the World. Each Render call records and submits one command list:
	//
	//     DepthPrepass  (opaque + masked draws)            -> view depth (D32, reversed: 1 near, 0 far)
	//     ForwardPass   (opaque + masked, then blended     -> HDR scene color (RGBA16F, pre-exposed luminance)
	//                    back to front; lights, ambient)
	//     TonemapPass   (temporary until T09's post chain) -> the target's color attachment
	//     DebugLinePass (scene debug lines, engine shapes) -> the target, depth-tested against the view depth
	//
	// Draw items are interpolated between their previous and current tick with `alpha`, culled against the view
	// frustum and sorted (opaque by pipeline, material and mesh; blended back to front). The view targets are sized
	// per view and kept while views of that size are rendered, so an editor viewport and a game camera of other sizes
	// can render in the same frame. GPU copies of assets live in RenderResources. Not thread safe; release it before
	// the device.
	class SceneRenderer
	{
	public:
		static constexpr uint32_t MaxLights = 256;
		// View targets that no Render call used for this many calls are released.
		static constexpr uint64_t TargetIdleRenders = 16;

		SceneRenderer(GraphicsDevice& device, AssetManager& assets);
		~SceneRenderer();
		SceneRenderer(const SceneRenderer&) = delete;
		SceneRenderer& operator=(const SceneRenderer&) = delete;

		// Renders `view` of `scene` at the interpolation factor `alpha` (0 = the previous tick, 1 = the current one)
		// into `target`, a framebuffer with one color attachment (any 8-bit UNORM or sRGB format, or a float format)
		// and no depth attachment; the view is rendered at the target's size. The color attachment is overwritten.
		[[nodiscard]] Result<SceneRenderStats>
		Render(const RenderScene& scene, const RenderView& view, float alpha, nvrhi::IFramebuffer* target);

		[[nodiscard]] RenderResources& GetResources() noexcept { return *m_Resources; }
		[[nodiscard]] GraphicsDevice& GetDevice() const noexcept { return m_Device; }
		// The targets of the views rendered at `size` (the HDR scene color and depth of the latest one); null when no
		// view of that size was rendered recently.
		[[nodiscard]] const ViewTargets* FindViewTargets(glm::uvec2 size) const;
		// Releases every view target (they are recreated on demand).
		void ReleaseViewTargets() noexcept { m_Targets.clear(); }

		// The lights of `scene` at `alpha` for a view with this exposure, the point and spot lights with a range that
		// lies outside `frustum` left out (when one is given), at most MaxLights (directional lights first).
		[[nodiscard]] static PackedLights
		PackLights(const RenderScene& scene, float alpha, float exposure, const Frustum* frustum);
		// Appends the engine debug shapes of the scene's lights (DebugShapes::Lights) and cameras
		// (DebugShapes::Cameras, frusta for targets of `aspectRatio`, drawn up to at most 100 units) at `alpha`.
		static void AppendDebugShapes(
			const RenderScene& scene, DebugShapes shapes, float alpha, float aspectRatio, DebugDrawList& list);

	private:
		struct TargetSlot
		{
			ViewTargets Targets;
			uint64_t LastUsed = 0;
		};

		[[nodiscard]] Result<ViewTargets*> AcquireTargets(glm::uvec2 size);
		// Fills m_Draws (and m_Bounds with the world bounds of the drawn submeshes when `collectBounds`).
		void BuildDrawList(
			const RenderScene& scene, const RenderView& view, float alpha, const Frustum& frustum, bool collectBounds,
			SceneRenderStats& stats);
		void AddMesh(
			AssetHandle meshHandle, const glm::mat4& world, int32_t submeshIndex,
			std::span<const AssetHandle> materialOverrides, const RenderView& view, const Frustum& frustum,
			bool collectBounds, SceneRenderStats& stats);
		[[nodiscard]] Result<uint64_t> Record(
			const RenderView& view, const ViewTargets& targets, nvrhi::IFramebuffer* target,
			const Gpu::FrameConstants& constants, std::span<const Gpu::Light> lights);

		GraphicsDevice& m_Device;
		AssetManager& m_Assets;
		Scope<RenderResources> m_Resources;
		nvrhi::CommandListHandle m_CommandList;
		nvrhi::BindingLayoutHandle m_FrameLayout;
		nvrhi::BufferHandle m_FrameConstants;
		nvrhi::BufferHandle m_LightBuffer;
		nvrhi::BindingSetHandle m_FrameBindingSet;
		Scope<MeshPipelines> m_Pipelines;
		Scope<DepthPrepass> m_DepthPrepass;
		Scope<ForwardPass> m_ForwardPass;
		Scope<TonemapPass> m_TonemapPass;
		Scope<DebugLinePass> m_DebugLinePass;

		std::vector<TargetSlot> m_Targets;
		uint64_t m_RenderCount = 0;
		SceneDrawList m_Draws;
		std::vector<AABB> m_Bounds;
		DebugDrawList m_ShapeLines;
		std::vector<DebugLine> m_Lines;
		uint32_t m_NextOrder = 0;
		std::unordered_set<AssetHandle> m_ReportedSubmeshes; // meshes asked for a submesh they lack; lookups only
		bool m_ReportedLightOverflow = false;
	};
}
