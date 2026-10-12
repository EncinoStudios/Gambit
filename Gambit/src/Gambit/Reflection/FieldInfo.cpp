#include "gbpch.h"

#include "Gambit/Reflection/FieldInfo.h"

namespace Gambit
{
	std::optional<int64_t> EnumInfo::FindValue(std::string_view name) const noexcept
	{
		for (const EnumEntry& entry : Entries)
		{
			if (entry.Name == name)
			{
				return entry.Value;
			}
		}
		return std::nullopt;
	}

	std::string_view EnumInfo::FindName(int64_t value) const noexcept
	{
		for (const EnumEntry& entry : Entries)
		{
			if (entry.Value == value)
			{
				return entry.Name;
			}
		}
		return {};
	}

	bool FieldAttributes::IsEquivalent(const FieldAttributes& other) const noexcept
	{
		const bool sameEnum = Enum == other.Enum || (Enum != nullptr && other.Enum != nullptr && *Enum == *other.Enum);
		const bool sameStruct = Struct == other.Struct
			|| (Struct != nullptr && other.Struct != nullptr && Struct->GetName() == other.Struct->GetName());
		return Flags == other.Flags && Range == other.Range && Tooltip == other.Tooltip && Category == other.Category
			&& AssetType == other.AssetType && sameEnum && sameStruct;
	}

	bool FieldInfo::CanHold(const FieldValue& value) const noexcept
	{
		if (!IsValueOfType(value, Type, ElementType))
		{
			return false;
		}
		if (Type == FieldType::Enum)
		{
			return Attributes.Enum != nullptr && Attributes.Enum->Contains(*std::get_if<int64_t>(&value));
		}
		if (Type == FieldType::Struct)
		{
			return Attributes.Struct != nullptr && Attributes.Struct->CanHold(*std::get_if<StructValue>(&value));
		}
		if (Type == FieldType::Array && ElementType == FieldType::Struct)
		{
			if (Attributes.Struct == nullptr)
			{
				return false;
			}
			const auto& elements = *std::get_if<std::vector<StructValue>>(&value);
			return std::ranges::all_of(
				elements, [&](const StructValue& element) { return Attributes.Struct->CanHold(element); });
		}
		return true;
	}

	FieldValue FieldInfo::GetValue(const void* component) const
	{
		GB_CORE_ASSERT(component != nullptr, "reading field '{}' of a null component", Name);
		return Access.Read(*this, GetRawPointer(component));
	}

	bool FieldInfo::SetValue(void* component, const FieldValue& value) const
	{
		GB_CORE_ASSERT(component != nullptr, "writing field '{}' of a null component", Name);
		if (!CanHold(value))
		{
			return false;
		}
		Access.Write(*this, GetRawPointer(component), value);
		return true;
	}

	void FieldInfo::HashValue(Hasher& hasher, const void* component) const
	{
		GB_CORE_ASSERT(component != nullptr, "hashing field '{}' of a null component", Name);
		Access.Hash(*this, hasher, GetRawPointer(component));
	}

	void* FieldInfo::GetRawPointer(void* component) const noexcept
	{
		return Access.Locate(component, Access.Offset);
	}

	const void* FieldInfo::GetRawPointer(const void* component) const noexcept
	{
		// Locate only computes an address; the constness is restored on return.
		return Access.Locate(const_cast<void*>(component), Access.Offset);
	}

	const FieldInfo* StructInfo::FindField(std::string_view name) const noexcept
	{
		const std::optional<size_t> index = FindFieldIndex(name);
		return index.has_value() ? &m_Fields[*index] : nullptr;
	}

	std::optional<size_t> StructInfo::FindFieldIndex(std::string_view name) const noexcept
	{
		for (size_t index = 0; index < m_Fields.size(); ++index)
		{
			if (m_Fields[index].Name == name)
			{
				return index;
			}
		}
		return std::nullopt;
	}

	bool StructInfo::CanHold(const StructValue& value) const noexcept
	{
		if (value.Values.size() != m_Fields.size())
		{
			return false;
		}
		for (size_t index = 0; index < m_Fields.size(); ++index)
		{
			if (!m_Fields[index].CanHold(value.Values[index]))
			{
				return false;
			}
		}
		return true;
	}

	StructValue StructInfo::Read(const void* object) const
	{
		GB_CORE_ASSERT(object != nullptr, "reading struct '{}' from a null object", m_Name);
		StructValue value;
		value.Values.reserve(m_Fields.size());
		for (const FieldInfo& field : m_Fields)
		{
			value.Values.push_back(field.GetValue(object));
		}
		return value;
	}

	bool StructInfo::Write(void* object, const StructValue& value) const
	{
		GB_CORE_ASSERT(object != nullptr, "writing struct '{}' to a null object", m_Name);
		if (!CanHold(value))
		{
			return false;
		}
		for (size_t index = 0; index < m_Fields.size(); ++index)
		{
			// CanHold has checked the value, so the field cannot refuse it.
			m_Fields[index].Access.Write(m_Fields[index], m_Fields[index].GetRawPointer(object), value.Values[index]);
		}
		return true;
	}

	void StructInfo::Hash(Hasher& hasher, const void* object) const
	{
		GB_CORE_ASSERT(object != nullptr, "hashing struct '{}' of a null object", m_Name);
		for (const FieldInfo& field : m_Fields)
		{
			if (!field.HasFlag(FieldFlags::NotHashed))
			{
				field.HashValue(hasher, object);
			}
		}
	}
}
