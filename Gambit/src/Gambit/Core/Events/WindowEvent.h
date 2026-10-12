#pragma once

#include "Gambit/Core/Events/Event.h"

#include <cstdint>
#include <filesystem>
#include <format>
#include <string>
#include <vector>

namespace Gambit
{
	// The user asked to close the window (title bar button, Alt+F4, ...).
	class WindowCloseEvent final : public Event
	{
	public:
		GB_EVENT_CLASS_TYPE(WindowClose)
		GB_EVENT_CLASS_CATEGORY(EventCategory::Window)
	};

	// The window's size in screen coordinates changed.
	class WindowResizeEvent final : public Event
	{
	public:
		WindowResizeEvent(uint32_t width, uint32_t height) noexcept
			: m_Width(width), m_Height(height)
		{}

		[[nodiscard]] uint32_t GetWidth() const noexcept { return m_Width; }
		[[nodiscard]] uint32_t GetHeight() const noexcept { return m_Height; }
		[[nodiscard]] std::string ToString() const override
		{
			return std::format("WindowResize ({}x{})", m_Width, m_Height);
		}

		GB_EVENT_CLASS_TYPE(WindowResize)
		GB_EVENT_CLASS_CATEGORY(EventCategory::Window)

	private:
		uint32_t m_Width;
		uint32_t m_Height;
	};

	// The window gained or lost input focus.
	class WindowFocusEvent final : public Event
	{
	public:
		explicit WindowFocusEvent(bool focused) noexcept
			: m_Focused(focused)
		{}

		[[nodiscard]] bool IsFocused() const noexcept { return m_Focused; }
		[[nodiscard]] std::string ToString() const override
		{
			return m_Focused ? "WindowFocus (gained)" : "WindowFocus (lost)";
		}

		GB_EVENT_CLASS_TYPE(WindowFocus)
		GB_EVENT_CLASS_CATEGORY(EventCategory::Window)

	private:
		bool m_Focused;
	};

	// The framebuffer size in pixels changed; this is the size to render at (differs from the window size on
	// high-DPI displays).
	class WindowFramebufferResizeEvent final : public Event
	{
	public:
		WindowFramebufferResizeEvent(uint32_t width, uint32_t height) noexcept
			: m_Width(width), m_Height(height)
		{}

		[[nodiscard]] uint32_t GetWidth() const noexcept { return m_Width; }
		[[nodiscard]] uint32_t GetHeight() const noexcept { return m_Height; }
		[[nodiscard]] std::string ToString() const override
		{
			return std::format("WindowFramebufferResize ({}x{})", m_Width, m_Height);
		}

		GB_EVENT_CLASS_TYPE(WindowFramebufferResize)
		GB_EVENT_CLASS_CATEGORY(EventCategory::Window)

	private:
		uint32_t m_Width;
		uint32_t m_Height;
	};

	// The window moved to a monitor with a different DPI scale, or the system scale changed.
	class WindowContentScaleEvent final : public Event
	{
	public:
		WindowContentScaleEvent(float scaleX, float scaleY) noexcept
			: m_ScaleX(scaleX), m_ScaleY(scaleY)
		{}

		[[nodiscard]] float GetScaleX() const noexcept { return m_ScaleX; }
		[[nodiscard]] float GetScaleY() const noexcept { return m_ScaleY; }
		[[nodiscard]] std::string ToString() const override
		{
			return std::format("WindowContentScale ({}, {})", m_ScaleX, m_ScaleY);
		}

		GB_EVENT_CLASS_TYPE(WindowContentScale)
		GB_EVENT_CLASS_CATEGORY(EventCategory::Window)

	private:
		float m_ScaleX;
		float m_ScaleY;
	};

	// Files were dropped onto the window.
	class WindowFileDropEvent final : public Event
	{
	public:
		explicit WindowFileDropEvent(std::vector<std::filesystem::path> paths)
			: m_Paths(std::move(paths))
		{}

		[[nodiscard]] const std::vector<std::filesystem::path>& GetPaths() const noexcept { return m_Paths; }
		[[nodiscard]] std::string ToString() const override
		{
			return std::format("WindowFileDrop ({} files)", m_Paths.size());
		}

		GB_EVENT_CLASS_TYPE(WindowFileDrop)
		GB_EVENT_CLASS_CATEGORY(EventCategory::Window)

	private:
		std::vector<std::filesystem::path> m_Paths;
	};
}
