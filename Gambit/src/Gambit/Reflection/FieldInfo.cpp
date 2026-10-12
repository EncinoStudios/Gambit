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
		return Flags == other.Flags && Range == other.Range && Tooltip == other.Tooltip && Category == other.Category
			&& AssetType == other.AssetType && sameEnum;
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
		return true;
	}

	FieldValue FieldInfo::GetValue(const void* component) const
	{
		GB_CORE_ASSERT(component != nullptr, "reading field '{}' of a null component", Name);
		return Access.Read(GetRawPointer(component));
	}

	bool FieldInfo::SetValue(void* component, const FieldValue& value) const
	{
		GB_CORE_ASSERT(component != nullptr, "writing field '{}' of a null component", Name);
		if (!CanHold(value))
		{
			return false;
		}
		Access.Write(GetRawPointer(component), value);
		return true;
	}

	void FieldInfo::HashValue(Hasher& hasher, const void* component) const
	{
		GB_CORE_ASSERT(component != nullptr, "hashing field '{}' of a null component", Name);
		Access.Hash(hasher, GetRawPointer(component));
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
}
