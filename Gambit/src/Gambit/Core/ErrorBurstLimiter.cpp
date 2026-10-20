#include "gbpch.h"

#include "Gambit/Core/ErrorBurstLimiter.h"

namespace Gambit
{
	ErrorBurstLimiter::ErrorBurstLimiter(std::string operation)
		: m_Operation(std::move(operation))
	{}

	void ErrorBurstLimiter::ReportFailure(std::string_view reason)
	{
		++m_FailureCount;
		if (m_FailureCount > 1 && reason == m_LastReason)
		{
			++m_SuppressedCount;
			return;
		}
		m_LastReason = std::string(reason);
		GB_CORE_ERROR(
			"{} failed: {}{}", m_Operation, reason,
			m_FailureCount == 1 ? " (repetitions are not logged until it succeeds again)" : "");
	}

	void ErrorBurstLimiter::ReportSuccess()
	{
		if (m_FailureCount == 0)
		{
			return;
		}
		GB_CORE_WARN(
			"{} succeeded again after {} failure(s), {} of them not logged", m_Operation, m_FailureCount,
			m_SuppressedCount);
		m_FailureCount = 0;
		m_SuppressedCount = 0;
		m_LastReason.clear();
	}
}
