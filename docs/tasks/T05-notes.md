# T05 notes: serialization, projects, assets, data tables

Implementation notes and the decisions taken beyond the brief (please confirm those marked **confirm**). Formats are in
`docs/guides/FileFormats.md`; decisions D-019, D-028 and D-029 in `docs/DECISIONS.md`.

## Follow-ups from the T04 review (done first)

| Item | What was done |
|---|---|
| `Struct` field type | `FieldType::Struct` and arrays of structs. `TypeRegistry::RegisterStruct<T>(name)` returns a `StructBuilder<T>` with the same `Field` calls as components (shared `Internal::MakeFieldInfo`). `FieldValue` is recursive through `StructValue { std::vector<FieldValue> Values }`. `StructInfo` (fields, defaults, `CanHold`, `Read/Write/Hash` of C++ objects) is shared through `FieldAttributes::Struct`. `FieldAccess::Read/Write/Hash` now take the `FieldInfo` (internal API) because struct fields need their `StructInfo`. Hashing is field by field, in place for C++ storage and via `HashFieldValue` for values, same bytes. Script components can declare struct fields (`DynamicFieldSchema` with `Attributes.Struct`, which must belong to the registry). JSON: objects. |
| IDComponent / RelationshipComponent writes | `ComponentInfo::GetRaw(Entity)` returns nullptr for `Internal` components (the const overload still reads them), `SetFieldValue` and `CopyTo` (now returns `bool`) refuse them, `ForEach` skips them; `World::Copy` uses a private unchecked copy. ID and Relationship were already `Internal` and `ReadOnly`. `ReadOnly` fields of other components are not refused by `ComponentInfo`: the flag tells tools not to offer edits and code (command buffer, loaders) may write them. T06 commands must check `ReadOnly` themselves. |
| `WorldTransformComponent` and the hash | New `ComponentFlags::Derived`: not hashed (presence included), not stored. `WorldTransform` is `Internal | Derived`. The hash seed is now `Gambit.World.StateHash.2`. `Inactive` stays hashed (**confirm**: it is derivable too, but hashing it catches World bugs; it could be flagged `Derived` as well). |
| `scripts/build.py` | Builds with `-k 0`; on failure prints up to 60 distinct compiler/linker error lines with file:line (GCC, Clang, MSVC, linker) and the number of failed steps; success output is unchanged. `scripts/test_build_errors.py` tests the parsing (`python3 -m unittest discover -s scripts -p "test_*.py"`); the Linux format job of CI runs it. |

## Layout

| Path | Contents |
|---|---|
| `Core/Utf8.h` | `IsValidUtf8` (moved out of `InputActions.cpp`) |
| `Serialization/Json.h` | `Json` (= `nlohmann::ordered_json`), `ParseJson`/`DumpJson` (no exceptions, depth limit, strict UTF-8), JSON pointers, `LoadReport` (warnings), `AddSource` |
| `Serialization/Migration.h` | `MigrationTable` (version steps) |
| `Serialization/FieldSerializer.h` | `FieldValue` / struct value / struct object to and from JSON for every `FieldType`, with paths |
| `Serialization/EntityData.h` | `EntityData`/`ComponentData`/`FieldData`, entity JSON, `CaptureSubtree`/`CaptureWorld`, `SpawnEntities` (validate first, UUID remapping, reference remapping), `CollectAssetReferences`, `ClearExternalEntityReferences` |
| `Serialization/SceneSerializer.h` | `.gscene`: `Serialize/Save/Parse/Read/Apply/Deserialize/Load`, `SceneMetadata`, `SceneData` |
| `Asset/AssetType.h` | `AssetHandle`, `AssetType`, `DetectAssetType` (extension table) |
| `Asset/Asset.h`, `IAssetImporter.h` | asset base class, importer interface and load context |
| `Asset/AssetMetadata.h` | `.meta` format |
| `Asset/AssetRegistry.h` | handle/path/type table and the project scan with `ScanReport` |
| `Asset/AssetManager.h` | loading, caching, reload, unload, dependencies, `RegisterFile`, `AssignNewHandle`, import settings |
| `Asset/DataTable.h` | CSV tables |
| `Asset/Prefab.h` | `Prefab` asset and `PrefabUtils` (create, instantiate, `.gprefab` format) |
| `Asset/SceneAsset.h`, `BuiltinImporters.h` | scene asset and the three importers (scene, prefab, data table) |
| `Asset/Components/AssetComponents.h` | `PrefabInstance`, `DataRef` |
| `Project/ProjectSettings.h`, `Project.h` | reflected `ProjectSettings` and `Project` |
| `GambitTests/src/TestUtils/TempDirectory.h` | temp project directory with a VFS |

## Decisions beyond the brief

1. **Files store `Parent`, not `Relationship`** (**confirm**). The brief's entity format has `UUID` and `Components`; the
   hierarchy needs one more fact, and list order (pre-order) already gives sibling order, so each non-root entity has
   `"Parent"`. The `Relationship` component lost its `Serializable` flag (it stays `Internal`). T04 notes item 6 had
   `Serializable | Internal` for it; no code depended on that.
2. **`FieldType::UUID` / `Attr::PlainUUID`** (**confirm**): `PrefabInstanceComponent::SourceEntity` is the UUID an entity
   has inside a prefab file; typed `Entity` it would be remapped and flagged as a dangling reference by tools. A plain
   identifier type costs a few lines in each switch and keeps `Entity` meaning "an entity of this world".
