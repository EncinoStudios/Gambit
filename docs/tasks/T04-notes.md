# T04 notes: reflection, World/ECS, systems, state hash

Implementation notes and the design decisions taken beyond the brief. Guide: `docs/guides/ECS.md`; recipe:
`.claude/skills/add-component/SKILL.md`.

## Layout

| File | Contents |
|---|---|
| `Reflection/FieldType.h` | `FieldType`, `FieldValue` (std::variant), `IsValueOfType`, `MakeZeroValue`, C++ type mapping |
| `Reflection/FieldInfo.h` | `FieldFlags`, `FieldRange`, `EnumInfo`, `FieldAttributes`, `Attr::*`, `FieldInfo` (get/set/raw/hash) |
| `Reflection/ComponentInfo.h` | `ComponentId`, `ComponentFlags`, `ComponentInfo` (Has/Add/Remove/GetRaw/CopyTo/ForEach/FindStorage) |
| `Reflection/TypeRegistry.h` | `TypeRegistry`, `ComponentBuilder<T>`, `EnumBuilder<E>`, dynamic schemas, `RegisterEngineTypes` |
| `Reflection/DynamicComponent.h` | storage of script components (one `FieldValue` per schema field) |
| `Reflection/FieldHashing.h` | byte encoding of field values in the state hash (shared by C++ and script fields) |
| `ECS/World.h`, `ECS/Entity.h` | World (entities, hierarchy, enabled state, resources, events, tick, copy, hash), Entity handle |
| `ECS/CommandBuffer.h`, `ECS/EventQueue.h` | deferred structural changes, per-tick typed events |
| `ECS/System.h`, `ECS/SystemScheduler.h` | `ISystem`, `SystemPhase` (D-024), `SimulationMode`, scheduler with stats |
| `ECS/Components/CoreComponents.h` | ID, Tag, Transform, Relationship, WorldTransform, Disabled, Inactive |
| `ECS/Systems/TransformSystem.h` | world matrices, parents before children |

## Decisions (please confirm)

1. **Registry instances.** `TypeRegistry` can be instantiated; `TypeRegistry::Get()` is the process-wide default with
   `RegisterEngineTypes` applied on first use, and `World()` uses it. A `World(TypeRegistry&)` references its
   registry, which must outlive it. Reason: tests stay isolated, and a CLI/MCP process can hold two projects whose
   script components differ (same name, other schema) without clashing. `World::Copy` keeps the source's registry.
2. **FieldValue holds storage types.** One alternative per storage type, not per `FieldType`: Vec3/Color3 share
   `glm::vec3`, Vec4/Color4 `glm::vec4`, Entity/AssetHandle `UUID`, Enum is `int64_t`. The `FieldInfo` says which
   `FieldType` a value has. This keeps `std::get<glm::vec3>` natural and avoids duplicate variant alternatives.
3. **Color and asset fields of C++ components** are marked with `Attr::Color{}` (an attribute beyond the brief's list;
   D-022 names "color") and `Attr::AssetType{ "Texture" }` on `glm::vec3/vec4` and `UUID` fields. The asset type is
   an identifier string because the `AssetType` enum arrives in T05; T05 should validate names against it.
4. **Arrays of enums** are not supported (the brief lists scalar/vector/String/Entity/AssetHandle elements).
5. **Component ids**: the EnTT type hash for C++ components (their default storage), FNV-1a 32 of the name for script
   components (their named storage). Collisions are rejected at registration. Ids are process-local; files and tools
   use names.
6. **Flags.** `Internal` = engine-managed: generic `ComponentInfo::Add/Remove` refuse it (typed C++ access is
   unrestricted). `Removable` = tools/scripts may remove it. Core flags: ID `Internal`; Tag `Serializable|EditorVisible`;
   Transform default; Relationship `Serializable|Internal` (T05 can rebuild the hierarchy from it); WorldTransform and
   Inactive `Internal`; Disabled default.
7. **Mandatory components** are ID, Tag and Relationship (static_asserts keep `Entity` from adding/removing them).
   `TransformComponent` is optional (pure data entities, UI); an ancestor without one counts as the identity.
8. **Disabled state**: `DisabledComponent` is the user's flag (serialized); the World keeps a derived
   `InactiveComponent` on the disabled entity and its whole subtree (EnTT signals on Disabled plus `SetParent`), so
   every path - typed, reflective, command buffer, loader - stays consistent. Systems skip inactive entities via
   `World::View<...>()`. The TransformSystem still updates inactive entities so their matrices are valid on
   re-enable.
