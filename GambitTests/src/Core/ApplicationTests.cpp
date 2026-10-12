#include <doctest/doctest.h>

#include "TestSupport.h"

#include "Gambit/Core/Application.h"
#include "Gambit/Core/Events/KeyEvent.h"
#include "Gambit/Core/Events/WindowEvent.h"
#include "Gambit/Core/Hash.h"
#include "Gambit/Core/LogSinks.h"
#include "Gambit/Math/Random.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

using namespace Gambit;
using std::chrono::nanoseconds;

namespace
{
	ApplicationSpecification HeadlessSpecification()
	{
		ApplicationSpecification specification;
		specification.Name = "TestApplication";
		specification.Headless = true;
		return specification;
	}

	// Records every callback it receives into a shared log, so that the order across layers can be checked.
	class RecordingLayer : public Layer
	{
	public:
		RecordingLayer(std::string name, std::vector<std::string>& log)
			: Layer(std::move(name)), m_Log(log)
		{}

		void OnAttach() override { m_Log.push_back(GetName() + ":attach"); }
		void OnDetach() override { m_Log.push_back(GetName() + ":detach"); }
		void OnUpdate(TimeStep step) override
		{
			m_Log.push_back(GetName() + ":update");
			UpdateSteps.push_back(step);
		}
		void OnFixedUpdate(TimeStep step) override
		{
			m_Log.push_back(GetName() + ":fixed");
			FixedSteps.push_back(step);
			if (CloseAtTick != 0 && step.GetTick() == CloseAtTick)
			{
				Application::Get().Close();
			}
		}
		void OnRender(float alpha) override
		{
			m_Log.push_back(GetName() + ":render");
			Alphas.push_back(alpha);
		}
		void OnEvent(Event& event) override
		{
			m_Log.push_back(GetName() + ":event " + event.ToString());
			if (HandleEvents)
			{
				event.Handled = true;
			}
		}

		std::vector<TimeStep> UpdateSteps;
		std::vector<TimeStep> FixedSteps;
		std::vector<float> Alphas;
		bool HandleEvents = false;
		uint64_t CloseAtTick = 0;

	private:
		std::vector<std::string>& m_Log;
	};

	// Restores the working directory when it goes out of scope.
	class ScopedWorkingDirectory
	{
	public:
		ScopedWorkingDirectory()
			: m_Saved(std::filesystem::current_path())
		{}
		~ScopedWorkingDirectory()
		{
			std::error_code ignored;
			std::filesystem::current_path(m_Saved, ignored);
		}

	private:
		std::filesystem::path m_Saved;
	};

	InputFrame FrameWithKey(KeyCode key)
	{
		InputFrame frame;
		frame.SetKey(key, true);
		return frame;
	}

	size_t Count(const std::vector<std::string>& log, const std::string& entry)
	{
		return static_cast<size_t>(std::count(log.begin(), log.end(), entry));
	}
}

TEST_CASE("Core: Application is a singleton for its lifetime")
{
	CHECK_FALSE(Application::Exists());
	{
		Application application(HeadlessSpecification());
		CHECK(Application::Exists());
		CHECK(&Application::Get() == &application);
		CHECK(application.IsHeadless());
		CHECK_FALSE(application.IsInitialized());
		REQUIRE(application.Initialize().HasValue());
		CHECK(application.IsInitialized());
		CHECK(application.Initialize().HasValue()); // idempotent
	}
	CHECK_FALSE(Application::Exists());
}

TEST_CASE("Core: Application normalizes its simulation settings")
{
	ApplicationSpecification specification = HeadlessSpecification();
	CHECK(specification.SimulationHz == 60);
	CHECK(specification.MaxSimStepsPerFrame == 8);

	specification.SimulationHz = 120;
	specification.MaxSimStepsPerFrame = 3;
	Application application(specification);
	CHECK(application.GetFixedStepSeconds() == 1.0f / 120.0f);
	CHECK(application.GetClock().GetMaxStepsPerFrame() == 3);
	CHECK(application.GetSpecification().SimulationHz == 120);
}

