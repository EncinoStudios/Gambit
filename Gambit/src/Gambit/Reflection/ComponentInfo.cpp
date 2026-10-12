#include "gbpch.h"

#include "Gambit/Reflection/ComponentInfo.h"

#include "Gambit/ECS/Entity.h"
#include "Gambit/ECS/World.h"
#include "Gambit/Reflection/TypeRegistry.h"

namespace Gambit
{
	namespace
	{
		// What GetRaw returns for a tag component that is present: EnTT stores no instances of empty types, and a
		// null pointer would read as "absent". Never dereferenced (tags have no fields).
		std::byte s_TagPlaceholder{};

		void AssertSameRegistry([[maybe_unused]] const ComponentInfo& info, [[maybe_unused]] const World& world)
		{
			GB_CORE_ASSERT(
				world.GetTypeRegistry().FindComponent(info.GetId()) == &info,
				"component '{}' does not belong to the world's TypeRegistry", info.GetName());
		}
	}

	const FieldInfo* ComponentInfo::FindField(std::string_view name) const noexcept
	{
		for (const FieldInfo& field : m_Fields)
		{
			if (field.Name == name)
			{
				return &field;
			}
		}
		return nullptr;
	}

	const entt::sparse_set* ComponentInfo::FindStorage(const World& world) const
	{
		AssertSameRegistry(*this, world);
		const entt::sparse_set* storage = world.GetRegistry().storage(m_Id);
		// Ids are checked for collisions when types are registered, so a storage of another type is a bug.
		GB_CORE_VERIFY(
			storage == nullptr || storage->info() == *m_StorageType, "the storage of component '{}' has another type",
			m_Name);
		return storage;
	}

	entt::sparse_set* ComponentInfo::FindStorage(World& world) const
	{
		return const_cast<entt::sparse_set*>(FindStorage(std::as_const(world)));
	}

	entt::sparse_set* ComponentInfo::GetStorage(World& world, bool create) const
	{
		entt::sparse_set* storage = FindStorage(world);
		if (storage == nullptr && create)
		{
			storage = &m_AssureStorage(world.GetRegistry(), m_Id);
		}
		return storage;
	}

	void* ComponentInfo::GetValue(entt::sparse_set& storage, entt::entity entity) const
	{
		return m_IsTag ? static_cast<void*>(&s_TagPlaceholder) : storage.value(entity);
	}

	const void* ComponentInfo::GetValue(const entt::sparse_set& storage, entt::entity entity) const
	{
		return m_IsTag ? static_cast<const void*>(&s_TagPlaceholder) : storage.value(entity);
	}

	bool ComponentInfo::Has(Entity entity) const
	{
		GB_CORE_ASSERT(entity.IsValid(), "component '{}': invalid entity", m_Name);
		return Has(*entity.GetWorld(), entity.GetHandle());
	}

	bool ComponentInfo::Has(const World& world, entt::entity entity) const
	{
		const entt::sparse_set* storage = FindStorage(world);
		return storage != nullptr && storage->contains(entity);
	}

	void* ComponentInfo::Add(Entity entity) const
	{
		GB_CORE_ASSERT(entity.IsValid(), "component '{}': invalid entity", m_Name);
		if (HasFlag(ComponentFlags::Internal))
		{
			return nullptr;
		}
		entt::sparse_set& storage = *GetStorage(*entity.GetWorld(), true);
		if (storage.contains(entity.GetHandle()))
		{
			return nullptr;
		}
		// push() default-constructs C++ components and copies the prototype (the schema defaults) of script ones.
		storage.push(entity.GetHandle(), m_IsDynamic ? &m_Prototype : nullptr);
		return GetValue(storage, entity.GetHandle());
	}

