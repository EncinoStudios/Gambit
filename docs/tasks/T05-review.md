# T05 review

Verdict: **APPROVE-WITH-FIXES**. The brief is covered without stubs; the struct field type, scene format, prefab
remapping (struct and struct-array `Entity` fields included), asset scan and CSV tables are generic, well tested and
deterministic. The implementer's choices are sound and are confirmed here: files store `Parent` instead of
`Relationship`; `FieldType::UUID` for identifiers that are not references; `int` columns are 64-bit and `float` columns
doubles; `Inactive` stays hashed (it is derived deterministically, and hashing it catches World bugs).

## Fixed in the review commit

| Where | Problem | Fix |
|---|---|---|
| `Asset/DataTable.cpp` | Errors for cells after a quoted field with line breaks gave the record's first line with a column from a later line. | Each field records its own line; regression test. |
| `Asset/DataTable.cpp` | Text that is not UTF-8 (Excel's "ANSI" CSV) was accepted and only failed later when the value went to JSON. Edits could also store such text, which the saved file then could not load. | Parse reports the field's line/column; `SetValue`, `AddRow` and `RenameRow` refuse it. |
| `Serialization/Migration.cpp` | A migration step that used a throwing nlohmann accessor (`at`, `get<T>`) on an unexpected document let the exception escape `Upgrade`, i.e. past every loader. | Exceptions from steps fail the upgrade like an error; test. |
| `Asset/AssetRegistry.cpp` | Asset files whose names are not portable virtual paths (possible on Linux/macOS): `a\b.png` hit a `GB_CORE_ASSERT` (bad content crashing Debug) after its `.meta` file had been written into another folder (`a/b.png.meta`); `aux.png` and `x:y.png` were misreported as missing `.meta` files. | Reported as the new `ScanIssueKind::InvalidPath` and skipped; test. |
| `Asset/AssetRegistry.cpp` | Duplicate handles were settled by path order on every scan, so copying `Crate.png` to `Crate - Copy.png` in a file browser made the *copy* take the handle on the next rescan (the original was dropped from the registry and the cache). | On a rescan the file that had the handle keeps it; path order only decides on a first scan. Test. |
| `Asset/AssetManager.cpp` | `GetHandle`/`FindByPath` (the path helpers for tools) only matched the exact normalized spelling. | Paths are normalized first (`Data\Weapons.csv`, `./Data//Weapons.csv`). |
| `Project/Project.cpp` | `Project::New` refused a directory with exactly one `.gproject` but created a third file next to two. | Any listing result other than "none found" is refused. |
| `Asset/Prefab.cpp` | A hand-edited `.gprefab` could keep references to entities outside itself (the notes promised a warning at instantiation; there was none). | `Parse` clears them with a warning in the report. |

## Open issues (need a decision or belong to a later task)

1. **Play-in-editor vs. scene file (T06).** A scene file stores the seed, not the RNG position, but `World::CreateEntity`
   draws UUIDs from the world RNG. An editor world in which entities were created with `CreateEntity` therefore has
   another RNG state (and state hash) than the same scene loaded from its file, so play-in-editor and the exported game
   would draw different random numbers. T06 should create editor entities with `UUID::Generate`
   (`UUIDMode::Random`) and start play mode from the serialized scene (or reseed the play copy).
2. **Partial updates of struct fields (T06/T11).** `ComponentDataFromJson` is a partial update per component field, but a
   partial struct object (`{"Weight": 0.5}` for an animator layer) decodes against the struct's defaults, resetting the
   other members. Commands and the CLI need a merge variant (decode onto the current `StructValue`) or path addressing
   (`Layers/1/Weight`).
3. **Prefab instantiation from systems (T11).** `PrefabUtils::Instantiate` creates entities immediately; simulation code
   must defer structural changes, and `CommandBuffer` has no spawn command yet.
4. **Projects from packs (T23).** `Project::Load` reads and writes the `.gproject` with `std::filesystem` and always
   mounts a `DirectoryMount`; every other loader goes through the VFS. The exported player will need a way to open a
   project from an existing VFS (a pack).
5. **Recursive structs.** A struct can hold a `std::vector` of itself (blend-tree nodes); that works, but its
   `StructInfo` then owns a `Ref` to itself and is never freed (leak at shutdown). Store a non-owning pointer if
   recursive structs are wanted (T14).
6. Structs are homogeneous: an array of structs cannot hold different module kinds. Particle modules (T19) should be a
   fixed set of struct fields (one per module kind, each with an `Enabled` flag) or need a variant field type.
