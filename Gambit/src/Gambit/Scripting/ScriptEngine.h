#pragma once

#include "Gambit/Core/Base.h"
#include "Gambit/Core/Result.h"
#include "Gambit/ECS/System.h"
#include "Gambit/Input/InputActions.h"
#include "Gambit/Scripting/ApiRegistry.h"
#include "Gambit/Scripting/ScriptValue.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct lua_State;

namespace Gambit
{
	class InputSource;
	class InputState;
	class Random;
	class TypeRegistry;
	class VFS;
	class World;

	namespace Internal
	{
		struct ScriptEngineState;
	}

	// What a script engine is for. Simulation: the engine belongs to a world and adds the declared systems to it.
	// Schema: the engine only registers the declared components (D-023) so that tools can load and save scenes that use
	// them (the project-open step of every host); systems and tests are recorded, never run.
	enum class ScriptMode : uint8_t
	{
		Schema,
		Simulation
	};

	// Guards against scripts that run away (D-041): exceeding either fails the script call with an error.
	struct ScriptLimits
	{
		// Bytes the Lua state may allocate in total.
		size_t MemoryBytes = size_t{ 256 } << 20;
		// Lua VM instructions one call from the engine may run (loading a script, one system update, an evaluation).
		uint64_t InstructionsPerCall = 50'000'000;
	};

	// A line of a script.
	struct ScriptLocation
	{
		std::string File; // "Scripts/Spin.lua"; empty when unknown
		int32_t Line = 0; // 0 when unknown
	};

	// A component a script declared with Component(name, fields) (D-023); the schema itself is in the TypeRegistry.
	struct ScriptComponentDeclaration
	{
		std::string Name;
		ScriptLocation Location;
	};

	// A system a script declared with System(name, { Phase, Order, OnCreate, OnUpdate, OnDestroy, RunInEditMode }).
	struct ScriptSystemDeclaration
	{
		std::string Name;
		SystemPhase Phase = SystemPhase::Update;
		int32_t Order = 0;
		bool RunInEditMode = false;
		ScriptLocation Location;
	};

	// A test a script declared with Test(name, { Run, Scene, Tags, Timeout, Seed }); the test runner (T12) runs it.
	struct ScriptTestDeclaration
	{
		std::string Name;
		std::string Scene; // empty: an empty world
		std::vector<std::string> Tags;
		std::optional<uint64_t> Timeout; // ticks
		std::optional<uint64_t> Seed;
		ScriptLocation Location;
	};

	// A script failure: where and what. Errors are logged when they happen and kept by the engine.
	struct ScriptError
	{
		// With the location of the error first: "Scripts/Spin.lua:12: attempt to index a nil value (local 'spin')".
		std::string Message;
		std::string File;    // "Scripts/Spin.lua"; empty when no script location is known
		int32_t Line = 0;    // 0 when no line is known
		std::string Context; // what the engine was running: "loading Scripts/Spin.lua", "SpinSystem.OnUpdate"
		std::string Traceback;
		uint64_t Tick = 0; // the world's tick (0 without a world)
	};

	struct ScriptEngineSpecification
	{
		ScriptMode Mode = ScriptMode::Simulation;
		// The world of a simulation engine; it must outlive the engine. Null in schema mode.
		World* Scene = nullptr;
		// Where declared components are registered: the world's registry when null and there is a world, else
		// TypeRegistry::Get(). Must outlive the engine.
		TypeRegistry* Types = nullptr;
		// The project's files: the scripts and the modules `require` finds. Null: no project scripts (LoadScript still
		// runs source text).
		const VFS* Files = nullptr;
		std::string ScriptsDirectory = "Scripts";
		// The project's input mapping, which Lua's Input functions read; null: no actions.
		Ref<const InputActions> Actions;
		ScriptLimits Limits;
		// The API to bind; ApiRegistry::Get() when null. Must outlive the engine.
		const ApiRegistry* Api = nullptr;
	};

	// The Lua 5.4 runtime of one world (D-041): a sandboxed Lua state that loads the project's scripts, which declare
	// components (D-023), systems and tests. Each simulation world gets its own engine, created for every play
	// session and test and destroyed with the world, so no state survives between runs except what lives in
	// components.
	//
	// The sandbox is deterministic (D-011): only text chunks load; the base library without dofile, loadfile and
	// collectgarbage, plus coroutine, string (without dump), table, utf8 and math, whose random functions draw from
	// the world's Random and whose transcendental functions are DMath; no io, os, debug or package; print and warn go
	// to the log; string hashing and table.sort use fixed seeds; metatables with __gc or __mode are refused. `require`
	// finds modules in the project's Scripts directory and the engine's Lua library ("Gambit.<Name>"). Once the scripts
	// are loaded the globals are frozen: assigning one raises an error, since gameplay state belongs in components.
	//
	// Every call into Lua is protected: an error never escapes into the engine. It is recorded with its location
	// (GetErrors), logged, and returned as a Result. A memory limit and an instruction watchdog per call stop runaway
	// scripts the same way.
	class ScriptEngine : public std::enable_shared_from_this<ScriptEngine>
	{
		struct ConstructionKey
		{
			explicit ConstructionKey() = default;
		};

