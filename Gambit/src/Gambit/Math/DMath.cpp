#include "gbpch.h"

#include "Gambit/Math/DMath.h"

#include <cmath>

namespace Gambit::DMath
{
	namespace
	{
		// Quaternions closer than this (cos of half the angle between them) are interpolated linearly, because the
		// sine of the angle in the denominator of slerp would be too small to divide by.
		constexpr float SlerpLinearThreshold = 0.9995f;
	}

	float Sin(float x)
	{
		return static_cast<float>(Sin(static_cast<double>(x)));
	}

	float Cos(float x)
	{
		return static_cast<float>(Cos(static_cast<double>(x)));
	}

	float Tan(float x)
	{
		return static_cast<float>(Tan(static_cast<double>(x)));
	}

	float Asin(float x)
	{
		return static_cast<float>(Asin(static_cast<double>(x)));
	}

	float Acos(float x)
	{
		return static_cast<float>(Acos(static_cast<double>(x)));
	}

	float Atan(float x)
	{
		return static_cast<float>(Atan(static_cast<double>(x)));
	}

	float Atan2(float y, float x)
	{
		return static_cast<float>(Atan2(static_cast<double>(y), static_cast<double>(x)));
	}

	float Exp(float x)
	{
		return static_cast<float>(Exp(static_cast<double>(x)));
	}

	float Log(float x)
	{
		return static_cast<float>(Log(static_cast<double>(x)));
	}

	float Log2(float x)
	{
		return static_cast<float>(Log2(static_cast<double>(x)));
	}

	float Pow(float x, float y)
	{
		return static_cast<float>(Pow(static_cast<double>(x), static_cast<double>(y)));
	}

	float Fmod(float x, float y)
	{
		return static_cast<float>(Fmod(static_cast<double>(x), static_cast<double>(y)));
	}

	glm::vec3 Normalize(const glm::vec3& x)
	{
		const float lengthSquared = glm::dot(x, x);
		if (!(lengthSquared > 0.0f) || !std::isfinite(lengthSquared))
		{
			return x;
		}
		return x / std::sqrt(lengthSquared);
	}

	glm::quat Normalize(const glm::quat& q)
	{
		const float lengthSquared = q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z;
		if (!(lengthSquared > 0.0f) || !std::isfinite(lengthSquared))
		{
			return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		}
		const float length = std::sqrt(lengthSquared);
		return glm::quat(q.w / length, q.x / length, q.y / length, q.z / length);
	}

	glm::quat AngleAxis(float angle, const glm::vec3& axis)
	{
		const glm::vec3 unitAxis = Normalize(axis);
		if (unitAxis == glm::vec3(0.0f))
		{
			return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		}
		const float halfAngle = angle * 0.5f;
		const float sine = Sin(halfAngle);
		return glm::quat(Cos(halfAngle), unitAxis.x * sine, unitAxis.y * sine, unitAxis.z * sine);
	}

	glm::quat QuatFromEuler(const glm::vec3& eulerAngles)
	{
		const glm::vec3 half = eulerAngles * 0.5f;
		const float cx = Cos(half.x);
		const float sx = Sin(half.x);
		const float cy = Cos(half.y);
		const float sy = Sin(half.y);
		const float cz = Cos(half.z);
		const float sz = Sin(half.z);
		return glm::quat(
			cx * cy * cz + sx * sy * sz, sx * cy * cz - cx * sy * sz, cx * sy * cz + sx * cy * sz,
			cx * cy * sz - sx * sy * cz);
	}

	glm::quat Slerp(const glm::quat& a, const glm::quat& b, float t)
	{
		float cosine = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
		glm::quat end = b;
		if (cosine < 0.0f)
		{
			// q and -q are the same rotation: take the short way around.
			end = glm::quat(-b.w, -b.x, -b.y, -b.z);
			cosine = -cosine;
		}

		if (cosine > SlerpLinearThreshold)
		{
			const float s = 1.0f - t;
			return Normalize(
				glm::quat(a.w * s + end.w * t, a.x * s + end.x * t, a.y * s + end.y * t, a.z * s + end.z * t));
		}

		const float angle = Acos(cosine);
		const float sine = Sin(angle);
		const float fromWeight = Sin((1.0f - t) * angle) / sine;
		const float toWeight = Sin(t * angle) / sine;
		return glm::quat(
			a.w * fromWeight + end.w * toWeight, a.x * fromWeight + end.x * toWeight,
			a.y * fromWeight + end.y * toWeight, a.z * fromWeight + end.z * toWeight);
	}
}
