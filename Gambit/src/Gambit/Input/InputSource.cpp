#include "gbpch.h"

#include "Gambit/Input/InputSource.h"

#include "Gambit/Input/InputDevices.h"

namespace Gambit
{
	InputFrame DeviceInputSource::Sample()
	{
		return m_Devices.Sample();
	}

	InputFrame ReplayInputSource::Sample()
	{
		if (IsFinished())
		{
			return {};
		}
		return m_Log->GetFrame(m_Position++);
	}
}
