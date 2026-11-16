#pragma once

// The API for binders: the code that adds a module's functions and types to the Lua state of every ScriptEngine
// (ApiRegistry::RegisterBinder). This is the one public header that includes sol2, which compiles slowly: include it
// only in the translation units that bind (Scripting/Bindings/*.cpp, a module's own LuaBindings.cpp).

#include "Gambit/Core/Base.h"
#include "Gambit/Scripting/ScriptEngine.h"

#include <sol/sol.hpp>

#include <format>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

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

	// The engine that owns a Lua state (its threads included).
	[[nodiscard]] ScriptEngine& GetScriptEngine(lua_State* state) noexcept;

	// The math values of the script API (Vec2, Vec3, Vec4, Quat, Color, Mat4), as sol2 usertypes. Functions that take
	// one read it with Get(). A color is a vec4 under a type of its own (ScriptColorTag), so Lua tells colors from
	// vectors.
	struct ScriptColorTag
	{};

	template<typename T, typename Tag = void>
	struct ScriptMathValue
	{
		T Value{};

		[[nodiscard]] T Get() const { return Value; }
	};

	using ScriptVec2 = ScriptMathValue<glm::vec2>;
	using ScriptVec3 = ScriptMathValue<glm::vec3>;
	using ScriptVec4 = ScriptMathValue<glm::vec4>;
	using ScriptQuat = ScriptMathValue<glm::quat>;
	using ScriptColor = ScriptMathValue<glm::vec4, ScriptColorTag>;
	using ScriptMat4 = ScriptMathValue<glm::mat4>;

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
