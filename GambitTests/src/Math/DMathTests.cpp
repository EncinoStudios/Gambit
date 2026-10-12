#include <doctest/doctest.h>

#include "Gambit/Core/FileSystem.h"
#include "Gambit/Math/DMath.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <format>
#include <functional>
#include <limits>
#include <string>
#include <vector>

namespace D = Gambit::DMath;

namespace
{
	// splitmix64: a tiny, fixed generator for test inputs, independent of the engine's Random.
	class Sampler
	{
	public:
		explicit Sampler(uint64_t seed)
			: m_State(seed)
		{}

		uint64_t NextBits()
		{
			uint64_t z = (m_State += 0x9E3779B97F4A7C15ull);
			z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
			z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
			return z ^ (z >> 31);
		}

		double Uniform(double low, double high)
		{
			return low + (high - low) * (static_cast<double>(NextBits() >> 11) / 9007199254740992.0);
		}

		// 10^e with e uniform in [low, high].
		double LogUniform(double lowExponent, double highExponent)
		{
			return std::pow(10.0, Uniform(lowExponent, highExponent));
		}

	private:
		uint64_t m_State;
	};

	// The distance in representable doubles; the key maps the bit patterns onto a monotonic integer line.
	int64_t OrderedKey(double value)
	{
		const int64_t bits = std::bit_cast<int64_t>(value);
		return bits < 0 ? std::numeric_limits<int64_t>::min() - bits : bits;
	}

	int64_t UlpDistance(double a, double b)
	{
		if (a == b)
		{
			return 0; // also +0 and -0
		}
		if (std::isnan(a) || std::isnan(b))
		{
			return std::numeric_limits<int64_t>::max();
		}
		const int64_t distance = OrderedKey(a) - OrderedKey(b);
		return distance < 0 ? -distance : distance;
	}

	int32_t OrderedKey(float value)
	{
		const int32_t bits = std::bit_cast<int32_t>(value);
		return bits < 0 ? std::numeric_limits<int32_t>::min() - bits : bits;
	}

	int64_t UlpDistance(float a, float b)
	{
		if (a == b)
		{
			return 0;
		}
		if (std::isnan(a) || std::isnan(b))
		{
			return std::numeric_limits<int64_t>::max();
		}
		const int64_t distance = static_cast<int64_t>(OrderedKey(a)) - static_cast<int64_t>(OrderedKey(b));
		return distance < 0 ? -distance : distance;
	}

	// std results differ from DMath's in the last bit or two, depending on the platform's libm: the bound is the sum
	// of both functions' worst-case errors (each is below one ulp).
	constexpr int64_t MaxUlpVersusStd = 2;

	struct UnaryFunction
	{
		const char* Name;
		double (*Mine)(double);
		double (*Reference)(double);
	};

	void CheckAccuracy(const UnaryFunction& function, const std::vector<double>& inputs)
	{
		int64_t worst = 0;
		double worstInput = 0;
		for (const double x : inputs)
		{
			const int64_t distance = UlpDistance(function.Mine(x), function.Reference(x));
			if (distance > worst)
			{
				worst = distance;
				worstInput = x;
			}
		}
		CHECK_MESSAGE(
			worst <= MaxUlpVersusStd, function.Name, " is ", worst,
			" ulp away from std at x = ", std::format("{:a}", worstInput));
	}

	std::vector<double> Samples(uint64_t seed, double low, double high, int count)
	{
		Sampler sampler(seed);
		std::vector<double> values;
		for (int i = 0; i < count; ++i)
		{
			values.push_back(sampler.Uniform(low, high));
		}
		return values;
	}

	void Append(std::vector<double>& target, const std::vector<double>& more)
	{
		target.insert(target.end(), more.begin(), more.end());
	}

	// Bit patterns of the canonical NaN every DMath function returns.
	constexpr uint64_t CanonicalNaN = 0x7FF8000000000000ull;

	bool IsCanonicalNaN(double value)
	{
		return std::bit_cast<uint64_t>(value) == CanonicalNaN;
	}

	constexpr double Infinity = std::numeric_limits<double>::infinity();
	const double QuietNaN = std::numeric_limits<double>::quiet_NaN();

	// Compares the class of a result with the standard library's: NaN, infinity, signed zero or a finite number.
	void CheckSameClass(const char* name, double mine, double reference, double x, double y = 0.0)
	{
		const std::string context = std::format("{}({:a}, {:a}): mine {:a}, std {:a}", name, x, y, mine, reference);
		if (std::isnan(reference))
		{
			CHECK_MESSAGE(IsCanonicalNaN(mine), context);
		}
		else if (std::isinf(reference))
		{
			CHECK_MESSAGE(mine == reference, context);
		}
		else if (reference == 0.0)
		{
			CHECK_MESSAGE((mine == 0.0 && std::signbit(mine) == std::signbit(reference)), context);
		}
		else
		{
			CHECK_MESSAGE(UlpDistance(mine, reference) <= MaxUlpVersusStd, context);
		}
	}

