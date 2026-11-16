#include "gbpch.h"

#include "Gambit/Scripting/ScriptEngine.h"

#include "Gambit/Core/VFS.h"
#include "Gambit/ECS/World.h"
#include "Gambit/Math/Random.h"
#include "Gambit/Reflection/TypeRegistry.h"
#include "Gambit/Scripting/Bindings/CoreBindings.h"
#include "Gambit/Scripting/LuaBindings.h"
#include "Gambit/Scripting/LuaLibrary.h"

#include <cctype>
#include <cstdlib>

namespace Gambit
{
	namespace Internal
	{
		struct ScriptEngineState
		{
			ScriptEngine* Owner = nullptr;
			ScriptEngineSpecification Specification;
			TypeRegistry* Types = nullptr;
			lua_State* Lua = nullptr;
			size_t MemoryUsed = 0;
			// The limits apply while a call from the engine runs (CallDepth > 0): what the engine itself does in the
			// state outside calls (binding the API, converting results) never fails for lack of memory.
			uint32_t CallDepth = 0;
			uint64_t InstructionsUsed = 0;
			bool Frozen = false;
			Random OwnRandom;
			std::vector<ScriptError> Errors;
			std::string Warning; // the pieces of a warning so far
		};
	}

	namespace
	{
		using Internal::ScriptEngineState;

		// The watchdog runs every HookInterval instructions; once a call is over its budget, every instruction, so that
		// a script that catches the error with pcall cannot keep running.
		constexpr int HookInterval = 1000;

		constexpr const char* LoadedKey = "Gambit.Loaded";       // module name -> value, or LoadingMarker
		constexpr const char* TracebackKey = "Gambit.Traceback"; // of the latest error
		constexpr const char* FreezeKey = "Gambit.Freeze";       // the function that freezes the globals
		constexpr const char* WorldKey = "Gambit.World";         // the world object, set by the World binder
		constexpr const char* LoadingMarker = "Gambit.Loading";  // a light userdata: the module is being loaded

		ScriptEngineState& GetState(lua_State* lua) noexcept
		{
			return **static_cast<ScriptEngineState**>(lua_getextraspace(lua));
		}

		void* Allocate(void* userData, void* pointer, size_t oldSize, size_t newSize) noexcept
		{
			ScriptEngineState& state = *static_cast<ScriptEngineState*>(userData);
			// Lua passes the kind of object instead of a size when it allocates a new block.
			const size_t previous = pointer != nullptr ? oldSize : 0;
			if (newSize == 0)
			{
				std::free(pointer);
				state.MemoryUsed -= previous;
				return nullptr;
			}
			if (newSize > previous && state.CallDepth > 0
			    && state.MemoryUsed - previous + newSize > state.Specification.Limits.MemoryBytes)
			{
				return nullptr; // Lua raises a memory error
			}
			void* block = std::realloc(pointer, newSize);
			if (block != nullptr)
			{
				state.MemoryUsed = state.MemoryUsed - previous + newSize;
			}
			return block;
		}

		void WatchdogHook(lua_State* lua, lua_Debug*)
		{
			ScriptEngineState& state = GetState(lua);
			if (state.CallDepth == 0)
			{
				return;
			}
			state.InstructionsUsed += HookInterval;
			if (state.InstructionsUsed <= state.Specification.Limits.InstructionsPerCall)
			{
				return;
			}
			lua_sethook(lua, &WatchdogHook, LUA_MASKCOUNT, 1);
			luaL_where(lua, 0); // the function that is running: where the loop is
			lua_pushfstring(
				lua, "instruction limit of %I exceeded (an endless loop?)",
				static_cast<lua_Integer>(state.Specification.Limits.InstructionsPerCall));
			lua_concat(lua, 2);
			lua_error(lua);
		}

		int Panic(lua_State* lua)
		{
			const char* message = lua_tostring(lua, -1);
			GB_CORE_CRITICAL(
				"unprotected Lua error (an engine bug): {}", message != nullptr ? message : "(no message)");
			return 0; // Lua aborts
		}