TEST_CASE("Core: Application runs fixed updates, updates and renders in layer order")
{
	std::vector<std::string> log;
	Application application(HeadlessSpecification());
	application.PushLayer(CreateScope<RecordingLayer>("A", log));
	application.PushLayer(CreateScope<RecordingLayer>("B", log));
	application.PushOverlay(CreateScope<RecordingLayer>("O", log));
	REQUIRE(application.Initialize().HasValue());
	log.clear();

	application.RunFrames(2);
	const std::vector<std::string> expected{ "A:fixed",  "B:fixed",  "O:fixed",  "A:update", "B:update", "O:update",
		                                     "A:render", "B:render", "O:render", "A:fixed",  "B:fixed",  "O:fixed",
		                                     "A:update", "B:update", "O:update", "A:render", "B:render", "O:render" };
	CHECK(log == expected);
	CHECK(application.GetTick() == 2);
	CHECK(application.GetFrameCount() == 2);
}

TEST_CASE("Core: Application gives fixed updates the fixed step and the tick number")
{
	std::vector<std::string> log;
	Application application(HeadlessSpecification());
	auto layer = CreateScope<RecordingLayer>("A", log);
	RecordingLayer* recorder = layer.get();
	application.PushLayer(std::move(layer));
	REQUIRE(application.Initialize().HasValue());

	application.RunFrames(5);
	REQUIRE(recorder->FixedSteps.size() == 5);
	for (size_t i = 0; i < 5; ++i)
	{
		CHECK(recorder->FixedSteps[i].GetTick() == i + 1);
		CHECK(recorder->FixedSteps[i].GetSeconds() == 1.0f / 60.0f);
	}
	REQUIRE(recorder->UpdateSteps.size() == 5);
	CHECK(recorder->UpdateSteps[4].GetSeconds() == doctest::Approx(1.0f / 60.0f).epsilon(1e-5));
	CHECK(recorder->UpdateSteps[4].GetTick() == 5); // updates see the number of completed ticks
	for (const float alpha : recorder->Alphas)
	{
		CHECK(alpha >= 0.0f);
		CHECK(alpha < 1.0f);
	}
}

TEST_CASE("Core: Application turns the frame duration into ticks and an interpolation alpha")
{
	std::vector<std::string> log;
	Application application(HeadlessSpecification());
	auto layer = CreateScope<RecordingLayer>("A", log);
	RecordingLayer* recorder = layer.get();
	application.PushLayer(std::move(layer));
	REQUIRE(application.Initialize().HasValue());

	application.RunFrame(nanoseconds(8'333'334)); // half a tick: nothing to simulate yet
	CHECK(application.GetTick() == 0);
	CHECK(recorder->FixedSteps.empty());
	REQUIRE(recorder->Alphas.size() == 1);
	CHECK(recorder->Alphas.back() == doctest::Approx(0.5f).epsilon(1e-5));

	application.RunFrame(
		nanoseconds(50'000'000)); // 3.5 ticks of real time since the start: 3 ticks, the half carried over
	CHECK(application.GetTick() == 3);
	CHECK(recorder->Alphas.back() == doctest::Approx(0.5f).epsilon(1e-4));
	REQUIRE(recorder->UpdateSteps.size() == 2);
	CHECK(recorder->UpdateSteps[1].GetSeconds() == doctest::Approx(0.05f));
	CHECK(application.GetFrameCount() == 2);
}

TEST_CASE("Core: Application synthetic frames run exactly one tick whatever real time has accumulated")
{
	// 1/999 s is not a whole number of nanoseconds, so a frame time rounded up to whole nanoseconds would carry a
	// remainder into the next frame and eventually produce a frame with two ticks.
	ApplicationSpecification specification = HeadlessSpecification();
	specification.SimulationHz = 999;
	std::vector<std::string> log;
	Application application(specification);
	auto layer = CreateScope<RecordingLayer>("A", log);
	RecordingLayer* recorder = layer.get();
	application.PushLayer(std::move(layer));
	REQUIRE(application.Initialize().HasValue());

	application.RunFrame(nanoseconds(1'001'001)); // 999'999'999 / 10^9 of a tick: none yet
	CHECK(application.GetTick() == 0);
	const float alpha = recorder->Alphas.back();

	application.RunFrames(3);
	CHECK(application.GetTick() == 3);
	CHECK(application.GetFrameCount() == 4);
	CHECK(recorder->Alphas.back() == alpha); // the partial tick of the real frame is kept for the next real frame
	CHECK(recorder->UpdateSteps.back().GetSeconds() == application.GetFixedStepSeconds());

	application.RunFrame(nanoseconds(1)); // completes the partial tick
	CHECK(application.GetTick() == 4);
}

