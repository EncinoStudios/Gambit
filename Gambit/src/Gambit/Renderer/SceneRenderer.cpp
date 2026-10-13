#include "gbpch.h"

#include "Gambit/Renderer/SceneRenderer.h"

#include "Gambit/Asset/AssetManager.h"
#include "Gambit/RHI/GraphicsDevice.h"
#include "Gambit/RHI/PipelineUtils.h"
#include "Gambit/Renderer/Passes/DebugLinePass.h"
#include "Gambit/Renderer/Passes/DepthPrepass.h"
#include "Gambit/Renderer/Passes/ForwardPass.h"
#include "Gambit/Renderer/Passes/MeshPipelines.h"
#include "Gambit/Renderer/Passes/TonemapPass.h"
#include "Gambit/Renderer/RenderResources.h"

#include <numbers>

namespace Gambit
{
	namespace
	{
		// Lumens of a point or spot light per candela of luminous intensity: 4 pi steradians.
		constexpr float LumensPerCandela = 4.0f * std::numbers::pi_v<float>;
		// The farthest a debug camera frustum is drawn (a far plane of 1000 would hide everything else).
		constexpr float MaxDebugFrustumDepth = 100.0f;
		// Above this angle a spot light's cone is a hemisphere: tan() and the cone shape degenerate.
		constexpr float MaxSpotAngle = 89.0f;

		glm::vec3 GetPosition(const glm::mat4& transform)
		{
			return glm::vec3(transform[3]);
		}

		// The entity's -Z axis (lights shine and cameras look along it), normalized; -Z for degenerate transforms.
		glm::vec3 GetForward(const glm::mat4& transform)
		{
			const glm::vec3 axis(transform[2]);
			const float length = glm::length(axis);
			return length > 0.0f && std::isfinite(length) ? -axis / length : glm::vec3(0.0f, 0.0f, -1.0f);
		}

		// The largest channel scaled to 1 (debug shapes show the hue of a light, not its intensity).
		glm::vec4 DebugColorOf(const glm::vec3& color)
		{
			const float largest = std::max({ color.r, color.g, color.b });
			return largest > 0.0f ? glm::vec4(color / largest, 1.0f) : glm::vec4(1.0f);
		}

		Gpu::DrawConstants MakeDrawConstants(const glm::mat4& world, const glm::mat3& normalMatrix, float sign)
		{
			Gpu::DrawConstants constants;
			constants.Model = world;
			for (int column = 0; column < 3; ++column)
			{
				constants.NormalMatrix[column] = glm::vec4(normalMatrix[column], column == 0 ? sign : 0.0f);
			}
			return constants;
		}

		// Opaque materials before masked ones (the cheaper depth pipeline first), then by pipeline state, material
		// and mesh; the scene order breaks ties so that the result never depends on addresses.
		bool OpaqueBefore(const DrawCommand& a, const DrawCommand& b)
		{
			return std::make_tuple(
					   a.Material->Alpha, a.Material->DoubleSided, a.Mirrored, a.MaterialHandle, a.MeshHandle, a.Order)
				< std::make_tuple(
					   b.Material->Alpha, b.Material->DoubleSided, b.Mirrored, b.MaterialHandle, b.MeshHandle, b.Order);
		}

		// Farthest first (painter's order), then the scene order.
		bool TransparentBefore(const DrawCommand& a, const DrawCommand& b)
		{
			if (a.Distance != b.Distance)
			{
				return a.Distance > b.Distance;
			}
			return a.Order < b.Order;
		}

		void AppendSpotShape(const RenderSpotLight& spot, float alpha, DebugDrawList& list)
		{
			const glm::mat4 transform = spot.Transform.Interpolate(alpha);
			const glm::vec3 position = GetPosition(transform);
			const glm::vec3 forward = GetForward(transform);
			const glm::vec4 color = DebugColorOf(spot.Light.Color);
			const float length = spot.Light.Range > 0.0f ? spot.Light.Range : 1.0f;
			const float angle = glm::radians(glm::clamp(spot.Light.OuterConeAngle, 0.0f, MaxSpotAngle));
			const float radius = length * std::tan(angle);
			const glm::vec3 center = position + forward * length;
			list.DrawCircle(center, forward, radius, color);
			const glm::vec3 helper =
				std::abs(forward.y) < 0.999f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
			const glm::vec3 side = glm::normalize(glm::cross(helper, forward));
			const glm::vec3 up = glm::cross(forward, side);
			for (const glm::vec3& offset : { side, -side, up, -up })
			{
				list.DrawLine(position, center + offset * radius, color);
			}
		}
	}

