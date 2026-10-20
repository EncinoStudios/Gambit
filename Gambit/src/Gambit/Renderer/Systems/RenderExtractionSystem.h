#pragma once

#include "Gambit/ECS/System.h"
#include "Gambit/Renderer/DebugDraw.h"
#include "Gambit/Renderer/RenderScene.h"

#include <optional>
#include <unordered_map>

namespace Gambit
{
	// Copies what the renderer needs from the world into a RenderScene (D-032), in the PreRender phase of every tick,
	// edit mode included: cameras, lights and mesh renderers of active entities with a WorldTransform (so add the
	// TransformSystem too), each with its world transform of the previous and of the current tick, plus the debug
	// lines. Reads the world only; the debug draw list it owns is presentation state, not simulation state.
	class RenderExtractionSystem final : public ISystem
	{
	public:
		static constexpr std::string_view Name = "RenderExtractionSystem";

		[[nodiscard]] std::string_view GetName() const override { return Name; }
		[[nodiscard]] SystemPhase GetPhase() const override { return SystemPhase::PreRender; }
		[[nodiscard]] bool RunsInEditMode() const override { return true; }

		void OnUpdate(World& world, TimeStep timeStep) override;

		// The scene as of the latest tick.
		[[nodiscard]] const RenderScene& GetRenderScene() const noexcept { return m_Scene; }

		// Copied into every extracted scene.
		void SetEnvironment(const RenderEnvironment& environment) { m_Environment = environment; }
		[[nodiscard]] const RenderEnvironment& GetEnvironment() const noexcept { return m_Environment; }

		// Where systems and scripts of this world draw debug lines (DebugDrawList documents their lifetime).
		[[nodiscard]] DebugDrawList& GetDebugDraw() noexcept { return m_DebugDraw; }

		// Forgets the previous transforms and time: the next extraction starts without motion, as if every entity
		// were seen for the first time (after loading a scene, or when the world jumped back in time).
		void ResetHistory();
		// Forgets the previous transform of one entity: the next extraction shows it where it is, without moving it
		// there over a tick (after a teleport or a respawn).
		void Snap(UUID entity) { m_PreviousTransforms.erase(entity); }

		// The extraction system of `world`, null when it has none.
		[[nodiscard]] static RenderExtractionSystem* Find(World& world);

	private:
		RenderScene m_Scene;
		RenderEnvironment m_Environment;
		DebugDrawList m_DebugDraw;
		// The world matrix each extracted entity had at the latest extraction (lookups only, never iterated).
		std::unordered_map<UUID, glm::mat4> m_PreviousTransforms;
		std::optional<double> m_PreviousTime; // the world's time at the latest extraction
	};
}
