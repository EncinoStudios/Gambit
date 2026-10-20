#include "gbpch.h"

#include "Gambit/ECS/World.h"

#include "Gambit/Core/Hash.h"
#include "Gambit/ECS/Entity.h"
#include "Gambit/Reflection/FieldHashing.h"
#include "Gambit/Reflection/TypeRegistry.h"

namespace Gambit
{
	namespace
	{
		constexpr std::string_view DefaultEntityName = "Entity";
		// Versions the layout of the state hash: change it whenever what is hashed, or how, changes.
		constexpr uint64_t StateHashSeed = "Gambit.World.StateHash.2"_hash;
	}

	World::World()
		: World(TypeRegistry::Get())
	{}

	World::World(TypeRegistry& types)
		: m_Types(types), m_CommandBuffer(*this), m_Scheduler(*this)
	{
		GB_CORE_VERIFY(
			types.FindComponent<IDComponent>() != nullptr && types.FindComponent<TagComponent>() != nullptr
				&& types.FindComponent<RelationshipComponent>() != nullptr
				&& types.FindComponent<TransformComponent>() != nullptr
				&& types.FindComponent<WorldTransformComponent>() != nullptr
				&& types.FindComponent<DisabledComponent>() != nullptr
				&& types.FindComponent<InactiveComponent>() != nullptr,
			"a World needs the core components in its TypeRegistry (RegisterEngineTypes)");
		m_Types.AttachWorld(*this);
		m_Registry.on_construct<DisabledComponent>().connect<&World::OnDisabledConstructed>(*this);
		m_Registry.on_destroy<DisabledComponent>().connect<&World::OnDisabledDestroyed>(*this);
	}

	World::~World()
	{
		m_Scheduler.RemoveAllSystems();
		// Components are destroyed before the maps the signal handlers read are gone.
		m_SuspendInactiveUpdates = true;
		m_Registry.clear();
		m_Registry.on_construct<DisabledComponent>().disconnect(this);
		m_Registry.on_destroy<DisabledComponent>().disconnect(this);
		m_Types.DetachWorld(*this);
	}

	Scope<World> World::Copy(const World& source)
	{
		GB_CORE_ASSERT(!source.m_Ticking, "copying a world while it ticks");
		auto copy = CreateScope<World>(source.m_Types);
		World& destination = *copy;
		destination.m_Random = source.m_Random;
		destination.m_Seed = source.m_Seed;
		destination.m_Tick = source.m_Tick;
		destination.m_Time = source.m_Time;
		destination.m_DeltaTime = source.m_DeltaTime;

		// The entity storage as it is (like an EnTT snapshot): the alive identifiers in [0, free_list()) and the
		// destroyed ones awaiting reuse after them, so entities created later get the same identifiers in both worlds.
		// Then every component storage in the source's order: systems iterate EnTT storages in their internal order, so
		// the copy must match it to evolve exactly like the source. The inactive state is copied as a component rather
		// than recomputed while the hierarchy is still incomplete.
		destination.m_SuspendInactiveUpdates = true;
		const auto& sourceEntities = *source.m_Registry.storage<entt::entity>();
		for (const entt::entity handle : std::span(sourceEntities.data(), sourceEntities.size()))
		{
			const entt::entity created = destination.m_Registry.create(handle);
			GB_CORE_VERIFY(created == handle, "World::Copy could not reproduce an entity identifier");
		}
		destination.m_Registry.storage<entt::entity>().free_list(sourceEntities.free_list());
		for (const ComponentInfo* info : source.m_Types.GetComponents())
		{
			const entt::sparse_set* storage = info->FindStorage(source);
			if (storage == nullptr)
			{
				continue;
			}
			for (size_t index = 0; index < storage->size(); ++index)
			{
				const entt::entity handle = storage->data()[index];
				info->CopyToUnchecked(source, handle, destination, handle);
			}
		}
		destination.m_SuspendInactiveUpdates = false;

		destination.m_EntityMap = source.m_EntityMap;
		for (const UUID root : source.m_Roots)
		{
			destination.AddRoot(root);
		}
		for (const auto& [type, queue] : source.m_EventQueues)
		{
			destination.m_EventQueues.emplace(type, queue->Clone());
		}
		destination.m_CommandBuffer.m_Commands = source.m_CommandBuffer.m_Commands;
		destination.m_CommandBuffer.m_ReservedUUIDs = source.m_CommandBuffer.m_ReservedUUIDs;
		return copy;
	}

