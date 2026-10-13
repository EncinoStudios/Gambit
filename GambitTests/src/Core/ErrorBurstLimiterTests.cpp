#include <doctest/doctest.h>

#include "TestSupport.h"

#include "Gambit/Core/ErrorBurstLimiter.h"

#include <algorithm>

using namespace Gambit;

namespace
{
	size_t CountEntries(const Tests::ScopedLogCapture& log, LogLevel level)
	{
		const std::vector<LogEntry> entries = log.GetEntries();
		return static_cast<size_t>(
			std::ranges::count_if(entries, [level](const LogEntry& entry) { return entry.Level == level; }));
	}
}

TEST_CASE("Core: ErrorBurstLimiter logs the first of identical failures and summarizes the burst")
{
	const Tests::ScopedLogCapture log;
	ErrorBurstLimiter limiter("Acquiring an image");
	CHECK_FALSE(limiter.IsInBurst());
	limiter.ReportSuccess(); // nothing to end
	CHECK(CountEntries(log, LogLevel::Warn) == 0);

	for (int i = 0; i < 5; ++i)
	{
		limiter.ReportFailure("ErrorSurfaceLostKHR");
	}
	CHECK(limiter.IsInBurst());
	CHECK(limiter.GetSuppressedCount() == 4);
	CHECK(CountEntries(log, LogLevel::Error) == 1);
	CHECK(log.Contains(LogLevel::Error, "Acquiring an image failed: ErrorSurfaceLostKHR"));

	// Another reason is news and is logged; repeating it is not.
	limiter.ReportFailure("ErrorOutOfHostMemory");
	limiter.ReportFailure("ErrorOutOfHostMemory");
	CHECK(CountEntries(log, LogLevel::Error) == 2);
	CHECK(limiter.GetSuppressedCount() == 5);

	limiter.ReportSuccess();
	CHECK_FALSE(limiter.IsInBurst());
	CHECK(limiter.GetSuppressedCount() == 0);
	CHECK(log.Contains(LogLevel::Warn, "succeeded again after 7 failure(s), 5 of them not logged"));

	// A new burst logs its first failure again.
	limiter.ReportFailure("ErrorSurfaceLostKHR");
	CHECK(CountEntries(log, LogLevel::Error) == 3);
}