		void Warn(void* userData, const char* message, int toContinue)
		{
			ScriptEngineState& state = *static_cast<ScriptEngineState*>(userData);
			if (state.Warning.empty() && toContinue == 0 && message[0] == '@')
			{
				return; // a control message ("@on", "@off"): warnings are always on
			}
			state.Warning += message;
			if (toContinue == 0)
			{
				GB_WARN("[Lua] {}", state.Warning);
				state.Warning.clear();
			}
		}

		// sol2 calls this where Lua called into C++ and a binding threw: the message gets the location of the calling
		// script line, as a Lua error would.
		int HandleBindingException(lua_State* lua, sol::optional<const std::exception&>, sol::string_view what)
		{
			luaL_where(lua, 1);
			lua_pushlstring(lua, what.data(), what.size());
			lua_concat(lua, 2);
			return 1;
		}

		// Whether the first line of `text` starts with a location "<chunk>:<line>:".
		bool HasLocation(std::string_view text) noexcept
		{
			const std::string_view line = text.substr(0, text.find('\n'));
			for (size_t colon = line.find(':'); colon != std::string_view::npos; colon = line.find(':', colon + 1))
			{
				size_t end = colon + 1;
				while (end < line.size() && std::isdigit(static_cast<unsigned char>(line[end])) != 0)
				{
					++end;
				}
				if (end > colon + 1 && end < line.size() && line[end] == ':')
				{
					return true;
				}
			}
			return false;
		}

		// The message handler of every call: makes the error a string that starts with its location (errors raised
		// without one get the location of the innermost script function) and keeps the traceback in the registry.
		int MessageHandler(lua_State* lua)
		{
			if (lua_type(lua, 1) != LUA_TSTRING)
			{
				if (luaL_callmeta(lua, 1, "__tostring") == 0 || lua_type(lua, -1) != LUA_TSTRING)
				{
					lua_pushfstring(lua, "(an error object of type %s)", luaL_typename(lua, 1));
				}
				lua_replace(lua, 1);
			}
			size_t size = 0;
			const char* text = lua_tolstring(lua, 1, &size);
			if (!HasLocation(std::string_view(text, size)))
			{
				lua_Debug frame;
				for (int level = 1; lua_getstack(lua, level, &frame) != 0; ++level)
				{
					lua_getinfo(lua, "Sl", &frame);
					if (frame.currentline > 0)
					{
						lua_pushfstring(lua, "%s:%d: ", frame.short_src, frame.currentline);
						lua_insert(lua, 1);
						lua_concat(lua, 2);
						break;
					}
				}
			}
			luaL_traceback(lua, lua, nullptr, 1);
			lua_setfield(lua, LUA_REGISTRYINDEX, TracebackKey);
			lua_settop(lua, 1);
			return 1;
		}

		// "Scripts/Spin.lua:12: message" -> file and line.
		void ParseLocation(ScriptError& error)
		{
			const std::string_view text = error.Message;
			const std::string_view line = text.substr(0, text.find('\n'));
			for (size_t colon = line.find(':'); colon != std::string_view::npos; colon = line.find(':', colon + 1))
			{
				size_t end = colon + 1;
				int32_t number = 0;
				while (end < line.size() && std::isdigit(static_cast<unsigned char>(line[end])) != 0
				       && number < 100'000'000)
				{
					number = number * 10 + (line[end] - '0');
					++end;
				}
				if (end > colon + 1 && end < line.size() && line[end] == ':')
				{
					error.File = std::string(line.substr(0, colon));
					error.Line = number;
					return;
				}
			}
		}

		std::string ToStdString(lua_State* lua, int index)
		{
			size_t size = 0;
			const char* text = lua_tolstring(lua, index, &size);
			return text != nullptr ? std::string(text, size) : std::string();
		}

		void BeginCall(ScriptEngineState& state)
		{
			if (state.CallDepth++ == 0)
			{
				state.InstructionsUsed = 0;
				lua_sethook(state.Lua, &WatchdogHook, LUA_MASKCOUNT, HookInterval);
			}
		}

		void EndCall(ScriptEngineState& state)
		{
			--state.CallDepth;
		}

