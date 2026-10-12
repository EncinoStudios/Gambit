#pragma once

#include "Gambit/ECS/System.h"

namespace Gambit
{
	// Computes WorldTransformComponent (D-021) in the Transform phase, also in edit mode: parents before children,
	// roots in World::GetRoots order, children in sibling order. Entities with a TransformComponent get a
	// WorldTransformComponent; entities without one lose it and pass their parent's matrix on to their children.
	// Inactive entities are included, so world transforms are valid whenever an entity becomes active again.
	class TransformSystem final : public ISystem
	{
	public:
		[[nodiscard]] std::string_view GetName() const override { return "TransformSystem"; }
		[[nodiscard]] SystemPhase GetPhase() const override { return SystemPhase::Transform; }
		[[nodiscard]] bool RunsInEditMode() const override { return true; }

		void OnUpdate(World& world, TimeStep timeStep) override;
	};
}
