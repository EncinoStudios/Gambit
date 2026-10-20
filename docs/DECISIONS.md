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

## Addenda (from implementation)

**D-015 DMath = musl port.** `Gambit::DMath` is a bit-exact port of musl libm 1.2.6 (MIT); NaN results are
canonicalized. Reference outputs live in `GambitTests/.../DMathReference.inl` (regenerate only with
`GB_UPDATE_GOLDEN=1` and a justification in the commit message).

**D-016 Input binding resolution.** When several bindings feed one action, the binding with the largest
magnitude wins (no summing); `InputActions.json` rejects unknown properties (strict, agent-friendly errors).

**D-017 World events are double-buffered.** At the start of tick N the event queues swap: `Events<T>().View()`
returns the complete set emitted during tick N-1 (every system sees each event exactly once, independent of
phase order); `ViewCurrent()` returns events emitted so far in tick N for same-tick consumers (e.g. PostPhysics
reading collisions). Both buffers are part of `World::Copy` and of the state hash.

**D-018 ECS details (T04).** Worlds hold a pointer to a `TypeRegistry` (global default; tests may use their
own). `TransformComponent` is optional. Disabled entities carry a derived `InactiveComponent` that systems skip.
Root ordering is preserved for authoring but not hashed. The state hash includes simulation time.

**D-019 Reflected structs, plain UUIDs, internal and derived components (T05).** `FieldType::Struct` (and arrays of
structs): a struct registered with `TypeRegistry::RegisterStruct<T>` has `FieldInfo`s like a component; its value is a
recursive `StructValue` (one `FieldValue` per field), hashed field by field with the same bytes in C++ and script
components. `FieldType::UUID` (`Attr::PlainUUID`) is an identifier that refers to nothing (never remapped). Generic
reflection never writes `Internal` components (`GetRaw(Entity)`, `SetFieldValue`, `CopyTo` and `ForEach` refuse them;
`World::Copy` is the one exception). `ComponentFlags::Derived` components (`WorldTransform`) are neither hashed (not even
their presence) nor stored. The hash seed became `Gambit.World.StateHash.2`.

**D-028 Serialization details (T05).** Documents are `nlohmann::ordered_json` (`Gambit::Json`). Floats are written as the
shortest text that reads back exactly (a float32 `0.1` is `0.1`), NaN and infinities as strings. A scene stores each
entity's `Parent` and relies on list order (hierarchy pre-order) for sibling order; `Relationship` is not stored. Entities
are always created in hierarchy pre-order, whatever the file's order. Files hold initial states: the seed, not the RNG
position. Loaders validate first and change nothing on failure; unknown members are warnings, missing ones defaults,
malformed values errors with JSON pointers. Scene `Settings` is an opaque object owned by the modules.

**D-029 Assets, projects and tables (T05).** A scan never deletes or silently rewrites: missing `.meta` files are created,
a `.meta` without a handle gets one, duplicates and unreadable metas are reported and the asset stays unregistered until
the user acts. `.gproject` is the reflected struct `ProjectSettings`. Data table `int` columns are 64-bit and `float`
columns doubles (decimals from spreadsheets stay exact); untyped columns are inferred, integers only when they are
canonical (`007` stays text), and the inferred type is not written back. Prefabs refer to no entity outside themselves and no other prefab; instances record `PrefabInstance`.

**D-036 Vulkan baseline.** Vulkan 1.3 instance/device (NVRHI uses core 1.3 entry points): timeline semaphores,
synchronization2, dynamic rendering required; shaders target SPIR-V for vulkan1.2. The `Application` owns the
`GraphicsDevice`; `--no-gpu` disables it. Golden-image tolerance: 2/255 per channel, ≤ 0.1% differing pixels.

**D-020 Simulation start.** Every simulation entry point (editor play, `sim.run`, Lua tests, runtime scene load,
replays) calls `World::BeginSimulation(seed)` exactly once: RNG reseeded from the scene seed, tick and time zeroed, events
and pending command-buffer commands cleared — so a scene played from the editor and the same scene loaded from disk
produce identical hashes. It runs before the simulation's systems are created, as for a scene just loaded from its file,
so what systems do in `OnCreate` (draw random numbers, queue commands) is part of every run alike;
`EditorContext::CreateSimulationWorld(scene, seed)` is that order for the tools.

**D-037 MikkTSpace (T08).** `ThirdParty/mikktspace` (zlib, pinned `master` commit) generates the tangents of meshes
that have none (glTF requires MikkTSpace). Tangents are `xyz` + bitangent sign in `w`, bitangent =
`sign * cross(normal, tangent)` pointing up the image: MikkTSpace sees `v' = 1 - v` because glTF's V runs down (the same
result as three.js's sign flip). Vertices whose corners get different tangents are split.

