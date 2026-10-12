#include "gbpch.h"

#include "Gambit/Reflection/FieldHashing.h"

#include "Gambit/Reflection/FieldInfo.h"

namespace Gambit
{
	void HashStructValue(Hasher& hasher, const StructInfo& info, const StructValue& value)
	{
		const std::span<const FieldInfo> fields = info.GetFields();
		GB_CORE_ASSERT(
			value.Values.size() == fields.size(), "struct '{}': the value has {} fields, the struct {}", info.GetName(),
			value.Values.size(), fields.size());
		for (size_t index = 0; index < fields.size(); ++index)
		{
			if (!fields[index].HasFlag(FieldFlags::NotHashed))
			{
				HashFieldValue(hasher, fields[index], value.Values[index]);
			}
		}
	}

	void HashStructValues(Hasher& hasher, const StructInfo& info, std::span<const StructValue> values)
	{
		hasher.UpdateValue(static_cast<uint64_t>(values.size()));
		for (const StructValue& value : values)
		{
			HashStructValue(hasher, info, value);
		}
	}

	void HashFieldValue(Hasher& hasher, const FieldInfo& field, const FieldValue& value)
	{
		const bool isStruct = field.Type == FieldType::Struct;
		const bool isStructArray = field.Type == FieldType::Array && field.ElementType == FieldType::Struct;
		if (isStruct || isStructArray)
		{
			GB_CORE_ASSERT(field.Attributes.Struct != nullptr, "field '{}' has no struct", field.Name);
			if (isStruct)
			{
				HashStructValue(hasher, *field.Attributes.Struct, std::get<StructValue>(value));
			}
			else
			{
				HashStructValues(hasher, *field.Attributes.Struct, std::get<std::vector<StructValue>>(value));
			}
			return;
		}
		std::visit(
			[&]<typename S>(const S& stored) {
				if constexpr (std::is_same_v<S, StructValue> || std::is_same_v<S, std::vector<StructValue>>)
				{
					GB_CORE_ASSERT(false, "field '{}' holds a struct value but is not a struct field", field.Name);
				}
				else
				{
					Internal::HashStored<S>(hasher, &stored);
				}
			},
			value);
	}
}
