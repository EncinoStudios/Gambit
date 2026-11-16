#include "gbpch.h"

#include "Gambit/Math/DMath.h"
#include "Gambit/Math/Random.h"
#include "Gambit/Scripting/ApiRegistry.h"
#include "Gambit/Scripting/Bindings/CoreBindings.h"
#include "Gambit/Scripting/LuaBindings.h"

#include <limits>

// The `^` operator of every Lua state (LuaUser.h). Lua keeps its own exact shortcut for squares.
extern "C" double gb_lua_pow(double base, double exponent)
{
	return exponent == 2.0 ? base * base : Gambit::DMath::Pow(base, exponent);
}

namespace Gambit::Bindings
{
	namespace
	{
		// These are plain C functions: they only use the Lua API and hold no C++ objects when they raise an error.

		int MathSin(lua_State* lua)
		{
			lua_pushnumber(lua, DMath::Sin(luaL_checknumber(lua, 1)));
			return 1;
		}

		int MathCos(lua_State* lua)
		{
			lua_pushnumber(lua, DMath::Cos(luaL_checknumber(lua, 1)));
			return 1;
		}

		int MathTan(lua_State* lua)
		{
			lua_pushnumber(lua, DMath::Tan(luaL_checknumber(lua, 1)));
			return 1;
		}

		int MathAsin(lua_State* lua)
		{
			lua_pushnumber(lua, DMath::Asin(luaL_checknumber(lua, 1)));
			return 1;
		}

		int MathAcos(lua_State* lua)
		{
			lua_pushnumber(lua, DMath::Acos(luaL_checknumber(lua, 1)));
			return 1;
		}

		// math.atan(y [, x]): the angle of the point (x, y), x defaulting to 1.
		int MathAtan(lua_State* lua)
		{
			const double y = luaL_checknumber(lua, 1);
			const double x = luaL_optnumber(lua, 2, 1.0);
			lua_pushnumber(lua, DMath::Atan2(y, x));
			return 1;
		}

		int MathExp(lua_State* lua)
		{
			lua_pushnumber(lua, DMath::Exp(luaL_checknumber(lua, 1)));
			return 1;
		}

		// math.log(x [, base]): natural logarithm, or in `base` (base 2 exactly through Log2).
		int MathLog(lua_State* lua)
		{
			const double x = luaL_checknumber(lua, 1);
			if (lua_isnoneornil(lua, 2))
			{
				lua_pushnumber(lua, DMath::Log(x));
				return 1;
			}
			const double base = luaL_checknumber(lua, 2);
			lua_pushnumber(lua, base == 2.0 ? DMath::Log2(x) : DMath::Log(x) / DMath::Log(base));
			return 1;
		}

		Random& GetRandom(lua_State* lua)
		{
			return GetScriptEngine(lua).GetRandom();
		}

		// math.random(): a float in [0, 1); math.random(m): an integer in [1, m]; math.random(m, n): an integer in
		// [m, n]; math.random(0): an integer with all bits random. All from the world's generator (D-011).
		int MathRandom(lua_State* lua)
		{
			Random& random = GetRandom(lua);
			switch (lua_gettop(lua))
			{
				case 0:
					lua_pushnumber(lua, random.NextDouble01());
					return 1;
				case 1:
				{
					const lua_Integer upper = luaL_checkinteger(lua, 1);
					if (upper == 0)
					{
						lua_pushinteger(lua, static_cast<lua_Integer>(random.NextU64()));
						return 1;
					}
					luaL_argcheck(lua, upper >= 1, 1, "interval is empty");
					lua_pushinteger(lua, random.RangeInt(int64_t{ 1 }, static_cast<int64_t>(upper)));
					return 1;
				}
				case 2:
				{
					const lua_Integer lower = luaL_checkinteger(lua, 1);
					const lua_Integer upper = luaL_checkinteger(lua, 2);
					luaL_argcheck(lua, lower <= upper, 2, "interval is empty");
					lua_pushinteger(lua, random.RangeInt(static_cast<int64_t>(lower), static_cast<int64_t>(upper)));
					return 1;
				}
				default:
					return luaL_error(lua, "wrong number of arguments to 'random'");
			}
		}

		// math.randomseed(n): reseeds the world's generator, which the engine shares (still deterministic). There is no
		// seed from the clock.
		int MathRandomSeed(lua_State* lua)
		{
			if (lua_isnoneornil(lua, 1))
			{
				return luaL_error(
					lua, "math.randomseed needs a seed: a random seed from the clock would not be deterministic");
			}
			const lua_Integer seed =
				lua_isinteger(lua, 1) != 0 ? lua_tointeger(lua, 1) : static_cast<lua_Integer>(luaL_checknumber(lua, 1));
			GetRandom(lua).Seed(static_cast<uint64_t>(seed));
			return 0;
		}
	}

	void OpenMathLibrary(lua_State* lua)
	{
		lua_getglobal(lua, LUA_MATHLIBNAME);
		const std::array<std::pair<const char*, lua_CFunction>, 10> functions = { {
			{ "sin", &MathSin },
			{ "cos", &MathCos },
			{ "tan", &MathTan },
			{ "asin", &MathAsin },
			{ "acos", &MathAcos },
			{ "atan", &MathAtan },
			{ "exp", &MathExp },
			{ "log", &MathLog },
			{ "random", &MathRandom },
			{ "randomseed", &MathRandomSeed },
		} };
		for (const auto& [name, function] : functions)
		{
			lua_pushcfunction(lua, function);
			lua_setfield(lua, -2, name);
		}
		lua_pop(lua, 1);
	}

	void RegisterSandboxApi(ApiRegistry& registry)
	{
		// The sandbox is part of every state, not a binder; its changes to the standard library are documented here.
		const auto document = [&](std::string_view name, std::string_view signature, std::string_view description,
		                          std::string_view example) {
			registry.Document(ApiEntry{
				.Name = std::string(name),
				.Kind = ApiEntryKind::Function,
				.Signature = std::string(signature),
				.Description = std::string(description),
				.Example = std::string(example),
				.Module = "Sandbox",
			});
		};
		document(
			"print", "print(...: any)",
			"Writes the values, separated by tabs, to the log (info level) with the location of the call.",
			"print(\"spawned\", entity.Name)");
		document(
			"require", "require(name: string) -> any",
			"Runs a module once and returns its value (true when it returns nothing). \"Util.Math\" is the project's "
			"Scripts/Util/Math.lua; \"Gambit.<Name>\" is a module of the engine's Lua library. Cycles are errors.",
			"local Util = require(\"Util.Math\")");
		document(
			"load",
			"load(chunk: string, chunkName?: string, mode?: \"t\", environment?: table) -> function | nil, string?",
			"Compiles a text chunk; binary chunks are refused. With `environment` the chunk sees that table as its "
			"globals. Returns nil and a message when the chunk does not compile.",
			"local f = load(\"return 1 + 1\")");
		document(
			"math.random", "math.random(m?: integer, n?: integer) -> number",
			"Lua's math.random drawing from the world's Random (D-011): a float in [0, 1) without arguments, an "
			"integer in [1, m] or [m, n], an integer with every bit random for math.random(0).",
			"local roll = math.random(1, 6)");
		document(
			"math.randomseed", "math.randomseed(seed: integer)",
			"Reseeds the world's Random, which the engine shares. A seed is required.", "math.randomseed(42)");
	}
}
