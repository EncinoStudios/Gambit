#include "gbpch.h"

#include "Gambit/Platform/GLFW/GLFWWindow.h"

#include "Gambit/Core/Events/GamepadEvent.h"
#include "Gambit/Core/Events/KeyEvent.h"
#include "Gambit/Core/Events/MouseEvent.h"
#include "Gambit/Core/Events/WindowEvent.h"
#include "Gambit/Core/FileSystem.h"
#include "Gambit/Platform/GLFW/GLFWContext.h"
#include "Gambit/Platform/GLFW/GLFWKeyMap.h"

#include <GLFW/glfw3.h>

namespace Gambit
{
	namespace
	{
		constexpr uint32_t MaxWindowExtent = 16384;

		// Windows that exist, for the global joystick callback. Only touched on the main thread (GLFW's rule).
		std::vector<GLFWWindow*> s_Windows;

		uint32_t ToExtent(int value) noexcept
		{
			return value > 0 ? static_cast<uint32_t>(value) : 0;
		}

		int ToGLFWCursorMode(CursorMode mode) noexcept
		{
			switch (mode)
			{
				case CursorMode::Normal:
					return GLFW_CURSOR_NORMAL;
				case CursorMode::Hidden:
					return GLFW_CURSOR_HIDDEN;
				case CursorMode::Locked:
					return GLFW_CURSOR_DISABLED;
			}
			return GLFW_CURSOR_NORMAL;
		}
	}

	// The GLFW callbacks translate to engine events. They are static members of a friend so that they can reach
	// GLFWWindow::Dispatch.
	struct GLFWCallbacks
	{
		static GLFWWindow* Get(GLFWwindow* handle)
		{
			return static_cast<GLFWWindow*>(glfwGetWindowUserPointer(handle));
		}

		static void OnClose(GLFWwindow* handle)
		{
			WindowCloseEvent event;
			Get(handle)->Dispatch(event);
			if (event.Handled)
			{
				glfwSetWindowShouldClose(handle, GLFW_FALSE); // GLFW already set the flag; a veto takes it back
			}
		}

		static void OnSize(GLFWwindow* handle, int width, int height)
		{
			WindowResizeEvent event(ToExtent(width), ToExtent(height));
			Get(handle)->Dispatch(event);
		}

		static void OnFramebufferSize(GLFWwindow* handle, int width, int height)
		{
			WindowFramebufferResizeEvent event(ToExtent(width), ToExtent(height));
			Get(handle)->Dispatch(event);
		}

		static void OnContentScale(GLFWwindow* handle, float scaleX, float scaleY)
		{
			WindowContentScaleEvent event(scaleX, scaleY);
			Get(handle)->Dispatch(event);
		}

		static void OnFocus(GLFWwindow* handle, int focused)
		{
			WindowFocusEvent event(focused == GLFW_TRUE);
			Get(handle)->Dispatch(event);
		}

		static void OnDrop(GLFWwindow* handle, int count, const char** paths)
		{
			std::vector<std::filesystem::path> files;
			for (int i = 0; i < count; ++i)
			{
				files.push_back(FileSystem::FromUtf8(paths[i]));
			}
			WindowFileDropEvent event(std::move(files));
			Get(handle)->Dispatch(event);
		}

		static void OnKey(GLFWwindow* handle, int glfwKey, int /*scancode*/, int action, int /*mods*/)
		{
			const KeyCode key = KeyCodeFromGLFW(glfwKey);
			if (key == KeyCode::None)
			{
				return;
			}
			if (action == GLFW_RELEASE)
			{
				KeyReleasedEvent event(key);
				Get(handle)->Dispatch(event);
			}
			else
			{
				KeyPressedEvent event(key, action == GLFW_REPEAT);
				Get(handle)->Dispatch(event);
			}
		}

		static void OnChar(GLFWwindow* handle, unsigned int codepoint)
		{
			KeyTypedEvent event(codepoint);
			Get(handle)->Dispatch(event);
		}

		static void OnCursorPosition(GLFWwindow* handle, double x, double y)
		{
			MouseMovedEvent event(static_cast<float>(x), static_cast<float>(y));
			Get(handle)->Dispatch(event);
		}

		static void OnScroll(GLFWwindow* handle, double offsetX, double offsetY)
		{
			MouseScrolledEvent event(static_cast<float>(offsetX), static_cast<float>(offsetY));
			Get(handle)->Dispatch(event);
		}

		static void OnMouseButton(GLFWwindow* handle, int glfwButton, int action, int /*mods*/)
		{
			const std::optional<MouseButton> button = MouseButtonFromGLFW(glfwButton);
			if (!button)
			{
				return;
			}
			if (action == GLFW_PRESS)
			{
				MouseButtonPressedEvent event(*button);
				Get(handle)->Dispatch(event);
			}
			else
			{
				MouseButtonReleasedEvent event(*button);
				Get(handle)->Dispatch(event);
			}
		}

