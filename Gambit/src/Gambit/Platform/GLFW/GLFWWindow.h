#pragma once

#include "Gambit/Platform/Window.h"

struct GLFWwindow;

namespace Gambit
{
	// The Window backend on top of GLFW (no graphics API; Vulkan surfaces are created from the native handle later).
	class GLFWWindow final : public Window
	{
	public:
		[[nodiscard]] static Result<Scope<GLFWWindow>> Create(const WindowSpecification& specification);

		~GLFWWindow() override;
		GLFWWindow(const GLFWWindow&) = delete;
		GLFWWindow& operator=(const GLFWWindow&) = delete;

		void SetEventCallback(EventCallback callback) override { m_EventCallback = std::move(callback); }
		void PollEvents() override;
		[[nodiscard]] bool ShouldClose() const override;
		void SetShouldClose(bool close) override;
		[[nodiscard]] glm::uvec2 GetSize() const override;
		[[nodiscard]] glm::uvec2 GetFramebufferSize() const override;
		[[nodiscard]] glm::vec2 GetContentScale() const override;
		[[nodiscard]] bool IsMinimized() const override;
		[[nodiscard]] bool IsFocused() const override;
		[[nodiscard]] const std::string& GetTitle() const override { return m_Title; }
		void SetTitle(std::string_view title) override;
		[[nodiscard]] bool IsVSync() const override { return m_VSync; }
		void SetVSync(bool enabled) override { m_VSync = enabled; }
		void SetCursorMode(CursorMode mode) override;
		[[nodiscard]] std::string GetClipboardText() const override;
		void SetClipboardText(std::string_view text) override;
		[[nodiscard]] void* GetNativeHandle() const override { return m_Handle; }

		[[nodiscard]] bool IsKeyDown(KeyCode key) const override;
		[[nodiscard]] bool IsMouseButtonDown(MouseButton button) const override;
		[[nodiscard]] glm::dvec2 GetCursorPosition() const override;
		[[nodiscard]] CursorMode GetCursorMode() const override { return m_CursorMode; }
		[[nodiscard]] bool GetGamepadState(uint32_t slot, GamepadState& state) const override;

		// Use Create; the tag keeps the constructor out of reach of other code while allowing CreateScope.
		struct PrivateTag
		{
			explicit PrivateTag() = default;
		};
		GLFWWindow(PrivateTag, GLFWwindow* handle, const WindowSpecification& specification);

	private:
		friend struct GLFWCallbacks;

		// Delivers an event to the callback; a throwing callback is logged, never propagated into GLFW.
		void Dispatch(Event& event);
		void InstallCallbacks();

		GLFWwindow* m_Handle;
		std::string m_Title;
		bool m_VSync;
		CursorMode m_CursorMode = CursorMode::Normal;
		EventCallback m_EventCallback;
	};
}
