#include "gbpch.h"

#include "Gambit/Core/Application.h"

#include "Gambit/Core/Events/WindowEvent.h"
#include "Gambit/Core/FileSystem.h"
#include "Gambit/Core/Timer.h"
#include "Gambit/Input/InputDevices.h"

#include <charconv>
#include <ranges>
#include <system_error>
#include <thread>

namespace Gambit
{
	namespace
	{
		// The input of a headless application that has no source: nothing pressed.
		class NoInputSource final : public InputSource
		{
		public:
			[[nodiscard]] InputFrame Sample() override { return {}; }
		};

		Result<uint64_t> ParsePositive(std::string_view option, const std::string& text, uint64_t maximum)
		{
			uint64_t value = 0;
			const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
			if (error != std::errc{} || end != text.data() + text.size() || value == 0 || value > maximum)
			{
				return Error(std::format("{} expects a whole number from 1 to {}, not '{}'", option, maximum, text));
			}
			return value;
		}
	}

	ApplicationCommandLineArgs::ApplicationCommandLineArgs(int argc, const char* const* argv)
	{
		for (int i = 0; i < argc; ++i)
		{
			Args.emplace_back(argv[i] != nullptr ? argv[i] : "");
		}
	}

	bool ApplicationCommandLineArgs::HasFlag(std::string_view name) const
	{
		return std::ranges::find(Args, name) != Args.end();
	}

	std::optional<std::string> ApplicationCommandLineArgs::GetValue(std::string_view name) const
	{
		for (size_t i = 0; i < Args.size(); ++i)
		{
			if (Args[i] == name)
			{
				return i + 1 < Args.size() ? std::optional<std::string>(Args[i + 1]) : std::nullopt;
			}
			if (Args[i].size() > name.size() && Args[i].starts_with(name) && Args[i][name.size()] == '=')
			{
				return Args[i].substr(name.size() + 1);
			}
		}
		return std::nullopt;
	}

	Result<void> ApplyStandardArguments(const ApplicationCommandLineArgs& args, ApplicationSpecification& specification)
	{
		specification.CommandLineArgs = args;
		if (args.HasFlag("--headless"))
		{
			specification.Headless = true;
		}
		if (args.HasFlag("--no-gpu"))
		{
			specification.EnableGraphics = false;
		}
		const bool wantsFrames = args.HasFlag("--frames")
			|| std::ranges::any_of(args.Args, [](const std::string& arg) { return arg.starts_with("--frames="); });
		if (wantsFrames)
		{
			const std::optional<std::string> frames = args.GetValue("--frames");
			if (!frames)
			{
				return Error("--frames expects a value");
			}
			const Result<uint64_t> count = ParsePositive("--frames", *frames, 1'000'000'000'000ull);
			if (!count)
			{
				return count.GetError();
			}
			specification.FrameLimit = *count;
		}
		const bool wantsRate = args.HasFlag("--hz")
			|| std::ranges::any_of(args.Args, [](const std::string& arg) { return arg.starts_with("--hz="); });
		if (wantsRate)
		{
			const std::optional<std::string> hz = args.GetValue("--hz");
			if (!hz)
			{
				return Error("--hz expects a value");
			}
			const Result<uint64_t> rate = ParsePositive("--hz", *hz, FixedTimestepClock::MaxSimulationHz);
			if (!rate)
			{
				return rate.GetError();
			}
			specification.SimulationHz = static_cast<uint32_t>(*rate);
		}
		return {};
	}

	Application::Application(ApplicationSpecification specification)
		: m_Specification(std::move(specification)),
		  m_Clock(
			  std::clamp<uint32_t>(m_Specification.SimulationHz, 1, FixedTimestepClock::MaxSimulationHz),
			  std::max<uint32_t>(m_Specification.MaxSimStepsPerFrame, 1))
	{
		GB_CORE_ASSERT(s_Instance == nullptr, "there can only be one Application");
		GB_CORE_ASSERT(
			m_Specification.SimulationHz >= 1 && m_Specification.SimulationHz <= FixedTimestepClock::MaxSimulationHz,
			"SimulationHz {} is out of range", m_Specification.SimulationHz);
		m_Specification.SimulationHz = m_Clock.GetHz();
		m_Specification.MaxSimStepsPerFrame = m_Clock.GetMaxStepsPerFrame();
		s_Instance = this;
	}

	Application::~Application()
	{
		// Top first, while the Application (and Application::Get) is still valid for the layers' destructors.
		for (Layer* layer : std::views::reverse(m_Layers.Snapshot()))
		{
			const Scope<Layer> removed = m_Layers.Pop(layer);
			removed->OnDetach();
		}
		m_InputSource.reset();
		m_Devices.reset();
		m_GraphicsDevice.reset(); // its swap chain belongs to the window
		m_Window.reset();
		s_Instance = nullptr;
	}

	Application& Application::Get()
	{
		GB_CORE_ASSERT(s_Instance != nullptr, "there is no Application");
		return *s_Instance;
	}

