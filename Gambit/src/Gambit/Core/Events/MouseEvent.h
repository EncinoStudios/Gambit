#pragma once

#include "Gambit/Core/Events/Event.h"
#include "Gambit/Input/KeyCode.h"

#include <format>
#include <string>

namespace Gambit
{
	// The cursor moved; the position is in window coordinates (screen units, origin top left).
	class MouseMovedEvent final : public Event
	{
	public:
		MouseMovedEvent(float x, float y) noexcept
			: m_X(x), m_Y(y)
		{}

		[[nodiscard]] float GetX() const noexcept { return m_X; }
		[[nodiscard]] float GetY() const noexcept { return m_Y; }
		[[nodiscard]] std::string ToString() const override { return std::format("MouseMoved ({}, {})", m_X, m_Y); }

		GB_EVENT_CLASS_TYPE(MouseMoved)
		GB_EVENT_CLASS_CATEGORY(EventCategory::Mouse | EventCategory::Input)

	private:
		float m_X;
		float m_Y;
	};

	// The scroll wheel or touchpad scrolled; positive Y is away from the user.
	class MouseScrolledEvent final : public Event
	{
	public:
		MouseScrolledEvent(float offsetX, float offsetY) noexcept
			: m_OffsetX(offsetX), m_OffsetY(offsetY)
		{}

		[[nodiscard]] float GetOffsetX() const noexcept { return m_OffsetX; }
		[[nodiscard]] float GetOffsetY() const noexcept { return m_OffsetY; }
		[[nodiscard]] std::string ToString() const override
		{
			return std::format("MouseScrolled ({}, {})", m_OffsetX, m_OffsetY);
		}

		GB_EVENT_CLASS_TYPE(MouseScrolled)
		GB_EVENT_CLASS_CATEGORY(EventCategory::Mouse | EventCategory::Input)

	private:
		float m_OffsetX;
		float m_OffsetY;
	};

	// Base of the events that carry a mouse button.
	class MouseButtonEvent : public Event
	{
	public:
		[[nodiscard]] MouseButton GetMouseButton() const noexcept { return m_Button; }

		GB_EVENT_CLASS_CATEGORY(EventCategory::Mouse | EventCategory::MouseButton | EventCategory::Input)

	protected:
		explicit MouseButtonEvent(MouseButton button) noexcept
			: m_Button(button)
		{}

		MouseButton m_Button;
	};

	class MouseButtonPressedEvent final : public MouseButtonEvent
	{
	public:
		explicit MouseButtonPressedEvent(MouseButton button) noexcept
			: MouseButtonEvent(button)
		{}

		[[nodiscard]] std::string ToString() const override
		{
			return std::format("MouseButtonPressed ({})", Gambit::ToString(m_Button));
		}

		GB_EVENT_CLASS_TYPE(MouseButtonPressed)
	};

	class MouseButtonReleasedEvent final : public MouseButtonEvent
	{
	public:
		explicit MouseButtonReleasedEvent(MouseButton button) noexcept
			: MouseButtonEvent(button)
		{}

		[[nodiscard]] std::string ToString() const override
		{
			return std::format("MouseButtonReleased ({})", Gambit::ToString(m_Button));
		}

		GB_EVENT_CLASS_TYPE(MouseButtonReleased)
	};
}
