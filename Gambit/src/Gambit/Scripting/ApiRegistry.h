#pragma once

#include "Gambit/Core/Base.h"

#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Gambit
{
	class ScriptBindingContext;

	// What a documented name of the Lua API is.
	enum class ApiEntryKind : uint8_t
	{
		Function, // a global function or a function of a table: "Math.Clamp", "Vec3" (a constructor)
		Method,   // called with ':' on an object: "World:Spawn", "Entity:Get"
		Property, // read (and maybe written) with '.' on an object: "World.Tick", "Entity.Name", "Vec3.x"
		Constant, // a value: "Math.Pi", or a table of constants documented as a whole: "Key"
		Type,     // a type of object, with its operators: "Vec3", "Entity", "World"
		Table     // a table of functions: "Math", "Input"
	};

	[[nodiscard]] std::string_view ToString(ApiEntryKind kind) noexcept;

	// The documentation of one name of the Lua API (D-041), from which `gambit api-stubs` and `gambit api-docs` (T12)
	// generate the LuaLS annotations and the reference.
	struct ApiEntry
	{
		// Global: "print", "Component". In a table: "Math.Clamp". Method: "World:Spawn". Property: "Entity.Name".
		std::string Name;
		ApiEntryKind Kind = ApiEntryKind::Function;
		// LuaLS-style: "Math.Clamp(value: number, min: number, max: number) -> number"; the type for properties and
		// constants: "Entity.Name: string".
		std::string Signature;
		std::string Description;
		std::string Example; // Lua code
		std::string Module;  // the binder that documents it ("Core", "Physics")
	};

	// Adds a module's Lua API to a new script state (ScriptEngine::Create runs every binder).
	using ScriptBinderFunction = std::function<void(ScriptBindingContext& context)>;

	struct ScriptBinder
	{
		std::string Name;
		ScriptBinderFunction Bind;
	};

	// The Lua API of the engine (D-041): the binders that add each module's functions to every script state, and the
	// documentation of every name they add. The core API is registered here; later modules (physics, audio, animation,
	// UI, particles) register their binder and document their names in their own files, from RegisterEngineScriptApi.
	// A completeness test checks that every name a script can reach is documented and that every documented name
	// exists. Not thread safe: register at startup, before script engines are created.
	class ApiRegistry
	{
	public:
		ApiRegistry() = default;
		ApiRegistry(const ApiRegistry&) = delete;
		ApiRegistry& operator=(const ApiRegistry&) = delete;

		// The process-wide registry, with every engine module registered on first use.
		[[nodiscard]] static ApiRegistry& Get();

		// Binders run in registration order, so a module may use what earlier ones bound (the math types). Names are
		// unique (verified).
		void RegisterBinder(std::string_view name, ScriptBinderFunction bind);
		[[nodiscard]] std::span<const ScriptBinder> GetBinders() const noexcept { return m_Binders; }

		// Names are unique (verified).
		void Document(ApiEntry entry);
		// Documents `name` for the module of the binder registered last.
		void Document(
			std::string_view name, ApiEntryKind kind, std::string_view signature, std::string_view description,
			std::string_view example = {});

		[[nodiscard]] const ApiEntry* Find(std::string_view name) const;
		// Sorted by name (byte-wise).
		[[nodiscard]] std::vector<const ApiEntry*> GetEntries() const;
		[[nodiscard]] size_t GetEntryCount() const noexcept { return m_Entries.size(); }

	private:
		std::vector<ScriptBinder> m_Binders;
		std::map<std::string, ApiEntry, std::less<>> m_Entries;
	};

	// Registers every engine module's binder and documentation, in a fixed order (core first). ApiRegistry::Get calls
	// it; tests call it on their own registries.
	void RegisterEngineScriptApi(ApiRegistry& registry);
}
