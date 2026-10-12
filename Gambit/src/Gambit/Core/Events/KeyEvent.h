#pragma once

#include "Gambit/Core/Events/Event.h"
#include "Gambit/Input/KeyCode.h"

#include <cstdint>
#include <format>
#include <string>

namespace Gambit
{
	// Base of the events that carry a physical key.
	class KeyEvent : public Event
	{
	public:
		[[nodiscard]] KeyCode GetKeyCode() const noexcept { return m_Key; }

		GB_EVENT_CLASS_CATEGORY(EventCategory::Keyboard | EventCategory::Input)

	protected:
		explicit KeyEvent(KeyCode key) noexcept
			: m_Key(key)
		{}

		KeyCode m_Key;
	};

	// A key went down, or auto-repeats while held (IsRepeat).
	class KeyPressedEvent final : public KeyEvent
	{
	public:
		explicit KeyPressedEvent(KeyCode key, bool repeat = false) noexcept
			: KeyEvent(key), m_Repeat(repeat)
		{}

		[[nodiscard]] bool IsRepeat() const noexcept { return m_Repeat; }
		[[nodiscard]] std::string ToString() const override
		{
			return std::format("KeyPressed (key {}{})", Gambit::ToString(m_Key), m_Repeat ? ", repeat" : "");
		}

		GB_EVENT_CLASS_TYPE(KeyPressed)

	private:
		bool m_Repeat;
	};

	class KeyReleasedEvent final : public KeyEvent
	{
	public:
		explicit KeyReleasedEvent(KeyCode key) noexcept
			: KeyEvent(key)
		{}

		[[nodiscard]] std::string ToString() const override
		{
			return std::format("KeyReleased (key {})", Gambit::ToString(m_Key));
		}

		GB_EVENT_CLASS_TYPE(KeyReleased)
	};

	// Text produced by the keyboard layout: one Unicode code point (use this, not key codes, for text fields).
	class KeyTypedEvent final : public Event
	{
	public:
		explicit KeyTypedEvent(uint32_t codepoint) noexcept
			: m_Codepoint(codepoint)
		{}

		[[nodiscard]] uint32_t GetCodepoint() const noexcept { return m_Codepoint; }
		[[nodiscard]] std::string ToString() const override { return std::format("KeyTyped (U+{:04X})", m_Codepoint); }

		GB_EVENT_CLASS_TYPE(KeyTyped)
		GB_EVENT_CLASS_CATEGORY(EventCategory::Keyboard | EventCategory::Input)

	private:
		uint32_t m_Codepoint;
	};
}
