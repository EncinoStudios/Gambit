#include <doctest/doctest.h>

#include "TestSupport.h"

#include "Gambit/Core/Log.h"
#include "Gambit/Core/LogSinks.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
	using namespace Gambit;

	struct Fixture
	{
		Logger Test{ "TEST" };
		Ref<RingBufferSink> Ring = CreateRef<RingBufferSink>(16);

		Fixture() { Test.AddSink(Ring); }
	};

	// A type whose formatter fails, to prove that logging never throws.
	struct Unformattable
	{};
}

template<>
struct std::formatter<Unformattable>
{
	constexpr auto parse(std::format_parse_context& context) { return context.begin(); }

	// Templated on the context because the standard lets an implementation pick the output iterator; libc++ checks
	// formattability with its own, so a formatter hard-wired to std::format_context is rejected there.
	template<typename FormatContext>
	auto format(const Unformattable&, FormatContext&) const -> typename FormatContext::iterator
	{
		throw std::runtime_error("cannot format");
	}
};

TEST_CASE("Log: level names")
{
	CHECK(ToString(LogLevel::Trace) == "trace");
	CHECK(ToString(LogLevel::Info) == "info");
	CHECK(ToString(LogLevel::Warn) == "warn");
	CHECK(ToString(LogLevel::Error) == "error");
	CHECK(ToString(LogLevel::Critical) == "critical");
	CHECK(ToString(LogLevel::Off) == "off");
}

TEST_CASE("Log: logger formats with std::format and delivers records to sinks")
{
	Fixture f;
	f.Test.Info("answer {} and {:>5}|{:.2f}", 42, "ab", 3.14159);

	const std::vector<LogEntry> entries = f.Ring->Snapshot();
	REQUIRE(entries.size() == 1);
	CHECK(entries[0].Message == "answer 42 and    ab|3.14");
	CHECK(entries[0].Level == LogLevel::Info);
	CHECK(entries[0].LoggerName == "TEST");
	CHECK(entries[0].Sequence == 1);
	const auto age = std::chrono::system_clock::now() - entries[0].Time;
	CHECK(age < std::chrono::minutes(1));
}

TEST_CASE("Log: every level helper uses its level")
{
	Fixture f;
	f.Test.Trace("t");
	f.Test.Info("i");
	f.Test.Warn("w");
	f.Test.Error("e");
	f.Test.Critical("c");

	const std::vector<LogEntry> entries = f.Ring->Snapshot();
	REQUIRE(entries.size() == 5);
	CHECK(entries[0].Level == LogLevel::Trace);
	CHECK(entries[1].Level == LogLevel::Info);
	CHECK(entries[2].Level == LogLevel::Warn);
	CHECK(entries[3].Level == LogLevel::Error);
	CHECK(entries[4].Level == LogLevel::Critical);
}

TEST_CASE("Log: logger level filters messages")
{
	Fixture f;
	f.Test.SetLevel(LogLevel::Warn);
	CHECK(f.Test.GetLevel() == LogLevel::Warn);
	CHECK_FALSE(f.Test.ShouldLog(LogLevel::Info));
	CHECK(f.Test.ShouldLog(LogLevel::Warn));

	f.Test.Trace("dropped");
	f.Test.Info("dropped");
	f.Test.Warn("kept");
	f.Test.Critical("kept");
	CHECK(f.Ring->GetSize() == 2);

	f.Test.SetLevel(LogLevel::Off);
	f.Test.Critical("dropped too");
	CHECK(f.Ring->GetSize() == 2);
}

TEST_CASE("Log: sink level filters independently of the logger")
{
	Fixture f;
	const Ref<RingBufferSink> errorsOnly = CreateRef<RingBufferSink>(8);
	errorsOnly->SetLevel(LogLevel::Error);
	f.Test.AddSink(errorsOnly);

	f.Test.Info("info");
	f.Test.Error("error");

	CHECK(f.Ring->GetSize() == 2);
	REQUIRE(errorsOnly->GetSize() == 1);
	CHECK(errorsOnly->Snapshot()[0].Message == "error");
}

