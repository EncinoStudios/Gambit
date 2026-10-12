#include <doctest/doctest.h>

#include "Gambit/Core/Hash.h"
#include "Gambit/Math/Random.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <numeric>
#include <vector>

using namespace Gambit;

namespace
{
	// Pearson's chi-square statistic of observed counts against equal expectations.
	double ChiSquare(const std::vector<int>& counts)
	{
		const double expected =
			static_cast<double>(std::accumulate(counts.begin(), counts.end(), 0)) / static_cast<double>(counts.size());
		double sum = 0.0;
		for (const int count : counts)
		{
			sum += (count - expected) * (count - expected) / expected;
		}
		return sum;
	}

	std::vector<uint32_t> Draw(Random& random, int count)
	{
		std::vector<uint32_t> values;
		for (int i = 0; i < count; ++i)
		{
			values.push_back(random.NextU32());
		}
		return values;
	}
}

// The sequences below come from the reference implementation of PCG32 (pcg32_srandom_r + pcg32_random_r); (42, 54)
// is the seed of the pcg-c demo program, whose output starts with 0xa15c02b7 0x7b47f409 0xba1d3330.
TEST_CASE("Math: Random matches the PCG32 reference sequences")
{
	struct Reference
	{
		uint64_t Seed;
		uint64_t Stream;
		std::array<uint32_t, 8> Values;
	};
	const Reference references[] = {
		{ 42,
		  54,
		  { 0xA15C02B7u, 0x7B47F409u, 0xBA1D3330u, 0x83D2F293u, 0xBFA4784Bu, 0xCBED606Eu, 0xBFC6A3ADu, 0x812FFF6Du } },
		{ 0,
		  0,
		  { 0xE4C14788u, 0x379C6516u, 0x5C4AB3BBu, 0x601D23E0u, 0x1C382B8Cu, 0xD1FAAB16u, 0x67680A2Du, 0x92014A6Eu } },
		{ 1,
		  1,
		  { 0xC9828F91u, 0x1592E274u, 0xC0262657u, 0xA5C2B6D3u, 0xAF811256u, 0x6C1C2879u, 0x30130B26u, 0x9CC21C33u } },
		{ 123456789,
		  987654321,
		  { 0x70AA3B49u, 0x2FE445CBu, 0xC5EA87B6u, 0x06DD9503u, 0xF424BE99u, 0x772B761Bu, 0xDA355A90u, 0xB86AD6BBu } },
		{ 0xDEADBEEFCAFEBABEull,
		  0x0123456789ABCDEFull,
		  { 0x1A70EF54u, 0x59A840FEu, 0xB799C69Eu, 0xC645D5C4u, 0x19D3D499u, 0xEAD6EE16u, 0x653E48D6u, 0x810FD50Fu } },
		{ ~0ull,
		  ~0ull,
		  { 0x2675C047u, 0x7779A837u, 0xA145AA13u, 0x5F6BE726u, 0x523C44C5u, 0x75A406D6u, 0xE6643B13u, 0x8E3D51A0u } },
	};
	for (const Reference& reference : references)
	{
		Random random(reference.Seed, reference.Stream);
		for (const uint32_t expected : reference.Values)
		{
			CHECK(random.NextU32() == expected);
		}
	}
}

TEST_CASE("Math: Random seeding follows pcg32_srandom_r")
{
	Random random(0, 0);
	CHECK(random.GetState().State == 0x5851F42D4C957F2Eull);
	CHECK(random.GetState().Increment == 1);

	Random seeded(42, 54);
	CHECK(seeded.GetState().State == 0x185706B82C2E03F8ull);
	CHECK(seeded.GetState().Increment == 0x6D); // (54 << 1) | 1

	Random defaulted;
	CHECK(defaulted.GetState() == Random(0, 0).GetState());
	CHECK(Random(~0ull, ~0ull).GetState().State == 0x4F5C17A566D501A5ull);
}

