#pragma once

#include "Gambit/Math/Math.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>

namespace Gambit
{
	// The complete state of a Random: enough to save, hash, roll back or replay a generator. `Increment` is always odd.
	struct RandomState
	{
		uint64_t State = 0;
		uint64_t Increment = 1;

		friend constexpr bool operator==(const RandomState&, const RandomState&) noexcept = default;
	};

	// The random number generator of the simulation (D-011): PCG32 (O'Neill's pcg32_random_r, XSH-RR output on a 64-bit
	// LCG), a pure function of its seed and stream, so every platform draws the same numbers. All gameplay randomness
	// comes from the Random owned by the World; std::rand, std::random_device and <random> engines are forbidden in
	// simulation code. The number of values a call consumes is part of its contract (reproducible replays depend on
	// it), and calls that draw several values do so in a fixed order.
	class Random
	{
	public:
		// Equivalent to pcg32_srandom_r(seed, stream): different streams give independent sequences for one seed.
		explicit Random(uint64_t seed = 0, uint64_t stream = 0) noexcept { Seed(seed, stream); }

		void Seed(uint64_t seed, uint64_t stream = 0) noexcept;

		[[nodiscard]] RandomState GetState() const noexcept { return { m_State, m_Increment }; }
		// Returns false, changing nothing, for an invalid state (an even increment cannot come from GetState).
		[[nodiscard]] bool SetState(const RandomState& state) noexcept;

		[[nodiscard]] uint32_t NextU32() noexcept;
		// Two draws: the first becomes the high word.
		[[nodiscard]] uint64_t NextU64() noexcept;
		// Uniform in [0, 1) with 24 random bits: every float in the range with a 2^-24 spacing is equally likely.
		[[nodiscard]] float NextFloat01() noexcept;
		// Uniform in [0, 1) with 53 random bits (two draws).
		[[nodiscard]] double NextDouble01() noexcept;

		// Uniform in [0, bound) without modulo bias (rejection sampling like pcg32_boundedrand_r, so it consumes one
		// draw, rarely more). A bound of 0 returns 0 without drawing.
		[[nodiscard]] uint32_t NextBounded(uint32_t bound) noexcept;

		// Uniform in [min, max], both inclusive, without modulo bias. min must not exceed max. When min >= max, min is
		// returned without drawing (a one-value range consumes nothing).
		[[nodiscard]] int32_t RangeInt(int32_t min, int32_t max) noexcept;
		[[nodiscard]] int64_t RangeInt(int64_t min, int64_t max) noexcept;
		// min + (max - min) * NextFloat01(): one draw.
		[[nodiscard]] float RangeFloat(float min, float max) noexcept;

		// True with the given probability (clamped to [0, 1]); always consumes one draw.
		[[nodiscard]] bool NextBool(float probability = 0.5f) noexcept;

		// Normally distributed (Box-Muller with DMath, two draws, the second value of the pair is discarded so that
		// the generator state stays the only state).
		[[nodiscard]] float NextGaussian(float mean = 0.0f, float standardDeviation = 1.0f) noexcept;

		// Uniform in the unit ball (rejection sampling: a variable number of draws).
		[[nodiscard]] glm::vec3 InsideUnitSphere() noexcept;
		// Uniform on the unit sphere (Marsaglia's method: a variable number of draws).
		[[nodiscard]] glm::vec3 OnUnitSphere() noexcept;

		// Fisher-Yates shuffle: every permutation is equally likely.
		template<typename T>
		void Shuffle(std::span<T> items) noexcept
		{
			for (size_t index = items.size(); index > 1; --index)
			{
				// One draw per step for any realistic size; the 64-bit sampler only exists for spans beyond 2^32 items.
				const uint64_t other = index <= std::numeric_limits<uint32_t>::max()
					? NextBounded(static_cast<uint32_t>(index))
					: NextBounded64(index);
				std::swap(items[index - 1], items[static_cast<size_t>(other)]);
			}
		}

	private:
		// Like NextBounded for bounds beyond 32 bits (two draws per attempt).
		[[nodiscard]] uint64_t NextBounded64(uint64_t bound) noexcept;

		uint64_t m_State = 0;
		uint64_t m_Increment = 1;
	};
}
