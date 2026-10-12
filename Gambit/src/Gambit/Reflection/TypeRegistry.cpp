#include "gbpch.h"

#include "Gambit/Reflection/TypeRegistry.h"

#include "Gambit/ECS/World.h"

#include <entt/core/hashed_string.hpp>

namespace Gambit
{
	static_assert(std::is_same_v<ComponentId, entt::id_type>, "ComponentId must be EnTT's storage id type");

	namespace
	{
		bool IsAsciiLetter(char character) noexcept
		{
			return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z');
		}

		bool IsAsciiDigit(char character) noexcept
		{
			return character >= '0' && character <= '9';
		}

		// Script components store their values in DynamicComponent::Values; the field's storage is the active
		// alternative of its variant.
		void* LocateDynamicValue(void* component, size_t index) noexcept
		{
			FieldValue& value = static_cast<DynamicComponent*>(component)->Values[index];
			return std::visit([](auto& stored) -> void* { return std::addressof(stored); }, value);
		}

		Internal::FieldAccess MakeDynamicAccess(FieldType type, FieldType elementType, size_t index)
		{
			const auto make = [index]<typename S>(std::type_identity<S>) {
				return Internal::FieldAccess{ index, &LocateDynamicValue, &Internal::ReadStored<S>,
					                          &Internal::WriteStored<S>, &Internal::HashStored<S> };
			};
			if (type == FieldType::Array)
			{
				return Internal::VisitStorageType(elementType, [&]<typename E>(std::type_identity<E>) {
					return make(std::type_identity<std::vector<E>>{});
				});
			}
			return Internal::VisitStorageType(type, make);
		}

		Result<void> ValidateEnum(const EnumInfo& info)
		{
			if (info.Entries.empty())
			{
				return Error("the enum has no values");
			}
			for (size_t i = 0; i < info.Entries.size(); ++i)
			{
				const EnumEntry& entry = info.Entries[i];
				if (!TypeRegistry::IsValidName(entry.Name))
				{
					return Error::Format("invalid enum value name '{}'", entry.Name);
				}
				for (size_t j = 0; j < i; ++j)
				{
					if (info.Entries[j].Name == entry.Name || info.Entries[j].Value == entry.Value)
					{
						return Error::Format("enum values '{}' and '{}' clash", info.Entries[j].Name, entry.Name);
					}
				}
			}
			return {};
		}

		// The rules every field obeys, whether it comes from C++ or from a script schema.
		Result<void> ValidateField(const FieldInfo& field)
		{
			if (!TypeRegistry::IsValidName(field.Name))
			{
				return Error::Format("invalid field name '{}'", field.Name);
			}
			if (field.Type == FieldType::None)
			{
				return Error::Format("field '{}' has no type", field.Name);
			}
			if (field.Type == FieldType::Array ? !IsArrayElementType(field.ElementType)
			                                   : field.ElementType != FieldType::None)
			{
				return Error::Format(
					"field '{}': {} is not a valid element type for {}", field.Name, ToString(field.ElementType),
					ToString(field.Type));
			}
			if (!IsValueOfType(field.Default, field.Type, field.ElementType))
			{
				return Error::Format(
					"field '{}': the default value is not of type {}", field.Name, ToString(field.Type));
			}

			const FieldAttributes& attributes = field.Attributes;
			const FieldType valueType = field.Type == FieldType::Array ? field.ElementType : field.Type;
			if (field.Type == FieldType::Enum)
			{
				if (attributes.Enum == nullptr)
				{
					return Error::Format("enum field '{}' has no enum values", field.Name);
				}
				if (Result<void> valid = ValidateEnum(*attributes.Enum); !valid)
				{
					return Error::Format("enum field '{}': {}", field.Name, valid.GetError().Message);
				}
				if (!field.CanHold(field.Default))
				{
					return Error::Format("enum field '{}': the default is not one of its values", field.Name);
				}
			}
			else if (attributes.Enum != nullptr)
			{
				return Error::Format("field '{}' is not an Enum field but has enum values", field.Name);
			}
			if (valueType == FieldType::AssetHandle)
			{
				if (!TypeRegistry::IsValidName(attributes.AssetType))
				{
					return Error::Format("asset field '{}' needs a valid asset type", field.Name);
				}
			}
			else if (!attributes.AssetType.empty())
			{
				return Error::Format("field '{}' is not an AssetHandle field but has an asset type", field.Name);
			}
			if (attributes.Range.has_value())
			{
				const FieldRange& range = *attributes.Range;
				if (!IsNumericFieldType(valueType))
				{
					return Error::Format("field '{}' of type {} cannot have a range", field.Name, ToString(field.Type));
				}
				if (!std::isfinite(range.Min) || !std::isfinite(range.Max) || range.Min > range.Max)
				{
					return Error::Format("field '{}' has an invalid range [{}, {}]", field.Name, range.Min, range.Max);
				}
			}
			return {};
		}