	const std::vector<double>& SpecialValues()
	{
		static const std::vector<double> values{ 0.0,
			                                     -0.0,
			                                     1.0,
			                                     -1.0,
			                                     0.5,
			                                     -0.5,
			                                     2.0,
			                                     -2.0,
			                                     3.0,
			                                     -3.0,
			                                     1e-310, // subnormal
			                                     -1e-310,
			                                     std::numeric_limits<double>::denorm_min(),
			                                     std::numeric_limits<double>::min(),
			                                     1e300,
			                                     -1e300,
			                                     std::numeric_limits<double>::max(),
			                                     710.0,
			                                     -750.0,
			                                     1075.0,
			                                     Infinity,
			                                     -Infinity,
			                                     QuietNaN };
		return values;
	}
}

// ---- accuracy against the standard library ----

TEST_CASE("Math: DMath trigonometric functions agree with std within 2 ulp")
{
	std::vector<double> inputs = Samples(1, -10.0, 10.0, 4000);
	Append(inputs, Samples(2, -1000.0, 1000.0, 2000));
	Append(inputs, Samples(3, -1.0e7, 1.0e7, 1000));
	Append(inputs, Samples(4, -1.0e-3, 1.0e-3, 500));
	Sampler sampler(5);
	for (int i = 0; i < 500; ++i)
	{
		inputs.push_back(
			sampler.LogUniform(-10, 300) * (i % 2 == 0 ? 1.0 : -1.0)); // huge arguments use the slow reduction
	}
	for (int k = -200; k <= 200; ++k)
	{
		inputs.push_back(k * 1.5707963267948966); // next to multiples of pi/2, where cancellation bites
		inputs.push_back(std::nextafter(k * 1.5707963267948966, 1e300));
	}

	CheckAccuracy({ "Sin", D::Sin, [](double x) { return std::sin(x); } }, inputs);
	CheckAccuracy({ "Cos", D::Cos, [](double x) { return std::cos(x); } }, inputs);
	CheckAccuracy({ "Tan", D::Tan, [](double x) { return std::tan(x); } }, inputs);
}

TEST_CASE("Math: DMath inverse trigonometric functions agree with std within 2 ulp")
{
	std::vector<double> unit = Samples(11, -1.0, 1.0, 6000);
	Append(unit, Samples(12, 0.9, 1.0, 1000));
	Append(unit, Samples(13, -1.0, -0.9, 1000));
	Append(unit, Samples(14, -1.0e-3, 1.0e-3, 500));
	CheckAccuracy({ "Asin", D::Asin, [](double x) { return std::asin(x); } }, unit);
	CheckAccuracy({ "Acos", D::Acos, [](double x) { return std::acos(x); } }, unit);

	std::vector<double> wide = Samples(15, -20.0, 20.0, 5000);
	Sampler sampler(16);
	for (int i = 0; i < 1000; ++i)
	{
		wide.push_back(sampler.LogUniform(-10, 300) * (i % 2 == 0 ? 1.0 : -1.0));
	}
	CheckAccuracy({ "Atan", D::Atan, [](double x) { return std::atan(x); } }, wide);

	int64_t worst = 0;
	for (int i = 0; i < 20000; ++i)
	{
		const double y = (i % 3 == 0) ? sampler.Uniform(-5.0, 5.0) : sampler.Uniform(-1.0e5, 1.0e5);
		const double x = (i % 5 == 0) ? sampler.Uniform(-1.0e-3, 1.0e-3) : sampler.Uniform(-100.0, 100.0);
		worst = std::max(worst, UlpDistance(D::Atan2(y, x), std::atan2(y, x)));
	}
	CHECK_MESSAGE(worst <= MaxUlpVersusStd, "Atan2 is ", worst, " ulp away from std");
}

TEST_CASE("Math: DMath exponential and logarithm agree with std within 2 ulp")
{
	std::vector<double> exponents = Samples(21, -20.0, 20.0, 5000);
	Append(exponents, Samples(22, -745.0, 709.0, 3000));
	Append(exponents, Samples(23, -1.0e-3, 1.0e-3, 500));
	CheckAccuracy({ "Exp", D::Exp, [](double x) { return std::exp(x); } }, exponents);

	std::vector<double> positives = Samples(24, 0.001, 1000.0, 5000);
	Append(positives, Samples(25, 0.9, 1.1, 2000)); // around 1, where log is nearly zero
	Sampler sampler(26);
	for (int i = 0; i < 1500; ++i)
	{
		positives.push_back(sampler.LogUniform(-320, 308)); // includes subnormals
	}
	CheckAccuracy({ "Log", D::Log, [](double x) { return std::log(x); } }, positives);
	CheckAccuracy({ "Log2", D::Log2, [](double x) { return std::log2(x); } }, positives);
}