TEST_CASE("Core: Application clamps a long frame and warns once per burst")
{
	LogConfig config = Tests::MakeTestLogConfig();
	config.Console = false;
	config.RingBufferCapacity = 64;
	Tests::ScopedLogInit logInit(config);

	Application application(HeadlessSpecification());
	REQUIRE(application.Initialize().HasValue());
	const auto warnings = [] {
		size_t count = 0;
		for (const LogEntry& entry : Log::GetRingBuffer()->Snapshot())
		{
			count += entry.Level == LogLevel::Warn && entry.Message.find("fell behind") != std::string::npos ? 1 : 0;
		}
		return count;
	};

	application.RunFrame(std::chrono::seconds(1));
	CHECK(application.GetTick() == 8); // the limit, not the 60 ticks a second is worth
	CHECK(warnings() == 1);
	application.RunFrame(std::chrono::seconds(2));
	application.RunFrame(std::chrono::seconds(3));
	CHECK(warnings() == 1); // the same burst
	CHECK(application.GetTick() == 24);

	application.RunFrame(nanoseconds(16'666'667)); // back to normal
	CHECK(warnings() == 1);
	application.RunFrame(std::chrono::seconds(1)); // a new burst
	CHECK(warnings() == 2);
	CHECK(application.GetClock().GetTotalDroppedTicks() > 0);
}

TEST_CASE("Core: Application StepFixed advances one tick with the given input")
{
	std::vector<std::string> log;
	Application application(HeadlessSpecification());
	application.PushLayer(CreateScope<RecordingLayer>("A", log));
	REQUIRE(application.Initialize().HasValue());
	log.clear();

	application.StepFixed(FrameWithKey(KeyCode::Space));
	CHECK(log == std::vector<std::string>{ "A:fixed" }); // only the fixed update
	CHECK(application.GetTick() == 1);
	CHECK(application.GetFrameCount() == 0);
	CHECK(application.GetInputState().GetFrame().IsKeyDown(KeyCode::Space));

	application.StepFixed(InputFrame{});
	CHECK(application.GetTick() == 2);
	CHECK_FALSE(application.GetInputState().GetFrame().IsKeyDown(KeyCode::Space));
	CHECK(application.GetInputState().GetPreviousFrame().IsKeyDown(KeyCode::Space));
}

TEST_CASE("Core: Application evaluates input actions on the frames it steps")
{
	const Result<InputActions> actions = InputActions::Parse(R"({
		"Version": 1,
		"Actions": [
			{ "Name": "Jump", "Type": "Button", "Bindings": [ { "Type": "Key", "Key": "Space" } ] }
		]
	})");
	REQUIRE(actions.HasValue());

	Application application(HeadlessSpecification());
	REQUIRE(application.Initialize().HasValue());
	application.SetInputActions(CreateRef<InputActions>(*actions));

	CHECK_FALSE(application.GetInputState().IsDown("Jump"));
	application.StepFixed(FrameWithKey(KeyCode::Space));
	CHECK(application.GetInputState().WasPressed("Jump"));
	application.StepFixed(FrameWithKey(KeyCode::Space));
	CHECK(application.GetInputState().IsDown("Jump"));
	CHECK_FALSE(application.GetInputState().WasPressed("Jump"));
	application.StepFixed(InputFrame{});
	CHECK(application.GetInputState().WasReleased("Jump"));
}