	Window& Application::GetWindow()
	{
		GB_CORE_ASSERT(m_Window != nullptr, "the application has no window (headless, or not initialized)");
		return *m_Window;
	}

	GraphicsDevice& Application::GetGraphicsDevice()
	{
		GB_CORE_ASSERT(m_GraphicsDevice != nullptr, "the application has no graphics device (EnableGraphics is off)");
		return *m_GraphicsDevice;
	}

	Result<void> Application::Initialize()
	{
		if (m_Initialized)
		{
			return {};
		}

		std::error_code error;
		const std::filesystem::path previousDirectory = std::filesystem::current_path(error);
		if (!m_Specification.WorkingDirectory.empty())
		{
			std::filesystem::current_path(m_Specification.WorkingDirectory, error);
			if (error)
			{
				return Error(std::format(
					"cannot use '{}' as the working directory: {}",
					FileSystem::ToUtf8(m_Specification.WorkingDirectory), error.message()));
			}
		}

		if (!m_Specification.Headless)
		{
			Result<Scope<Window>> window = Window::Create(m_Specification.Window);
			if (!window)
			{
				std::error_code ignored;
				std::filesystem::current_path(previousDirectory, ignored); // a failed Initialize changes nothing
				return Error(std::format("cannot open the window: {}", window.GetError().Message));
			}
			m_Window = std::move(*window);
			m_Window->SetEventCallback([this](Event& event) { OnEvent(event); });
			m_Devices = CreateScope<InputDevices>(*m_Window);
		}

		if (m_Specification.EnableGraphics)
		{
			GraphicsDeviceSpecification graphics = m_Specification.Graphics;
			graphics.ApplicationName = m_Specification.Name;
			graphics.Headless = m_Specification.Headless;
			if (m_Window != nullptr)
			{
				graphics.VSync = m_Window->IsVSync();
			}
			Result<Scope<GraphicsDevice>> device = GraphicsDevice::Create(graphics, m_Window.get());
			if (!device)
			{
				m_Devices.reset();
				m_Window.reset();
				std::error_code ignored;
				std::filesystem::current_path(previousDirectory, ignored);
				return Error(
					std::format("cannot create the graphics device: {}", device.GetError().Message),
					device.GetError().Code);
			}
			m_GraphicsDevice = std::move(*device);
		}

		if (m_InputSource == nullptr)
		{
			SetUpDefaultInputSource();
		}
		m_Initialized = true;
		GB_CORE_INFO(
			"{}: {} at {} Hz (at most {} ticks per frame)", m_Specification.Name,
			m_Specification.Headless ? "headless" : "windowed", m_Specification.SimulationHz,
			m_Specification.MaxSimStepsPerFrame);
		return {};
	}

	void Application::SetUpDefaultInputSource()
	{
		if (m_Devices != nullptr)
		{
			m_InputSource = CreateScope<DeviceInputSource>(*m_Devices);
		}
		else
		{
			m_InputSource = CreateScope<NoInputSource>();
		}
	}

	void Application::SetInputSource(Scope<InputSource> source)
	{
		if (source != nullptr)
		{
			m_InputSource = std::move(source);
		}
		else
		{
			SetUpDefaultInputSource();
		}
	}

