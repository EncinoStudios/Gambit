#pragma once

#include "Gambit/Core/Result.h"
#include "Gambit/ECS/Components/CoreComponents.h"
#include "Gambit/ECS/Entity.h"
#include "Gambit/Renderer/Model.h"
#include "Gambit/Serialization/EntityData.h"

#include <optional>

namespace Gambit
{
	// Turning models into entities (D-038).
	namespace ModelUtils
	{
		struct InstantiateOptions
		{
			// FromWorld draws the new UUIDs from the world's random generator, which keeps simulations deterministic
			// (D-011); Random is for editors and tools. Keep is not allowed (a model has no entity UUIDs).
			UUIDMode Ids = UUIDMode::FromWorld;
		};

		// Creates the model's node hierarchy below one new root entity named `name` (the model's file name without
		// extension when empty) and returns that root. The root goes below `parent` (a root itself when null) with
		// `transform` as its local transform (the identity when unset). Every node becomes an entity with its name, a
		// Transform, and a MeshRenderer, Camera (never primary) or light component for what hangs at it. Not while the
		// world's systems run (use the command buffer there).
		[[nodiscard]] Result<Entity> Instantiate(
			World& world, const Model& model, Entity parent = {},
			const std::optional<TransformComponent>& transform = std::nullopt, std::string_view name = {},
			const InstantiateOptions& options = {});
	}
}