	// ---- Entities ----

	Entity World::CreateEntity(std::string_view name)
	{
		return CreateEntityWithUUID(GenerateUUID(), name);
	}

	Entity World::CreateEntityWithUUID(UUID uuid, std::string_view name)
	{
		if (!uuid.IsValid())
		{
			GB_CORE_ERROR("World: an entity cannot have the null UUID");
			return {};
		}
		if (m_EntityMap.contains(uuid))
		{
			GB_CORE_ERROR("World: UUID {} is already used by another entity", uuid.ToString());
			return {};
		}

		const entt::entity handle = m_Registry.create();
		m_Registry.emplace<IDComponent>(handle, uuid);
		m_Registry.emplace<TagComponent>(handle, std::string(name.empty() ? DefaultEntityName : name));
		m_Registry.emplace<RelationshipComponent>(handle);
		m_EntityMap.emplace(uuid, handle);
		AddRoot(uuid);
		return Entity(handle, this);
	}

	void World::DestroyEntity(Entity entity)
	{
		GB_CORE_ASSERT(entity.GetWorld() == this && entity.IsValid(), "destroying an invalid entity");
		const entt::entity root = entity.GetHandle();
		DetachFromParent(root);

		// The subtree in pre-order (sibling order: deterministic), destroyed leaves first.
		std::vector<entt::entity> subtree{ root };
		for (size_t index = 0; index < subtree.size(); ++index)
		{
			for (const UUID child : m_Registry.get<RelationshipComponent>(subtree[index]).Children)
			{
				subtree.push_back(FindHandle(child));
			}
		}
		m_SuspendInactiveUpdates = true; // the whole subtree goes: nothing left to update
		for (auto handle = subtree.rbegin(); handle != subtree.rend(); ++handle)
		{
			m_EntityMap.erase(GetUUID(*handle));
			m_Registry.destroy(*handle);
		}
		m_SuspendInactiveUpdates = false;
	}

	Entity World::TryGetEntity(UUID uuid)
	{
		const entt::entity handle = FindHandle(uuid);
		return handle == entt::null ? Entity() : Entity(handle, this);
	}

	bool World::ContainsEntity(UUID uuid) const
	{
		return m_EntityMap.contains(uuid);
	}

	Entity World::FindEntityByName(std::string_view name)
	{
		entt::entity found = entt::null;
		UUID foundUUID;
		for (const auto [handle, tag, id] : m_Registry.view<TagComponent, IDComponent>().each())
		{
			if (tag.Name == name && (found == entt::null || id.ID < foundUUID))
			{
				found = handle;
				foundUUID = id.ID;
			}
		}
		return found == entt::null ? Entity() : Entity(found, this);
	}

	std::vector<UUID> World::GetEntityUUIDs() const
	{
		std::vector<UUID> uuids;
		uuids.reserve(m_EntityMap.size());
		for (const auto& [uuid, handle] : m_EntityMap)
		{
			uuids.push_back(uuid);
		}
		std::ranges::sort(uuids);
		return uuids;
	}

	UUID World::GenerateUUID()
	{
		for (;;)
		{
			const UUID uuid(m_Random.NextU64());
			if (uuid.IsValid() && !m_EntityMap.contains(uuid) && !m_CommandBuffer.m_ReservedUUIDs.contains(uuid))
			{
				return uuid;
			}
		}
	}

