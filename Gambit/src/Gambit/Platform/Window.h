#pragma once

#include "Gambit/Core/Base.h"
#include "Gambit/Core/Events/Event.h"
#include "Gambit/Core/Result.h"
#include "Gambit/Input/DeviceState.h"
#include "Gambit/Math/Math.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace Gambit
{
	struct WindowSpecification
	{
		std::string Title = "Gambit";
		uint32_t Width = 1280; // client area in screen coordinates; ignored for Fullscreen (the monitor's mode is used)
		uint32_t Height = 720;
		bool VSync = true;
		bool Resizable = true;
		bool Fullscreen = false;
		bool Decorated = true;
		bool Maximized = false;
		bool Visible = true; // false creates the window hidden (tools and tests)
	};

	// An operating system window without a graphics context: the renderer attaches a surface to the native handle
	// (D-030). It reports what happens to it as events and lets the engine poll the input devices (IDeviceState). All
	// functions must be called from the thread that created the window, normally the main thread.
	class Window : public IDeviceState
	{
	public:
		using EventCallback = std::function<void(Event&)>;

		~Window() override = default;

		// Opens a window. Fails (never terminates the process) when the platform layer cannot start, for example
		// without a display, or the window cannot be created.
		[[nodiscard]] static Result<Scope<Window>> Create(const WindowSpecification& specification);

		// Receives every event PollEvents produces, in order. An exception thrown by the callback is logged and
		// dropped.
		virtual void SetEventCallback(EventCallback callback) = 0;
		// Processes the pending operating system events, calling the event callback for each.
		virtual void PollEvents() = 0;

		[[nodiscard]] virtual bool ShouldClose() const = 0;
		virtual void SetShouldClose(bool close) = 0;

		// Window size in screen coordinates and framebuffer size in pixels (they differ on high-DPI displays).
		[[nodiscard]] virtual glm::uvec2 GetSize() const = 0;
		[[nodiscard]] virtual glm::uvec2 GetFramebufferSize() const = 0;
		// Pixels per screen coordinate of the monitor the window is on (the DPI scale; 1 or larger on most systems).
		[[nodiscard]] virtual glm::vec2 GetContentScale() const = 0;
		[[nodiscard]] virtual bool IsMinimized() const = 0;
		[[nodiscard]] virtual bool IsFocused() const = 0;

		[[nodiscard]] virtual const std::string& GetTitle() const = 0;
		virtual void SetTitle(std::string_view title) = 0;

		// The window has no swap chain yet: the setting is kept for the renderer's presentation (T07) to read.
		[[nodiscard]] virtual bool IsVSync() const = 0;
		virtual void SetVSync(bool enabled) = 0;

		virtual void SetCursorMode(CursorMode mode) = 0;

		[[nodiscard]] virtual std::string GetClipboardText() const = 0;
		virtual void SetClipboardText(std::string_view text) = 0;

		// The platform's window handle (a GLFWwindow* for the GLFW backend) for code that talks to the platform API.
		[[nodiscard]] virtual void* GetNativeHandle() const = 0;
	};
}