		// lua_pcall with the message handler and, when `limited`, the limits (engine code runs without them); on
		// failure the error (with `context`) replaces the function and its arguments, nothing is left on the stack, and
		// the error is returned.
		Result<void> ProtectedCall(
			ScriptEngineState& state, int argumentCount, int resultCount, std::string_view context, bool record,
			bool limited = true)
		{
			lua_State* lua = state.Lua;
			const int functionIndex = lua_gettop(lua) - argumentCount;
			GB_CORE_ASSERT(functionIndex >= 1, "ProtectedCall needs a function below its arguments");
			// The message handler, the traceback and the results need room on the stack (Lua checks the room for the
			// call itself).
			if (lua_checkstack(lua, 4 + std::max(resultCount, 0)) == 0)
			{
				lua_settop(lua, functionIndex - 1);
				return Error::Format("{}: the Lua stack is full", context);
			}
			lua_pushcfunction(lua, &MessageHandler);
			lua_insert(lua, functionIndex);
			lua_pushnil(lua);
			lua_setfield(lua, LUA_REGISTRYINDEX, TracebackKey);

			if (limited)
			{
				BeginCall(state);
			}
			const int status = lua_pcall(lua, argumentCount, resultCount, functionIndex);
			if (limited)
			{
				EndCall(state);
			}
			lua_remove(lua, functionIndex);
			if (status == LUA_OK)
			{
				return {};
			}

			ScriptError error;
			if (status == LUA_ERRMEM)
			{
				error.Message = std::format(
					"the script ran out of memory: the limit is {} bytes ({} in use)",
					state.Specification.Limits.MemoryBytes, state.MemoryUsed);
			}
			else
			{
				error.Message = ToStdString(lua, -1);
			}
			lua_pop(lua, 1);
			lua_getfield(lua, LUA_REGISTRYINDEX, TracebackKey);
			error.Traceback = ToStdString(lua, -1);
			lua_pop(lua, 1);
			ParseLocation(error);
			error.Context = std::string(context);
			if (state.Specification.Scene != nullptr)
			{
				error.Tick = state.Specification.Scene->GetTick();
			}
			Error result(std::format("{}: {}", context, error.Message));
			if (record)
			{
				state.Owner->ReportError(std::move(error));
			}
			return result;
		}

		bool IsIdentifier(std::string_view text) noexcept
		{
			const auto isLetter = [](char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; };
			return !text.empty() && isLetter(text.front())
				&& std::ranges::all_of(text, [&](char c) { return isLetter(c) || (c >= '0' && c <= '9'); });
		}

		// "Util.Math": identifiers separated by dots.
		bool IsModuleName(std::string_view name) noexcept
		{
			if (name.empty())
			{
				return false;
			}
			for (size_t start = 0;;)
			{
				const size_t dot = name.find('.', start);
				if (!IsIdentifier(
						name.substr(start, dot == std::string_view::npos ? std::string_view::npos : dot - start)))
				{
					return false;
				}
				if (dot == std::string_view::npos)
				{
					return true;
				}
				start = dot + 1;
			}
		}

		// The module a script file is, when it has a valid name below the scripts directory: "Scripts/Util/Math.lua"
		// is "Util.Math".
		std::optional<std::string> ModuleOfPath(std::string_view scriptsDirectory, std::string_view path)
		{
			const std::string prefix = std::string(scriptsDirectory) + "/";
			if (!path.starts_with(prefix) || !path.ends_with(".lua"))
			{
				return std::nullopt;
			}
			std::string module(path.substr(prefix.size(), path.size() - prefix.size() - 4));
			std::ranges::replace(module, '/', '.');
			if (module.starts_with("Gambit.") || !IsModuleName(module))
			{
				return std::nullopt;
			}
			return module;
		}

		const EmbeddedTextFile* FindLibraryModule(std::string_view name)
		{
			const std::span<const EmbeddedTextFile> library = GetEngineLuaLibrary();
			const auto found = std::ranges::lower_bound(library, name, {}, &EmbeddedTextFile::Name);
			return found != library.end() && found->Name == name ? &*found : nullptr;
		}

		// Pushes Loaded[module] (nil when absent).
		void PushLoaded(lua_State* lua, std::string_view module)
		{
			lua_getfield(lua, LUA_REGISTRYINDEX, LoadedKey);
			lua_pushlstring(lua, module.data(), module.size());
			lua_rawget(lua, -2);
			lua_remove(lua, -2);
		}

