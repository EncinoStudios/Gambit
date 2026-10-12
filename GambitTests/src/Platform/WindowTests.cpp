#include <doctest/doctest.h>

#include "TestSupport.h"

#include "Gambit/Core/Application.h"
#include "Gambit/Core/Events/WindowEvent.h"
#include "Gambit/Core/FileSystem.h"
#include "Gambit/Core/LogSinks.h"
#include "Gambit/Platform/GLFW/GLFWContext.h"
#include "Gambit/Platform/Window.h"

#include <GLFW/glfw3.h>

#include <cmath>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(__linux__)
#	include <stdlib.h>
#endif

using namespace Gambit;

// These tests open real (hidden) windows. They live in the doctest test suite "window", which scripts/test.py runs on a
// virtual display (xvfb-run) when there is no display, and they return early, passing, when windows cannot be opened.

namespace
{
	WindowSpecification HiddenWindow(std::string title = "Gambit window test")
	{
		WindowSpecification specification;
		specification.Title = std::move(title);
		specification.Width = 320;
		specification.Height = 200;
		specification.Visible = false;
		return specification;
	}

	// Whether windows can be opened in this environment (probed once, silently).
	bool CanOpenWindows()
	{
		static const bool available = [] {
			LogConfig quiet = Tests::MakeTestLogConfig();
			quiet.Level = LogLevel::Off;
			quiet.Console = false;
			Tests::ScopedLogInit muted(quiet);
			return Window::Create(HiddenWindow()).HasValue();
		}();
		return available;
	}

#define GB_REQUIRE_WINDOWS()                                                                                           \
	do                                                                                                                 \
	{                                                                                                                  \
		if (!CanOpenWindows())                                                                                         \
		{                                                                                                              \
			MESSAGE("skipped: no window can be opened in this environment (no display)");                              \
			return;                                                                                                    \
		}                                                                                                              \
	} while (false)

	Scope<Window> OpenWindow(const WindowSpecification& specification = HiddenWindow())
	{
		Result<Scope<Window>> window = Window::Create(specification);
		const std::string problem = window.HasValue() ? std::string() : window.GetError().Message;
		REQUIRE_MESSAGE(window.HasValue(), problem);
		return std::move(*window);
	}

	GLFWwindow* Handle(Window& window)
	{
		return static_cast<GLFWwindow*>(window.GetNativeHandle());
	}

	// Collects the events of a window as descriptions.
	struct EventLog
	{
		std::vector<std::string> Descriptions;
		std::vector<EventType> Types;

		void Attach(Window& window)
		{
			window.SetEventCallback([this](Event& event) {
				Descriptions.push_back(event.ToString());
				Types.push_back(event.GetEventType());
			});
		}
	};
}

TEST_CASE("Platform: Window opens with the requested properties" * doctest::test_suite("window"))
{
	GB_REQUIRE_WINDOWS();
	WindowSpecification specification = HiddenWindow("Test Title");
	specification.VSync = false;
	specification.Resizable = false;
	specification.Decorated = false;
	const Scope<Window> window = OpenWindow(specification);

	CHECK(window->GetTitle() == "Test Title");
	CHECK(window->GetSize() == glm::uvec2(320, 200));
	CHECK(window->GetFramebufferSize().x >= 320);
	CHECK(window->GetFramebufferSize().y >= 200);
	CHECK(window->GetContentScale().x >= 1.0f);
	CHECK(window->GetContentScale().y >= 1.0f);
	CHECK_FALSE(window->IsVSync());
	CHECK_FALSE(window->ShouldClose());
	CHECK_FALSE(window->IsMinimized());
	CHECK(window->GetNativeHandle() != nullptr);
	CHECK(window->GetCursorMode() == CursorMode::Normal);

	GLFWwindow* handle = Handle(*window);
	CHECK(glfwGetWindowAttrib(handle, GLFW_RESIZABLE) == GLFW_FALSE);
	CHECK(glfwGetWindowAttrib(handle, GLFW_DECORATED) == GLFW_FALSE);
	CHECK(glfwGetWindowAttrib(handle, GLFW_VISIBLE) == GLFW_FALSE);
	CHECK(glfwGetWindowAttrib(handle, GLFW_CLIENT_API) == GLFW_NO_API); // Vulkan comes later: no OpenGL context
}