TEST_CASE("Log: sinks can be added and removed")
{
	Fixture f;
	CHECK(f.Test.HasSinks());

	const Ref<RingBufferSink> second = CreateRef<RingBufferSink>(4);
	f.Test.AddSink(second);
	f.Test.Info("both");
	CHECK(f.Ring->GetSize() == 1);
	CHECK(second->GetSize() == 1);

	CHECK(f.Test.RemoveSink(second));
	CHECK_FALSE(f.Test.RemoveSink(second));
	f.Test.Info("only first");
	CHECK(f.Ring->GetSize() == 2);
	CHECK(second->GetSize() == 1);

	f.Test.ClearSinks();
	CHECK_FALSE(f.Test.HasSinks());
	f.Test.Info("nobody listens");
	CHECK(f.Ring->GetSize() == 2);
}

TEST_CASE("Log: a failing formatter is reported instead of thrown")
{
	Fixture f;
	CHECK_NOTHROW(f.Test.Warn("value {}", Unformattable{}));

	const std::vector<LogEntry> entries = f.Ring->Snapshot();
	REQUIRE(entries.size() == 1);
	CHECK(entries[0].Level == LogLevel::Error);
	CHECK(entries[0].Message == "failed to format a warn log message: cannot format");
}

TEST_CASE("Log: record formatting")
{
	LogRecord record;
	record.Level = LogLevel::Warn;
	record.LoggerName = "GAMBIT";
	record.Message = "hello world";
	record.Time = std::chrono::system_clock::time_point{} + std::chrono::milliseconds(123);

	const std::string line = FormatLogRecord(record);

	// "[HH:MM:SS.mmm] [GAMBIT] [warn] hello world\n"; the hour depends on the time zone, the rest does not.
	REQUIRE(line.size() > 14);
	CHECK(line[0] == '[');
	CHECK(line[3] == ':');
	CHECK(line[6] == ':');
	CHECK(line[9] == '.');
	CHECK(line.substr(10, 3) == "123");
	CHECK(line.substr(13) == "] [GAMBIT] [warn] hello world\n");

	const std::string colored = FormatLogRecord(record, true);
	CHECK(colored.starts_with("\x1b["));
	CHECK(colored.ends_with("hello world\x1b[0m\n"));
	CHECK(colored.find("\x1b[33m") == 0);
}

TEST_CASE("Log: ring buffer keeps the newest records")
{
	RingBufferSink ring(3);
	CHECK(ring.GetCapacity() == 3);
	CHECK(ring.GetSize() == 0);
	CHECK(ring.Snapshot().empty());

	Logger logger("RING");
	const Ref<RingBufferSink> shared = CreateRef<RingBufferSink>(3);
	logger.AddSink(shared);
	for (int i = 1; i <= 5; ++i)
	{
		logger.Info("message {}", i);
	}

	CHECK(shared->GetSize() == 3);
	CHECK(shared->GetTotalWritten() == 5);

	const std::vector<LogEntry> entries = shared->Snapshot();
	REQUIRE(entries.size() == 3);
	CHECK(entries[0].Message == "message 3");
	CHECK(entries[1].Message == "message 4");
	CHECK(entries[2].Message == "message 5");
	CHECK(entries[0].Sequence == 3);
	CHECK(entries[2].Sequence == 5);

	const std::vector<LogEntry> newer = shared->SnapshotAfter(4);
	REQUIRE(newer.size() == 1);
	CHECK(newer[0].Message == "message 5");
	CHECK(shared->SnapshotAfter(5).empty());
	CHECK(shared->SnapshotAfter(0).size() == 3);
}

TEST_CASE("Log: ring buffer clear keeps sequence numbers increasing")
{
	Logger logger("RING");
	const Ref<RingBufferSink> ring = CreateRef<RingBufferSink>(4);
	logger.AddSink(ring);

	logger.Info("a");
	logger.Info("b");
	ring->Clear();
	CHECK(ring->GetSize() == 0);
	CHECK(ring->GetTotalWritten() == 2);

	logger.Info("c");
	const std::vector<LogEntry> entries = ring->Snapshot();
	REQUIRE(entries.size() == 1);
	CHECK(entries[0].Sequence == 3);
}

