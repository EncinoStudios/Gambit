#pragma once

#include "Gambit/Core/Layer.h"

#include <nvrhi/nvrhi.h>

#include <cstdint>

namespace Gambit
{
	// The player's only layer for now: clears the back buffer every frame, so that the window (or the offscreen back
	// buffer of a headless run) shows that the graphics device works. Scene rendering replaces the clear in later
	// tasks.
	class RuntimeLayer final : public Layer
	{
	public:
		RuntimeLayer();

		void OnDetach() override;
		void OnRender(float interpolationAlpha) override;

	private:
		nvrhi::CommandListHandle m_CommandList;
		uint64_t m_RenderedFrames = 0;
	};
}
