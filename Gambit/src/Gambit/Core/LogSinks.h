#pragma once

#include "Gambit/Core/Base.h"
#include "Gambit/Core/Log.h"
#include "Gambit/Core/Result.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace Gambit
{
	enum class ConsoleColor : uint8_t
	{
		Auto,   // only when the stream is a terminal and NO_COLOR is not set
		Always, // also when the stream is redirected
		Never
	};

	// Writes one line per record to stdout or stderr, colored by level when the terminal supports it.
	class ConsoleSink final : public LogSink
	{
	public:
		explicit ConsoleSink(ConsoleStream stream = ConsoleStream::StdOut, ConsoleColor color = ConsoleColor::Auto);

		void Write(const LogRecord& record) override;
		void Flush() override;

		[[nodiscard]] bool IsColored() const noexcept { return m_Colored; }

	private:
		std::FILE* m_File;
		bool m_Colored;
		std::mutex m_Mutex;
	};

	// Appends plain-text lines to a file. Warnings and above are flushed immediately so that the cause of a crash
	// is on disk; call Flush (or let the sink die) to force the rest.
	class FileSink final : public LogSink
	{
		struct PrivateTag
		{
			explicit PrivateTag() = default;
		};

	public:
		enum class Mode : uint8_t
		{
			Truncate,
			Append
		};

		// Creates missing parent directories. Fails (with the reason) when the file cannot be opened.
		[[nodiscard]] static Result<Ref<FileSink>>
		Create(const std::filesystem::path& path, Mode mode = Mode::Truncate);

		FileSink(PrivateTag, std::ofstream stream);

		void Write(const LogRecord& record) override;
		void Flush() override;

	private:
		std::ofstream m_Stream;
		std::mutex m_Mutex;
	};

	// A stored log record (owning copy of a LogRecord).
	struct LogEntry
	{
		uint64_t Sequence = 0; // 1-based, strictly increasing in arrival order within one RingBufferSink
		LogLevel Level = LogLevel::Info;
		std::string LoggerName;
		std::string Message;
		std::chrono::system_clock::time_point Time;
	};

	// Keeps the most recent `capacity` records in memory for the editor console and the automation API. Writers
	// and readers may run on any thread; Snapshot returns a consistent copy.
	class RingBufferSink final : public LogSink
	{
	public:
		// A capacity of 0 is raised to 1.
		explicit RingBufferSink(size_t capacity);

		void Write(const LogRecord& record) override;

		[[nodiscard]] size_t GetCapacity() const noexcept { return m_Entries.size(); }
		// Number of records currently stored (at most the capacity).
		[[nodiscard]] size_t GetSize() const;
		// Number of records ever written, including those that were overwritten or cleared.
		[[nodiscard]] uint64_t GetTotalWritten() const;

		// Stored records, oldest first.
		[[nodiscard]] std::vector<LogEntry> Snapshot() const;
		// Stored records newer than `sequence`, oldest first. Poll with the last Sequence seen to read incrementally.
		[[nodiscard]] std::vector<LogEntry> SnapshotAfter(uint64_t sequence) const;
		void Clear();

	private:
		mutable std::mutex m_Mutex;
		std::vector<LogEntry> m_Entries; // fixed size == capacity, used as a circular buffer
		size_t m_Oldest = 0;
		size_t m_Size = 0;
		uint64_t m_NextSequence = 1;
	};
}