TEST_CASE("Log: ring buffer capacity is at least one")
{
	RingBufferSink ring(0);
	CHECK(ring.GetCapacity() == 1);

	Logger logger("RING");
	const Ref<RingBufferSink> shared = CreateRef<RingBufferSink>(0);
	logger.AddSink(shared);
	logger.Info("one");
	logger.Info("two");
	REQUIRE(shared->GetSize() == 1);
	CHECK(shared->Snapshot()[0].Message == "two");
}

TEST_CASE("Log: concurrent logging from many threads is safe and lossless")
{
	constexpr int ThreadCount = 4;
	constexpr int MessagesPerThread = 500;

	Logger first("FIRST");
	Logger second("SECOND");
	const Ref<RingBufferSink> ring = CreateRef<RingBufferSink>(ThreadCount * MessagesPerThread);
	first.AddSink(ring);
	second.AddSink(ring);

	std::vector<std::thread> threads;
	for (int t = 0; t < ThreadCount; ++t)
	{
		threads.emplace_back([&, t] {
			Logger& logger = (t % 2 == 0) ? first : second;
			for (int i = 0; i < MessagesPerThread; ++i)
			{
				logger.Info("thread {} message {}", t, i);
			}
		});
	}
	for (std::thread& thread : threads)
	{
		thread.join();
	}

	CHECK(ring->GetTotalWritten() == ThreadCount * MessagesPerThread);
	const std::vector<LogEntry> entries = ring->Snapshot();
	REQUIRE(entries.size() == ThreadCount * MessagesPerThread);

	// Sequence numbers are strictly increasing in storage order, and within one thread messages stay ordered.
	std::vector<int> lastMessage(ThreadCount, -1);
	for (size_t i = 0; i < entries.size(); ++i)
	{
		if (i > 0)
		{
			CHECK(entries[i].Sequence == entries[i - 1].Sequence + 1);
		}
		int thread = 0;
		int message = 0;
		REQUIRE(std::sscanf(entries[i].Message.c_str(), "thread %d message %d", &thread, &message) == 2);
		CHECK(message == lastMessage[thread] + 1);
		lastMessage[thread] = message;
	}
}

TEST_CASE("Log: file sink writes plain lines and appends or truncates")
{
	const Tests::ScopedTempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "nested" / "dir" / "engine.log";

	{
		Result<Ref<FileSink>> sink = FileSink::Create(path);
		REQUIRE(sink.HasValue());
		Logger logger("FILE");
		logger.AddSink(sink.Value());
		logger.Info("first line");
		logger.Warn("second {}", "line");
	}

	std::string content = Tests::ReadTextFile(path);
	CHECK(content.find("[FILE] [info] first line\n") != std::string::npos);
	CHECK(content.find("[FILE] [warn] second line\n") != std::string::npos);
	CHECK(content.find('\x1b') == std::string::npos);

	{
		Result<Ref<FileSink>> sink = FileSink::Create(path, FileSink::Mode::Append);
		REQUIRE(sink.HasValue());
		Logger logger("FILE");
		logger.AddSink(sink.Value());
		logger.Info("third line");
	}
	content = Tests::ReadTextFile(path);
	CHECK(content.find("first line") != std::string::npos);
	CHECK(content.find("third line") != std::string::npos);

	{
		Result<Ref<FileSink>> sink = FileSink::Create(path, FileSink::Mode::Truncate);
		REQUIRE(sink.HasValue());
	}
	CHECK(Tests::ReadTextFile(path).empty());
}

TEST_CASE("Log: file sink flushes warnings immediately")
{
	const Tests::ScopedTempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "crash.log";

	Result<Ref<FileSink>> sink = FileSink::Create(path);
	REQUIRE(sink.HasValue());
	Logger logger("FILE");
	logger.AddSink(sink.Value());

	logger.Error("about to crash");
	// The sink is still alive: the record must already be on disk.
	CHECK(Tests::ReadTextFile(path).find("about to crash") != std::string::npos);
}