TEST_CASE("Math: Random sequences are reproducible and streams are independent")
{
	Random first(2024, 5);
	Random second(2024, 5);
	CHECK(Draw(first, 100) == Draw(second, 100));

	Random otherSeed(2025, 5);
	Random otherStream(2024, 6);
	Random base(2024, 5);
	const std::vector<uint32_t> baseline = Draw(base, 100);
	CHECK(Draw(otherSeed, 100) != baseline);
	CHECK(Draw(otherStream, 100) != baseline);

	first.Seed(2024, 5); // reseeding restarts the sequence
	CHECK(Draw(first, 100) == baseline);

	// A long sequence has a checked-in digest: any drift in the generator is caught here.
	Random digest(2024, 5);
	Hasher hasher;
	for (int i = 0; i < 10000; ++i)
	{
		hasher.UpdateValue(digest.NextU32());
	}
	CHECK(hasher.Digest() == 0x6B0AD4B48A61A906ull);
}

TEST_CASE("Math: Random state can be saved, restored and validated")
{
	Random random(99, 1);
	static_cast<void>(Draw(random, 17));
	const RandomState saved = random.GetState();
	const std::vector<uint32_t> expected = Draw(random, 50);

	Random other(1, 2);
	REQUIRE(other.SetState(saved));
	CHECK(other.GetState() == saved);
	CHECK(Draw(other, 50) == expected);

	REQUIRE(random.SetState(saved)); // rolling back
	CHECK(Draw(random, 50) == expected);

	const RandomState before = other.GetState();
	CHECK_FALSE(other.SetState({ 123, 2 })); // an even increment is not a state of any PCG32 stream
	CHECK(other.GetState() == before);
	CHECK(RandomState{} == RandomState{ 0, 1 });
}

TEST_CASE("Math: Random NextU64 and the unit floats are built from whole draws")
{
	Random random(42, 54);
	CHECK(random.NextU64() == 0xA15C02B77B47F409ull);
	CHECK(random.NextU64() == 0xBA1D333083D2F293ull);
	CHECK(random.NextU64() == 0xBFA4784BCBED606Eull);

	// 24-bit floats and 53-bit doubles: the top bits of the draws scaled by a power of two.
	Random floats(42, 54);
	CHECK(floats.NextFloat01() == 0.6303101778030396f);
	CHECK(floats.NextFloat01() == 0.4815666675567627f);
	CHECK(floats.NextFloat01() == 0.727008044719696f);
	CHECK(floats.NextFloat01() == 0.5149375200271606f);

	Random doubles(42, 54);
	CHECK(doubles.NextDouble01() == 0.6303102205231708);
	CHECK(doubles.NextDouble01() == 0.7270080560154601);
	CHECK(doubles.NextDouble01() == 0.7486033616113921);

	Random counted(7, 7);
	Random reference(7, 7);
	static_cast<void>(counted.NextU64());
	static_cast<void>(reference.NextU32());
	static_cast<void>(reference.NextU32());
	CHECK(counted.GetState() == reference.GetState()); // two draws
	static_cast<void>(counted.NextDouble01());
	static_cast<void>(reference.NextU64());
	CHECK(counted.GetState() == reference.GetState());
	static_cast<void>(counted.NextFloat01());
	static_cast<void>(reference.NextU32());
	CHECK(counted.GetState() == reference.GetState()); // one draw
}

TEST_CASE("Math: Random unit floats and doubles stay in [0, 1)")
{
	Random random(5, 5);
	float minimumFloat = 1.0f;
	float maximumFloat = 0.0f;
	double minimumDouble = 1.0;
	double maximumDouble = 0.0;
	for (int i = 0; i < 100000; ++i)
	{
		const float f = random.NextFloat01();
		const double d = random.NextDouble01();
		minimumFloat = std::min(minimumFloat, f);
		maximumFloat = std::max(maximumFloat, f);
		minimumDouble = std::min(minimumDouble, d);
		maximumDouble = std::max(maximumDouble, d);
	}
	CHECK(minimumFloat >= 0.0f);
	CHECK(maximumFloat < 1.0f);
	CHECK(minimumDouble >= 0.0);
	CHECK(maximumDouble < 1.0);
	CHECK(minimumFloat < 0.001f);
	CHECK(maximumFloat > 0.999f);
	CHECK(minimumDouble < 0.001);
	CHECK(maximumDouble > 0.999);
}

