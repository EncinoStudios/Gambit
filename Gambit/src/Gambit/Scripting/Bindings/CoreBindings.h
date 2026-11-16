#pragma once

#include "Gambit/Core/Result.h"
#include "Gambit/Scripting/ScriptValue.h"

struct lua_State;

// The engine's own Lua API, one registration function per binding translation unit (the sol2-heavy code is split over
// several of them to keep compile times sane). RegisterEngineScriptApi calls the Register functions in a fixed order;
// ScriptEngine uses the rest. Only the Scripting module includes this header.

namespace Gambit
{
	class ApiRegistry;

	namespace Bindings
	{
		// ---- The sandbox (ScriptEngine creation) ----

		// Replaces the transcendental functions of the math library by DMath and its random functions by the engine's
		// Random (D-011). MathLibrary.cpp.
		void OpenMathLibrary(lua_State* state);
		// Documents print, require, load and the math library changes. MathLibrary.cpp.
		void RegisterSandboxApi(ApiRegistry& registry);

		// ---- Binders ----

		// Vec2, Vec3, Vec4, Quat, Color, Mat4 and Math. MathBindings.cpp.
		void RegisterMathApi(ApiRegistry& registry);
		// Component, System, Test and the field type constructors (Int, Enum, Array, ...). DeclarationBindings.cpp.
		void RegisterDeclarationApi(ApiRegistry& registry);
		// The proxies of components, structs and arrays. ComponentBindings.cpp.
		void RegisterComponentApi(ApiRegistry& registry);
		// Entity. EntityBindings.cpp.
		void RegisterEntityApi(ApiRegistry& registry);
		// World. WorldBindings.cpp.
		void RegisterWorldApi(ApiRegistry& registry);
		// SortedPairs and Table, the engine Lua library (Gambit/Lua). LibraryApi.cpp.
		void RegisterLibraryApi(ApiRegistry& registry);

		// ---- Plain data (ValueBindings.cpp) ----

		// The value at `index` as plain data; fails for functions, coroutines, foreign userdata, keys that are not
		// booleans, numbers or strings, and tables nested deeper than ScriptValue::MaxDepth (cycles included). Never
		// raises a Lua error.
		[[nodiscard]] Result<ScriptValue> ReadScriptValue(lua_State* state, int index);
	}
}