TEST_CASE("Log: file sink reports why a file cannot be created")
{
	const Tests::ScopedTempDirectory directory;
	const std::filesystem::path blocker = directory.GetPath() / "blocker";
	{
		std::ofstream(blocker) << "not a directory";
	}

	const Result<Ref<FileSink>> underFile = FileSink::Create(blocker / "sub" / "engine.log");
	REQUIRE_FALSE(underFile.HasValue());
	CHECK_FALSE(underFile.GetError().Message.empty());

	const Result<Ref<FileSink>> directoryAsFile = FileSink::Create(directory.GetPath());
	REQUIRE_FALSE(directoryAsFile.HasValue());
	CHECK(directoryAsFile.GetError().Message.find("cannot open") != std::string::npos);
}

TEST_CASE("Log: console sink writes formatted lines and honors the color mode")
{
	std::string output;
	{
		Tests::StderrCapture capture;
		ConsoleSink plain(ConsoleStream::StdErr, ConsoleColor::Never);
		ConsoleSink colored(ConsoleStream::StdErr, ConsoleColor::Always);
		CHECK_FALSE(plain.IsColored());

		Logger logger("CON");
		logger.AddSink(CreateRef<ConsoleSink>(ConsoleStream::StdErr, ConsoleColor::Never));
		logger.Error("plain message");

		LogRecord record;
		record.Level = LogLevel::Critical;
		record.LoggerName = "CON";
		record.Message = "colored message";
		record.Time = std::chrono::system_clock::now();
		colored.Write(record);
		colored.Flush();
		output = capture.Finish();
	}

	CHECK(output.find("[CON] [error] plain message\n") != std::string::npos);
	CHECK(output.find("\x1b[1;97;41m") != std::string::npos);
	CHECK(output.find("colored message\x1b[0m\n") != std::string::npos);
}

TEST_CASE("Log: facade routes core and client macros to named loggers")
{
	LogConfig config;
	config.Console = false;
	config.RingBufferCapacity = 32;
	const Tests::ScopedLogInit init(config);

	REQUIRE(Log::IsInitialized());
	const Ref<RingBufferSink> ring = Log::GetRingBuffer();
	REQUIRE(ring != nullptr);

	GB_CORE_TRACE("core trace {}", 1);
	GB_CORE_INFO("core info");
	GB_CORE_WARN("core warn");
	GB_CORE_ERROR("core error");
	GB_CORE_CRITICAL("core critical");
	GB_TRACE("client trace {}", 2);
	GB_INFO("client info");
	GB_WARN("client warn");
	GB_ERROR("client error");
	GB_CRITICAL("client critical");

	const std::vector<LogEntry> entries = ring->Snapshot();
	REQUIRE(entries.size() == 10);
	CHECK(entries[0].LoggerName == "GAMBIT");
	CHECK(entries[0].Message == "core trace 1");
	CHECK(entries[0].Level == LogLevel::Trace);
	CHECK(entries[4].Level == LogLevel::Critical);
	CHECK(entries[5].LoggerName == "APP");
	CHECK(entries[5].Message == "client trace 2");
	CHECK(entries[9].Message == "client critical");
	CHECK(entries[9].Level == LogLevel::Critical);
}

TEST_CASE("Log: macros do not evaluate their arguments when the level is disabled")
{
	LogConfig config;
	config.Console = false;
	config.Level = LogLevel::Warn;
	config.RingBufferCapacity = 8;
	const Tests::ScopedLogInit init(config);

	int evaluations = 0;
	const auto expensive = [&evaluations] {
		++evaluations;
		return 7;
	};

	GB_CORE_INFO("skipped {}", expensive());
	GB_INFO("skipped {}", expensive());
	CHECK(evaluations == 0);

	GB_CORE_WARN("kept {}", expensive());
	CHECK(evaluations == 1);
	CHECK(Log::GetRingBuffer()->GetSize() == 1);
}