3. **Scene `Name` and `Settings` are not part of `World`**: `SceneMetadata` travels with the world in whoever owns it
   (T06's `EditorContext`). `Settings` is an opaque JSON object keyed by module.
4. **Entities are created in hierarchy pre-order** whatever the order of a file, so the storage order that systems iterate
   depends on structure only (a hand-edited file behaves like the saved one). Component order inside an entity has no
   observable effect (EnTT pools are per type), so it is not canonicalized.
5. **Floats**: written with the shortest text that reads back exactly (`std::to_chars`), through the JSON library's own
   parser so nothing depends on the C locale; integers may be written as `3.0`; NaN/infinities are strings. A `float` is
   verified to round-trip and falls back to the exact double text otherwise.
6. **Spawn is all or nothing**: `SpawnEntities` validates UUIDs, parents, cycles, components (unknown or engine-managed
   ones are errors there, warnings when reading JSON) and every value before creating anything.
   `UUIDMode::Keep` also accepts a root whose parent already exists in the world (restoring a deleted subtree under its old
   parent, appended as last child: T06 undo needs a sibling-position API if exact restoration matters).
7. **Prefabs**: nested prefabs are not supported in this version (`PrefabInstance` is dropped when capturing and set on
   every entity when instantiating); references to entities outside the prefab are cleared with a warning at capture and
   at instantiation. `Instantiate` uses `UUIDMode::FromWorld` by default (deterministic); tools pass `Random`.
8. **Asset scan** (**confirm** the duplicate policy): files below `Assets`, `Scenes`, `Data`, `Scripts` whose names say
   they are assets are registered; missing `.meta` files are created (`ScanOptions::WriteMetas`, default on); a `.meta`
   without a handle gets one with its settings kept; unreadable `.meta` files, duplicates and orphans are reported and
   never rewritten or deleted. For duplicates the file that had the handle before the scan keeps it (review fix: a copy
   named "Crate - Copy.png" sorts before "Crate.png"), else the first file in path order, and the other stays
   unregistered until `AssignNewHandle`: guessing which file is the original would silently break references. Asset
   files with non-portable names (backslash, `:`, device names) are reported as `InvalidPath` (review fix).
   `AnimationClip` and `Skeleton` have no file extensions (they will be sub-assets of mesh imports, T14).
9. **AssetManager** loads synchronously, caches by handle, remembers failures (logged once) until `ReloadAsset`, and
   `ReloadAsset` replaces the cached object. Hold handles, ask again for current data. `Project::Load` does not scan:
   scanning writes `.meta` files, and read-only commands should not.
10. **DataTable** (**confirm**): `int` is 64-bit and `float` a double (a spreadsheet's `0.1` or `9.95` must not change).
    Untyped columns are inferred (integers only when canonical, `007` stays text; a blank cell keeps a column text).
    The id column is always text. Column names must be identifiers. BOM and CRLF are preserved. `RenameRow` exists because
    editing the id column of a table is a rename.
11. **Project**: the file is `<Name>.gproject` (tools accept the directory when it holds exactly one); settings are the
    reflected struct `ProjectSettings` (inspector and CLI can use them without code); `DefaultSeed` is 1, used for the
    seed of the scene `New` creates; `Rendering.Tonemapper` is `ACES | PBRNeutral` (D-032); `Export` has `OutputName` and
    `Compress` only (T23 extends it). `Project::New` creates folders, `InputActions.json` (Move, Look, Jump, Interact),
    and `Scenes/Main.gscene`, never overwriting existing files.
12. **`nlohmann_json` is a public dependency of `Gambit`** and `Gambit.h` includes the serialization headers, because the
    command registry (T06) and the CLI pass `Json` documents.
13. **Asset type names** in `Attr::AssetType` are checked against the `AssetType` enum by a test over the whole registry
    (the reflection layer cannot depend on the asset module).
14. `RegisterEngineTypes` now also registers the asset components and the project types.

## For T06 and T11

- Commands: `ComponentToJson`, `ComponentDataFromJson` (a partial object is a partial update; apply with
  `ComponentInfo::SetFieldValue` after checking `ReadOnly`), `EntityToJson`/`EntityFromJson`, `CaptureSubtree`
  (`keepRootParent` for delete/undo) and `SpawnEntities` (`UUIDMode::Random`, `ExternalReferences::Keep` for duplicate),
  `LoadReport` for the warnings of a result, `AssetManager::Scan/RegisterFile/AssignNewHandle/SetImportSettings`,
  `DataTable::SetValueFromText/AddRow/RemoveRow/RenameRow`, `Project::New/Load/GetSettings`.
- Lua: `FieldInfo::GetValue/SetValue` work for structs (`StructValue`); `Data.Get(table, row)` is
  `AssetManager::GetAsset<DataTable>` plus `DataTable::GetRow`. Remember that `GetRaw(Entity)` is null for `Internal`
  components: use `GetFieldValue`.

## Tests

Reflection: structs (registration, defaults, read/write, validation, hashing in place equals hashing values, world hash,
copy, script components with structs, rejected schemas), internal components, derived components, identity/hierarchy
in the hash. Serialization: JSON parse/dump/depth/UTF-8, migrations, every `FieldType` round trip, float/double
bit-exactness over thousands of patterns, NaN/infinity, integer ranges, error paths, struct objects, entity data (capture,
spawn, remap, validation, atomicity, determinism), scene layout (golden text), byte-identical round trip, state hash
equality, 120-tick equivalence of a loaded scene, shuffled files, VFS save/load, warnings and errors. Assets: types,
metadata, registry, scan (create/keep/duplicate/orphan/invalid/mismatch/read-only), manager (load/cache/reload/failure/
unload/dependencies/register/new handle), data tables (RFC 4180 edge cases, inference, typed columns, errors with
line/column, editing, VFS), prefabs (create, instantiate, remap, determinism, transform, file format, errors),
components. Project: scaffold, golden file, load from file or directory, validation, active project.
