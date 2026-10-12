#pragma once

#include "Gambit/Core/Assert.h"
#include "Gambit/Core/Base.h"
#include "Gambit/Core/Result.h"
#include "Gambit/Reflection/ComponentInfo.h"
#include "Gambit/Reflection/FieldHashing.h"
#include "Gambit/Reflection/FieldInfo.h"

#include <entt/core/type_info.hpp>
#include <entt/entity/registry.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace Gambit
{
	class World;

	// One field of a script component schema (D-023).
	struct DynamicFieldSchema
	{
		std::string Name;
		FieldType Type = FieldType::None;
		FieldType ElementType = FieldType::None; // Array fields only
		FieldValue Default;                      // must hold the alternative of Type (an EnumInfo value for enums)
		FieldAttributes Attributes;              // Enum fields need Attributes.Enum, AssetHandle fields AssetType
	};

	// A script-defined component type: what Lua's Component("Name", { ... }) declares.
	struct DynamicComponentSchema
	{
		std::string Name;
		std::vector<DynamicFieldSchema> Fields;
		ComponentFlags Flags = ComponentFlags::Default;
	};

	template<typename T>
	class ComponentBuilder;
	template<typename E>
	class EnumBuilder;

	// The single source of truth about component types (D-022): every C++ component is registered here once (one
	// registration function per module, see RegisterEngineTypes), script components are registered at runtime, and
	// serialization, the inspector, Lua, the CLI, World::Copy and state hashing all work from this data alone.
	// Names are case-sensitive identifiers ([A-Za-z_][A-Za-z0-9_]*, at most MaxNameLength bytes).
	//
	// Worlds reference the registry they were created with, which must outlive them. Not thread safe: register,
	// unregister and look up from the thread that runs the worlds.
	class TypeRegistry
	{
	public:
		static constexpr size_t MaxNameLength = 128;

		// An empty registry; RegisterEngineTypes adds the engine's components.
		TypeRegistry() = default;
		~TypeRegistry();
		TypeRegistry(const TypeRegistry&) = delete;
		TypeRegistry& operator=(const TypeRegistry&) = delete;

		// The process-wide registry, with every engine type registered on first use. Worlds use it by default.
		[[nodiscard]] static TypeRegistry& Get();

		[[nodiscard]] static bool IsValidName(std::string_view name) noexcept;

		// Registers C++ component T under `name`; add its fields with the returned builder:
		//   registry.RegisterComponent<TransformComponent>("Transform").Field("Translation", &T::Translation)...
		// T must be a plain, default-constructible and copyable struct (D-020). Registering a name, id or type twice
		// is a programmer error (verified).
		template<typename T>
		ComponentBuilder<T> RegisterComponent(std::string_view name, ComponentFlags flags = ComponentFlags::Default);

		// Registers enum E; fields of type E then become Enum fields. Register enums before the components using them.
		template<typename E>
		EnumBuilder<E> RegisterEnum(std::string_view name);

		// Registers a script component. Registering an identical schema under an existing name returns the existing
		// type (scripts are reloaded); a different schema under a taken name, an invalid name or an inconsistent
		// field is an error.
		[[nodiscard]] Result<const ComponentInfo*> RegisterDynamicComponent(const DynamicComponentSchema& schema);
		// Unregisters a script component and removes every instance of it from every world using this registry.
		// ComponentInfo pointers to it become invalid. Must not run while a world iterates that component.
		[[nodiscard]] Result<void> UnregisterDynamicComponent(std::string_view name);

		[[nodiscard]] const ComponentInfo* FindComponent(std::string_view name) const;
		[[nodiscard]] const ComponentInfo* FindComponent(ComponentId id) const;
		template<typename T>
		[[nodiscard]] const ComponentInfo* FindComponent() const
		{
			const auto found = m_ComponentsByType.find(entt::type_index<T>::value());
			return found == m_ComponentsByType.end() ? nullptr : found->second;
		}
		// Like FindComponent<T>, but T must be registered (verified).
		template<typename T>
		[[nodiscard]] const ComponentInfo& GetComponent() const
		{
			const ComponentInfo* info = FindComponent<T>();
			GB_CORE_VERIFY(info != nullptr, "component type {} is not registered", entt::type_name<T>::value());
			return *info;
		}
		// Every registered component, sorted by name (byte-wise): the deterministic order of hashing and files. The
		// span is invalidated by the next registration change.
		[[nodiscard]] std::span<const ComponentInfo* const> GetComponents() const noexcept { return m_Sorted; }

		[[nodiscard]] Ref<const EnumInfo> FindEnum(std::string_view name) const;
		template<typename E>
		[[nodiscard]] Ref<const EnumInfo> FindEnum() const
		{
			const auto found = m_EnumsByType.find(entt::type_index<E>::value());
			return found == m_EnumsByType.end() ? nullptr : found->second;
		}

		// Changes with every registration and unregistration, so caches of lookups can tell when to refresh.
		[[nodiscard]] uint64_t GetGeneration() const noexcept { return m_Generation; }

	private:
		template<typename T>
		friend class ComponentBuilder;
		template<typename E>
		friend class EnumBuilder;
		friend class World;

		ComponentInfo& AddComponent(Scope<ComponentInfo> info, std::optional<uint32_t> typeIndex);
		void AddField(ComponentInfo& component, FieldInfo field);
		EnumInfo& AddEnum(std::string_view name, uint32_t typeIndex);
		void AddEnumEntry(EnumInfo& info, std::string_view name, int64_t value);
		void RebuildSortedList();

		void AttachWorld(World& world);
		void DetachWorld(World& world);

		std::map<std::string, Scope<ComponentInfo>, std::less<>> m_Components; // by name
		std::unordered_map<ComponentId, ComponentInfo*> m_ComponentsById;      // lookups only, never iterated
		std::unordered_map<uint32_t, ComponentInfo*> m_ComponentsByType;       // by entt::type_index
		std::vector<const ComponentInfo*> m_Sorted;
		std::map<std::string, Ref<EnumInfo>, std::less<>> m_Enums;
		std::unordered_map<uint32_t, Ref<EnumInfo>> m_EnumsByType;
		std::vector<World*> m_Worlds;
		uint64_t m_Generation = 0;
	};

	// Registers every engine component and enum (core ECS components now; later modules add their registration
	// function here). TypeRegistry::Get() calls it; tests call it on their own registries.
	void RegisterEngineTypes(TypeRegistry& registry);

	namespace Internal
	{
		template<typename F>
		void* LocateMember(void* component, size_t offset) noexcept
		{
			return std::launder(reinterpret_cast<F*>(static_cast<std::byte*>(component) + offset));
		}

		template<typename S>
		FieldValue ReadStored(const void* storage)
		{
			return FieldValue(std::in_place_type<S>, *static_cast<const S*>(storage));
		}

		template<typename S>
		void WriteStored(void* storage, const FieldValue& value)
		{
			*static_cast<S*>(storage) = *std::get_if<S>(&value);
		}

		template<typename E>
		FieldValue ReadEnum(const void* storage)
		{
			using Underlying = std::underlying_type_t<E>;
			return FieldValue(
				std::in_place_type<int64_t>,
				static_cast<int64_t>(static_cast<Underlying>(*static_cast<const E*>(storage))));
		}

		template<typename E>
		void WriteEnum(void* storage, const FieldValue& value)
		{
			using Underlying = std::underlying_type_t<E>;
			*static_cast<E*>(storage) = static_cast<E>(static_cast<Underlying>(*std::get_if<int64_t>(&value)));
		}

		template<typename T>
		entt::sparse_set& AssureStorage(entt::registry& registry, ComponentId id)
		{
			return registry.storage<T>(id);
		}

		template<typename T>
		void AssignComponent(void* destination, const void* source)
		{
			if constexpr (entt::component_traits<T>::page_size != 0u)
			{
				*static_cast<T*>(destination) = *static_cast<const T*>(source);
			}
		}

		void ApplyAttribute(FieldInfo& field, const Attr::Range& range);
		void ApplyAttribute(FieldInfo& field, const Attr::Tooltip& tooltip);
		void ApplyAttribute(FieldInfo& field, const Attr::Category& category);
		void ApplyAttribute(FieldInfo& field, const Attr::AssetType& assetType);
		void ApplyAttribute(FieldInfo& field, const Attr::Color& color);
		void ApplyAttribute(FieldInfo& field, const Attr::Hidden& hidden);
		void ApplyAttribute(FieldInfo& field, const Attr::ReadOnly& readOnly);
		void ApplyAttribute(FieldInfo& field, const Attr::NotSerialized& notSerialized);
		void ApplyAttribute(FieldInfo& field, const Attr::NotHashed& notHashed);
	}

	// Adds the fields of a C++ component (TypeRegistry::RegisterComponent). Fields are reflected in call order.
	template<typename T>
	class ComponentBuilder
	{
	public:
		ComponentBuilder(TypeRegistry& registry, ComponentInfo& info)
			: m_Registry(registry), m_Info(info)
		{}

		// `member` must be a data member of T of a supported type: bool, int32_t, uint32_t, int64_t, uint64_t, float,
		// double, glm::vec2/vec3/vec4, glm::quat, std::string, UUID (an entity reference, or an asset handle with
		// Attr::AssetType), a registered enum, or a std::vector of any of these except enums. The field's default is
		// its value in a default-constructed T.
		template<typename F, typename... AttributeTypes>
		ComponentBuilder& Field(std::string_view name, F T::*member, const AttributeTypes&... attributes)
		{
			FieldInfo field;
			field.Name = std::string(name);
			if constexpr (std::is_enum_v<F>)
			{
				using Underlying = std::underlying_type_t<F>;
				static_assert(
					std::is_signed_v<Underlying> || sizeof(Underlying) < sizeof(int64_t),
					"enum fields are stored as int64: 64-bit unsigned enums are not supported");
				field.Type = FieldType::Enum;
				field.Attributes.Enum = m_Registry.FindEnum<F>();
				GB_CORE_VERIFY(
					field.Attributes.Enum != nullptr, "field '{}.{}': register enum {} before the component",
					m_Info.GetName(), name, entt::type_name<F>::value());
				field.Access.Read = &Internal::ReadEnum<F>;
				field.Access.Write = &Internal::WriteEnum<F>;
				field.Access.Hash = &Internal::HashEnum<F>;
			}
			else
			{
				static_assert(
					Internal::FieldTypeOf<F>::Type != FieldType::None,
					"unsupported field type (see ComponentBuilder::Field for the supported ones)");
				field.Type = Internal::FieldTypeOf<F>::Type;
				field.ElementType = Internal::FieldTypeOf<F>::ElementType;
				field.Access.Read = &Internal::ReadStored<F>;
				field.Access.Write = &Internal::WriteStored<F>;
				field.Access.Hash = &Internal::HashStored<F>;
			}
			field.Access.Locate = &Internal::LocateMember<F>;
			field.Access.Offset = static_cast<size_t>(
				reinterpret_cast<const std::byte*>(std::addressof(m_Defaults.*member))
				- reinterpret_cast<const std::byte*>(std::addressof(m_Defaults)));
			(Internal::ApplyAttribute(field, attributes), ...);
			field.Default = field.Access.Read(std::addressof(m_Defaults.*member));
			m_Registry.AddField(m_Info, std::move(field));
			return *this;
		}

	private:
		TypeRegistry& m_Registry;
		ComponentInfo& m_Info;
		T m_Defaults{};
	};

	// Adds the values of a C++ enum (TypeRegistry::RegisterEnum).
	template<typename E>
	class EnumBuilder
	{
	public:
		EnumBuilder(TypeRegistry& registry, EnumInfo& info)
			: m_Registry(registry), m_Info(info)
		{}

		// Names are identifiers, unique within the enum, and so are the values.
		EnumBuilder& Value(std::string_view name, E value)
		{
			m_Registry.AddEnumEntry(m_Info, name, static_cast<int64_t>(static_cast<std::underlying_type_t<E>>(value)));
			return *this;
		}

	private:
		TypeRegistry& m_Registry;
		EnumInfo& m_Info;
	};

	template<typename T>
	ComponentBuilder<T> TypeRegistry::RegisterComponent(std::string_view name, ComponentFlags flags)
	{
		static_assert(std::is_class_v<T> && !std::is_polymorphic_v<T>, "components are plain data structs (D-020)");
		static_assert(
			std::is_default_constructible_v<T> && std::is_copy_constructible_v<T> && std::is_copy_assignable_v<T>,
			"components must be default-constructible and copyable");

		auto info = CreateScope<ComponentInfo>(ComponentInfo::ConstructionKey{});
		info->m_Name = std::string(name);
		info->m_Id = entt::type_hash<T>::value();
		info->m_Flags = flags;
		info->m_IsTag = entt::component_traits<T>::page_size == 0u; // EnTT keeps no instances of empty types
		info->m_StorageType = &entt::type_id<T>();
		info->m_AssureStorage = &Internal::AssureStorage<T>;
		info->m_Assign = &Internal::AssignComponent<T>;
		return ComponentBuilder<T>(*this, AddComponent(std::move(info), entt::type_index<T>::value()));
	}

	template<typename E>
	EnumBuilder<E> TypeRegistry::RegisterEnum(std::string_view name)
	{
		static_assert(std::is_enum_v<E>, "RegisterEnum needs an enum type");
		return EnumBuilder<E>(*this, AddEnum(name, entt::type_index<E>::value()));
	}
}
