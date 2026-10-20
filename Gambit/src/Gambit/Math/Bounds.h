#pragma once

#include "Gambit/Math/Math.h"

#include <array>
#include <limits>
#include <span>

namespace Gambit
{
	// An axis-aligned bounding box. The default box is empty (Min > Max) and grows with Expand.
	struct AABB
	{
		glm::vec3 Min{ std::numeric_limits<float>::max() };
		glm::vec3 Max{ std::numeric_limits<float>::lowest() };

		[[nodiscard]] static AABB FromPoints(std::span<const glm::vec3> points) noexcept;

		[[nodiscard]] bool IsEmpty() const noexcept { return Min.x > Max.x || Min.y > Max.y || Min.z > Max.z; }
		[[nodiscard]] glm::vec3 GetCenter() const noexcept { return (Min + Max) * 0.5f; }
		// Half the size along each axis.
		[[nodiscard]] glm::vec3 GetExtents() const noexcept { return (Max - Min) * 0.5f; }

		void Expand(const glm::vec3& point) noexcept;
		void Expand(const AABB& other) noexcept;
		[[nodiscard]] bool Contains(const glm::vec3& point) const noexcept;
		// The box around this box transformed by `transform` (an affine matrix); empty stays empty.
		[[nodiscard]] AABB Transformed(const glm::mat4& transform) const noexcept;

		friend bool operator==(const AABB&, const AABB&) noexcept = default;
	};

	// A sphere that contains a set of points (not necessarily the smallest one).
	struct BoundingSphere
	{
		glm::vec3 Center{ 0.0f };
		float Radius = 0.0f;

		// Centered on the box of the points, with the distance to the farthest point as radius.
		[[nodiscard]] static BoundingSphere FromPoints(std::span<const glm::vec3> points) noexcept;

		friend bool operator==(const BoundingSphere&, const BoundingSphere&) noexcept = default;
	};

	// The volume that a view-projection matrix maps into the clip volume (x and y in [-w, w], depth in [0, w], in
	// either depth direction): six planes facing inwards, normalized, in the order left, right, bottom, top, depth 0,
	// depth w. A point p is inside a plane when dot(plane.xyz, p) + plane.w >= 0.
	struct Frustum
	{
		std::array<glm::vec4, 6> Planes{};

		[[nodiscard]] static Frustum FromViewProjection(const glm::mat4& viewProjection) noexcept;

		// Conservative tests for culling: false only when the shape is entirely outside one plane (shapes near the
		// frustum's edges may pass while outside). Empty boxes are outside.
		[[nodiscard]] bool Intersects(const AABB& box) const noexcept;
		[[nodiscard]] bool Intersects(const glm::vec3& center, float radius) const noexcept;
	};
}
