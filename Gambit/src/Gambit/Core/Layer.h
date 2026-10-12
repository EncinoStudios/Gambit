#pragma once

#include "Gambit/Core/Events/Event.h"
#include "Gambit/Core/TimeStep.h"

#include <string>
#include <string_view>

namespace Gambit
{
	// A unit of application behavior with a place in the update order: the editor, the game runtime, a debug overlay.
	// Layers are updated bottom to top and receive events top to bottom.
	class Layer
	{
	public:
		explicit Layer(std::string name = "Layer")
			: m_Name(std::move(name))
		{}
		virtual ~Layer() = default;
		Layer(const Layer&) = delete;
		Layer& operator=(const Layer&) = delete;

		// Called when the layer joins / leaves the application's layer stack.
		virtual void OnAttach() {}
		virtual void OnDetach() {}

		// Once per rendered frame with the real frame duration (UI, camera smoothing, anything that is not simulation).
		virtual void OnUpdate(TimeStep /*step*/) {}
		// Once per simulation tick with the fixed duration and the tick number (D-011); the input of the tick is in
		// Application::GetInputState(). This is where the simulation advances.
		virtual void OnFixedUpdate(TimeStep /*step*/) {}
		// Once per rendered frame after the updates; `interpolationAlpha` in [0, 1) is how far the frame lies between
		// the last two simulation ticks, for smoothing what is drawn.
		virtual void OnRender(float /*interpolationAlpha*/) {}
		// Mark the event Handled to keep the layers below from seeing it.
		virtual void OnEvent(Event& /*event*/) {}
		// For layers that draw ImGui. The Application does not call it yet: the editor's ImGui layer will (T20).
		virtual void OnImGuiRender() {}

		[[nodiscard]] const std::string& GetName() const noexcept { return m_Name; }

	private:
		std::string m_Name;
	};
}
