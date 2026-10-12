#include "gbpch.h"

#include "Gambit/ECS/SystemScheduler.h"

#include "Gambit/Core/Timer.h"
#include "Gambit/ECS/World.h"

namespace Gambit
{
	SystemScheduler::SystemScheduler(World& world)
		: m_World(world)
	{}

	SystemScheduler::~SystemScheduler()
	{
		GB_CORE_ASSERT(m_Entries.empty(), "the World removes its systems before it is destroyed");
	}

	Result<void> SystemScheduler::AddSystem(Scope<ISystem> system)
	{
		if (system == nullptr)
		{
			return Error("cannot add a null system");
		}
		if (m_Running)
		{
			return Error::Format("cannot add system '{}' while the systems run", system->GetName());
		}
		const std::string_view name = system->GetName();
		if (name.empty())
		{
			return Error("a system needs a name");
		}
		if (FindEntry(name) != nullptr)
		{
			return Error::Format("a system named '{}' already exists", name);
		}
		const SystemPhase phase = system->GetPhase();
		if (static_cast<size_t>(phase) >= SystemPhaseCount)
		{
			return Error::Format("system '{}' has an invalid phase", name);
		}

		Entry entry;
		entry.Name = std::string(name);
		entry.Phase = phase;
		entry.Order = system->GetOrder();
		entry.RunsInEditMode = system->RunsInEditMode();
		entry.System = std::move(system);
		const auto before = [](const Entry& a, const Entry& b) {
			if (a.Phase != b.Phase)
			{
				return a.Phase < b.Phase;
			}
			if (a.Order != b.Order)
			{
				return a.Order < b.Order;
			}
			return a.Name < b.Name;
		};
		const auto position = std::ranges::upper_bound(m_Entries, entry, before);
		ISystem& added = *m_Entries.insert(position, std::move(entry))->System;
		added.OnCreate(m_World);
		return {};
	}

	bool SystemScheduler::RemoveSystem(std::string_view name)
	{
		GB_CORE_ASSERT(!m_Running, "cannot remove system '{}' while the systems run", name);
		const auto position =
			std::ranges::find_if(m_Entries, [name](const Entry& entry) { return entry.Name == name; });
		if (m_Running || position == m_Entries.end())
		{
			return false;
		}
		Scope<ISystem> system = std::move(position->System);
		m_Entries.erase(position);
		system->OnDestroy(m_World);
		return true;
	}

	void SystemScheduler::RemoveAllSystems()
	{
		GB_CORE_ASSERT(!m_Running, "cannot remove the systems while they run");
		while (!m_Entries.empty())
		{
			Scope<ISystem> system = std::move(m_Entries.back().System);
			m_Entries.pop_back();
			system->OnDestroy(m_World);
		}
	}

	ISystem* SystemScheduler::FindSystem(std::string_view name) const
	{
		const Entry* entry = FindEntry(name);
		return entry != nullptr ? entry->System.get() : nullptr;
	}

	std::vector<ISystem*> SystemScheduler::GetSystems() const
	{
		std::vector<ISystem*> systems;
		systems.reserve(m_Entries.size());
		for (const Entry& entry : m_Entries)
		{
			systems.push_back(entry.System.get());
		}
		return systems;
	}

	bool SystemScheduler::SetSystemEnabled(std::string_view name, bool enabled)
	{
		Entry* entry = FindEntry(name);
		if (entry == nullptr)
		{
			return false;
		}
		entry->Enabled = enabled;
		return true;
	}

	bool SystemScheduler::IsSystemEnabled(std::string_view name) const
	{
		const Entry* entry = FindEntry(name);
		return entry != nullptr && entry->Enabled;
	}

	const SystemStats* SystemScheduler::GetStats(std::string_view name) const
	{
		const Entry* entry = FindEntry(name);
		return entry != nullptr ? &entry->Stats : nullptr;
	}

	void SystemScheduler::ResetStats()
	{
		for (Entry& entry : m_Entries)
		{
			entry.Stats = {};
		}
	}

	void SystemScheduler::Run(TimeStep timeStep, SimulationMode mode)
	{
		GB_CORE_ASSERT(!m_Running, "SystemScheduler::Run is not reentrant");
		m_Running = true;
		size_t next = 0;
		for (size_t phase = 0; phase < SystemPhaseCount; ++phase)
		{
			for (; next < m_Entries.size() && static_cast<size_t>(m_Entries[next].Phase) == phase; ++next)
			{
				Entry& entry = m_Entries[next];
				if (!entry.Enabled || (mode == SimulationMode::Edit && !entry.RunsInEditMode))
				{
					continue;
				}
				// Wall-clock timing feeds the statistics only, never the simulation (D-011).
				const Timer timer;
				entry.System->OnUpdate(m_World, timeStep);
				const double milliseconds = timer.ElapsedMilliseconds();
				SystemStats& stats = entry.Stats;
				++stats.UpdateCount;
				stats.LastMilliseconds = milliseconds;
				stats.MaxMilliseconds = std::max(stats.MaxMilliseconds, milliseconds);
				stats.TotalMilliseconds += milliseconds;
			}
			m_World.GetCommandBuffer().Flush();
		}
		m_Running = false;
	}

	SystemScheduler::Entry* SystemScheduler::FindEntry(std::string_view name)
	{
		return const_cast<Entry*>(std::as_const(*this).FindEntry(name));
	}

	const SystemScheduler::Entry* SystemScheduler::FindEntry(std::string_view name) const
	{
		for (const Entry& entry : m_Entries)
		{
			if (entry.Name == name)
			{
				return &entry;
			}
		}
		return nullptr;
	}
}