	bool ComponentInfo::Remove(Entity entity) const
	{
		GB_CORE_ASSERT(entity.IsValid(), "component '{}': invalid entity", m_Name);
		if (!HasFlag(ComponentFlags::Removable) || HasFlag(ComponentFlags::Internal))
		{
			return false;
		}
		entt::sparse_set* storage = FindStorage(*entity.GetWorld());
		return storage != nullptr && storage->remove(entity.GetHandle());
	}

	void* ComponentInfo::GetRaw(Entity entity) const
	{
		GB_CORE_ASSERT(entity.IsValid(), "component '{}': invalid entity", m_Name);
		if (HasFlag(ComponentFlags::Internal))
		{
			return nullptr;
		}
		entt::sparse_set* storage = FindStorage(*entity.GetWorld());
		if (storage == nullptr || !storage->contains(entity.GetHandle()))
		{
			return nullptr;
		}
		return GetValue(*storage, entity.GetHandle());
	}

	const void* ComponentInfo::GetRaw(const World& world, entt::entity entity) const
	{
		const entt::sparse_set* storage = FindStorage(world);
		if (storage == nullptr || !storage->contains(entity))
		{
			return nullptr;
		}
		return GetValue(*storage, entity);
	}

	bool ComponentInfo::CopyTo(Entity source, Entity destination) const
	{
		GB_CORE_ASSERT(source.IsValid() && destination.IsValid(), "component '{}': invalid entity", m_Name);
		return CopyTo(*source.GetWorld(), source.GetHandle(), *destination.GetWorld(), destination.GetHandle());
	}

	bool ComponentInfo::CopyTo(
		const World& sourceWorld, entt::entity source, World& destinationWorld, entt::entity destination) const
	{
		if (HasFlag(ComponentFlags::Internal))
		{
			return false;
		}
		CopyToUnchecked(sourceWorld, source, destinationWorld, destination);
		return true;
	}

	void ComponentInfo::CopyToUnchecked(
		const World& sourceWorld, entt::entity source, World& destinationWorld, entt::entity destination) const
	{
		const entt::sparse_set* sourceStorage = FindStorage(sourceWorld);
		GB_CORE_ASSERT(
			sourceStorage != nullptr && sourceStorage->contains(source), "copying component '{}' the source lacks",
			m_Name);
		entt::sparse_set& destinationStorage = *GetStorage(destinationWorld, true);
		// Storage pages never move, so the source stays valid while the destination storage grows (same world).
		const void* value = m_IsTag ? nullptr : sourceStorage->value(source);
		if (destinationStorage.contains(destination))
		{
			if (!m_IsTag)
			{
				m_Assign(destinationStorage.value(destination), value);
			}
			return;
		}
		destinationStorage.push(destination, value);
	}

	void ComponentInfo::ForEach(World& world, const std::function<void(Entity, void*)>& function) const
	{
		entt::sparse_set* storage = HasFlag(ComponentFlags::Internal) ? nullptr : FindStorage(world);
		if (storage == nullptr)
		{
			return;
		}
		const std::vector<entt::entity> snapshot(storage->data(), storage->data() + storage->size());
		for (const entt::entity entity : snapshot)
		{
			if (storage->contains(entity))
			{
				function(Entity(entity, &world), GetValue(*storage, entity));
			}
		}
	}

	std::optional<FieldValue> ComponentInfo::GetFieldValue(Entity entity, std::string_view field) const
	{
		GB_CORE_ASSERT(entity.IsValid(), "component '{}': invalid entity", m_Name);
		const FieldInfo* info = FindField(field);
		const void* component = info != nullptr ? GetRaw(*entity.GetWorld(), entity.GetHandle()) : nullptr;
		if (component == nullptr)
		{
			return std::nullopt;
		}
		return info->GetValue(component);
	}

	bool ComponentInfo::SetFieldValue(Entity entity, std::string_view field, const FieldValue& value) const
	{
		const FieldInfo* info = FindField(field);
		void* component = info != nullptr ? GetRaw(entity) : nullptr;
		return component != nullptr && info->SetValue(component, value);
	}
}
