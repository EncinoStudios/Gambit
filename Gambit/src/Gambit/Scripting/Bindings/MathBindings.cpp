#include "gbpch.h"

#include "Gambit/Math/DMath.h"
#include "Gambit/Scripting/ApiRegistry.h"
#include "Gambit/Scripting/Bindings/CoreBindings.h"
#include "Gambit/Scripting/LuaBindings.h"

#include <glm/gtc/matrix_inverse.hpp>

// Vec2, Vec3, Vec4, Quat, Color and Mat4 (D-041): value types with operators. Everything is computed with plain float
// arithmetic, square roots and DMath, so results are the same on every platform (D-011). Lua numbers are doubles;
// vectors store floats like the components they come from.

namespace Gambit::Bindings
{
	namespace
	{
		template<int N>
		using Vector = glm::vec<N, float, glm::defaultp>;

		template<int N>
		using ScriptVector = ScriptMathValue<Vector<N>>;

		template<int N>
		constexpr std::string_view VectorName()
		{
			if constexpr (N == 2)
			{
				return "Vec2";
			}
			else if constexpr (N == 3)
			{
				return "Vec3";
			}
			else
			{
				return "Vec4";
			}
		}

		constexpr std::array<const char*, 4> Axes = { "x", "y", "z", "w" };
		constexpr std::array<const char*, 4> Channels = { "r", "g", "b", "a" };

		float CheckFloat(const sol::stack_proxy& value, std::string_view function, size_t position)
		{
			if (value.get_type() != sol::type::number)
			{
				ThrowScriptError(
					"{}: argument {} must be a number, got {}", function, position,
					sol::type_name(value.lua_state(), value.get_type()));
			}
			return static_cast<float>(value.as<double>());
		}

		std::string FormatFloats(std::string_view name, std::span<const float> parts)
		{
			std::string text(name);
			text += '(';
			for (size_t index = 0; index < parts.size(); ++index)
			{
				text += std::format("{}{}", index == 0 ? "" : ", ", parts[index]);
			}
			text += ')';
			return text;
		}

		// x / |x|; the zero vector and vectors that are not finite stay as they are (like DMath::Normalize).
		template<int N>
		Vector<N> NormalizeVector(const Vector<N>& x)
		{
			const float lengthSquared = glm::dot(x, x);
			if (!(lengthSquared > 0.0f) || !std::isfinite(lengthSquared))
			{
				return x;
			}
			return x / std::sqrt(lengthSquared);
		}

