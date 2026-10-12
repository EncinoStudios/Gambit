#include "gbpch.h"

#include "Gambit/Core/Log.h"

#include "Gambit/Core/Assert.h"
#include "Gambit/Core/LogSinks.h"

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <iterator>
#include <new>

namespace Gambit
{
	namespace
	{
		struct LogState
		{
			Logger Core{ "GAMBIT" };
			Logger Client{ "APP" };
			std::mutex Mutex; // guards the members below; the loggers have their own locks
			Ref<RingBufferSink> RingBuffer;
			bool Initialized = false;
		};

		LogState& GetState()
		{
			// Never destroyed: statics in other translation units may log from their destructors, so the loggers
			// must outlive every one of them. An atexit hook releases the sinks (flushing the log file) instead.
			alignas(LogState) static std::byte storage[sizeof(LogState)];
			static LogState* const state = [] {
				LogState* created = new (storage) LogState();
				std::atexit([] { Log::Shutdown(); });
				return created;
			}();
			return *state;
		}

		std::tm ToLocalTime(std::time_t time)
		{
			std::tm local{};
#if defined(GB_PLATFORM_WINDOWS)
			localtime_s(&local, &time);
#else
			localtime_r(&time, &local);
#endif
			return local;
		}

		std::string_view ColorPrefix(LogLevel level) noexcept
		{
			switch (level)
			{
				case LogLevel::Trace:
					return "\x1b[90m"; // gray
				case LogLevel::Info:
					return "\x1b[32m"; // green
				case LogLevel::Warn:
					return "\x1b[33m"; // yellow
				case LogLevel::Error:
					return "\x1b[31m"; // red
				case LogLevel::Critical:
					return "\x1b[1;97;41m"; // bold white on red
				case LogLevel::Off:
					break;
			}
			return {};
		}

		constexpr std::string_view ColorReset = "\x1b[0m";
	}

	std::string_view ToString(LogLevel level) noexcept
	{
		switch (level)
		{
			case LogLevel::Trace:
				return "trace";
			case LogLevel::Info:
				return "info";
			case LogLevel::Warn:
				return "warn";
			case LogLevel::Error:
				return "error";
			case LogLevel::Critical:
				return "critical";
			case LogLevel::Off:
				return "off";
		}
		return "unknown";
	}

	std::string FormatLogRecord(const LogRecord& record, bool colored)
	{
		using namespace std::chrono;

		const std::tm local = ToLocalTime(system_clock::to_time_t(record.Time));
		const auto sinceEpoch = duration_cast<milliseconds>(record.Time.time_since_epoch());
		const long long millis = ((sinceEpoch.count() % 1000) + 1000) % 1000;

		std::string line;
		line.reserve(record.Message.size() + record.LoggerName.size() + 48);
		if (colored)
		{
			line += ColorPrefix(record.Level);
		}
		std::format_to(
			std::back_inserter(line), "[{:02}:{:02}:{:02}.{:03}] [{}] [{}] {}", local.tm_hour, local.tm_min,
			local.tm_sec, millis, record.LoggerName, ToString(record.Level), record.Message);
		if (colored)
		{
			line += ColorReset;
		}
		line += '\n';
		return line;
	}

	Logger::Logger(std::string name, LogLevel level)
		: m_Name(std::move(name)), m_Level(level)
	{}

	void Logger::AddSink(Ref<LogSink> sink)
	{
		GB_CORE_ASSERT(sink != nullptr, "Logger::AddSink requires a sink");
		if (!sink)
		{
			return;
		}
		std::scoped_lock lock(m_Mutex);
		m_Sinks.push_back(std::move(sink));
	}

	bool Logger::RemoveSink(const Ref<LogSink>& sink)
	{
		std::scoped_lock lock(m_Mutex);
		const auto it = std::find(m_Sinks.begin(), m_Sinks.end(), sink);
		if (it == m_Sinks.end())
		{
			return false;
		}
		m_Sinks.erase(it);
		return true;
	}

