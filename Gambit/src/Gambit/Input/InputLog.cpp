#include "gbpch.h"

#include "Gambit/Input/InputLog.h"

#include "Gambit/Core/BinaryStream.h"
#include "Gambit/Core/FileSystem.h"
#include "Gambit/Core/FixedTimestepClock.h"
#include "Gambit/Core/Hash.h"

namespace Gambit
{
	namespace
	{
		constexpr uint32_t Magic = 0x4C494247; // "GBIL" as little endian bytes G B I L
		constexpr uint32_t MaxSimulationHz = FixedTimestepClock::MaxSimulationHz;
		constexpr size_t ChecksumSize = sizeof(uint64_t);
		// A frame without typed text takes this many bytes, so the frame count of a file is bounded by its size.
		constexpr size_t MinimumFrameSize = 102;
	}

	const InputFrame& InputLog::GetFrame(size_t index) const
	{
		GB_CORE_ASSERT(index < m_Frames.size(), "input log frame {} out of range ({} frames)", index, m_Frames.size());
		return m_Frames[index];
	}

	Result<void> InputLog::Validate() const
	{
		if (m_Header.ProjectName.size() > MaxInputLogStringLength
		    || m_Header.ScenePath.size() > MaxInputLogStringLength)
		{
			return Error(std::format(
				"input log: project name and scene path may have at most {} bytes", MaxInputLogStringLength));
		}
		if (m_Header.SimulationHz == 0 || m_Header.SimulationHz > MaxSimulationHz)
		{
			return Error(std::format("input log: invalid simulation rate {} Hz", m_Header.SimulationHz));
		}
		return {};
	}

	Buffer InputLog::Serialize() const
	{
		GB_CORE_ASSERT(Validate().HasValue(), "serializing an input log with an invalid header");
		BinaryWriter writer;
		writer.WriteU32(Magic);
		writer.WriteU16(InputLogFormatVersion);
		writer.WriteU16(InputFrameFormatVersion);
		writer.WriteString(m_Header.ProjectName);
		writer.WriteString(m_Header.ScenePath);
		writer.WriteU64(m_Header.Seed);
		writer.WriteU32(m_Header.SimulationHz);
		writer.WriteU64(m_Frames.size());
		for (const InputFrame& frame : m_Frames)
		{
			frame.Write(writer);
		}
		writer.WriteU64(Hash::XXH64(writer.View(), 0));
		return writer.ToBuffer();
	}

	Result<InputLog> InputLog::Deserialize(BufferView bytes)
	{
		if (bytes.Size() < sizeof(uint32_t) + ChecksumSize)
		{
			return Error("input log: file is too short");
		}
		BinaryReader reader(bytes);
		if (reader.ReadU32() != Magic)
		{
			return Error("input log: not an input log (bad magic)");
		}

		// The checksum covers everything before it; verify it first so that damaged content is reported as damage.
		const BufferView body = bytes.Subview(0, bytes.Size() - ChecksumSize);
		BinaryReader checksumReader(bytes.Subview(bytes.Size() - ChecksumSize, ChecksumSize));
		if (checksumReader.ReadU64() != Hash::XXH64(body, 0))
		{
			return Error("input log: checksum mismatch (the file is damaged or truncated)");
		}

		BinaryReader content(body);
		static_cast<void>(content.ReadU32()); // magic
		const uint16_t version = content.ReadU16();
		const uint16_t frameVersion = content.ReadU16();
		if (version != InputLogFormatVersion || frameVersion != InputFrameFormatVersion)
		{
			return Error(std::format(
				"input log: unsupported format version {}/{} (this build reads {}/{})", version, frameVersion,
				InputLogFormatVersion, InputFrameFormatVersion));
		}

		InputLogHeader header;
		header.ProjectName = content.ReadString(MaxInputLogStringLength);
		header.ScenePath = content.ReadString(MaxInputLogStringLength);
		header.Seed = content.ReadU64();
		header.SimulationHz = content.ReadU32();
		const uint64_t frameCount = content.ReadU64();
		if (content.Failed())
		{
			return Error("input log: damaged header");
		}
		if (header.SimulationHz == 0 || header.SimulationHz > MaxSimulationHz)
		{
			return Error(std::format("input log: invalid simulation rate {} Hz", header.SimulationHz));
		}
		if (frameCount > content.Remaining() / MinimumFrameSize)
		{
			return Error(std::format("input log: {} frames do not fit in {} bytes", frameCount, content.Remaining()));
		}

		InputLog log(std::move(header));
		log.m_Frames.reserve(static_cast<size_t>(frameCount));
		for (uint64_t i = 0; i < frameCount; ++i)
		{
			Result<InputFrame> frame = InputFrame::Read(content);
			if (!frame)
			{
				return Error(std::format("input log: frame {}: {}", i, frame.GetError().Message));
			}
			log.m_Frames.push_back(*frame);
		}
		if (content.Remaining() != 0)
		{
			return Error("input log: unexpected data after the last frame");
		}
		return log;
	}

	Result<void> InputLog::Save(const std::filesystem::path& path) const
	{
		if (Result<void> valid = Validate(); !valid)
		{
			return valid;
		}
		return FileSystem::WriteBinary(path, Serialize().View());
	}

	Result<InputLog> InputLog::Load(const std::filesystem::path& path)
	{
		const Result<Buffer> bytes = FileSystem::ReadBinary(path);
		if (!bytes)
		{
			return bytes.GetError();
		}
		Result<InputLog> log = Deserialize(bytes->View());
		if (!log)
		{
			return Error(std::format("{}: {}", FileSystem::ToUtf8(path), log.GetError().Message));
		}
		return log;
	}
}