TEST_CASE("Core: Application replays a recorded input log through the input source")
{
	auto log = CreateRef<InputLog>();
	for (const KeyCode key : { KeyCode::A, KeyCode::B, KeyCode::C })
	{
		log->Record(FrameWithKey(key));
	}

	std::vector<std::string> events;
	Application application(HeadlessSpecification());
	application.SetInputSource(CreateScope<ReplayInputSource>(log));
	REQUIRE(application.Initialize().HasValue());

	std::vector<KeyCode> seen;
	for (int tick = 0; tick < 5; ++tick)
	{
		application.RunFrames(1);
		const InputFrame& frame = application.GetInputState().GetFrame();
		seen.push_back(
			frame.IsKeyDown(KeyCode::A)       ? KeyCode::A
				: frame.IsKeyDown(KeyCode::B) ? KeyCode::B
				: frame.IsKeyDown(KeyCode::C) ? KeyCode::C
											  : KeyCode::None);
	}
	CHECK(seen == std::vector<KeyCode>{ KeyCode::A, KeyCode::B, KeyCode::C, KeyCode::None, KeyCode::None });
	CHECK(application.GetInputSource().IsFinished());
}

TEST_CASE("Core: Application records the input of every tick")
{
	auto source = CreateRef<InputLog>();
	for (int i = 0; i < 4; ++i)
	{
		InputFrame frame;
		frame.MouseX = i * 64;
		source->Record(frame);
	}

	Application application(HeadlessSpecification());
	application.SetInputSource(CreateScope<ReplayInputSource>(source));
	const auto recording = CreateRef<InputLog>(InputLogHeader{ "Project", "Scenes/Main.gscene", 99, 60 });
	application.SetInputRecorder(recording);
	REQUIRE(application.Initialize().HasValue());

	application.RunFrames(3);
	application.StepFixed(FrameWithKey(KeyCode::X)); // a manually stepped tick is recorded as well
	REQUIRE(recording->GetFrameCount() == 4);
	CHECK(recording->GetFrame(0) == source->GetFrame(0));
	CHECK(recording->GetFrame(2) == source->GetFrame(2));
	CHECK(recording->GetFrame(3).IsKeyDown(KeyCode::X));

	application.SetInputRecorder(nullptr);
	application.StepFixed(InputFrame{});
	CHECK(recording->GetFrameCount() == 4);
}

TEST_CASE("Core: Application without a source reads no input and a null source restores the default")
{
	Application application(HeadlessSpecification());
	REQUIRE(application.Initialize().HasValue());
	application.RunFrames(2);
	CHECK(application.GetInputState().GetFrame() == InputFrame{});

	auto log = CreateRef<InputLog>();
	log->Record(FrameWithKey(KeyCode::Q));
	application.SetInputSource(CreateScope<ReplayInputSource>(log));
	application.RunFrames(1);
	CHECK(application.GetInputState().GetFrame().IsKeyDown(KeyCode::Q));

	application.SetInputSource(nullptr);
	application.RunFrames(1);
	CHECK(application.GetInputState().GetFrame() == InputFrame{});
}

TEST_CASE("Core: Application sends events to the layers from the top")
{
	std::vector<std::string> log;
	Application application(HeadlessSpecification());
	application.PushLayer(CreateScope<RecordingLayer>("A", log));
	application.PushLayer(CreateScope<RecordingLayer>("B", log));
	application.PushOverlay(CreateScope<RecordingLayer>("O", log));
	log.clear();

	KeyPressedEvent event(KeyCode::W);
	application.OnEvent(event);
	CHECK(
		log
		== std::vector<std::string>{ "O:event KeyPressed (key W)", "B:event KeyPressed (key W)",
	                                 "A:event KeyPressed (key W)" });
	CHECK_FALSE(event.Handled);
}

TEST_CASE("Core: Application stops delivering a handled event")
{
	std::vector<std::string> log;
	Application application(HeadlessSpecification());
	application.PushLayer(CreateScope<RecordingLayer>("A", log));
	auto middle = CreateScope<RecordingLayer>("B", log);
	middle->HandleEvents = true;
	application.PushLayer(std::move(middle));
	application.PushOverlay(CreateScope<RecordingLayer>("O", log));
	log.clear();

	KeyReleasedEvent event(KeyCode::W);
	application.OnEvent(event);
	CHECK(log == std::vector<std::string>{ "O:event KeyReleased (key W)", "B:event KeyReleased (key W)" });
	CHECK(event.Handled);
}

namespace
{
	// Asks the application to close during the first tick, as the window would when the user clicks the close button.
	class CloseRequestLayer : public Layer
	{
	public:
		explicit CloseRequestLayer(bool veto)
			: Layer("CloseRequest"), m_Veto(veto)
		{}

