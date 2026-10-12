#include "gbpch.h"

#include "Gambit/Input/InputState.h"

#include <cmath>

namespace Gambit
{
	namespace
	{
		// sqrt(2) rounded to float (what std::sqrt(2.0f) returns): the length of a diagonal of unit steps.
		constexpr float Sqrt2 = 1.41421356f;

		// Deadzone on one axis: values below it read as 0, the rest is rescaled so that the full range stays [0, 1].
		float ApplyDeadzone(float value, float deadzone) noexcept
		{
			const float magnitude = std::abs(value);
			if (magnitude <= deadzone)
			{
				return 0.0f;
			}
			const float rescaled = std::min(1.0f, (magnitude - deadzone) / (1.0f - deadzone));
			return value < 0.0f ? -rescaled : rescaled;
		}

		// Radial deadzone of a stick: a circle around the center instead of a cross, so diagonals feel the same.
		glm::vec2 ApplyRadialDeadzone(glm::vec2 value, float deadzone) noexcept
		{
			const float length = std::sqrt(value.x * value.x + value.y * value.y);
			if (length <= deadzone)
			{
				return glm::vec2(0.0f);
			}
			const float rescaled = std::min(1.0f, (length - deadzone) / (1.0f - deadzone));
			return value * (rescaled / length);
		}

		float AxisOf(glm::vec2 value, InputAxis axis) noexcept
		{
			return axis == InputAxis::X ? value.x : value.y;
		}

		float LengthSquared(glm::vec2 value) noexcept
		{
			return value.x * value.x + value.y * value.y;
		}

		// The gamepads a binding reads: its own slot, or every connected one.
		template<typename Visitor>
		void ForEachGamepad(const InputBinding& binding, const InputFrame& frame, Visitor&& visit)
		{
			for (size_t slot = 0; slot < MaxGamepads; ++slot)
			{
				if ((binding.Gamepad < 0 || binding.Gamepad == static_cast<int>(slot))
				    && frame.Gamepads[slot].Connected)
				{
					visit(frame.Gamepads[slot]);
				}
			}
		}

		bool IsDigitalDown(const InputBinding& binding, const InputFrame& frame)
		{
			switch (binding.Type)
			{
				case InputBindingType::Key:
					return frame.IsKeyDown(binding.Key);
				case InputBindingType::MouseButton:
					return frame.IsMouseButtonDown(binding.Button);
				case InputBindingType::GamepadButton:
				{
					bool down = false;
					ForEachGamepad(binding, frame, [&](const GamepadFrame& pad) {
						down = down || pad.IsButtonDown(binding.PadButton);
					});
					return down;
				}
				case InputBindingType::GamepadAxis:
				{
					bool down = false;
					ForEachGamepad(binding, frame, [&](const GamepadFrame& pad) {
						down = down || std::abs(pad.GetAxis(binding.PadAxis)) >= binding.Threshold;
					});
					return down;
				}
				default:
					return false;
			}
		}

		// A one-dimensional reading of a binding.
		float EvaluateAxis(const InputBinding& binding, const InputFrame& frame)
		{
			switch (binding.Type)
			{
				case InputBindingType::Key:
				case InputBindingType::MouseButton:
				case InputBindingType::GamepadButton:
					return IsDigitalDown(binding, frame) ? binding.Scale : 0.0f;
				case InputBindingType::MouseDelta:
				{
					const glm::vec2 delta(
						static_cast<float>(frame.MouseDeltaX) / static_cast<float>(InputFixedPointScale),
						static_cast<float>(frame.MouseDeltaY) / static_cast<float>(InputFixedPointScale));
					const float value = AxisOf(delta, binding.Axis) * binding.Scale;
					return binding.Invert ? -value : value;
				}
				case InputBindingType::Scroll:
				{
					const glm::vec2 scroll(
						static_cast<float>(frame.ScrollX) / static_cast<float>(InputFixedPointScale),
						static_cast<float>(frame.ScrollY) / static_cast<float>(InputFixedPointScale));
					const float value = AxisOf(scroll, binding.Axis) * binding.Scale;
					return binding.Invert ? -value : value;
				}
				case InputBindingType::GamepadAxis:
				{
					float best = 0.0f;
					ForEachGamepad(binding, frame, [&](const GamepadFrame& pad) {
						float value = ApplyDeadzone(pad.GetAxis(binding.PadAxis), binding.Deadzone) * binding.Scale;
						value = binding.Invert ? -value : value;
						if (std::abs(value) > std::abs(best))
						{
							best = value;
						}
					});
					return best;
				}
				case InputBindingType::Composite1D:
				{
					const float positive = frame.IsKeyDown(binding.Key) ? 1.0f : 0.0f;
					const float negative = frame.IsKeyDown(binding.NegativeKey) ? 1.0f : 0.0f;
					return (positive - negative) * binding.Scale;
				}
				default:
					return 0.0f;
			}
		}

