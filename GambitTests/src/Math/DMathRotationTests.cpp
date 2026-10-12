#include <doctest/doctest.h>

#include "Gambit/Core/Hash.h"
#include "Gambit/Math/DMath.h"

#include <cmath>
#include <limits>
#include <vector>

namespace D = Gambit::DMath;
using Gambit::NearlyEqual;

namespace
{
	constexpr float HalfPi = 1.57079632679f;
	constexpr float NaN = std::numeric_limits<float>::quiet_NaN();
	const glm::quat Identity(1.0f, 0.0f, 0.0f, 0.0f);

	float Length(const glm::quat& q)
	{
		return std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
	}

	bool SameRotation(const glm::quat& a, const glm::quat& b, float tolerance = 1e-5f)
	{
		return std::abs(glm::dot(a, b)) > 1.0f - tolerance;
	}

	glm::vec3 Rotate(const glm::quat& q, const glm::vec3& v)
	{
		return q * v;
	}

	std::vector<glm::vec3> SampleAngles()
	{
		return { { 0.0f, 0.0f, 0.0f },   { 0.1f, 0.2f, 0.3f },   { 1.0f, -0.5f, 2.0f },  { -3.0f, 0.7f, 1.2f },
			     { HalfPi, 0.0f, 0.0f }, { 0.0f, HalfPi, 0.0f }, { 0.0f, 0.0f, HalfPi }, { 3.0f, 3.0f, 3.0f },
			     { -6.0f, 6.0f, 0.01f }, { 0.5f, 1.4f, -2.2f } };
	}
}

TEST_CASE("Math: DMath AngleAxis builds a unit rotation about the axis")
{
	const glm::quat quarterTurn = D::AngleAxis(HalfPi, glm::vec3(0.0f, 0.0f, 1.0f));
	CHECK(NearlyEqual(Rotate(quarterTurn, glm::vec3(1.0f, 0.0f, 0.0f)), glm::vec3(0.0f, 1.0f, 0.0f), 1e-6f));
	CHECK(NearlyEqual(Rotate(quarterTurn, glm::vec3(0.0f, 1.0f, 0.0f)), glm::vec3(-1.0f, 0.0f, 0.0f), 1e-6f));
	CHECK(NearlyEqual(Rotate(quarterTurn, glm::vec3(0.0f, 0.0f, 1.0f)), glm::vec3(0.0f, 0.0f, 1.0f), 1e-6f));

	for (const glm::vec3& axis :
	     { glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(1.0f, 2.0f, 3.0f), glm::vec3(-0.3f, 0.9f, 0.1f) })
	{
		for (const float angle : { 0.0f, 0.1f, 1.0f, 3.0f, -2.0f, 6.0f })
		{
			const glm::quat mine = D::AngleAxis(angle, axis);
			CHECK(Length(mine) == doctest::Approx(1.0f).epsilon(1e-6));
			CHECK(NearlyEqual(mine, glm::angleAxis(angle, glm::normalize(axis)), 1e-6f));
		}
	}
}

TEST_CASE("Math: DMath AngleAxis ignores the length of the axis and survives a zero axis")
{
	CHECK(
		NearlyEqual(D::AngleAxis(1.0f, glm::vec3(0.0f, 5.0f, 0.0f)), D::AngleAxis(1.0f, glm::vec3(0.0f, 1.0f, 0.0f))));
	CHECK(D::AngleAxis(1.0f, glm::vec3(0.0f)) == Identity);
	CHECK(D::AngleAxis(0.0f, glm::vec3(1.0f, 0.0f, 0.0f)) == Identity);
}

TEST_CASE("Math: DMath QuatFromEuler matches glm and applies X, then Y, then Z")
{
	CHECK(D::QuatFromEuler(glm::vec3(0.0f)) == Identity);

	for (const glm::vec3& angles : SampleAngles())
	{
		const glm::quat mine = D::QuatFromEuler(angles);
		CHECK(Length(mine) == doctest::Approx(1.0f).epsilon(1e-6));
		CHECK(NearlyEqual(mine, glm::quat(angles), 2e-6f));
	}

	const glm::vec3 angles(0.4f, -1.1f, 2.3f);
	const glm::quat composed = D::AngleAxis(angles.z, glm::vec3(0, 0, 1)) * D::AngleAxis(angles.y, glm::vec3(0, 1, 0))
		* D::AngleAxis(angles.x, glm::vec3(1, 0, 0));
	CHECK(SameRotation(D::QuatFromEuler(angles), composed, 1e-6f));

	// X first: a quarter turn about X takes +Y to +Z, then a quarter turn about Z takes +Z to itself.
	const glm::quat xThenZ = D::QuatFromEuler(glm::vec3(HalfPi, 0.0f, HalfPi));
	CHECK(NearlyEqual(Rotate(xThenZ, glm::vec3(0.0f, 1.0f, 0.0f)), glm::vec3(0.0f, 0.0f, 1.0f), 1e-6f));
	CHECK(NearlyEqual(Rotate(xThenZ, glm::vec3(1.0f, 0.0f, 0.0f)), glm::vec3(0.0f, 1.0f, 0.0f), 1e-6f));
}