		// Joysticks are global in GLFW: every window hears about them. Only the first MaxGamepads joysticks are used.
		static void OnJoystick(int joystick, int connection)
		{
			const int slot = joystick - GLFW_JOYSTICK_1;
			if (slot < 0 || slot >= static_cast<int>(MaxGamepads))
			{
				return;
			}
			if (connection == GLFW_CONNECTED)
			{
				if (glfwJoystickIsGamepad(joystick) != GLFW_TRUE)
				{
					return; // a joystick without a gamepad mapping cannot be read as a gamepad
				}
				const char* name = glfwGetGamepadName(joystick);
				GamepadConnectedEvent event(static_cast<uint32_t>(slot), name != nullptr ? name : "");
				for (GLFWWindow* window : std::vector<GLFWWindow*>(s_Windows))
				{
					window->Dispatch(event);
				}
			}
			else if (connection == GLFW_DISCONNECTED)
			{
				GamepadDisconnectedEvent event(static_cast<uint32_t>(slot));
				for (GLFWWindow* window : std::vector<GLFWWindow*>(s_Windows))
				{
					window->Dispatch(event);
				}
			}
		}
	};

	Result<Scope<GLFWWindow>> GLFWWindow::Create(const WindowSpecification& specification)
	{
		if (specification.Width == 0 || specification.Height == 0 || specification.Width > MaxWindowExtent
		    || specification.Height > MaxWindowExtent)
		{
			return Error(std::format(
				"invalid window size {}x{} (1 to {} in each direction)", specification.Width, specification.Height,
				MaxWindowExtent));
		}
		if (const Result<void> context = GLFWContext::Acquire(); !context)
		{
			return context.GetError();
		}

		glfwDefaultWindowHints();
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		glfwWindowHint(GLFW_RESIZABLE, specification.Resizable ? GLFW_TRUE : GLFW_FALSE);
		glfwWindowHint(GLFW_DECORATED, specification.Decorated ? GLFW_TRUE : GLFW_FALSE);
		glfwWindowHint(GLFW_MAXIMIZED, specification.Maximized ? GLFW_TRUE : GLFW_FALSE);
		glfwWindowHint(GLFW_VISIBLE, specification.Visible ? GLFW_TRUE : GLFW_FALSE);

		GLFWmonitor* monitor = nullptr;
		int width = static_cast<int>(specification.Width);
		int height = static_cast<int>(specification.Height);
		if (specification.Fullscreen)
		{
			monitor = glfwGetPrimaryMonitor();
			const GLFWvidmode* mode = monitor != nullptr ? glfwGetVideoMode(monitor) : nullptr;
			if (mode == nullptr)
			{
				GLFWContext::Release();
				return Error("cannot go fullscreen: no primary monitor or video mode");
			}
			// The monitor's current mode keeps the desktop resolution (a borderless fullscreen window).
			glfwWindowHint(GLFW_RED_BITS, mode->redBits);
			glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
			glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
			glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);
			width = mode->width;
			height = mode->height;
		}

		GLFWwindow* handle = glfwCreateWindow(width, height, specification.Title.c_str(), monitor, nullptr);
		if (handle == nullptr)
		{
			const char* description = nullptr;
			glfwGetError(&description);
			GLFWContext::Release();
			return Error(std::format(
				"cannot create the window '{}': {}", specification.Title,
				description != nullptr ? description : "unknown error"));
		}