		void OnFixedUpdate(TimeStep step) override
		{
			if (step.GetTick() == 1)
			{
				WindowCloseEvent event;
				Application::Get().OnEvent(event);
			}
		}

		void OnEvent(Event& event) override
		{
			if (m_Veto && event.GetEventType() == EventType::WindowClose)
			{
				event.Handled = true; // e.g. an editor asking "save changes?"
			}
		}

	private:
		bool m_Veto;
	};
}

TEST_CASE("Core: Application closes on an unhandled window close event only")
{
	ApplicationSpecification specification = HeadlessSpecification();
	specification.FrameLimit = 4;

	{
		Application application(specification);
		application.PushLayer(CreateScope<CloseRequestLayer>(false));
		CHECK(application.Run() == 0);
		CHECK(application.GetFrameCount() == 1); // closed in the first frame
	}
	{
		Application application(specification);
		application.PushLayer(CreateScope<CloseRequestLayer>(true));
		CHECK(application.Run() == 0);
		CHECK(application.GetFrameCount() == 4); // vetoed: runs to the frame limit
	}
}

TEST_CASE("Core: Application Run stops at the frame limit with synthetic frames")
{
	ApplicationSpecification specification = HeadlessSpecification();
	specification.FrameLimit = 10;
	std::vector<std::string> log;
	Application application(specification);
	auto layer = CreateScope<RecordingLayer>("A", log);
	RecordingLayer* recorder = layer.get();
	application.PushLayer(std::move(layer));

	const auto start = std::chrono::steady_clock::now();
	CHECK(application.Run() == 0);
	const auto elapsed = std::chrono::steady_clock::now() - start;
	CHECK(application.GetFrameCount() == 10);
	CHECK(application.GetTick() == 10); // one tick per synthetic frame
	CHECK(recorder->FixedSteps.size() == 10);
	CHECK(elapsed < std::chrono::seconds(2)); // no waiting for real time
	CHECK_FALSE(application.IsRunning());
}

TEST_CASE("Core: Application Run stops when a layer closes it")
{
	ApplicationSpecification specification = HeadlessSpecification();
	specification.FrameLimit = 1000;
	std::vector<std::string> log;
	Application application(specification);
	auto layer = CreateScope<RecordingLayer>("A", log);
	layer->CloseAtTick = 7;
	application.PushLayer(std::move(layer));

	CHECK(application.Run() == 0);
	CHECK(application.GetTick() == 7);
	CHECK(application.GetFrameCount() == 7); // the frame in which Close was called still completes
}

TEST_CASE("Core: Application Run reports a failed initialization")
{
	LogConfig config = Tests::MakeTestLogConfig();
	config.Console = false;
	Tests::ScopedLogInit logInit(config);

	ApplicationSpecification specification = HeadlessSpecification();
	specification.WorkingDirectory = "/this/directory/does/not/exist";
	Application application(specification);
	const Result<void> initialized = application.Initialize();
	REQUIRE_FALSE(initialized.HasValue());
	CHECK(initialized.GetError().Message.find("working directory") != std::string::npos);
	CHECK_FALSE(application.IsInitialized());
	CHECK(application.Run() == 1);
}

TEST_CASE("Core: Application Initialize applies the working directory")
{
	const ScopedWorkingDirectory restore;
	Tests::ScopedTempDirectory directory;

	ApplicationSpecification specification = HeadlessSpecification();
	specification.WorkingDirectory = directory.GetPath();
	Application application(specification);
	REQUIRE(application.Initialize().HasValue());
	CHECK(std::filesystem::equivalent(std::filesystem::current_path(), directory.GetPath()));
}

TEST_CASE("Core: Application attaches and detaches layers")
{
	std::vector<std::string> log;
	{
		Application application(HeadlessSpecification());
		Layer* first = application.PushLayer(CreateScope<RecordingLayer>("A", log));
		application.PushLayer(CreateScope<RecordingLayer>("B", log));
		application.PushOverlay(CreateScope<RecordingLayer>("O", log));
		CHECK(log == std::vector<std::string>{ "A:attach", "B:attach", "O:attach" });

		log.clear();
		Scope<Layer> removed = application.PopLayer(first);
		REQUIRE(removed != nullptr);
		CHECK(removed.get() == first);
		CHECK(log == std::vector<std::string>{ "A:detach" });
		CHECK(application.PopLayer(first) == nullptr); // no longer in the stack
		log.clear();
	}
	// Destruction detaches from the top down.
	CHECK(log == std::vector<std::string>{ "O:detach", "B:detach" });
}