TEST_CASE("Math: Random NextBounded is unbiased rejection sampling like pcg32_boundedrand_r")
{
	Random random(42, 54);
	const int expectedDice[] = { 3, 3, 2, 1, 1, 4, 5, 3, 0, 2, 0, 1, 4, 1, 3, 3, 0, 3, 2, 0 };
	for (const int expected : expectedDice)
	{
		CHECK(random.NextBounded(6) == static_cast<uint32_t>(expected));
	}

	Random mixed(7, 3);
	const std::pair<uint32_t, uint32_t> cases[] = { { 1, 0 },
		                                            { 2, 0 },
		                                            { 3, 0 },
		                                            { 10, 0 },
		                                            { 100, 99 },
		                                            { 1000, 767 },
		                                            { 65537, 37645 },
		                                            { 0x80000001u, 275933086u },
		                                            { 0xFFFFFFFFu, 3090171492u },
		                                            { 6, 3 },
		                                            { 6, 4 },
		                                            { 6, 2 } };
	for (const auto& [bound, expected] : cases)
	{
		CHECK(mixed.NextBounded(bound) == expected);
	}

	Random zero(1, 1);
	const RandomState before = zero.GetState();
	CHECK(zero.NextBounded(0) == 0);
	CHECK(zero.GetState() == before); // a bound of zero draws nothing
}

TEST_CASE("Math: Random RangeInt is inclusive, unbiased and handles the extremes")
{
	Random random(7, 3);
	const int expected[] = { -5, 2, 4, 0, 5, 2, 1, -2, -4, 1, -1, 0 };
	for (const int value : expected)
	{
		CHECK(random.RangeInt(-5, 5) == value);
	}

	Random counts(8, 8);
	std::vector<int> buckets(6, 0);
	for (int i = 0; i < 60000; ++i)
	{
		const int32_t value = counts.RangeInt(1, 6);
		REQUIRE(value >= 1);
		REQUIRE(value <= 6);
		++buckets[static_cast<size_t>(value - 1)];
	}
	CHECK(*std::min_element(buckets.begin(), buckets.end()) > 0);
	CHECK(ChiSquare(buckets) < 20.52); // 0.1% critical value for 5 degrees of freedom

	// The span of the whole type is 2^32: the result is min plus a raw draw (2544825812 here), modulo 2^32.
	Random extremes(3, 4);
	CHECK(extremes.RangeInt(std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()) == 397342164);

	Random bounds(9, 9);
	for (int i = 0; i < 1000; ++i)
	{
		const int32_t value =
			bounds.RangeInt(std::numeric_limits<int32_t>::max() - 2, std::numeric_limits<int32_t>::max());
		CHECK(value >= std::numeric_limits<int32_t>::max() - 2);
		const int32_t negative =
			bounds.RangeInt(std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::min() + 1);
		CHECK(negative <= std::numeric_limits<int32_t>::min() + 1);
	}

	Random degenerate(1, 1);
	const RandomState before = degenerate.GetState();
	CHECK(degenerate.RangeInt(4, 4) == 4);
	CHECK(degenerate.RangeInt(9, 2) == 9); // min > max collapses to min
	CHECK(degenerate.GetState() == before);
}

TEST_CASE("Math: Random RangeInt supports 64-bit ranges")
{
	Random random(11, 13);
	CHECK(random.RangeInt(int64_t{ 0 }, int64_t{ 9 }) == 3);
	CHECK(random.RangeInt(-(int64_t{ 1 } << 40), int64_t{ 1 } << 40) == -384533977245);
	CHECK(
		random.RangeInt(std::numeric_limits<int64_t>::min(), std::numeric_limits<int64_t>::max())
		== 1900843485626177670);
	CHECK(random.RangeInt(int64_t{ 5 }, int64_t{ 5 }) == 5);

	Random bounds(12, 12);
	for (int i = 0; i < 1000; ++i)
	{
		const int64_t value = bounds.RangeInt(int64_t{ -3 }, int64_t{ 3 });
		CHECK(value >= -3);
		CHECK(value <= 3);
	}
}