		template<int N>
		void BindVector(sol::state_view& lua)
		{
			using V = Vector<N>;
			using S = ScriptVector<N>;
			constexpr std::string_view name = VectorName<N>();

			sol::usertype<S> type = lua.new_usertype<S>(name, sol::no_constructor);
			type[sol::call_constructor] = [](sol::variadic_args args) {
				if (args.size() == 0)
				{
					return S{};
				}
				if (args.size() != N)
				{
					ThrowScriptError("{}: expected 0 or {} numbers, got {} arguments", VectorName<N>(), N, args.size());
				}
				V value(0.0f);
				for (int index = 0; index < N; ++index)
				{
					value[index] =
						CheckFloat(args[static_cast<size_t>(index)], VectorName<N>(), static_cast<size_t>(index) + 1);
				}
				return S{ value };
			};
			for (int index = 0; index < N; ++index)
			{
				type[Axes[static_cast<size_t>(index)]] = sol::property(
					[index](const S& self) { return static_cast<double>(self.Get()[index]); },
					[index](S& self, double value) { self.Value[index] = static_cast<float>(value); });
			}

			type["Dot"] = [](const S& a, const S& b) { return static_cast<double>(glm::dot(a.Get(), b.Get())); };
			type["Length"] = [](const S& self) {
				const V value = self.Get();
				return static_cast<double>(std::sqrt(glm::dot(value, value)));
			};
			type["Normalize"] = [](const S& self) {
				if constexpr (N == 3)
				{
					return S{ DMath::Normalize(self.Get()) };
				}
				else
				{
					return S{ NormalizeVector<N>(self.Get()) };
				}
			};
			type["Lerp"] = [](const S& a, const S& b, double t) {
				const V from = a.Get();
				return S{ from + (b.Get() - from) * static_cast<float>(t) };
			};
			type["Distance"] = [](const S& a, const S& b) {
				const V difference = b.Get() - a.Get();
				return static_cast<double>(std::sqrt(glm::dot(difference, difference)));
			};
			if constexpr (N == 3)
			{
				type["Cross"] = [](const S& a, const S& b) { return S{ glm::cross(a.Get(), b.Get()) }; };
			}

			type[sol::meta_function::addition] = [](const S& a, const S& b) { return S{ a.Get() + b.Get() }; };
			type[sol::meta_function::subtraction] = [](const S& a, const S& b) { return S{ a.Get() - b.Get() }; };
			type[sol::meta_function::multiplication] = sol::overload(
				[](const S& a, const S& b) { return S{ a.Get() * b.Get() }; },
				[](const S& a, double b) { return S{ a.Get() * static_cast<float>(b) }; },
				[](double a, const S& b) { return S{ static_cast<float>(a) * b.Get() }; });
			type[sol::meta_function::division] = sol::overload(
				[](const S& a, const S& b) { return S{ a.Get() / b.Get() }; },
				[](const S& a, double b) { return S{ a.Get() / static_cast<float>(b) }; });
			type[sol::meta_function::unary_minus] = [](const S& self) { return S{ -self.Get() }; };
			type[sol::meta_function::equal_to] = [](const S& a, const S& b) { return a.Get() == b.Get(); };
			type[sol::meta_function::to_string] = [](const S& self) {
				const V value = self.Get();
				return FormatFloats(VectorName<N>(), std::span<const float>(&value.x, N));
			};
		}

