#pragma once

#include "Gambit/Core/Base.h"
#include "Gambit/Core/Events/Event.h"
#include "Gambit/Core/FixedTimestepClock.h"
#include "Gambit/Core/Layer.h"
#include "Gambit/Core/LayerStack.h"
#include "Gambit/Core/Result.h"
#include "Gambit/Input/InputActions.h"
#include "Gambit/Input/InputFrame.h"
#include "Gambit/Input/InputLog.h"
#include "Gambit/Input/InputSource.h"
#include "Gambit/Input/InputState.h"
#include "Gambit/Platform/Window.h"
#include "Gambit/RHI/GraphicsDevice.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Gambit
{
	class InputDevices;

	// The command line of the program, kept as strings. Args[0] is the program name.
	struct ApplicationCommandLineArgs
	{
		std::vector<std::string> Args;

		ApplicationCommandLineArgs() = default;
		ApplicationCommandLineArgs(int argc, const char* const* argv);

		// Whether the exact argument `name` (for example "--headless") is present.
		[[nodiscard]] bool HasFlag(std::string_view name) const;
		// The value of "--name value" or "--name=value"; nullopt when the option is absent or has no value.
		[[nodiscard]] std::optional<std::string> GetValue(std::string_view name) const;
	};

	struct ApplicationSpecification
	{
		std::string Name = "Gambit Application";
		std::filesystem::path WorkingDirectory; // made the current directory by Initialize when not empty
		ApplicationCommandLineArgs CommandLineArgs;
		bool Headless = false; // no window and no input devices
		WindowSpecification Window;
		uint32_t SimulationHz = 60;       // fixed simulation ticks per second (D-011)
		uint32_t MaxSimStepsPerFrame = 8; // a longer frame is clamped to this many ticks (the rest is dropped)
		uint64_t FrameLimit = 0;          // Run returns after this many frames; 0 runs until Close
		// Creates a GraphicsDevice in Initialize: presenting to the window, or offscreen when Headless (the Headless,
		// VSync and ApplicationName fields of Graphics are taken from this specification). Frames then render between
		// the device's BeginFrame and Present.
		bool EnableGraphics = false;
		GraphicsDeviceSpecification Graphics;
	};

	// Applies the arguments every Gambit executable understands to `specification`: --headless, --frames N (sets
	// FrameLimit), --hz N (SimulationHz) and --no-gpu (no graphics device, for machines without Vulkan and dedicated
	// simulation runs). Fails with a message for malformed values.
	[[nodiscard]] Result<void>
	ApplyStandardArguments(const ApplicationCommandLineArgs& args, ApplicationSpecification& specification);

	// The program: owns the window, the layers and the main loop (D-011, D-024).
	//
	// One frame is: poll events, then run the fixed-timestep clock to find how many simulation ticks the elapsed real
	// time is worth; for each tick the input source supplies an InputFrame and every layer's OnFixedUpdate runs; then
	// OnUpdate and OnRender(alpha) run once (with a graphics device, OnRender runs between its BeginFrame and Present
	// and is skipped when no frame can be rendered). Simulation therefore sees only fixed steps and the tick counter,
	// never the wall clock. Everything must be used from the main thread. There is one Application per process at a
	// time.
	class Application
	{
	public:
		explicit Application(ApplicationSpecification specification);
		virtual ~Application();
		Application(const Application&) = delete;
		Application& operator=(const Application&) = delete;

		[[nodiscard]] static Application& Get();
		[[nodiscard]] static bool Exists() noexcept { return s_Instance != nullptr; }

		// Sets the working directory, opens the window (unless Headless) and creates the graphics device (when
		// EnableGraphics). Run calls it when needed. Safe to call twice. On failure nothing stays open and the error
		// says why (no display, no Vulkan device, bad directory, ...).
		[[nodiscard]] Result<void> Initialize();
		[[nodiscard]] bool IsInitialized() const noexcept { return m_Initialized; }

		// Runs frames until Close() or the window is closed, or FrameLimit frames have run. Returns the process exit
		// code: 0 on success, 1 when Initialize failed. With FrameLimit set the frames are synthetic (exactly one tick
		// each, no waiting), which makes a limited run deterministic and fast; otherwise frames follow the real clock.
		[[nodiscard]] int Run();
		void Close() noexcept { m_Running = false; }
		[[nodiscard]] bool IsRunning() const noexcept { return m_Running; }

		// One frame with the given duration, without polling the window: the simulation advances by as many ticks as
		// the duration is worth, then OnUpdate and OnRender run. This is what Run does per iteration, and the way tools
		// and tests drive an Application with synthetic time. Requires Initialize().
		void RunFrame(std::chrono::nanoseconds frameTime);
		// `count` frames of exactly one tick each (frame time = 1 / SimulationHz). Requires Initialize().
		void RunFrames(uint64_t count);
		// One simulation tick with the given input, bypassing the clock and the input source (it is still recorded):
		// the manual stepping API of tools and tests. Runs OnFixedUpdate only. Requires Initialize().
		void StepFixed(const InputFrame& input);

		// Routes an event to the input devices, then to the layers from the top; an unhandled WindowClose closes the
		// application.
		void OnEvent(Event& event);

		// Layers are attached on push and detached when removed or when the application is destroyed (top first).
		Layer* PushLayer(Scope<Layer> layer);
		Layer* PushOverlay(Scope<Layer> overlay);
		// Detaches and returns the layer, or null when it is not in the stack.
		[[nodiscard]] Scope<Layer> PopLayer(Layer* layer);

		// The source of each tick's input. The default is the devices (windowed) or no input (headless); a null source
		// restores the default.
		void SetInputSource(Scope<InputSource> source);
		[[nodiscard]] InputSource& GetInputSource() { return *m_InputSource; }
		// Appends every tick's input frame to `log` (also from StepFixed); null stops recording.
		void SetInputRecorder(Ref<InputLog> log) { m_InputRecorder = std::move(log); }
		void SetInputActions(Ref<const InputActions> actions) { m_InputState.SetActions(std::move(actions)); }
		// The input of the current tick as the layers should read it.
		[[nodiscard]] const InputState& GetInputState() const noexcept { return m_InputState; }

		[[nodiscard]] const ApplicationSpecification& GetSpecification() const noexcept { return m_Specification; }
		[[nodiscard]] bool IsHeadless() const noexcept { return m_Specification.Headless; }
		// The window; only valid after Initialize and when not Headless.
		[[nodiscard]] Window& GetWindow();
		[[nodiscard]] bool HasGraphicsDevice() const noexcept { return m_GraphicsDevice != nullptr; }
		// The graphics device; only valid after Initialize and with EnableGraphics.
		[[nodiscard]] GraphicsDevice& GetGraphicsDevice();
		// Simulation ticks executed so far.
		[[nodiscard]] uint64_t GetTick() const noexcept { return m_Tick; }
		[[nodiscard]] uint64_t GetFrameCount() const noexcept { return m_FrameCount; }
		[[nodiscard]] const FixedTimestepClock& GetClock() const noexcept { return m_Clock; }
		// Duration of one tick in seconds.
		[[nodiscard]] float GetFixedStepSeconds() const noexcept { return m_Clock.GetStepSeconds(); }

	private:
		void SetUpDefaultInputSource();
		// One frame of exactly one tick (RunFrames and Run with a FrameLimit).
		void RunSyntheticFrame();
		// The part of a frame after the tick count is known: the ticks, then OnUpdate and OnRender.
		void ExecuteFrame(uint32_t ticks, float frameSeconds);
		void RunFixedTick(const InputFrame& input);
		void UpdateLayers(TimeStep step);
		void RenderLayers(float alpha);
		// RenderLayers wrapped in the graphics device's frame, when there is a device.
		void RenderFrame(float alpha);

		inline static Application* s_Instance = nullptr;

		ApplicationSpecification m_Specification;
		FixedTimestepClock m_Clock;
		LayerStack m_Layers;
		Scope<Window> m_Window;
		Scope<GraphicsDevice> m_GraphicsDevice; // declared after the window: it is destroyed first
		Scope<InputDevices> m_Devices;
		Scope<InputSource> m_InputSource;
		Ref<InputLog> m_InputRecorder;
		InputState m_InputState;
		uint64_t m_Tick = 0;
		uint64_t m_FrameCount = 0;
		bool m_Initialized = false;
		bool m_Running = false;
		bool m_Minimized = false;
		bool m_ClampBurstActive = false;
		bool m_DeviceLossHandled = false;
	};
}
