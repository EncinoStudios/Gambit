#pragma once

#include <bit>
#include <cstdint>

// Helpers of the DMath implementation files (musl ports). Not part of the public API.

namespace Gambit::DMath::Internal
{
	[[nodiscard]] inline uint64_t AsBits(double value) noexcept
	{
		return std::bit_cast<uint64_t>(value);
	}

	[[nodiscard]] inline double FromBits(uint64_t bits) noexcept
	{
		return std::bit_cast<double>(bits);
	}

	[[nodiscard]] inline uint32_t HighWord(double value) noexcept
	{
		return static_cast<uint32_t>(AsBits(value) >> 32);
	}

	[[nodiscard]] inline uint32_t LowWord(double value) noexcept
	{
		return static_cast<uint32_t>(AsBits(value));
	}

	[[nodiscard]] inline double WithLowWord(double value, uint32_t low) noexcept
	{
		return FromBits((AsBits(value) & 0xFFFFFFFF00000000ull) | low);
	}

	// The one NaN DMath functions return for NaN results (positive, quiet, zero payload). Hardware default NaNs differ
	// in sign between x86-64 and ARM64, which would make otherwise identical simulations diverge bitwise.
	[[nodiscard]] inline double QuietNaN() noexcept
	{
		return FromBits(0x7FF8000000000000ull);
	}

	[[nodiscard]] inline double PositiveInfinity() noexcept
	{
		return FromBits(0x7FF0000000000000ull);
	}

	// 2^exponent, exact for exponents of normal numbers (-1022 to 1023).
	[[nodiscard]] constexpr double PowerOfTwo(int exponent) noexcept
	{
		return std::bit_cast<double>(static_cast<uint64_t>(0x3FF + exponent) << 52);
	}

	// value * 2^exponent; exact while the result is a normal number, which the callers guarantee (|exponent| < 1000).
	[[nodiscard]] inline double ScaleByPowerOfTwo(double value, int exponent) noexcept
	{
		return value * PowerOfTwo(exponent);
	}
}
