#include <doctest/doctest.h>

#include "Gambit/Core/Events/GamepadEvent.h"
#include "Gambit/Core/Events/KeyEvent.h"
#include "Gambit/Core/Events/MouseEvent.h"
#include "Gambit/Core/Events/WindowEvent.h"

#include <algorithm>
#include <memory>
#include <vector>

using namespace Gambit;

TEST_CASE("Core: events report their static and dynamic type")
{
	static_assert(KeyPressedEvent::GetStaticType() == EventType::KeyPressed);
	static_assert(WindowCloseEvent::GetStaticType() == EventType::WindowClose);

	const KeyPressedEvent pressed(KeyCode::W);
	CHECK(pressed.GetEventType() == EventType::KeyPressed);
	const Event& base = pressed;
	CHECK(base.GetEventType() == KeyPressedEvent::GetStaticType());

	std::vector<std::unique_ptr<Event>> events;
	events.push_back(std::make_unique<WindowCloseEvent>());
	events.push_back(std::make_unique<WindowResizeEvent>(1, 2));
	events.push_back(std::make_unique<WindowFocusEvent>(true));
	events.push_back(std::make_unique<WindowFramebufferResizeEvent>(1, 2));
	events.push_back(std::make_unique<WindowContentScaleEvent>(1.0f, 2.0f));
	events.push_back(std::make_unique<WindowFileDropEvent>(std::vector<std::filesystem::path>{}));
	events.push_back(std::make_unique<KeyPressedEvent>(KeyCode::A));
	events.push_back(std::make_unique<KeyReleasedEvent>(KeyCode::A));
	events.push_back(std::make_unique<KeyTypedEvent>(65));
	events.push_back(std::make_unique<MouseMovedEvent>(1.0f, 2.0f));
	events.push_back(std::make_unique<MouseScrolledEvent>(1.0f, 2.0f));
	events.push_back(std::make_unique<MouseButtonPressedEvent>(MouseButton::Left));
	events.push_back(std::make_unique<MouseButtonReleasedEvent>(MouseButton::Left));
	events.push_back(std::make_unique<GamepadConnectedEvent>(0, "Pad"));
	events.push_back(std::make_unique<GamepadDisconnectedEvent>(0));

	std::vector<EventType> types;
	for (const auto& event : events)
	{
		types.push_back(event->GetEventType());
		CHECK_FALSE(event->Handled);
		CHECK_FALSE(ToString(event->GetEventType()).empty());
		CHECK(event->ToString().starts_with(std::string(ToString(event->GetEventType()))));
	}
	std::sort(types.begin(), types.end());
	CHECK(std::adjacent_find(types.begin(), types.end()) == types.end()); // every class has its own type
	CHECK(types.size() == 15);
}

TEST_CASE("Core: events belong to categories")
{
	CHECK(WindowResizeEvent(1, 1).IsInCategory(EventCategory::Window));
	CHECK_FALSE(WindowResizeEvent(1, 1).IsInCategory(EventCategory::Input));
	CHECK(WindowFileDropEvent({}).IsInCategory(EventCategory::Window));

	const KeyPressedEvent key(KeyCode::A);
	CHECK(key.IsInCategory(EventCategory::Keyboard));
	CHECK(key.IsInCategory(EventCategory::Input));
	CHECK_FALSE(key.IsInCategory(EventCategory::Mouse));
	CHECK(KeyTypedEvent(65).IsInCategory(EventCategory::Keyboard));

	const MouseButtonPressedEvent button(MouseButton::Right);
	CHECK(button.IsInCategory(EventCategory::Mouse));
	CHECK(button.IsInCategory(EventCategory::MouseButton));
	CHECK(button.IsInCategory(EventCategory::Input));
	CHECK_FALSE(button.IsInCategory(EventCategory::Keyboard));
	CHECK(MouseMovedEvent(0, 0).IsInCategory(EventCategory::Mouse));
	CHECK_FALSE(MouseMovedEvent(0, 0).IsInCategory(EventCategory::MouseButton));

	CHECK(GamepadConnectedEvent(1, "x").IsInCategory(EventCategory::Gamepad));
	CHECK(GamepadDisconnectedEvent(1).IsInCategory(EventCategory::Input));

	CHECK(HasCategory(EventCategory::Keyboard | EventCategory::Input, EventCategory::Input));
	CHECK_FALSE(HasCategory(EventCategory::Keyboard, EventCategory::Input));
	CHECK_FALSE(HasCategory(EventCategory::None, EventCategory::Window));
}

