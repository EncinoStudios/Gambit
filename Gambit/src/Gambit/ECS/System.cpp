#include "gbpch.h"

#include "Gambit/ECS/System.h"

namespace Gambit
{
	namespace
	{
		constexpr std::array<std::string_view, SystemPhaseCount> PhaseNames = {
			"Input",       "PreUpdate", "Update",     "PrePhysics", "Physics",
			"PostPhysics", "Animation", "LateUpdate", "Transform",  "PreRender"
		};
	}

	std::string_view ToString(SystemPhase phase) noexcept
	{
		const auto index = static_cast<size_t>(phase);
		return index < PhaseNames.size() ? PhaseNames[index] : std::string_view("Unknown");
	}

	std::optional<SystemPhase> ParseSystemPhase(std::string_view name) noexcept
	{
		for (size_t index = 0; index < PhaseNames.size(); ++index)
		{
			if (PhaseNames[index] == name)
			{
				return static_cast<SystemPhase>(index);
			}
		}
		return std::nullopt;
	}
}
