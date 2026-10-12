# T01 notes (implementation report details)

## Deviations from the brief

1. **Vulkan-Headers is an extra submodule.** The brief says NVRHI is added "recursive — it carries Vulkan-Headers".
   NVRHI `main` (6b96fb03e075) has an empty `.gitmodules`; its CMake instead fetches Vulkan-Headers `v1.4.352` with
   `FetchContent` at configure time, which would make builds depend on the network. Vulkan-Headers is therefore pinned
   as `ThirdParty/Vulkan-Headers` at `v1.4.352` (the tag NVRHI expects). It is not wired into CMake yet; T07 creates
   `Vulkan::Headers` from it (NVRHI accepts an existing target) and sets `NVRHI_FETCH_VULKAN_HEADERS=OFF`.
   D-003 already lists "NVRHI (+ its Vulkan-Headers)", so no new DECISIONS entry was added.
2. **Every other pinned ref matches the brief.** All requested tags exist upstream. Branch-tracking pins resolved to:
   imgui `docking` 778992face67, ImGuizmo `master` 105e4fde45e4, stb `master` 2c980bb59875, nanosvg `master`
   239e102ec2c6, NVRHI `main` 6b96fb03e075 (state of 2026-10-12). Monocypher comes from its author's GitHub repository
   `loupvaillant/Monocypher` (tag 4.0.3). The full table with licenses is `ThirdParty/README.md`.
3. **`shallow = true` in `.gitmodules` is not honored by `git submodule update`** (git 2.43 clones full history, 2 min
   and 700 MB). It is set as requested; `git submodule update --init --recursive --depth 1` gives the shallow
   checkout (50 s, 95 MB) and is what `actions/checkout` does in CI. Docs recommend `--depth 1`, and AGENTS.md
   section 4 was updated to show it.
4. **`GB_WARNINGS_AS_ERRORS` defaults to ON** (D-001 says "ON in CI"). A plain `cmake` run is then already held to the
   definition of done in AGENTS.md section 5. The presets and `build.py` also set it explicitly; `build.py --no-werror`
   turns it off.
5. **Warnings and determinism flags apply to all first-party targets** (engine, CLI, runtime, editor, tests), not only
   the `Gambit` library, via `gambit_configure_target()`. Test code that computes expected floating-point values must
   see the same contraction rules as the engine.
6. **GPU tests are selected by doctest test suite `gpu`** (AGENTS.md section 4), not by a `[gpu]` tag as D-005's text
   says. `GB_TEST_NO_GPU=1` (or `scripts/test.py --no-gpu`) excludes the suite in the custom `main`. There are no GPU
   tests yet.

## Added beyond the brief (all small and used)

- `Core/LogSinks.h/.cpp` (console, file, ring-buffer sinks) and `Core/Assert.cpp`: keeps `Log.h` light and gives the
  assert handler a home.
- `cmake/GambitConfigurations.cmake`: the Debug/Release/Dist configuration setup.
- `GambitCLICore` static library (everything of the CLI except `main`) so the dispatcher is unit tested; CTest also
  runs the real binary (`Cli.Version`, `Cli.Help`, `Cli.UnknownCommand`) and the runtime/editor mains.
- `scripts/_common.py` shared by the four scripts; `GambitTests/src/TestSupport.*` (temp dirs, stderr capture).
- `GambitTests/src/ThirdParty/ThirdPartyTests.cpp`: one smoke test per wired dependency that pins its version and
  checks known-answer vectors (xxHash, BLAKE2b, RFC 8032 Ed25519, LZ4 round trip), so wiring or version drift fails CI.

## Design choices worth knowing

- **Logging.** Two global loggers, `GAMBIT` (`GB_CORE_*`) and `APP` (`GB_*`). Sinks are shared, thread-safe objects;
  a logger serializes delivery under its own mutex, so sinks must not log or assert from `Write` (documented on
  `LogSink`). The macros test the level before evaluating arguments. Logging never throws (a throwing formatter is
  reported as an error record). The loggers are never destroyed; an `atexit` hook detaches sinks so the log file is
  flushed even when `Log::Shutdown` is forgotten. `LogConfig::ConsoleTarget` lets tools whose stdout is data (the CLI,
  later `gambit mcp`) log to stderr; the CLI does.
- **Ring buffer** assigns its own 1-based `Sequence` numbers under its lock, so `SnapshotAfter(lastSeen)` is a safe
  incremental poll for the editor console and the automation API.
- **Asserts.** `GB_CORE_ASSERT/GB_ASSERT` vanish in Dist (the condition is type-checked but not evaluated);
  `GB_CORE_VERIFY/GB_VERIFY` always run. Failures go to a replaceable `AssertHandler` (default: log critical, flush,
  then trap into the debugger). A handler returning `Continue` exists for tests only. `Result::Value()` on an error
  and `GetError()` on a success are VERIFY failures; they trap in every configuration, so there is no unit test that
  triggers them (it would need a death test).
- **Result<T>** is `std::variant<T, Error>`, `[[nodiscard]]`, implicitly convertible from `T` and `Error`;
  `Result<void>` is a success by default. `Error` is `{ Message, Code }`.
- **CLI dispatcher** (`gambit`): `Command{ Name, Summary, Usage, Aliases, Handler }`; `help` is built in, `version` is
  registered in `RegisterBuiltinCommands` together with its `--version` / `-V` aliases. Exit codes: 0 success, 1 command
  failure, 2 usage error. Later tasks add commands only in `RegisterBuiltinCommands` (or a function it calls).
- **Build.** Sources are collected with `GLOB_RECURSE ... CONFIGURE_DEPENDS` so parallel tasks do not conflict on CMake
  lists. `Dist` is Release flags plus `GB_DIST` and no editor (single-config generators; the presets use Ninja).
  `build.py` recreates the build directory when `CC`/`CXX` or the configure options change.
- **clang-format 18** is the reference. CI checks formatting only in the Linux GCC Debug job; other jobs use
  `check.py --skip-format` because clang-format output differs slightly between versions.

## Verified

- GCC 13.3 and Clang 18.1 on Linux x86-64: Debug, Release and Dist build with zero warnings and pass all tests.
- From a fresh copy with shallow submodules (`--depth 1`), `scripts/check.py` passes with both compilers.
- AddressSanitizer + UBSan and ThreadSanitizer builds of `GambitTests` are clean.

## Not verified (no access in this environment)

- Windows/MSVC and macOS/AppleClang builds, and the CI workflow itself. Points most likely to need a touch-up:
  MSVC flags (`/Zc:preprocessor`, `/Z7` via `CMAKE_MSVC_DEBUG_INFORMATION_FORMAT`), `<format>` with AppleClang 15
  (deployment target is set to macOS 13.3 for this reason), `ilammy/msvc-dev-cmd` plus `pip install ninja` on Windows,
  and the `_isatty` / `SetConsoleMode` code in `LogSinks.cpp`.

## Open points for the orchestrator

- T07 needs a decision on glslang's SPIRV-Tools / SPIRV-Headers dependency: pin two more submodules at the revisions in
  `ThirdParty/glslang/known_good.json`, or build glslang with `ENABLE_OPT=OFF`. Fetching them with
  `update_glslang_sources.py` would modify the submodule, which AGENTS.md forbids.
- Optionally pin Vulkan-Headers to v1.4.363 (the Vulkan SDK that glslang's tag belongs to) instead of v1.4.352
  (what NVRHI main is tested with). Kept at v1.4.352 as the conservative choice.
