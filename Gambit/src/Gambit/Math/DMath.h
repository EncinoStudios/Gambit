#pragma once

#include "Gambit/Math/Math.h"

// Deterministic math for simulation code (D-011). Every function returns bit-identical results on every platform,
// compiler and optimization level, because it is implemented with plain IEEE-754 arithmetic (ports of musl libc's
// libm) instead of calling the platform's libm, whose results differ in the last bit between vendors and versions.
// Simulation code must use these instead of std::sin, std::pow and friends, and instead of glm's wrappers of them
// (glm::sin, glm::rotate, glm::angleAxis, glm::slerp, ...). Rendering, audio and editor code may use std math.
//
// Accuracy: below 1 ulp for the trigonometric functions, about 0.55 ulp for exp, log, log2 and pow; the float
// overloads compute in double and round once. Special values follow C99 (infinities, signed zeros, domain errors),
// except that every NaN result is the same canonical quiet NaN. No function sets errno or the floating-point
// exception flags. sqrt, floor, ceil, trunc and fabs are exactly rounded by IEEE-754, so std::sqrt and friends are
// deterministic too and have no DMath counterpart.

namespace Gambit::DMath
{
	[[nodiscard]] double Sin(double x);
	[[nodiscard]] double Cos(double x);
	[[nodiscard]] double Tan(double x);
	[[nodiscard]] double Asin(double x);
	[[nodiscard]] double Acos(double x);
	[[nodiscard]] double Atan(double x);
	// The angle of the point (x, y) in (-pi, pi].
	[[nodiscard]] double Atan2(double y, double x);
	[[nodiscard]] double Exp(double x);
	// Natural logarithm.
	[[nodiscard]] double Log(double x);
	[[nodiscard]] double Log2(double x);
	[[nodiscard]] double Pow(double x, double y);
	// The remainder of x / y with the sign of x, computed exactly.
	[[nodiscard]] double Fmod(double x, double y);

	[[nodiscard]] float Sin(float x);
	[[nodiscard]] float Cos(float x);
	[[nodiscard]] float Tan(float x);
	[[nodiscard]] float Asin(float x);
	[[nodiscard]] float Acos(float x);
	[[nodiscard]] float Atan(float x);
	[[nodiscard]] float Atan2(float y, float x);
	[[nodiscard]] float Exp(float x);
	[[nodiscard]] float Log(float x);
	[[nodiscard]] float Log2(float x);
	[[nodiscard]] float Pow(float x, float y);
	[[nodiscard]] float Fmod(float x, float y);

	// ---- Vectors and rotations built only from the functions above and exactly rounded arithmetic ----

	// x scaled to unit length; the zero vector (or one that is not finite) is returned unchanged.
	[[nodiscard]] glm::vec3 Normalize(const glm::vec3& x);
	// q scaled to unit length; a zero or non-finite quaternion becomes the identity.
	[[nodiscard]] glm::quat Normalize(const glm::quat& q);

	// The rotation of `angle` radians about `axis` (which need not be normalized); the identity for a zero axis.
	[[nodiscard]] glm::quat AngleAxis(float angle, const glm::vec3& axis);

	// The rotation that applies the angles (radians) about X, then Y, then Z of the world axes: q = qz * qy * qx. This
	// is the order of glm::quat(vec3) and glm::eulerAngleXYZ.
	[[nodiscard]] glm::quat QuatFromEuler(const glm::vec3& eulerAngles);

	// Spherical interpolation along the shortest arc from a to b, for unit quaternions and t in [0, 1]. Falls back to
	// a normalized linear interpolation when a and b are almost equal.
	[[nodiscard]] glm::quat Slerp(const glm::quat& a, const glm::quat& b, float t);
}