TEST_CASE("Math: DMath Pow agrees with std within 2 ulp")
{
	Sampler sampler(31);
	int64_t worst = 0;
	for (int i = 0; i < 20000; ++i)
	{
		double x = sampler.Uniform(0.0, 100.0);
		double y = sampler.Uniform(-20.0, 20.0);
		if (i % 4 == 1)
		{
			x = sampler.Uniform(0.9, 1.1);
			y = sampler.Uniform(-1000.0, 1000.0);
		}
		else if (i % 4 == 2)
		{
			x = -sampler.Uniform(0.0, 50.0); // negative bases with integer exponents
			y = std::floor(sampler.Uniform(-15.0, 15.0));
		}
		else if (i % 4 == 3)
		{
			x = sampler.LogUniform(-300, 300);
			y = sampler.Uniform(-2.0, 2.0);
		}
		worst = std::max(worst, UlpDistance(D::Pow(x, y), std::pow(x, y)));
	}
	CHECK_MESSAGE(worst <= MaxUlpVersusStd, "Pow is ", worst, " ulp away from std");
}

TEST_CASE("Math: DMath Fmod is exact")
{
	Sampler sampler(41);
	for (int i = 0; i < 20000; ++i)
	{
		const double x =
			(i % 2 == 0) ? sampler.Uniform(-1000.0, 1000.0) : sampler.LogUniform(-300, 300) * (i % 4 == 1 ? 1.0 : -1.0);
		const double y = (i % 3 == 0) ? sampler.Uniform(0.001, 50.0) : sampler.LogUniform(-310, 300);
		const double mine = D::Fmod(x, y);
		const double reference = std::fmod(x, y);
		if (std::bit_cast<uint64_t>(mine) != std::bit_cast<uint64_t>(reference))
		{
			FAIL(std::format("Fmod({:a}, {:a}) = {:a}, std {:a}", x, y, mine, reference));
		}
	}
}

// ---- special values ----

TEST_CASE("Math: DMath unary functions handle special values like the C library")
{
	for (const double x : SpecialValues())
	{
		CheckSameClass("sin", D::Sin(x), std::sin(x), x);
		CheckSameClass("cos", D::Cos(x), std::cos(x), x);
		CheckSameClass("tan", D::Tan(x), std::tan(x), x);
		CheckSameClass("asin", D::Asin(x), std::asin(x), x);
		CheckSameClass("acos", D::Acos(x), std::acos(x), x);
		CheckSameClass("atan", D::Atan(x), std::atan(x), x);
		CheckSameClass("exp", D::Exp(x), std::exp(x), x);
		CheckSameClass("log", D::Log(x), std::log(x), x);
		CheckSameClass("log2", D::Log2(x), std::log2(x), x);
	}
}

TEST_CASE("Math: DMath binary functions handle special values like the C library")
{
	for (const double x : SpecialValues())
	{
		for (const double y : SpecialValues())
		{
			CheckSameClass("pow", D::Pow(x, y), std::pow(x, y), x, y);
			CheckSameClass("atan2", D::Atan2(x, y), std::atan2(x, y), x, y);
			CheckSameClass("fmod", D::Fmod(x, y), std::fmod(x, y), x, y);
		}
	}
}

