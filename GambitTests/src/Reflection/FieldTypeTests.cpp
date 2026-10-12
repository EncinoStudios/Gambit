#include <doctest/doctest.h>

#include "Gambit/Reflection/FieldType.h"

#include <string>
#include <vector>

using namespace Gambit;

TEST_CASE("Reflection: field type names round-trip")
{
	for (int index = 0; index <= static_cast<int>(FieldType::Array); ++index)
	{
		const auto type = static_cast<FieldType>(index);
		const std::optional<FieldType> parsed = ParseFieldType(ToString(type));
		REQUIRE_MESSAGE(parsed.has_value(), ToString(type));
		CHECK(*parsed == type);
	}
	CHECK(ToString(FieldType::AssetHandle) == "AssetHandle");
	CHECK(ToString(FieldType::Color4) == "Color4");
	CHECK_FALSE(ParseFieldType("vec3").has_value()); // case-sensitive
	CHECK_FALSE(ParseFieldType("").has_value());
	CHECK(ToString(static_cast<FieldType>(200)) == "Unknown");
}

TEST_CASE("Reflection: array element types and numeric types")
{
	CHECK(IsArrayElementType(FieldType::Bool));
	CHECK(IsArrayElementType(FieldType::Color3));
	CHECK(IsArrayElementType(FieldType::String));
	CHECK(IsArrayElementType(FieldType::Entity));
	CHECK(IsArrayElementType(FieldType::AssetHandle));
	CHECK(IsArrayElementType(FieldType::UUID));
	CHECK(IsArrayElementType(FieldType::Struct));
	CHECK_FALSE(IsArrayElementType(FieldType::None));
	CHECK_FALSE(IsArrayElementType(FieldType::Enum));
	CHECK_FALSE(IsArrayElementType(FieldType::Array));

	CHECK(IsNumericFieldType(FieldType::Int32));
	CHECK(IsNumericFieldType(FieldType::Double));
	CHECK(IsNumericFieldType(FieldType::Vec3));
	CHECK(IsNumericFieldType(FieldType::Color4));
	CHECK_FALSE(IsNumericFieldType(FieldType::Bool));
	CHECK_FALSE(IsNumericFieldType(FieldType::Quat));
	CHECK_FALSE(IsNumericFieldType(FieldType::String));
	CHECK_FALSE(IsNumericFieldType(FieldType::Enum));
}

TEST_CASE("Reflection: values match the storage type of their field type")
{
	CHECK(IsValueOfType(true, FieldType::Bool, FieldType::None));
	CHECK(IsValueOfType(int32_t{ 1 }, FieldType::Int32, FieldType::None));
	CHECK_FALSE(IsValueOfType(int32_t{ 1 }, FieldType::Int64, FieldType::None)); // no implicit widening
	CHECK(IsValueOfType(int64_t{ 1 }, FieldType::Enum, FieldType::None));
	CHECK(IsValueOfType(glm::vec3(1.0f), FieldType::Vec3, FieldType::None));
	CHECK(IsValueOfType(glm::vec3(1.0f), FieldType::Color3, FieldType::None));
	CHECK(IsValueOfType(glm::vec4(1.0f), FieldType::Color4, FieldType::None));
	CHECK(IsValueOfType(UUID(5), FieldType::Entity, FieldType::None));
	CHECK(IsValueOfType(UUID(5), FieldType::AssetHandle, FieldType::None));
	CHECK(IsValueOfType(UUID(5), FieldType::UUID, FieldType::None));
	CHECK(IsValueOfType(std::string("x"), FieldType::String, FieldType::None));
	CHECK(IsValueOfType(StructValue{}, FieldType::Struct, FieldType::None));
	CHECK(IsValueOfType(std::vector<StructValue>{}, FieldType::Array, FieldType::Struct));
	CHECK_FALSE(IsValueOfType(StructValue{}, FieldType::Array, FieldType::Struct));
	CHECK_FALSE(IsValueOfType(std::vector<StructValue>{}, FieldType::Struct, FieldType::None));
	CHECK_FALSE(IsValueOfType(1.0f, FieldType::Struct, FieldType::None));
	CHECK(IsValueOfType(std::vector<float>{ 1.0f }, FieldType::Array, FieldType::Float));
	CHECK(IsValueOfType(std::vector<UUID>{}, FieldType::Array, FieldType::AssetHandle));
	CHECK_FALSE(IsValueOfType(std::vector<float>{}, FieldType::Array, FieldType::Double));
	CHECK_FALSE(IsValueOfType(1.0f, FieldType::Array, FieldType::Float));
	CHECK_FALSE(IsValueOfType(1.0f, FieldType::Float, FieldType::Float)); // only arrays have an element type
	CHECK_FALSE(IsValueOfType(1.0f, FieldType::None, FieldType::None));
	CHECK_FALSE(IsValueOfType(std::vector<int64_t>{}, FieldType::Array, FieldType::Enum));
}

TEST_CASE("Reflection: zero values")
{
	for (int index = static_cast<int>(FieldType::Bool); index < static_cast<int>(FieldType::Array); ++index)
	{
		const auto type = static_cast<FieldType>(index);
		if (type != FieldType::Struct) // a struct's zero value is that struct's default, see the struct tests
		{
			CHECK_MESSAGE(IsValueOfType(MakeZeroValue(type), type, FieldType::None), ToString(type));
		}
		if (IsArrayElementType(type))
		{
			CHECK_MESSAGE(IsValueOfType(MakeZeroValue(FieldType::Array, type), FieldType::Array, type), ToString(type));
		}
	}
	CHECK(std::get<glm::quat>(MakeZeroValue(FieldType::Quat)) == glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
	CHECK(std::get<glm::vec3>(MakeZeroValue(FieldType::Color3)) == glm::vec3(0.0f));
	CHECK(std::get<UUID>(MakeZeroValue(FieldType::Entity)) == UUID());
	CHECK(std::get<UUID>(MakeZeroValue(FieldType::UUID)) == UUID());
	CHECK(std::get<std::vector<StructValue>>(MakeZeroValue(FieldType::Array, FieldType::Struct)).empty());
	CHECK(std::get<int64_t>(MakeZeroValue(FieldType::Enum)) == 0);
	CHECK(std::get<std::vector<std::string>>(MakeZeroValue(FieldType::Array, FieldType::String)).empty());
}