	void Logger::ClearSinks()
	{
		std::scoped_lock lock(m_Mutex);
		m_Sinks.clear();
	}

	bool Logger::HasSinks() const
	{
		std::scoped_lock lock(m_Mutex);
		return !m_Sinks.empty();
	}

	void Logger::Flush()
	{
		std::scoped_lock lock(m_Mutex);
		for (const Ref<LogSink>& sink : m_Sinks)
		{
			sink->Flush();
		}
	}

	void Logger::Submit(LogLevel level, std::string_view message)
	{
		if (!ShouldLog(level))
		{
			return;
		}

		const LogRecord record{ level, m_Name, message, std::chrono::system_clock::now() };
		std::scoped_lock lock(m_Mutex);
		for (const Ref<LogSink>& sink : m_Sinks)
		{
			if (sink->ShouldLog(level))
			{
				sink->Write(record);
			}
		}
	}

	void Logger::SubmitFormatFailure(LogLevel level, const char* reason)
	{
		std::string message = "failed to format a ";
		message += ToString(level);
		message += " log message: ";
		message += reason;
		Submit(LogLevel::Error, message);
	}

	void Log::Init(const LogConfig& config)
	{
		Shutdown();

		LogState& state = GetState();
		std::string fileProblem;
		{
			std::scoped_lock lock(state.Mutex);
			state.Core.SetLevel(config.Level);
			state.Client.SetLevel(config.Level);

			const auto attach = [&state](const Ref<LogSink>& sink) {
				state.Core.AddSink(sink);
				state.Client.AddSink(sink);
			};

			if (config.Console)
			{
				const Ref<ConsoleSink> console = CreateRef<ConsoleSink>(config.ConsoleTarget);
				console->SetLevel(config.ConsoleLevel);
				attach(console);
			}

			if (!config.FilePath.empty())
			{
				Result<Ref<FileSink>> file = FileSink::Create(config.FilePath);
				if (file.HasValue())
				{
					attach(file.Value());
				}
				else
				{
					fileProblem = file.GetError().Message;
				}
			}

			if (config.RingBufferCapacity > 0)
			{
				state.RingBuffer = CreateRef<RingBufferSink>(config.RingBufferCapacity);
				attach(state.RingBuffer);
			}

			state.Initialized = true;
		}

		if (!fileProblem.empty())
		{
			GB_CORE_WARN("File logging is disabled: {}", fileProblem);
		}
	}

	void Log::Shutdown()
	{
		LogState& state = GetState();
		std::scoped_lock lock(state.Mutex);
		state.Core.Flush();
		state.Client.Flush();
		state.Core.ClearSinks();
		state.Client.ClearSinks();
		state.RingBuffer.reset();
		state.Initialized = false;
	}

	bool Log::IsInitialized()
	{
		LogState& state = GetState();
		std::scoped_lock lock(state.Mutex);
		return state.Initialized;
	}

	Logger& Log::GetCoreLogger()
	{
		return GetState().Core;
	}

	Logger& Log::GetClientLogger()
	{
		return GetState().Client;
	}

	Ref<RingBufferSink> Log::GetRingBuffer()
	{
		LogState& state = GetState();
		std::scoped_lock lock(state.Mutex);
		return state.RingBuffer;
	}

	void Log::AddSink(const Ref<LogSink>& sink)
	{
		LogState& state = GetState();
		state.Core.AddSink(sink);
		state.Client.AddSink(sink);
	}

	void Log::RemoveSink(const Ref<LogSink>& sink)
	{
		LogState& state = GetState();
		state.Core.RemoveSink(sink);
		state.Client.RemoveSink(sink);
	}

	void Log::SetLevel(LogLevel level)
	{
		LogState& state = GetState();
		state.Core.SetLevel(level);
		state.Client.SetLevel(level);
	}

	void Log::Flush()
	{
		LogState& state = GetState();
		state.Core.Flush();
		state.Client.Flush();
	}
}
