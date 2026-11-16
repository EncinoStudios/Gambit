#include <doctest/doctest.h>

#include "Scripting/ScriptTestSupport.h"

#include "Gambit/Math/DMath.h"

using namespace Gambit;
using namespace Gambit::Tests;

TEST_CASE("Scripting: vectors have components and operators")
{
	ScriptFixture fixture;
	fixture.Create();
	CHECK(fixture.Eval("return Vec2(1, 2) + Vec2(3, 4)") == ScriptValue(glm::vec2(4.0f, 6.0f)));
	CHECK(fixture.Eval("return Vec3(1, 2, 3) * 2") == ScriptValue(glm::vec3(2.0f, 4.0f, 6.0f)));
	CHECK(fixture.Eval("return 2 * Vec3(1, 2, 3)") == ScriptValue(glm::vec3(2.0f, 4.0f, 6.0f)));
	CHECK(fixture.Eval("return Vec3(1, 2, 3) * Vec3(2, 2, 2)") == ScriptValue(glm::vec3(2.0f, 4.0f, 6.0f)));
	CHECK(fixture.Eval("return Vec3(2, 4, 6) / 2") == ScriptValue(glm::vec3(1.0f, 2.0f, 3.0f)));
	CHECK(fixture.Eval("return -Vec4(1, 2, 3, 4)") == ScriptValue(glm::vec4(-1.0f, -2.0f, -3.0f, -4.0f)));
	CHECK(fixture.Eval("return Vec3() == Vec3(0, 0, 0)") == ScriptValue(true));
	CHECK(fixture.Eval("local v = Vec3(1, 2, 3) v.y = 5 return v") == ScriptValue(glm::vec3(1.0f, 5.0f, 3.0f)));
	CHECK(fixture.Eval("return Vec3(1, 2, 3).z") == ScriptValue(3.0));
	CHECK(fixture.Eval("return tostring(Vec2(1, 0.5))") == ScriptValue(std::string("Vec2(1, 0.5)")));

	CHECK(fixture.Eval("return Vec3(1, 2, 3):Dot(Vec3(4, 5, 6))") == ScriptValue(32.0));
	CHECK(fixture.Eval("return Vec3.Dot(Vec3(1, 0, 0), Vec3(0, 1, 0))") == ScriptValue(0.0));
	CHECK(fixture.Eval("return Vec3(1, 0, 0):Cross(Vec3(0, 1, 0))") == ScriptValue(glm::vec3(0.0f, 0.0f, 1.0f)));
	CHECK(fixture.Eval("return Vec2(3, 4):Length()") == ScriptValue(5.0));
	CHECK(
		fixture.Eval("return Vec3(0, 3, 4):Normalize()") == ScriptValue(DMath::Normalize(glm::vec3(0.0f, 3.0f, 4.0f))));
	CHECK(fixture.Eval("return Vec3():Normalize()") == ScriptValue(glm::vec3(0.0f)));
	CHECK(
		fixture.Eval("return Vec3.Lerp(Vec3(0, 0, 0), Vec3(2, 4, 6), 0.5)")
		== ScriptValue(glm::vec3(1.0f, 2.0f, 3.0f)));
	CHECK(fixture.Eval("return Vec2.Distance(Vec2(1, 1), Vec2(4, 5))") == ScriptValue(5.0));
	CHECK(Contains(fixture.EvalError("return Vec3(1, 2)"), "expected 0 or 3 numbers"));
	CHECK(Contains(fixture.EvalError("return Vec2(1, 'a')"), "argument 2 must be a number"));
}

TEST_CASE("Scripting: quaternions rotate deterministically")
{
	ScriptFixture fixture;
	fixture.Create();
	CHECK(fixture.Eval("return Quat()") == ScriptValue(glm::quat(1.0f, 0.0f, 0.0f, 0.0f)));
	CHECK(fixture.Eval("return Quat(0, 0, 0, 1).w") == ScriptValue(1.0));
	const glm::quat quarter = DMath::AngleAxis(glm::half_pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f));
	CHECK(fixture.Eval("return Quat.AngleAxis(Math.Pi / 2, Vec3(0, 1, 0))") == ScriptValue(quarter));
	CHECK(
		fixture.Eval("return Quat.AngleAxis(Math.Pi / 2, Vec3(0, 1, 0)) * Vec3(1, 0, 0)")
		== ScriptValue(quarter * glm::vec3(1.0f, 0.0f, 0.0f)));
	CHECK(
		fixture.Eval("return Quat.Euler(0.1, 0.2, 0.3)")
		== ScriptValue(DMath::QuatFromEuler(glm::vec3(0.1f, 0.2f, 0.3f))));
	CHECK(
		fixture.Eval("return Quat.Euler(Vec3(0.1, 0.2, 0.3))")
		== ScriptValue(DMath::QuatFromEuler(glm::vec3(0.1f, 0.2f, 0.3f))));
	CHECK(
		fixture.Eval("local q = Quat.AngleAxis(1, Vec3(0, 0, 1)) return q * q:Inverse()")
		== ScriptValue(glm::quat(1.0f, 0.0f, 0.0f, 0.0f)));
	const glm::quat half = DMath::Slerp(glm::quat(1.0f, 0.0f, 0.0f, 0.0f), quarter, 0.5f);
	CHECK(
		fixture.Eval("return Quat.Slerp(Quat(), Quat.AngleAxis(Math.Pi / 2, Vec3(0, 1, 0)), 0.5)")
		== ScriptValue(half));

	// LookRotation turns -Z toward the target.
	const ScriptValue forward = fixture.Eval("return Quat.LookRotation(Vec3(1, 0, 0)) * Vec3(0, 0, -1)");
	CHECK(NearlyEqual(*forward.TryGet<glm::vec3>(), glm::vec3(1.0f, 0.0f, 0.0f)));
	const ScriptValue up = fixture.Eval("return Quat.LookRotation(Vec3(0, 1, 0)) * Vec3(0, 0, -1)");
	CHECK(NearlyEqual(*up.TryGet<glm::vec3>(), glm::vec3(0.0f, 1.0f, 0.0f)));
	CHECK(fixture.Eval("return Quat.LookRotation(Vec3())") == ScriptValue(glm::quat(1.0f, 0.0f, 0.0f, 0.0f)));
}

