#include "gbpch.h"

#include "Gambit/ECS/Components/CoreComponents.h"

#include "Gambit/Reflection/TypeRegistry.h"

namespace Gambit
{
	void RegisterCoreComponents(TypeRegistry& registry)
	{
		constexpr ComponentFlags Serializable = ComponentFlags::Serializable;
		constexpr ComponentFlags EditorVisible = ComponentFlags::EditorVisible;
		constexpr ComponentFlags EngineManaged = ComponentFlags::Internal;
		constexpr ComponentFlags EngineDerived = ComponentFlags::Internal | ComponentFlags::Derived;

		// The UUID is the entity's key in files (D-025), so the component itself is not serialized.
		registry.RegisterComponent<IDComponent>("ID", EngineManaged)
			.Field("ID", &IDComponent::ID, Attr::ReadOnly{}, Attr::Tooltip{ "The entity's unique identifier" });

		registry.RegisterComponent<TagComponent>("Tag", Serializable | EditorVisible)
			.Field("Name", &TagComponent::Name, Attr::Tooltip{ "Display name; need not be unique" });

		registry.RegisterComponent<TransformComponent>("Transform")
			.Field("Translation", &TransformComponent::Translation, Attr::Tooltip{ "Position relative to the parent" })
			.Field("Rotation", &TransformComponent::Rotation, Attr::Tooltip{ "Rotation relative to the parent" })
			.Field("Scale", &TransformComponent::Scale, Attr::Tooltip{ "Scale relative to the parent" });

		// Edited through World::SetParent only. Files store the parent of each entity and its position in the entity
		// list instead (SceneSerializer), so the component itself is not serialized.
		registry.RegisterComponent<RelationshipComponent>("Relationship", EngineManaged)
			.Field("Parent", &RelationshipComponent::Parent, Attr::ReadOnly{})
			.Field("Children", &RelationshipComponent::Children, Attr::ReadOnly{});

		// Derived every tick by the TransformSystem; the matrix is not a reflected field and the component does not
		// take part in the state hash at all.
		registry.RegisterComponent<WorldTransformComponent>("WorldTransform", EngineDerived);

		registry.RegisterComponent<DisabledComponent>("Disabled");

		// Derived from DisabledComponent and the hierarchy by the World.
		registry.RegisterComponent<InactiveComponent>("Inactive", EngineManaged);
	}
}
