#pragma once

// The single include for glm. GLM_FORCE_DEPTH_ZERO_TO_ONE, GLM_FORCE_RADIANS and GLM_ENABLE_EXPERIMENTAL are
// defined for every consumer of the engine by the Gambit CMake target; SIMD intrinsics are deliberately off so that
// the arithmetic is the same on every platform (D-011). glm's trigonometric and exponential functions call the
// platform libm: simulation code must use Gambit::DMath instead (Math/DMath.h).

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace Gambit
{
	// Splits an affine transform into translation, rotation and scale (T * R * S). A negative determinant is
	// reported as a negative X scale. Returns false, leaving the outputs unchanged, for matrices that are not
	// affine (projective), not finite, or singular (a scale of zero). Pure arithmetic and square roots, so it is
	// deterministic and safe for simulation code.
	[[nodiscard]] bool
	DecomposeTransform(const glm::mat4& transform, glm::vec3& translation, glm::quat& rotation, glm::vec3& scale);

	// The matrix T * R * S. `rotation` must be normalized.
	[[nodiscard]] glm::mat4
	ComposeTransform(const glm::vec3& translation, const glm::quat& rotation, const glm::vec3& scale);

	// Whether two values are equal within a tolerance that is absolute near zero and relative for larger
	// magnitudes: |a - b| <= epsilon * max(1, |a|, |b|). NaN is never equal to anything; equal infinities are.
	[[nodiscard]] bool NearlyEqual(float a, float b, float epsilon = 1e-5f) noexcept;
	[[nodiscard]] bool NearlyEqual(double a, double b, double epsilon = 1e-9) noexcept;
	[[nodiscard]] bool NearlyEqual(const glm::vec2& a, const glm::vec2& b, float epsilon = 1e-5f) noexcept;
	[[nodiscard]] bool NearlyEqual(const glm::vec3& a, const glm::vec3& b, float epsilon = 1e-5f) noexcept;
	[[nodiscard]] bool NearlyEqual(const glm::vec4& a, const glm::vec4& b, float epsilon = 1e-5f) noexcept;
	// Component-wise, so q and -q (the same rotation) are not nearly equal.
	[[nodiscard]] bool NearlyEqual(const glm::quat& a, const glm::quat& b, float epsilon = 1e-5f) noexcept;
	[[nodiscard]] bool NearlyEqual(const glm::mat4& a, const glm::mat4& b, float epsilon = 1e-5f) noexcept;
}
