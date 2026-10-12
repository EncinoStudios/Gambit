#include <doctest/doctest.h>

#include "Gambit/Math/Math.h"

#include <cmath>
#include <limits>

#if !defined(GLM_FORCE_DEPTH_ZERO_TO_ONE) || !defined(GLM_FORCE_RADIANS) || !defined(GLM_ENABLE_EXPERIMENTAL)
#	error "the Gambit target must define the glm configuration macros for every consumer"
#endif
#if defined(GLM_FORCE_INTRINSICS) || defined(GLM_FORCE_SSE2) || defined(GLM_FORCE_AVX)
#	error "SIMD intrinsics must stay off: simulation arithmetic has to be identical on every platform"
#endif

using namespace Gambit;

namespace
{
	constexpr float Infinity = std::numeric_limits<float>::infinity();
	constexpr float NaN = std::numeric_limits<float>::quiet_NaN();

	// Whether two quaternions describe the same rotation (q and -q do).
	bool SameRotation(const glm::quat& a, const glm::quat& b)
	{
		return std::abs(glm::dot(a, b)) > 1.0f - 1e-5f;
	}
}

TEST_CASE("Math: glm uses the Vulkan depth range and radians")
{
	const glm::mat4 projection = glm::perspective(glm::radians(60.0f), 1.0f, 0.5f, 100.0f);
	const glm::vec4 nearPoint = projection * glm::vec4(0.0f, 0.0f, -0.5f, 1.0f);
	const glm::vec4 farPoint = projection * glm::vec4(0.0f, 0.0f, -100.0f, 1.0f);
	CHECK(nearPoint.z / nearPoint.w == doctest::Approx(0.0f).scale(1.0));
	CHECK(farPoint.z / farPoint.w == doctest::Approx(1.0f));
	CHECK(glm::radians(180.0f) == doctest::Approx(glm::pi<float>()));
}

TEST_CASE("Math: ComposeTransform builds T * R * S")
{
	const glm::vec3 translation(1.0f, -2.0f, 3.0f);
	const glm::quat rotation = glm::angleAxis(0.7f, glm::normalize(glm::vec3(1.0f, 2.0f, 3.0f)));
	const glm::vec3 scale(2.0f, 0.5f, 3.0f);

	const glm::mat4 composed = ComposeTransform(translation, rotation, scale);
	const glm::mat4 reference =
		glm::translate(glm::mat4(1.0f), translation) * glm::mat4_cast(rotation) * glm::scale(glm::mat4(1.0f), scale);
	CHECK(NearlyEqual(composed, reference));

	const glm::vec4 point = composed * glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	const glm::vec4 expected = reference * glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
	CHECK(NearlyEqual(point, expected));
	CHECK(composed[3][3] == 1.0f);
	CHECK(composed[0][3] == 0.0f);

	CHECK(ComposeTransform(glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f)) == glm::mat4(1.0f));
}

