#include "gbpch.h"

#include "Gambit/Math/Bounds.h"

namespace Gambit
{
	AABB AABB::FromPoints(std::span<const glm::vec3> points) noexcept
	{
		AABB box;
		for (const glm::vec3& point : points)
		{
			box.Expand(point);
		}
		return box;
	}

	void AABB::Expand(const glm::vec3& point) noexcept
	{
		Min = glm::min(Min, point);
		Max = glm::max(Max, point);
	}

	void AABB::Expand(const AABB& other) noexcept
	{
		if (!other.IsEmpty())
		{
			Min = glm::min(Min, other.Min);
			Max = glm::max(Max, other.Max);
		}
	}

	bool AABB::Contains(const glm::vec3& point) const noexcept
	{
		return glm::all(glm::greaterThanEqual(point, Min)) && glm::all(glm::lessThanEqual(point, Max));
	}

	AABB AABB::Transformed(const glm::mat4& transform) const noexcept
	{
		if (IsEmpty())
		{
			return *this;
		}
		// Arvo's method: the extents along each world axis are sums of the absolute rotated extents.
		const glm::vec3 center = glm::vec3(transform * glm::vec4(GetCenter(), 1.0f));
		const glm::vec3 extents = GetExtents();
		glm::vec3 worldExtents{ 0.0f };
		for (int column = 0; column < 3; ++column)
		{
			worldExtents += glm::abs(glm::vec3(transform[column])) * extents[column];
		}
		return AABB{ center - worldExtents, center + worldExtents };
	}

	BoundingSphere BoundingSphere::FromPoints(std::span<const glm::vec3> points) noexcept
	{
		const AABB box = AABB::FromPoints(points);
		if (box.IsEmpty())
		{
			return {};
		}
		BoundingSphere sphere;
		sphere.Center = box.GetCenter();
		float radiusSquared = 0.0f;
		for (const glm::vec3& point : points)
		{
			const glm::vec3 offset = point - sphere.Center;
			radiusSquared = std::max(radiusSquared, glm::dot(offset, offset));
		}
		sphere.Radius = std::sqrt(radiusSquared);
		return sphere;
	}

	Frustum Frustum::FromViewProjection(const glm::mat4& viewProjection) noexcept
	{
		// Gribb and Hartmann: each plane is a sum or difference of rows of the matrix (glm stores columns).
		const auto row = [&](int index) {
			return glm::vec4(
				viewProjection[0][index], viewProjection[1][index], viewProjection[2][index], viewProjection[3][index]);
		};
		Frustum frustum;
		frustum.Planes = {
			row(3) + row(0), row(3) - row(0), row(3) + row(1), row(3) - row(1), row(2), row(3) - row(2)
		};
		for (glm::vec4& plane : frustum.Planes)
		{
			const float length = glm::length(glm::vec3(plane));
			if (length > 0.0f)
			{
				plane /= length;
			}
		}
		return frustum;
	}

	bool Frustum::Intersects(const AABB& box) const noexcept
	{
		if (box.IsEmpty())
		{
			return false;
		}
		const glm::vec3 center = box.GetCenter();
		const glm::vec3 extents = box.GetExtents();
		for (const glm::vec4& plane : Planes)
		{
			const glm::vec3 normal(plane);
			// The distance of the box's corner farthest along the normal.
			if (glm::dot(normal, center) + glm::dot(glm::abs(normal), extents) + plane.w < 0.0f)
			{
				return false;
			}
		}
		return true;
	}

	bool Frustum::Intersects(const glm::vec3& center, float radius) const noexcept
	{
		return std::ranges::all_of(
			Planes, [&](const glm::vec4& plane) { return glm::dot(glm::vec3(plane), center) + plane.w >= -radius; });
	}
}