9. **WorldTransformComponent** holds a `glm::mat4` and has no reflected fields (there is no Mat4 `FieldType`); it is
   derived, so nothing is lost for files or hashing. `World::ComputeWorldTransform` gives an always-current matrix
   bit-identical to the system's.
10. **Root order** is the order in which entities became roots (creation or detachment), kept in O(1) and copied by
    `World::Copy`, but **not hashed**: the brief requires equal hashes for different creation orders of unrelated
    entities. Child order is hashed (it is a Relationship field). There is no sibling-reorder API yet (T06/T07).
11. **FindEntityByName** returns the match with the smallest UUID (independent of creation history).
12. **State hash** = versioned seed, tick, simulated time (added: two worlds at the same tick but with different time
    steps differ), RNG state, entity count, then per entity (ascending UUID): UUID, and per component present (by name)
    the name and every field without `NotHashed`, hashed in place (no copies). Tag presence counts.
13. **Edit-mode ticks** clear events, flush commands and run edit-safe systems but do not advance tick or time.
14. **Events** are double-buffered (D-017, decided in review): each `World::Tick` (edit ticks too) swaps the buffers,
    `View()` is the previous tick's complete set and `ViewCurrent()` this tick's so far; `ClearEvents` drops both.
    Both buffers are copied and hashed. Event types provide `HashEvent(Hasher&, const T&)` (ADL, usually through
    `HashEventFields`); queues are hashed as sorted per-queue digests, so the hash needs no event type identity (type
    indices depend on first use, type names on the compiler).
15. **CommandBuffer**: `CreateEntity` reserves the UUID at record time (world RNG, so recording order decides it) and
    `GenerateUUID` never hands out a reserved one. Commands that no longer apply are skipped silently and counted
    (`CommandBufferFlushResult`). Typed `AddComponent<T>` adds or replaces; the reflective one keeps an existing
    component. Added `SetParent` and `Execute(uuid, fn)` commands beyond the brief's list. The World flushes once
    before the first phase too (commands recorded between ticks).
16. **World::Copy** recreates the entity storage as it is (alive identifiers and the destroyed ones awaiting reuse, like
    an EnTT snapshot, so later entities get the same handles) and copies every storage in packed order, so systems
    iterate the copy exactly like the source (verified by a rollback test that continues a copy for 500 ticks).
    Systems are not copied; the caller adds them.
17. **Script component lifetime**: registering an identical schema again returns the existing type (script reload);
    a changed schema under a taken name is an error (hot-reload migration is T11's). Unregistering removes the
    storage, with every instance, from every world attached to the registry; pending commands for it are skipped.
18. `CreateEntityWithUUID` returns a null `Entity` (and logs an error) for a null or taken UUID; `SetParent` returns
    `Result<void>` (cycles are content errors from tools and scripts, not programmer errors).

## Follow-ups from the T02/T03 review

- **VFS writes**: a path that is a mount point or a virtual directory leading to one is rejected with `InvalidPath`
  for `Write` and `Remove` (whatever the age of that mount). In addition, a write is rejected with `ReadOnly` when a
  newer read-only mount provides the path, because reads would keep returning that mount's version (the same class
  of silent shadowing). Otherwise writes go to the newest writable mount containing the path, as before.
- **Portable names**: `NormalizePath` rejects segments ending in `.` or space and reserved device names, compared
  ASCII case-insensitively on the part before the first dot with trailing spaces removed. The list follows the
  current Windows documentation, a superset of the brief: CON, PRN, AUX, NUL, COM0-9, LPT0-9 and COM/LPT followed by
  superscript 1, 2 or 3. Characters Windows forbids (`<>"|?*`) are still accepted; rejecting them too would complete
  the portability rule (suggested follow-up).
- **Permission errors**: new `FileError::PermissionDenied` and `FileSystem::ClassifyError(std::error_code)` used by
  every FileSystem and DirectoryMount failure (stream opens use errno). `EROFS` maps to `ReadOnly`. Behaviour change:
  writing into a missing directory now reports `NotFound` (was `Io`). The real permission tests skip themselves when
  privileges bypass permissions (root in this sandbox); they were verified unprivileged via `setpriv` and run in CI.
- **Input ranges**: `InputFrame::Validate()` holds every range rule (sticks [-32767, 32767], triggers [0, 32767] plus
  the existing ones); `Read` uses it and `InputLog::Validate`/`Save` now refuse frames that could not be loaded.
  `InputDevices` clamps triggers with the new `QuantizeGamepadTrigger`, so device frames always validate. The
  InputFrame test fixture used the now-invalid axis value -32768; its reference hash was updated accordingly.
