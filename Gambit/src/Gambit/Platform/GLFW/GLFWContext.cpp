#include "gbpch.h"

#include "Gambit/Platform/GLFW/GLFWContext.h"

#include <GLFW/glfw3.h>

namespace Gambit
{
	namespace
	{
		std::mutex s_Mutex;
		uint32_t s_ReferenceCount = 0;

		void OnError(int code, const char* description)
		{
			GB_CORE_ERROR("GLFW error 0x{:X}: {}", code, description != nullptr ? description : "(no description)");
		}
	}

	Result<void> GLFWContext::Acquire()
	{
		const std::scoped_lock lock(s_Mutex);
		if (s_ReferenceCount == 0)
		{
			glfwSetErrorCallback(OnError);
			if (glfwInit() != GLFW_TRUE)
			{
				const char* description = nullptr;
				glfwGetError(&description);
				return Error(
					std::format("cannot initialize GLFW: {}", description != nullptr ? description : "unknown error"));
			}
		}
		++s_ReferenceCount;
		return {};
	}

	void GLFWContext::Release()
	{
		const std::scoped_lock lock(s_Mutex);
		GB_CORE_ASSERT(s_ReferenceCount > 0, "GLFWContext::Release without a matching Acquire");
		if (s_ReferenceCount == 0)
		{
			return;
		}
		if (--s_ReferenceCount == 0)
		{
			glfwTerminate();
		}
	}

	uint32_t GLFWContext::GetReferenceCount()
	{
		const std::scoped_lock lock(s_Mutex);
		return s_ReferenceCount;
	}
}
