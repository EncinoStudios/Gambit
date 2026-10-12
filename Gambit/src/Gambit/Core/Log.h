#pragma once

#include "Gambit/Core/Base.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <format>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Gambit
{
	class RingBufferSink;

	enum class LogLevel : uint8_t
	{
		Trace = 0,
		Info,
		Warn,
		Error,
		Critical,
		Off // threshold only: disables a logger or sink, never used for a message
	};

	// Lower-case name of a level ("trace", "info", "warn", "error", "critical", "off").
	[[nodiscard]] std::string_view ToString(LogLevel level) noexcept;

	// One log event as delivered to sinks. The string views are only valid for the duration of LogSink::Write.
	struct LogRecord
	{
		LogLevel Level = LogLevel::Info;
		std::string_view LoggerName;
		std::string_view Message;
		std::chrono::system_clock::time_point Time;
	};

	// Renders "[HH:MM:SS.mmm] [LOGGER] [level] message\n" in local time, optionally wrapped in ANSI colors.
	[[nodiscard]] std::string FormatLogRecord(const LogRecord& record, bool colored = false);

	// Destination for log records. A logger calls Write while holding its own lock, so a sink must not log, assert
	// or call back into the logger from Write. Sinks that may be shared between loggers (the built-in ones are)
	// must be thread-safe themselves.
	class LogSink
	{
	public:
		virtual ~LogSink() = default;

		virtual void Write(const LogRecord& record) = 0;
		virtual void Flush() {}

		// Records below this level are not delivered to the sink.
		void SetLevel(LogLevel level) noexcept { m_Level.store(level, std::memory_order_relaxed); }
		[[nodiscard]] LogLevel GetLevel() const noexcept { return m_Level.load(std::memory_order_relaxed); }
		[[nodiscard]] bool ShouldLog(LogLevel level) const noexcept { return level >= GetLevel(); }

	private:
		std::atomic<LogLevel> m_Level{ LogLevel::Trace };
	};

	// A named, thread-safe logger that formats with std::format and fans records out to its sinks.
	class Logger
	{
	public:
		explicit Logger(std::string name, LogLevel level = LogLevel::Trace);
		Logger(const Logger&) = delete;
		Logger& operator=(const Logger&) = delete;

		[[nodiscard]] const std::string& GetName() const noexcept { return m_Name; }

		void SetLevel(LogLevel level) noexcept { m_Level.store(level, std::memory_order_relaxed); }
		[[nodiscard]] LogLevel GetLevel() const noexcept { return m_Level.load(std::memory_order_relaxed); }
		[[nodiscard]] bool ShouldLog(LogLevel level) const noexcept
		{
			return level != LogLevel::Off && level >= GetLevel();
		}

		void AddSink(Ref<LogSink> sink);
		// Returns false when the sink was not attached.
		bool RemoveSink(const Ref<LogSink>& sink);
		void ClearSinks();
		[[nodiscard]] bool HasSinks() const;
		void Flush();

		// Delivers an already formatted message to every sink that accepts its level.
		void Submit(LogLevel level, std::string_view message);

		// Formats and delivers a message. The format string is checked at compile time. Formatting problems (a
		// throwing user formatter) are reported as an error record instead of propagating: logging never throws.
		template<typename... Args>
		void Write(LogLevel level, std::format_string<Args...> format, Args&&... args)
		{
			if (!ShouldLog(level))
			{
				return;
			}
			try
			{
				Submit(level, std::format(format, std::forward<Args>(args)...));
			}
			catch (const std::exception& exception)
			{
				SubmitFormatFailure(level, exception.what());
			}
		}

		template<typename... Args>
		void Trace(std::format_string<Args...> format, Args&&... args)
		{
			Write(LogLevel::Trace, format, std::forward<Args>(args)...);
		}

		template<typename... Args>
		void Info(std::format_string<Args...> format, Args&&... args)
		{
			Write(LogLevel::Info, format, std::forward<Args>(args)...);
		}

		template<typename... Args>
		void Warn(std::format_string<Args...> format, Args&&... args)
		{
			Write(LogLevel::Warn, format, std::forward<Args>(args)...);
		}

		template<typename... Args>
		void Error(std::format_string<Args...> format, Args&&... args)
		{
			Write(LogLevel::Error, format, std::forward<Args>(args)...);
		}

		template<typename... Args>
		void Critical(std::format_string<Args...> format, Args&&... args)
		{
			Write(LogLevel::Critical, format, std::forward<Args>(args)...);
		}

	private:
		void SubmitFormatFailure(LogLevel level, const char* reason);

		std::string m_Name;
		std::atomic<LogLevel> m_Level;
		mutable std::mutex m_Mutex; // guards m_Sinks and serializes delivery so records stay ordered per logger
		std::vector<Ref<LogSink>> m_Sinks;
	};

	enum class ConsoleStream : uint8_t
	{
		StdOut,
		StdErr // for tools whose stdout carries data, e.g. the MCP server (D-051)
	};

	// What Log::Init sets up. The defaults are a colored console on stdout plus a 2048-record ring buffer.
	struct LogConfig
	{
		LogLevel Level = LogLevel::Trace; // threshold of both loggers
		bool Console = true;
		ConsoleStream ConsoleTarget = ConsoleStream::StdOut;
		LogLevel ConsoleLevel = LogLevel::Trace;
		std::filesystem::path FilePath;   // empty: no log file
		size_t RingBufferCapacity = 2048; // 0: no ring buffer
	};

	// Global access to the two engine loggers: "GAMBIT" (engine code, GB_CORE_* macros) and "APP" (games and
	// tools, GB_* macros). Before Init and after Shutdown the loggers exist but have no sinks, so logging is a no-op.
	class Log
	{
	public:
		// Replaces any previous configuration. A log file that cannot be opened is reported as a warning; startup
		// continues without it.
		static void Init(const LogConfig& config = {});
		// Flushes and detaches all sinks (closing the log file).
		static void Shutdown();
		[[nodiscard]] static bool IsInitialized();

		[[nodiscard]] static Logger& GetCoreLogger();
		[[nodiscard]] static Logger& GetClientLogger();
		// The ring buffer created by Init (editor console, automation API), or null when disabled.
		[[nodiscard]] static Ref<RingBufferSink> GetRingBuffer();

		// Attaches a sink to both loggers.
		static void AddSink(const Ref<LogSink>& sink);
		static void RemoveSink(const Ref<LogSink>& sink);
		static void SetLevel(LogLevel level);
		static void Flush();
	};
}