TEST_CASE("Math: DMath returns one canonical NaN and exact values at exact points")
{
	CHECK(IsCanonicalNaN(D::Sin(Infinity)));
	CHECK(IsCanonicalNaN(D::Cos(-Infinity)));
	CHECK(IsCanonicalNaN(D::Tan(QuietNaN)));
	CHECK(IsCanonicalNaN(D::Asin(1.5)));
	CHECK(IsCanonicalNaN(D::Acos(-1.0000001)));
	CHECK(IsCanonicalNaN(D::Atan(QuietNaN)));
	CHECK(IsCanonicalNaN(D::Atan2(QuietNaN, 1.0)));
	CHECK(IsCanonicalNaN(D::Exp(QuietNaN)));
	CHECK(IsCanonicalNaN(D::Log(-1.0)));
	CHECK(IsCanonicalNaN(D::Log2(-Infinity)));
	CHECK(IsCanonicalNaN(D::Pow(-2.0, 0.5)));
	CHECK(IsCanonicalNaN(D::Pow(QuietNaN, 2.0)));
	CHECK(IsCanonicalNaN(D::Fmod(1.0, 0.0)));
	CHECK(IsCanonicalNaN(D::Fmod(Infinity, 2.0)));
	// A NaN with a negative sign or a payload comes out canonical as well.
	CHECK(IsCanonicalNaN(D::Sin(std::bit_cast<double>(0xFFF8000000000123ull))));
	CHECK(IsCanonicalNaN(D::Pow(std::bit_cast<double>(0xFFF0000000000001ull), 3.0)));

	CHECK(D::Sin(0.0) == 0.0);
	CHECK(std::signbit(D::Sin(-0.0)));
	CHECK(D::Cos(0.0) == 1.0);
	CHECK(D::Exp(0.0) == 1.0);
	CHECK(D::Log(1.0) == 0.0);
	CHECK(D::Log2(8.0) == 3.0);
	CHECK(D::Log2(0.5) == -1.0);
	CHECK(D::Log2(1024.0) == 10.0);
	CHECK(D::Pow(2.0, 10.0) == 1024.0);
	CHECK(D::Pow(2.0, -2.0) == 0.25);
	CHECK(D::Pow(-2.0, 3.0) == -8.0);
	CHECK(D::Pow(7.0, 0.0) == 1.0);
	CHECK(D::Pow(1.0, QuietNaN) == 1.0);
	CHECK(D::Exp(710.0) == Infinity);
	CHECK(D::Exp(-750.0) == 0.0);
	CHECK(D::Log(0.0) == -Infinity);
	CHECK(D::Atan2(1.0, 0.0) == D::Asin(1.0));
	CHECK(D::Fmod(7.5, 2.0) == 1.5);
	CHECK(D::Fmod(-7.5, 2.0) == -1.5);
	CHECK(D::Fmod(7.5, -2.0) == 1.5);
	CHECK(std::signbit(D::Fmod(-4.0, 2.0)));
	CHECK(D::Fmod(1.0, Infinity) == 1.0);
}

TEST_CASE("Math: DMath identities hold")
{
	Sampler sampler(51);
	for (int i = 0; i < 2000; ++i)
	{
		const double x = sampler.Uniform(-10.0, 10.0);
		const double s = D::Sin(x);
		const double c = D::Cos(x);
		CHECK(std::abs(s * s + c * c - 1.0) < 1e-15);
		CHECK(D::Sin(-x) == -s);
		CHECK(D::Cos(-x) == c);
		CHECK(D::Atan2(s, c) == doctest::Approx(std::remainder(x, 6.283185307179586)).epsilon(1e-12));
		const double e = D::Exp(x);
		CHECK(std::abs(D::Log(e) - x) < 1e-14);
		CHECK(D::Pow(e, 2.0) == doctest::Approx(D::Exp(2.0 * x)).epsilon(1e-14));
	}
}

// ---- float overloads ----

TEST_CASE("Math: DMath float overloads round the double result once and stay within 1 ulp of std")
{
	Sampler sampler(61);
	int64_t worstCos = 0;
	int64_t worstExp = 0;
	int64_t worstLog = 0;
	int64_t worstPow = 0;
	int64_t worstAtan2 = 0;
	for (int i = 0; i < 5000; ++i)
	{
		const auto x = static_cast<float>(sampler.Uniform(-100.0, 100.0));
		const auto y = static_cast<float>(sampler.Uniform(-10.0, 10.0));
		const auto positive = static_cast<float>(sampler.Uniform(0.001, 1000.0));

		CHECK(D::Sin(x) == static_cast<float>(D::Sin(static_cast<double>(x))));
		CHECK(D::Pow(positive, y) == static_cast<float>(D::Pow(static_cast<double>(positive), static_cast<double>(y))));

		worstCos = std::max(worstCos, UlpDistance(D::Cos(x), std::cos(x)));
		worstExp = std::max(worstExp, UlpDistance(D::Exp(y), std::exp(y)));
		worstLog = std::max(worstLog, UlpDistance(D::Log(positive), std::log(positive)));
		worstPow = std::max(worstPow, UlpDistance(D::Pow(positive, y), std::pow(positive, y)));
		worstAtan2 = std::max(worstAtan2, UlpDistance(D::Atan2(y, x), std::atan2(y, x)));
	}
	CHECK(worstCos <= 1);
	CHECK(worstExp <= 1);
	CHECK(worstLog <= 1);
	CHECK(worstPow <= 1);
	CHECK(worstAtan2 <= 1);

	CHECK(D::Fmod(7.5f, 2.0f) == 1.5f);
	CHECK(D::Tan(0.0f) == 0.0f);
	CHECK(D::Asin(1.0f) == doctest::Approx(1.5707964f));
	CHECK(D::Acos(-1.0f) == doctest::Approx(3.1415927f));
	CHECK(D::Atan(1.0f) == doctest::Approx(0.7853982f));
	CHECK(D::Log2(8.0f) == 3.0f);
	CHECK(std::isnan(D::Sin(std::numeric_limits<float>::infinity())));
}