TEST_CASE("Platform: Window title, vsync flag and close request" * doctest::test_suite("window"))
{
	GB_REQUIRE_WINDOWS();
	const Scope<Window> window = OpenWindow();

	window->SetTitle("Another title \xC3\xA9");
	CHECK(window->GetTitle() == "Another title \xC3\xA9");
	CHECK(std::string(glfwGetWindowTitle(Handle(*window))) == "Another title \xC3\xA9");

	CHECK(window->IsVSync());
	window->SetVSync(false);
	CHECK_FALSE(window->IsVSync());

	CHECK_FALSE(window->ShouldClose());
	window->SetShouldClose(true);
	CHECK(window->ShouldClose());
	window->SetShouldClose(false);
	CHECK_FALSE(window->ShouldClose());
}

TEST_CASE("Platform: Window cursor modes" * doctest::test_suite("window"))
{
	GB_REQUIRE_WINDOWS();
	const Scope<Window> window = OpenWindow();
	GLFWwindow* handle = Handle(*window);

	window->SetCursorMode(CursorMode::Hidden);
	CHECK(window->GetCursorMode() == CursorMode::Hidden);
	CHECK(glfwGetInputMode(handle, GLFW_CURSOR) == GLFW_CURSOR_HIDDEN);

	window->SetCursorMode(CursorMode::Locked);
	CHECK(window->GetCursorMode() == CursorMode::Locked);
	CHECK(glfwGetInputMode(handle, GLFW_CURSOR) == GLFW_CURSOR_DISABLED);

	window->SetCursorMode(CursorMode::Normal);
	CHECK(window->GetCursorMode() == CursorMode::Normal);
	CHECK(glfwGetInputMode(handle, GLFW_CURSOR) == GLFW_CURSOR_NORMAL);
}

TEST_CASE("Platform: Window clipboard" * doctest::test_suite("window"))
{
	GB_REQUIRE_WINDOWS();
	const Scope<Window> window = OpenWindow();
	window->SetClipboardText("Gambit clipboard \xC3\xA9\xE2\x9C\x93");
	CHECK(window->GetClipboardText() == "Gambit clipboard \xC3\xA9\xE2\x9C\x93");
	window->SetClipboardText("");
	CHECK(window->GetClipboardText().empty());
}

TEST_CASE("Platform: Window reports no input on a fresh window" * doctest::test_suite("window"))
{
	GB_REQUIRE_WINDOWS();
	const Scope<Window> window = OpenWindow();
	for (size_t index = 1; index < KeyCodeCount; ++index)
	{
		CHECK_FALSE(window->IsKeyDown(static_cast<KeyCode>(index)));
	}
	CHECK_FALSE(window->IsKeyDown(KeyCode::None));
	for (size_t index = 0; index < MouseButtonCount; ++index)
	{
		CHECK_FALSE(window->IsMouseButtonDown(static_cast<MouseButton>(index)));
	}
	CHECK_FALSE(window->IsMouseButtonDown(MouseButton::Count));
	const glm::dvec2 cursor = window->GetCursorPosition();
	CHECK(std::isfinite(cursor.x));
	CHECK(std::isfinite(cursor.y));

	GamepadState state;
	CHECK_FALSE(window->GetGamepadState(static_cast<uint32_t>(MaxGamepads), state)); // only four slots exist
	CHECK_FALSE(window->GetGamepadState(1000, state));
}

