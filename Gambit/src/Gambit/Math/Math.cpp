#include "gbpch.h"

#include "Gambit/Math/Math.h"

#include <cmath>

namespace Gambit
{
	namespace
	{
		template<typename T>
		bool NearlyEqualScalar(T a, T b, T epsilon) noexcept
		{
			if (a == b)
			{
				return true; // also equal infinities
			}
			if (!std::isfinite(a) || !std::isfinite(b))
			{
				return false; // NaN, or an infinity against anything else
			}
			const T scale = std::max({ T(1), std::abs(a), std::abs(b) });
			return std::abs(a - b) <= epsilon * scale;
		}

		bool IsFinite(const glm::vec3& v) noexcept
		{
			return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
		}
	}

	bool DecomposeTransform(const glm::mat4& transform, glm::vec3& translation, glm::quat& rotation, glm::vec3& scale)
	{
		// An affine transform has the bottom row (0, 0, 0, 1).
		if (transform[0][3] != 0.0f || transform[1][3] != 0.0f || transform[2][3] != 0.0f || transform[3][3] != 1.0f)
		{
			return false;
		}

		const glm::vec3 column0(transform[0]);
		const glm::vec3 column1(transform[1]);
		const glm::vec3 column2(transform[2]);
		glm::vec3 decomposedScale(glm::length(column0), glm::length(column1), glm::length(column2));
		const glm::vec3 position(transform[3]);
		if (!IsFinite(decomposedScale) || !IsFinite(position) || decomposedScale.x == 0.0f || decomposedScale.y == 0.0f
		    || decomposedScale.z == 0.0f)
		{
			return false;
		}

		glm::mat3 axes(column0 / decomposedScale.x, column1 / decomposedScale.y, column2 / decomposedScale.z);
		if (glm::dot(glm::cross(axes[0], axes[1]), axes[2]) < 0.0f)
		{
			// A mirroring transform: move the reflection into the scale so that the rotation stays proper.
			decomposedScale.x = -decomposedScale.x;
			axes[0] = -axes[0];
		}

		translation = position;
		rotation = glm::normalize(glm::quat_cast(axes));
		scale = decomposedScale;
		return true;
	}

	glm::mat4 ComposeTransform(const glm::vec3& translation, const glm::quat& rotation, const glm::vec3& scale)
	{
		const glm::mat3 axes = glm::mat3_cast(rotation);
		return glm::mat4(
			glm::vec4(axes[0] * scale.x, 0.0f), glm::vec4(axes[1] * scale.y, 0.0f), glm::vec4(axes[2] * scale.z, 0.0f),
			glm::vec4(translation, 1.0f));
	}

	bool NearlyEqual(float a, float b, float epsilon) noexcept
	{
		return NearlyEqualScalar(a, b, epsilon);
	}

	bool NearlyEqual(double a, double b, double epsilon) noexcept
	{
		return NearlyEqualScalar(a, b, epsilon);
	}

	bool NearlyEqual(const glm::vec2& a, const glm::vec2& b, float epsilon) noexcept
	{
		return NearlyEqual(a.x, b.x, epsilon) && NearlyEqual(a.y, b.y, epsilon);
	}

	bool NearlyEqual(const glm::vec3& a, const glm::vec3& b, float epsilon) noexcept
	{
		return NearlyEqual(a.x, b.x, epsilon) && NearlyEqual(a.y, b.y, epsilon) && NearlyEqual(a.z, b.z, epsilon);
	}

	bool NearlyEqual(const glm::vec4& a, const glm::vec4& b, float epsilon) noexcept
	{
		return NearlyEqual(a.x, b.x, epsilon) && NearlyEqual(a.y, b.y, epsilon) && NearlyEqual(a.z, b.z, epsilon)
			&& NearlyEqual(a.w, b.w, epsilon);
	}

	bool NearlyEqual(const glm::quat& a, const glm::quat& b, float epsilon) noexcept
	{
		return NearlyEqual(a.x, b.x, epsilon) && NearlyEqual(a.y, b.y, epsilon) && NearlyEqual(a.z, b.z, epsilon)
			&& NearlyEqual(a.w, b.w, epsilon);
	}

	bool NearlyEqual(const glm::mat4& a, const glm::mat4& b, float epsilon) noexcept
	{
		return NearlyEqual(a[0], b[0], epsilon) && NearlyEqual(a[1], b[1], epsilon) && NearlyEqual(a[2], b[2], epsilon)
			&& NearlyEqual(a[3], b[3], epsilon);
	}
}
