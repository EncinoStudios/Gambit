# Gambit — Design Decisions Log

Each decision has an ID so code reviews, task briefs and comments can cite it (`D-012`).
Decisions are final unless a later entry supersedes them. Append new entries at the end.

## Foundations

**D-001 Language & build.** C++20, CMake ≥ 3.24, Ninja. Compilers: MSVC 2022, Clang ≥ 16, GCC ≥ 13,
AppleClang ≥ 15. Build configurations: `Debug`, `Release`, `Dist`. `Dist` defines `GB_DIST`, strips the
editor, ImGui debug UI and asserts; it is what `gambit export` ships. Warnings: `-Wall -Wextra -Wpedantic`
(`/W4` on MSVC), warnings-as-errors for engine code (`GB_WARNINGS_AS_ERRORS`, ON in CI), never for ThirdParty.

**D-002 Repository layout (Hazel-like).**
```
Gambit/            engine static library   (Gambit/src/Gambit/<Module>/..., Gambit/Shaders/)
GambitEditor/      editor executable (ImGui), never shipped
GambitRuntime/     player executable used by export
GambitCLI/         `gambit` command-line tool + MCP server (headless authoring, tests, export)
GambitTests/       doctest unit/integration tests (+ golden images)
Projects/          FeatureTest project + demo projects (game content only, no C++)
ThirdParty/        git submodules pinned to release tags/commits (+ ThirdParty/CMakeLists.txt)
scripts/           quiet build/test/format scripts (Python, cross-platform)
docs/              ROADMAP, DECISIONS, tasks/, guides/, pipeline/
.claude/skills/    development skills for agents
```

**D-003 Dependencies** (submodules, pinned): glfw, NVRHI (+ its Vulkan-Headers), glm, imgui (docking),
ImGuizmo, EnTT v3.16, JoltPhysics v5.x, Lua 5.4.x, sol2 v3.5, miniaudio, stb, cgltf, nlohmann/json,
doctest, glslang (host shader compiler, built from source), nanosvg, earcut.hpp, cpp-httplib, Monocypher, lz4, xxHash.
No spdlog/fmt: logging uses `std::format`. Adding a dependency requires a DECISIONS entry (license must be
MIT/BSD/zlib/Apache-2.0/CC0/Unlicense/public domain or similarly permissive).

**D-004 Code style = Hazel.** See AGENTS.md §Style. Enforced by `.clang-format` + `scripts/format.py --check`
+ review. Namespace `Gambit`, macros prefixed `GB_`, precompiled header `gbpch.h`.

**D-005 Testing.** doctest. `GambitTests` executable registered with CTest. Every engine feature needs unit
tests; GPU tests run on any Vulkan device (lavapipe in CI/headless) and are in the doctest test suite `gpu` so they can be
skipped with `GB_TEST_NO_GPU=1`. Golden-image tests compare with per-pixel tolerance. Game-level behaviour
tests are written in Lua inside projects (D-041) and run with `gambit test`.

**D-006 Quiet tooling.** `scripts/build.py`, `scripts/test.py`, `scripts/format.py`, `scripts/check.py` print
only errors/failures plus a one-line summary; full logs go to `build/logs/`.

## Core & determinism

**D-010 Logging/asserts.** `GB_CORE_INFO/WARN/ERROR`, `GB_INFO/...` (client), `GB_CORE_ASSERT/GB_ASSERT`
(stripped in Dist), `GB_VERIFY` (always on). Log sinks: console, file, in-memory ring buffer (editor console,
automation API).

**D-011 Determinism contract.** Simulation runs on a fixed timestep (default 60 Hz, project setting). Given
the same build, same content, same seed and same per-tick input log, the simulation state hash after every
tick is bit-identical. Cross-platform determinism is a design goal (no FMA contraction, deterministic math
functions in simulation, Jolt cross-platform determinism), verified only on CI platforms.
- Compile flags: `-ffp-contract=off` (GCC/Clang), `/fp:precise` (MSVC); never `-ffast-math`.
- Simulation code must not use `std::sin/cos/tan/atan2/exp/log/pow` directly; use `Gambit::DMath` (deterministic
  implementations). Rendering/audio/editor may use std math.
- Simulation reads time only via `TimeStep`/tick counter; wall clock is forbidden in simulation.
- All randomness goes through `Gambit::Random` (PCG32) owned by the World; seeded from the scene seed.
- Single-threaded simulation system execution in a fixed, explicit order. Jolt runs with its job system but
  is deterministic by design.
- Entity creation/destruction during systems is deferred to a command buffer flushed at fixed points.
- Iteration order over entities is the EnTT pool order, which is deterministic for a deterministic operation
  sequence. Never iterate `std::unordered_map` keyed by pointers in simulation.

**D-012 Input.** Raw device input is sampled once per simulation tick into an `InputFrame` (key bitset, mouse
buttons, mouse delta quantized to 1/64 px, gamepad axes quantized to int16). Gameplay reads actions from the
project's `InputActions.json` mapping applied to the `InputFrame`. `InputLog` = seed + scene + frames;
replay = feed frames instead of devices.