	SceneRenderer::SceneRenderer(GraphicsDevice& device, AssetManager& assets)
		: m_Device(device), m_Assets(assets), m_Resources(CreateScope<RenderResources>(device, assets))
	{
		nvrhi::IDevice& nvrhiDevice = *device.GetNvrhiDevice();
		m_CommandList = nvrhiDevice.createCommandList();
		GB_CORE_VERIFY(m_CommandList != nullptr, "NVRHI could not create the scene renderer's command list");

		Result<nvrhi::BindingLayoutHandle> frameLayout = CreateBindingLayout(
			device,
			MakeBindingLayoutDesc(
				nvrhi::ShaderType::Vertex | nvrhi::ShaderType::Pixel, Gpu::FrameSet,
				{ nvrhi::BindingLayoutItem::ConstantBuffer(Gpu::FrameConstantsSlot),
		          nvrhi::BindingLayoutItem::StructuredBuffer_SRV(Gpu::LightsSlot),
		          nvrhi::BindingLayoutItem::PushConstants(Gpu::DrawConstantsSlot, sizeof(Gpu::DrawConstants)) }));
		GB_CORE_VERIFY(frameLayout.HasValue(), "{}", frameLayout.GetError().Message);
		m_FrameLayout = *frameLayout;

		m_FrameConstants = nvrhiDevice.createBuffer(nvrhi::BufferDesc()
		                                                .setByteSize(sizeof(Gpu::FrameConstants))
		                                                .setIsConstantBuffer(true)
		                                                .setInitialState(nvrhi::ResourceStates::ConstantBuffer)
		                                                .setKeepInitialState(true)
		                                                .setDebugName("Frame constants"));
		m_LightBuffer = nvrhiDevice.createBuffer(nvrhi::BufferDesc()
		                                             .setByteSize(MaxLights * sizeof(Gpu::Light))
		                                             .setStructStride(sizeof(Gpu::Light))
		                                             .setInitialState(nvrhi::ResourceStates::ShaderResource)
		                                             .setKeepInitialState(true)
		                                             .setDebugName("Lights"));
		GB_CORE_VERIFY(
			m_FrameConstants != nullptr && m_LightBuffer != nullptr,
			"NVRHI could not create the scene renderer's buffers");
		Result<nvrhi::BindingSetHandle> frameSet = CreateBindingSet(
			device, m_FrameLayout,
			{ nvrhi::BindingSetItem::ConstantBuffer(Gpu::FrameConstantsSlot, m_FrameConstants),
		      nvrhi::BindingSetItem::StructuredBuffer_SRV(Gpu::LightsSlot, m_LightBuffer),
		      nvrhi::BindingSetItem::PushConstants(Gpu::DrawConstantsSlot, sizeof(Gpu::DrawConstants)) });
		GB_CORE_VERIFY(frameSet.HasValue(), "{}", frameSet.GetError().Message);
		m_FrameBindingSet = *frameSet;

		m_Pipelines = CreateScope<MeshPipelines>(device, m_FrameLayout, m_Resources->GetMaterialBindingLayout());
		m_DepthPrepass = CreateScope<DepthPrepass>(*m_Pipelines);
		m_ForwardPass = CreateScope<ForwardPass>(*m_Pipelines);
		m_TonemapPass = CreateScope<TonemapPass>(device);
		m_DebugLinePass = CreateScope<DebugLinePass>(device);
	}

	SceneRenderer::~SceneRenderer() = default;

	const ViewTargets* SceneRenderer::FindViewTargets(glm::uvec2 size) const
	{
		const auto found =
			std::ranges::find_if(m_Targets, [size](const TargetSlot& slot) { return slot.Targets.Size == size; });
		return found != m_Targets.end() ? &found->Targets : nullptr;
	}