	// ---- Hierarchy ----

	Result<void> World::SetParent(Entity child, Entity parent, bool keepWorldTransform)
	{
		GB_CORE_ASSERT(child.GetWorld() == this && child.IsValid(), "SetParent: invalid child");
		GB_CORE_ASSERT(
			parent.GetWorld() == nullptr || (parent.GetWorld() == this && parent.IsValid()),
			"SetParent: invalid parent");
		const bool hasParent = parent.GetWorld() != nullptr;
		if (hasParent && (parent == child || IsAncestorOf(child, parent)))
		{
			return Error::Format(
				"cannot make '{}' a child of '{}': the hierarchy would contain a cycle", child.GetName(),
				parent.GetName());
		}

		const UUID childUUID = child.GetUUID();
		const UUID parentUUID = hasParent ? parent.GetUUID() : UUID();
		if (m_Registry.get<RelationshipComponent>(child.GetHandle()).Parent == parentUUID)
		{
			return {};
		}

		const glm::mat4 worldTransform = keepWorldTransform ? ComputeWorldTransform(child) : glm::mat4(1.0f);
		DetachFromParent(child.GetHandle());
		if (hasParent)
		{
			m_Registry.get<RelationshipComponent>(parent.GetHandle()).Children.push_back(childUUID);
			m_Registry.get<RelationshipComponent>(child.GetHandle()).Parent = parentUUID;
		}
		else
		{
			AddRoot(childUUID);
		}

		if (keepWorldTransform)
		{
			if (auto* transform = m_Registry.try_get<TransformComponent>(child.GetHandle()))
			{
				const glm::mat4 parentTransform = hasParent ? ComputeWorldTransform(parent) : glm::mat4(1.0f);
				const glm::mat4 local = glm::inverse(parentTransform) * worldTransform;
				if (!DecomposeTransform(local, transform->Translation, transform->Rotation, transform->Scale))
				{
					GB_CORE_WARN(
						"SetParent: the world transform of '{}' cannot be kept under '{}' (degenerate parent "
						"transform)",
						child.GetName(), hasParent ? parent.GetName() : std::string("the root"));
				}
			}
		}
		UpdateInactiveSubtree(child.GetHandle());
		return {};
	}

	void World::SetSiblingIndex(Entity entity, size_t index)
	{
		GB_CORE_ASSERT(entity.GetWorld() == this && entity.IsValid(), "SetSiblingIndex: invalid entity");
		const UUID uuid = entity.GetUUID();
		const UUID parent = m_Registry.get<RelationshipComponent>(entity.GetHandle()).Parent;
		if (parent.IsValid())
		{
			std::vector<UUID>& siblings = m_Registry.get<RelationshipComponent>(FindHandle(parent)).Children;
			const auto current = std::ranges::find(siblings, uuid);
			GB_CORE_ASSERT(current != siblings.end(), "the hierarchy is inconsistent");
			siblings.erase(current);
			siblings.insert(siblings.begin() + static_cast<std::ptrdiff_t>(std::min(index, siblings.size())), uuid);
			return;
		}
		RemoveRoot(uuid);
		const auto target = std::next(m_Roots.begin(), static_cast<std::ptrdiff_t>(std::min(index, m_Roots.size())));
		m_RootPositions.emplace(uuid, m_Roots.insert(target, uuid));
	}

	size_t World::GetSiblingIndex(Entity entity) const
	{
		GB_CORE_ASSERT(entity.GetWorld() == this && entity.IsValid(), "GetSiblingIndex: invalid entity");
		const UUID uuid = entity.GetUUID();
		const UUID parent = m_Registry.get<RelationshipComponent>(entity.GetHandle()).Parent;
		if (parent.IsValid())
		{
			const std::vector<UUID>& siblings = m_Registry.get<RelationshipComponent>(FindHandle(parent)).Children;
			return static_cast<size_t>(std::ranges::find(siblings, uuid) - siblings.begin());
		}
		const std::list<UUID>::const_iterator position = m_RootPositions.at(uuid);
		return static_cast<size_t>(std::distance(m_Roots.cbegin(), position));
	}

