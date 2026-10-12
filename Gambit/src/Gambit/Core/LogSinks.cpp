#include "gbpch.h"

#include "Gambit/Core/LogSinks.h"

#include <algorithm>
#include <cstdlib>
#include <system_error>

#if defined(GB_PLATFORM_WINDOWS)
#	include <io.h>
#	include <windows.h>
#else
#	include <unistd.h>
#endif

namespace Gambit
{
	namespace
	{
		bool IsTerminal(std::FILE* file)
		{
#if defined(GB_PLATFORM_WINDOWS)
			return _isatty(_fileno(file)) != 0;
#else
			return isatty(fileno(file)) != 0;
#endif
		}

		// Windows consoles only interpret ANSI escape sequences once virtual terminal processing is enabled.
		bool EnableAnsiEscapes([[maybe_unused]] std::FILE* file)
		{
#if defined(GB_PLATFORM_WINDOWS)
			const HANDLE handle = GetStdHandle(file == stdout ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE);
			DWORD mode = 0;
			return handle != INVALID_HANDLE_VALUE && GetConsoleMode(handle, &mode) != 0
				&& SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
#else
			return true;
#endif
		}

		bool ShouldUseColor(std::FILE* file, ConsoleColor mode)
		{
			switch (mode)
			{
				case ConsoleColor::Never:
					return false;
				case ConsoleColor::Always:
					// Forced: escapes are emitted even when the stream is a pipe or file (CI logs, pagers), where
					// enabling console processing fails on Windows.
					static_cast<void>(EnableAnsiEscapes(file));
					return true;
				case ConsoleColor::Auto:
					break;
			}

			// https://no-color.org: any non-empty value disables color.
			const char* noColor = std::getenv("NO_COLOR");
			if (noColor != nullptr && *noColor != '\0')
			{
				return false;
			}
			const char* term = std::getenv("TERM");
			if (term != nullptr && std::string_view(term) == "dumb")
			{
				return false;
			}
			return IsTerminal(file) && EnableAnsiEscapes(file);
		}
	}

	ConsoleSink::ConsoleSink(ConsoleStream stream, ConsoleColor color)
		: m_File(stream == ConsoleStream::StdOut ? stdout : stderr), m_Colored(ShouldUseColor(m_File, color))
	{}

	void ConsoleSink::Write(const LogRecord& record)
	{
		const std::string line = FormatLogRecord(record, m_Colored);
		std::scoped_lock lock(m_Mutex);
		std::fwrite(line.data(), 1, line.size(), m_File);
		// Console output is low volume and must interleave correctly with other writers of the same stream.
		std::fflush(m_File);
	}

	void ConsoleSink::Flush()
	{
		std::scoped_lock lock(m_Mutex);
		std::fflush(m_File);
	}

	Result<Ref<FileSink>> FileSink::Create(const std::filesystem::path& path, Mode mode)
	{
		std::error_code error;
		if (path.has_parent_path())
		{
			std::filesystem::create_directories(path.parent_path(), error);
			if (error)
			{
				return Error::FormatWithCode(
					error.value(), "cannot create directory '{}': {}", path.parent_path().string(), error.message());
			}
		}

		const auto openMode =
			std::ios::out | std::ios::binary | (mode == Mode::Append ? std::ios::app : std::ios::trunc);
		std::ofstream stream(path, openMode);
		if (!stream.is_open())
		{
			return Error::Format("cannot open log file '{}' for writing", path.string());
		}
		return CreateRef<FileSink>(PrivateTag{}, std::move(stream));
	}

	FileSink::FileSink(PrivateTag, std::ofstream stream)
		: m_Stream(std::move(stream))
	{}

	void FileSink::Write(const LogRecord& record)
	{
		const std::string line = FormatLogRecord(record, false);
		std::scoped_lock lock(m_Mutex);
		m_Stream.write(line.data(), static_cast<std::streamsize>(line.size()));
		if (record.Level >= LogLevel::Warn)
		{
			m_Stream.flush();
		}
	}

	void FileSink::Flush()
	{
		std::scoped_lock lock(m_Mutex);
		m_Stream.flush();
	}

	RingBufferSink::RingBufferSink(size_t capacity)
		: m_Entries(std::max<size_t>(capacity, 1))
	{}

	void RingBufferSink::Write(const LogRecord& record)
	{
		std::scoped_lock lock(m_Mutex);

		const size_t capacity = m_Entries.size();
		size_t slot = 0;
		if (m_Size < capacity)
		{
			slot = (m_Oldest + m_Size) % capacity;
			++m_Size;
		}
		else
		{
			slot = m_Oldest;
			m_Oldest = (m_Oldest + 1) % capacity;
		}

		LogEntry& entry = m_Entries[slot];
		entry.Sequence = m_NextSequence++;
		entry.Level = record.Level;
		entry.LoggerName.assign(record.LoggerName);
		entry.Message.assign(record.Message);
		entry.Time = record.Time;
	}

	size_t RingBufferSink::GetSize() const
	{
		std::scoped_lock lock(m_Mutex);
		return m_Size;
	}

	uint64_t RingBufferSink::GetTotalWritten() const
	{
		std::scoped_lock lock(m_Mutex);
		return m_NextSequence - 1;
	}

	std::vector<LogEntry> RingBufferSink::Snapshot() const
	{
		return SnapshotAfter(0);
	}

	std::vector<LogEntry> RingBufferSink::SnapshotAfter(uint64_t sequence) const
	{
		std::scoped_lock lock(m_Mutex);

		std::vector<LogEntry> result;
		result.reserve(m_Size);
		for (size_t i = 0; i < m_Size; ++i)
		{
			const LogEntry& entry = m_Entries[(m_Oldest + i) % m_Entries.size()];
			if (entry.Sequence > sequence)
			{
				result.push_back(entry);
			}
		}
		return result;
	}

	void RingBufferSink::Clear()
	{
		std::scoped_lock lock(m_Mutex);
		m_Oldest = 0;
		m_Size = 0;
	}
}
