#pragma once

#include "Gambit/Core/Buffer.h"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

// Hash functions. Every result is a pure function of the input bytes: identical on every platform and run, so the
// values may be stored in files and compared across machines (state hashes, D-013; content hashes).

namespace Gambit
{
	namespace Hash
	{
		// xxHash XXH64 of a byte range. The seed is deliberately not defaulted here: XXH64("text", 12345) must pick
		// the string overload below, not read 12345 bytes from a literal.
		[[nodiscard]] uint64_t XXH64(const void* data, size_t size, uint64_t seed) noexcept;

		[[nodiscard]] inline uint64_t XXH64(BufferView data, uint64_t seed = 0) noexcept
		{
			return XXH64(data.Data(), data.Size(), seed);
		}

		[[nodiscard]] inline uint64_t XXH64(std::string_view text, uint64_t seed = 0) noexcept
		{
			return XXH64(text.data(), text.size(), seed);
		}

		inline constexpr uint64_t FNV1aOffsetBasis = 14695981039346656037ull;
		inline constexpr uint64_t FNV1aPrime = 1099511628211ull;

		// 64-bit FNV-1a of a string. Cheap enough to evaluate at compile time, which makes it the choice for
		// switch labels and name ids (see operator""_hash); XXH64 is the choice for bulk data.
		[[nodiscard]] constexpr uint64_t FNV1a(std::string_view text) noexcept
		{
			uint64_t hash = FNV1aOffsetBasis;
			for (const char character : text)
			{
				hash ^= static_cast<unsigned char>(character);
				hash *= FNV1aPrime;
			}
			return hash;
		}
	}

	// Mixes `value` into `seed` (the boost::hash_combine construction with 64-bit constants). The result depends
	// on the order of the values; it is meant for building hashes of composite keys, not for security.
	[[nodiscard]] constexpr uint64_t HashCombine(uint64_t seed, uint64_t value) noexcept
	{
		return seed ^ (value + 0x9E3779B97F4A7C15ull + (seed << 6) + (seed >> 2));
	}

	// Incremental XXH64: feed data in any number of pieces; Digest() equals Hash::XXH64 of the concatenation.
	// Copyable, so a running state can be forked.
	class Hasher
	{
	public:
		explicit Hasher(uint64_t seed = 0) noexcept;

		// Restarts with `seed`, discarding everything fed so far.
		void Reset(uint64_t seed = 0) noexcept;

		void Update(const void* data, size_t size) noexcept;
		void Update(BufferView data) noexcept { Update(data.Data(), data.Size()); }
		void Update(std::string_view text) noexcept { Update(text.data(), text.size()); }

		// Feeds the object representation of an integer, floating-point value, bool or enum (little-endian, the
		// only byte order the engine supports). Floats are hashed by bit pattern, so -0.0 and 0.0 differ and equal
		// NaN payloads match; that is what state hashing wants.
		template<typename T>
			requires(std::is_arithmetic_v<T> || std::is_enum_v<T>)
		void UpdateValue(T value) noexcept
		{
			Update(&value, sizeof(T));
		}

		// The hash of everything fed so far; the hasher stays usable.
		[[nodiscard]] uint64_t Digest() const noexcept;

	private:
		// Opaque XXH64_state_t (sizeof and alignment are checked in Hash.cpp), so xxhash.h stays out of this header.
		alignas(8) std::byte m_State[88];
	};

	inline namespace Literals
	{
		// "name"_hash is the compile-time FNV-1a of the literal.
		[[nodiscard]] constexpr uint64_t operator""_hash(const char* text, size_t length) noexcept
		{
			return Hash::FNV1a(std::string_view(text, length));
		}
	}
}
