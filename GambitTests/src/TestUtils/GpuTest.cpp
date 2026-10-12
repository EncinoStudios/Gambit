#include "TestUtils/GpuTest.h"

#include "TestSupport.h"

#include "Gambit/Core/Log.h"

#include <doctest/doctest.h>

#include <atomic>

namespace Gambit::Tests
{
	namespace
	{
		constexpr glm::uvec2 SharedBackBufferSize{ 256, 256 };

		// Counts the error and critical records it receives.
		class ErrorCountingSink final : public LogSink
		{
		public:
			void Write(const LogRecord& record) override
			{
				if (record.Level >= LogLevel::Error)
				{
					++m_Count;
				}
			}

			[[nodiscard]] uint64_t GetCount() const noexcept { return m_Count.load(); }

		private:
			std::atomic<uint64_t> m_Count{ 0 };
		};

		struct SharedDevice
		{
			bool Attempted = false;
			Scope<GraphicsDevice> Device;
			std::string SkipReason;
			std::string FailureReason; // creation failed for a reason other than "no usable GPU here"
		};

		SharedDevice& GetShared()
		{
			static SharedDevice shared;
			return shared;
		}

		void CreateSharedDevice(SharedDevice& shared)
		{
			shared.Attempted = true;
			if (AreGpuTestsDisabled())
			{
				shared.SkipReason = "GPU tests are disabled (GB_TEST_NO_GPU=1)";
				return;
			}
			GraphicsDeviceSpecification specification;
			specification.ApplicationName = "GambitTests";
			specification.EnableValidation = true;
			specification.Headless = true;
			specification.BackBufferWidth = SharedBackBufferSize.x;
			specification.BackBufferHeight = SharedBackBufferSize.y;
			Result<Scope<GraphicsDevice>> device = GraphicsDevice::Create(specification);
			if (device)
			{
				shared.Device = std::move(*device);
				return;
			}
			const Error& error = device.GetError();
			if (HasGraphicsDeviceError(error, GraphicsDeviceError::VulkanUnavailable)
			    || HasGraphicsDeviceError(error, GraphicsDeviceError::NoSuitableDevice))
			{
				shared.SkipReason = error.Message;
			}
			else
			{
				shared.FailureReason = error.Message;
			}
		}
	}

	bool AreGpuTestsDisabled()
	{
		return IsEnvironmentFlagSet("GB_TEST_NO_GPU");
	}

	GraphicsDevice* GetSharedGraphicsDevice()
	{
		SharedDevice& shared = GetShared();
		if (!shared.Attempted)
		{
			CreateSharedDevice(shared);
		}
		if (!shared.FailureReason.empty())
		{
			FAIL("cannot create the GPU test device: ", shared.FailureReason);
		}
		return shared.Device.get();
	}

	const std::string& GetGpuSkipReason()
	{
		return GetShared().SkipReason;
	}

	uint64_t ShutdownSharedGraphicsDevice()
	{
		SharedDevice& shared = GetShared();
		if (shared.Device == nullptr)
		{
			return 0;
		}
		// The device's own counter dies with it, so the errors of the teardown (objects a test leaked are reported when
		// the Vulkan device is destroyed) are counted from the log instead.
		const Ref<ErrorCountingSink> sink = CreateRef<ErrorCountingSink>();
		Log::GetCoreLogger().AddSink(sink);
		shared.Device.reset();
		Log::GetCoreLogger().RemoveSink(sink);
		return sink->GetCount();
	}

	GpuTestScope::GpuTestScope()
		: m_Device(GetSharedGraphicsDevice())
	{
		if (m_Device != nullptr)
		{
			m_ErrorsAtStart = m_Device->GetValidationErrorCount();
		}
	}

	GpuTestScope::~GpuTestScope()
	{
		if (m_Device == nullptr)
		{
			return;
		}
		// A test that stopped early (a failed REQUIRE) must not leave the shared device mid-frame or resized.
		if (m_Device->IsInFrame())
		{
			FAIL_CHECK("the test left the shared graphics device between BeginFrame and Present");
			static_cast<void>(m_Device->Present());
		}
		if (m_Device->GetBackBufferSize() != SharedBackBufferSize)
		{
			FAIL_CHECK("the test left the shared graphics device's back buffers resized");
			static_cast<void>(m_Device->ResizeBackBuffers(SharedBackBufferSize.x, SharedBackBufferSize.y));
		}
		// Work the test submitted may still report errors when it executes; let it finish first.
		m_Device->WaitIdle();
		const uint64_t errors = m_Device->GetValidationErrorCount() - m_ErrorsAtStart;
		CHECK_MESSAGE(errors == 0, errors, " Vulkan/NVRHI validation error(s) during this test (see the log)");
	}
}
