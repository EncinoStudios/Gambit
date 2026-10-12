---
name: add-component
description: Add a new ECS component (C++ data struct) to the Gambit engine and register it in the TypeRegistry so that serialization, the inspector, Lua, the CLI, World::Copy and state hashing pick it up. Use whenever a task introduces a component, adds or changes a component field, or adds an enum used by a component.
---

# Add a component

Components are plain data (D-020) and reflection is the single source of truth (D-022): one struct, one
registration, one test. Nothing else is per-component. Background: `docs/guides/ECS.md`.

## 1. Declare the struct

- Put it in the owning module: `Gambit/src/Gambit/<Module>/Components/<Name>Component.h` (or the module's existing
  components header). Name: `<Thing>Component`; registered name: `<Thing>` (stable forever: files, Lua and the CLI
  use it; renaming needs a file-format migration).
- Public PascalCase fields with default member initializers; no virtual functions, no logic beyond trivial accessors
  (like `TransformComponent::GetMatrix`). Must be default-constructible and copyable.
- Field types: `bool`, `int32_t`, `uint32_t`, `int64_t`, `uint64_t`, `float`, `double`, `glm::vec2/3/4`, `glm::quat`,
  `std::string`, `UUID`, a registered enum, or `std::vector` of any of these except enums. No raw pointers, no
  `entt::entity` (store the target's `UUID`), no other types: extend `FieldType` (+ JSON, Lua, hashing) first if a
  type is truly missing.
- Derived/cached data that systems recompute: mark `Attr::NotHashed{}` and `Attr::NotSerialized{}`. Runtime state
  that matters for determinism stays hashed even when it is not serialized.

## 2. Register it

In the module's registration function (create `void Register<Module>Types(TypeRegistry&)` if the module has none and
call it from `RegisterEngineTypes` in `Gambit/src/Gambit/Reflection/EngineTypes.cpp`):

```cpp
registry.RegisterEnum<BodyType>("BodyType")              // enums first; value names are identifiers
    .Value("Static", BodyType::Static).Value("Dynamic", BodyType::Dynamic);

registry.RegisterComponent<RigidBodyComponent>("RigidBody")  // flags default to Serializable|EditorVisible|Removable
    .Field("Type", &RigidBodyComponent::Type)
    .Field("Mass", &RigidBodyComponent::Mass, Attr::Range{ 0.0, 10000.0 }, Attr::Tooltip{ "0 = from density" })
    .Field("Tint", &RigidBodyComponent::Tint, Attr::Color{})                 // glm::vec3/vec4 -> Color3/Color4
    .Field("Mesh", &RigidBodyComponent::Mesh, Attr::AssetType{ "Mesh" })    // UUID -> AssetHandle
    .Field("Velocity", &RigidBodyComponent::Velocity, Attr::NotSerialized{}, Attr::ReadOnly{});
```

- Fields are reflected in call order (that is also the file and hash order): append new fields at the end.
- Engine-managed components (derived state, identity) get `ComponentFlags::Internal`; tags (empty structs) work as is.
- Registration mistakes (duplicate names, bad attributes, unregistered enums) fail a `GB_CORE_VERIFY` at startup.

## 3. Use it from systems

Read and write it in systems only (`world.View<RigidBodyComponent, TransformComponent>()` skips inactive entities).
Structural changes during a system go through `world.GetCommandBuffer()`. Randomness from `world.GetRandom()`.

## 4. Test it

In `GambitTests/src/<Module>/`, test case names `"<Module>: <behaviour>"`:
- registration: `registry.FindComponent("RigidBody")`, field types, attributes, defaults;
- every field round-trips through `FieldInfo::SetValue/GetValue` on a component added with `ComponentInfo::Add`;
- if a system uses it: a determinism test (two worlds, same seed, equal `ComputeStateHash()` every tick) and a
  `World::Copy` mid-run that continues identically.
Use a local `TypeRegistry` + `RegisterEngineTypes` (see `GambitTests/src/ECS/TestWorld.h`) rather than mutating the
process-wide registry.

## 5. Finish

Run `python3 scripts/format.py` and `python3 scripts/check.py` (also with `CC=clang CXX=clang++`). Mention the new
component in the module guide; once T11 exists, regenerate the Lua stubs (`gambit api-stubs`).