**D-038 Sub-assets (T08).** An asset file may contain assets (a glTF file is a `Model` whose meshes, materials and
textures are sub-assets). Each sub-asset has a key unique in its file (`Mesh/<name>`, `Material/<name>`,
`Texture/<name>`, `#<index>` for missing or shared names, `Texture/<name> (Linear)` for an image used both as color and
as data) and a handle of its own, listed in the file's `.meta` under `SubAssets`, so re-imports keep references. A scan
registers the listed sub-assets (the registry records `Parent` and `SubAssetKey`); an import that produces new keys adds
them to the `.meta`; keys that disappear stay listed (a sub-asset that comes back keeps its handle). A listed handle that
another asset has (another file or its sub-asset, the file itself, a built-in) is not registered (scan,
`RegisterFile`), and the next import gives its key a new handle and rewrites the `.meta`, so a model never refers to
another asset. `RegisterFile` registers the sub-assets an existing `.meta` lists, like a scan. Loading a sub-asset loads
its file; a sub-asset has no import settings of its own. `.glb`/`.gltf` files are `Model` assets (not `Mesh`).

**D-039 Built-in assets (T08).** Assets the engine provides without files have fixed small handles and the path
`builtin:<name>`: the MeshFactory primitives `0x101`.. (Cube, Sphere, Plane, Capsule, Cylinder, Cone, Torus, Quad),
the default textures `0x201`.. (White, Black, FlatNormal, Checker) and the default material `0x301`. Modules register
them in `RegisterEngineAssets`; they win over a project asset that claims the same handle, which a scan reports as a
`DuplicateHandle` and leaves unregistered (`RegisterFile` refuses it) until `AssignNewHandle` gives it its own.

**D-044 Renderer conventions (T08).** Reversed depth (D32, near 1, far 0, `Greater` tests) with GLM's zero-to-one
projections; meshes are counter-clockwise seen from outside (glTF), mirrored transforms flip the front face. Physical
light units: directional lights in lux, point and spot lights in lumens (candela = lumens / 4 pi for both), exposure
from EV100 (`1 / (1.2 * 2^EV100)`) applied to every luminance before shading (pre-exposed `RGBA16F` scene color), so
emissive colors are luminances too. A depth prepass feeds a forward pass that tests `GreaterOrEqual` against it
(identical, `invariant` positions); at most 256 lights per view in a structured buffer. The renderer reads only a
`RenderScene`, extracted in `PreRender` with previous and current tick transforms and simulated times, interpolated with
`alpha` (the shaders' `Time` is `RenderScene::GetTime(alpha)`). Each `RenderView` has a `ViewId` (a scene camera's is
its entity UUID): view targets, and later per-view temporal state, are kept per id. A `MeshRenderer`'s `SubmeshIndex`
and `MaterialOverrides` apply to `Mesh` assets; a `Model` draws its meshes with their own materials and ignores them,
with a warning, until model-level overrides are defined. GPU copies (`RenderResources`) hold their assets, so
`UnloadUnused` keeps what is drawn, and are released after `IdleFramesBeforeRelease` renders without use.

**D-045 glTF import scope (T08).** Supported (also when required): `KHR_mesh_quantization`, `KHR_texture_transform`,
`KHR_materials_emissive_strength`, `KHR_lights_punctual`; a file that requires another extension is refused, other used
extensions are ignored. A material has one UV transform (`UVTiling`/`UVOffset`, taken from the first textured slot's
`KHR_texture_transform`, rotation unsupported) and samples every texture with `TEXCOORD_0`; both limits are warnings on
import. Points and lines are skipped; skins, animations and morph targets arrive with T14.

**D-046 Exposure.** Exposure is EV100 everywhere. `ProjectSettings.Rendering` holds `ExposureEV100` (default 15,
replacing the linear `Exposure` multiplier via a `.gproject` migration); extraction passes it to every view unless
the camera's `PostProcessComponent` (T09) overrides it (manual EV100 or auto-exposure).

**D-047 Asset reference attributes accept several types.** `Attr::AssetType` holds a list of accepted
`AssetType`s (e.g. `MeshRenderer.Mesh` accepts `Mesh` and `Model`). Serialization, inspector pickers, Lua and
command schemas validate against the list.

