#pragma once

#include "Gambit/Core/Base.h"
#include "Gambit/Core/Layer.h"

#include <cstddef>
#include <vector>

namespace Gambit
{
	// The ordered layers of an application: regular layers first, overlays (always above all regular layers) after
	// them. The stack owns its layers. Attaching and detaching is the Application's business, not the stack's.
	class LayerStack
	{
	public:
		LayerStack() = default;
		LayerStack(const LayerStack&) = delete;
		LayerStack& operator=(const LayerStack&) = delete;

		// Adds above the other layers, below the overlays; returns the layer for later removal.
		Layer* PushLayer(Scope<Layer> layer);
		// Adds above everything.
		Layer* PushOverlay(Scope<Layer> overlay);
		// Removes and returns the layer (null when it is not in the stack).
		[[nodiscard]] Scope<Layer> Pop(const Layer* layer);

		[[nodiscard]] bool Contains(const Layer* layer) const noexcept;

		[[nodiscard]] size_t Size() const noexcept { return m_Layers.size(); }
		[[nodiscard]] bool IsEmpty() const noexcept { return m_Layers.empty(); }
		[[nodiscard]] Layer* Get(size_t index) const { return m_Layers[index].get(); }

		// Bottom to top.
		[[nodiscard]] std::vector<Layer*> Snapshot() const;

	private:
		std::vector<Scope<Layer>> m_Layers;
		size_t m_OverlayStart = 0; // index of the first overlay
	};
}
