#pragma once

#include "Gambit/Core/UUID.h"
#include "Gambit/Math/Math.h"

#include <string>
#include <vector>

// The components every world understands (D-021). Plain data only (D-020); the World and the TransformSystem
// maintain the hierarchy, the derived transforms and the inactive state.

namespace Gambit
{
	class TypeRegistry;

	// The entity's identity: unique within its world, stable across saves and World::Copy. Every entity has one and it
	// never changes (the World indexes entities by it).
	struct IDComponent
	{
		UUID ID;
	};

	// The entity's display name; every entity has one. Names need not be unique.
	struct TagComponent
	{
		std::string Name;
	};

	// Transform relative to the parent (to the world for roots): translation * rotation * scale. Rotation must stay
	// normalized.
	struct TransformComponent
	{
		glm::vec3 Translation{ 0.0f };
		glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
		glm::vec3 Scale{ 1.0f };

		[[nodiscard]] glm::mat4 GetMatrix() const { return ComposeTransform(Translation, Rotation, Scale); }
	};

	// Hierarchy links; every entity has one. Written only by World (SetParent, DestroyEntity): read it, never edit it.
	// Files do not store it: they store each entity's parent, and the order of the entities gives the sibling order.
	struct RelationshipComponent
	{
		UUID Parent;                // null for roots
		std::vector<UUID> Children; // in sibling order
	};

	// The local-to-world matrix, computed by the TransformSystem from the TransformComponents of the entity and its
	// ancestors (an ancestor without a transform counts as the identity). Derived data: not serialized, and it does not
	// take part in the state hash at all (not even its presence).
	struct WorldTransformComponent
	{
		glm::mat4 Matrix{ 1.0f };
	};

	// The user disabled this entity: it and its whole subtree become inactive.
	struct DisabledComponent
	{};

	// Present on every entity that is disabled or has a disabled ancestor; maintained by the World. Systems skip
	// inactive entities (World::View excludes them).
	struct InactiveComponent
	{};

	// Registers the core components: "ID", "Tag", "Transform", "Relationship", "WorldTransform", "Disabled",
	// "Inactive".
	void RegisterCoreComponents(TypeRegistry& registry);
}
