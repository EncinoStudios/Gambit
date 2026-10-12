---
name: code-review
description: Review checklist for Gambit changes. Use before committing your own work and when reviewing someone else's change, to verify style, error handling, determinism rules, strict ECS, reflection registration, tests, documentation and commit hygiene against AGENTS.md and docs/DECISIONS.md.
---

# Code review checklist

Review the diff (`git diff` and `git status` for new files) against this list, then run `python3 scripts/check.py`
(and `--config Release`, and `CC=clang CXX=clang++` when the change is non-trivial). Report findings as
file:line with the rule and a suggested fix; fix your own findings before asking for an independent review.
Anything outside the change's scope goes into the report or a comment (D-052), not into the diff.

## 1. Scope and correctness
- [ ] The change does what the task brief says, nothing more (no drive-by refactors, no speculative features).
- [ ] Edge cases handled: empty input, null, zero capacity, overflow, concurrent use where the type claims thread safety.
- [ ] No dead code, no commented-out code, no `TODO` stubs posing as features, no leftover debug output.

## 2. Style (AGENTS.md section 2, D-004)
- [ ] Names follow the table: PascalCase types/functions/enum values/constants, `m_` members, `s_` statics, camelCase
      locals and parameters, `GB_` macros, PascalCase files matching the main type.
- [ ] Namespace `Gambit`, contents indented; Allman braces; tabs; `Type* ptr` / `Type& ref`; `#pragma once`.
- [ ] Include order: `gbpch.h` first in engine `.cpp` files, own header, Gambit headers, third-party, std; includes
      are `"Gambit/Module/File.h"` paths; no includes into another module's `Internal/` folder.
- [ ] `python3 scripts/format.py --check` passes. No `using namespace` in headers.
- [ ] `Ref`/`Scope` via `CreateRef`/`CreateScope`; no raw `new`/`delete` outside allocators.
- [ ] Comments explain why, not what; each public class has a one-line doc comment.

## 3. Error handling
- [ ] Runtime failures (bad content, IO, parse) return `Result<T>` or `bool` plus a log line; nothing aborts on bad content.
- [ ] Programmer errors use `GB_CORE_ASSERT`; invariants that must hold in shipping builds use `GB_CORE_VERIFY`.
- [ ] Inputs are validated at module boundaries (files, JSON, Lua, command arguments).
- [ ] Third-party exceptions (nlohmann, sol2, std::filesystem) are caught at the boundary and converted.
- [ ] `[[nodiscard]]` on fallible returns; no ignored `Result`.

## 4. Determinism (D-011)
Applies to everything that runs in the simulation (not rendering, audio or editor code).
- [ ] Fixed timestep and tick counter only; no wall clock (`std::chrono::*_clock`, `time()`).
- [ ] Randomness only through `Gambit::Random`; no `rand`, `std::random_device`, `std::mt19937` in simulation.
- [ ] `Gambit::DMath` instead of `std::sin/cos/tan/atan2/exp/log/pow`.
- [ ] No iteration over `std::unordered_map`/`set` keyed by pointers; no uninitialized reads; no reliance on
      address values or thread timing; no threads except Jolt's job system.
- [ ] No `-ffast-math`, no new floating-point flags; entity creation/destruction deferred to the command buffer.
- [ ] A determinism test exists or was extended when simulation code changed.

## 5. Architecture
- [ ] Strict ECS (D-020): components are plain data, behaviour lives in systems.
- [ ] New components are registered in the TypeRegistry (D-022); no hand-written per-component serializer or inspector.
- [ ] Renderer never reads the `World`; data flows through render extraction (D-032).
- [ ] Assets are referenced by `AssetHandle`; all file access goes through `VFS` (D-014).
- [ ] New editor features expose a `Command` in the `CommandRegistry` first (D-051).
- [ ] New dependencies have a DECISIONS entry and a permissive license (D-003); `ThirdParty/` sources untouched.

## 6. Tests
- [ ] New behaviour has doctest tests named `"<Module>: <behaviour>"`, mirrored under `GambitTests/src/<Module>/`.
- [ ] Gameplay-facing features also have Lua tests in `Projects/FeatureTest`; bug fixes have a regression test.
- [ ] Tests are fast (<100 ms; heavier ones in suite `"slow"`), deterministic, independent of order and time zone,
      and clean up files they create. GPU tests are in suite `"gpu"`.
- [ ] Tests pass in Debug and Release; assert-related tests are guarded for Dist (`GB_DIST`).

## 7. Build and CI
- [ ] Zero warnings with GCC and Clang (and MSVC where reachable) in Debug and Release; no warnings disabled to get green.
- [ ] CMake changes keep third-party targets warning-free and first-party targets on `gambit_configure_target()`.
- [ ] No build output, logs or editor files committed.

## 8. Documentation and commit hygiene
- [ ] Public API changes are reflected in docs and, once they exist, Lua stubs (`gambit api-stubs`).
- [ ] `docs/ROADMAP.md` status is updated only when the task is done, reviewed and pushed.
- [ ] Commit subject is imperative, at most 72 characters; the body explains what and why and cites task and decision
      IDs (`T05`, `D-013`); intentionally regenerated golden images are called out.
- [ ] No history rewrites, no force pushes.
