#pragma once

#include "Gambit/Core/Base.h"
#include "Gambit/Core/Result.h"
#include "Gambit/Core/TimeStep.h"
#include "Gambit/Core/UUID.h"
#include "Gambit/ECS/CommandBuffer.h"
#include "Gambit/ECS/Components/CoreComponents.h"
#include "Gambit/ECS/EventQueue.h"
#include "Gambit/ECS/System.h"
#include "Gambit/ECS/SystemScheduler.h"
#include "Gambit/Math/Math.h"
#include "Gambit/Math/Random.h"

#include <entt/core/type_info.hpp>
#include <entt/entity/registry.hpp>

#include <cstddef>
#include <cstdint>
#include <list>
#include <map>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Gambit
{
	class Entity;
	class TypeRegistry;

	// A simulated scene (D-021): entities with components in an EnTT registry, the hierarchy, the systems that run on
	// them and the simulation resources (random generator, tick counter, simulated time, per-tick events). Everything
	// that defines the simulation state is reflected through its TypeRegistry, which is what makes saving, copying and
	// hashing it generic. Deterministic (D-011): given the same operations in the same order, two worlds stay
	// bit-identical, and ComputeStateHash proves it. Include "Gambit/ECS/Entity.h" to use it (it includes this).
	//
	// Every entity has an IDComponent (UUID), a TagComponent (name) and a RelationshipComponent (hierarchy).
	// Entity creation and destruction while systems run go through the CommandBuffer.
	class World
	{
	public:
		// `types` must outlive the world and have the core components registered (RegisterEngineTypes).
		explicit World(TypeRegistry& types);
		// A world on the process-wide TypeRegistry::Get().
		World();
		~World();
		World(const World&) = delete;
		World& operator=(const World&) = delete;

		// A deep copy (play mode, rollback): the same entities with the same handles (destroyed ones awaiting reuse
		// included, so later entities get the same handles too), every component of every registered type (script
		// components too) in the same storage order, the random generator, tick, time, seed, root order, both buffers
		// of every event queue and pending commands. Systems are not copied: add them to the copy. Continuing the copy
		// and the source with the same operations keeps them identical.
		[[nodiscard]] static Scope<World> Copy(const World& source);

		// ---- Entities ----

		// UUID from GenerateUUID. An empty name becomes "Entity".
		Entity CreateEntity(std::string_view name = {});
		// Fails (returns a null Entity, logs an error) for the null UUID or one that is taken.
		[[nodiscard]] Entity CreateEntityWithUUID(UUID uuid, std::string_view name = {});
		// Destroys the entity and all its descendants and detaches it from its parent.
		void DestroyEntity(Entity entity);

		// A null Entity when no entity has the UUID.
		[[nodiscard]] Entity TryGetEntity(UUID uuid);
		[[nodiscard]] bool ContainsEntity(UUID uuid) const;
		// The entity with this name; when several have it, the one with the smallest UUID (independent of creation
		// history). A null Entity when none has it.
		[[nodiscard]] Entity FindEntityByName(std::string_view name);
		[[nodiscard]] size_t GetEntityCount() const noexcept { return m_EntityMap.size(); }
		// Every entity's UUID, ascending.
		[[nodiscard]] std::vector<UUID> GetEntityUUIDs() const;

		// A fresh UUID for an entity: drawn from the world RNG (deterministic, D-011), never null, never in use and
		// never reserved by a pending CreateEntity of the world's command buffer.
		[[nodiscard]] UUID GenerateUUID();

		// ---- Hierarchy ----

		// Makes `child` the last child of `parent`, or a root (the last one) when `parent` is null. With
		// keepWorldTransform the child's TransformComponent is recomputed so that its world transform stays the same.
		// Fails when `parent` is `child` or one of its descendants (a cycle). Reparenting to the current parent
		// changes nothing.
		[[nodiscard]] Result<void> SetParent(Entity child, Entity parent, bool keepWorldTransform = false);
		[[nodiscard]] Entity GetParent(Entity entity);
		// In sibling order.
		[[nodiscard]] std::vector<Entity> GetChildren(Entity entity);
		// Moves `entity` to position `index` among its siblings (the children of its parent, or the roots); an index
		// beyond the last position moves it to the end. Sibling order of children is simulation state (hashed with the
		// parent's Relationship), the order of roots is presentation state.
		void SetSiblingIndex(Entity entity, size_t index);
		// The position of `entity` among its siblings (see SetSiblingIndex).
		[[nodiscard]] size_t GetSiblingIndex(Entity entity) const;
		// The entities without a parent in a stable order: the order in which they became roots (creation, or
		// detachment from a parent). Root order is presentation state: it is copied but not hashed.
		[[nodiscard]] std::vector<Entity> GetRoots();
		// Whether `ancestor` is a (strict) ancestor of `entity`.
		[[nodiscard]] bool IsAncestorOf(Entity ancestor, Entity entity) const;
		// The local-to-world matrix from the current TransformComponents of the entity and its ancestors (ancestors
		// without one count as the identity); unlike WorldTransformComponent it is never stale.
		[[nodiscard]] glm::mat4 ComputeWorldTransform(Entity entity) const;

		// ---- Enabled state ----

		// Adds or removes the entity's DisabledComponent; the entity and its subtree become inactive when it or an
		// ancestor is disabled.
		void SetEnabled(Entity entity, bool enabled);
		// Whether the entity itself is enabled (has no DisabledComponent); it may still be inactive through an
		// ancestor.
		[[nodiscard]] bool IsEnabled(Entity entity) const;
		// Whether the entity is active: neither it nor an ancestor is disabled (it has no InactiveComponent).
		[[nodiscard]] bool IsActive(Entity entity) const;

		// A view of the active entities with all of the given components; what systems iterate.
		template<typename... Components>
		[[nodiscard]] auto View()
		{
			return m_Registry.view<Components...>(entt::exclude<InactiveComponent>);
		}

		// ---- Simulation resources ----

		[[nodiscard]] Random& GetRandom() noexcept { return m_Random; }
		[[nodiscard]] const Random& GetRandom() const noexcept { return m_Random; }
		[[nodiscard]] uint64_t GetSeed() const noexcept { return m_Seed; }
		// Stores the seed and reseeds the random generator with it.
		void SetSeed(uint64_t seed);
		// Starts a simulation from the entities as they are (D-020 "Simulation start"): reseeds the random generator
		// from `seed` (the scene's seed), zeroes the tick counter and the simulated time and drops the events of both
		// buffers. A scene file stores only the seed, so this is what makes a copy of an edited scene (whose random
		// generator may have been used by editing) start exactly like the same scene loaded from its file. Every
		// simulation entry point calls it before the first tick: editor play, `sim.run`, Lua tests, runtime scene
		// load. Not while ticking.
		void BeginSimulation(uint64_t seed);
		// The number of Play ticks run (the tick being run, during a tick).
		[[nodiscard]] uint64_t GetTick() const noexcept { return m_Tick; }
		// Simulated seconds: the sum of the time steps of the Play ticks so far, including the current one.
		[[nodiscard]] double GetTime() const noexcept { return m_Time; }
		// The time step of the latest Play tick.
		[[nodiscard]] float GetDeltaTime() const noexcept { return m_DeltaTime; }

		// The events of type T (D-017): View() holds the previous tick's, ViewCurrent() this tick's so far.
		template<WorldEvent T>
		[[nodiscard]] EventQueue<T>& Events()
		{
			auto& queue = m_EventQueues[entt::type_index<T>::value()];
			if (queue == nullptr)
			{
				queue = CreateScope<EventQueue<T>>();
			}
			return static_cast<EventQueue<T>&>(*queue);
		}
		// Drops every event of both buffers of every queue.
		void ClearEvents() noexcept;

		[[nodiscard]] CommandBuffer& GetCommandBuffer() noexcept { return m_CommandBuffer; }
		[[nodiscard]] SystemScheduler& GetScheduler() noexcept { return m_Scheduler; }

		// Runs one fixed simulation step: in Play mode advances the tick counter and time, then (both modes) swaps the
		// event buffers (D-017), flushes pending commands and runs the scheduler.
		void Tick(TimeStep timeStep, SimulationMode mode = SimulationMode::Play);

		// The state hash (D-013): every hashed field of every component of every entity (entities by UUID, components
		// by name, fields in declaration order; floating point by bit pattern; Derived components are left out, even
		// their presence), plus the random generator state, the tick, the simulated time and the events of both buffers
		// (D-017). Equal for worlds that hold the same state, whatever the order in which unrelated entities were
		// created or event types were first used.
		[[nodiscard]] uint64_t ComputeStateHash() const;

		[[nodiscard]] entt::registry& GetRegistry() noexcept { return m_Registry; }
		[[nodiscard]] const entt::registry& GetRegistry() const noexcept { return m_Registry; }
		[[nodiscard]] TypeRegistry& GetTypeRegistry() const noexcept { return m_Types; }

	private:
		[[nodiscard]] entt::entity FindHandle(UUID uuid) const;
		[[nodiscard]] UUID GetUUID(entt::entity handle) const;
		void AddRoot(UUID uuid);
		void RemoveRoot(UUID uuid);
		void SwapEventBuffers() noexcept;
		void DetachFromParent(entt::entity handle);
		void UpdateInactiveSubtree(entt::entity root, entt::entity ignoringDisabledOf = entt::null);
		void OnDisabledConstructed(entt::registry& registry, entt::entity handle);
		void OnDisabledDestroyed(entt::registry& registry, entt::entity handle);

		TypeRegistry& m_Types;
		entt::registry m_Registry;
		std::unordered_map<UUID, entt::entity> m_EntityMap; // lookups only, never iterated
		// Roots in the order they became roots: a list (stable iterators) with an index for O(1) removal.
		std::list<UUID> m_Roots;
		std::unordered_map<UUID, std::list<UUID>::iterator> m_RootPositions; // lookups only

		Random m_Random;
		uint64_t m_Seed = 0;
		uint64_t m_Tick = 0;
		double m_Time = 0.0;
		float m_DeltaTime = 0.0f;
		std::map<uint32_t, Scope<Internal::IEventQueue>> m_EventQueues; // by entt::type_index

		CommandBuffer m_CommandBuffer;
		SystemScheduler m_Scheduler;
		// Set while the world changes many entities at once (copying, destroying a subtree), when the inactive state is
		// either copied or irrelevant.
		bool m_SuspendInactiveUpdates = false;
		bool m_Ticking = false;
	};
}
