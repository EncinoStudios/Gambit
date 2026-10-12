#pragma once

#include "Gambit/Core/Base.h"
#include "Gambit/Input/InputActions.h"
#include "Gambit/Input/InputFrame.h"
#include "Gambit/Math/Math.h"

#include <string_view>

namespace Gambit
{
	// Gameplay's view of the input (D-012): the action mapping applied to the current and the previous tick's
	// InputFrame, which gives the pressed and released edges. Evaluation uses integer inputs, float arithmetic and
	// exactly rounded square roots only, so it is deterministic. Unknown actions read as released / zero.
	class InputState
	{
	public:
		InputState() = default;
		explicit InputState(Ref<const InputActions> actions)
			: m_Actions(std::move(actions))
		{}

		void SetActions(Ref<const InputActions> actions) { m_Actions = std::move(actions); }
		[[nodiscard]] const Ref<const InputActions>& GetActions() const noexcept { return m_Actions; }

		// Advances one simulation tick: `frame` becomes the current frame and the old current frame the previous one.
		void Update(const InputFrame& frame) noexcept;
		// Forgets both frames (everything released).
		void Reset() noexcept;

		[[nodiscard]] const InputFrame& GetFrame() const noexcept { return m_Current; }
		[[nodiscard]] const InputFrame& GetPreviousFrame() const noexcept { return m_Previous; }

		// Whether the action is held: any binding of a Button action down, any non-zero value of an axis action.
		[[nodiscard]] bool IsDown(InputActionId action) const;
		// Down now and not on the previous tick.
		[[nodiscard]] bool WasPressed(InputActionId action) const;
		// Up now and down on the previous tick.
		[[nodiscard]] bool WasReleased(InputActionId action) const;
		// The value of an Axis1D action, 1 or 0 for a Button action, the length of an Axis2D action's vector.
		[[nodiscard]] float GetAxis(InputActionId action) const;
		// The value of an Axis2D action, (value, 0) for the others.
		[[nodiscard]] glm::vec2 GetAxis2D(InputActionId action) const;

		[[nodiscard]] bool IsDown(std::string_view action) const { return IsDown(Find(action)); }
		[[nodiscard]] bool WasPressed(std::string_view action) const { return WasPressed(Find(action)); }
		[[nodiscard]] bool WasReleased(std::string_view action) const { return WasReleased(Find(action)); }
		[[nodiscard]] float GetAxis(std::string_view action) const { return GetAxis(Find(action)); }
		[[nodiscard]] glm::vec2 GetAxis2D(std::string_view action) const { return GetAxis2D(Find(action)); }

		[[nodiscard]] InputActionId Find(std::string_view action) const noexcept
		{
			return m_Actions ? m_Actions->Find(action) : InvalidInputAction;
		}

	private:
		Ref<const InputActions> m_Actions;
		InputFrame m_Current;
		InputFrame m_Previous;
	};
}