TEST_CASE("Log: macros are single statements")
{
	LogConfig config;
	config.Console = false;
	config.RingBufferCapacity = 8;
	const Tests::ScopedLogInit init(config);

	// Must compile and bind the else to the outer if. The condition is false but not a constant (MSVC C4127).
	const bool flag = Log::GetRingBuffer() == nullptr;
	if (flag)
		GB_CORE_INFO("never");
	else
		GB_CORE_INFO("else branch");

	const std::vector<LogEntry> entries = Log::GetRingBuffer()->Snapshot();
	REQUIRE(entries.size() == 1);
	CHECK(entries[0].Message == "else branch");
}

TEST_CASE("Log: init writes the log file and shutdown releases it")
{
	const Tests::ScopedTempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "logs" / "gambit.log";

	LogConfig config;
	config.Console = false;
	config.FilePath = path;
	config.RingBufferCapacity = 0;
	{
		const Tests::ScopedLogInit init(config);
		CHECK(Log::GetRingBuffer() == nullptr);
		GB_CORE_INFO("to the file {}", 1);
		GB_INFO("from the client");
		Log::Shutdown();
		CHECK_FALSE(Log::IsInitialized());

		// After shutdown logging is a harmless no-op.
		GB_CORE_INFO("not written");
	}

	const std::string content = Tests::ReadTextFile(path);
	CHECK(content.find("[GAMBIT] [info] to the file 1\n") != std::string::npos);
	CHECK(content.find("[APP] [info] from the client\n") != std::string::npos);
	CHECK(content.find("not written") == std::string::npos);
}

TEST_CASE("Log: init survives an unusable log file and says so")
{
	const Tests::ScopedTempDirectory directory;
	const std::filesystem::path blocker = directory.GetPath() / "blocker";
	{
		std::ofstream(blocker) << "x";
	}

	LogConfig config;
	config.Console = false;
	config.FilePath = blocker / "gambit.log";
	config.RingBufferCapacity = 8;
	const Tests::ScopedLogInit init(config);

	REQUIRE(Log::IsInitialized());
	const Ref<RingBufferSink> ring = Log::GetRingBuffer();
	REQUIRE(ring != nullptr);

	const std::vector<LogEntry> entries = ring->Snapshot();
	REQUIRE(entries.size() == 1);
	CHECK(entries[0].Level == LogLevel::Warn);
	CHECK(entries[0].Message.starts_with("File logging is disabled: "));

	GB_CORE_INFO("still working");
	CHECK(ring->GetSize() == 2);
}

TEST_CASE("Log: init can be repeated and replaces the configuration")
{
	LogConfig first;
	first.Console = false;
	first.RingBufferCapacity = 4;
	const Tests::ScopedLogInit init(first);
	const Ref<RingBufferSink> firstRing = Log::GetRingBuffer();
	GB_CORE_INFO("one");

	LogConfig second = first;
	second.RingBufferCapacity = 8;
	Log::Init(second);
	const Ref<RingBufferSink> secondRing = Log::GetRingBuffer();
	GB_CORE_INFO("two");

	CHECK(firstRing != secondRing);
	CHECK(firstRing->GetSize() == 1);
	REQUIRE(secondRing->GetSize() == 1);
	CHECK(secondRing->GetCapacity() == 8);
	CHECK(secondRing->Snapshot()[0].Message == "two");
}

TEST_CASE("Log: custom sinks can be attached to both loggers")
{
	struct CountingSink final : LogSink
	{
		void Write(const LogRecord&) override { ++Count; }
		std::atomic<int> Count{ 0 };
	};

	LogConfig config;
	config.Console = false;
	config.RingBufferCapacity = 0;
	const Tests::ScopedLogInit init(config);

	const Ref<CountingSink> sink = CreateRef<CountingSink>();
	Log::AddSink(sink);
	GB_CORE_INFO("core");
	GB_INFO("client");
	CHECK(sink->Count == 2);

	Log::RemoveSink(sink);
	GB_CORE_INFO("core");
	CHECK(sink->Count == 2);
}
