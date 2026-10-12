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

		// The UUID is the entity's key in files (D-025), so the component itself is not serialized.
		registry.RegisterComponent<IDComponent>("ID", EngineManaged)
			.Field("ID", &IDComponent::ID, Attr::ReadOnly{}, Attr::Tooltip{ "The entity's unique identifier" });

		registry.RegisterComponent<TagComponent>("Tag", Serializable | EditorVisible)
			.Field("Name", &TagComponent::Name, Attr::Tooltip{ "Display name; need not be unique" });

		registry.RegisterComponent<TransformComponent>("Transform")
			.Field("Translation", &TransformComponent::Translation, Attr::Tooltip{ "Position relative to the parent" })
			.Field("Rotation", &TransformComponent::Rotation, Attr::Tooltip{ "Rotation relative to the parent" })
			.Field("Scale", &TransformComponent::Scale, Attr::Tooltip{ "Scale relative to the parent" });

		// Edited through World::SetParent only; serialized so that files can rebuild the hierarchy.
		registry.RegisterComponent<RelationshipComponent>("Relationship", Serializable | EngineManaged)
			.Field("Parent", &RelationshipComponent::Parent, Attr::ReadOnly{})
			.Field("Children", &RelationshipComponent::Children, Attr::ReadOnly{});

		// Derived every tick by the TransformSystem; the matrix is not a reflected field.
		registry.RegisterComponent<WorldTransformComponent>("WorldTransform", EngineManaged);

		registry.RegisterComponent<DisabledComponent>("Disabled");

		// Derived from DisabledComponent and the hierarchy by the World.
		registry.RegisterComponent<InactiveComponent>("Inactive", EngineManaged);
	}
}
