#pragma once

#include <span>
#include <string_view>

namespace Gambit
{
	// A text file embedded into the engine at build time.
	struct EmbeddedTextFile
	{
		std::string_view Name;
		std::string_view Text;
	};

	// The engine's Lua library (Gambit/Lua, D-041), embedded by gb_embed_lua_library and sorted by module name:
	// Gambit/Lua/StateMachine.lua is the module "Gambit.StateMachine". Every script engine loads each module and makes
	// it the global named by the module's last part (StateMachine); scripts may also `require` it.
	[[nodiscard]] std::span<const EmbeddedTextFile> GetEngineLuaLibrary();
}
