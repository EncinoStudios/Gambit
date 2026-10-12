#include "gbpch.h"

#include "Gambit/Core/LayerStack.h"

namespace Gambit
{
	Layer* LayerStack::PushLayer(Scope<Layer> layer)
	{
		GB_CORE_ASSERT(layer != nullptr, "cannot push a null layer");
		Layer* raw = layer.get();
		m_Layers.insert(m_Layers.begin() + static_cast<std::ptrdiff_t>(m_OverlayStart), std::move(layer));
		++m_OverlayStart;
		return raw;
	}

	Layer* LayerStack::PushOverlay(Scope<Layer> overlay)
	{
		GB_CORE_ASSERT(overlay != nullptr, "cannot push a null overlay");
		Layer* raw = overlay.get();
		m_Layers.push_back(std::move(overlay));
		return raw;
	}

	Scope<Layer> LayerStack::Pop(const Layer* layer)
	{
		const auto found =
			std::ranges::find_if(m_Layers, [layer](const Scope<Layer>& candidate) { return candidate.get() == layer; });
		if (found == m_Layers.end())
		{
			return nullptr;
		}
		const auto index = static_cast<size_t>(found - m_Layers.begin());
		Scope<Layer> removed = std::move(*found);
		m_Layers.erase(found);
		if (index < m_OverlayStart)
		{
			--m_OverlayStart;
		}
		return removed;
	}

	bool LayerStack::Contains(const Layer* layer) const noexcept
	{
		return std::ranges::any_of(
			m_Layers, [layer](const Scope<Layer>& candidate) { return candidate.get() == layer; });
	}

	std::vector<Layer*> LayerStack::Snapshot() const
	{
		std::vector<Layer*> layers;
		layers.reserve(m_Layers.size());
		for (const Scope<Layer>& layer : m_Layers)
		{
			layers.push_back(layer.get());
		}
		return layers;
	}
}