TEST_CASE("Math: DMath Normalize scales vectors and quaternions to unit length")
{
	CHECK(NearlyEqual(D::Normalize(glm::vec3(3.0f, 0.0f, 4.0f)), glm::vec3(0.6f, 0.0f, 0.8f), 1e-7f));
	CHECK(D::Normalize(glm::vec3(0.0f)) == glm::vec3(0.0f));
	CHECK(D::Normalize(glm::vec3(0.0f, -2.0f, 0.0f)) == glm::vec3(0.0f, -1.0f, 0.0f));
	const glm::vec3 invalid = D::Normalize(glm::vec3(NaN, 1.0f, 0.0f));
	CHECK(std::isnan(invalid.x));
	CHECK(invalid.y == 1.0f); // returned unchanged

	const glm::quat scaled = D::Normalize(glm::quat(2.0f, 0.0f, 0.0f, 0.0f));
	CHECK(scaled == Identity);
	CHECK(Length(D::Normalize(glm::quat(1.0f, 2.0f, 3.0f, 4.0f))) == doctest::Approx(1.0f).epsilon(1e-6));
	CHECK(D::Normalize(glm::quat(0.0f, 0.0f, 0.0f, 0.0f)) == Identity);
	CHECK(D::Normalize(glm::quat(NaN, 0.0f, 0.0f, 0.0f)) == Identity);
	CHECK(D::Normalize(glm::quat(std::numeric_limits<float>::infinity(), 0.0f, 0.0f, 0.0f)) == Identity);
}

TEST_CASE("Math: DMath Slerp interpolates along the shortest arc")
{
	const glm::quat a = Identity;
	const glm::quat b = D::AngleAxis(HalfPi, glm::vec3(0.0f, 0.0f, 1.0f));

	CHECK(NearlyEqual(D::Slerp(a, b, 0.0f), a, 1e-6f));
	CHECK(NearlyEqual(D::Slerp(a, b, 1.0f), b, 1e-6f));
	CHECK(NearlyEqual(D::Slerp(a, b, 0.5f), D::AngleAxis(HalfPi * 0.5f, glm::vec3(0.0f, 0.0f, 1.0f)), 1e-6f));
	CHECK(NearlyEqual(D::Slerp(a, b, 0.25f), D::AngleAxis(HalfPi * 0.25f, glm::vec3(0.0f, 0.0f, 1.0f)), 1e-6f));

	// -b is the same rotation as b: the path must not take the long way around.
	const glm::quat negated(-b.w, -b.x, -b.y, -b.z);
	CHECK(SameRotation(D::Slerp(a, negated, 0.5f), D::AngleAxis(HalfPi * 0.5f, glm::vec3(0.0f, 0.0f, 1.0f)), 1e-6f));
	CHECK(D::Slerp(a, negated, 0.5f).w > 0.9f);
}

TEST_CASE("Math: DMath Slerp keeps unit length and agrees with glm")
{
	const std::vector<glm::vec3> angles = SampleAngles();
	for (size_t i = 0; i < angles.size(); ++i)
	{
		const glm::quat a = D::QuatFromEuler(angles[i]);
		const glm::quat b = D::QuatFromEuler(angles[(i + 3) % angles.size()]);
		for (const float t : { 0.0f, 0.1f, 0.5f, 0.9f, 1.0f })
		{
			const glm::quat mine = D::Slerp(a, b, t);
			CHECK(Length(mine) == doctest::Approx(1.0f).epsilon(1e-5));
			CHECK(SameRotation(mine, glm::slerp(a, b, t), 1e-5f));
		}
	}
}

TEST_CASE("Math: DMath Slerp of nearly equal quaternions falls back to a normalized lerp")
{
	const glm::quat a = D::AngleAxis(1.0f, glm::vec3(0.0f, 1.0f, 0.0f));
	const glm::quat b = D::AngleAxis(1.00001f, glm::vec3(0.0f, 1.0f, 0.0f));
	const glm::quat middle = D::Slerp(a, b, 0.5f);
	CHECK(Length(middle) == doctest::Approx(1.0f).epsilon(1e-6));
	CHECK(SameRotation(middle, D::AngleAxis(1.000005f, glm::vec3(0.0f, 1.0f, 0.0f)), 1e-6f));

	const glm::quat same = D::Slerp(a, a, 0.7f);
	CHECK(NearlyEqual(same, a, 1e-6f));
	const glm::quat opposite = D::Slerp(a, glm::quat(-a.w, -a.x, -a.y, -a.z), 0.3f);
	CHECK(NearlyEqual(opposite, a, 1e-6f));
}

TEST_CASE("Math: DMath rotation helpers produce platform-independent bits")
{
	// A regression guard for the float arithmetic around DMath: these exact bits must come out of every compiler.
	Gambit::Hasher hasher;
	const auto feed = [&hasher](const glm::quat& q) {
		hasher.UpdateValue(q.w);
		hasher.UpdateValue(q.x);
		hasher.UpdateValue(q.y);
		hasher.UpdateValue(q.z);
	};
	for (const glm::vec3& angles : SampleAngles())
	{
		feed(D::QuatFromEuler(angles));
		feed(D::AngleAxis(angles.x + 0.25f, angles + glm::vec3(0.1f)));
	}
	feed(D::Slerp(D::QuatFromEuler({ 0.3f, 1.2f, -0.4f }), D::QuatFromEuler({ -1.0f, 0.2f, 2.5f }), 0.37f));
	feed(D::Normalize(glm::quat(0.3f, -1.2f, 0.4f, 2.2f)));
	const glm::vec3 unit = D::Normalize(glm::vec3(0.3f, -1.2f, 4.0f));
	hasher.UpdateValue(unit.x);
	hasher.UpdateValue(unit.y);
	hasher.UpdateValue(unit.z);
	CHECK(hasher.Digest() == 0xC1696E94C88F5306ull);
}
