#include "gbpch.h"

#include "Gambit/Core/Events/Event.h"

namespace Gambit
{
	std::string_view ToString(EventType type) noexcept
	{
		switch (type)
		{
			case EventType::None:
				return "None";
			case EventType::WindowClose:
				return "WindowClose";
			case EventType::WindowResize:
				return "WindowResize";
			case EventType::WindowFocus:
				return "WindowFocus";
			case EventType::WindowFramebufferResize:
				return "WindowFramebufferResize";
			case EventType::WindowContentScale:
				return "WindowContentScale";
			case EventType::WindowFileDrop:
				return "WindowFileDrop";
			case EventType::KeyPressed:
				return "KeyPressed";
			case EventType::KeyReleased:
				return "KeyReleased";
			case EventType::KeyTyped:
				return "KeyTyped";
			case EventType::MouseMoved:
				return "MouseMoved";
			case EventType::MouseScrolled:
				return "MouseScrolled";
			case EventType::MouseButtonPressed:
				return "MouseButtonPressed";
			case EventType::MouseButtonReleased:
				return "MouseButtonReleased";
			case EventType::GamepadConnected:
				return "GamepadConnected";
			case EventType::GamepadDisconnected:
				return "GamepadDisconnected";
		}
		return "Unknown";
	}

	std::string Event::ToString() const
	{
		return std::string(Gambit::ToString(GetEventType()));
	}
}