TEST_CASE("Scripting: colors and matrices")
{
	ScriptFixture fixture;
	fixture.Create();
	CHECK(fixture.Eval("return Color()") == ScriptValue(ScriptColorValue{ glm::vec4(1.0f) }));
	CHECK(fixture.Eval("return Color(1, 0.5, 0).a") == ScriptValue(1.0));
	CHECK(
		fixture.Eval("return Color(1, 0.5, 0, 0.25) * 2")
		== ScriptValue(ScriptColorValue{ glm::vec4(2.0f, 1.0f, 0.0f, 0.5f) }));
	CHECK(
		fixture.Eval("return Color.Lerp(Color(0, 0, 0, 0), Color(1, 1, 1, 1), 0.5)")
		== ScriptValue(ScriptColorValue{ glm::vec4(0.5f) }));
	CHECK(
		fixture.Eval("return Mat4.Translate(Vec3(1, 2, 3)):TransformPoint(Vec3(1, 1, 1))")
		== ScriptValue(glm::vec3(2.0f, 3.0f, 4.0f)));
	CHECK(
		fixture.Eval("return Mat4.Translate(Vec3(1, 2, 3)):TransformDirection(Vec3(1, 1, 1))")
		== ScriptValue(glm::vec3(1.0f, 1.0f, 1.0f)));
	CHECK(fixture.Eval("return Mat4.TRS(Vec3(1, 2, 3), Quat(), Vec3(2, 2, 2)):Get(1, 4)") == ScriptValue(1.0));
	CHECK(fixture.Eval("local m = Mat4.Scale(Vec3(2, 2, 2)) return (m * m:Inverse()) == Mat4()") == ScriptValue(true));
	CHECK(
		fixture.Eval("return Mat4.Translate(Vec3(1, 0, 0)) * Vec4(0, 0, 0, 1)")
		== ScriptValue(glm::vec4(1.0f, 0.0f, 0.0f, 1.0f)));
	CHECK(Contains(fixture.EvalError("return Mat4():Get(0, 1)"), "row and column must be 1 to 4"));
}

TEST_CASE("Scripting: Math helpers")
{
	ScriptFixture fixture;
	fixture.Create();
	CHECK(fixture.Eval("return Math.Pi") == ScriptValue(glm::pi<double>()));
	CHECK(fixture.Eval("return Math.Clamp(5, 0, 3)") == ScriptValue(3.0));
	CHECK(Contains(fixture.EvalError("return Math.Clamp(1, 3, 0)"), "min (3) is greater than max (0)"));
	CHECK(fixture.Eval("return Math.Lerp(2, 4, 0.5)") == ScriptValue(3.0));
	CHECK(fixture.Eval("return Math.InverseLerp(2, 4, 3)") == ScriptValue(0.5));
	CHECK(fixture.Eval("return Math.InverseLerp(2, 2, 3)") == ScriptValue(0.0));
	CHECK(fixture.Eval("return Math.SmoothStep(0, 1, 0.5)") == ScriptValue(0.5));
	CHECK(fixture.Eval("return Math.SmoothStep(0, 1, 2)") == ScriptValue(1.0));
	CHECK(fixture.Eval("return Math.MoveTowards(0, 10, 3)") == ScriptValue(3.0));
	CHECK(fixture.Eval("return Math.MoveTowards(9, 10, 3)") == ScriptValue(10.0));
	CHECK(fixture.Eval("return Math.MoveTowards(0, -10, 3)") == ScriptValue(-3.0));
	const ScriptValue delta = fixture.Eval("return Math.DeltaAngle(Math.Radians(350), Math.Radians(10))");
	CHECK(NearlyEqual(*delta.TryGet<double>(), glm::radians(20.0), 1e-12));
	CHECK(fixture.Eval("return Math.Degrees(Math.Pi)") == ScriptValue(180.0));
}
