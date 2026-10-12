#pragma once

#include "Gambit/Core/Assert.h"
#include "Gambit/Core/UUID.h"
#include "Gambit/ECS/Components/CoreComponents.h"
#include "Gambit/ECS/World.h"
#include "Gambit/Reflection/TypeRegistry.h"

#include <entt/core/type_info.hpp>
#include <entt/entity/registry.hpp>

#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace Gambit
{
	// A handle to an entity of a World (D-021): its EnTT identifier plus the world. Cheap to copy; it dangles once the
	// entity is destroyed (IsValid tells). Every operation asserts that the handle is valid, Get/Remove that the
	// component is present, Add that it is not.
	class Entity
	{
	public:
		Entity() = default;
		Entity(entt::entity handle, World* world) noexcept
			: m_Handle(handle), m_World(world)
		{}

		// Returns the new component (nothing for tag components). T must be registered in the world's TypeRegistry.
		template<typename T, typename... Args>
		decltype(auto) AddComponent(Args&&... args)
		{
			AssertManagedByUser<T>();
			AssertValid();
			GB_CORE_ASSERT(
				m_World->GetTypeRegistry().template FindComponent<T>() != nullptr,
				"component type {} is not registered", entt::type_name<T>::value());
			GB_CORE_ASSERT(
				!HasComponent<T>(), "entity {} already has component {}", GetUUID().ToString(),
				entt::type_name<T>::value());
			return m_World->GetRegistry().emplace<T>(m_Handle, std::forward<Args>(args)...);
		}

		template<typename T, typename... Args>
		decltype(auto) AddOrReplaceComponent(Args&&... args)
		{
			AssertManagedByUser<T>();
			AssertValid();
			GB_CORE_ASSERT(
				m_World->GetTypeRegistry().template FindComponent<T>() != nullptr,
				"component type {} is not registered", entt::type_name<T>::value());
			return m_World->GetRegistry().emplace_or_replace<T>(m_Handle, std::forward<Args>(args)...);
		}

		template<typename T>
		[[nodiscard]] T& GetComponent()
		{
			static_assert(!std::is_empty_v<T>, "tag components hold no data: use HasComponent");
			AssertValid();
			GB_CORE_ASSERT(
				HasComponent<T>(), "entity {} has no component {}", GetUUID().ToString(), entt::type_name<T>::value());
			return m_World->GetRegistry().get<T>(m_Handle);
		}

		template<typename T>
		[[nodiscard]] const T& GetComponent() const
		{
			static_assert(!std::is_empty_v<T>, "tag components hold no data: use HasComponent");
			AssertValid();
			GB_CORE_ASSERT(
				HasComponent<T>(), "entity {} has no component {}", GetUUID().ToString(), entt::type_name<T>::value());
			return std::as_const(*m_World).GetRegistry().get<T>(m_Handle);
		}

		// Null when the entity does not have the component.
		template<typename T>
		[[nodiscard]] T* TryGetComponent()
		{
			static_assert(!std::is_empty_v<T>, "tag components hold no data: use HasComponent");
			AssertValid();
			return m_World->GetRegistry().try_get<T>(m_Handle);
		}

		template<typename T>
		[[nodiscard]] const T* TryGetComponent() const
		{
			static_assert(!std::is_empty_v<T>, "tag components hold no data: use HasComponent");
			AssertValid();
			return std::as_const(*m_World).GetRegistry().try_get<T>(m_Handle);
		}

		template<typename T>
		[[nodiscard]] bool HasComponent() const
		{
			AssertValid();
			return std::as_const(*m_World).GetRegistry().all_of<T>(m_Handle);
		}

		template<typename T>
		void RemoveComponent()
		{
			AssertManagedByUser<T>();
			AssertValid();
			GB_CORE_ASSERT(
				HasComponent<T>(), "entity {} has no component {}", GetUUID().ToString(), entt::type_name<T>::value());
			m_World->GetRegistry().remove<T>(m_Handle);
		}

		[[nodiscard]] UUID GetUUID() const { return GetComponent<IDComponent>().ID; }
		[[nodiscard]] const std::string& GetName() const { return GetComponent<TagComponent>().Name; }
		void SetName(std::string_view name) { GetComponent<TagComponent>().Name = std::string(name); }

		// Not null and alive.
		[[nodiscard]] bool IsValid() const
		{
			return m_World != nullptr && std::as_const(*m_World).GetRegistry().valid(m_Handle);
		}
		[[nodiscard]] explicit operator bool() const { return IsValid(); }

		[[nodiscard]] entt::entity GetHandle() const noexcept { return m_Handle; }
		[[nodiscard]] World* GetWorld() const noexcept { return m_World; }

		friend bool operator==(const Entity&, const Entity&) noexcept = default;

	private:
		void AssertValid() const { GB_CORE_ASSERT(IsValid(), "using an invalid entity handle"); }

		// The identity components and the inactive state belong to the World.
		template<typename T>
		static constexpr void AssertManagedByUser()
		{
			static_assert(
				!std::is_same_v<T, IDComponent> && !std::is_same_v<T, TagComponent>
					&& !std::is_same_v<T, RelationshipComponent>,
				"every entity always has its ID, Tag and Relationship components");
			static_assert(!std::is_same_v<T, InactiveComponent>, "the World maintains InactiveComponent (SetEnabled)");
		}

		entt::entity m_Handle = entt::null;
		World* m_World = nullptr;
	};
}
