# ECS guide: components and systems

Gambit is strict ECS (D-020): **components are plain data, systems hold all logic**. A `World` wraps an
`entt::registry`; `Entity` (`Gambit/ECS/Entity.h`, includes `World.h`) is a handle to one entity. Every entity has
`IDComponent` (UUID), `TagComponent` (name) and `RelationshipComponent` (parent + ordered children).

## Add a component

1. Declare a plain struct in your module (`Gambit/<Module>/Components/...`): public PascalCase fields with defaults,
   no virtual functions, no behaviour beyond trivial helpers.
   ```cpp
   struct HealthComponent { float Current = 100.0f; float Maximum = 100.0f; ModeEnum Mode = ModeEnum::Normal; };
   ```
2. Register it once, in the module's registration function (called from `RegisterEngineTypes`,
   `Reflection/EngineTypes.cpp`). Register enums first:
   ```cpp
   registry.RegisterEnum<ModeEnum>("HealthMode").Value("Normal", ModeEnum::Normal).Value("God", ModeEnum::God);
   registry.RegisterComponent<HealthComponent>("Health")                     // stable name: files, Lua, CLI
       .Field("Current", &HealthComponent::Current, Attr::Range{ 0.0, 1000.0 })
       .Field("Maximum", &HealthComponent::Maximum)
       .Field("Mode", &HealthComponent::Mode, Attr::Tooltip{ "God mode ignores damage" });
   ```
   Field types: `bool`, `int32_t`, `uint32_t`, `int64_t`, `uint64_t`, `float`, `double`, `glm::vec2/3/4`
   (`Attr::Color` for colors), `glm::quat`, `std::string`, `UUID` (entity reference; `Attr::AssetType{ "Texture" }`
   for asset handles), registered enums, and `std::vector` of those (not enums). Other attributes: `Hidden`,
   `ReadOnly`, `NotSerialized`, `NotHashed` (derived data only), `Category`. Component flags (`Serializable`,
   `EditorVisible`, `Removable`, `Internal`) default to the first three.
3. That is all: serialization, inspector, Lua, CLI, `World::Copy` and the state hash work from the registration.
   Never hand-write per-component serializers. Add a test that sets each field through reflection.

Script components (D-023) are registered at runtime with `TypeRegistry::RegisterDynamicComponent(schema)` and stored
as `DynamicComponent`; tools treat them exactly like C++ components through `ComponentInfo`.

## Add a system

```cpp
class RegenerationSystem final : public ISystem
{
public:
    std::string_view GetName() const override { return "RegenerationSystem"; } // unique per world
    SystemPhase GetPhase() const override { return SystemPhase::Update; }
    int32_t GetOrder() const override { return 0; } // within the phase; ties break by name
    void OnUpdate(World& world, TimeStep step) override
    {
        for (auto [entity, health] : world.View<HealthComponent>().each()) // skips inactive entities
            health.Current = std::min(health.Maximum, health.Current + 5.0f * step.GetSeconds());
    }
};
world.GetScheduler().AddSystem(CreateScope<RegenerationSystem>());
```

Phases run in the order of D-024 (`Input` ... `Transform`, `PreRender`). `RunsInEditMode()` opts a system into
`SimulationMode::Edit` ticks. Add `TransformSystem` to every world that needs `WorldTransformComponent`.

## Rules that keep simulations deterministic (D-011)

- Create, destroy, add or remove while a system runs through `world.GetCommandBuffer()`; commands address entities
  by UUID and are applied after the phase. `CreateEntity` there returns the new UUID immediately.
- Randomness only from `world.GetRandom()`; new entity UUIDs come from it too (`World::GenerateUUID`).
- Time only from the `TimeStep` / `world.GetTime()`; trig and exp through `DMath`.
- Keep simulation state in components (systems may cache, but the cache must be rebuildable): only components are
  saved, copied (`World::Copy`) and hashed (`World::ComputeStateHash`).
- Change the hierarchy only with `World::SetParent`/`DestroyEntity`, the enabled state with `World::SetEnabled`.
- Events are double-buffered (D-017): `world.Events<T>().Emit(...)`; `View()` is the previous tick's complete set
  (every system sees each event once, whatever its phase), `ViewCurrent()` this tick's so far. Events are copied and
  hashed, so each type needs `void HashEvent(Hasher&, const T&)` next to it (`HashEventFields(hasher, e.A, e.B)`).
- Every simulation change needs a determinism test: run two worlds with the same seed and compare state hashes.