		// Loaded[module] = the value on top of the stack (popped).
		void SetLoaded(lua_State* lua, std::string_view module)
		{
			lua_getfield(lua, LUA_REGISTRYINDEX, LoadedKey);
			lua_pushlstring(lua, module.data(), module.size());
			lua_pushvalue(lua, -3);
			lua_rawset(lua, -3);
			lua_pop(lua, 2);
		}

		void MarkLoading(lua_State* lua, std::string_view module)
		{
			lua_pushlightuserdata(lua, const_cast<char*>(LoadingMarker));
			SetLoaded(lua, module);
		}

		bool IsLoadingMarker(lua_State* lua, int index)
		{
			return lua_type(lua, index) == LUA_TLIGHTUSERDATA && lua_touserdata(lua, index) == LoadingMarker;
		}

		// The sandbox's `load`: text chunks only; an environment table may replace the globals of the chunk. Like
		// Lua's load it returns nil and a message when the chunk does not compile.
		int SandboxLoad(lua_State* lua)
		{
			if (lua_type(lua, 1) != LUA_TSTRING)
			{
				return luaL_error(lua, "load: the chunk must be a string (scripts load text only)");
			}
			size_t size = 0;
			const char* text = lua_tolstring(lua, 1, &size);
			const char* chunkName = luaL_optstring(lua, 2, "=(load)");
			const char* mode = luaL_optstring(lua, 3, "t");
			const bool hasEnvironment = !lua_isnone(lua, 4);
			if (std::string_view(mode) != "t")
			{
				lua_pushnil(lua);
				lua_pushstring(lua, "binary chunks are not allowed: load accepts text only (mode \"t\")");
				return 2;
			}
			if (luaL_loadbufferx(lua, text, size, chunkName, "t") != LUA_OK)
			{
				lua_pushnil(lua);
				lua_insert(lua, -2);
				return 2;
			}
			if (hasEnvironment)
			{
				lua_pushvalue(lua, 4);
				if (lua_setupvalue(lua, -2, 1) == nullptr)
				{
					lua_pop(lua, 1);
				}
			}
			return 1;
		}

		// The sandbox's `print`: to the client log, with the location of the call.
		int SandboxPrint(lua_State* lua)
		{
			const int count = lua_gettop(lua);
			luaL_Buffer buffer;
			luaL_buffinit(lua, &buffer);
			luaL_where(lua, 1);
			luaL_addvalue(&buffer);
			for (int index = 1; index <= count; ++index)
			{
				if (index > 1)
				{
					luaL_addchar(&buffer, '\t');
				}
				luaL_tolstring(lua, index, nullptr);
				luaL_addvalue(&buffer);
			}
			luaL_pushresult(&buffer);
			size_t size = 0;
			const char* text = lua_tolstring(lua, -1, &size);
			GB_INFO("{}", std::string_view(text, size));
			return 0;
		}

		// The sandbox's `setmetatable`: refuses finalizers and weak tables, whose behavior depends on when the garbage
		// collector runs (D-011). The original function is upvalue 1.
		int SandboxSetMetatable(lua_State* lua)
		{
			if (lua_type(lua, 2) == LUA_TTABLE)
			{
				for (const char* field : { "__gc", "__mode" })
				{
					lua_pushstring(lua, field);
					const bool present = lua_rawget(lua, 2) != LUA_TNIL;
					lua_pop(lua, 1);
					if (present)
					{
						return luaL_error(
							lua,
							"setmetatable: metatables with %s are not allowed: finalizers and weak tables make "
							"scripts depend on the garbage collector",
							field);
					}
				}
			}
			lua_pushvalue(lua, lua_upvalueindex(1));
			lua_insert(lua, 1);
			lua_call(lua, lua_gettop(lua) - 1, 1);
			return 1;
		}

