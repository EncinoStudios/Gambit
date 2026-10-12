#include "gbpch.h"

#include "Gambit/Platform/Window.h"

#include "Gambit/Platform/GLFW/GLFWWindow.h"

namespace Gambit
{
	Result<Scope<Window>> Window::Create(const WindowSpecification& specification)
	{
		Result<Scope<GLFWWindow>> window = GLFWWindow::Create(specification);
		if (!window)
		{
			return window.GetError();
		}
		return Scope<Window>(std::move(*window));
	}
}
