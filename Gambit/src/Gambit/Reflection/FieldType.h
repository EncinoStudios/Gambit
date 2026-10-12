#pragma once

#include "Gambit/Core/Assert.h"
#include "Gambit/Core/UUID.h"
#include "Gambit/Math/Math.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

namespace Gambit
{
	// The type of a reflected field (D-022). Serialization, the inspector, Lua, the CLI and state hashing dispatch on
	// it.
	enum class FieldType : uint8_t
	{
		None = 0, // not a field type: the element type of fields that are not arrays
		Bool,
		Int32,
		UInt32,
		Int64,
		UInt64,
		Float,
		Double,
		Vec2,
		Vec3,
		Vec4,
		Quat,
		Color3, // stored like Vec3 (linear RGB)
		Color4, // stored like Vec4 (linear RGBA)
		String,
		Entity,      // UUID of another entity of the same world (the null UUID refers to no entity)
		AssetHandle, // UUID of an asset of the type named by FieldAttributes::AssetType (null = no asset)
		Enum,        // one of the values of an EnumInfo; an int64 in a FieldValue
		Array        // std::vector of an element type: any type above except None and Enum
	};

	// "Bool", "Vec3", "AssetHandle", ...: the names used in files, schemas and error messages.
	[[nodiscard]] std::string_view ToString(FieldType type) noexcept;
	[[nodiscard]] std::optional<FieldType> ParseFieldType(std::string_view name) noexcept;

	// Whether arrays of `type` are supported (every scalar, vector, color, String, Entity and AssetHandle type).
	[[nodiscard]] constexpr bool IsArrayElementType(FieldType type) noexcept
	{
		return type != FieldType::None && type != FieldType::Enum && type != FieldType::Array;
	}

	// Whether FieldAttributes::Range applies: integers, floating point, vectors and colors (per component), and arrays
	// of them.
	[[nodiscard]] bool IsNumericFieldType(FieldType type) noexcept;

	// A field value of any FieldType. The alternative is the storage type of the field: Color3 and Vec3 share
	// glm::vec3, Color4 and Vec4 share glm::vec4, Entity and AssetHandle share UUID and Enum is an int64. The field's
	// FieldInfo says which FieldType a value has.
	using FieldValue = std::variant<
		bool, int32_t, uint32_t, int64_t, uint64_t, float, double, glm::vec2, glm::vec3, glm::vec4, glm::quat,
		std::string, UUID, std::vector<bool>, std::vector<int32_t>, std::vector<uint32_t>, std::vector<int64_t>,
		std::vector<uint64_t>, std::vector<float>, std::vector<double>, std::vector<glm::vec2>, std::vector<glm::vec3>,
		std::vector<glm::vec4>, std::vector<glm::quat>, std::vector<std::string>, std::vector<UUID>>;

	// Whether `value` holds the alternative a field of `type` (with `elementType` for arrays) is stored as.
	[[nodiscard]] bool IsValueOfType(const FieldValue& value, FieldType type, FieldType elementType) noexcept;

	// The neutral value of a type: false, zero, zero vectors and colors, the identity quaternion, "", the null UUID,
	// enum value 0 and empty arrays. `type` must not be None, and `elementType` must be a valid array element type
	// exactly when `type` is Array.
	[[nodiscard]] FieldValue MakeZeroValue(FieldType type, FieldType elementType = FieldType::None);

	namespace Internal
	{
		// The FieldType a C++ field type maps to. Vec3/Vec4 become Color3/Color4 with Attr::Color, UUID becomes
		// AssetHandle with Attr::AssetType; enums are handled by the registry (they must be registered).
		template<typename T>
		struct FieldTypeOf
		{
			static constexpr FieldType Type = FieldType::None;
			static constexpr FieldType ElementType = FieldType::None;
		};

#define GB_INTERNAL_FIELD_TYPE_OF(CppType, Value)                                                                      \
	template<>                                                                                                         \
	struct FieldTypeOf<CppType>                                                                                        \
	{                                                                                                                  \
		static constexpr FieldType Type = FieldType::Value;                                                            \
		static constexpr FieldType ElementType = FieldType::None;                                                      \
	}

		GB_INTERNAL_FIELD_TYPE_OF(bool, Bool);
		GB_INTERNAL_FIELD_TYPE_OF(int32_t, Int32);
		GB_INTERNAL_FIELD_TYPE_OF(uint32_t, UInt32);
		GB_INTERNAL_FIELD_TYPE_OF(int64_t, Int64);
		GB_INTERNAL_FIELD_TYPE_OF(uint64_t, UInt64);
		GB_INTERNAL_FIELD_TYPE_OF(float, Float);
		GB_INTERNAL_FIELD_TYPE_OF(double, Double);
		GB_INTERNAL_FIELD_TYPE_OF(glm::vec2, Vec2);
		GB_INTERNAL_FIELD_TYPE_OF(glm::vec3, Vec3);
		GB_INTERNAL_FIELD_TYPE_OF(glm::vec4, Vec4);
		GB_INTERNAL_FIELD_TYPE_OF(glm::quat, Quat);
		GB_INTERNAL_FIELD_TYPE_OF(std::string, String);
		GB_INTERNAL_FIELD_TYPE_OF(UUID, Entity);

#undef GB_INTERNAL_FIELD_TYPE_OF

		template<typename T>
		struct FieldTypeOf<std::vector<T>>
		{
			static constexpr FieldType Type =
				IsArrayElementType(FieldTypeOf<T>::Type) ? FieldType::Array : FieldType::None;
			static constexpr FieldType ElementType = FieldTypeOf<T>::Type;
		};

		// Calls function(std::type_identity<S>{}) with S the FieldValue alternative of `type` (int64_t for Enum).
		// `type` must not be None or Array (pass the element type of arrays).
		template<typename Function>
		decltype(auto) VisitStorageType(FieldType type, Function&& function)
		{
			switch (type)
			{
				case FieldType::Bool:
					return function(std::type_identity<bool>{});
				case FieldType::Int32:
					return function(std::type_identity<int32_t>{});
				case FieldType::UInt32:
					return function(std::type_identity<uint32_t>{});
				case FieldType::Int64:
				case FieldType::Enum:
					return function(std::type_identity<int64_t>{});
				case FieldType::UInt64:
					return function(std::type_identity<uint64_t>{});
				case FieldType::Float:
					return function(std::type_identity<float>{});
				case FieldType::Double:
					return function(std::type_identity<double>{});
				case FieldType::Vec2:
					return function(std::type_identity<glm::vec2>{});
				case FieldType::Vec3:
				case FieldType::Color3:
					return function(std::type_identity<glm::vec3>{});
				case FieldType::Vec4:
				case FieldType::Color4:
					return function(std::type_identity<glm::vec4>{});
				case FieldType::Quat:
					return function(std::type_identity<glm::quat>{});
				case FieldType::String:
					return function(std::type_identity<std::string>{});
				case FieldType::Entity:
				case FieldType::AssetHandle:
					return function(std::type_identity<UUID>{});
				case FieldType::None:
				case FieldType::Array:
					break;
			}
			GB_CORE_VERIFY(false, "field type {} has no storage type", static_cast<int>(type));
			return function(std::type_identity<bool>{});
		}
	}
}
