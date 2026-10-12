#include "gbpch.h"

#include "Gambit/ECS/Systems/TransformSystem.h"

#include "Gambit/ECS/Entity.h"
#include "Gambit/ECS/World.h"

namespace Gambit
{
	void TransformSystem::OnUpdate(World& world, TimeStep timeStep)
	{
		static_cast<void>(timeStep);
		entt::registry& registry = world.GetRegistry();

		// Collected first: a view must not be iterated while its storages change.
		std::vector<entt::entity> stale;
		for (const entt::entity handle : registry.view<WorldTransformComponent>(entt::exclude<TransformComponent>))
		{
			stale.push_back(handle);
		}
		for (const entt::entity handle : stale)
		{
			registry.remove<WorldTransformComponent>(handle);
		}

		// Depth-first from the roots, parents before children: world = parent world * local (the same products as
		// World::ComputeWorldTransform, so both agree bit for bit).
		std::vector<std::pair<entt::entity, glm::mat4>> pending;
		const std::vector<Entity> roots = world.GetRoots();
		for (auto root = roots.rbegin(); root != roots.rend(); ++root)
		{
			pending.emplace_back(root->GetHandle(), glm::mat4(1.0f));
		}
		while (!pending.empty())
		{
			const auto [handle, parentMatrix] = pending.back();
			pending.pop_back();

			glm::mat4 matrix = parentMatrix;
			if (const auto* transform = registry.try_get<TransformComponent>(handle))
			{
				matrix = parentMatrix * transform->GetMatrix();
				registry.emplace_or_replace<WorldTransformComponent>(handle, matrix);
			}
			const std::vector<UUID>& children = registry.get<RelationshipComponent>(handle).Children;
			for (auto child = children.rbegin(); child != children.rend(); ++child)
			{
				pending.emplace_back(world.TryGetEntity(*child).GetHandle(), matrix);
			}
		}
	}
}
