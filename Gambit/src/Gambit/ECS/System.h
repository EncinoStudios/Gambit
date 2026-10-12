#pragma once

#include "Gambit/Core/TimeStep.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace Gambit
{
	class World;

	// The fixed phases of a tick, in execution order (D-024). The command buffer is flushed after each phase.
	enum class SystemPhase : uint8_t
	{
		Input,
		PreUpdate,
		Update,
		PrePhysics,
		Physics,
		PostPhysics,
		Animation,
		LateUpdate,
		Transform,
		PreRender, // render extraction
		Count
	};

	inline constexpr size_t SystemPhaseCount = static_cast<size_t>(SystemPhase::Count);

	[[nodiscard]] std::string_view ToString(SystemPhase phase) noexcept;
	[[nodiscard]] std::optional<SystemPhase> ParseSystemPhase(std::string_view name) noexcept;

	// What a tick is for: Play advances the simulation (tick counter, time) and runs every enabled system; Edit runs
	// only the editor-safe systems (RunsInEditMode) and leaves the simulation state alone (D-024).
	enum class SimulationMode : uint8_t
	{
		Play,
		Edit
	};

	// Behaviour that runs every tick (D-020: all logic lives in systems). Systems run in a fixed order: by phase, then
	// order, then name (byte-wise), all read once when the system is added. A system may keep caches, but simulation
	// state lives in components, so that hashing, saving and World::Copy see all of it.
	class ISystem
	{
	public:
		virtual ~ISystem() = default;

		// Unique within a world.
		[[nodiscard]] virtual std::string_view GetName() const = 0;
		[[nodiscard]] virtual SystemPhase GetPhase() const = 0;
		[[nodiscard]] virtual int32_t GetOrder() const { return 0; }
		[[nodiscard]] virtual bool RunsInEditMode() const { return false; }

		// When the system is added to a world / removed from it (or the world is destroyed).
		virtual void OnCreate(World& world) { static_cast<void>(world); }
		virtual void OnUpdate(World& world, TimeStep timeStep) = 0;
		virtual void OnDestroy(World& world) { static_cast<void>(world); }
	};
}