TEST_CASE("Core: events carry their data")
{
	const WindowResizeEvent resize(800, 600);
	CHECK(resize.GetWidth() == 800);
	CHECK(resize.GetHeight() == 600);
	const WindowFramebufferResizeEvent framebuffer(1600, 1200);
	CHECK(framebuffer.GetWidth() == 1600);
	CHECK(framebuffer.GetHeight() == 1200);
	CHECK(WindowFocusEvent(true).IsFocused());
	CHECK_FALSE(WindowFocusEvent(false).IsFocused());
	const WindowContentScaleEvent scale(2.0f, 1.5f);
	CHECK(scale.GetScaleX() == 2.0f);
	CHECK(scale.GetScaleY() == 1.5f);
	const WindowFileDropEvent drop({ "a.png", "dir/b.glb" });
	REQUIRE(drop.GetPaths().size() == 2);
	CHECK(drop.GetPaths()[1] == std::filesystem::path("dir/b.glb"));

	const KeyPressedEvent first(KeyCode::Space);
	CHECK(first.GetKeyCode() == KeyCode::Space);
	CHECK_FALSE(first.IsRepeat());
	CHECK(KeyPressedEvent(KeyCode::Space, true).IsRepeat());
	CHECK(KeyReleasedEvent(KeyCode::Escape).GetKeyCode() == KeyCode::Escape);
	CHECK(KeyTypedEvent(0x1F600).GetCodepoint() == 0x1F600);

	const MouseMovedEvent moved(10.5f, 20.25f);
	CHECK(moved.GetX() == 10.5f);
	CHECK(moved.GetY() == 20.25f);
	const MouseScrolledEvent scrolled(0.0f, -1.0f);
	CHECK(scrolled.GetOffsetX() == 0.0f);
	CHECK(scrolled.GetOffsetY() == -1.0f);
	CHECK(MouseButtonPressedEvent(MouseButton::Middle).GetMouseButton() == MouseButton::Middle);
	CHECK(MouseButtonReleasedEvent(MouseButton::Left).GetMouseButton() == MouseButton::Left);

	const GamepadConnectedEvent connected(2, "Xbox Controller");
	CHECK(connected.GetSlot() == 2);
	CHECK(connected.GetName() == "Xbox Controller");
	CHECK(GamepadDisconnectedEvent(3).GetSlot() == 3);
}

TEST_CASE("Core: events describe themselves for logs")
{
	CHECK(KeyPressedEvent(KeyCode::W).ToString() == "KeyPressed (key W)");
	CHECK(KeyPressedEvent(KeyCode::W, true).ToString() == "KeyPressed (key W, repeat)");
	CHECK(KeyReleasedEvent(KeyCode::LeftShift).ToString() == "KeyReleased (key LeftShift)");
	CHECK(KeyTypedEvent(0x41).ToString() == "KeyTyped (U+0041)");
	CHECK(WindowResizeEvent(1280, 720).ToString() == "WindowResize (1280x720)");
	CHECK(WindowCloseEvent().ToString() == "WindowClose");
	CHECK(WindowFocusEvent(false).ToString() == "WindowFocus (lost)");
	CHECK(MouseButtonPressedEvent(MouseButton::Right).ToString() == "MouseButtonPressed (Right)");
	CHECK(WindowFileDropEvent({ "a", "b", "c" }).ToString() == "WindowFileDrop (3 files)");
	CHECK(GamepadConnectedEvent(1, "Pad").ToString() == "GamepadConnected (slot 1, 'Pad')");
	CHECK(GamepadDisconnectedEvent(1).ToString() == "GamepadDisconnected (slot 1)");
	CHECK(ToString(EventType::None) == "None");
	CHECK(ToString(static_cast<EventType>(200)) == "Unknown");
}

TEST_CASE("Core: EventDispatcher calls a handler only for the matching type")
{
	KeyPressedEvent event(KeyCode::E);
	EventDispatcher dispatcher(event);

	int released = 0;
	CHECK_FALSE(dispatcher.Dispatch<KeyReleasedEvent>([&](KeyReleasedEvent&) {
		++released;
		return true;
	}));
	CHECK(released == 0);
	CHECK_FALSE(event.Handled);

	KeyCode seen = KeyCode::None;
	CHECK(dispatcher.Dispatch<KeyPressedEvent>([&](KeyPressedEvent& pressed) {
		seen = pressed.GetKeyCode();
		return true;
	}));
	CHECK(seen == KeyCode::E);
	CHECK(event.Handled);
}

TEST_CASE("Core: EventDispatcher records whether the handler consumed the event")
{
	KeyPressedEvent event(KeyCode::Q);
	EventDispatcher dispatcher(event);
	CHECK(dispatcher.Dispatch<KeyPressedEvent>([](KeyPressedEvent&) { return false; }));
	CHECK_FALSE(event.Handled); // a handler that declines leaves the event for the layers below

	event.Handled = true;
	CHECK(dispatcher.Dispatch<KeyPressedEvent>([](KeyPressedEvent&) { return false; }));
	CHECK(event.Handled); // an event that was handled stays handled

	WindowResizeEvent resize(3, 4);
	EventDispatcher resizeDispatcher(resize);
	uint32_t width = 0;
	resizeDispatcher.Dispatch<WindowResizeEvent>([&](WindowResizeEvent& e) {
		width = e.GetWidth();
		return false;
	});
	CHECK(width == 3);
}

TEST_CASE("Core: EventDispatcher dispatches through the base class reference")
{
	std::unique_ptr<Event> event = std::make_unique<MouseScrolledEvent>(0.0f, 2.0f);
	EventDispatcher dispatcher(*event);
	float scrolled = 0.0f;
	dispatcher.Dispatch<MouseMovedEvent>([&](MouseMovedEvent&) { return true; });
	dispatcher.Dispatch<MouseScrolledEvent>([&](MouseScrolledEvent& e) {
		scrolled = e.GetOffsetY();
		return true;
	});
	CHECK(scrolled == 2.0f);
	CHECK(event->Handled);
}