		Scope<GLFWWindow> window = CreateScope<GLFWWindow>(PrivateTag{}, handle, specification);
		window->InstallCallbacks();
		return window;
	}

	GLFWWindow::GLFWWindow(PrivateTag, GLFWwindow* handle, const WindowSpecification& specification)
		: m_Handle(handle), m_Title(specification.Title), m_VSync(specification.VSync)
	{
		s_Windows.push_back(this);
	}

	GLFWWindow::~GLFWWindow()
	{
		std::erase(s_Windows, this);
		glfwDestroyWindow(m_Handle);
		GLFWContext::Release();
	}

	void GLFWWindow::InstallCallbacks()
	{
		glfwSetWindowUserPointer(m_Handle, this);
		glfwSetWindowCloseCallback(m_Handle, GLFWCallbacks::OnClose);
		glfwSetWindowSizeCallback(m_Handle, GLFWCallbacks::OnSize);
		glfwSetFramebufferSizeCallback(m_Handle, GLFWCallbacks::OnFramebufferSize);
		glfwSetWindowContentScaleCallback(m_Handle, GLFWCallbacks::OnContentScale);
		glfwSetWindowFocusCallback(m_Handle, GLFWCallbacks::OnFocus);
		glfwSetDropCallback(m_Handle, GLFWCallbacks::OnDrop);
		glfwSetKeyCallback(m_Handle, GLFWCallbacks::OnKey);
		glfwSetCharCallback(m_Handle, GLFWCallbacks::OnChar);
		glfwSetCursorPosCallback(m_Handle, GLFWCallbacks::OnCursorPosition);
		glfwSetScrollCallback(m_Handle, GLFWCallbacks::OnScroll);
		glfwSetMouseButtonCallback(m_Handle, GLFWCallbacks::OnMouseButton);
		glfwSetJoystickCallback(GLFWCallbacks::OnJoystick);
	}

	void GLFWWindow::Dispatch(Event& event)
	{
		if (!m_EventCallback)
		{
			return;
		}
		try
		{
			m_EventCallback(event);
		}
		catch (const std::exception& exception)
		{
			GB_CORE_ERROR("An event handler threw while handling {}: {}", event.ToString(), exception.what());
		}
		catch (...)
		{
			GB_CORE_ERROR("An event handler threw an unknown exception while handling {}", event.ToString());
		}
	}

	void GLFWWindow::PollEvents()
	{
		glfwPollEvents();
	}

	bool GLFWWindow::ShouldClose() const
	{
		return glfwWindowShouldClose(m_Handle) == GLFW_TRUE;
	}

	void GLFWWindow::SetShouldClose(bool close)
	{
		glfwSetWindowShouldClose(m_Handle, close ? GLFW_TRUE : GLFW_FALSE);
	}

	glm::uvec2 GLFWWindow::GetSize() const
	{
		int width = 0;
		int height = 0;
		glfwGetWindowSize(m_Handle, &width, &height);
		return { ToExtent(width), ToExtent(height) };
	}

	glm::uvec2 GLFWWindow::GetFramebufferSize() const
	{
		int width = 0;
		int height = 0;
		glfwGetFramebufferSize(m_Handle, &width, &height);
		return { ToExtent(width), ToExtent(height) };
	}

	glm::vec2 GLFWWindow::GetContentScale() const
	{
		float scaleX = 1.0f;
		float scaleY = 1.0f;
		glfwGetWindowContentScale(m_Handle, &scaleX, &scaleY);
		return { scaleX, scaleY };
	}

	bool GLFWWindow::IsMinimized() const
	{
		return glfwGetWindowAttrib(m_Handle, GLFW_ICONIFIED) == GLFW_TRUE;
	}

	bool GLFWWindow::IsFocused() const
	{
		return glfwGetWindowAttrib(m_Handle, GLFW_FOCUSED) == GLFW_TRUE;
	}

	void GLFWWindow::SetTitle(std::string_view title)
	{
		m_Title = title;
		glfwSetWindowTitle(m_Handle, m_Title.c_str());
	}

	void GLFWWindow::SetCursorMode(CursorMode mode)
	{
		glfwSetInputMode(m_Handle, GLFW_CURSOR, ToGLFWCursorMode(mode));
		if (mode == CursorMode::Locked && glfwRawMouseMotionSupported() == GLFW_TRUE)
		{
			glfwSetInputMode(m_Handle, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE); // movement without pointer acceleration
		}
		m_CursorMode = mode;
	}

	std::string GLFWWindow::GetClipboardText() const
	{
		const char* text = glfwGetClipboardString(m_Handle);
		return text != nullptr ? text : "";
	}

	void GLFWWindow::SetClipboardText(std::string_view text)
	{
		glfwSetClipboardString(m_Handle, std::string(text).c_str());
	}

	bool GLFWWindow::IsKeyDown(KeyCode key) const
	{
		const int glfwKey = KeyCodeToGLFW(key);
		return glfwKey != GLFW_KEY_UNKNOWN && glfwGetKey(m_Handle, glfwKey) == GLFW_PRESS;
	}

	bool GLFWWindow::IsMouseButtonDown(MouseButton button) const
	{
		const int glfwButton = MouseButtonToGLFW(button);
		return glfwButton >= 0 && glfwGetMouseButton(m_Handle, glfwButton) == GLFW_PRESS;
	}

	glm::dvec2 GLFWWindow::GetCursorPosition() const
	{
		double x = 0.0;
		double y = 0.0;
		glfwGetCursorPos(m_Handle, &x, &y);
		return { x, y };
	}

	bool GLFWWindow::GetGamepadState(uint32_t slot, GamepadState& state) const
	{
		if (slot >= MaxGamepads)
		{
			return false;
		}
		GLFWgamepadstate raw{};
		if (glfwGetGamepadState(GLFW_JOYSTICK_1 + static_cast<int>(slot), &raw) != GLFW_TRUE)
		{
			return false;
		}
		for (size_t button = 0; button < GamepadButtonCount; ++button)
		{
			state.Buttons[button] = raw.buttons[button] == GLFW_PRESS;
		}
		for (size_t axis = 0; axis < GamepadAxisCount; ++axis)
		{
			state.Axes[axis] = raw.axes[axis];
		}
		// GLFW reports the triggers from -1 (released) to 1 (fully pressed); the engine uses 0 to 1.
		for (const GamepadAxis trigger : { GamepadAxis::LeftTrigger, GamepadAxis::RightTrigger })
		{
			float& value = state.Axes[static_cast<size_t>(trigger)];
			value = std::clamp((value + 1.0f) * 0.5f, 0.0f, 1.0f);
		}
		return true;
	}
}
