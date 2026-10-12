#pragma once

#include "Gambit/Core/Base.h"

#include <concepts>
#include <cstdint>
#include <string>
#include <string_view>

namespace Gambit
{
	enum class EventType : uint8_t
	{
		None = 0,
		WindowClose,
		WindowResize,
		WindowFocus,
		WindowFramebufferResize,
		WindowContentScale,
		WindowFileDrop,
		KeyPressed,
		KeyReleased,
		KeyTyped,
		MouseMoved,
		MouseScrolled,
		MouseButtonPressed,
		MouseButtonReleased,
		GamepadConnected,
		GamepadDisconnected
	};

	// Bit flags: an event belongs to one or more categories, so that layers can filter without knowing every type.
	enum class EventCategory : uint32_t
	{
		None = 0,
		Window = GB_BIT(0),
		Input = GB_BIT(1),
		Keyboard = GB_BIT(2),
		Mouse = GB_BIT(3),
		MouseButton = GB_BIT(4),
		Gamepad = GB_BIT(5)
	};

	[[nodiscard]] constexpr EventCategory operator|(EventCategory a, EventCategory b) noexcept
	{
		return static_cast<EventCategory>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
	}

	[[nodiscard]] constexpr bool HasCategory(EventCategory flags, EventCategory category) noexcept
	{
		return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(category)) != 0;
	}

	[[nodiscard]] std::string_view ToString(EventType type) noexcept;

// Boilerplate of a concrete event class: its type (also as a compile-time constant for EventDispatcher) and category.
#define GB_EVENT_CLASS_TYPE(eventType)                                                                                 \
	[[nodiscard]] static constexpr ::Gambit::EventType GetStaticType() noexcept                                        \
	{                                                                                                                  \
		return ::Gambit::EventType::eventType;                                                                         \
	}                                                                                                                  \
	[[nodiscard]] ::Gambit::EventType GetEventType() const noexcept override                                           \
	{                                                                                                                  \
		return GetStaticType();                                                                                        \
	}

#define GB_EVENT_CLASS_CATEGORY(category)                                                                              \
	[[nodiscard]] ::Gambit::EventCategory GetCategoryFlags() const noexcept override                                   \
	{                                                                                                                  \
		return category;                                                                                               \
	}

	// Base of everything the platform layer reports (window, keyboard, mouse, gamepad). Events travel from the
	// window through Application::OnEvent to the layers, top layer first, until one marks the event Handled.
	class Event
	{
	public:
		virtual ~Event() = default;

		[[nodiscard]] virtual EventType GetEventType() const noexcept = 0;
		[[nodiscard]] virtual EventCategory GetCategoryFlags() const noexcept = 0;
		// Readable description for logs, e.g. "KeyPressed (key W)".
		[[nodiscard]] virtual std::string ToString() const;

		[[nodiscard]] bool IsInCategory(EventCategory category) const noexcept
		{
			return HasCategory(GetCategoryFlags(), category);
		}

		// Set by a handler that consumed the event; lower layers no longer see it.
		bool Handled = false;
	};

	// Type-safe unpacking of an Event: Dispatch<T> calls the handler when the event is a T and records its result in
	// Handled. A handler returns true when it consumed the event.
	class EventDispatcher
	{
	public:
		explicit EventDispatcher(Event& event) noexcept
			: m_Event(event)
		{}

		// Returns whether the event had type T (and the handler ran).
		template<typename T, typename Handler>
			requires std::derived_from<T, Event> && std::invocable<Handler, T&>
		bool Dispatch(Handler&& handler)
		{
			if (m_Event.GetEventType() != T::GetStaticType())
			{
				return false;
			}
			m_Event.Handled |= static_cast<bool>(handler(static_cast<T&>(m_Event)));
			return true;
		}

	private:
		Event& m_Event;
	};
}