	Result<ViewTargets*> SceneRenderer::AcquireTargets(glm::uvec2 size)
	{
		// Targets of sizes no view uses any more go (the GPU keeps them until the frames using them are done).
		std::erase_if(m_Targets, [this, size](const TargetSlot& slot) {
			return slot.Targets.Size != size && m_RenderCount - slot.LastUsed > TargetIdleRenders;
		});
		const auto found =
			std::ranges::find_if(m_Targets, [size](const TargetSlot& slot) { return slot.Targets.Size == size; });
		if (found != m_Targets.end())
		{
			found->LastUsed = m_RenderCount;
			return &found->Targets;
		}

		nvrhi::IDevice& device = *m_Device.GetNvrhiDevice();
		ViewTargets targets;
		targets.Size = size;
		targets.SceneColor = device.createTexture(nvrhi::TextureDesc()
		                                              .setWidth(size.x)
		                                              .setHeight(size.y)
		                                              .setFormat(ViewTargets::SceneColorFormat)
		                                              .setIsRenderTarget(true)
		                                              .setInitialState(nvrhi::ResourceStates::ShaderResource)
		                                              .setKeepInitialState(true)
		                                              .setDebugName("Scene color"));
		targets.Depth = device.createTexture(nvrhi::TextureDesc()
		                                         .setWidth(size.x)
		                                         .setHeight(size.y)
		                                         .setFormat(ViewTargets::DepthFormat)
		                                         .setIsRenderTarget(true)
		                                         .setInitialState(nvrhi::ResourceStates::DepthWrite)
		                                         .setKeepInitialState(true)
		                                         .setDebugName("Scene depth"));
		if (!targets.SceneColor || !targets.Depth)
		{
			return Error::Format("NVRHI could not create the {}x{} view targets (see the log)", size.x, size.y);
		}
		targets.DepthFramebuffer = device.createFramebuffer(nvrhi::FramebufferDesc().setDepthAttachment(targets.Depth));
		targets.SceneFramebuffer = device.createFramebuffer(
			nvrhi::FramebufferDesc().addColorAttachment(targets.SceneColor).setDepthAttachment(targets.Depth));
		if (!targets.DepthFramebuffer || !targets.SceneFramebuffer)
		{
			return Error::Format("NVRHI could not create the {}x{} view framebuffers (see the log)", size.x, size.y);
		}
		m_Targets.push_back({ std::move(targets), m_RenderCount });
		return &m_Targets.back().Targets;
	}

	PackedLights
	SceneRenderer::PackLights(const RenderScene& scene, float alpha, float exposure, const Frustum* frustum)
	{
		PackedLights packed;
		const auto add = [&](const Gpu::Light& light) {
			if (packed.Lights.size() >= MaxLights)
			{
				++packed.Dropped;
				return;
			}
			packed.Lights.push_back(light);
		};
		const auto visible = [&](const glm::vec3& position, float range) {
			if (frustum != nullptr && range > 0.0f && !frustum->Intersects(position, range))
			{
				++packed.Culled;
				return false;
			}
			return true;
		};
		for (const RenderDirectionalLight& directional : scene.DirectionalLights)
		{
			const glm::mat4 transform = directional.Transform.Interpolate(alpha);
			Gpu::Light light;
			light.PositionType = glm::vec4(GetPosition(transform), static_cast<float>(Gpu::LightType::Directional));
			light.DirectionRange = glm::vec4(GetForward(transform), 0.0f);
			light.Color =
				glm::vec4(directional.Light.Color * std::max(directional.Light.Illuminance, 0.0f) * exposure, 0.0f);
			add(light);
		}
		for (const RenderSpotLight& spot : scene.SpotLights)
		{
			const glm::mat4 transform = spot.Transform.Interpolate(alpha);
			const float range = std::max(spot.Light.Range, 0.0f);
			if (!visible(GetPosition(transform), range))
			{
				continue;
			}
			const float outer = glm::radians(glm::clamp(spot.Light.OuterConeAngle, 0.0f, MaxSpotAngle));
			const float inner =
				std::min(glm::radians(glm::clamp(spot.Light.InnerConeAngle, 0.0f, MaxSpotAngle)), outer);
			const float cosOuter = std::cos(outer);
			const float cosInner = std::cos(inner);
			Gpu::Light light;
			light.PositionType = glm::vec4(GetPosition(transform), static_cast<float>(Gpu::LightType::Spot));
			light.DirectionRange = glm::vec4(GetForward(transform), range);
			light.Color = glm::vec4(
				spot.Light.Color * (std::max(spot.Light.Intensity, 0.0f) / LumensPerCandela) * exposure, 0.0f);
			light.Spot = glm::vec4(cosOuter, 1.0f / std::max(cosInner - cosOuter, 1e-4f), 0.0f, 0.0f);
			add(light);
		}
		for (const RenderPointLight& point : scene.PointLights)
		{
			const glm::mat4 transform = point.Transform.Interpolate(alpha);
			const float range = std::max(point.Light.Range, 0.0f);
			if (!visible(GetPosition(transform), range))
			{
				continue;
			}
			Gpu::Light light;
			light.PositionType = glm::vec4(GetPosition(transform), static_cast<float>(Gpu::LightType::Point));
			light.DirectionRange = glm::vec4(0.0f, 0.0f, -1.0f, range);
			light.Color = glm::vec4(
				point.Light.Color * (std::max(point.Light.Intensity, 0.0f) / LumensPerCandela) * exposure, 0.0f);
			add(light);
		}
		return packed;
	}