TEST_CASE("Math: DecomposeTransform recovers translation, rotation and scale")
{
	struct Case
	{
		glm::vec3 Translation;
		glm::vec3 Axis;
		float Angle;
		glm::vec3 Scale;
	};
	const Case cases[] = {
		{ { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, 0.0f, { 1.0f, 1.0f, 1.0f } },
		{ { 1.0f, 2.0f, 3.0f }, { 0.0f, 1.0f, 0.0f }, 1.0f, { 1.0f, 1.0f, 1.0f } },
		{ { -5.0f, 0.25f, 100.0f }, { 1.0f, 1.0f, 0.0f }, 2.5f, { 2.0f, 3.0f, 0.5f } },
		{ { 0.0f, 0.0f, -1.0f }, { 0.0f, 0.0f, 1.0f }, 3.1f, { 10.0f, 10.0f, 10.0f } },
		{ { 7.0f, 7.0f, 7.0f }, { 1.0f, 2.0f, 3.0f }, -2.0f, { 0.01f, 0.02f, 0.03f } },
		{ { 1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, 3.14159265f, { 1.0f, 2.0f, 3.0f } },
	};
	for (const Case& c : cases)
	{
		const glm::quat rotation = glm::angleAxis(c.Angle, glm::normalize(c.Axis));
		const glm::mat4 matrix = ComposeTransform(c.Translation, rotation, c.Scale);

		glm::vec3 translation(0.0f);
		glm::quat decomposedRotation(1.0f, 0.0f, 0.0f, 0.0f);
		glm::vec3 scale(0.0f);
		REQUIRE(DecomposeTransform(matrix, translation, decomposedRotation, scale));
		CHECK(NearlyEqual(translation, c.Translation, 1e-5f));
		CHECK(NearlyEqual(scale, c.Scale, 1e-5f));
		CHECK(SameRotation(decomposedRotation, rotation));
		CHECK(glm::length(decomposedRotation) == doctest::Approx(1.0f));
		CHECK(NearlyEqual(ComposeTransform(translation, decomposedRotation, scale), matrix, 1e-4f));
	}
}

TEST_CASE("Math: DecomposeTransform moves a mirroring into the X scale")
{
	const glm::quat rotation = glm::angleAxis(0.9f, glm::normalize(glm::vec3(0.0f, 1.0f, 1.0f)));
	const glm::mat4 mirrored = ComposeTransform(glm::vec3(1.0f, 2.0f, 3.0f), rotation, glm::vec3(1.0f, -2.0f, 3.0f));

	glm::vec3 translation;
	glm::quat decomposedRotation;
	glm::vec3 scale;
	REQUIRE(DecomposeTransform(mirrored, translation, decomposedRotation, scale));
	CHECK(scale.x < 0.0f);
	CHECK(scale.y > 0.0f);
	CHECK(scale.z > 0.0f);
	CHECK(glm::length(decomposedRotation) == doctest::Approx(1.0f));
	CHECK(NearlyEqual(ComposeTransform(translation, decomposedRotation, scale), mirrored, 1e-4f));
}

TEST_CASE("Math: DecomposeTransform rejects matrices that are not proper affine transforms")
{
	const glm::vec3 translation0(1.0f, 2.0f, 3.0f);
	const glm::quat rotation0(0.0f, 1.0f, 0.0f, 0.0f);
	const glm::vec3 scale0(4.0f, 5.0f, 6.0f);

	glm::mat4 projective = glm::perspective(1.0f, 1.0f, 0.1f, 10.0f);
	glm::mat4 singular =
		ComposeTransform(glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 0.0f, 1.0f));
	glm::mat4 withNaN = glm::mat4(1.0f);
	withNaN[3][1] = NaN;
	glm::mat4 withInfinity = glm::mat4(1.0f);
	withInfinity[0][0] = Infinity;
	glm::mat4 notNormalized = glm::mat4(1.0f);
	notNormalized[3][3] = 2.0f;
	glm::mat4 shearedBottomRow = glm::mat4(1.0f);
	shearedBottomRow[1][3] = 0.5f;

	for (const glm::mat4& matrix : { projective, singular, withNaN, withInfinity, notNormalized, shearedBottomRow })
	{
		glm::vec3 translation = translation0;
		glm::quat rotation = rotation0;
		glm::vec3 scale = scale0;
		CHECK_FALSE(DecomposeTransform(matrix, translation, rotation, scale));
		// The outputs are left alone on failure.
		CHECK(translation == translation0);
		CHECK(rotation == rotation0);
		CHECK(scale == scale0);
	}
}

TEST_CASE("Math: DecomposeTransform of the identity")
{
	glm::vec3 translation(9.0f);
	glm::quat rotation(0.0f, 1.0f, 0.0f, 0.0f);
	glm::vec3 scale(9.0f);
	REQUIRE(DecomposeTransform(glm::mat4(1.0f), translation, rotation, scale));
	CHECK(translation == glm::vec3(0.0f));
	CHECK(scale == glm::vec3(1.0f));
	CHECK(rotation == glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
}

TEST_CASE("Math: NearlyEqual compares with a tolerance that grows with the magnitude")
{
	CHECK(NearlyEqual(1.0f, 1.0f));
	CHECK(NearlyEqual(1.0f, 1.000001f));
	CHECK_FALSE(NearlyEqual(1.0f, 1.001f));
	CHECK(NearlyEqual(0.0f, 1e-6f));
	CHECK_FALSE(NearlyEqual(0.0f, 1e-3f));
	CHECK(NearlyEqual(100000.0f, 100000.5f)); // relative for large values
	CHECK_FALSE(NearlyEqual(100000.0f, 100010.0f));
	CHECK(NearlyEqual(-3.0f, -3.00001f));
	CHECK(NearlyEqual(0.0f, -0.0f));
	CHECK(NearlyEqual(1.0f, 1.1f, 0.2f));

	CHECK(NearlyEqual(Infinity, Infinity));
	CHECK_FALSE(NearlyEqual(Infinity, -Infinity));
	CHECK_FALSE(NearlyEqual(Infinity, 1.0f));
	CHECK_FALSE(NearlyEqual(NaN, NaN));
	CHECK_FALSE(NearlyEqual(NaN, 1.0f));

	CHECK(NearlyEqual(1.0, 1.0 + 1e-12));
	CHECK_FALSE(NearlyEqual(1.0, 1.0 + 1e-6));
	CHECK(NearlyEqual(1.0, 1.0 + 1e-6, 1e-5));
}

TEST_CASE("Math: NearlyEqual for vectors, quaternions and matrices checks every component")
{
	CHECK(NearlyEqual(glm::vec2(1.0f, 2.0f), glm::vec2(1.0f, 2.000001f)));
	CHECK_FALSE(NearlyEqual(glm::vec2(1.0f, 2.0f), glm::vec2(1.0f, 2.1f)));
	CHECK(NearlyEqual(glm::vec3(1.0f, 2.0f, 3.0f), glm::vec3(1.0f, 2.0f, 3.000001f)));
	CHECK_FALSE(NearlyEqual(glm::vec3(1.0f, 2.0f, 3.0f), glm::vec3(1.0f, 2.0f, 3.1f)));
	CHECK(NearlyEqual(glm::vec4(1.0f), glm::vec4(1.0f, 1.0f, 1.0f, 1.000001f)));
	CHECK_FALSE(NearlyEqual(glm::vec4(1.0f), glm::vec4(1.0f, 1.0f, 1.0f, 1.1f)));
	CHECK_FALSE(NearlyEqual(glm::vec3(1.0f), glm::vec3(1.0f, NaN, 1.0f)));

	const glm::quat q = glm::angleAxis(1.0f, glm::vec3(0.0f, 0.0f, 1.0f));
	CHECK(NearlyEqual(q, q));
	CHECK_FALSE(NearlyEqual(q, -q)); // component-wise
	CHECK_FALSE(NearlyEqual(q, glm::quat(1.0f, 0.0f, 0.0f, 0.0f)));

	glm::mat4 a(1.0f);
	glm::mat4 b(1.0f);
	CHECK(NearlyEqual(a, b));
	b[2][1] = 0.5f;
	CHECK_FALSE(NearlyEqual(a, b));
}
