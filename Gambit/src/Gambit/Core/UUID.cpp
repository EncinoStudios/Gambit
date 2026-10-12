#include "gbpch.h"

#include "Gambit/Core/UUID.h"

#include <charconv>
#include <random>
#include <thread>

namespace Gambit
{
	namespace
	{
		// Mixes the clock and the thread id; only used when the operating system refuses to provide entropy.
		std::mt19937_64 CreateFallbackEngine()
		{
			const auto now =
				static_cast<uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
			const auto thread = static_cast<uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
			std::seed_seq sequence{ static_cast<uint32_t>(now), static_cast<uint32_t>(now >> 32),
				                    static_cast<uint32_t>(thread), static_cast<uint32_t>(thread >> 32) };
			return std::mt19937_64(sequence);
		}

		std::mt19937_64 CreateEngine()
		{
			try
			{
				std::random_device device;
				std::array<uint32_t, 8> words{}; // 256 bits: one 32-bit seed would limit the engines to 2^32 streams
				for (uint32_t& word : words)
				{
					word = device();
				}
				std::seed_seq sequence(words.begin(), words.end());
				return std::mt19937_64(sequence);
			}
			catch (const std::exception& exception)
			{
				GB_CORE_WARN("UUID: no OS entropy available ({}); seeding from the clock", exception.what());
				return CreateFallbackEngine();
			}
		}
	}

	UUID UUID::Generate()
	{
		// One engine per thread: no lock, and threads never share a stream.
		thread_local std::mt19937_64 engine = CreateEngine();
		uint64_t value = 0;
		while (value == 0)
		{
			value = engine();
		}
		return UUID(value);
	}

	std::optional<UUID> UUID::Parse(std::string_view text)
	{
		if (text.size() < 3 || text[0] != '0' || (text[1] != 'x' && text[1] != 'X'))
		{
			return std::nullopt;
		}
		const std::string_view digits = text.substr(2);
		if (digits.size() > 16)
		{
			return std::nullopt;
		}

		uint64_t value = 0;
		const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), value, 16);
		if (error != std::errc{} || end != digits.data() + digits.size())
		{
			return std::nullopt;
		}
		// from_chars accepts a leading '-' for signed types only, and never a '+' or whitespace, so every accepted
		// string consists of hexadecimal digits.
		return UUID(value);
	}

	std::string UUID::ToString() const
	{
		return std::format("0x{:016X}", m_Value);
	}
}
