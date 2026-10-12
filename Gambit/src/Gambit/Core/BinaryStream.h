#pragma once

#include "Gambit/Core/Buffer.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Gambit
{
	// Appends fixed-width little-endian values to a growing byte buffer: the encoding of the engine's binary formats
	// (input frames and logs, later packs and caches), identical on every platform.
	class BinaryWriter
	{
	public:
		void WriteU8(uint8_t value) { m_Bytes.push_back(static_cast<std::byte>(value)); }
		void WriteU16(uint16_t value) { WriteUnsigned(value, 2); }
		void WriteU32(uint32_t value) { WriteUnsigned(value, 4); }
		void WriteU64(uint64_t value) { WriteUnsigned(value, 8); }
		void WriteI16(int16_t value) { WriteU16(std::bit_cast<uint16_t>(value)); }
		void WriteI32(int32_t value) { WriteU32(std::bit_cast<uint32_t>(value)); }

		void WriteBytes(BufferView bytes) { m_Bytes.insert(m_Bytes.end(), bytes.Data(), bytes.Data() + bytes.Size()); }

		// A 32-bit length followed by the bytes of the string.
		void WriteString(std::string_view text)
		{
			WriteU32(static_cast<uint32_t>(text.size()));
			WriteBytes(BufferView::FromText(text));
		}

		[[nodiscard]] size_t Size() const noexcept { return m_Bytes.size(); }
		[[nodiscard]] BufferView View() const noexcept { return BufferView(m_Bytes.data(), m_Bytes.size()); }
		[[nodiscard]] Buffer ToBuffer() const { return Buffer::Copy(View()); }

	private:
		void WriteUnsigned(uint64_t value, int byteCount)
		{
			for (int i = 0; i < byteCount; ++i)
			{
				m_Bytes.push_back(static_cast<std::byte>((value >> (8 * i)) & 0xFF));
			}
		}

		std::vector<std::byte> m_Bytes;
	};

	// Reads what BinaryWriter wrote from untrusted bytes. A read past the end (or an invalid string length) does not
	// throw: it returns zero and puts the reader in a failed state that stays, so a parser can read a whole structure
	// and check Failed() once.
	class BinaryReader
	{
	public:
		explicit BinaryReader(BufferView bytes) noexcept
			: m_Bytes(bytes)
		{}

		[[nodiscard]] uint8_t ReadU8() noexcept { return static_cast<uint8_t>(ReadUnsigned(1)); }
		[[nodiscard]] uint16_t ReadU16() noexcept { return static_cast<uint16_t>(ReadUnsigned(2)); }
		[[nodiscard]] uint32_t ReadU32() noexcept { return static_cast<uint32_t>(ReadUnsigned(4)); }
		[[nodiscard]] uint64_t ReadU64() noexcept { return ReadUnsigned(8); }
		[[nodiscard]] int16_t ReadI16() noexcept { return std::bit_cast<int16_t>(ReadU16()); }
		[[nodiscard]] int32_t ReadI32() noexcept { return std::bit_cast<int32_t>(ReadU32()); }

		// A string written by WriteString; fails when it is longer than `maxLength` or than the remaining bytes.
		[[nodiscard]] std::string ReadString(size_t maxLength)
		{
			const uint32_t length = ReadU32();
			if (m_Failed || length > maxLength || length > Remaining())
			{
				m_Failed = true;
				return {};
			}
			std::string text(reinterpret_cast<const char*>(m_Bytes.Data() + m_Position), length);
			m_Position += length;
			return text;
		}

		[[nodiscard]] bool Failed() const noexcept { return m_Failed; }
		[[nodiscard]] size_t Remaining() const noexcept { return m_Bytes.Size() - m_Position; }
		[[nodiscard]] size_t GetPosition() const noexcept { return m_Position; }

	private:
		uint64_t ReadUnsigned(int byteCount) noexcept
		{
			if (m_Failed || Remaining() < static_cast<size_t>(byteCount))
			{
				m_Failed = true;
				return 0;
			}
			uint64_t value = 0;
			for (int i = 0; i < byteCount; ++i)
			{
				value |=
					static_cast<uint64_t>(std::to_integer<uint8_t>(m_Bytes.Data()[m_Position + static_cast<size_t>(i)]))
					<< (8 * i);
			}
			m_Position += static_cast<size_t>(byteCount);
			return value;
		}

		BufferView m_Bytes;
		size_t m_Position = 0;
		bool m_Failed = false;
	};
}