// ---- bit-exact reference table ----

namespace
{
	struct UnaryCase
	{
		uint64_t X;
		uint64_t Result;
	};

	struct BinaryCase
	{
		uint64_t X;
		uint64_t Y;
		uint64_t Result;
	};

	struct UnaryCaseF
	{
		uint32_t X;
		uint32_t Result;
	};

	struct BinaryCaseF
	{
		uint32_t X;
		uint32_t Y;
		uint32_t Result;
	};

#include "DMathReference.inl"

	// The inputs of the reference table: fixed anchors and points from a fixed generator, ~50 per function. Changing
	// them (or an implementation) is a deliberate act: regenerate the table, see TableUpdateRequested().
	struct ReferenceInputs
	{
		std::vector<double> Sin;
		std::vector<double> Cos;
		std::vector<double> Tan;
		std::vector<double> Asin;
		std::vector<double> Acos;
		std::vector<double> Atan;
		std::vector<double> Exp;
		std::vector<double> Log;
		std::vector<double> Log2;
		std::vector<std::pair<double, double>> Atan2; // (y, x)
		std::vector<std::pair<double, double>> Pow;
		std::vector<std::pair<double, double>> Fmod;
	};

	ReferenceInputs MakeReferenceInputs()
	{
		ReferenceInputs inputs;
		const std::vector<double> angles{ 0.0,
			                              0.5,
			                              -0.5,
			                              1.0,
			                              2.0,
			                              3.0,
			                              1.5707963267948966,
			                              3.141592653589793,
			                              6.283185307179586,
			                              -1.0,
			                              -2.5,
			                              10.0,
			                              100.0,
			                              1000.0,
			                              1.0e5,
			                              1647099.0,
			                              1.0e9,
			                              1.0e15,
			                              1.0e22,
			                              1.0e100,
			                              1.0e300,
			                              -1.0e300,
			                              0.7853981633974483,
			                              2.356194490192345,
			                              4.71238898038469 };
		for (auto* target : { &inputs.Sin, &inputs.Cos, &inputs.Tan })
		{
			*target = angles;
			Append(*target, Samples(101, -10.0, 10.0, 10));
			Append(*target, Samples(102, -1.0e4, 1.0e4, 10));
			Append(*target, Samples(103, -1.0e12, 1.0e12, 5));
		}

		const std::vector<double> unit{
			0.0, 1.0, -1.0, 0.5, -0.5, 0.25, 0.75, 0.9, 0.975, 0.99, 1.0e-9, 1.0e-20, -0.3
		};
		for (auto* target : { &inputs.Asin, &inputs.Acos })
		{
			*target = unit;
			Append(*target, Samples(104, -1.0, 1.0, 37));
		}

		inputs.Atan = { 0.0, 0.2, 0.5, 1.0, 1.5, 2.0, 3.0, 10.0, 100.0, 1.0e10, 1.0e20, 1.0e300, -0.4, -7.0 };
		Append(inputs.Atan, Samples(105, -20.0, 20.0, 30));
		Sampler logSampler(106);
		for (int i = 0; i < 6; ++i)
		{
			inputs.Atan.push_back(logSampler.LogUniform(-12, 30));
		}

		inputs.Exp = { 0.0,   1.0,    -1.0,   0.5,    10.0,  -10.0,   100.0,       700.0,
			           709.7, -700.0, -740.0, -745.0, 710.0, 1.0e-10, 0.0009765625 };
		Append(inputs.Exp, Samples(107, -20.0, 20.0, 20));
		Append(inputs.Exp, Samples(108, -700.0, 700.0, 15));

		inputs.Log = { 1.0,       2.0,       10.0, 0.5,    0.1,  1.0e-300,         1.0e300, 4.9406564584124654e-324,
			           1.0000001, 0.9999999, 8.0,  1024.0, 0.75, 2.718281828459045 };
		Sampler positiveSampler(109);
		for (int i = 0; i < 24; ++i)
		{
			// Most points lie in the range of a float so that the float table has them too.
			inputs.Log.push_back(i < 18 ? positiveSampler.LogUniform(-37, 37) : positiveSampler.LogUniform(-300, 300));
		}
		Append(inputs.Log, Samples(110, 0.9, 1.1, 12));
		inputs.Log2 = inputs.Log;

		inputs.Atan2 = { { 0.0, 1.0 },          { 1.0, 0.0 },   { 0.0, -1.0 },    { -1.0, 0.0 },
			             { 1.0, 1.0 },          { 1.0, -1.0 },  { -1.0, -1.0 },   { -1.0, 1.0 },
			             { 3.0, 4.0 },          { -3.0, 4.0 },  { 1.0e-10, 5.0 }, { 5.0, 1.0e-10 },
			             { 1.0e300, 1.0e-300 }, { 0.5, -0.25 }, { -0.0, -1.0 },   { 2.0, 1.0 } };
		Sampler pairSampler(111);
		for (int i = 0; i < 34; ++i)
		{
			const double y = pairSampler.Uniform(-100.0, 100.0);
			const double x = (i % 4 == 0) ? pairSampler.Uniform(-0.01, 0.01) : pairSampler.Uniform(-100.0, 100.0);
			inputs.Atan2.push_back({ y, x });
		}

		inputs.Pow = { { 2.0, 10.0 },   { 2.0, 0.5 },
			           { 10.0, -3.0 },  { 0.5, 2.0 },
			           { 9.0, 0.5 },    { -8.0, 3.0 },
			           { -2.0, 4.0 },   { 1.0001, 10000.0 },
			           { 3.0, 3.3 },    { 1.0e10, 30.0 },
			           { 2.0, 1023.0 }, { 2.0, -1074.0 },
			           { 0.1, 0.1 },    { 2.718281828459045, 1.0 },
			           { 0.0, 3.0 },    { 5.0, 0.0 } };
		Sampler powSampler(112);
		for (int i = 0; i < 20; ++i)
		{
			inputs.Pow.push_back({ powSampler.Uniform(0.0, 100.0), powSampler.Uniform(-20.0, 20.0) });
		}
		for (int i = 0; i < 8; ++i)
		{
			inputs.Pow.push_back({ -powSampler.Uniform(0.0, 50.0), std::floor(powSampler.Uniform(-9.0, 9.0)) });
		}
		for (int i = 0; i < 6; ++i)
		{
			inputs.Pow.push_back({ powSampler.Uniform(0.9, 1.1), powSampler.Uniform(-1000.0, 1000.0) });
		}

		inputs.Fmod = { { 5.5, 2.0 }, { -5.5, 2.0 },    { 5.5, -2.0 },          { 1.0e10, 3.0 },
			            { 1.0, 0.1 }, { 1.0e300, 7.0 }, { 0.3, 0.1 },           { 100.0, 1.0e-5 },
			            { 4.0, 2.0 }, { 1.0, 1.0e300 }, { 1.0e-310, 3.0e-311 }, { 123456.789, 0.5 } };
		Sampler fmodSampler(113);
		for (int i = 0; i < 38; ++i)
		{
			inputs.Fmod.push_back({ fmodSampler.Uniform(-1000.0, 1000.0),
			                        (i % 2 == 0) ? fmodSampler.Uniform(0.001, 50.0) : fmodSampler.LogUniform(-5, 5) });
		}
		return inputs;
	}

