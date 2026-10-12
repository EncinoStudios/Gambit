#pragma once

#include "Gambit/Core/Result.h"

#include <cstdint>

namespace Gambit
{
	// Reference-counted ownership of the GLFW library: it is initialized when the first user acquires it and terminated
	// when the last one releases it, so windows (and tools using GLFW directly) can come and go independently. GLFW
	// errors are routed to the core logger. Like GLFW itself, it must only be used from the main thread.
	class GLFWContext
	{
	public:
		// Initializes GLFW on the first call. Fails, with the GLFW error as the message, when the platform cannot be
		// initialized (no display, missing drivers); a failed call holds no reference.
		[[nodiscard]] static Result<void> Acquire();
		// Drops a reference obtained by a successful Acquire; the last one terminates GLFW.
		static void Release();
		[[nodiscard]] static uint32_t GetReferenceCount();
	};
}
