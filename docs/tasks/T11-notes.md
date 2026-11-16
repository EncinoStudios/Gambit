# T11 notes (running log)

Checkpoints (from the brief): (1) Lua/sol2 build + sandboxed ScriptEngine (errors, memory cap, watchdog) + tests;
(2) Component/System declarations, reference proxies, ScriptSystem + tests; (3) core API + ApiRegistry + Lua library +
tests; (4) project-open schema registration in every host, commands, docs/guide/skill, determinism test.

## Done
- Checkpoint 1: `lua::lua` / `sol2::sol2` targets (ThirdParty/CMakeLists.txt), `Gambit/Scripting/LuaUser.h` (D-060),
  `ScriptEngine` (sandbox, require through the VFS and the embedded engine library, script discovery in path order, global
  freeze, protected calls with file:line + traceback, memory cap, instruction watchdog, Evaluate), `ApiRegistry` with the
  binder list and docs, math library on DMath/world Random, Vec2/3/4, Quat, Color, Mat4 and Math bindings, `ScriptValue`
  (plain data, also the future event payload), `MemoryMount` (Core), engine Lua library `SortedPairs`, `Table`
  (`Gambit/Lua`, embedded by `cmake/GambitLuaLibrary.cmake`).

- Checkpoint 2: `Component(...)` declarations (inferred and explicit field types, fields ordered by name), `System` and
  `Test` declarations (`ScriptEngine::GetComponents/GetSystems/GetTests`), schema mode, `ScriptSystem` +
  `ScriptRuntimeSystem` (`AttachToWorld`, `ScriptEngine::Find`), `Entity` and the first part of `World` (Spawn, Destroy,
  Find, FindByUUID, Query, QueryFirst, Count, Tick, Time, DeltaTime, Random), component/struct/array proxies by
  `FieldBinding` with bound vectors, `GetOptionalArgument`. Tests: DeclarationTests, ComponentProxyTests,
  WorldBindingsTests, ScriptSystemTests.

## Remaining
- Checkpoint 3: rest of the World API (Instantiate, LoadScene, Emit/Events incl. reflected C++ event types), Input, Data,
  Log/Debug, StateMachine library module, `Copy()` for bound math values, ApiRegistry docs for the component/struct/array
  proxies (ComponentBindings.cpp documents nothing yet) and the ApiRegistry completeness test.
- Checkpoint 4: EditorContext project-open schema step + unregistering on close, CreateSimulationWorld scripting,
  CreateSimulationWorldFromFile, sim.run scriptErrors/loadedScenes, CLI round-trip test, docs/guides/Scripting.md,
  .claude/skills/add-script-api, 600-tick determinism test (slow), Clang + Release + Dist checks, ROADMAP -> review.
- Follow-up to consider: the memory cap fails allocations inside C++ bindings too, where Lua's error longjmps over C++
  frames (leaks, skipped catch blocks). Safer: a soft limit checked by the watchdog hook (a normal Lua error at a safe
  point) plus a hard ceiling in the allocator for single huge allocations.

## Findings
- sol2 strips the class table from `call_constructor` arguments; class tables hold only `__name`, methods and properties
  live in the instance metatable (walk `next, getmetatable(instance)`), `pairs(ClassTable)` errors (sol2 `__pairs`).
- C++ code that pushes onto a Lua stack must `lua_checkstack` (recursive conversions overflowed the 20 guaranteed slots).
- `sol::optional<sol::object>::value_or(sol::lua_nil)` yields an object without a Lua state: `get_type()` on it crashes.
  Take a plain `sol::object` parameter (an absent argument is a nil object with a state).
- `sol::optional<T>` parameters take a value of the wrong type for a missing one: use `GetOptionalArgument<T>`
  (LuaBindings.h) with a `sol::object` parameter.
- sol2's `is<int64_t>()` checks number precision only with `SOL_ALL_SAFETIES_ON` (not in Dist): binding code that must
  tell integers from floats uses `lua_isinteger` / `lua_tointegerx` itself (ComponentBindings.cpp `ToInteger`).
- Floats print NaN as `-nan` on x86: format non-finite numbers by hand in messages (`DescribeNonFinite`).