	int Application::Run()
	{
		if (const Result<void> initialized = Initialize(); !initialized)
		{
			GB_CORE_ERROR("{}: {}", m_Specification.Name, initialized.GetError().Message);
			return 1;
		}

		m_Running = true;
		const bool synthetic = m_Specification.FrameLimit > 0;
		Timer frameTimer;
		uint64_t framesRun = 0;
		while (m_Running)
		{
			if (m_Window != nullptr)
			{
				m_Window->PollEvents();
				if (!m_Running)
				{
					break;
				}
			}

			const std::chrono::nanoseconds elapsed(static_cast<int64_t>(frameTimer.ElapsedSeconds() * 1.0e9));
			frameTimer.Reset();
			const uint64_t ticksBefore = m_Tick;
			if (synthetic)
			{
				RunSyntheticFrame();
			}
			else
			{
				RunFrame(elapsed);
			}

			if (m_Specification.FrameLimit > 0 && ++framesRun >= m_Specification.FrameLimit)
			{
				break;
			}
			if (!synthetic && m_Tick == ticksBefore)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(1)); // nothing to simulate yet: do not spin
			}
		}
		m_Running = false;
		GB_CORE_INFO("{}: ran {} frames, {} simulation ticks", m_Specification.Name, m_FrameCount, m_Tick);
		if (m_GraphicsDevice != nullptr)
		{
			GB_CORE_INFO("{}: presented {} frames", m_Specification.Name, m_GraphicsDevice->GetFrameCount());
		}
		return 0;
	}

	void Application::RunFrame(std::chrono::nanoseconds frameTime)
	{
		GB_CORE_ASSERT(m_Initialized, "Application::Initialize must succeed before frames are run");

		const uint32_t ticks = m_Clock.Advance(frameTime);
		if (m_Clock.WasClamped())
		{
			// One warning per burst of slow frames, not one per frame.
			if (!m_ClampBurstActive)
			{
				GB_CORE_WARN(
					"The simulation fell behind: {} ticks dropped (at most {} per frame); this is logged once per "
					"burst",
					m_Clock.GetLastDroppedTicks(), m_Clock.GetMaxStepsPerFrame());
			}
			m_ClampBurstActive = true;
		}
		else
		{
			m_ClampBurstActive = false;
		}
		ExecuteFrame(ticks, std::chrono::duration<float>(frameTime).count());
	}

	void Application::RunFrames(uint64_t count)
	{
		GB_CORE_ASSERT(m_Initialized, "Application::Initialize must succeed before frames are run");
		for (uint64_t i = 0; i < count; ++i)
		{
			RunSyntheticFrame();
		}
	}

	void Application::RunSyntheticFrame()
	{
		// Exactly one tick, without the clock: 1 / SimulationHz is rarely a whole number of nanoseconds, and the
		// rounding of synthetic frame times would add up to an extra tick in long runs. The clock keeps any partial
		// tick of real frames for the next real frame.
		m_ClampBurstActive = false;
		ExecuteFrame(1, m_Clock.GetStepSeconds());
	}

	void Application::ExecuteFrame(uint32_t ticks, float frameSeconds)
	{
		m_Minimized = m_Window != nullptr && m_Window->IsMinimized();
		for (uint32_t i = 0; i < ticks; ++i)
		{
			RunFixedTick(m_InputSource->Sample());
		}
		UpdateLayers(TimeStep(frameSeconds, m_Tick));
		if (!m_Minimized)
		{
			RenderFrame(m_Clock.GetAlpha());
		}
		++m_FrameCount;
	}

	void Application::RenderFrame(float alpha)
	{
		if (m_GraphicsDevice == nullptr)
		{
			RenderLayers(alpha);
			return;
		}
		if (m_Window != nullptr && m_Window->IsVSync() != m_GraphicsDevice->IsVSync())
		{
			m_GraphicsDevice->SetVSync(m_Window->IsVSync());
		}
		if (m_GraphicsDevice->BeginFrame())
		{
			RenderLayers(alpha);
			m_GraphicsDevice->Present();
		}
		if (m_GraphicsDevice->IsDeviceLost() && !m_DeviceLossHandled)
		{
			// Nothing can be rendered any more (BeginFrame returns false from now on): end the run.
			GB_CORE_ERROR("{}: closing, the graphics device was lost", m_Specification.Name);
			m_DeviceLossHandled = true;
			Close();
		}
	}

	void Application::StepFixed(const InputFrame& input)
	{
		GB_CORE_ASSERT(m_Initialized, "Application::Initialize must succeed before ticks are stepped");
		RunFixedTick(input);
	}

	void Application::RunFixedTick(const InputFrame& input)
	{
		++m_Tick;
		m_InputState.Update(input);
		if (m_InputRecorder != nullptr)
		{
			m_InputRecorder->Record(input);
		}
		const TimeStep step(m_Clock.GetStepSeconds(), m_Tick);
		for (Layer* layer : m_Layers.Snapshot())
		{
			if (m_Layers.Contains(layer))
			{
				layer->OnFixedUpdate(step);
			}
		}
	}

	void Application::UpdateLayers(TimeStep step)
	{
		for (Layer* layer : m_Layers.Snapshot())
		{
			if (m_Layers.Contains(layer))
			{
				layer->OnUpdate(step);
			}
		}
	}

	void Application::RenderLayers(float alpha)
	{
		for (Layer* layer : m_Layers.Snapshot())
		{
			if (m_Layers.Contains(layer))
			{
				layer->OnRender(alpha);
			}
		}
	}

	void Application::OnEvent(Event& event)
	{
		if (m_Devices != nullptr)
		{
			m_Devices->OnEvent(event);
		}

		const std::vector<Layer*> layers = m_Layers.Snapshot();
		for (Layer* layer : std::views::reverse(layers))
		{
			if (event.Handled)
			{
				break;
			}
			if (m_Layers.Contains(layer))
			{
				layer->OnEvent(event);
			}
		}

		if (!event.Handled && event.GetEventType() == EventType::WindowClose)
		{
			Close();
		}
	}

	Layer* Application::PushLayer(Scope<Layer> layer)
	{
		Layer* pushed = m_Layers.PushLayer(std::move(layer));
		pushed->OnAttach();
		return pushed;
	}

	Layer* Application::PushOverlay(Scope<Layer> overlay)
	{
		Layer* pushed = m_Layers.PushOverlay(std::move(overlay));
		pushed->OnAttach();
		return pushed;
	}

	Scope<Layer> Application::PopLayer(Layer* layer)
	{
		Scope<Layer> removed = m_Layers.Pop(layer);
		if (removed != nullptr)
		{
			removed->OnDetach();
		}
		return removed;
	}
}