	void SceneRenderer::AppendDebugShapes(
		const RenderScene& scene, DebugShapes shapes, float alpha, float aspectRatio, DebugDrawList& list)
	{
		if (HasFlag(shapes, DebugShapes::Lights))
		{
			for (const RenderDirectionalLight& directional : scene.DirectionalLights)
			{
				const glm::mat4 transform = directional.Transform.Interpolate(alpha);
				const glm::vec3 position = GetPosition(transform);
				const glm::vec3 forward = GetForward(transform);
				const glm::vec4 color = DebugColorOf(directional.Light.Color);
				list.DrawCircle(position, forward, 0.25f, color);
				list.DrawLine(position, position + forward, color);
			}
			for (const RenderPointLight& point : scene.PointLights)
			{
				const glm::vec3 position = GetPosition(point.Transform.Interpolate(alpha));
				const glm::vec4 color = DebugColorOf(point.Light.Color);
				list.DrawSphere(position, 0.1f, color);
				if (point.Light.Range > 0.0f)
				{
					list.DrawSphere(position, point.Light.Range, color);
				}
			}
			for (const RenderSpotLight& spot : scene.SpotLights)
			{
				AppendSpotShape(spot, alpha, list);
			}
		}
		if (HasFlag(shapes, DebugShapes::Cameras))
		{
			for (const RenderCamera& camera : scene.Cameras)
			{
				CameraComponent shown = camera.Camera;
				shown.Far = std::min(shown.Far, shown.Near + MaxDebugFrustumDepth);
				const glm::mat4 view = glm::inverse(camera.Transform.Interpolate(alpha));
				list.DrawFrustum(MakeProjection(shown, aspectRatio) * view, glm::vec4(0.8f, 0.8f, 0.8f, 1.0f));
			}
		}
	}

