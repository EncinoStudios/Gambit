#pragma once

#include "Gambit/Core/Events/Event.h"

#include <cstdint>
#include <format>
#include <string>

namespace Gambit
{
	// A gamepad was plugged in. `slot` is the index in InputFrame::Gamepads (0 to MaxGamepads - 1).
	class GamepadConnectedEvent final : public Event
	{
	public:
		GamepadConnectedEvent(uint32_t slot, std::string name)
			: m_Slot(slot), m_Name(std::move(name))
		{}

		[[nodiscard]] uint32_t GetSlot() const noexcept { return m_Slot; }
		[[nodiscard]] const std::string& GetName() const noexcept { return m_Name; }
		[[nodiscard]] std::string ToString() const override
		{
			return std::format("GamepadConnected (slot {}, '{}')", m_Slot, m_Name);
		}

		GB_EVENT_CLASS_TYPE(GamepadConnected)
		GB_EVENT_CLASS_CATEGORY(EventCategory::Gamepad | EventCategory::Input)

	private:
		uint32_t m_Slot;
		std::string m_Name;
	};

	class GamepadDisconnectedEvent final : public Event
	{
	public:
		explicit GamepadDisconnectedEvent(uint32_t slot) noexcept
			: m_Slot(slot)
		{}

		[[nodiscard]] uint32_t GetSlot() const noexcept { return m_Slot; }
		[[nodiscard]] std::string ToString() const override
		{
			return std::format("GamepadDisconnected (slot {})", m_Slot);
		}

		GB_EVENT_CLASS_TYPE(GamepadDisconnected)
		GB_EVENT_CLASS_CATEGORY(EventCategory::Gamepad | EventCategory::Input)

	private:
		uint32_t m_Slot;
	};
}
