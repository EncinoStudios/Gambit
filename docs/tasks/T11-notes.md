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

## Remaining
- Checkpoint 2: Component(...) declarations (type inference, explicit specs, fields ordered by name), System/Test
  declarations, ScriptSystem in the scheduler, Entity/World basics, component/struct/array proxies with bound vectors.
- Checkpoint 3: rest of the World/Entity API, events (script + reflected C++ event types), Input, Data, Log/Debug,
  Instantiate, LoadScene, StateMachine library module, ApiRegistry completeness test.
- Checkpoint 4: EditorContext project-open schema step + unregistering on close, CreateSimulationWorld scripting,
  CreateSimulationWorldFromFile, sim.run scriptErrors/loadedScenes, CLI round-trip test, docs/guides/Scripting.md,
  .claude/skills/add-script-api, 600-tick determinism test (slow), Clang + Release + Dist checks, ROADMAP -> review.

## Findings
- sol2 strips the class table from `call_constructor` arguments; class tables hold only `__name`, methods and properties
  live in the instance metatable (walk `next, getmetatable(instance)`), `pairs(ClassTable)` errors (sol2 `__pairs`).
- C++ code that pushes onto a Lua stack must `lua_checkstack` (recursive conversions overflowed the 20 guaranteed slots).