	void SceneRenderer::AddMesh(
		AssetHandle meshHandle, const glm::mat4& world, int32_t submeshIndex,
		std::span<const AssetHandle> materialOverrides, const RenderView& view, const Frustum& frustum,
		bool collectBounds, SceneRenderStats& stats)
	{
		const GpuMesh* mesh = m_Resources->GetMesh(meshHandle);
		if (mesh == nullptr)
		{
			return;
		}
		const glm::mat3 linear(world);
		const float determinant = glm::determinant(linear);
		if (determinant == 0.0f || !std::isfinite(determinant))
		{
			return; // a zero scale: nothing to see
		}
		const glm::mat3 normalMatrix = glm::transpose(glm::inverse(linear));
		const float sign = determinant < 0.0f ? -1.0f : 1.0f;
		const Gpu::DrawConstants constants = MakeDrawConstants(world, normalMatrix, sign);

		uint32_t first = 0;
		auto count = static_cast<uint32_t>(mesh->Submeshes.size());
		if (submeshIndex >= 0)
		{
			if (static_cast<size_t>(submeshIndex) >= mesh->Submeshes.size())
			{
				if (m_ReportedSubmeshes.insert(meshHandle).second)
				{
					GB_CORE_WARN(
						"Renderer: mesh {} has {} submeshes; a MeshRenderer asks for submesh {}", meshHandle.ToString(),
						mesh->Submeshes.size(), submeshIndex);
				}
				return;
			}
			first = static_cast<uint32_t>(submeshIndex);
			count = 1;
		}

		for (uint32_t index = first; index < first + count; ++index)
		{
			const Submesh& submesh = mesh->Submeshes[index];
			const AABB bounds = submesh.Bounds.Transformed(world);
			if (!frustum.Intersects(bounds))
			{
				++stats.CulledDraws;
				continue;
			}
			const uint32_t slot = submesh.MaterialSlot;
			AssetHandle materialHandle = slot < materialOverrides.size() && materialOverrides[slot].IsValid()
				? materialOverrides[slot]
				: mesh->Materials[slot];
			if (!materialHandle.IsValid())
			{
				materialHandle = Material::DefaultHandle;
			}
			DrawCommand draw;
			draw.Mesh = mesh;
			draw.SubmeshIndex = index;
			draw.Material = &m_Resources->GetMaterial(materialHandle);
			draw.MeshHandle = meshHandle;
			draw.MaterialHandle = materialHandle;
			draw.Constants = constants;
			draw.Mirrored = determinant < 0.0f;
			// The view-space depth of the bounds' center: the sort distance of perspective and orthographic views.
			draw.Distance = -(view.View * glm::vec4(bounds.GetCenter(), 1.0f)).z;
			draw.Order = m_NextOrder++;
			(draw.Material->Alpha == AlphaMode::Blend ? m_Draws.Transparent : m_Draws.Opaque).push_back(draw);
			if (collectBounds)
			{
				m_Bounds.push_back(bounds);
			}
		}
	}

	void SceneRenderer::BuildDrawList(
		const RenderScene& scene, const RenderView& view, float alpha, const Frustum& frustum, bool collectBounds,
		SceneRenderStats& stats)
	{
		m_Draws.Clear();
		m_Bounds.clear();
		m_NextOrder = 0;
		for (const RenderDrawItem& item : scene.DrawItems)
		{
			++stats.DrawItems;
			const glm::mat4 world = item.Transform.Interpolate(alpha);
			if (m_Assets.GetType(item.Mesh) == AssetType::Model)
			{
				const Ref<Model> model = m_Resources->GetModel(item.Mesh);
				if (model == nullptr)
				{
					continue;
				}
				// A model draws every submesh of every node's mesh; overrides apply to the slots of each mesh.
				for (const ModelNode& node : model->GetNodes())
				{
					if (node.Mesh.IsValid())
					{
						AddMesh(
							node.Mesh, world * node.ModelTransform, -1, item.MaterialOverrides, view, frustum,
							collectBounds, stats);
					}
				}
				continue;
			}
			AddMesh(item.Mesh, world, item.SubmeshIndex, item.MaterialOverrides, view, frustum, collectBounds, stats);
		}
		std::ranges::sort(m_Draws.Opaque, OpaqueBefore);
		std::ranges::sort(m_Draws.Transparent, TransparentBefore);
		stats.DrawCalls = static_cast<uint32_t>(m_Draws.Opaque.size() + m_Draws.Transparent.size());
	}

