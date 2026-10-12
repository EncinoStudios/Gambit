#include "gbpch.h"

#include "Gambit/Math/Random.h"

#include "Gambit/Math/DMath.h"

namespace Gambit
{
	namespace
	{
		constexpr uint64_t Multiplier = 6364136223846793005ull;
		constexpr float FloatUnit = 1.0f / 16777216.0f;         // 2^-24
		constexpr double DoubleUnit = 1.0 / 9007199254740992.0; // 2^-53
		constexpr double TwoPi = 6.283185307179586476925286766559;
	}

	void Random::Seed(uint64_t seed, uint64_t stream) noexcept
	{
		m_State = 0;
		m_Increment = (stream << 1) | 1;
		static_cast<void>(NextU32());
		m_State += seed;
		static_cast<void>(NextU32());
	}

	bool Random::SetState(const RandomState& state) noexcept
	{
		if ((state.Increment & 1) == 0)
		{
			return false;
		}
		m_State = state.State;
		m_Increment = state.Increment;
		return true;
	}

	uint32_t Random::NextU32() noexcept
	{
		const uint64_t oldState = m_State;
		m_State = oldState * Multiplier + m_Increment;
		const auto xorShifted = static_cast<uint32_t>(((oldState >> 18) ^ oldState) >> 27);
		const auto rotation = static_cast<uint32_t>(oldState >> 59);
		return std::rotr(xorShifted, static_cast<int>(rotation));
	}

	uint64_t Random::NextU64() noexcept
	{
		// Two statements: the order of evaluation of the operands of | is unspecified.
		const uint64_t high = NextU32();
		const uint64_t low = NextU32();
		return (high << 32) | low;
	}

	float Random::NextFloat01() noexcept
	{
		return static_cast<float>(NextU32() >> 8) * FloatUnit;
	}

	double Random::NextDouble01() noexcept
	{
		return static_cast<double>(NextU64() >> 11) * DoubleUnit;
	}

	uint32_t Random::NextBounded(uint32_t bound) noexcept
	{
		if (bound == 0)
		{
			return 0;
		}
		// Reject the draws below 2^32 mod bound: what remains is a whole number of copies of [0, bound).
		const uint32_t threshold = (0u - bound) % bound;
		for (;;)
		{
			const uint32_t value = NextU32();
			if (value >= threshold)
			{
				return value % bound;
			}
		}
	}

	uint64_t Random::NextBounded64(uint64_t bound) noexcept
	{
		if (bound == 0)
		{
			return 0;
		}
		const uint64_t threshold = (0ull - bound) % bound;
		for (;;)
		{
			const uint64_t value = NextU64();
			if (value >= threshold)
			{
				return value % bound;
			}
		}
	}

	int32_t Random::RangeInt(int32_t min, int32_t max) noexcept
	{
		if (min >= max)
		{
			return min;
		}
		// Unsigned arithmetic wraps, which makes the span of the full int32 range come out as 0 (2^32).
		const uint32_t span = static_cast<uint32_t>(max) - static_cast<uint32_t>(min) + 1u;
		const uint32_t offset = span == 0 ? NextU32() : NextBounded(span);
		return static_cast<int32_t>(static_cast<uint32_t>(min) + offset);
	}

	int64_t Random::RangeInt(int64_t min, int64_t max) noexcept
	{
		if (min >= max)
		{
			return min;
		}
		const uint64_t span = static_cast<uint64_t>(max) - static_cast<uint64_t>(min) + 1u;
		const uint64_t offset = span == 0 ? NextU64() : NextBounded64(span);
		return static_cast<int64_t>(static_cast<uint64_t>(min) + offset);
	}

	float Random::RangeFloat(float min, float max) noexcept
	{
		return min + (max - min) * NextFloat01();
	}

	bool Random::NextBool(float probability) noexcept
	{
		return NextFloat01() < probability;
	}

	float Random::NextGaussian(float mean, float standardDeviation) noexcept
	{
		// u1 in (0, 1] keeps the logarithm finite.
		const double u1 = 1.0 - NextDouble01();
		const double u2 = NextDouble01();
		const double radius = std::sqrt(-2.0 * DMath::Log(u1));
		const double standardNormal = radius * DMath::Cos(TwoPi * u2);
		return mean + standardDeviation * static_cast<float>(standardNormal);
	}

	glm::vec3 Random::InsideUnitSphere() noexcept
	{
		for (;;)
		{
			// One statement per draw: fixed order.
			const float x = 2.0f * NextFloat01() - 1.0f;
			const float y = 2.0f * NextFloat01() - 1.0f;
			const float z = 2.0f * NextFloat01() - 1.0f;
			if (x * x + y * y + z * z <= 1.0f)
			{
				return { x, y, z };
			}
		}
	}

	glm::vec3 Random::OnUnitSphere() noexcept
	{
		for (;;)
		{
			const float u = 2.0f * NextFloat01() - 1.0f;
			const float v = 2.0f * NextFloat01() - 1.0f;
			const float s = u * u + v * v;
			if (s < 1.0f)
			{
				const float factor = 2.0f * std::sqrt(1.0f - s);
				return { u * factor, v * factor, 1.0f - 2.0f * s };
			}
		}
	}
}