	Entity World::GetParent(Entity entity)
	{
		GB_CORE_ASSERT(entity.GetWorld() == this && entity.IsValid(), "GetParent: invalid entity");
		return TryGetEntity(m_Registry.get<RelationshipComponent>(entity.GetHandle()).Parent);
	}

	std::vector<Entity> World::GetChildren(Entity entity)
	{
		GB_CORE_ASSERT(entity.GetWorld() == this && entity.IsValid(), "GetChildren: invalid entity");
		const std::vector<UUID>& children = m_Registry.get<RelationshipComponent>(entity.GetHandle()).Children;
		std::vector<Entity> result;
		result.reserve(children.size());
		for (const UUID child : children)
		{
			result.emplace_back(FindHandle(child), this);
		}
		return result;
	}

	std::vector<Entity> World::GetRoots()
	{
		std::vector<Entity> roots;
		roots.reserve(m_Roots.size());
		for (const UUID root : m_Roots)
		{
			roots.emplace_back(FindHandle(root), this);
		}
		return roots;
	}

	bool World::IsAncestorOf(Entity ancestor, Entity entity) const
	{
		GB_CORE_ASSERT(
			ancestor.GetWorld() == this && ancestor.IsValid() && entity.GetWorld() == this && entity.IsValid(),
			"IsAncestorOf: invalid entity");
		const UUID ancestorUUID = m_Registry.get<IDComponent>(ancestor.GetHandle()).ID;
		UUID current = m_Registry.get<RelationshipComponent>(entity.GetHandle()).Parent;
		while (current.IsValid())
		{
			if (current == ancestorUUID)
			{
				return true;
			}
			current = m_Registry.get<RelationshipComponent>(FindHandle(current)).Parent;
		}
		return false;
	}

	glm::mat4 World::ComputeWorldTransform(Entity entity) const
	{
		GB_CORE_ASSERT(entity.GetWorld() == this && entity.IsValid(), "ComputeWorldTransform: invalid entity");
		std::vector<entt::entity> chain{ entity.GetHandle() };
		for (UUID parent = m_Registry.get<RelationshipComponent>(entity.GetHandle()).Parent; parent.IsValid();)
		{
			const entt::entity handle = FindHandle(parent);
			chain.push_back(handle);
			parent = m_Registry.get<RelationshipComponent>(handle).Parent;
		}
		// The same products in the same order as the TransformSystem: parent world * local, from the root down.
		glm::mat4 matrix(1.0f);
		for (auto handle = chain.rbegin(); handle != chain.rend(); ++handle)
		{
			if (const auto* transform = m_Registry.try_get<TransformComponent>(*handle))
			{
				matrix = matrix * transform->GetMatrix();
			}
		}
		return matrix;
	}

	// ---- Enabled state ----

	void World::SetEnabled(Entity entity, bool enabled)
	{
		GB_CORE_ASSERT(entity.GetWorld() == this && entity.IsValid(), "SetEnabled: invalid entity");
		const bool disabled = m_Registry.all_of<DisabledComponent>(entity.GetHandle());
		if (enabled && disabled)
		{
			m_Registry.remove<DisabledComponent>(entity.GetHandle());
		}
		else if (!enabled && !disabled)
		{
			m_Registry.emplace<DisabledComponent>(entity.GetHandle());
		}
	}

	bool World::IsEnabled(Entity entity) const
	{
		GB_CORE_ASSERT(entity.GetWorld() == this && entity.IsValid(), "IsEnabled: invalid entity");
		return !m_Registry.all_of<DisabledComponent>(entity.GetHandle());
	}