**D-053 Command model and CLI (T06).** A command is a `CommandInfo` (dotted name, summary, argument schema in the JSON
Schema subset of MCP tools, flags `Mutating`/`Undoable`/`RequiresProject`/`RequiresScene`/`EditorOnly`/`WritesFiles`) and a
handler `(EditorContext&, const Json&) -> CommandResult`. `CommandRegistry::Execute` validates the arguments before the
handler (unknown, missing, wrong type, enum, range; defaults applied), checks the flags' preconditions, wraps undoable
commands in an undo group and never lets an exception escape. A result is success (data, message, warnings) or an error
code (`unknown-command`, `invalid-arguments`, `no-project`, `no-scene`, `not-found`, `already-exists`, `conflict`,
`unsupported`, `failed`, `internal`) with a message that names the argument and what would work. `gambit <command|words>
--option value` runs one command as one session on the project of `--project` or the current directory (its start scene
is open; `--scene` picks another), saves the scenes and tables it changed when it succeeded (never under `--dry-run`),
prints a summary or the result as JSON, and exits 0 (ok), 1 (the command failed) or 2 (usage: unknown command or option,
arguments the schema rejects). `gambit batch` runs JSON lines in one session, stops at the first failure and then saves
nothing unless `--keep-going`. Commands that write files at once carry `WritesFiles` (and `Mutating`) and are refused in dry
runs; a command that only returns data carries neither (`bulk.export-csv` returns CSV text, `bulk.write-csv` writes it). A
command that replaces a file refuses when that file is an open document with unsaved changes and closes it otherwise. A
scene whose load dropped data the build cannot read (unknown components, fields, members) is never saved over silently:
saving it is a `conflict` that names the data, unless the caller passes `discardUnknown`. The command-line parser
(`Automation/CommandLine.h`) and the batch runner (`Automation/CommandBatch.h`) belong to the engine, so every host reads
command lines and batches alike; the CLI adds only process concerns (global options, streams, exit codes).

**D-054 Undo (T06).** An `IUndoableOperation` performs its change on the first `Redo`, recording what `Undo` needs from
the state it finds, and names its documents (scene or table paths) instead of pointing at them. The generic operations
(component fields before/after, component presence, entity subtree snapshots with their UUIDs and sibling position,
reparenting, table cells and rows) are built from reflection, so new components and commands need no undo code. Every
undoable command is one step (an undo group, rolled back when the command fails); consecutive field edits with the same merge
key and target merge (drags), as do consecutive groups whose operations all carry the key and merge pairwise (a drag of
several entities through `bulk.set`), never across undo, redo or a save. Not undoable, by design: commands that write files
at once (`project.new`, `scene.save`, `asset.import`, `prefab.create`, `bulk.write-csv` — the file system is not a document
of the history), commands that change the session rather than a document (`scene.new`, `scene.close`, `undo`, `redo`) and
comments (`comment.*`: project data that is written whenever it changes, D-056). A document is dirty when the operations applied to it
differ from the count at its last save; the history is bounded (1000 steps); a failing undo or redo clears it and keeps the
documents dirty. Entities made by tools get random UUIDs and never draw from the world's generator.

**D-055 Partial edits merge into structs (T06).** `component.set`, `bulk.set`, CSV import and, later, Lua table assignment
start from the current value: a struct (also inside an array of structs, element i into element i) changes the members it
names and keeps the others; vectors and other arrays are replaced whole; elements beyond the current array start from the
struct's defaults. Loading files and prefabs still starts from defaults (`FieldValueMergeFromJson` vs. `FieldValueFromJson`).

**D-056 Comments (T06).** A thread is `<Project>/Comments/<id>.json` (format version 1: `Id` `"0x"` + 16 hex digits,
`Target`, `Author`, `Status` `Open`/`Resolved`, `Created`/`Updated` and message timestamps in UTC ISO-8601 from the wall
clock, `Messages`), written whenever it changes. Authors are 1 to 128 and messages 1 to 16384 bytes of valid UTF-8; targets
are validated per kind. Target kinds and statuses are PascalCase like the other file formats (`Entity`, `Component`,
`Asset`, `File`, `Feature`, `Position`; `Open`/`Resolved`); this replaces the lower-case spelling of D-052. Files that are not
valid threads are reported and never rewritten. The folder is not scanned for
assets and is never exported. `GAMBIT_AUTHOR` names the author on the command line; dry runs keep threads in memory.

**D-057 Struct infos belong to the registry (T06).** `FieldAttributes::Struct` is a non-owning pointer to a `StructInfo`
that the `TypeRegistry` owns and outlives, so a struct can hold a `std::vector` of itself (blend trees) without a reference
cycle that would never be freed.