	public:
		// Creates the Lua state, the sandbox and the API of every binder of the specification's ApiRegistry.
		[[nodiscard]] static Result<Ref<ScriptEngine>> Create(ScriptEngineSpecification specification);

		// Adds a module's Lua API to every script engine created from now on (ApiRegistry::Get().RegisterBinder).
		static void RegisterBinder(std::string_view name, ScriptBinderFunction bind);

		ScriptEngine(ConstructionKey, ScriptEngineSpecification specification);
		~ScriptEngine();
		ScriptEngine(const ScriptEngine&) = delete;
		ScriptEngine& operator=(const ScriptEngine&) = delete;

		// ---- Loading ----

		// Runs every <ScriptsDirectory>/**/*.lua of the specification's files in path order (modules that a script
		// required earlier have run already and are skipped). Every script is attempted; fails when any failed, after
		// recording each failure.
		[[nodiscard]] Result<void> LoadScripts();
		// Runs `source` as the script file `path` (its name in error messages). The module of a path below the scripts
		// directory is marked as loaded.
		[[nodiscard]] Result<void> LoadScript(std::string_view path, std::string_view source);
		// Freezes the globals: from now on assigning a global raises an error. Loading more scripts fails afterwards.
		void FinishLoading();
		[[nodiscard]] bool IsLoadingFinished() const noexcept;

		// Runs `code` as a chunk and returns its first result as plain data (nil without one). The chunk receives the
		// world object as its argument (`local world = ...`) when the engine has a world. Errors are returned, not
		// recorded.
		[[nodiscard]] Result<ScriptValue> Evaluate(std::string_view code, std::string_view chunkName = "=(evaluate)");

		// What the loaded scripts declared, in declaration order.
		[[nodiscard]] std::span<const ScriptComponentDeclaration> GetComponents() const noexcept;
		[[nodiscard]] std::span<const ScriptSystemDeclaration> GetSystems() const noexcept;
		[[nodiscard]] std::span<const ScriptTestDeclaration> GetTests() const noexcept;

		// ---- Simulation ----

		// Adds the engine's systems to its world (simulation mode, after FinishLoading): "ScriptRuntime" (first of the
		// Input phase: it owns the engine for the world and feeds the tick's input) and one ScriptSystem per declared
		// system, whose OnCreate runs now. The world then keeps the engine alive. Fails when a system cannot be added
		// (its name is taken); OnCreate errors disable their system and are recorded, they do not fail this.
		[[nodiscard]] Result<void> AttachToWorld();
		// The engine attached to `world`, null when there is none.
		[[nodiscard]] static ScriptEngine* Find(World& world);

		// Where the input of each Play tick comes from: ScriptRuntime samples it once per tick. Null (the default): no
		// input, every action released. Not owned; must outlive its use.
		void SetInputSource(InputSource* source) noexcept;
		[[nodiscard]] InputSource* GetInputSource() const noexcept;
		// The input of the current tick, as Lua's Input functions read it.
		[[nodiscard]] InputState& GetInput() noexcept;

		// ---- State ----

		[[nodiscard]] ScriptMode GetMode() const noexcept;
		// Null in schema mode.
		[[nodiscard]] World* GetWorld() const noexcept;
		[[nodiscard]] TypeRegistry& GetTypeRegistry() const noexcept;
		[[nodiscard]] const ScriptEngineSpecification& GetSpecification() const noexcept;
		// The world's random generator, or the engine's own (seed 0) without a world.
		[[nodiscard]] Random& GetRandom() noexcept;
		// Bytes the Lua state holds now.
		[[nodiscard]] size_t GetMemoryUsage() const noexcept;

		// Every error so far, oldest first.
		[[nodiscard]] std::span<const ScriptError> GetErrors() const noexcept;
		// Records and logs an error found by engine code that runs on behalf of scripts.
		void ReportError(ScriptError error);

		// The Lua state, for binders (Scripting/LuaBindings.h). Owned by the engine.
		[[nodiscard]] lua_State* GetLuaState() const noexcept;

	private:
		Scope<Internal::ScriptEngineState> m_State;
	};
}