	bool TableUpdateRequested()
	{
		const char* value = std::getenv("GB_UPDATE_GOLDEN");
		return value != nullptr && *value != '\0' && std::string_view(value) != "0";
	}

	template<typename F>
	void AppendUnary(std::string& out, const char* name, const std::vector<double>& inputs, F function)
	{
		out += std::format("inline constexpr UnaryCase {}Cases[] = {{\n", name);
		for (const double x : inputs)
		{
			out += std::format(
				"\t{{ 0x{:016X}ull, 0x{:016X}ull }},\n", std::bit_cast<uint64_t>(x),
				std::bit_cast<uint64_t>(function(x)));
		}
		out += "};\n\n";
	}

	template<typename F>
	void
	AppendBinary(std::string& out, const char* name, const std::vector<std::pair<double, double>>& inputs, F function)
	{
		out += std::format("inline constexpr BinaryCase {}Cases[] = {{\n", name);
		for (const auto& [x, y] : inputs)
		{
			out += std::format(
				"\t{{ 0x{:016X}ull, 0x{:016X}ull, 0x{:016X}ull }},\n", std::bit_cast<uint64_t>(x),
				std::bit_cast<uint64_t>(y), std::bit_cast<uint64_t>(function(x, y)));
		}
		out += "};\n\n";
	}

	template<typename F>
	void AppendUnaryF(std::string& out, const char* name, const std::vector<double>& inputs, F function)
	{
		out += std::format("inline constexpr UnaryCaseF {}CasesF[] = {{\n", name);
		for (const double input : inputs)
		{
			const auto x = static_cast<float>(input);
			if (!std::isfinite(x))
			{
				continue;
			}
			out += std::format(
				"\t{{ 0x{:08X}u, 0x{:08X}u }},\n", std::bit_cast<uint32_t>(x), std::bit_cast<uint32_t>(function(x)));
		}
		out += "};\n\n";
	}

