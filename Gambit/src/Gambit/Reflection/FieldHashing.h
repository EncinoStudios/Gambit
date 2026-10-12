#pragma once

#include "Gambit/Core/Hash.h"
#include "Gambit/Core/UUID.h"
#include "Gambit/Math/Math.h"
#include "Gambit/Reflection/FieldType.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

// The byte encoding of field values in the state hash (D-013), shared by C++ and script component fields so that
// both hash alike. Changing it changes every state hash: bump the version seed of World::ComputeStateHash.

namespace Gambit
{
	struct FieldInfo;

	// Feeds a value of `field` to a hash: any type, with structs field by field (the fields without NotHashed) and
	// arrays as their size followed by the elements. The same bytes as hashing the field in place
	// (FieldInfo::HashValue).
	void HashFieldValue(Hasher& hasher, const FieldInfo& field, const FieldValue& value);
	void HashStructValue(Hasher& hasher, const StructInfo& info, const StructValue& value);
	// The size of the array, then each struct.
	void HashStructValues(Hasher& hasher, const StructInfo& info, std::span<const StructValue> values);
}

namespace Gambit::Internal
{
	inline void HashText(Hasher& hasher, std::string_view text)
	{
		hasher.UpdateValue(static_cast<uint64_t>(text.size()));
		hasher.Update(text);
	}

	// One value of a FieldValue alternative (or array element): vectors component by component in x, y, z, w order,
	// floating point by bit pattern (-0.0 and 0.0 differ, as later arithmetic may tell them apart), strings with their
	// length, UUIDs by value.
	template<typename T>
	void HashScalar(Hasher& hasher, const T& value)
	{
		if constexpr (std::is_same_v<T, bool>)
		{
			hasher.UpdateValue(static_cast<uint8_t>(value ? 1 : 0));
		}
		else if constexpr (std::is_arithmetic_v<T>)
		{
			hasher.UpdateValue(value);
		}
		else if constexpr (std::is_same_v<T, glm::vec2>)
		{
			hasher.UpdateValue(value.x);
			hasher.UpdateValue(value.y);
		}
		else if constexpr (std::is_same_v<T, glm::vec3>)
		{
			hasher.UpdateValue(value.x);
			hasher.UpdateValue(value.y);
			hasher.UpdateValue(value.z);
		}
		else if constexpr (std::is_same_v<T, glm::vec4> || std::is_same_v<T, glm::quat>)
		{
			hasher.UpdateValue(value.x);
			hasher.UpdateValue(value.y);
			hasher.UpdateValue(value.z);
			hasher.UpdateValue(value.w);
		}
		else if constexpr (std::is_same_v<T, std::string>)
		{
			HashText(hasher, value);
		}
		else
		{
			static_assert(std::is_same_v<T, UUID>, "not a field storage type");
			hasher.UpdateValue(value.GetValue());
		}
	}

	// A field stored as S (a FieldValue alternative); arrays are hashed as their size followed by the elements.
	template<typename S>
	void HashStored(Hasher& hasher, const void* storage)
	{
		const S& value = *static_cast<const S*>(storage);
		if constexpr (IsStdVector<S>)
		{
			hasher.UpdateValue(static_cast<uint64_t>(value.size()));
			for (const auto& element : value)
			{
				if constexpr (std::is_same_v<S, std::vector<bool>>)
				{
					HashScalar(hasher, static_cast<bool>(element)); // a proxy, not a bool
				}
				else
				{
					HashScalar(hasher, element);
				}
			}
		}
		else
		{
			HashScalar(hasher, value);
		}
	}

	// A C++ enum field, hashed as the int64 its FieldValue holds (like script enums).
	template<typename E>
	void HashEnum(Hasher& hasher, const void* storage)
	{
		hasher.UpdateValue(
			static_cast<int64_t>(static_cast<std::underlying_type_t<E>>(*static_cast<const E*>(storage))));
	}
}
