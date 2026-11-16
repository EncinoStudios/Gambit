#include "gbpch.h"

#include "Gambit/Scripting/Bindings/CoreBindings.h"
#include "Gambit/Scripting/LuaBindings.h"

// Plain data between Lua and the engine (ScriptValue). Only the raw Lua API is used here (lua_next, lua_to*), which
// never raises an error, so C++ objects may live across the calls.

namespace Gambit::Bindings
{
	namespace
	{
		template<typename T>
		bool IsUserdataOf(lua_State* lua, int index)
		{
			return sol::stack::check<T>(lua, index, &sol::no_panic);
		}

		Result<ScriptValue> Read(lua_State* lua, int index, size_t depth);

		Result<ScriptValue> ReadTable(lua_State* lua, int index, size_t depth)
		{
			if (depth >= ScriptValue::MaxDepth)
			{
				return Error::Format(
					"tables are nested deeper than {} levels (or contain themselves): not plain data",
					ScriptValue::MaxDepth);
			}
			// lua_next needs the key and the value on the stack, at every level of nesting.
			if (lua_checkstack(lua, 3) == 0)
			{
				return Error("the Lua stack is full: the table is nested too deeply");
			}
			ScriptTable entries;
			lua_pushnil(lua);
			while (lua_next(lua, index) != 0)
			{
				Result<ScriptValue> key = Read(lua, lua_absindex(lua, -2), depth + 1);
				if (key && !ScriptValue::IsValidKey(*key))
				{
					key = Error::Format("a table key of type {} is not plain data", luaL_typename(lua, -2));
				}
				Result<ScriptValue> value =
					key ? Read(lua, lua_absindex(lua, -1), depth + 1) : Result<ScriptValue>(key.GetError());
				if (!value)
				{
					lua_pop(lua, 2);
					return value;
				}
				entries.push_back(ScriptTableEntry{ std::move(*key), std::move(*value) });
				lua_pop(lua, 1);
			}
			std::ranges::sort(entries, ScriptValue::CompareKeys, &ScriptTableEntry::Key);
			return ScriptValue(std::move(entries));
		}

		Result<ScriptValue> Read(lua_State* lua, int index, size_t depth)
		{
			switch (lua_type(lua, index))
			{
				case LUA_TNONE:
				case LUA_TNIL:
					return ScriptValue();
				case LUA_TBOOLEAN:
					return ScriptValue(lua_toboolean(lua, index) != 0);
				case LUA_TNUMBER:
					if (lua_isinteger(lua, index) != 0)
					{
						return ScriptValue(static_cast<int64_t>(lua_tointeger(lua, index)));
					}
					return ScriptValue(static_cast<double>(lua_tonumber(lua, index)));
				case LUA_TSTRING:
				{
					size_t size = 0;
					const char* text = lua_tolstring(lua, index, &size);
					return ScriptValue(std::string(text, size));
				}
				case LUA_TTABLE:
					return ReadTable(lua, index, depth);
				case LUA_TUSERDATA:
					if (IsUserdataOf<ScriptVec2>(lua, index))
					{
						return ScriptValue(sol::stack::get<ScriptVec2&>(lua, index).Get());
					}
					if (IsUserdataOf<ScriptVec3>(lua, index))
					{
						return ScriptValue(sol::stack::get<ScriptVec3&>(lua, index).Get());
					}
					if (IsUserdataOf<ScriptVec4>(lua, index))
					{
						return ScriptValue(sol::stack::get<ScriptVec4&>(lua, index).Get());
					}
					if (IsUserdataOf<ScriptQuat>(lua, index))
					{
						return ScriptValue(sol::stack::get<ScriptQuat&>(lua, index).Get());
					}
					if (IsUserdataOf<ScriptColor>(lua, index))
					{
						return ScriptValue(ScriptColorValue{ sol::stack::get<ScriptColor&>(lua, index).Get() });
					}
					break;
				default:
					break;
			}
			return Error::Format("a value of type {} is not plain data", luaL_typename(lua, index));
		}
	}

	Result<ScriptValue> ReadScriptValue(lua_State* lua, int index)
	{
		return Read(lua, lua_absindex(lua, index), 0);
	}
}