TEST_CASE("Math: Random RangeFloat and NextBool")
{
	Random random(13, 1);
	float lowest = 100.0f;
	float highest = -100.0f;
	for (int i = 0; i < 10000; ++i)
	{
		const float value = random.RangeFloat(-2.5f, 7.5f);
		lowest = std::min(lowest, value);
		highest = std::max(highest, value);
	}
	CHECK(lowest >= -2.5f);
	CHECK(highest < 7.5f);
	CHECK(lowest < -2.4f);
	CHECK(highest > 7.4f);

	Random same(14, 1);
	Random reference(14, 1);
	CHECK(same.RangeFloat(2.0f, 4.0f) == 2.0f + 2.0f * reference.NextFloat01());

	Random flips(15, 2);
	int heads = 0;
	for (int i = 0; i < 20000; ++i)
	{
		heads += flips.NextBool() ? 1 : 0;
	}
	CHECK(heads > 9700);
	CHECK(heads < 10300);

	Random biased(16, 2);
	int hits = 0;
	for (int i = 0; i < 20000; ++i)
	{
		hits += biased.NextBool(0.1f) ? 1 : 0;
	}
	CHECK(hits > 1800);
	CHECK(hits < 2200);

	Random certain(17, 2);
	const RandomState before = certain.GetState();
	for (int i = 0; i < 100; ++i)
	{
		CHECK(certain.NextBool(1.0f));
		CHECK(certain.NextBool(2.0f));
		CHECK_FALSE(certain.NextBool(0.0f));
		CHECK_FALSE(certain.NextBool(-1.0f));
	}
	CHECK_FALSE(certain.GetState() == before);
}

TEST_CASE("Math: Random raw output is uniform")
{
	Random random(21, 0);
	std::vector<int> highBits(16, 0);
	std::vector<int> lowBits(16, 0);
	for (int i = 0; i < 160000; ++i)
	{
		const uint32_t value = random.NextU32();
		++highBits[value >> 28];
		++lowBits[value & 15u];
	}
	CHECK(ChiSquare(highBits) < 37.7); // 0.1% critical value for 15 degrees of freedom
	CHECK(ChiSquare(lowBits) < 37.7);

	std::vector<int> floatBuckets(20, 0);
	Random floats(22, 0);
	for (int i = 0; i < 100000; ++i)
	{
		++floatBuckets[static_cast<size_t>(floats.NextFloat01() * 20.0f)];
	}
	CHECK(ChiSquare(floatBuckets) < 43.8); // 0.1% critical value for 19 degrees of freedom
}

TEST_CASE("Math: Random NextGaussian has the requested mean and deviation")
{
	Random random(31, 4);
	constexpr int Count = 100000;
	double sum = 0.0;
	double sumOfSquares = 0.0;
	int withinOneSigma = 0;
	int withinTwoSigma = 0;
	for (int i = 0; i < Count; ++i)
	{
		const double value = random.NextGaussian(2.0f, 3.0f);
		sum += value;
		sumOfSquares += value * value;
		withinOneSigma += std::abs(value - 2.0) < 3.0 ? 1 : 0;
		withinTwoSigma += std::abs(value - 2.0) < 6.0 ? 1 : 0;
	}
	const double mean = sum / Count;
	const double variance = sumOfSquares / Count - mean * mean;
	CHECK(mean == doctest::Approx(2.0).epsilon(0.01));
	CHECK(std::sqrt(variance) == doctest::Approx(3.0).epsilon(0.02));
	CHECK(static_cast<double>(withinOneSigma) / Count == doctest::Approx(0.6827).epsilon(0.01));
	CHECK(static_cast<double>(withinTwoSigma) / Count == doctest::Approx(0.9545).epsilon(0.01));

	Random standard(32, 4);
	CHECK(std::isfinite(standard.NextGaussian()));
	Random draws(33, 4);
	Random reference(33, 4);
	static_cast<void>(draws.NextGaussian());
	static_cast<void>(reference.NextU64());
	static_cast<void>(reference.NextU64());
	CHECK(draws.GetState() == reference.GetState()); // exactly two double draws (four words)
}