		bool HaveSameSchema(const ComponentInfo& a, const ComponentInfo& b)
		{
			const std::span<const FieldInfo> fieldsA = a.GetFields();
			const std::span<const FieldInfo> fieldsB = b.GetFields();
			return a.GetFlags() == b.GetFlags()
				&& std::ranges::equal(fieldsA, fieldsB, [](const FieldInfo& x, const FieldInfo& y) {
					   return x.Name == y.Name && x.Type == y.Type && x.ElementType == y.ElementType
						   && x.Default == y.Default && x.Attributes.IsEquivalent(y.Attributes);
				   });
		}
	}

	namespace Internal
	{
		void ApplyAttribute(FieldInfo& field, const Attr::Range& range)
		{
			field.Attributes.Range = FieldRange{ range.Min, range.Max };
		}

		void ApplyAttribute(FieldInfo& field, const Attr::Tooltip& tooltip)
		{
			field.Attributes.Tooltip = std::string(tooltip.Text);
		}

		void ApplyAttribute(FieldInfo& field, const Attr::Category& category)
		{
			field.Attributes.Category = std::string(category.Name);
		}

		void ApplyAttribute(FieldInfo& field, const Attr::AssetType& assetType)
		{
			FieldType& valueType = field.Type == FieldType::Array ? field.ElementType : field.Type;
			GB_CORE_VERIFY(
				valueType == FieldType::Entity, "field '{}': Attr::AssetType applies to UUID fields only", field.Name);
			valueType = FieldType::AssetHandle;
			field.Attributes.AssetType = std::string(assetType.Type);
		}

		void ApplyAttribute(FieldInfo& field, const Attr::Color&)
		{
			FieldType& valueType = field.Type == FieldType::Array ? field.ElementType : field.Type;
			GB_CORE_VERIFY(
				valueType == FieldType::Vec3 || valueType == FieldType::Vec4,
				"field '{}': Attr::Color applies to glm::vec3 and glm::vec4 fields only", field.Name);
			valueType = valueType == FieldType::Vec3 ? FieldType::Color3 : FieldType::Color4;
		}

		void ApplyAttribute(FieldInfo& field, const Attr::Hidden&)
		{
			field.Attributes.Flags = field.Attributes.Flags | FieldFlags::Hidden;
		}

		void ApplyAttribute(FieldInfo& field, const Attr::ReadOnly&)
		{
			field.Attributes.Flags = field.Attributes.Flags | FieldFlags::ReadOnly;
		}

		void ApplyAttribute(FieldInfo& field, const Attr::NotSerialized&)
		{
			field.Attributes.Flags = field.Attributes.Flags | FieldFlags::NotSerialized;
		}

		void ApplyAttribute(FieldInfo& field, const Attr::NotHashed&)
		{
			field.Attributes.Flags = field.Attributes.Flags | FieldFlags::NotHashed;
		}
	}

	TypeRegistry::~TypeRegistry()
	{
		GB_CORE_ASSERT(m_Worlds.empty(), "a TypeRegistry must outlive the {} worlds that use it", m_Worlds.size());
	}

