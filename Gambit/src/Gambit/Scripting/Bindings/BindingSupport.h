#pragma once

#include "Gambit/Scripting/LuaBindings.h"

#include <string_view>
#include <vector>

// What the engine's own binding translation units share (sol2-heavy, Scripting module only). Binders of other modules
// use Gambit/Scripting/LuaBindings.h.

namespace Gambit::Bindings
{
	// The Lua functions of a declared system.
	struct ScriptSystemFunctions
	{
		sol::function OnCreate; // may be empty
		sol::function OnUpdate;
		sol::function OnDestroy; // may be empty
	};

	// What an engine's scripts declared: the public descriptions plus the Lua values behind them (parallel vectors).
	struct ScriptDeclarations
	{
		std::vector<ScriptComponentDeclaration> Components;
		std::vector<ScriptSystemDeclaration> Systems;
		std::vector<ScriptSystemFunctions> SystemFunctions;
		std::vector<ScriptTestDeclaration> Tests;
		std::vector<sol::table> TestDefinitions;
	};

	// The declarations of an engine (ScriptEngine.cpp).
	[[nodiscard]] ScriptDeclarations& GetDeclarations(ScriptEngine& engine);

	// The registry key of the world object that systems receive (set by the World binder when the engine has a world).
	inline constexpr const char* WorldObjectKey = "Gambit.World";

	// ---- Components (ComponentBindings.cpp) ----

	// The component type with this name; throws a ScriptException naming it when there is none.
	[[nodiscard]] const ComponentInfo& FindComponentType(ScriptEngine& engine, std::string_view name);
	// A proxy of the entity's component (the entity must have it).
	[[nodiscard]] sol::object
	MakeComponentProxy(sol::state_view lua, ScriptEngine& engine, UUID entity, const ComponentInfo& info);
	// Writes the fields that the table `values` names into the entity's component, each merged into its current value
	// (a struct changes only the members it names, D-055). Nil changes nothing. Throws a ScriptException (before any
	// change) for an unknown field or a value that does not fit, and for engine-managed components.
	void
	AssignComponentFields(ScriptEngine& engine, Entity entity, const ComponentInfo& info, const sol::object& values);

	// Converts a Lua value to a value of a field type (with `attributes` for enums, structs and asset types); `name`
	// names it in messages ("Spin.Speed") and `current` is the value it replaces (structs merge into it, D-055). Floats
	// must be finite and strings valid UTF-8. Throws a ScriptException.
	[[nodiscard]] FieldValue ConvertToField(
		ScriptEngine& engine, const sol::object& value, FieldType type, FieldType elementType,
		const FieldAttributes& attributes, std::string_view name, const FieldValue* current = nullptr);

	// ---- Entities (EntityBindings.cpp) ----

	[[nodiscard]] sol::object MakeEntityObject(sol::state_view lua, ScriptEngine& engine, UUID entity);
}