TEST_CASE("Core: Application tolerates layers that change the stack during a frame")
{
	struct SelfRemovingLayer : Layer
	{
		SelfRemovingLayer(int& updates, Scope<Layer>& graveyard)
			: Layer("Remover"), Updates(updates), Graveyard(graveyard)
		{}
		void OnFixedUpdate(TimeStep) override
		{
			++Updates;
			Graveyard = Application::Get().PopLayer(this); // the layer removes itself while it is being updated
		}
		int& Updates;
		Scope<Layer>& Graveyard;
	};

	std::vector<std::string> log;
	Application application(HeadlessSpecification());
	int updates = 0;
	Scope<Layer> graveyard;
	application.PushLayer(CreateScope<SelfRemovingLayer>(updates, graveyard));
	application.PushLayer(CreateScope<RecordingLayer>("B", log));
	REQUIRE(application.Initialize().HasValue());
	log.clear();

	application.RunFrames(3);
	CHECK(updates == 1);
	CHECK(Count(log, "B:fixed") == 3);
	CHECK(graveyard != nullptr);
}

TEST_CASE("Core: ApplicationCommandLineArgs finds flags and values")
{
	const char* argv[] = { "program", "--headless", "--frames", "12", "--hz=90", "--lonely" };
	const ApplicationCommandLineArgs args(6, argv);
	CHECK(args.Args.size() == 6);
	CHECK(args.HasFlag("--headless"));
	CHECK_FALSE(args.HasFlag("--head"));
	CHECK(args.GetValue("--frames") == "12");
	CHECK(args.GetValue("--hz") == "90");
	CHECK_FALSE(args.GetValue("--lonely").has_value());
	CHECK_FALSE(args.GetValue("--missing").has_value());
	CHECK(ApplicationCommandLineArgs().Args.empty());
}

TEST_CASE("Core: ApplyStandardArguments sets headless mode, frame limit and rate")
{
	const char* argv[] = { "program", "--headless", "--frames", "10", "--hz", "120" };
	ApplicationSpecification specification;
	REQUIRE(ApplyStandardArguments(ApplicationCommandLineArgs(6, argv), specification).HasValue());
	CHECK(specification.Headless);
	CHECK(specification.FrameLimit == 10);
	CHECK(specification.SimulationHz == 120);
	CHECK(specification.CommandLineArgs.Args.size() == 6);

	const char* equalsForm[] = { "program", "--frames=7" };
	ApplicationSpecification other;
	REQUIRE(ApplyStandardArguments(ApplicationCommandLineArgs(2, equalsForm), other).HasValue());
	CHECK_FALSE(other.Headless);
	CHECK(other.FrameLimit == 7);
	CHECK(other.SimulationHz == 60);

	ApplicationSpecification untouched;
	const char* none[] = { "program" };
	REQUIRE(ApplyStandardArguments(ApplicationCommandLineArgs(1, none), untouched).HasValue());
	CHECK(untouched.FrameLimit == 0);
}

TEST_CASE("Core: ApplyStandardArguments rejects malformed values")
{
	for (const char* bad : { "--frames", "--frames=", "--frames=abc", "--frames=0", "--frames=-3", "--frames=1.5",
	                         "--frames=99999999999999999999", "--hz", "--hz=0", "--hz=1001", "--hz=fast" })
	{
		const char* argv[] = { "program", bad };
		ApplicationSpecification specification;
		const Result<void> result = ApplyStandardArguments(ApplicationCommandLineArgs(2, argv), specification);
		CHECK_MESSAGE(!result.HasValue(), "argument: ", bad);
		if (!result.HasValue())
		{
			CHECK_FALSE(result.GetError().Message.empty());
		}
	}
	const char* separate[] = { "program", "--frames", "abc" };
	ApplicationSpecification specification;
	CHECK_FALSE(ApplyStandardArguments(ApplicationCommandLineArgs(3, separate), specification).HasValue());
}

// ---- determinism ----