	template<typename F>
	void
	AppendBinaryF(std::string& out, const char* name, const std::vector<std::pair<double, double>>& inputs, F function)
	{
		out += std::format("inline constexpr BinaryCaseF {}CasesF[] = {{\n", name);
		for (const auto& [a, b] : inputs)
		{
			const auto x = static_cast<float>(a);
			const auto y = static_cast<float>(b);
			if (!std::isfinite(x) || !std::isfinite(y))
			{
				continue;
			}
			out += std::format(
				"\t{{ 0x{:08X}u, 0x{:08X}u, 0x{:08X}u }},\n", std::bit_cast<uint32_t>(x), std::bit_cast<uint32_t>(y),
				std::bit_cast<uint32_t>(function(x, y)));
		}
		out += "};\n\n";
	}

	std::string GenerateReferenceTable()
	{
		const ReferenceInputs in = MakeReferenceInputs();
		std::string out = "// Reference results of Gambit::DMath for bit-exactness tests (DMathTests.cpp): every "
						  "platform and compiler must\n"
						  "// reproduce these exactly. Generated, do not edit: run the test \"Math: DMath matches the "
						  "reference table\" with\n"
						  "// GB_UPDATE_GOLDEN=1 after an intended change of DMath, review the diff, and say so in the "
						  "commit message.\n"
						  "// The current values were cross-checked against musl libc 1.2.6 compiled with GCC "
						  "(identical in every bit).\n"
						  "// clang-format off\n\n";
		AppendUnary(out, "Sin", in.Sin, [](double x) { return D::Sin(x); });
		AppendUnary(out, "Cos", in.Cos, [](double x) { return D::Cos(x); });
		AppendUnary(out, "Tan", in.Tan, [](double x) { return D::Tan(x); });
		AppendUnary(out, "Asin", in.Asin, [](double x) { return D::Asin(x); });
		AppendUnary(out, "Acos", in.Acos, [](double x) { return D::Acos(x); });
		AppendUnary(out, "Atan", in.Atan, [](double x) { return D::Atan(x); });
		AppendUnary(out, "Exp", in.Exp, [](double x) { return D::Exp(x); });
		AppendUnary(out, "Log", in.Log, [](double x) { return D::Log(x); });
		AppendUnary(out, "Log2", in.Log2, [](double x) { return D::Log2(x); });
		AppendBinary(out, "Atan2", in.Atan2, [](double y, double x) { return D::Atan2(y, x); });
		AppendBinary(out, "Pow", in.Pow, [](double x, double y) { return D::Pow(x, y); });
		AppendBinary(out, "Fmod", in.Fmod, [](double x, double y) { return D::Fmod(x, y); });

		AppendUnaryF(out, "Sin", in.Sin, [](float x) { return D::Sin(x); });
		AppendUnaryF(out, "Cos", in.Cos, [](float x) { return D::Cos(x); });
		AppendUnaryF(out, "Tan", in.Tan, [](float x) { return D::Tan(x); });
		AppendUnaryF(out, "Asin", in.Asin, [](float x) { return D::Asin(x); });
		AppendUnaryF(out, "Acos", in.Acos, [](float x) { return D::Acos(x); });
		AppendUnaryF(out, "Atan", in.Atan, [](float x) { return D::Atan(x); });
		AppendUnaryF(out, "Exp", in.Exp, [](float x) { return D::Exp(x); });
		AppendUnaryF(out, "Log", in.Log, [](float x) { return D::Log(x); });
		AppendUnaryF(out, "Log2", in.Log2, [](float x) { return D::Log2(x); });
		AppendBinaryF(out, "Atan2", in.Atan2, [](float y, float x) { return D::Atan2(y, x); });
		AppendBinaryF(out, "Pow", in.Pow, [](float x, float y) { return D::Pow(x, y); });
		AppendBinaryF(out, "Fmod", in.Fmod, [](float x, float y) { return D::Fmod(x, y); });
		out += "// clang-format on\n";
		return out;
	}

	template<size_t N, typename F>
	void CheckUnary(const char* name, const UnaryCase (&cases)[N], F function)
	{
		CHECK_MESSAGE(N >= 40, name, " has only ", N, " reference cases");
		for (const UnaryCase& row : cases)
		{
			const uint64_t actual = std::bit_cast<uint64_t>(function(std::bit_cast<double>(row.X)));
			CHECK_MESSAGE(
				actual == row.Result,
				std::format(
					"{}({:a}) = 0x{:016X}, expected 0x{:016X}", name, std::bit_cast<double>(row.X), actual,
					row.Result));
		}
	}

