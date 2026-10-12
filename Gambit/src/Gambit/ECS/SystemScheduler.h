#pragma once

#include "Gambit/Core/Base.h"
#include "Gambit/Core/Result.h"
#include "Gambit/Core/TimeStep.h"
#include "Gambit/ECS/System.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Gambit
{
	class World;

	// Wall-clock cost of a system, for profiling tools only: never fed back into the simulation (D-011).
	struct SystemStats
	{
		uint64_t UpdateCount = 0;
		double LastMilliseconds = 0.0;
		double MaxMilliseconds = 0.0;
		double TotalMilliseconds = 0.0;
	};

	// Runs the systems of one World in a deterministic order (D-024): phase after phase; within a phase by order, then
	// by name; the world's CommandBuffer is flushed after every phase. Owned by its World.
	class SystemScheduler
	{
	public:
		explicit SystemScheduler(World& world);
		~SystemScheduler();
		SystemScheduler(const SystemScheduler&) = delete;
		SystemScheduler& operator=(const SystemScheduler&) = delete;

		// Adds a system and calls its OnCreate. Fails for a null system, an empty or taken name, or while running.
		[[nodiscard]] Result<void> AddSystem(Scope<ISystem> system);
		// Calls OnDestroy and removes the system; false when there is none of that name or while running.
		bool RemoveSystem(std::string_view name);
		// Removes every system, in reverse execution order. Not while running.
		void RemoveAllSystems();

		[[nodiscard]] ISystem* FindSystem(std::string_view name) const;
		// In execution order.
		[[nodiscard]] std::vector<ISystem*> GetSystems() const;
		[[nodiscard]] size_t GetSystemCount() const noexcept { return m_Entries.size(); }

		// Disabled systems are skipped. Allowed while running (a failing system may disable itself): it affects the
		// systems that have not run yet. False when there is no such system.
		bool SetSystemEnabled(std::string_view name, bool enabled);
		[[nodiscard]] bool IsSystemEnabled(std::string_view name) const;

		// Null when there is no such system.
		[[nodiscard]] const SystemStats* GetStats(std::string_view name) const;
		void ResetStats();

		// One tick: for every phase, its enabled systems (in Edit mode only those that run in edit mode), then a flush
		// of the command buffer. World::Tick calls this.
		void Run(TimeStep timeStep, SimulationMode mode);
		[[nodiscard]] bool IsRunning() const noexcept { return m_Running; }

	private:
		struct Entry
		{
			Scope<ISystem> System;
			std::string Name;
			SystemPhase Phase = SystemPhase::Update;
			int32_t Order = 0;
			bool RunsInEditMode = false;
			bool Enabled = true;
			SystemStats Stats;
		};

		[[nodiscard]] Entry* FindEntry(std::string_view name);
		[[nodiscard]] const Entry* FindEntry(std::string_view name) const;

		World& m_World;
		std::vector<Entry> m_Entries; // in execution order
		bool m_Running = false;
	};
}
