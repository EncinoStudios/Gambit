# T04 review

Verdict: **APPROVE-WITH-FIXES**. Every deliverable exists, is tested and works, including the T02/T03 follow-ups. EnTT
use is sound: storages are reached by id, so script components live in named `DynamicComponent` storages. Views never
iterate while their storages change (`ForEach` takes a snapshot, `TransformSystem` collects first). Unregistering a
script type drops its storage in every attached world. The state hash sorts entities by UUID and components by name,
and covers script fields with the same encoding as C++ ones. D-018 matches the code (the world holds a reference, not a
pointer, to its registry).

## Fixed during review

1. **D-017 implemented.** `EventQueue<T>` is double-buffered: `View()` returns the previous tick's events and
   `ViewCurrent()` the events of the tick so far. Every `World::Tick` swaps the buffers, edit ticks included, and
   `ClearEvents` drops both. Both buffers are copied by `World::Copy` and part of the state hash. Event types now
   provide `HashEvent(Hasher&, const T&)` (found by ADL, usually written with `HashEventFields`). This is enforced by
   the `WorldEvent` concept. Each queue is hashed to its own digest and the digests are sorted, because
   `entt::type_index` depends on first use and type names depend on the compiler. Tests cover the buffer semantics, an
   "exactly once whatever the phase" test, hash sensitivity (each field, the buffer position, and independence from
   type order), and copies. The 1000-tick determinism test now reacts to the previous tick's events, so its rollback
   copy depends on copied events. A mutation test confirmed that a missing buffer fails it.
2. **`World::Copy` lost the entity free list.** Only the alive identifiers were recreated. The source then recycled a
   destroyed index (with a bumped version) while the copy created a fresh one, so the handles diverged after the copy.
   Iteration order happened to survive, but cached handles and anything keyed by handle did not. The whole entity
   storage is now reproduced and `free_list` is restored, as EnTT snapshots do. A test checks that later entities get
   identical handles.
3. A dead retry loop was removed from `CommandBuffer::CreateEntity`: `GenerateUUID` already skips reserved UUIDs.
4. Docs: `docs/guides/ECS.md` (events), `T04-notes.md` items 14 and 16.

## Open (need a decision)

- **Nested struct arrays (T14/T19).** `FieldValue` is a flat variant and `FieldAccess` only reads, writes and hashes
  whole values. Arrays of reflected structs need a recursive value type (for example a `StructValue` alternative
  behind indirection) and element access operations, also inside `DynamicComponent`. Design this at the start of T14.
- **Event names.** T11/T13 want `world:Events("CollisionEnter")`. Registering event types (name plus fields) in the
  TypeRegistry would give Lua/CLI access and could replace the hand-written `HashEvent` with generic hashing. It would
  also give the hash a type identity: today two event types with byte-identical contents are indistinguishable.
- **Reflective writes bypass invariants.** `SetFieldValue` and `CopyTo` do not enforce `ReadOnly` or `Internal`.
  Writing `ID.ID` or `Relationship.*` that way corrupts the UUID map or the hierarchy, and `IsAncestorOf` would loop
  on a cycle. The T05 loader must rebuild the hierarchy through `SetParent`, and T06 commands must refuse ReadOnly
  fields and Internal components.
- **`WorldTransformComponent` presence is hashed.** It has no fields, but its name is hashed when present. The brief
  says "not hashed". This is harmless while the TransformSystem keeps it in step with `Transform`. A component-level
  "derived" flag would make it exact.

Final check: `scripts/check.py` is green with GCC 13 Debug and with Clang 18 Release. There are 355 test cases and
no warnings.
