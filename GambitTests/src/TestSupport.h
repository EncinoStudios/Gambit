#pragma once

#include "Gambit/Core/Log.h"
#include "Gambit/Core/LogSinks.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Gambit::Tests
{
	// Whether the environment variable `name` is set to something other than "" or "0" (GB_TEST_NO_GPU,
	// GB_UPDATE_GOLDEN).
	[[nodiscard]] bool IsEnvironmentFlagSet(const char* name);

	// Log setup of the test executable: warnings and above on stderr, no ring buffer.
	[[nodiscard]] LogConfig MakeTestLogConfig();

	// Re-initializes the global log with `config` and restores the test configuration on destruction, so a test
	// can use the Log facade without leaking state into the next one.
	class ScopedLogInit
	{
	public:
		explicit ScopedLogInit(const LogConfig& config);
		~ScopedLogInit();
		ScopedLogInit(const ScopedLogInit&) = delete;
		ScopedLogInit& operator=(const ScopedLogInit&) = delete;
	};

	// Routes the log into memory only (nothing reaches the console) until destruction, for tests that provoke and
	// check log messages.
	class ScopedLogCapture
	{
	public:
		ScopedLogCapture();

		[[nodiscard]] std::vector<LogEntry> GetEntries() const;
		// Whether a record of exactly `level` contains `text`.
		[[nodiscard]] bool Contains(LogLevel level, std::string_view text) const;

	private:
		ScopedLogInit m_Init;
	};

	// A fresh empty directory under the system temp directory, removed (with its contents) on destruction.
	class ScopedTempDirectory
	{
	public:
		ScopedTempDirectory();
		~ScopedTempDirectory();
		ScopedTempDirectory(const ScopedTempDirectory&) = delete;
		ScopedTempDirectory& operator=(const ScopedTempDirectory&) = delete;

		[[nodiscard]] const std::filesystem::path& GetPath() const noexcept { return m_Path; }

	private:
		std::filesystem::path m_Path;
	};

	// Redirects the process-level stderr into a file until Finish() (or destruction).
	class StderrCapture
	{
	public:
		StderrCapture();
		~StderrCapture();
		StderrCapture(const StderrCapture&) = delete;
		StderrCapture& operator=(const StderrCapture&) = delete;

		// Restores stderr and returns everything written since construction.
		[[nodiscard]] std::string Finish();

	private:
		void Restore();

		ScopedTempDirectory m_Directory;
		std::filesystem::path m_File;
		int m_SavedDescriptor = -1;
	};

	[[nodiscard]] std::string ReadTextFile(const std::filesystem::path& path);
}
