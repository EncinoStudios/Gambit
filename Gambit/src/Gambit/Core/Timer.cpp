#include "gbpch.h"

#include "Gambit/Core/Timer.h"

namespace Gambit
{
	ScopedTimer::~ScopedTimer()
	{
		Log::GetCoreLogger().Write(m_Level, "{}: {:.3f} ms", m_Name, m_Timer.ElapsedMilliseconds());
	}
}
