#include "RuntimeLayer.h"

#include "Gambit/Core/Application.h"
#include "Gambit/Core/Log.h"
#include "Gambit/RHI/GraphicsDevice.h"

#include <nvrhi/utils.h>

namespace Gambit
{
	namespace
	{
		const nvrhi::Color ClearColor(0.08f, 0.09f, 0.11f, 1.0f);
	}

	RuntimeLayer::RuntimeLayer()
		: Layer("Runtime")
	{}

	void RuntimeLayer::OnDetach()
	{
		if (m_CommandList)
		{
			GB_INFO("GambitRuntime: rendered {} frames", m_RenderedFrames);
		}
		m_CommandList = nullptr;
	}

	void RuntimeLayer::OnRender(float /*interpolationAlpha*/)
	{
		// The layer is attached before Application::Initialize creates the device, so the command list is made here.
		Application& application = Application::Get();
		if (!application.HasGraphicsDevice())
		{
			return;
		}
		GraphicsDevice& device = application.GetGraphicsDevice();
		if (!m_CommandList)
		{
			m_CommandList = device.GetNvrhiDevice()->createCommandList();
		}
		m_CommandList->open();
		nvrhi::utils::ClearColorAttachment(m_CommandList, device.GetCurrentFramebuffer(), 0, ClearColor);
		m_CommandList->close();
		device.GetNvrhiDevice()->executeCommandList(m_CommandList);
		++m_RenderedFrames;
	}
}