	TypeRegistry& TypeRegistry::Get()
	{
		static TypeRegistry& s_Registry = []() -> TypeRegistry& {
			static TypeRegistry registry;
			RegisterEngineTypes(registry);
			return registry;
		}();
		return s_Registry;
	}

	bool TypeRegistry::IsValidName(std::string_view name) noexcept
	{
		if (name.empty() || name.size() > MaxNameLength || !(IsAsciiLetter(name.front()) || name.front() == '_'))
		{
			return false;
		}
		return std::ranges::all_of(name, [](char character) {
			return IsAsciiLetter(character) || IsAsciiDigit(character) || character == '_';
		});
	}

	Result<const ComponentInfo*> TypeRegistry::RegisterDynamicComponent(const DynamicComponentSchema& schema)
	{
		if (!IsValidName(schema.Name))
		{
			return Error::Format("invalid component name '{}'", schema.Name);
		}

		auto info = CreateScope<ComponentInfo>(ComponentInfo::ConstructionKey{});
		info->m_Name = schema.Name;
		info->m_Id = entt::hashed_string::value(schema.Name.data(), schema.Name.size());
		info->m_Flags = schema.Flags;
		info->m_IsDynamic = true;
		info->m_StorageType = &entt::type_id<DynamicComponent>();
		info->m_AssureStorage = &Internal::AssureStorage<DynamicComponent>;
		info->m_Assign = &Internal::AssignComponent<DynamicComponent>;
		for (size_t index = 0; index < schema.Fields.size(); ++index)
		{
			const DynamicFieldSchema& source = schema.Fields[index];
			FieldInfo field;
			field.Name = source.Name;
			field.Type = source.Type;
			field.ElementType = source.ElementType;
			field.Attributes = source.Attributes;
			field.Default = source.Default;
			if (Result<void> valid = ValidateField(field); !valid)
			{
				return Error::Format("component '{}': {}", schema.Name, valid.GetError().Message);
			}
			if (info->FindField(field.Name) != nullptr)
			{
				return Error::Format("component '{}': field '{}' is declared twice", schema.Name, field.Name);
			}
			field.Access = MakeDynamicAccess(field.Type, field.ElementType, index);
			info->m_Prototype.Values.push_back(field.Default);
			info->m_Fields.push_back(std::move(field));
		}

		if (const ComponentInfo* existing = FindComponent(schema.Name); existing != nullptr)
		{
			if (existing->IsDynamic() && HaveSameSchema(*existing, *info))
			{
				return existing;
			}
			return Error::Format(
				"component '{}' is already registered{}", schema.Name,
				existing->IsDynamic() ? " with a different schema" : " by the engine");
		}
		if (m_ComponentsById.contains(info->m_Id) || info->m_Id == entt::type_hash<entt::entity>::value())
		{
			return Error::Format(
				"component '{}' collides with the id of '{}': rename it", schema.Name,
				m_ComponentsById.contains(info->m_Id) ? m_ComponentsById.at(info->m_Id)->GetName() : "entity");
		}
		return &AddComponent(std::move(info), std::nullopt);
	}

	Result<void> TypeRegistry::UnregisterDynamicComponent(std::string_view name)
	{
		const auto found = m_Components.find(name);
		if (found == m_Components.end())
		{
			return Error::Format("component '{}' is not registered", name);
		}
		const ComponentInfo& info = *found->second;
		if (!info.IsDynamic())
		{
			return Error::Format("component '{}' is a C++ component and cannot be unregistered", name);
		}

		for (World* world : m_Worlds)
		{
			GB_CORE_ASSERT(!world->GetScheduler().IsRunning(), "unregistering '{}' while a world ticks", name);
			// Drops the storage and every instance; pending commands for the component find no type and are skipped.
			world->GetRegistry().reset(info.GetId());
		}
		m_ComponentsById.erase(info.GetId());
		m_Components.erase(found);
		RebuildSortedList();
		++m_Generation;
		return {};
	}