		// Engine code run once in every state: hides sol2's internal globals from `pairs(_G)`, protects the metatable
		// of the global table, and returns the function that freezes the globals (FinishLoading).
		constexpr std::string_view SandboxChunk = R"lua(
local G, next, type, error, tostring, rawequal, rawset, rawget, setmetatable = ...

local function IsHidden(key)
	return type(key) == "string" and key:sub(1, 4) == "sol."
end

local function VisibleNext(t, key)
	local value
	repeat
		key, value = next(t, key)
	until key == nil or not IsHidden(key)
	return key, value
end

local metatable = {
	__pairs = function(t)
		return VisibleNext, t, nil
	end,
	__metatable = false,
}
setmetatable(G, metatable)

return function()
	local backing = {}
	for key, value in next, G do
		backing[key] = value
	end
	for key in next, backing do
		rawset(G, key, nil)
	end
	local frozen = "globals are frozen once the scripts are loaded: keep state in components and helpers in locals"
	backing.rawset = function(t, key, value)
		if rawequal(t, G) then
			error("cannot assign the global '" .. tostring(key) .. "': " .. frozen, 2)
		end
		return rawset(t, key, value)
	end
	backing.rawget = function(t, key)
		if rawequal(t, G) then
			return rawget(backing, key)
		end
		return rawget(t, key)
	end
	metatable.__index = backing
	metatable.__newindex = function(_, key)
		error("cannot assign the global '" .. tostring(key) .. "': " .. frozen, 2)
	end
	metatable.__pairs = function()
		return function(_, key)
			return VisibleNext(backing, key)
		end, G, nil
	end
end
)lua";

		Result<void> OpenSandbox(ScriptEngineState& state)
		{
			lua_State* lua = state.Lua;
			if (lua_checkstack(lua, 16) == 0)
			{
				return Error("the Lua stack cannot grow");
			}
			const std::array<std::pair<const char*, lua_CFunction>, 6> libraries = { {
				{ LUA_GNAME, &luaopen_base },
				{ LUA_COLIBNAME, &luaopen_coroutine },
				{ LUA_TABLIBNAME, &luaopen_table },
				{ LUA_STRLIBNAME, &luaopen_string },
				{ LUA_MATHLIBNAME, &luaopen_math },
				{ LUA_UTF8LIBNAME, &luaopen_utf8 },
			} };
			for (const auto& [name, open] : libraries)
			{
				luaL_requiref(lua, name, open, 1);
				lua_pop(lua, 1);
			}

			lua_pushglobaltable(lua);
			for (const char* removed : { "dofile", "loadfile", "collectgarbage" })
			{
				lua_pushnil(lua);
				lua_setfield(lua, -2, removed);
			}
			lua_pushcfunction(lua, &SandboxLoad);
			lua_setfield(lua, -2, "load");
			lua_pushcfunction(lua, &SandboxPrint);
			lua_setfield(lua, -2, "print");
			lua_getfield(lua, -1, "setmetatable");
			lua_pushcclosure(lua, &SandboxSetMetatable, 1);
			lua_setfield(lua, -2, "setmetatable");
			lua_getfield(lua, -1, LUA_STRLIBNAME);
			lua_pushnil(lua);
			lua_setfield(lua, -2, "dump"); // bytecode is useless where only text loads
			lua_pop(lua, 2);
			Bindings::OpenMathLibrary(lua);

			lua_newtable(lua);
			lua_setfield(lua, LUA_REGISTRYINDEX, LoadedKey);

			// The sandbox chunk gets the original functions, before scripts can replace them.
			if (luaL_loadbufferx(lua, SandboxChunk.data(), SandboxChunk.size(), "=(sandbox)", "t") != LUA_OK)
			{
				return Error::Format("the sandbox does not compile: {}", ToStdString(lua, -1));
			}
			lua_pushglobaltable(lua);
			for (const char* name : { "next", "type", "error", "tostring", "rawequal", "rawset", "rawget" })
			{
				lua_getglobal(lua, name);
			}
			lua_getglobal(lua, "setmetatable");
			if (Result<void> ran = ProtectedCall(state, 9, 1, "creating the sandbox", false, false); !ran)
			{
				return ran;
			}
			lua_setfield(lua, LUA_REGISTRYINDEX, FreezeKey);
			return {};
		}

