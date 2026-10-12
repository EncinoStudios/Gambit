#pragma once

#include "Gambit/Input/KeyCode.h"

namespace Gambit
{
	// Translation between GLFW's key and mouse button numbers and the engine's compact codes (the engine's numbering is
	// what input logs store, so it must not depend on GLFW).

	// KeyCode::None for GLFW_KEY_UNKNOWN and for keys the engine does not know.
	[[nodiscard]] KeyCode KeyCodeFromGLFW(int glfwKey) noexcept;
	// GLFW_KEY_UNKNOWN (-1) for KeyCode::None.
	[[nodiscard]] int KeyCodeToGLFW(KeyCode key) noexcept;

	// Mouse buttons use the same numbers in GLFW and the engine (0 to 7); out-of-range values yield nullopt / -1.
	[[nodiscard]] std::optional<MouseButton> MouseButtonFromGLFW(int glfwButton) noexcept;
	[[nodiscard]] int MouseButtonToGLFW(MouseButton button) noexcept;
}