	const ComponentInfo* TypeRegistry::FindComponent(std::string_view name) const
	{
		const auto found = m_Components.find(name);
		return found == m_Components.end() ? nullptr : found->second.get();
	}

	const ComponentInfo* TypeRegistry::FindComponent(ComponentId id) const
	{
		const auto found = m_ComponentsById.find(id);
		return found == m_ComponentsById.end() ? nullptr : found->second;
	}

	Ref<const EnumInfo> TypeRegistry::FindEnum(std::string_view name) const
	{
		const auto found = m_Enums.find(name);
		return found == m_Enums.end() ? nullptr : found->second;
	}

	ComponentInfo& TypeRegistry::AddComponent(Scope<ComponentInfo> info, std::optional<uint32_t> typeIndex)
	{
		const std::string& name = info->GetName();
		GB_CORE_VERIFY(IsValidName(name), "invalid component name '{}'", name);
		GB_CORE_VERIFY(!m_Components.contains(name), "component '{}' is registered twice", name);
		GB_CORE_VERIFY(
			!m_ComponentsById.contains(info->GetId()), "component '{}' collides with the id of another component",
			name);
		GB_CORE_VERIFY(
			!typeIndex.has_value() || !m_ComponentsByType.contains(*typeIndex),
			"the type of component '{}' is already registered", name);

		ComponentInfo& added = *info;
		m_ComponentsById.emplace(added.GetId(), &added);
		if (typeIndex.has_value())
		{
			m_ComponentsByType.emplace(*typeIndex, &added);
		}
		m_Components.emplace(name, std::move(info));
		RebuildSortedList();
		++m_Generation;
		return added;
	}

	void TypeRegistry::AddField(ComponentInfo& component, FieldInfo field)
	{
		const Result<void> valid = ValidateField(field);
		GB_CORE_VERIFY(valid.HasValue(), "component '{}': {}", component.GetName(), valid.GetError().Message);
		GB_CORE_VERIFY(
			component.FindField(field.Name) == nullptr, "component '{}': field '{}' is registered twice",
			component.GetName(), field.Name);
		component.m_Fields.push_back(std::move(field));
		++m_Generation;
	}

	EnumInfo& TypeRegistry::AddEnum(std::string_view name, uint32_t typeIndex)
	{
		GB_CORE_VERIFY(IsValidName(name), "invalid enum name '{}'", name);
		GB_CORE_VERIFY(!m_Enums.contains(name), "enum '{}' is registered twice", name);
		GB_CORE_VERIFY(!m_EnumsByType.contains(typeIndex), "the type of enum '{}' is already registered", name);
		auto info = CreateRef<EnumInfo>();
		info->Name = std::string(name);
		m_Enums.emplace(info->Name, info);
		m_EnumsByType.emplace(typeIndex, info);
		++m_Generation;
		return *info;
	}

	void TypeRegistry::AddEnumEntry(EnumInfo& info, std::string_view name, int64_t value)
	{
		GB_CORE_VERIFY(IsValidName(name), "enum '{}': invalid value name '{}'", info.Name, name);
		GB_CORE_VERIFY(!info.FindValue(name).has_value(), "enum '{}': value '{}' is registered twice", info.Name, name);
		GB_CORE_VERIFY(
			!info.Contains(value), "enum '{}': '{}' has the same value as '{}'", info.Name, name, info.FindName(value));
		info.Entries.push_back({ std::string(name), value });
		++m_Generation;
	}

	void TypeRegistry::RebuildSortedList()
	{
		m_Sorted.clear();
		m_Sorted.reserve(m_Components.size());
		for (const auto& [name, info] : m_Components)
		{
			m_Sorted.push_back(info.get());
		}
	}

	void TypeRegistry::AttachWorld(World& world)
	{
		m_Worlds.push_back(&world);
	}

	void TypeRegistry::DetachWorld(World& world)
	{
		std::erase(m_Worlds, &world);
	}
}