	template<size_t N, typename F>
	void CheckBinary(const char* name, const BinaryCase (&cases)[N], F function)
	{
		CHECK_MESSAGE(N >= 40, name, " has only ", N, " reference cases");
		for (const BinaryCase& row : cases)
		{
			const uint64_t actual =
				std::bit_cast<uint64_t>(function(std::bit_cast<double>(row.X), std::bit_cast<double>(row.Y)));
			CHECK_MESSAGE(
				actual == row.Result,
				std::format(
					"{}({:a}, {:a}) = 0x{:016X}, expected 0x{:016X}", name, std::bit_cast<double>(row.X),
					std::bit_cast<double>(row.Y), actual, row.Result));
		}
	}

	template<size_t N, typename F>
	void CheckUnaryF(const char* name, const UnaryCaseF (&cases)[N], F function)
	{
		CHECK_MESSAGE(N >= 40, name, " has only ", N, " float reference cases");
		for (const UnaryCaseF& row : cases)
		{
			const uint32_t actual = std::bit_cast<uint32_t>(function(std::bit_cast<float>(row.X)));
			CHECK_MESSAGE(
				actual == row.Result,
				std::format(
					"{}f({:a}) = 0x{:08X}, expected 0x{:08X}", name, std::bit_cast<float>(row.X), actual, row.Result));
		}
	}

	template<size_t N, typename F>
	void CheckBinaryF(const char* name, const BinaryCaseF (&cases)[N], F function)
	{
		CHECK_MESSAGE(N >= 40, name, " has only ", N, " float reference cases");
		for (const BinaryCaseF& row : cases)
		{
			const uint32_t actual =
				std::bit_cast<uint32_t>(function(std::bit_cast<float>(row.X), std::bit_cast<float>(row.Y)));
			CHECK_MESSAGE(
				actual == row.Result,
				std::format(
					"{}f({:a}, {:a}) = 0x{:08X}, expected 0x{:08X}", name, std::bit_cast<float>(row.X),
					std::bit_cast<float>(row.Y), actual, row.Result));
		}
	}
}

TEST_CASE("Math: DMath matches the reference table bit for bit")
{
	if (TableUpdateRequested())
	{
		const std::string path = std::string(GB_TEST_SOURCE_DIR) + "/Math/DMathReference.inl";
		REQUIRE(Gambit::FileSystem::WriteText(path, GenerateReferenceTable()).HasValue());
		FAIL("regenerated ", path, ": review the diff, rebuild and run again without GB_UPDATE_GOLDEN");
	}

	CheckUnary("Sin", SinCases, [](double x) { return D::Sin(x); });
	CheckUnary("Cos", CosCases, [](double x) { return D::Cos(x); });
	CheckUnary("Tan", TanCases, [](double x) { return D::Tan(x); });
	CheckUnary("Asin", AsinCases, [](double x) { return D::Asin(x); });
	CheckUnary("Acos", AcosCases, [](double x) { return D::Acos(x); });
	CheckUnary("Atan", AtanCases, [](double x) { return D::Atan(x); });
	CheckUnary("Exp", ExpCases, [](double x) { return D::Exp(x); });
	CheckUnary("Log", LogCases, [](double x) { return D::Log(x); });
	CheckUnary("Log2", Log2Cases, [](double x) { return D::Log2(x); });
	CheckBinary("Atan2", Atan2Cases, [](double y, double x) { return D::Atan2(y, x); });
	CheckBinary("Pow", PowCases, [](double x, double y) { return D::Pow(x, y); });
	CheckBinary("Fmod", FmodCases, [](double x, double y) { return D::Fmod(x, y); });
}

TEST_CASE("Math: DMath float overloads match the reference table bit for bit")
{
	CheckUnaryF("Sin", SinCasesF, [](float x) { return D::Sin(x); });
	CheckUnaryF("Cos", CosCasesF, [](float x) { return D::Cos(x); });
	CheckUnaryF("Tan", TanCasesF, [](float x) { return D::Tan(x); });
	CheckUnaryF("Asin", AsinCasesF, [](float x) { return D::Asin(x); });
	CheckUnaryF("Acos", AcosCasesF, [](float x) { return D::Acos(x); });
	CheckUnaryF("Atan", AtanCasesF, [](float x) { return D::Atan(x); });
	CheckUnaryF("Exp", ExpCasesF, [](float x) { return D::Exp(x); });
	CheckUnaryF("Log", LogCasesF, [](float x) { return D::Log(x); });
	CheckUnaryF("Log2", Log2CasesF, [](float x) { return D::Log2(x); });
	CheckBinaryF("Atan2", Atan2CasesF, [](float y, float x) { return D::Atan2(y, x); });
	CheckBinaryF("Pow", PowCasesF, [](float x, float y) { return D::Pow(x, y); });
	CheckBinaryF("Fmod", FmodCasesF, [](float x, float y) { return D::Fmod(x, y); });
}