	bool World::IsActive(Entity entity) const
	{
		GB_CORE_ASSERT(entity.GetWorld() == this && entity.IsValid(), "IsActive: invalid entity");
		return !m_Registry.all_of<InactiveComponent>(entity.GetHandle());
	}

	// ---- Simulation ----

	void World::SetSeed(uint64_t seed)
	{
		m_Seed = seed;
		m_Random.Seed(seed);
	}

	void World::BeginSimulation(uint64_t seed)
	{
		GB_CORE_ASSERT(!m_Ticking, "World::BeginSimulation while the world ticks");
		SetSeed(seed);
		m_Tick = 0;
		m_Time = 0.0;
		m_DeltaTime = 0.0f;
		ClearEvents();
		m_CommandBuffer.Clear(); // a scene file stores no pending commands either
	}

	void World::ClearEvents() noexcept
	{
		for (auto& [type, queue] : m_EventQueues)
		{
			queue->Clear();
		}
	}

	void World::SwapEventBuffers() noexcept
	{
		for (auto& [type, queue] : m_EventQueues)
		{
			queue->Swap();
		}
	}

	void World::Tick(TimeStep timeStep, SimulationMode mode)
	{
		GB_CORE_ASSERT(!m_Ticking, "World::Tick is not reentrant");
		m_Ticking = true;
		if (mode == SimulationMode::Play)
		{
			++m_Tick;
			m_DeltaTime = timeStep.GetSeconds();
			m_Time += static_cast<double>(timeStep.GetSeconds());
		}
		SwapEventBuffers();
		m_CommandBuffer.Flush();
		m_Scheduler.Run(TimeStep(timeStep.GetSeconds(), m_Tick), mode);
		m_Ticking = false;
	}

	uint64_t World::ComputeStateHash() const
	{
		Hasher hasher(StateHashSeed);
		hasher.UpdateValue(m_Tick);
		hasher.UpdateValue(m_Time);
		const RandomState random = m_Random.GetState();
		hasher.UpdateValue(random.State);
		hasher.UpdateValue(random.Increment);

		// Events (D-017) in an order that needs no identity of their types (which differs between runs and
		// compilers): every queue holding events gets a digest of its own, and the sorted digests are hashed. Queues
		// without events count as absent.
		std::vector<uint64_t> eventDigests;
		for (const auto& [type, queue] : m_EventQueues)
		{
			if (!queue->IsEmpty())
			{
				Hasher queueHasher(StateHashSeed);
				queue->Hash(queueHasher);
				eventDigests.push_back(queueHasher.Digest());
			}
		}
		std::ranges::sort(eventDigests);
		hasher.UpdateValue(static_cast<uint64_t>(eventDigests.size()));
		for (const uint64_t digest : eventDigests)
		{
			hasher.UpdateValue(digest);
		}

		std::vector<std::pair<UUID, entt::entity>> entities(m_EntityMap.begin(), m_EntityMap.end());
		std::ranges::sort(entities, {}, &std::pair<UUID, entt::entity>::first);
		hasher.UpdateValue(static_cast<uint64_t>(entities.size()));

		// Components by name; types this world never stored have no storage and are skipped up front, and derived
		// components (recomputed from the hashed state, even their presence) are left out.
		std::vector<std::pair<const ComponentInfo*, const entt::sparse_set*>> storages;
		for (const ComponentInfo* info : m_Types.GetComponents())
		{
			if (info->HasFlag(ComponentFlags::Derived))
			{
				continue;
			}
			if (const entt::sparse_set* storage = info->FindStorage(*this); storage != nullptr)
			{
				storages.emplace_back(info, storage);
			}
		}

		// Fields are hashed in place (FieldInfo::HashValue): no copies, no allocations.
		for (const auto& [uuid, handle] : entities)
		{
			hasher.UpdateValue(uuid.GetValue());
			for (const auto& [info, storage] : storages)
			{
				if (!storage->contains(handle))
				{
					continue;
				}
				hasher.UpdateValue(uint8_t{ 1 }); // a component follows
				Internal::HashText(hasher, info->GetName());
				if (info->IsTag())
				{
					continue; // presence is all there is
				}
				const void* component = storage->value(handle);
				for (const FieldInfo& field : info->GetFields())
				{
					if (!field.HasFlag(FieldFlags::NotHashed))
					{
						field.HashValue(hasher, component);
					}
				}
			}
			hasher.UpdateValue(uint8_t{ 0 }); // end of the entity
		}
		return hasher.Digest();
	}