// The level is a token (Trace, Info, ...) so the check below happens before the arguments are evaluated.
#define GB_INTERNAL_LOG(logger, level, ...)                                                                            \
	do                                                                                                                 \
	{                                                                                                                  \
		::Gambit::Logger& gbLogger = (logger);                                                                         \
		if (gbLogger.ShouldLog(::Gambit::LogLevel::level))                                                             \
		{                                                                                                              \
			gbLogger.Write(::Gambit::LogLevel::level, __VA_ARGS__);                                                    \
		}                                                                                                              \
	} while (false)

// Engine loggers
#define GB_CORE_TRACE(...) GB_INTERNAL_LOG(::Gambit::Log::GetCoreLogger(), Trace, __VA_ARGS__)
#define GB_CORE_INFO(...) GB_INTERNAL_LOG(::Gambit::Log::GetCoreLogger(), Info, __VA_ARGS__)
#define GB_CORE_WARN(...) GB_INTERNAL_LOG(::Gambit::Log::GetCoreLogger(), Warn, __VA_ARGS__)
#define GB_CORE_ERROR(...) GB_INTERNAL_LOG(::Gambit::Log::GetCoreLogger(), Error, __VA_ARGS__)
#define GB_CORE_CRITICAL(...) GB_INTERNAL_LOG(::Gambit::Log::GetCoreLogger(), Critical, __VA_ARGS__)

// Client (game, editor, tool) loggers
#define GB_TRACE(...) GB_INTERNAL_LOG(::Gambit::Log::GetClientLogger(), Trace, __VA_ARGS__)
#define GB_INFO(...) GB_INTERNAL_LOG(::Gambit::Log::GetClientLogger(), Info, __VA_ARGS__)
#define GB_WARN(...) GB_INTERNAL_LOG(::Gambit::Log::GetClientLogger(), Warn, __VA_ARGS__)
#define GB_ERROR(...) GB_INTERNAL_LOG(::Gambit::Log::GetClientLogger(), Error, __VA_ARGS__)
#define GB_CRITICAL(...) GB_INTERNAL_LOG(::Gambit::Log::GetClientLogger(), Critical, __VA_ARGS__)