		// A two-dimensional reading of a binding.
		glm::vec2 EvaluateAxis2D(const InputBinding& binding, const InputFrame& frame)
		{
			switch (binding.Type)
			{
				case InputBindingType::MouseDelta:
				case InputBindingType::Scroll:
				{
					const bool mouse = binding.Type == InputBindingType::MouseDelta;
					glm::vec2 value(
						static_cast<float>(mouse ? frame.MouseDeltaX : frame.ScrollX)
							/ static_cast<float>(InputFixedPointScale),
						static_cast<float>(mouse ? frame.MouseDeltaY : frame.ScrollY)
							/ static_cast<float>(InputFixedPointScale));
					value *= binding.Scale;
					return glm::vec2(binding.InvertX ? -value.x : value.x, binding.InvertY ? -value.y : value.y);
				}
				case InputBindingType::GamepadStick:
				{
					const bool right = binding.Stick == GamepadStick::Right;
					const GamepadAxis axisX = right ? GamepadAxis::RightX : GamepadAxis::LeftX;
					const GamepadAxis axisY = right ? GamepadAxis::RightY : GamepadAxis::LeftY;
					glm::vec2 best(0.0f);
					ForEachGamepad(binding, frame, [&](const GamepadFrame& pad) {
						glm::vec2 value =
							ApplyRadialDeadzone(glm::vec2(pad.GetAxis(axisX), pad.GetAxis(axisY)), binding.Deadzone)
							* binding.Scale;
						value = glm::vec2(binding.InvertX ? -value.x : value.x, binding.InvertY ? -value.y : value.y);
						if (LengthSquared(value) > LengthSquared(best))
						{
							best = value;
						}
					});
					return best;
				}
				case InputBindingType::Composite2D:
				{
					const auto down = [&](KeyCode key) { return frame.IsKeyDown(key) ? 1.0f : 0.0f; };
					glm::vec2 value(down(binding.Right) - down(binding.Left), down(binding.Up) - down(binding.Down));
					if (value.x != 0.0f && value.y != 0.0f)
					{
						value /= Sqrt2; // diagonals are as fast as straight lines
					}
					return value * binding.Scale;
				}
				default:
					return glm::vec2(0.0f);
			}
		}

		// The value of an action on a frame: x for a Button (1 or 0) and an Axis1D, the vector for an Axis2D.
		// An action with several bindings takes the binding with the largest magnitude (the first one on ties).
		glm::vec2 EvaluateAction(const InputAction& action, const InputFrame& frame)
		{
			glm::vec2 best(0.0f);
			for (const InputBinding& binding : action.Bindings)
			{
				glm::vec2 value(0.0f);
				switch (action.Type)
				{
					case InputActionType::Button:
						value.x = IsDigitalDown(binding, frame) ? 1.0f : 0.0f;
						break;
					case InputActionType::Axis1D:
						value.x = EvaluateAxis(binding, frame);
						break;
					case InputActionType::Axis2D:
						value = EvaluateAxis2D(binding, frame);
						break;
				}
				if (LengthSquared(value) > LengthSquared(best))
				{
					best = value;
				}
			}
			return best;
		}

		bool IsActive(const InputAction& action, const InputFrame& frame)
		{
			const glm::vec2 value = EvaluateAction(action, frame);
			return value.x != 0.0f || value.y != 0.0f;
		}
	}

	void InputState::Update(const InputFrame& frame) noexcept
	{
		m_Previous = m_Current;
		m_Current = frame;
	}

	void InputState::Reset() noexcept
	{
		m_Previous = {};
		m_Current = {};
	}

	bool InputState::IsDown(InputActionId action) const
	{
		return m_Actions && action < m_Actions->GetActions().size()
			&& IsActive(m_Actions->GetActions()[action], m_Current);
	}

	bool InputState::WasPressed(InputActionId action) const
	{
		return m_Actions && action < m_Actions->GetActions().size()
			&& IsActive(m_Actions->GetActions()[action], m_Current)
			&& !IsActive(m_Actions->GetActions()[action], m_Previous);
	}

	bool InputState::WasReleased(InputActionId action) const
	{
		return m_Actions && action < m_Actions->GetActions().size()
			&& !IsActive(m_Actions->GetActions()[action], m_Current)
			&& IsActive(m_Actions->GetActions()[action], m_Previous);
	}

	float InputState::GetAxis(InputActionId action) const
	{
		if (!m_Actions || action >= m_Actions->GetActions().size())
		{
			return 0.0f;
		}
		const InputAction& definition = m_Actions->GetActions()[action];
		const glm::vec2 value = EvaluateAction(definition, m_Current);
		return definition.Type == InputActionType::Axis2D ? std::sqrt(LengthSquared(value)) : value.x;
	}

	glm::vec2 InputState::GetAxis2D(InputActionId action) const
	{
		if (!m_Actions || action >= m_Actions->GetActions().size())
		{
			return glm::vec2(0.0f);
		}
		return EvaluateAction(m_Actions->GetActions()[action], m_Current);
	}
}