	Result<SceneRenderStats>
	SceneRenderer::Render(const RenderScene& scene, const RenderView& view, float alpha, nvrhi::IFramebuffer* target)
	{
		if (target == nullptr)
		{
			return Error("the scene renderer needs a target framebuffer");
		}
		const nvrhi::FramebufferDesc& targetDesc = target->getDesc();
		const nvrhi::FramebufferInfoEx& targetInfo = target->getFramebufferInfo();
		if (targetDesc.colorAttachments.size() != 1 || targetDesc.depthAttachment.valid())
		{
			return Error("the scene renderer's target needs one color attachment and no depth attachment");
		}
		if (targetInfo.width == 0 || targetInfo.height == 0 || targetInfo.sampleCount != 1)
		{
			return Error("the scene renderer's target must be single-sampled and not empty");
		}
		++m_RenderCount;
		m_Resources->BeginFrame();
		const glm::uvec2 size(targetInfo.width, targetInfo.height);
		Result<ViewTargets*> targets = AcquireTargets(size);
		if (!targets)
		{
			return targets.GetError();
		}

		SceneRenderStats stats;
		const glm::mat4 viewProjection = view.Projection * view.View;
		const Frustum frustum = Frustum::FromViewProjection(viewProjection);
		const bool showBounds = HasFlag(view.Shapes, DebugShapes::Bounds);
		BuildDrawList(scene, view, alpha, frustum, showBounds, stats);

		const float exposure = ExposureFromEV100(view.ExposureEV100);
		const PackedLights lights = PackLights(scene, alpha, exposure, &frustum);
		if (lights.Dropped != 0 && !m_ReportedLightOverflow)
		{
			m_ReportedLightOverflow = true;
			GB_CORE_WARN(
				"Renderer: the scene has more than {} visible lights; {} are left out (reported once)", MaxLights,
				lights.Dropped);
		}
		stats.Lights = static_cast<uint32_t>(lights.Lights.size());

		Gpu::FrameConstants constants;
		constants.View = view.View;
		constants.Projection = view.Projection;
		constants.ViewProjection = viewProjection;
		constants.CameraPosition = glm::vec4(view.GetPosition(), 1.0f);
		constants.AmbientRadiance =
			glm::vec4(scene.Environment.AmbientColor * scene.Environment.AmbientLuminance * exposure, 0.0f);
		constants.Exposure = exposure;
		constants.Time = view.Time;
		constants.LightCount = stats.Lights;

		m_Lines.clear();
		if (view.DrawDebugLines)
		{
			m_Lines = scene.DebugLines;
		}
		m_ShapeLines.Clear();
		for (const AABB& bounds : m_Bounds)
		{
			m_ShapeLines.DrawAABB(bounds, glm::vec4(1.0f, 0.85f, 0.2f, 1.0f));
		}
		AppendDebugShapes(
			scene, view.Shapes, alpha, static_cast<float>(size.x) / static_cast<float>(size.y), m_ShapeLines);
		m_Lines.insert(m_Lines.end(), m_ShapeLines.GetLines().begin(), m_ShapeLines.GetLines().end());
		stats.DebugLines = static_cast<uint32_t>(m_Lines.size());

		Result<uint64_t> triangles = Record(view, **targets, target, constants, lights.Lights);
		if (!triangles)
		{
			return triangles.GetError();
		}
		stats.Triangles = *triangles;
		return stats;
	}

	Result<uint64_t> SceneRenderer::Record(
		const RenderView& view, const ViewTargets& targets, nvrhi::IFramebuffer* target,
		const Gpu::FrameConstants& constants, std::span<const Gpu::Light> lights)
	{
		nvrhi::IDevice& device = *m_Device.GetNvrhiDevice();
		// The target's color with the view's depth, for the overlays that are depth tested. Made per call: targets
		// such as back buffers must not be kept across frames.
		const nvrhi::FramebufferHandle overlay =
			device.createFramebuffer(nvrhi::FramebufferDesc()
		                                 .addColorAttachment(target->getDesc().colorAttachments[0])
		                                 .setDepthAttachment(targets.Depth));
		if (!overlay)
		{
			return Error("NVRHI could not create the overlay framebuffer (see the log)");
		}

		nvrhi::ICommandList& commandList = *m_CommandList;
		commandList.open();
		const auto record = [&]() -> Result<uint64_t> {
			commandList.writeBuffer(m_FrameConstants, &constants, sizeof(constants));
			if (!lights.empty())
			{
				commandList.writeBuffer(m_LightBuffer, lights.data(), lights.size_bytes());
			}
			const SceneFrame frame{ commandList, targets, m_Draws, m_FrameBindingSet, view.ClearColor };
			if (Result<uint64_t> depth = m_DepthPrepass->Execute(frame); !depth)
			{
				return depth.GetError();
			}
			Result<uint64_t> shaded = m_ForwardPass->Execute(frame);
			if (!shaded)
			{
				return shaded.GetError();
			}
			if (Result<void> tonemapped = m_TonemapPass->Execute(commandList, targets.SceneColor, overlay); !tonemapped)
			{
				return tonemapped.GetError();
			}
			if (Result<void> lines = m_DebugLinePass->Execute(commandList, m_Lines, constants.ViewProjection, overlay);
			    !lines)
			{
				return lines.GetError();
			}
			return shaded;
		};
		Result<uint64_t> recorded = record();
		commandList.close();
		if (!recorded)
		{
			return recorded.GetError();
		}
		device.executeCommandList(m_CommandList);
		return recorded;
	}
}
