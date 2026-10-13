#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace Gambit
{
	// Keeps an error that repeats every frame (acquiring an image from a lost window surface, say) from flooding the
	// core log: the first failure of a burst of identical failures is logged as an error and the repetitions are only
	// counted; a different failure is logged again, and the next success ends the burst with a warning that says how
	// many failures were not logged. Not thread safe: one limiter per operation of one object.
	class ErrorBurstLimiter
	{
	public:
		// `operation` starts every message ("Acquiring a swap chain image").
		explicit ErrorBurstLimiter(std::string operation);

		// Logs "<operation> failed: <reason>" unless the current burst already logged this reason.
		void ReportFailure(std::string_view reason);
		// Ends a burst of failures, if there is one.
		void ReportSuccess();

		[[nodiscard]] bool IsInBurst() const noexcept { return m_FailureCount != 0; }
		// Failures of the current burst that were counted but not logged.
		[[nodiscard]] uint64_t GetSuppressedCount() const noexcept { return m_SuppressedCount; }

	private:
		std::string m_Operation;
		std::string m_LastReason;
		uint64_t m_FailureCount = 0;
		uint64_t m_SuppressedCount = 0;
	};
}
