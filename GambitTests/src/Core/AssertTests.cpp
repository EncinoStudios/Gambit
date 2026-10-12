#include <doctest/doctest.h>

#include "TestSupport.h"

#include "Gambit/Core/Assert.h"
#include "Gambit/Core/LogSinks.h"

#include <string>
#include <vector>

namespace
{
	using namespace Gambit;

	struct RecordedFailure
	{
		AssertKind Kind;
		bool Core;
		std::string Expression;
		std::string Message;
		std::string File;
		uint32_t Line;
	};

	std::vector<RecordedFailure> s_Failures;

	AssertAction RecordingHandler(const AssertFailure& failure)
	{
		s_Failures.push_back({ failure.Kind, failure.Core, std::string(failure.Expression),
		                       std::string(failure.Message), std::string(failure.File), failure.Line });
		return AssertAction::Continue;
	}

	// Routes assertion failures into s_Failures for the lifetime of the object.
	class ScopedRecordingHandler
	{
	public:
		ScopedRecordingHandler()
			: m_Previous(SetAssertHandler(&RecordingHandler))
		{
			s_Failures.clear();
		}
		~ScopedRecordingHandler() { SetAssertHandler(m_Previous); }
		ScopedRecordingHandler(const ScopedRecordingHandler&) = delete;
		ScopedRecordingHandler& operator=(const ScopedRecordingHandler&) = delete;

	private:
		AssertHandler m_Previous;
	};
}

TEST_CASE("Assert: passing checks do not report and evaluate verify conditions once")
{
	const ScopedRecordingHandler handler;

	int evaluations = 0;
	const auto condition = [&evaluations] {
		++evaluations;
		return true;
	};

	GB_CORE_VERIFY(condition());
	GB_VERIFY(condition(), "with a message {}", 1);
	CHECK(evaluations == 2);

	GB_CORE_ASSERT(1 + 1 == 2);
	GB_ASSERT(true, "never printed {}", 3);
	CHECK(s_Failures.empty());
}

TEST_CASE("Assert: verify reports expression, message and location in every configuration")
{
	const ScopedRecordingHandler handler;

	const uint32_t line = __LINE__ + 1;
	GB_CORE_VERIFY(1 + 1 == 3, "math is {} today", "broken");
	GB_VERIFY(false);

	REQUIRE(s_Failures.size() == 2);
	CHECK(s_Failures[0].Kind == AssertKind::Verify);
	CHECK(s_Failures[0].Core);
	CHECK(s_Failures[0].Expression == "1 + 1 == 3");
	CHECK(s_Failures[0].Message == "math is broken today");
	CHECK(s_Failures[0].File.ends_with("AssertTests.cpp"));
	CHECK(s_Failures[0].Line == line);

	CHECK(s_Failures[1].Kind == AssertKind::Verify);
	CHECK_FALSE(s_Failures[1].Core);
	CHECK(s_Failures[1].Expression == "false");
	CHECK(s_Failures[1].Message.empty());
}

#if defined(GB_DIST)

TEST_CASE("Assert: asserts are compiled out in Dist and do not evaluate their condition")
{
	const ScopedRecordingHandler handler;

	int evaluations = 0;
	const auto condition = [&evaluations] {
		++evaluations;
		return false;
	};

	GB_CORE_ASSERT(condition(), "unused {}", evaluations);
	GB_ASSERT(condition());
	CHECK(evaluations == 0);
	CHECK(s_Failures.empty());
}

#else

TEST_CASE("Assert: failing asserts report through the handler")
{
	const ScopedRecordingHandler handler;

	GB_CORE_ASSERT(2 < 1, "two is not less than {}", 1);
	GB_ASSERT(nullptr != nullptr);

	REQUIRE(s_Failures.size() == 2);
	CHECK(s_Failures[0].Kind == AssertKind::Assert);
	CHECK(s_Failures[0].Core);
	CHECK(s_Failures[0].Expression == "2 < 1");
	CHECK(s_Failures[0].Message == "two is not less than 1");
	CHECK_FALSE(s_Failures[1].Core);
	CHECK(s_Failures[1].Expression == "nullptr != nullptr");
}

#endif

TEST_CASE("Assert: SetAssertHandler returns the previous handler and nullptr restores the default")
{
	const AssertHandler original = SetAssertHandler(&RecordingHandler);
	CHECK(SetAssertHandler(nullptr) == &RecordingHandler);

	// nullptr installed the default handler, so removing it again returns the default, not nullptr.
	const AssertHandler defaultHandler = SetAssertHandler(&RecordingHandler);
	CHECK(defaultHandler != nullptr);
	CHECK(defaultHandler == original);
	SetAssertHandler(original);
}

TEST_CASE("Assert: the default handler logs at critical level and asks to break")
{
	LogConfig config;
	config.Console = false;
	config.RingBufferCapacity = 8;
	const Tests::ScopedLogInit init(config);

	const AssertAction withMessage =
		ReportAssertFailure(AssertKind::Assert, true, "value > 0", "Some/File.cpp", 12, "value was -1");
	const AssertAction withoutMessage =
		ReportAssertFailure(AssertKind::Verify, false, "ptr != nullptr", "Other.cpp", 99, "");
	CHECK(withMessage == AssertAction::Break);
	CHECK(withoutMessage == AssertAction::Break);

	const std::vector<LogEntry> entries = Log::GetRingBuffer()->Snapshot();
	REQUIRE(entries.size() == 2);
	CHECK(entries[0].Level == LogLevel::Critical);
	CHECK(entries[0].LoggerName == "GAMBIT");
	CHECK(entries[0].Message == "Assertion failed: value > 0 (Some/File.cpp:12): value was -1");
	CHECK(entries[1].LoggerName == "APP");
	CHECK(entries[1].Message == "Verification failed: ptr != nullptr (Other.cpp:99)");
}

TEST_CASE("Assert: the default handler still prints to stderr when no log sink is attached")
{
	LogConfig config;
	config.Console = false;
	config.RingBufferCapacity = 0;
	const Tests::ScopedLogInit init(config);

	std::string output;
	{
		Tests::StderrCapture capture;
		CHECK(ReportAssertFailure(AssertKind::Assert, true, "x", "F.cpp", 1, "boom") == AssertAction::Break);
		output = capture.Finish();
	}
	CHECK(output == "Assertion failed: x (F.cpp:1): boom\n");
}
