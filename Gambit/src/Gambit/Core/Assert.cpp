#include "gbpch.h"

#include "Gambit/Core/Assert.h"

#include <cstdio>

namespace Gambit
{
	namespace
	{
		AssertAction DefaultAssertHandler(const AssertFailure& failure)
		{
			const std::string_view kind = failure.Kind == AssertKind::Assert ? "Assertion" : "Verification";
			const std::string text = failure.Message.empty()
				? std::format("{} failed: {} ({}:{})", kind, failure.Expression, failure.File, failure.Line)
				: std::format(
					  "{} failed: {} ({}:{}): {}", kind, failure.Expression, failure.File, failure.Line,
					  failure.Message);

			Logger& logger = failure.Core ? Log::GetCoreLogger() : Log::GetClientLogger();
			if (logger.HasSinks() && logger.ShouldLog(LogLevel::Critical))
			{
				logger.Submit(LogLevel::Critical, text);
				Log::Flush();
			}
			else
			{
				// Without a sink the failure would otherwise vanish and the process just trap silently.
				std::fprintf(stderr, "%s\n", text.c_str());
				std::fflush(stderr);
			}
			return AssertAction::Break;
		}

		std::atomic<AssertHandler> s_Handler{ &DefaultAssertHandler };
	}

	AssertHandler SetAssertHandler(AssertHandler handler) noexcept
	{
		return s_Handler.exchange(handler != nullptr ? handler : &DefaultAssertHandler);
	}

	AssertAction ReportAssertFailure(
		AssertKind kind, bool core, std::string_view expression, std::string_view file, uint32_t line,
		std::string_view message)
	{
		const AssertFailure failure{ kind, core, expression, message, file, line };
		return s_Handler.load()(failure);
	}
}