**D-013 State hashing.** `World::ComputeStateHash()` = XXH64-style hash over every reflected component field
of every entity (in deterministic order) + RNG state + tick. Used by determinism tests and replay validation.

**D-014 Virtual file system.** All runtime file access goes through `VFS` with mounts: `DirectoryMount`
(editor/dev), `PackMount` (exported `.gpak`), `RemoteMount` (HTTP content server, D-071). Paths are
project-relative with forward slashes (e.g. `Assets/Meshes/Crate.glb`).

## ECS

**D-020 Strict ECS.** Entities are IDs. Components are plain data structs: public fields, default member
initializers, no virtual functions, no logic beyond trivial accessors. All behaviour lives in Systems (C++ or
Lua). Engine core services (renderer, asset manager, physics backend, audio device) are not ECS and may be
classes with behaviour.

**D-021 EnTT** behind a `World` (aka Scene) wrapper and an `Entity` handle. Every entity has `IDComponent`
(64-bit UUID) and `TagComponent` (name). Hierarchy: `RelationshipComponent` (parent UUID + ordered children).
`TransformComponent` (local TRS) → `WorldTransformComponent` computed by `TransformSystem`.

**D-022 Reflection.** A single `TypeRegistry` describes every component (name, fields, field types,
attributes: range, tooltip, color, hidden, asset type, enum names). It drives JSON serialization, the editor
inspector, Lua access, CLI/MCP get/set, the bulk (spreadsheet) editor and state hashing. Registering a
component = one registration function in its module; nothing else should need per-component code.

**D-023 Script-defined components.** Lua may declare data-only components with a schema (fields + defaults).
They are stored in EnTT named storages of a generic `DynamicComponent` and are first-class in the
TypeRegistry (serialized, inspectable, hashable).

**D-024 System scheduling.** Fixed phases per tick: `Input → PreUpdate → Update → PrePhysics → Physics →
PostPhysics → Animation → LateUpdate → Transform → PreRender(extraction)`. Systems declare phase and integer
order; ties broken by registration name. Editor (non-play) mode runs only the editor-safe subset (transform,
animation preview, render extraction).

**D-025 Serialization.** Scenes (`.gscene`), prefabs (`.gprefab`), materials (`.gmat`), project (`.gproject`),
input actions, settings: JSON (nlohmann), stable key order, 2-space indent, UUIDs as strings
(`"0x1A2B..."`) to stay diff- and AI-friendly. Every format carries `"Version"` with migration code.

**D-026 Prefabs.** A prefab is a serialized entity subtree. Instances carry `PrefabInstanceComponent`
(prefab handle + source entity UUID). Spawning at runtime creates deterministic UUIDs from the world RNG.

**D-027 Bulk/spreadsheet content.** (a) Data tables: CSV files (`Data/*.csv`, first row = column names, first
column = row id) loaded as `DataTable` assets; components reference rows via `DataRefComponent`
(table + row id) and scripts read `Data.Get("Weapons", "pistol").Damage`. (b) Bulk component editing: editor
panel + CLI command listing every entity with component X in a scene as a table (rows = entities,
columns = fields), editable and exportable/importable as CSV. Dialogue is a data table too.

## Rendering

**D-030 RHI.** NVRHI, Vulkan backend on all platforms (MoltenVK on macOS, via portability enumeration).
Vulkan is loaded dynamically (Vulkan-Hpp dynamic dispatcher); NVRHI's bundled Vulkan-Headers are used.
`GraphicsDevice` supports windowed (GLFW surface + swapchain) and headless (offscreen target) modes.
Validation layers on in Debug when available.

**D-031 Shaders.** GLSL 460 in `Gambit/Shaders/*.glsl`, compiled at build time to SPIR-V with glslang built
from source, embedded into the engine binary as generated C++ arrays (no loose shader files in exports).
Binding convention: set 0..N explicit `layout(set, binding)`; NVRHI Vulkan binding offsets set to zero, every
binding slot unique per set.

**D-032 Frame pipeline (3D).** CSM shadow pass → depth/normal prepass → SSAO (GTAO-style + bilateral
blur) → forward PBR opaque (metal/rough, IBL, punctual lights, soft shadows: PCF with PCSS-style blocker
search) → skybox → transparent → particles → bloom → tonemap (ACES fitted + Khronos PBR Neutral, exposure)
→ 2D/UI overlay → (editor: ImGui). Scene color RGBA16F. Render extraction copies ECS data into a
`RenderScene` so rendering never touches the World directly.

**D-033 IBL.** Equirectangular `.hdr` (Polyhaven, CC0) → cubemap → irradiance (SH9 or small cube) +
GGX prefiltered mip chain + BRDF LUT, generated by compute shaders at load and cached in memory.
Polyhaven is not reachable from the dev sandbox; CC0 Polyhaven HDRIs mirrored in the three.js repo are used
and `scripts/fetch_hdri.py` fetches by Polyhaven asset name when the API is reachable.

