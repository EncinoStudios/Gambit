#pragma once

#include "Gambit/Core/Base.h"
#include "Gambit/Reflection/DynamicComponent.h"
#include "Gambit/Reflection/FieldInfo.h"

#include <entt/core/fwd.hpp>
#include <entt/entity/fwd.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Gambit
{
	class Entity;
	class World;

	// Identifies a component type within a TypeRegistry and names its EnTT storage: the EnTT type hash for C++
	// components, the hash of the name for script components. Not stable across builds or platforms; files, Lua and
	// the CLI use component names.
	using ComponentId = uint32_t;

	// How tools treat a component type (D-022).
	enum class ComponentFlags : uint8_t
	{
		None = 0,
		Serializable = GB_BIT(0),  // stored in scene and prefab files
		EditorVisible = GB_BIT(1), // shown by the inspector and offered to "add component"
		Removable = GB_BIT(2),     // tools and scripts may remove it (ComponentInfo::Remove)
		Internal = GB_BIT(3),      // engine-managed: generic access refuses to add, remove, copy, write or iterate it
		Derived = GB_BIT(4),       // recomputed by a system from other state: left out of the state hash (even its
		                           // presence) and of files
		Default = GB_BIT(0) | GB_BIT(1) | GB_BIT(2)
	};

	[[nodiscard]] constexpr ComponentFlags operator|(ComponentFlags a, ComponentFlags b) noexcept
	{
		return static_cast<ComponentFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
	}

	// Whether every bit of `flag` is set.
	[[nodiscard]] constexpr bool HasFlag(ComponentFlags flags, ComponentFlags flag) noexcept
	{
		return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(flag)) == static_cast<uint8_t>(flag)
			&& flag != ComponentFlags::None;
	}

	// Reflection data of one component type, C++ or script-defined, and the type-erased operations every tool uses
	// to work with it on a World (D-022): Has/Add/Remove/GetRaw/CopyTo/ForEach. Created and owned by a TypeRegistry;
	// a ComponentInfo stays valid until its type is unregistered (only script components can be). The raw component
	// pointers these functions hand out stay valid until a component of the same type is added to or removed from the
	// world.
	//
	// These generic operations never change Internal components (identity, hierarchy, derived state): the World owns
	// their invariants and offers dedicated APIs (CreateEntityWithUUID, SetParent, SetEnabled). Add, Remove, CopyTo,
	// SetFieldValue and the writable GetRaw and ForEach refuse them; reading them is allowed (GetFieldValue and the
	// const GetRaw).
	class ComponentInfo
	{
		struct ConstructionKey
		{
			explicit ConstructionKey() = default;
		};

	public:
		explicit ComponentInfo(ConstructionKey) {}

		[[nodiscard]] const std::string& GetName() const noexcept { return m_Name; }
		[[nodiscard]] ComponentId GetId() const noexcept { return m_Id; }
		[[nodiscard]] ComponentFlags GetFlags() const noexcept { return m_Flags; }
		[[nodiscard]] bool HasFlag(ComponentFlags flag) const noexcept { return Gambit::HasFlag(m_Flags, flag); }
		// Script-defined (stored as DynamicComponent).
		[[nodiscard]] bool IsDynamic() const noexcept { return m_IsDynamic; }
		// An empty C++ struct: presence is all there is (GetRaw returns a non-null placeholder without data).
		[[nodiscard]] bool IsTag() const noexcept { return m_IsTag; }

		// In registration (declaration) order.
		[[nodiscard]] std::span<const FieldInfo> GetFields() const noexcept { return m_Fields; }
		[[nodiscard]] const FieldInfo* FindField(std::string_view name) const noexcept;

		[[nodiscard]] bool Has(Entity entity) const;
		[[nodiscard]] bool Has(const World& world, entt::entity entity) const;

		// Adds the component with its default values and returns it. Returns nullptr, changing nothing, when the
		// entity already has it or the component is Internal.
		void* Add(Entity entity) const;

		// Removes the component. False, changing nothing, when the entity does not have it or the component is not
		// Removable (or Internal).
		bool Remove(Entity entity) const;

		// The writable component of `entity`: nullptr when it has none or the component is Internal.
		[[nodiscard]] void* GetRaw(Entity entity) const;
		// The component for reading, Internal ones included; nullptr when the entity has none.
		[[nodiscard]] const void* GetRaw(const World& world, entt::entity entity) const;

		// Copies the component of `source` onto `destination`, adding it or replacing its value, and returns true.
		// Returns false, changing nothing, for an Internal component. The entities may belong to different worlds that
		// share this registry; `source` must have the component.
		bool CopyTo(Entity source, Entity destination) const;
		bool
		CopyTo(const World& sourceWorld, entt::entity source, World& destinationWorld, entt::entity destination) const;

		// Calls `function` for every entity that has the component, in storage order. Iterates a snapshot, so the
		// function may add, remove or destroy; entities that lose the component before their turn are skipped. Does
		// nothing for Internal components, whose raw storage is not for writing.
		void ForEach(World& world, const std::function<void(Entity, void*)>& function) const;

		// The EnTT storage of this component in `world`, nullptr while the world has never held one (for runtime views
		// and for walking a storage in order).
		[[nodiscard]] const entt::sparse_set* FindStorage(const World& world) const;
		[[nodiscard]] entt::sparse_set* FindStorage(World& world) const;

		// Field access by name; nullopt / false when the entity lacks the component, the field does not exist or (for
		// SetFieldValue) the value does not fit the field (FieldInfo::CanHold) or the component is Internal. Fields
		// marked ReadOnly are not refused here: that flag tells tools (the inspector, the CLI, the bulk editor) not to
		// offer edits, while code (command buffers, loaders) may write them.
		[[nodiscard]] std::optional<FieldValue> GetFieldValue(Entity entity, std::string_view field) const;
		[[nodiscard]] bool SetFieldValue(Entity entity, std::string_view field, const FieldValue& value) const;

	private:
		friend class TypeRegistry;
		friend class World; // World::Copy reproduces every component, Internal ones included

		void CopyToUnchecked(
			const World& sourceWorld, entt::entity source, World& destinationWorld, entt::entity destination) const;
		[[nodiscard]] entt::sparse_set* GetStorage(World& world, bool create) const;
		[[nodiscard]] void* GetValue(entt::sparse_set& storage, entt::entity entity) const;
		[[nodiscard]] const void* GetValue(const entt::sparse_set& storage, entt::entity entity) const;

		std::string m_Name;
		ComponentId m_Id = 0;
		ComponentFlags m_Flags = ComponentFlags::Default;
		bool m_IsDynamic = false;
		bool m_IsTag = false;
		std::vector<FieldInfo> m_Fields;

		// Storage binding: the element type of the storage, how to create it, how to copy-assign one component onto
		// another, and (script components) the instance new components are copied from.
		const entt::type_info* m_StorageType = nullptr;
		entt::sparse_set& (*m_AssureStorage)(entt::registry& registry, ComponentId id) = nullptr;
		void (*m_Assign)(void* destination, const void* source) = nullptr;
		DynamicComponent m_Prototype;
	};
}
