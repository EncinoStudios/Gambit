#include "gbpch.h"

#include "Gambit/Renderer/Systems/RenderExtractionSystem.h"

#include "Gambit/ECS/Entity.h"
#include "Gambit/ECS/World.h"

namespace Gambit
{
	namespace
	{
		template<typename Item>
		void SortByEntity(std::vector<Item>& items)
		{
			std::ranges::sort(items, [](const Item& a, const Item& b) { return a.Entity < b.Entity; });
		}
	}

	void RenderExtractionSystem::OnUpdate(World& world, TimeStep timeStep)
	{
		std::unordered_map<UUID, glm::mat4> current;
		// An entity seen for the first time has no previous transform: it does not move during its first tick.
		const auto makeTransform = [&](UUID entity, const glm::mat4& matrix) {
			current.emplace(entity, matrix);
			const auto previous = m_PreviousTransforms.find(entity);
			return RenderTransform{ previous != m_PreviousTransforms.end() ? previous->second : matrix, matrix };
		};

		m_Scene.Clear();
		m_Scene.Tick = world.GetTick();
		m_Scene.Environment = m_Environment;

		for (auto [handle, id, transform, camera] :
		     world.View<IDComponent, WorldTransformComponent, CameraComponent>().each())
		{
			m_Scene.Cameras.push_back({ id.ID, makeTransform(id.ID, transform.Matrix), camera });
		}
		for (auto [handle, id, transform, renderer] :
		     world.View<IDComponent, WorldTransformComponent, MeshRendererComponent>().each())
		{
			if (!renderer.Visible || !renderer.Mesh.IsValid())
			{
				continue;
			}
			RenderDrawItem item;
			item.Entity = id.ID;
			item.Transform = makeTransform(id.ID, transform.Matrix);
			item.Mesh = renderer.Mesh;
			item.SubmeshIndex = renderer.SubmeshIndex;
			item.MaterialOverrides = renderer.MaterialOverrides;
			item.CastShadows = renderer.CastShadows;
			item.ReceiveShadows = renderer.ReceiveShadows;
			m_Scene.DrawItems.push_back(std::move(item));
		}
		for (auto [handle, id, transform, light] :
		     world.View<IDComponent, WorldTransformComponent, DirectionalLightComponent>().each())
		{
			m_Scene.DirectionalLights.push_back({ id.ID, makeTransform(id.ID, transform.Matrix), light });
		}
		for (auto [handle, id, transform, light] :
		     world.View<IDComponent, WorldTransformComponent, PointLightComponent>().each())
		{
			m_Scene.PointLights.push_back({ id.ID, makeTransform(id.ID, transform.Matrix), light });
		}
		for (auto [handle, id, transform, light] :
		     world.View<IDComponent, WorldTransformComponent, SpotLightComponent>().each())
		{
			m_Scene.SpotLights.push_back({ id.ID, makeTransform(id.ID, transform.Matrix), light });
		}
		// The renderer's order does not depend on the storage order of the components.
		SortByEntity(m_Scene.Cameras);
		SortByEntity(m_Scene.DrawItems);
		SortByEntity(m_Scene.DirectionalLights);
		SortByEntity(m_Scene.PointLights);
		SortByEntity(m_Scene.SpotLights);

		m_Scene.DebugLines = m_DebugDraw.GetLines();
		m_DebugDraw.Advance(timeStep.GetSeconds());
		m_PreviousTransforms = std::move(current);
	}

	RenderExtractionSystem* RenderExtractionSystem::Find(World& world)
	{
		return dynamic_cast<RenderExtractionSystem*>(world.GetScheduler().FindSystem(Name));
	}
}