**D-034 2D.** `Renderer2D` batches quads/sprites/circles/lines and tessellated vector shapes. Pixel-perfect
mode: orthographic camera with integer scale to a reference resolution, texel snapping, nearest sampling.
Vector artwork: SVG via nanosvg → adaptive flattening → earcut fill triangulation + stroke expansion,
re-tessellated per scale bucket. Text: stb_truetype SDF atlas.

**D-035 Game UI** is ECS-based (`UIRectComponent` anchors/pivot, `UITextComponent`, `UIImageComponent`,
`UIVectorComponent`, `UIButtonComponent`) rendered by Renderer2D in screen space. ImGui is editor/debug only.

## Physics, animation, audio

**D-040 Physics = Jolt** with `JPH_CROSS_PLATFORM_DETERMINISTIC`, `JPH_DOUBLE_PRECISION` off. Components:
`RigidBodyComponent`, `BoxCollider`, `SphereCollider`, `CapsuleCollider`, `MeshCollider` (static),
`ConvexHullCollider`, `CharacterControllerComponent` (Jolt CharacterVirtual), joints (`FixedJoint`,
`HingeJoint`). Contact events are written as data into a per-tick event list readable by systems/Lua.
Physics steps exactly once per simulation tick.

**D-041 Scripting = Lua 5.4 + sol2.** Lua compiled from source with `luai_makeseed` fixed (deterministic
string hashing), `math.random` replaced by the world RNG, `os`/`io`/`debug` libraries removed, only text
chunks loaded. Scripts declare components (`Component`), systems (`System`) and tests (`Test`); they never
attach behaviour to entities. Lua test blocks run headless via `gambit test` with JUnit XML output.
Lua-language-server type stubs are generated from the TypeRegistry + API registry (`gambit api-stubs`).

**D-042 Animation.** glTF skins/clips. `AnimatorComponent` with layers (clip, time, speed, weight, blend mode
override|additive, bone mask), crossfades; evaluated in the `Animation` phase on the fixed tick. FK via pose
access from systems/Lua; IK: analytic two-bone, FABRIK chain, look-at. Morph targets with GPU blending;
baked cloth = morph-target or vertex-cache animation exported from DCC (glTF is the contract);
`gambit bake-cloth` offline PBD baker for meshes without DCC. GPU skinning.

**D-043 Audio = miniaudio** engine; `AudioSourceComponent`, `AudioListenerComponent`; playback triggered by
simulation, output-only (never feeds back into simulation). Null backend in headless/tests.

## Editor, automation, comments

**D-050 Editor** = standalone `GambitEditor` executable (ImGui docking + ImGuizmo). Every mutating editor
action is an undoable `Command` executed through the shared `CommandRegistry`.

**D-051 Automation.** `CommandRegistry` (engine library, `Gambit/Automation`) holds named commands with JSON
argument schemas, executed against an `EditorContext` (project, open scenes, selection, undo stack). Exposed
via: the `gambit` CLI (headless), `gambit mcp` (MCP stdio server for AI agents), and the running editor's
localhost JSON-RPC endpoint (`--automation-port`). Capabilities include project/scene/entity/component/asset
CRUD, script authoring, play/step/pause, screenshots (headless render), state queries, log reads, tests,
export and comments — enough for an agent to build a complete game without human input.

**D-052 Comments.** One JSON file per thread in `<Project>/Comments/`. Target kinds: `entity`
(scene + UUID), `component`, `asset`, `file` (+ line), `feature` (free tag), `position` (scene + world pos).
Author (user or agent name), status open/resolved, replies, timestamps. Visible in editor (panel, viewport
pins, inspector badge), CLI and MCP. Excluded from exports.

## Export & content delivery

**D-070 Export.** `gambit export` cooks assets, compiles Lua to text chunks (no bytecode), builds a `.gpak`
(TOC + LZ4 blocks + BLAKE2b hashes, Ed25519-signed manifest via Monocypher) and copies the `Dist`
`GambitRuntime` renamed to the game. Exports target the host platform.

**D-071 Remote content.** `--remote <url>` exports a client without the content pack: it holds only the
runtime, the server URL and the publisher public key. At runtime `RemoteMount` fetches the signed manifest and
content blocks over HTTP(S) (cpp-httplib; HTTPS when OpenSSL is available), verifies signatures/hashes, keeps
content in memory only (optionally an encrypted on-disk cache), and re-checks the manifest version for live
updates. `gambit serve` hosts packs for development.

## Content & licensing

**D-080 Third-party content.** Only CC0 or CC-BY 4.0 assets (HDRIs, models, fonts under OFL/Apache-2.0) may be
committed. Each one is listed with source URL, author and license in `ATTRIBUTION.md` at the repo root. Prefer
procedurally generated content (MeshFactory, generator scripts in `tools/`) for tests and demos; keep committed
binaries small (≤ ~4 MB each, 1k HDRIs).
