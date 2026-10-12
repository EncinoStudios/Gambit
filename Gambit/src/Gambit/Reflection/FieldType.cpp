#include "gbpch.h"

#include "Gambit/Reflection/FieldType.h"

namespace Gambit
{
	namespace
	{
		constexpr std::array<std::string_view, static_cast<size_t>(FieldType::Array) + 1> FieldTypeNames = {
			"None", "Bool", "Int32",  "UInt32", "Int64",  "UInt64", "Float",       "Double", "Vec2", "Vec3",
			"Vec4", "Quat", "Color3", "Color4", "String", "Entity", "AssetHandle", "Enum",   "Array"
		};

		// Spelled out for the glm types so the result never depends on how glm's default constructors are configured.
		template<typename T>
		T ZeroOf()
		{
			if constexpr (std::is_same_v<T, glm::quat>)
			{
				return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
			}
			else if constexpr (
				std::is_same_v<T, glm::vec2> || std::is_same_v<T, glm::vec3> || std::is_same_v<T, glm::vec4>)
			{
				return T(0.0f);
			}
			else
			{
				return T{};
			}
		}

		template<typename T>
		bool HoldsScalarOrArray(const FieldValue& value, bool isArray) noexcept
		{
			return isArray ? std::holds_alternative<std::vector<T>>(value) : std::holds_alternative<T>(value);
		}
	}

	std::string_view ToString(FieldType type) noexcept
	{
		const auto index = static_cast<size_t>(type);
		return index < FieldTypeNames.size() ? FieldTypeNames[index] : std::string_view("Unknown");
	}

	std::optional<FieldType> ParseFieldType(std::string_view name) noexcept
	{
		for (size_t index = 0; index < FieldTypeNames.size(); ++index)
		{
			if (FieldTypeNames[index] == name)
			{
				return static_cast<FieldType>(index);
			}
		}
		return std::nullopt;
	}

	bool IsNumericFieldType(FieldType type) noexcept
	{
		switch (type)
		{
			case FieldType::Int32:
			case FieldType::UInt32:
			case FieldType::Int64:
			case FieldType::UInt64:
			case FieldType::Float:
			case FieldType::Double:
			case FieldType::Vec2:
			case FieldType::Vec3:
			case FieldType::Vec4:
			case FieldType::Color3:
			case FieldType::Color4:
				return true;
			default:
				return false;
		}
	}

	bool IsValueOfType(const FieldValue& value, FieldType type, FieldType elementType) noexcept
	{
		const bool isArray = type == FieldType::Array;
		if (isArray ? !IsArrayElementType(elementType) : (type == FieldType::None || elementType != FieldType::None))
		{
			return false;
		}
		return Internal::VisitStorageType(isArray ? elementType : type, [&]<typename T>(std::type_identity<T>) {
			return HoldsScalarOrArray<T>(value, isArray);
		});
	}

	FieldValue MakeZeroValue(FieldType type, FieldType elementType)
	{
		if (type == FieldType::Array)
		{
			GB_CORE_ASSERT(IsArrayElementType(elementType), "invalid array element type {}", ToString(elementType));
			return Internal::VisitStorageType(elementType, []<typename T>(std::type_identity<T>) {
				return FieldValue(std::in_place_type<std::vector<T>>);
			});
		}
		GB_CORE_ASSERT(elementType == FieldType::None, "only arrays have an element type");
		return Internal::VisitStorageType(
			type, []<typename T>(std::type_identity<T>) { return FieldValue(std::in_place_type<T>, ZeroOf<T>()); });
	}
}