		// The rotation that turns the -Z axis toward `forward` with +Y as close to `up` as possible (glTF cameras look
		// down -Z, D-044). The identity for a zero forward; another up when `up` is parallel to `forward`.
		glm::quat LookRotation(const glm::vec3& forward, const glm::vec3& up)
		{
			const glm::vec3 direction = DMath::Normalize(forward);
			if (glm::dot(direction, direction) == 0.0f)
			{
				return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
			}
			glm::vec3 right = glm::cross(direction, up);
			if (glm::dot(right, right) < 1e-12f)
			{
				right = glm::cross(direction, std::abs(direction.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(0, 0, 1));
			}
			right = DMath::Normalize(right);
			const glm::vec3 trueUp = glm::cross(right, direction);
			glm::mat3 basis(1.0f);
			basis[0] = right;
			basis[1] = trueUp;
			basis[2] = -direction;
			return DMath::Normalize(glm::quat_cast(basis));
		}

		void BindQuat(sol::state_view& lua)
		{
			using S = ScriptQuat;
			sol::usertype<S> type = lua.new_usertype<S>("Quat", sol::no_constructor);
			type[sol::call_constructor] = [](sol::variadic_args args) {
				if (args.size() == 0)
				{
					return S{ glm::quat(1.0f, 0.0f, 0.0f, 0.0f) };
				}
				if (args.size() != 4)
				{
					ThrowScriptError("Quat: expected 0 or 4 numbers (x, y, z, w), got {} arguments", args.size());
				}
				std::array<float, 4> parts{};
				for (size_t index = 0; index < 4; ++index)
				{
					parts[index] = CheckFloat(args[index], "Quat", index + 1);
				}
				return S{ glm::quat(parts[3], parts[0], parts[1], parts[2]) };
			};
			for (int index = 0; index < 4; ++index)
			{
				type[Axes[static_cast<size_t>(index)]] = sol::property(
					[index](const S& self) { return static_cast<double>(self.Get()[index]); },
					[index](S& self, double value) { self.Value[index] = static_cast<float>(value); });
			}

			type["AngleAxis"] = [](double angle, const ScriptVec3& axis) {
				return S{ DMath::AngleAxis(static_cast<float>(angle), axis.Get()) };
			};
			type["Euler"] = [](sol::variadic_args args) {
				glm::vec3 angles(0.0f);
				if (args.size() == 1 && args[0].is<ScriptVec3>())
				{
					angles = args[0].as<ScriptVec3>().Get();
				}
				else if (args.size() == 3)
				{
					for (size_t index = 0; index < 3; ++index)
					{
						angles[static_cast<int>(index)] = CheckFloat(args[index], "Quat.Euler", index + 1);
					}
				}
				else
				{
					ThrowScriptError("Quat.Euler: expected a Vec3 or three numbers (radians about X, Y and Z)");
				}
				return S{ DMath::QuatFromEuler(angles) };
			};
			type["LookRotation"] = [](const ScriptVec3& forward, sol::optional<ScriptVec3> up) {
				return S{ LookRotation(forward.Get(), up.has_value() ? up->Get() : glm::vec3(0.0f, 1.0f, 0.0f)) };
			};
			type["Slerp"] = [](const S& a, const S& b, double t) {
				return S{ DMath::Slerp(a.Get(), b.Get(), static_cast<float>(t)) };
			};
			type["Inverse"] = [](const S& self) { return S{ glm::inverse(self.Get()) }; };
			type["Normalize"] = [](const S& self) { return S{ DMath::Normalize(self.Get()) }; };

			type[sol::meta_function::multiplication] = sol::overload(
				[](const S& a, const S& b) { return S{ a.Get() * b.Get() }; },
				[](const S& a, const ScriptVec3& b) { return ScriptVec3{ a.Get() * b.Get() }; });
			type[sol::meta_function::equal_to] = [](const S& a, const S& b) { return a.Get() == b.Get(); };
			type[sol::meta_function::to_string] = [](const S& self) {
				const glm::quat value = self.Get();
				const std::array<float, 4> parts = { value.x, value.y, value.z, value.w };
				return FormatFloats("Quat", parts);
			};
		}

		void BindColor(sol::state_view& lua)
		{
			using S = ScriptColor;
			sol::usertype<S> type = lua.new_usertype<S>("Color", sol::no_constructor);
			type[sol::call_constructor] = [](sol::variadic_args args) {
				if (args.size() == 0)
				{
					return S{ glm::vec4(1.0f) };
				}
				if (args.size() != 3 && args.size() != 4)
				{
					ThrowScriptError("Color: expected 0, 3 or 4 numbers (r, g, b, a), got {} arguments", args.size());
				}
				glm::vec4 value(1.0f);
				for (size_t index = 0; index < args.size(); ++index)
				{
					value[static_cast<int>(index)] = CheckFloat(args[index], "Color", index + 1);
				}
				return S{ value };
			};
			for (int index = 0; index < 4; ++index)
			{
				type[Channels[static_cast<size_t>(index)]] = sol::property(
					[index](const S& self) { return static_cast<double>(self.Get()[index]); },
					[index](S& self, double value) { self.Value[index] = static_cast<float>(value); });
			}
			type["Lerp"] = [](const S& a, const S& b, double t) {
				const glm::vec4 from = a.Get();
				return S{ from + (b.Get() - from) * static_cast<float>(t) };
			};
			type[sol::meta_function::addition] = [](const S& a, const S& b) { return S{ a.Get() + b.Get() }; };
			type[sol::meta_function::subtraction] = [](const S& a, const S& b) { return S{ a.Get() - b.Get() }; };
			type[sol::meta_function::multiplication] = sol::overload(
				[](const S& a, const S& b) { return S{ a.Get() * b.Get() }; },
				[](const S& a, double b) { return S{ a.Get() * static_cast<float>(b) }; },
				[](double a, const S& b) { return S{ static_cast<float>(a) * b.Get() }; });
			type[sol::meta_function::equal_to] = [](const S& a, const S& b) { return a.Get() == b.Get(); };
			type[sol::meta_function::to_string] = [](const S& self) {
				const glm::vec4 value = self.Get();
				return FormatFloats("Color", std::span<const float>(&value.x, 4));
			};
		}

		void BindMat4(sol::state_view& lua)
		{
			using S = ScriptMat4;
			sol::usertype<S> type = lua.new_usertype<S>("Mat4", sol::no_constructor);
			type[sol::call_constructor] = []() { return S{ glm::mat4(1.0f) }; };
			type["Translate"] = [](const ScriptVec3& translation) {
				return S{ glm::translate(glm::mat4(1.0f), translation.Get()) };
			};
			type["Rotate"] = [](const ScriptQuat& rotation) { return S{ glm::mat4_cast(rotation.Get()) }; };
			type["Scale"] = [](const ScriptVec3& scale) { return S{ glm::scale(glm::mat4(1.0f), scale.Get()) }; };
			type["TRS"] = [](const ScriptVec3& translation, const ScriptQuat& rotation, const ScriptVec3& scale) {
				return S{ ComposeTransform(translation.Get(), DMath::Normalize(rotation.Get()), scale.Get()) };
			};
			type["Inverse"] = [](const S& self) { return S{ glm::inverse(self.Get()) }; };
			type["Transpose"] = [](const S& self) { return S{ glm::transpose(self.Get()) }; };
			type["TransformPoint"] = [](const S& self, const ScriptVec3& point) {
				return ScriptVec3{ glm::vec3(self.Get() * glm::vec4(point.Get(), 1.0f)) };
			};
			type["TransformDirection"] = [](const S& self, const ScriptVec3& direction) {
				return ScriptVec3{ glm::vec3(self.Get() * glm::vec4(direction.Get(), 0.0f)) };
			};
			type["Get"] = [](const S& self, int64_t row, int64_t column) {
				if (row < 1 || row > 4 || column < 1 || column > 4)
				{
					ThrowScriptError("Mat4:Get: row and column must be 1 to 4, got {} and {}", row, column);
				}
				return static_cast<double>(self.Get()[static_cast<int>(column - 1)][static_cast<int>(row - 1)]);
			};
			type[sol::meta_function::multiplication] = sol::overload(
				[](const S& a, const S& b) { return S{ a.Get() * b.Get() }; },
				[](const S& a, const ScriptVec4& b) { return ScriptVec4{ a.Get() * b.Get() }; });
			type[sol::meta_function::equal_to] = [](const S& a, const S& b) { return a.Get() == b.Get(); };
			type[sol::meta_function::to_string] = [](const S& self) {
				const glm::mat4 transposed = glm::transpose(self.Get()); // rows, for reading
				return FormatFloats("Mat4", std::span<const float>(&transposed[0][0], 16));
			};
		}

		double CheckedClamp(double value, double minimum, double maximum)
		{
			if (!(minimum <= maximum))
			{
				ThrowScriptError("Math.Clamp: min ({}) is greater than max ({})", minimum, maximum);
			}
			return std::clamp(value, minimum, maximum);
		}

		void BindMath(sol::state_view& lua)
		{
			sol::table math = lua.create_named_table("Math");
			math["Pi"] = glm::pi<double>();
			math.set_function("Clamp", &CheckedClamp);
			math.set_function("Lerp", [](double a, double b, double t) { return a + (b - a) * t; });
			math.set_function(
				"InverseLerp", [](double a, double b, double value) { return a == b ? 0.0 : (value - a) / (b - a); });
			math.set_function("SmoothStep", [](double edge0, double edge1, double x) {
				if (edge0 == edge1)
				{
					return x < edge0 ? 0.0 : 1.0;
				}
				const double t = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
				return t * t * (3.0 - 2.0 * t);
			});
			math.set_function("MoveTowards", [](double current, double target, double maxDelta) {
				const double difference = target - current;
				if (std::abs(difference) <= maxDelta)
				{
					return target;
				}
				return current + (difference > 0.0 ? maxDelta : -maxDelta);
			});
			math.set_function("DeltaAngle", [](double from, double to) {
				constexpr double TwoPi = 2.0 * glm::pi<double>();
				double delta = DMath::Fmod(to - from, TwoPi);
				if (delta > glm::pi<double>())
				{
					delta -= TwoPi;
				}
				else if (delta <= -glm::pi<double>())
				{
					delta += TwoPi;
				}
				return delta;
			});
			math.set_function("Radians", [](double degrees) { return degrees * (glm::pi<double>() / 180.0); });
			math.set_function("Degrees", [](double radians) { return radians * (180.0 / glm::pi<double>()); });
		}

		void BindMathTypes(ScriptBindingContext& context)
		{
			sol::state_view& lua = context.GetLua();
			BindVector<2>(lua);
			BindVector<3>(lua);
			BindVector<4>(lua);
			BindQuat(lua);
			BindColor(lua);
			BindMat4(lua);
			BindMath(lua);
		}

		template<int N>
		void DocumentVector(ApiRegistry& registry)
		{
			const std::string name(VectorName<N>());
			const std::string parameters = N == 2 ? "x: number, y: number"
				: N == 3                          ? "x: number, y: number, z: number"
												  : "x: number, y: number, z: number, w: number";
			registry.Document(
				name, ApiEntryKind::Type, std::format("{}({}?) -> {}", name, parameters, name),
				std::format(
					"A {}-component float vector, zero without arguments. Operators: + and - (vectors), * and / (by a "
					"number or component-wise by a vector), unary -, ==, tostring. A vector that a component field "
					"returned writes through to the field when its components are assigned.",
					N),
				std::format(
					"local v = {}({})", name,
					N == 2       ? "1, 2"
						: N == 3 ? "1, 2, 3"
								 : "1, 2, 3, 4"));
			for (int index = 0; index < N; ++index)
			{
				registry.Document(
					std::format("{}.{}", name, Axes[static_cast<size_t>(index)]), ApiEntryKind::Property,
					std::format("{}.{}: number", name, Axes[static_cast<size_t>(index)]),
					std::format("Component {} of the vector (read and write).", index + 1));
			}
			registry.Document(
				name + ":Dot", ApiEntryKind::Method, std::format("{}:Dot(other: {}) -> number", name, name),
				"The dot product.", "local d = a:Dot(b)");
			registry.Document(
				name + ":Length", ApiEntryKind::Method, std::format("{}:Length() -> number", name), "The length.",
				"local speed = velocity:Length()");
			registry.Document(
				name + ":Normalize", ApiEntryKind::Method, std::format("{}:Normalize() -> {}", name, name),
				"The vector scaled to length 1; a zero vector stays zero.", "local direction = offset:Normalize()");
			registry.Document(
				name + ".Lerp", ApiEntryKind::Function,
				std::format("{}.Lerp(a: {}, b: {}, t: number) -> {}", name, name, name, name),
				"Linear interpolation a + (b - a) * t (t is not clamped).",
				std::format("local mid = {}.Lerp(a, b, 0.5)", name));
			registry.Document(
				name + ".Distance", ApiEntryKind::Function,
				std::format("{}.Distance(a: {}, b: {}) -> number", name, name, name),
				"The distance between two points.", std::format("local d = {}.Distance(a, b)", name));
			if constexpr (N == 3)
			{
				registry.Document(
					"Vec3:Cross", ApiEntryKind::Method, "Vec3:Cross(other: Vec3) -> Vec3", "The cross product.",
					"local normal = edge1:Cross(edge2)");
			}
		}

		void DocumentMathTypes(ApiRegistry& registry)
		{
			DocumentVector<2>(registry);
			DocumentVector<3>(registry);
			DocumentVector<4>(registry);

			registry.Document(
				"Quat", ApiEntryKind::Type, "Quat(x?: number, y?: number, z?: number, w?: number) -> Quat",
				"A rotation quaternion, the identity without arguments (components in file order x, y, z, w). "
				"Operators: Quat * Quat composes (the right one applies first), Quat * Vec3 rotates the vector, ==, "
				"tostring.",
				"local q = Quat.AngleAxis(Math.Pi / 2, Vec3(0, 1, 0))");
			for (const char* axis : Axes)
			{
				registry.Document(
					std::format("Quat.{}", axis), ApiEntryKind::Property, std::format("Quat.{}: number", axis),
					"A component of the quaternion (read and write).");
			}
			registry.Document(
				"Quat.AngleAxis", ApiEntryKind::Function, "Quat.AngleAxis(angle: number, axis: Vec3) -> Quat",
				"The rotation of `angle` radians about `axis` (need not be normalized); the identity for a zero axis.",
				"local spin = Quat.AngleAxis(speed * dt, Vec3(0, 1, 0))");
			registry.Document(
				"Quat.Euler", ApiEntryKind::Function, "Quat.Euler(x: number, y: number, z: number) -> Quat",
				"The rotation by the angles (radians) about X, then Y, then Z; also takes one Vec3 of angles.",
				"local q = Quat.Euler(0, Math.Radians(90), 0)");
			registry.Document(
				"Quat.LookRotation", ApiEntryKind::Function, "Quat.LookRotation(forward: Vec3, up?: Vec3) -> Quat",
				"The rotation that turns -Z (the way cameras look) toward `forward`, with +Y as close to `up` "
				"(default Vec3(0, 1, 0)) as possible.",
				"transform.Rotation = Quat.LookRotation(target - position)");
			registry.Document(
				"Quat.Slerp", ApiEntryKind::Function, "Quat.Slerp(a: Quat, b: Quat, t: number) -> Quat",
				"Spherical interpolation along the shortest arc, t in [0, 1].", "local q = Quat.Slerp(a, b, 0.25)");
			registry.Document(
				"Quat:Inverse", ApiEntryKind::Method, "Quat:Inverse() -> Quat", "The inverse rotation.",
				"local back = q:Inverse()");
			registry.Document(
				"Quat:Normalize", ApiEntryKind::Method, "Quat:Normalize() -> Quat",
				"The quaternion scaled to length 1; the identity for a zero quaternion.", "q = q:Normalize()");

			registry.Document(
				"Color", ApiEntryKind::Type, "Color(r?: number, g?: number, b?: number, a?: number) -> Color",
				"A linear RGBA color, white without arguments, alpha 1 when omitted. Operators: + and -, * (by a "
				"number "
				"or component-wise), ==, tostring.",
				"local red = Color(1, 0, 0)");
			for (const char* channel : Channels)
			{
				registry.Document(
					std::format("Color.{}", channel), ApiEntryKind::Property, std::format("Color.{}: number", channel),
					"A channel of the color (read and write).");
			}
			registry.Document(
				"Color.Lerp", ApiEntryKind::Function, "Color.Lerp(a: Color, b: Color, t: number) -> Color",
				"Linear interpolation of every channel (t is not clamped).", "local c = Color.Lerp(red, blue, 0.5)");

			registry.Document(
				"Mat4", ApiEntryKind::Type, "Mat4() -> Mat4",
				"A 4x4 matrix, the identity without arguments. Operators: Mat4 * Mat4, Mat4 * Vec4, ==, tostring.",
				"local m = Mat4.TRS(position, rotation, Vec3(1, 1, 1))");
			registry.Document(
				"Mat4.Translate", ApiEntryKind::Function, "Mat4.Translate(translation: Vec3) -> Mat4",
				"A translation matrix.", "local m = Mat4.Translate(Vec3(0, 1, 0))");
			registry.Document(
				"Mat4.Rotate", ApiEntryKind::Function, "Mat4.Rotate(rotation: Quat) -> Mat4", "A rotation matrix.",
				"local m = Mat4.Rotate(q)");
			registry.Document(
				"Mat4.Scale", ApiEntryKind::Function, "Mat4.Scale(scale: Vec3) -> Mat4", "A scale matrix.",
				"local m = Mat4.Scale(Vec3(2, 2, 2))");
			registry.Document(
				"Mat4.TRS", ApiEntryKind::Function, "Mat4.TRS(translation: Vec3, rotation: Quat, scale: Vec3) -> Mat4",
				"translation * rotation * scale, the matrix of a Transform.", "local m = Mat4.TRS(t, r, s)");
			registry.Document(
				"Mat4:Inverse", ApiEntryKind::Method, "Mat4:Inverse() -> Mat4", "The inverse matrix.",
				"local toLocal = m:Inverse()");
			registry.Document(
				"Mat4:Transpose", ApiEntryKind::Method, "Mat4:Transpose() -> Mat4", "The transposed matrix.",
				"local t = m:Transpose()");
			registry.Document(
				"Mat4:TransformPoint", ApiEntryKind::Method, "Mat4:TransformPoint(point: Vec3) -> Vec3",
				"The point transformed (with translation).", "local world = m:TransformPoint(Vec3(0, 0, 1))");
			registry.Document(
				"Mat4:TransformDirection", ApiEntryKind::Method, "Mat4:TransformDirection(direction: Vec3) -> Vec3",
				"The direction transformed (without translation).",
				"local forward = m:TransformDirection(Vec3(0, 0, -1))");
			registry.Document(
				"Mat4:Get", ApiEntryKind::Method, "Mat4:Get(row: integer, column: integer) -> number",
				"One element, rows and columns counted from 1.", "local tx = m:Get(1, 4)");

			registry.Document(
				"Math", ApiEntryKind::Table, "Math", "Scalar helpers; angles are radians.",
				"local x = Math.Clamp(v, 0, 1)");
			registry.Document(
				"Math.Pi", ApiEntryKind::Constant, "Math.Pi: number", "The number pi.", "local half = Math.Pi / 2");
			registry.Document(
				"Math.Clamp", ApiEntryKind::Function, "Math.Clamp(value: number, min: number, max: number) -> number",
				"The value limited to [min, max]; min greater than max is an error.",
				"local health = Math.Clamp(h, 0, 100)");
			registry.Document(
				"Math.Lerp", ApiEntryKind::Function, "Math.Lerp(a: number, b: number, t: number) -> number",
				"a + (b - a) * t (t is not clamped).", "local x = Math.Lerp(0, 10, 0.5)");
			registry.Document(
				"Math.InverseLerp", ApiEntryKind::Function,
				"Math.InverseLerp(a: number, b: number, value: number) -> number",
				"The t for which Lerp(a, b, t) is `value`; 0 when a equals b.", "local t = Math.InverseLerp(0, 10, 5)");
			registry.Document(
				"Math.SmoothStep", ApiEntryKind::Function,
				"Math.SmoothStep(edge0: number, edge1: number, x: number) -> number",
				"0 below edge0, 1 above edge1, a smooth Hermite curve between (as in shaders).",
				"local fade = Math.SmoothStep(0, 1, t)");
			registry.Document(
				"Math.MoveTowards", ApiEntryKind::Function,
				"Math.MoveTowards(current: number, target: number, maxDelta: number) -> number",
				"`current` moved toward `target` by at most `maxDelta`.",
				"speed = Math.MoveTowards(speed, 10, 2 * dt)");
			registry.Document(
				"Math.DeltaAngle", ApiEntryKind::Function, "Math.DeltaAngle(from: number, to: number) -> number",
				"The shortest signed angle from `from` to `to`, in (-pi, pi].", "local turn = Math.DeltaAngle(a, b)");
			registry.Document(
				"Math.Radians", ApiEntryKind::Function, "Math.Radians(degrees: number) -> number",
				"Degrees to radians.", "local angle = Math.Radians(90)");
			registry.Document(
				"Math.Degrees", ApiEntryKind::Function, "Math.Degrees(radians: number) -> number",
				"Radians to degrees.", "local degrees = Math.Degrees(Math.Pi)");
		}
	}

	void RegisterMathApi(ApiRegistry& registry)
	{
		registry.RegisterBinder("Math", &BindMathTypes);
		DocumentMathTypes(registry);
	}
}
