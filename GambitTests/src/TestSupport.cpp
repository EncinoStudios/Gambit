#include "TestSupport.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <fstream>
#include <iterator>
#include <string_view>
#include <system_error>

#if defined(_WIN32)
#	include <io.h>
#	define GB_TEST_DUP _dup
#	define GB_TEST_DUP2 _dup2
#	define GB_TEST_CLOSE _close
#	define GB_TEST_FILENO _fileno
#else
#	include <unistd.h>
#	define GB_TEST_DUP dup
#	define GB_TEST_DUP2 dup2
#	define GB_TEST_CLOSE close
#	define GB_TEST_FILENO fileno
#endif

namespace Gambit::Tests
{
	bool IsEnvironmentFlagSet(const char* name)
	{
		const char* value = std::getenv(name);
		return value != nullptr && *value != '\0' && std::string_view(value) != "0";
	}

	LogConfig MakeTestLogConfig()
	{
		LogConfig config;
		config.ConsoleTarget = ConsoleStream::StdErr;
		config.ConsoleLevel = LogLevel::Warn;
		config.RingBufferCapacity = 0;
		return config;
	}

	namespace
	{
		LogConfig MakeCaptureConfig()
		{
			LogConfig config;
			config.Console = false;
			config.RingBufferCapacity = 256;
			return config;
		}
	}

	ScopedLogCapture::ScopedLogCapture()
		: m_Init(MakeCaptureConfig())
	{}

	std::vector<LogEntry> ScopedLogCapture::GetEntries() const
	{
		const Ref<RingBufferSink> buffer = Log::GetRingBuffer();
		return buffer != nullptr ? buffer->Snapshot() : std::vector<LogEntry>{};
	}

	bool ScopedLogCapture::Contains(LogLevel level, std::string_view text) const
	{
		for (const LogEntry& entry : GetEntries())
		{
			if (entry.Level == level && entry.Message.find(text) != std::string::npos)
			{
				return true;
			}
		}
		return false;
	}

	ScopedLogInit::ScopedLogInit(const LogConfig& config)
	{
		Log::Init(config);
	}

	ScopedLogInit::~ScopedLogInit()
	{
		Log::Init(MakeTestLogConfig());
	}

	ScopedTempDirectory::ScopedTempDirectory()
	{
		static std::atomic<unsigned> s_Counter{ 0 };
		const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
		m_Path = std::filesystem::temp_directory_path() / std::format("gambit-tests-{}-{}", stamp, s_Counter++);
		std::filesystem::create_directories(m_Path);
	}

	ScopedTempDirectory::~ScopedTempDirectory()
	{
		std::error_code ignored;
		std::filesystem::remove_all(m_Path, ignored);
	}

	StderrCapture::StderrCapture()
		: m_File(m_Directory.GetPath() / "stderr.txt")
	{
		std::fflush(stderr);
		std::FILE* file = std::fopen(m_File.string().c_str(), "wb");
		if (file == nullptr)
		{
			return;
		}
		m_SavedDescriptor = GB_TEST_DUP(GB_TEST_FILENO(stderr));
		GB_TEST_DUP2(GB_TEST_FILENO(file), GB_TEST_FILENO(stderr));
		std::fclose(file);
	}

	StderrCapture::~StderrCapture()
	{
		Restore();
	}

	void StderrCapture::Restore()
	{
		if (m_SavedDescriptor < 0)
		{
			return;
		}
		std::fflush(stderr);
		GB_TEST_DUP2(m_SavedDescriptor, GB_TEST_FILENO(stderr));
		GB_TEST_CLOSE(m_SavedDescriptor);
		m_SavedDescriptor = -1;
	}

	std::string StderrCapture::Finish()
	{
		Restore();
		return ReadTextFile(m_File);
	}

	std::string ReadTextFile(const std::filesystem::path& path)
	{
		std::ifstream stream(path, std::ios::binary);
		return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
	}
}
