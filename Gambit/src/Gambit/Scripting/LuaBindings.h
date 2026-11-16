#pragma once

// The API for binders: the code that adds a module's functions and types to the Lua state of every ScriptEngine
// (ApiRegistry::RegisterBinder). This is the one public header that includes sol2, which compiles slowly: include it
// only in the translation units that bind (Scripting/Bindings/*.cpp, a module's own LuaBindings.cpp).

#include "Gambit/Core/Base.h"
#include "Gambit/Core/UUID.h"
#include "Gambit/ECS/Entity.h"
#include "Gambit/Reflection/ComponentInfo.h"
#include "Gambit/Reflection/FieldType.h"
#include "Gambit/Scripting/ScriptEngine.h"

#include <sol/sol.hpp>

#include <array>
#include <cstdint>
#include <format>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace Gambit
{
	// The error a binding raises for bad script input. sol2 catches it where Lua called into C++ and raises it as a Lua
	// error whose message starts with the location of the calling script line ("Scripts/Spin.lua:12: ..."). Never let
	// it escape anywhere else: only throw it from functions that Lua calls.
	class ScriptException final : public std::runtime_error
	{
	public:
		using std::runtime_error::runtime_error;
	};

	template<typename... Args>
	[[noreturn]] void ThrowScriptError(std::format_string<Args...> format, Args&&... args)
	{
		throw ScriptException(std::format(format, std::forward<Args>(args)...));
	}

	// An optional argument of a binding, taken as a sol::object parameter: nothing when it is nil or absent, the value
	// when it is a T, and a script error that names it otherwise (`what`: "Entity:SetParent: parent", `expected`: "an
	// Entity"). Prefer this to a sol::optional<T> parameter, which takes a value of the wrong type for a missing one.
	template<typename T>
	[[nodiscard]] std::optional<T>
	GetOptionalArgument(const sol::object& value, std::string_view what, std::string_view expected)
	{
		if (!value.valid())
		{
			return std::nullopt;
		}
		if (!value.is<T>())
		{
			ThrowScriptError(
				"{}: expected {} or nil, got {}", what, expected, sol::type_name(value.lua_state(), value.get_type()));
		}
		return value.as<T>();
	}

	// The engine that owns a Lua state (its threads included).
	[[nodiscard]] ScriptEngine& GetScriptEngine(lua_State* state) noexcept;

	// Where the innermost script function on the call stack is running: the script line that called into C++. Empty
	// when no script function is running.
	[[nodiscard]] ScriptLocation GetCallerLocation(lua_State* state);

	// Where a value that a component proxy handed out lives: a field of a component of an entity and, inside it, the
	// struct fields and array elements to follow. Proxies keep the binding instead of a pointer and resolve it on every
	// access, so they never dangle: a proxy whose entity, component or element is gone raises a clear error.
	struct FieldBinding
	{
		static constexpr size_t MaxDepth = 8;

		ScriptEngine* Engine = nullptr;
		UUID Entity;
		ComponentId Component = 0;
		// Path[0] is the index of the component's field; each further entry the index of a struct field or of an array
		// element (from 0) inside the value reached so far.
		std::array<uint32_t, MaxDepth> Path{};
		uint8_t Depth = 0; // entries of Path in use; 0 is the component itself
	};

	// The current value at a binding (Depth >= 1). Throws a ScriptException when the binding is stale.
	[[nodiscard]] FieldValue ReadBinding(const FieldBinding& binding);
	// Replaces the value at a binding (Depth >= 1); `value` must hold the alternative of the bound value's type. Throws
	// a ScriptException when the binding is stale or the value does not fit (a float that is not finite, ...).
	void WriteBinding(const FieldBinding& binding, const FieldValue& value);

	// The math values of the script API (Vec2, Vec3, Vec4, Quat, Color, Mat4), as sol2 usertypes. Functions that take
	// one read it with Get(). A color is a vec4 under a type of its own (ScriptColorTag), so Lua tells colors from
	// vectors. A value that a component field returned is bound to the field: reading it reads the field, and
	// assigning a component (`transform.Translation.x = 5`) writes through. Arithmetic yields plain values.
	struct ScriptColorTag
	{};

	template<typename T, typename Tag = void>
	struct ScriptMathValue
	{
		static constexpr bool IsColor = std::is_same_v<Tag, ScriptColorTag>;

		T Value{};
		Ref<const FieldBinding> Binding; // null for plain values

		ScriptMathValue() = default;
		explicit ScriptMathValue(const T& value, Ref<const FieldBinding> binding = nullptr)
			: Value(value), Binding(std::move(binding))
		{}

		[[nodiscard]] T Get() const
		{
			if (Binding == nullptr)
			{
				return Value;
			}
			const FieldValue current = ReadBinding(*Binding);
			if constexpr (IsColor)
			{
				if (const glm::vec3* rgb = std::get_if<glm::vec3>(&current); rgb != nullptr)
				{
					return glm::vec4(*rgb, 1.0f); // a Color3 field
				}
			}
			const T* stored = std::get_if<T>(&current);
			if (stored == nullptr)
			{
				throw ScriptException("the field this value is bound to changed its type");
			}
			return *stored;
		}

		// Assigns component `index` (x, y, z, w or r, g, b, a), through to the field when bound.
		void SetComponent(int index, float component)
		{
			if (Binding == nullptr)
			{
				Value[index] = component;
				return;
			}
			FieldValue current = ReadBinding(*Binding);
			if constexpr (IsColor)
			{
				if (glm::vec3* rgb = std::get_if<glm::vec3>(&current); rgb != nullptr)
				{
					if (index == 3)
					{
						throw ScriptException("the color is a Color3 field: it has no alpha");
					}
					(*rgb)[index] = component;
					WriteBinding(*Binding, current);
					return;
				}
			}
			T* stored = std::get_if<T>(&current);
			if (stored == nullptr)
			{
				throw ScriptException("the field this value is bound to changed its type");
			}
			(*stored)[index] = component;
			WriteBinding(*Binding, current);
		}
	};

	using ScriptVec2 = ScriptMathValue<glm::vec2>;
	using ScriptVec3 = ScriptMathValue<glm::vec3>;
	using ScriptVec4 = ScriptMathValue<glm::vec4>;
	using ScriptQuat = ScriptMathValue<glm::quat>;
	using ScriptColor = ScriptMathValue<glm::vec4, ScriptColorTag>;

	// A matrix is never bound: no field holds one.
	struct ScriptMat4
	{
		glm::mat4 Value{ 1.0f };

		ScriptMat4() = default;
		explicit ScriptMat4(const glm::mat4& value)
			: Value(value)
		{}

		[[nodiscard]] const glm::mat4& Get() const noexcept { return Value; }
	};

	// An entity of the engine's world as Lua sees it (the Entity usertype): by UUID, so a handle never dangles.
	struct ScriptEntity
	{
		ScriptEngine* Engine = nullptr;
		UUID Id;
	};

	// The live entity of a script handle; throws a ScriptException that names it when it no longer exists or belongs to
	// another engine than `engine`.
	[[nodiscard]] Entity ResolveEntity(ScriptEngine& engine, const ScriptEntity& entity);

	// What a binder works with: the engine whose state it fills, the state, and the global table.
	class ScriptBindingContext
	{
	public:
		explicit ScriptBindingContext(ScriptEngine& engine)
			: m_Engine(engine), m_Lua(engine.GetLuaState())
		{}

		[[nodiscard]] ScriptEngine& GetEngine() const noexcept { return m_Engine; }
		[[nodiscard]] sol::state_view& GetLua() noexcept { return m_Lua; }
		[[nodiscard]] sol::table GetGlobals() { return m_Lua.globals(); }

	private:
		ScriptEngine& m_Engine;
		sol::state_view m_Lua;
	};

	// Calls the function below `argumentCount` arguments on top of the stack in protected mode, under the engine's
	// limits, and leaves `resultCount` results (LUA_MULTRET: all of them) in its place. An error is recorded with its
	// location (ScriptEngine::GetErrors) when `record` is true, logged, and returned; the stack then holds nothing in
	// place of the function and its arguments. `context` names what runs ("SpinSystem.OnUpdate").
	[[nodiscard]] Result<void> CallScriptFunction(
		ScriptEngine& engine, int argumentCount, int resultCount, std::string_view context, bool record = true);
}