namespace
{
	// A tiny simulation: a position driven by the "Move" action plus world randomness, hashed after every tick.
	class SimulationLayer : public Layer
	{
	public:
		explicit SimulationLayer(uint64_t seed)
			: Layer("Simulation"), m_Random(seed)
		{}

		void OnFixedUpdate(TimeStep step) override
		{
			const glm::vec2 move = Application::Get().GetInputState().GetAxis2D("Move");
			m_Position += move * step.GetSeconds() * 5.0f;
			m_Position.x += m_Random.RangeFloat(-0.01f, 0.01f);
			m_Position.y += m_Random.NextGaussian(0.0f, 0.01f);

			Hasher hasher;
			hasher.UpdateValue(step.GetTick());
			hasher.UpdateValue(m_Position.x);
			hasher.UpdateValue(m_Position.y);
			hasher.UpdateValue(m_Random.GetState().State);
			Hashes.push_back(hasher.Digest());
		}

		std::vector<uint64_t> Hashes;

	private:
		Random m_Random;
		glm::vec2 m_Position{ 0.0f };
	};

	Ref<const InputActions> MoveActions()
	{
		const Result<InputActions> actions = InputActions::Parse(R"({
			"Version": 1,
			"Actions": [
				{ "Name": "Move", "Type": "Axis2D", "Bindings": [
					{ "Type": "Composite2D", "Up": "W", "Down": "S", "Left": "A", "Right": "D" } ] }
			]
		})");
		REQUIRE(actions.HasValue());
		return CreateRef<InputActions>(*actions);
	}

	Ref<InputLog> MakeMoveLog(int frames)
	{
		auto log = CreateRef<InputLog>(InputLogHeader{ "Test", "Scenes/Test.gscene", 4242, 60 });
		for (int i = 0; i < frames; ++i)
		{
			InputFrame frame;
			frame.SetKey(KeyCode::W, (i / 10) % 2 == 0);
			frame.SetKey(KeyCode::D, (i / 7) % 3 == 0);
			frame.SetKey(KeyCode::A, (i / 13) % 5 == 0);
			log->Record(frame);
		}
		return log;
	}

	std::vector<uint64_t>
	RunSimulation(const Ref<InputLog>& log, const std::vector<nanoseconds>& frameTimes, size_t ticks)
	{
		Application application(HeadlessSpecification());
		auto layer = CreateScope<SimulationLayer>(log->GetHeader().Seed);
		SimulationLayer* simulation = layer.get();
		application.PushLayer(std::move(layer));
		application.SetInputActions(MoveActions());
		application.SetInputSource(CreateScope<ReplayInputSource>(log));
		REQUIRE(application.Initialize().HasValue());

		for (size_t frame = 0; simulation->Hashes.size() < ticks; ++frame)
		{
			application.RunFrame(frameTimes[frame % frameTimes.size()]);
		}
		simulation->Hashes.resize(ticks);
		return simulation->Hashes;
	}
}

TEST_CASE("Core: the simulation is identical for any frame rate when the input is the same")
{
	constexpr size_t Ticks = 300;
	const Ref<InputLog> log = MakeMoveLog(Ticks + 10);

	const std::vector<uint64_t> steady = RunSimulation(log, { nanoseconds(16'666'667) }, Ticks);
	REQUIRE(steady.size() == Ticks);
	CHECK(steady == RunSimulation(log, { nanoseconds(16'666'667) }, Ticks)); // repeatable

	// A slow, irregular render rate (ticks arrive in bursts of 0 to 3) and a fast one produce the same tick states.
	CHECK(
		steady
		== RunSimulation(
			log,
			{ nanoseconds(5'000'000), nanoseconds(41'000'000), nanoseconds(7'000'000), nanoseconds(50'000'000),
	          nanoseconds(1'000'000) },
			Ticks));
	CHECK(steady == RunSimulation(log, { nanoseconds(2'000'000) }, Ticks));

	// A different seed or different input gives a different history from the first tick on.
	auto otherSeed = CreateRef<InputLog>(*log);
	otherSeed->SetHeader({ "Test", "Scenes/Test.gscene", 4243, 60 });
	CHECK(steady != RunSimulation(otherSeed, { nanoseconds(16'666'667) }, Ticks));
}