	// ---- Internals ----

	entt::entity World::FindHandle(UUID uuid) const
	{
		const auto found = m_EntityMap.find(uuid);
		return found == m_EntityMap.end() ? entt::null : found->second;
	}

	UUID World::GetUUID(entt::entity handle) const
	{
		return m_Registry.get<IDComponent>(handle).ID;
	}

	void World::AddRoot(UUID uuid)
	{
		GB_CORE_ASSERT(!m_RootPositions.contains(uuid), "{} is already a root", uuid.ToString());
		m_Roots.push_back(uuid);
		m_RootPositions.emplace(uuid, std::prev(m_Roots.end()));
	}

	void World::RemoveRoot(UUID uuid)
	{
		const auto found = m_RootPositions.find(uuid);
		GB_CORE_ASSERT(found != m_RootPositions.end(), "{} is not a root", uuid.ToString());
		m_Roots.erase(found->second);
		m_RootPositions.erase(found);
	}

	void World::DetachFromParent(entt::entity handle)
	{
		RelationshipComponent& relationship = m_Registry.get<RelationshipComponent>(handle);
		if (!relationship.Parent.IsValid())
		{
			RemoveRoot(GetUUID(handle));
			return;
		}
		std::vector<UUID>& siblings = m_Registry.get<RelationshipComponent>(FindHandle(relationship.Parent)).Children;
		const auto position = std::ranges::find(siblings, GetUUID(handle));
		GB_CORE_ASSERT(position != siblings.end(), "the hierarchy is inconsistent");
		siblings.erase(position);
		relationship.Parent = UUID();
	}

	void World::UpdateInactiveSubtree(entt::entity root, entt::entity ignoringDisabledOf)
	{
		const UUID parent = m_Registry.get<RelationshipComponent>(root).Parent;
		const bool parentInactive = parent.IsValid() && m_Registry.all_of<InactiveComponent>(FindHandle(parent));

		// Pre-order (children pushed in reverse), so the inactive storage changes in a deterministic order.
		std::vector<std::pair<entt::entity, bool>> pending{ { root, parentInactive } };
		while (!pending.empty())
		{
			const auto [handle, inheritedInactive] = pending.back();
			pending.pop_back();
			const bool disabled = handle != ignoringDisabledOf && m_Registry.all_of<DisabledComponent>(handle);
			const bool inactive = inheritedInactive || disabled;
			if (inactive != m_Registry.all_of<InactiveComponent>(handle))
			{
				if (inactive)
				{
					m_Registry.emplace<InactiveComponent>(handle);
				}
				else
				{
					m_Registry.remove<InactiveComponent>(handle);
				}
			}
			const std::vector<UUID>& children = m_Registry.get<RelationshipComponent>(handle).Children;
			for (auto child = children.rbegin(); child != children.rend(); ++child)
			{
				pending.emplace_back(FindHandle(*child), inactive);
			}
		}
	}

	void World::OnDisabledConstructed(entt::registry&, entt::entity handle)
	{
		if (!m_SuspendInactiveUpdates)
		{
			UpdateInactiveSubtree(handle);
		}
	}

	void World::OnDisabledDestroyed(entt::registry&, entt::entity handle)
	{
		// Runs before the component is gone, so the entity must be treated as no longer disabled.
		if (!m_SuspendInactiveUpdates)
		{
			UpdateInactiveSubtree(handle, handle);
		}
	}
}