TEST_CASE("Platform: GLFW callbacks become engine events" * doctest::test_suite("window"))
{
	GB_REQUIRE_WINDOWS();
	const Scope<Window> window = OpenWindow();
	GLFWwindow* handle = Handle(*window);
	EventLog events;
	events.Attach(*window);

	// The window installed its callbacks; fetching them lets the test deliver synthetic input without an OS.
	const auto keyCallback = glfwSetKeyCallback(handle, nullptr);
	glfwSetKeyCallback(handle, keyCallback);
	keyCallback(handle, GLFW_KEY_W, 17, GLFW_PRESS, 0);
	keyCallback(handle, GLFW_KEY_W, 17, GLFW_REPEAT, 0);
	keyCallback(handle, GLFW_KEY_W, 17, GLFW_RELEASE, 0);
	keyCallback(handle, GLFW_KEY_LEFT_SHIFT, 42, GLFW_PRESS, GLFW_MOD_SHIFT);
	keyCallback(handle, GLFW_KEY_UNKNOWN, 99, GLFW_PRESS, 0); // no engine key: dropped
	keyCallback(handle, 999, 0, GLFW_PRESS, 0);

	const auto charCallback = glfwSetCharCallback(handle, nullptr);
	glfwSetCharCallback(handle, charCallback);
	charCallback(handle, 0x1F600);

	const auto cursorCallback = glfwSetCursorPosCallback(handle, nullptr);
	glfwSetCursorPosCallback(handle, cursorCallback);
	cursorCallback(handle, 12.5, 40.0);

	const auto scrollCallback = glfwSetScrollCallback(handle, nullptr);
	glfwSetScrollCallback(handle, scrollCallback);
	scrollCallback(handle, 0.0, -1.0);

	const auto buttonCallback = glfwSetMouseButtonCallback(handle, nullptr);
	glfwSetMouseButtonCallback(handle, buttonCallback);
	buttonCallback(handle, GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
	buttonCallback(handle, GLFW_MOUSE_BUTTON_RIGHT, GLFW_RELEASE, 0);
	buttonCallback(handle, 20, GLFW_PRESS, 0); // not a mouse button

	CHECK(
		events.Descriptions
		== std::vector<std::string>{ "KeyPressed (key W)", "KeyPressed (key W, repeat)", "KeyReleased (key W)",
	                                 "KeyPressed (key LeftShift)", "KeyTyped (U+1F600)", "MouseMoved (12.5, 40)",
	                                 "MouseScrolled (0, -1)", "MouseButtonPressed (Left)",
	                                 "MouseButtonReleased (Right)" });
}

TEST_CASE("Platform: GLFW window callbacks become window events" * doctest::test_suite("window"))
{
	GB_REQUIRE_WINDOWS();
	const Scope<Window> window = OpenWindow();
	GLFWwindow* handle = Handle(*window);
	EventLog events;
	events.Attach(*window);

	const auto sizeCallback = glfwSetWindowSizeCallback(handle, nullptr);
	glfwSetWindowSizeCallback(handle, sizeCallback);
	sizeCallback(handle, 800, 600);
	sizeCallback(handle, -5, 0); // never negative

	const auto framebufferCallback = glfwSetFramebufferSizeCallback(handle, nullptr);
	glfwSetFramebufferSizeCallback(handle, framebufferCallback);
	framebufferCallback(handle, 1600, 1200);

	const auto scaleCallback = glfwSetWindowContentScaleCallback(handle, nullptr);
	glfwSetWindowContentScaleCallback(handle, scaleCallback);
	scaleCallback(handle, 2.0f, 1.5f);

	const auto focusCallback = glfwSetWindowFocusCallback(handle, nullptr);
	glfwSetWindowFocusCallback(handle, focusCallback);
	focusCallback(handle, GLFW_TRUE);
	focusCallback(handle, GLFW_FALSE);

	const auto dropCallback = glfwSetDropCallback(handle, nullptr);
	glfwSetDropCallback(handle, dropCallback);
	const char* dropped[] = { "/tmp/a.png", "/tmp/\xC3\xA9.glb" };
	dropCallback(handle, 2, dropped);

	const auto closeCallback = glfwSetWindowCloseCallback(handle, nullptr);
	glfwSetWindowCloseCallback(handle, closeCallback);
	closeCallback(handle);

	CHECK(
		events.Descriptions
		== std::vector<std::string>{ "WindowResize (800x600)", "WindowResize (0x0)",
	                                 "WindowFramebufferResize (1600x1200)", "WindowContentScale (2, 1.5)",
	                                 "WindowFocus (gained)", "WindowFocus (lost)", "WindowFileDrop (2 files)",
	                                 "WindowClose" });
}

TEST_CASE("Platform: a handled close event cancels the close request" * doctest::test_suite("window"))
{
	GB_REQUIRE_WINDOWS();
	const Scope<Window> window = OpenWindow();
	GLFWwindow* handle = Handle(*window);
	const auto closeCallback = glfwSetWindowCloseCallback(handle, nullptr);
	glfwSetWindowCloseCallback(handle, closeCallback);

	bool veto = true;
	window->SetEventCallback([&veto](Event& event) {
		if (event.GetEventType() == EventType::WindowClose)
		{
			event.Handled = veto; // for example an unsaved-changes prompt
		}
	});

	glfwSetWindowShouldClose(handle, GLFW_TRUE); // what GLFW does just before it calls the close callback
	closeCallback(handle);
	CHECK_FALSE(window->ShouldClose());

	veto = false;
	glfwSetWindowShouldClose(handle, GLFW_TRUE);
	closeCallback(handle);
	CHECK(window->ShouldClose());
}

TEST_CASE("Platform: file drops carry UTF-8 paths" * doctest::test_suite("window"))
{
	GB_REQUIRE_WINDOWS();
	const Scope<Window> window = OpenWindow();
	GLFWwindow* handle = Handle(*window);
	std::vector<std::filesystem::path> paths;
	window->SetEventCallback([&paths](Event& event) {
		if (event.GetEventType() == EventType::WindowFileDrop)
		{
			paths = static_cast<WindowFileDropEvent&>(event).GetPaths();
		}
	});
	const auto dropCallback = glfwSetDropCallback(handle, nullptr);
	glfwSetDropCallback(handle, dropCallback);
	const char* dropped[] = { "a/b.txt", "\xC3\xA9\xC3\xA8.txt" };
	dropCallback(handle, 2, dropped);
	REQUIRE(paths.size() == 2);
	CHECK(paths[0] == std::filesystem::path("a/b.txt"));
	CHECK(FileSystem::ToUtf8(paths[1]) == "\xC3\xA9\xC3\xA8.txt");
}

TEST_CASE("Platform: an exception in the event handler does not escape into GLFW" * doctest::test_suite("window"))
{
	GB_REQUIRE_WINDOWS();
	LogConfig config = Tests::MakeTestLogConfig();
	config.Console = false;
	config.RingBufferCapacity = 16;
	Tests::ScopedLogInit logInit(config);

	const Scope<Window> window = OpenWindow();
	GLFWwindow* handle = Handle(*window);
	int calls = 0;
	window->SetEventCallback([&calls](Event&) {
		++calls;
		throw std::runtime_error("handler failed");
	});
	const auto keyCallback = glfwSetKeyCallback(handle, nullptr);
	glfwSetKeyCallback(handle, keyCallback);

	CHECK_NOTHROW(keyCallback(handle, GLFW_KEY_A, 0, GLFW_PRESS, 0));
	CHECK_NOTHROW(keyCallback(handle, GLFW_KEY_B, 0, GLFW_PRESS, 0));
	CHECK(calls == 2);
	bool logged = false;
	for (const LogEntry& entry : Log::GetRingBuffer()->Snapshot())
	{
		logged =
			logged || (entry.Level == LogLevel::Error && entry.Message.find("handler failed") != std::string::npos);
	}
	CHECK(logged);
}

TEST_CASE("Platform: joystick callbacks become gamepad events" * doctest::test_suite("window"))
{
	GB_REQUIRE_WINDOWS();
	const Scope<Window> window = OpenWindow();
	EventLog events;
	events.Attach(*window);

	const auto joystickCallback = glfwSetJoystickCallback(nullptr);
	REQUIRE(joystickCallback != nullptr);
	glfwSetJoystickCallback(joystickCallback);

	joystickCallback(GLFW_JOYSTICK_1, GLFW_DISCONNECTED);
	joystickCallback(GLFW_JOYSTICK_4, GLFW_DISCONNECTED);
	joystickCallback(GLFW_JOYSTICK_5, GLFW_DISCONNECTED); // the engine reads four gamepads
	joystickCallback(GLFW_JOYSTICK_2, GLFW_CONNECTED);    // nothing is plugged in: no gamepad mapping, no event
	CHECK(
		events.Descriptions
		== std::vector<std::string>{ "GamepadDisconnected (slot 0)", "GamepadDisconnected (slot 3)" });
}

TEST_CASE("Platform: GLFW is initialized once and terminated with the last window" * doctest::test_suite("window"))
{
	GB_REQUIRE_WINDOWS();
	REQUIRE(GLFWContext::GetReferenceCount() == 0);
	{
		Scope<Window> first = OpenWindow();
		CHECK(GLFWContext::GetReferenceCount() == 1);
		{
			const Scope<Window> second = OpenWindow();
			CHECK(GLFWContext::GetReferenceCount() == 2);
			CHECK(second->GetNativeHandle() != first->GetNativeHandle());
		}
		CHECK(GLFWContext::GetReferenceCount() == 1);
		first->SetTitle("still works"); // the library is still initialized
		CHECK(first->GetTitle() == "still works");
	}
	CHECK(GLFWContext::GetReferenceCount() == 0);
	const Scope<Window> again = OpenWindow(); // and it can be started again
	CHECK(GLFWContext::GetReferenceCount() == 1);

	REQUIRE(GLFWContext::Acquire().HasValue()); // direct users share the same count
	CHECK(GLFWContext::GetReferenceCount() == 2);
	GLFWContext::Release();
	CHECK(GLFWContext::GetReferenceCount() == 1);
}

TEST_CASE("Platform: Window creation validates the specification" * doctest::test_suite("window"))
{
	GB_REQUIRE_WINDOWS();
	for (const auto& [width, height] :
	     std::vector<std::pair<uint32_t, uint32_t>>{ { 0, 100 }, { 100, 0 }, { 16385, 100 }, { 100, 100000 } })
	{
		WindowSpecification specification = HiddenWindow();
		specification.Width = width;
		specification.Height = height;
		const Result<Scope<Window>> window = Window::Create(specification);
		CHECK_MESSAGE(!window.HasValue(), width, "x", height);
		if (!window.HasValue())
		{
			CHECK(window.GetError().Message.find("invalid window size") != std::string::npos);
		}
	}
	CHECK(GLFWContext::GetReferenceCount() == 0);
}

#if defined(__linux__)
TEST_CASE(
	"Platform: Window creation fails with an error, not an exit, without a display" * doctest::test_suite("window"))
{
	REQUIRE(GLFWContext::GetReferenceCount() == 0);
	const char* display = std::getenv("DISPLAY");
	const char* wayland = std::getenv("WAYLAND_DISPLAY");
	const std::string savedDisplay = display != nullptr ? display : "";
	const std::string savedWayland = wayland != nullptr ? wayland : "";
	unsetenv("DISPLAY");
	unsetenv("WAYLAND_DISPLAY");

	LogConfig quiet = Tests::MakeTestLogConfig();
	quiet.Level = LogLevel::Off;
	quiet.Console = false;
	Result<Scope<Window>> window = Result<Scope<Window>>(Error("not attempted"));
	{
		Tests::ScopedLogInit muted(quiet);
		window = Window::Create(HiddenWindow());
	}

	if (display != nullptr)
	{
		setenv("DISPLAY", savedDisplay.c_str(), 1);
	}
	if (wayland != nullptr)
	{
		setenv("WAYLAND_DISPLAY", savedWayland.c_str(), 1);
	}
	REQUIRE_FALSE(window.HasValue());
	CHECK(window.GetError().Message.find("GLFW") != std::string::npos);
	CHECK(GLFWContext::GetReferenceCount() == 0); // a failed start holds nothing
}
#endif

TEST_CASE("Platform: Application opens a window, polls events and runs frames" * doctest::test_suite("window"))
{
	GB_REQUIRE_WINDOWS();
	ApplicationSpecification specification;
	specification.Name = "WindowTestApplication";
	specification.Window = HiddenWindow("Application window");
	specification.FrameLimit = 6;
	Application application(specification);

	REQUIRE(application.Initialize().HasValue());
	CHECK_FALSE(application.IsHeadless());
	CHECK(application.GetWindow().GetTitle() == "Application window");
	CHECK(application.GetWindow().GetSize() == glm::uvec2(320, 200));

	CHECK(application.Run() == 0);
	CHECK(application.GetFrameCount() == 6);
	CHECK(application.GetTick() == 6);
	// The default source reads the devices: nothing is pressed on a virtual display.
	CHECK(application.GetInputState().GetFrame().Keys == std::array<uint64_t, 2>{});
}

TEST_CASE("Platform: Application fails cleanly when the window cannot be opened" * doctest::test_suite("window"))
{
	GB_REQUIRE_WINDOWS();
	Tests::ScopedTempDirectory directory;
	const std::filesystem::path before = std::filesystem::current_path();

	ApplicationSpecification specification;
	specification.WorkingDirectory = directory.GetPath();
	specification.Window = HiddenWindow();
	specification.Window.Width = 0; // invalid
	LogConfig quiet = Tests::MakeTestLogConfig();
	quiet.Console = false;
	Tests::ScopedLogInit muted(quiet);

	Application application(specification);
	const Result<void> initialized = application.Initialize();
	REQUIRE_FALSE(initialized.HasValue());
	CHECK(initialized.GetError().Message.find("cannot open the window") != std::string::npos);
	CHECK_FALSE(application.IsInitialized());
	CHECK(std::filesystem::current_path() == before); // a failed Initialize leaves the working directory alone
	CHECK(application.Run() == 1);
	CHECK(std::filesystem::current_path() == before);
}