		// Runs a module's source and leaves its value on the stack (true when it returns nothing). Errors carry the
		// location and are not recorded: the caller decides.
		Result<void> RunModule(
			ScriptEngineState& state, std::string_view chunkName, std::string_view source, std::string_view context,
			bool record)
		{
			lua_State* lua = state.Lua;
			if (luaL_loadbufferx(lua, source.data(), source.size(), std::string(chunkName).c_str(), "t") != LUA_OK)
			{
				ScriptError error;
				error.Message = ToStdString(lua, -1);
				lua_pop(lua, 1);
				ParseLocation(error);
				error.Context = std::string(context);
				Error result(std::format("{}: {}", context, error.Message));
				if (record)
				{
					state.Owner->ReportError(std::move(error));
				}
				return result;
			}
			if (Result<void> ran = ProtectedCall(state, 0, 1, context, record); !ran)
			{
				return ran;
			}
			if (lua_isnil(lua, -1))
			{
				lua_pop(lua, 1);
				lua_pushboolean(lua, 1);
			}
			return {};
		}

		// `require(name)`: a module of the project ("Util.Math" is <Scripts>/Util/Math.lua) or of the engine library
		// ("Gambit.StateMachine"), run once and cached.
		sol::object Require(sol::this_state lua, std::string_view name)
		{
			ScriptEngineState& state = GetState(lua);
			if (!IsModuleName(name))
			{
				ThrowScriptError(
					"require: '{}' is not a module name (identifiers separated by dots: \"Util.Math\")", name);
			}
			PushLoaded(lua, name);
			if (IsLoadingMarker(lua, -1))
			{
				lua_pop(lua, 1);
				ThrowScriptError("require: module '{}' requires itself (a cycle of requires)", name);
			}
			if (!lua_isnil(lua, -1))
			{
				return sol::stack::pop<sol::object>(lua);
			}
			lua_pop(lua, 1);

			std::string chunkName;
			std::string source;
			if (name.starts_with("Gambit."))
			{
				const EmbeddedTextFile* module = FindLibraryModule(name);
				if (module == nullptr)
				{
					ThrowScriptError("require: the engine library has no module '{}'", name);
				}
				chunkName = std::format("@Gambit/Lua/{}.lua", name.substr(7));
				std::ranges::replace(chunkName.begin() + 12, chunkName.end() - 4, '.', '/');
				source = std::string(module->Text);
			}
			else
			{
				std::string path = std::string(name);
				std::ranges::replace(path, '.', '/');
				path = std::format("{}/{}.lua", state.Specification.ScriptsDirectory, path);
				const VFS* files = state.Specification.Files;
				Result<std::string> text = files != nullptr
					? files->ReadText(path)
					: Result<std::string>(Error("the engine has no project files"));
				if (!text)
				{
					ThrowScriptError("require: module '{}' not found ({}: {})", name, path, text.GetError().Message);
				}
				chunkName = "@" + path;
				source = std::move(*text);
			}

			MarkLoading(lua, name);
			if (Result<void> ran = RunModule(state, chunkName, source, std::format("loading module '{}'", name), false);
			    !ran)
			{
				lua_pushnil(lua);
				SetLoaded(lua, name);
				ThrowScriptError("{}", ran.GetError().Message);
			}
			lua_pushvalue(lua, -1);
			SetLoaded(lua, name);
			return sol::stack::pop<sol::object>(lua);
		}
	}

	// ---- ScriptEngine ----

	ScriptEngine& GetScriptEngine(lua_State* state) noexcept
	{
		return *GetState(state).Owner;
	}

	Result<void>
	CallScriptFunction(ScriptEngine& engine, int argumentCount, int resultCount, std::string_view context, bool record)
	{
		return ProtectedCall(GetState(engine.GetLuaState()), argumentCount, resultCount, context, record);
	}

	void ScriptEngine::RegisterBinder(std::string_view name, ScriptBinderFunction bind)
	{
		ApiRegistry::Get().RegisterBinder(name, std::move(bind));
	}

	ScriptEngine::ScriptEngine(ConstructionKey, ScriptEngineSpecification specification)
		: m_State(CreateScope<Internal::ScriptEngineState>())
	{
		m_State->Owner = this;
		m_State->Specification = std::move(specification);
		ScriptEngineSpecification& spec = m_State->Specification;
		m_State->Types = spec.Types != nullptr ? spec.Types
			: spec.Scene != nullptr            ? &spec.Scene->GetTypeRegistry()
											   : &TypeRegistry::Get();
		if (spec.Api == nullptr)
		{
			spec.Api = &ApiRegistry::Get();
		}
	}

	ScriptEngine::~ScriptEngine()
	{
		if (m_State->Lua != nullptr)
		{
			lua_close(m_State->Lua);
		}
	}

	Result<Ref<ScriptEngine>> ScriptEngine::Create(ScriptEngineSpecification specification)
	{
		if (specification.Mode == ScriptMode::Simulation && specification.Scene == nullptr)
		{
			return Error("a simulation script engine needs a world");
		}
		if (specification.Mode == ScriptMode::Schema && specification.Scene != nullptr)
		{
			return Error("a schema script engine has no world");
		}
		if (specification.Limits.MemoryBytes == 0 || specification.Limits.InstructionsPerCall == 0)
		{
			return Error("the script limits must not be zero");
		}
		if (specification.ScriptsDirectory.empty())
		{
			return Error("the scripts directory must not be empty");
		}

		Ref<ScriptEngine> engine = CreateRef<ScriptEngine>(ConstructionKey{}, std::move(specification));
		ScriptEngineState& state = *engine->m_State;
		state.Lua = lua_newstate(&Allocate, &state);
		if (state.Lua == nullptr)
		{
			return Error("cannot create a Lua state: out of memory");
		}
		*static_cast<ScriptEngineState**>(lua_getextraspace(state.Lua)) = &state;
		sol::set_default_state(state.Lua, &Panic);
		sol::set_default_exception_handler(state.Lua, &HandleBindingException);
		lua_setwarnf(state.Lua, &Warn, &state);
		lua_sethook(state.Lua, &WatchdogHook, LUA_MASKCOUNT, HookInterval);

		if (Result<void> opened = OpenSandbox(state); !opened)
		{
			return opened.GetError();
		}
		sol::state_view(state.Lua).set_function("require", &Require);

		ScriptBindingContext context(*engine);
		for (const ScriptBinder& binder : state.Specification.Api->GetBinders())
		{
			try
			{
				binder.Bind(context);
			}
			catch (const std::exception& exception)
			{
				return Error::Format("the script binder '{}' failed: {}", binder.Name, exception.what());
			}
		}

		// The engine library modules become globals named by their last part.
		for (const EmbeddedTextFile& module : GetEngineLuaLibrary())
		{
			lua_State* lua = state.Lua;
			lua_getglobal(lua, "require");
			lua_pushlstring(lua, module.Name.data(), module.Name.size());
			if (Result<void> loaded = ProtectedCall(state, 1, 1, std::format("loading {}", module.Name), false, false);
			    !loaded)
			{
				return loaded.GetError();
			}
			const std::string_view global = module.Name.substr(module.Name.find_last_of('.') + 1);
			lua_setglobal(lua, std::string(global).c_str());
		}
		return engine;
	}

	// ---- Loading ----

	Result<void> ScriptEngine::LoadScripts()
	{
		ScriptEngineState& state = *m_State;
		const VFS* files = state.Specification.Files;
		const std::string& directory = state.Specification.ScriptsDirectory;
		if (files == nullptr || !files->Exists(directory))
		{
			return {};
		}
		Result<std::vector<DirectoryEntry>> entries = files->List(directory, true);
		if (!entries)
		{
			return Error::Format("cannot list the scripts in '{}': {}", directory, entries.GetError().Message);
		}

		size_t failures = 0;
		size_t scripts = 0;
		std::optional<Error> first;
		for (const DirectoryEntry& entry : *entries)
		{
			if (entry.IsDirectory || !entry.Path.ends_with(".lua"))
			{
				continue;
			}
			++scripts;
			if (const std::optional<std::string> module = ModuleOfPath(directory, entry.Path); module.has_value())
			{
				PushLoaded(state.Lua, *module);
				const bool loaded = !lua_isnil(state.Lua, -1);
				lua_pop(state.Lua, 1);
				if (loaded)
				{
					continue; // a script required it already
				}
			}
			Result<void> result = [&]() -> Result<void> {
				Result<std::string> source = files->ReadText(entry.Path);
				if (!source)
				{
					ScriptError error;
					error.Message = std::format("cannot read the script: {}", source.GetError().Message);
					error.File = entry.Path;
					error.Context = std::format("loading {}", entry.Path);
					ReportError(error);
					return Error::Format("{}: {}", entry.Path, error.Message);
				}
				return LoadScript(entry.Path, *source);
			}();
			if (!result)
			{
				++failures;
				if (!first.has_value())
				{
					first = result.GetError();
				}
			}
		}
		if (failures > 0)
		{
			return Error::Format(
				"{} of {} scripts failed to load{}", failures, scripts,
				first.has_value() ? std::format(" (the first: {})", first->Message) : std::string());
		}
		return {};
	}

	Result<void> ScriptEngine::LoadScript(std::string_view path, std::string_view source)
	{
		ScriptEngineState& state = *m_State;
		if (state.Frozen)
		{
			return Error::Format("cannot load '{}': loading is finished", path);
		}
		const std::optional<std::string> module = ModuleOfPath(state.Specification.ScriptsDirectory, path);
		if (module.has_value())
		{
			MarkLoading(state.Lua, *module);
		}
		Result<void> ran = RunModule(state, "@" + std::string(path), source, std::format("loading {}", path), true);
		if (!ran)
		{
			if (module.has_value())
			{
				lua_pushnil(state.Lua);
				SetLoaded(state.Lua, *module);
			}
			return ran;
		}
		if (module.has_value())
		{
			SetLoaded(state.Lua, *module);
		}
		else
		{
			lua_pop(state.Lua, 1);
		}
		return {};
	}

	void ScriptEngine::FinishLoading()
	{
		ScriptEngineState& state = *m_State;
		if (state.Frozen)
		{
			return;
		}
		lua_getfield(state.Lua, LUA_REGISTRYINDEX, FreezeKey);
		[[maybe_unused]] const Result<void> frozen = ProtectedCall(state, 0, 0, "freezing the globals", false, false);
		GB_CORE_ASSERT(frozen.HasValue(), "freezing the globals failed: {}", frozen ? "" : frozen.GetError().Message);
		state.Frozen = true;
	}

	bool ScriptEngine::IsLoadingFinished() const noexcept
	{
		return m_State->Frozen;
	}

	Result<ScriptValue> ScriptEngine::Evaluate(std::string_view code, std::string_view chunkName)
	{
		ScriptEngineState& state = *m_State;
		lua_State* lua = state.Lua;
		if (luaL_loadbufferx(lua, code.data(), code.size(), std::string(chunkName).c_str(), "t") != LUA_OK)
		{
			std::string message = ToStdString(lua, -1);
			lua_pop(lua, 1);
			return Error(std::move(message));
		}
		lua_getfield(lua, LUA_REGISTRYINDEX, WorldKey);
		const int argumentCount = lua_isnil(lua, -1) ? 0 : 1;
		if (argumentCount == 0)
		{
			lua_pop(lua, 1);
		}
		if (Result<void> ran = ProtectedCall(state, argumentCount, 1, "evaluate", false); !ran)
		{
			return ran.GetError();
		}
		Result<ScriptValue> value = Bindings::ReadScriptValue(lua, -1);
		lua_pop(lua, 1);
		return value;
	}

	// ---- State ----

	ScriptMode ScriptEngine::GetMode() const noexcept
	{
		return m_State->Specification.Mode;
	}

	World* ScriptEngine::GetWorld() const noexcept
	{
		return m_State->Specification.Scene;
	}

	TypeRegistry& ScriptEngine::GetTypeRegistry() const noexcept
	{
		return *m_State->Types;
	}

	const ScriptEngineSpecification& ScriptEngine::GetSpecification() const noexcept
	{
		return m_State->Specification;
	}

	Random& ScriptEngine::GetRandom() noexcept
	{
		World* world = m_State->Specification.Scene;
		return world != nullptr ? world->GetRandom() : m_State->OwnRandom;
	}

	size_t ScriptEngine::GetMemoryUsage() const noexcept
	{
		return m_State->MemoryUsed;
	}

	std::span<const ScriptError> ScriptEngine::GetErrors() const noexcept
	{
		return m_State->Errors;
	}

	void ScriptEngine::ReportError(ScriptError error)
	{
		GB_ERROR("[script] {}: {}", error.Context, error.Message);
		if (!error.Traceback.empty())
		{
			GB_TRACE("[script] {}", error.Traceback);
		}
		m_State->Errors.push_back(std::move(error));
	}

	lua_State* ScriptEngine::GetLuaState() const noexcept
	{
		return m_State->Lua;
	}
}
