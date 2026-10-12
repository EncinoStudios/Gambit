#pragma once

#include "Gambit/Core/Buffer.h"
#include "Gambit/Core/Result.h"
#include "Gambit/Input/InputFrame.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Gambit
{
	// What a recording needs to be replayed: the project and scene it ran on, the random seed and the tick rate.
	struct InputLogHeader
	{
		std::string ProjectName;
		std::string ScenePath;
		uint64_t Seed = 0;
		uint32_t SimulationHz = 60;

		friend bool operator==(const InputLogHeader&, const InputLogHeader&) noexcept = default;
	};

	// A recording of the player's input, one InputFrame per simulation tick (D-012). Feeding the frames back instead of
	// the devices reproduces the run exactly. The binary form (.ginput) is little endian, starts with a versioned
	// header and ends with a checksum, so truncated or damaged files are rejected rather than replayed wrongly.
	class InputLog
	{
	public:
		InputLog() = default;
		explicit InputLog(InputLogHeader header)
			: m_Header(std::move(header))
		{}

		[[nodiscard]] const InputLogHeader& GetHeader() const noexcept { return m_Header; }
		void SetHeader(InputLogHeader header) { m_Header = std::move(header); }

		// Appends the input of the next tick.
		void Record(const InputFrame& frame) { m_Frames.push_back(frame); }
		void Clear() noexcept { m_Frames.clear(); }

		[[nodiscard]] size_t GetFrameCount() const noexcept { return m_Frames.size(); }
		[[nodiscard]] const std::vector<InputFrame>& GetFrames() const noexcept { return m_Frames; }
		// The recorded input of tick `index` (0 is the first tick); the index must be in range.
		[[nodiscard]] const InputFrame& GetFrame(size_t index) const;

		// Whether the header can be stored: strings of at most MaxInputLogStringLength bytes, a rate of 1 to 1000 Hz.
		[[nodiscard]] Result<void> Validate() const;

		// The binary form; the header must be valid (Save checks it and reports a problem instead).
		[[nodiscard]] Buffer Serialize() const;
		[[nodiscard]] static Result<InputLog> Deserialize(BufferView bytes);

		[[nodiscard]] Result<void> Save(const std::filesystem::path& path) const;
		[[nodiscard]] static Result<InputLog> Load(const std::filesystem::path& path);

		friend bool operator==(const InputLog& a, const InputLog& b) noexcept
		{
			return a.m_Header == b.m_Header && a.m_Frames == b.m_Frames;
		}

	private:
		InputLogHeader m_Header;
		std::vector<InputFrame> m_Frames;
	};

	inline constexpr size_t MaxInputLogStringLength = 4096;

	// Version of the .ginput file; it also fixes the InputFrame layout the file uses (InputFrameFormatVersion).
	inline constexpr uint16_t InputLogFormatVersion = 1;
}
