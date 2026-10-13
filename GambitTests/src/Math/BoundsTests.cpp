#include <doctest/doctest.h>

#include "Gambit/Math/Bounds.h"

#include <array>
#include <vector>

using namespace Gambit;

TEST_CASE("Math: an AABB starts empty and grows around points and boxes")
{
	AABB box;
	CHECK(box.IsEmpty());
	box.Expand(AABB{}); // an empty box adds nothing
	CHECK(box.IsEmpty());
	box.Expand(glm::vec3(1.0f, -2.0f, 3.0f));
	CHECK_FALSE(box.IsEmpty());
	CHECK(box.Min == glm::vec3(1.0f, -2.0f, 3.0f));
	CHECK(box.Max == glm::vec3(1.0f, -2.0f, 3.0f));
	box.Expand(AABB{ glm::vec3(-1.0f), glm::vec3(0.0f) });
	CHECK(box.Min == glm::vec3(-1.0f, -2.0f, -1.0f));
	CHECK(box.Max == glm::vec3(1.0f, 0.0f, 3.0f));
	CHECK(box.GetCenter() == glm::vec3(0.0f, -1.0f, 1.0f));
	CHECK(box.GetExtents() == glm::vec3(1.0f, 1.0f, 2.0f));
	CHECK(box.Contains(glm::vec3(0.0f)));
	CHECK(box.Contains(box.Max));
	CHECK_FALSE(box.Contains(glm::vec3(0.0f, 0.5f, 0.0f)));

	const std::vector<glm::vec3> points = { { 0, 0, 0 }, { 2, 1, -1 }, { -1, 3, 0 } };
	const AABB fromPoints = AABB::FromPoints(points);
	CHECK(fromPoints.Min == glm::vec3(-1.0f, 0.0f, -1.0f));
	CHECK(fromPoints.Max == glm::vec3(2.0f, 3.0f, 0.0f));
	CHECK(AABB::FromPoints({}).IsEmpty());
}

TEST_CASE("Math: a transformed AABB encloses the transformed corners tightly")
{
	const AABB box{ glm::vec3(-1.0f, -2.0f, -3.0f), glm::vec3(1.0f, 2.0f, 3.0f) };
	const glm::mat4 transform = ComposeTransform(
		glm::vec3(10.0f, 0.0f, -5.0f), glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
		glm::vec3(2.0f));
	const AABB moved = box.Transformed(transform);
	// 90 degrees about Y swaps the X and Z extents; the scale doubles them.
	CHECK(NearlyEqual(moved.Min, glm::vec3(4.0f, -4.0f, -7.0f)));
	CHECK(NearlyEqual(moved.Max, glm::vec3(16.0f, 4.0f, -3.0f)));
	for (int corner = 0; corner < 8; ++corner)
	{
		const glm::vec3 local(
			corner & 1 ? box.Max.x : box.Min.x, corner & 2 ? box.Max.y : box.Min.y, corner & 4 ? box.Max.z : box.Min.z);
		const glm::vec3 world(transform * glm::vec4(local, 1.0f));
		CHECK(AABB{ moved.Min - 1e-4f, moved.Max + 1e-4f }.Contains(world));
	}
	CHECK(AABB{}.Transformed(transform).IsEmpty());
}

TEST_CASE("Math: a bounding sphere contains every point")
{
	const std::vector<glm::vec3> points = { { -1, 0, 0 }, { 3, 0, 0 }, { 1, 2, 0 } };
	const BoundingSphere sphere = BoundingSphere::FromPoints(points);
	CHECK(sphere.Center == glm::vec3(1.0f, 1.0f, 0.0f));
	CHECK(sphere.Radius == doctest::Approx(std::sqrt(5.0f)));
	for (const glm::vec3& point : points)
	{
		CHECK(glm::distance(point, sphere.Center) <= sphere.Radius + 1e-5f);
	}
	CHECK(BoundingSphere::FromPoints({}).Radius == 0.0f);
}

TEST_CASE("Math: a frustum from a reversed-depth projection culls what lies outside")
{
	// Looking down -Z from the origin, 90 degrees vertically, square, near 1 and far 100 with reversed depth.
	const glm::mat4 projection = glm::perspectiveRH_ZO(glm::radians(90.0f), 1.0f, 100.0f, 1.0f);
	const Frustum frustum = Frustum::FromViewProjection(projection);
	for (const glm::vec4& plane : frustum.Planes)
	{
		CHECK(glm::length(glm::vec3(plane)) == doctest::Approx(1.0f));
	}

	const auto box = [](const glm::vec3& center, float half) { return AABB{ center - half, center + half }; };
	CHECK(frustum.Intersects(box(glm::vec3(0.0f, 0.0f, -10.0f), 0.5f)));
	CHECK_FALSE(frustum.Intersects(box(glm::vec3(0.0f, 0.0f, 10.0f), 0.5f)));   // behind the eye
	CHECK_FALSE(frustum.Intersects(box(glm::vec3(0.0f, 0.0f, -0.25f), 0.1f)));  // before the near plane
	CHECK_FALSE(frustum.Intersects(box(glm::vec3(0.0f, 0.0f, -150.0f), 1.0f))); // beyond the far plane
	CHECK_FALSE(frustum.Intersects(box(glm::vec3(20.0f, 0.0f, -10.0f), 1.0f)));
	CHECK_FALSE(frustum.Intersects(box(glm::vec3(0.0f, -20.0f, -10.0f), 1.0f)));
	CHECK(frustum.Intersects(box(glm::vec3(10.5f, 0.0f, -10.0f), 1.0f))); // straddles the right plane
	CHECK_FALSE(frustum.Intersects(AABB{}));

	CHECK(frustum.Intersects(glm::vec3(0.0f, 0.0f, -50.0f), 1.0f));
	CHECK(frustum.Intersects(glm::vec3(12.0f, 0.0f, -10.0f), 2.0f));
	CHECK_FALSE(frustum.Intersects(glm::vec3(13.0f, 0.0f, -10.0f), 2.0f)); // 3 / sqrt(2) > 2 from the plane
	CHECK_FALSE(frustum.Intersects(glm::vec3(0.0f, 0.0f, 5.0f), 2.0f));

	// Orthographic views work the same way.
	const Frustum box2 = Frustum::FromViewProjection(glm::orthoRH_ZO(-1.0f, 1.0f, -1.0f, 1.0f, 10.0f, 0.1f));
	CHECK(box2.Intersects(box(glm::vec3(0.0f, 0.0f, -5.0f), 0.1f)));
	CHECK_FALSE(box2.Intersects(box(glm::vec3(1.5f, 0.0f, -5.0f), 0.1f)));
	CHECK_FALSE(box2.Intersects(box(glm::vec3(0.0f, 0.0f, -11.0f), 0.1f)));
}