TEST_CASE("Math: Random InsideUnitSphere fills the ball uniformly")
{
	Random random(41, 9);
	constexpr int Count = 40000;
	glm::dvec3 centroid(0.0);
	int innerHalf = 0;
	for (int i = 0; i < Count; ++i)
	{
		const glm::vec3 point = random.InsideUnitSphere();
		const float length = glm::length(point);
		REQUIRE(length <= 1.0f);
		centroid += glm::dvec3(point);
		innerHalf += length < 0.5f ? 1 : 0;
	}
	CHECK(glm::length(centroid / static_cast<double>(Count)) < 0.02);
	// A uniform ball holds (1/2)^3 = 12.5% of its points within half the radius.
	CHECK(static_cast<double>(innerHalf) / Count == doctest::Approx(0.125).epsilon(0.05));
}

TEST_CASE("Math: Random OnUnitSphere returns unit vectors spread over the sphere")
{
	Random random(51, 9);
	constexpr int Count = 40000;
	glm::dvec3 centroid(0.0);
	std::array<int, 8> octants{};
	for (int i = 0; i < Count; ++i)
	{
		const glm::vec3 point = random.OnUnitSphere();
		REQUIRE(glm::length(point) == doctest::Approx(1.0f).epsilon(1e-5));
		centroid += glm::dvec3(point);
		++octants[static_cast<size_t>((point.x > 0 ? 1 : 0) | (point.y > 0 ? 2 : 0) | (point.z > 0 ? 4 : 0))];
	}
	CHECK(glm::length(centroid / static_cast<double>(Count)) < 0.02);
	for (const int octant : octants)
	{
		CHECK(static_cast<double>(octant) / Count == doctest::Approx(0.125).epsilon(0.06));
	}
}

TEST_CASE("Math: Random Shuffle permutes with every permutation equally likely")
{
	Random reference(5, 6);
	std::vector<int> items(10);
	std::iota(items.begin(), items.end(), 0);
	reference.Shuffle(std::span<int>(items));
	CHECK(items == std::vector<int>{ 5, 8, 1, 7, 4, 6, 2, 9, 0, 3 });

	Random random(61, 6);
	std::map<std::array<int, 3>, int> permutations;
	for (int i = 0; i < 60000; ++i)
	{
		std::array<int, 3> three{ 0, 1, 2 };
		random.Shuffle(std::span<int>(three));
		++permutations[three];
	}
	REQUIRE(permutations.size() == 6);
	std::vector<int> counts;
	for (const auto& [permutation, count] : permutations)
	{
		counts.push_back(count);
	}
	CHECK(ChiSquare(counts) < 20.52);

	std::vector<int> none;
	random.Shuffle(std::span<int>(none));
	std::vector<int> one{ 42 };
	const RandomState before = random.GetState();
	random.Shuffle(std::span<int>(one));
	CHECK(one == std::vector<int>{ 42 });
	CHECK(random.GetState() == before); // fewer than two items draw nothing

	std::vector<std::string> names{ "a", "b", "c", "d" };
	random.Shuffle(std::span<std::string>(names));
	std::sort(names.begin(), names.end());
	CHECK(names == std::vector<std::string>{ "a", "b", "c", "d" });
}

TEST_CASE("Math: Random derived values are platform-independent")
{
	// Everything built on DMath and float arithmetic, hashed: the digest must be the same on every compiler.
	Random random(2026, 17);
	Hasher hasher;
	for (int i = 0; i < 200; ++i)
	{
		hasher.UpdateValue(random.NextFloat01());
		hasher.UpdateValue(random.NextGaussian());
		hasher.UpdateValue(random.RangeFloat(-3.0f, 9.0f));
		hasher.UpdateValue(random.NextBool(0.3f));
		hasher.UpdateValue(random.RangeInt(-100, 100));
	}
	for (int i = 0; i < 100; ++i)
	{
		const glm::vec3 inside = random.InsideUnitSphere();
		const glm::vec3 on = random.OnUnitSphere();
		hasher.UpdateValue(inside.x);
		hasher.UpdateValue(inside.y);
		hasher.UpdateValue(inside.z);
		hasher.UpdateValue(on.x);
		hasher.UpdateValue(on.y);
		hasher.UpdateValue(on.z);
	}
	std::array<int, 100> deck;
	std::iota(deck.begin(), deck.end(), 0);
	random.Shuffle(std::span<int>(deck));
	hasher.Update(deck.data(), sizeof(deck));
	hasher.UpdateValue(random.GetState().State);
	CHECK(hasher.Digest() == 0xFAEF4A47237A1749ull);
}
