#pragma once

#include "Gambit/Core/Result.h"
#include "Gambit/Reflection/FieldInfo.h"
#include "Gambit/Reflection/FieldType.h"
#include "Gambit/Serialization/Json.h"

#include <functional>
#include <span>
#include <string_view>

// Reflection to JSON and back (D-022, D-025): the one place that knows how a FieldValue looks in a file, so scenes,
// prefabs, project files, settings and the automation API all agree.
//
// The JSON form of a field value, by FieldType:
//   Bool: true/false; Int32, UInt32, Int64, UInt64: integers; Float, Double: numbers (written with the shortest text
//   that reads back to the same value; NaN and infinities as the strings "NaN", "Infinity", "-Infinity");
//   Vec2/3/4 and Color3/4: arrays of numbers ([x, y, z], [r, g, b, a]); Quat: [x, y, z, w]; String: a string;
//   Entity, AssetHandle, UUID: "0x00000000DEADBEEF" strings (the null UUID is "0x0000000000000000");
//   Enum: the name of the value; Struct: an object with one member per field; Array: an array of its elements.
// Integer fields also read integral floats (3.0). Nothing throws past these functions: malformed values are errors
// whose message starts with the JSON pointer of the offending element.

namespace Gambit
{
	// Whether files store the field (everything but NotSerialized).
	[[nodiscard]] inline bool IsSerialized(const FieldInfo& field) noexcept
	{
		return !field.HasFlag(FieldFlags::NotSerialized);
	}

	// `path` is the JSON pointer the result will have in its document; errors start with the pointer of the offending
	// element below it. Fails for values that do not fit the field (FieldInfo::CanHold) and for strings that are not
	// valid UTF-8.
	[[nodiscard]] Result<Json>
	FieldValueToJson(const FieldInfo& field, const FieldValue& value, std::string_view path = {});

	// `path` is the JSON pointer of `json`, for messages. Warnings (unknown members of struct values) go to `report`.
	[[nodiscard]] Result<FieldValue>
	FieldValueFromJson(const FieldInfo& field, const Json& json, std::string_view path, LoadReport& report);

	// Who a merge edits for. Tools (component.set, bulk.set, the inspector) may not change struct members marked
	// FieldFlags::ReadOnly, though they may repeat their current value; code (script table assignment) may change them.
	enum class MergeAccess : uint8_t
	{
		Tool,
		Code
	};

	// Like FieldValueFromJson, but for editing: a struct given as an object (or elements of an array of structs given
	// as objects) change only the members the object names, the others keep their values from `current`; nested structs
	// merge the same way. Element i of an array of structs merges into element i of the current array, elements beyond
	// it start from the struct's defaults, and the result has as many elements as `json`. Everything else (scalars,
	// vectors, arrays of other types) is replaced as a whole, exactly as FieldValueFromJson reads it. Unknown members
	// of struct objects are warnings in `report`; with MergeAccess::Tool a read-only member that would change is an
	// error naming its pointer. This is the rule of component.set, bulk.set and script table assignment. (The flags of
	// `field` itself are the caller's business.)
	[[nodiscard]] Result<FieldValue> FieldValueMergeFromJson(
		const FieldInfo& field, const FieldValue& current, const Json& json, std::string_view path, LoadReport& report,
		MergeAccess access);

	// A struct value as an object with a member for each serialized field, in field order.
	[[nodiscard]] Result<Json>
	StructValueToJson(const StructInfo& info, const StructValue& value, std::string_view path = {});
	// Fields missing from the object take their defaults; unknown members are warnings.
	[[nodiscard]] Result<StructValue>
	StructValueFromJson(const StructInfo& info, const Json& json, std::string_view path, LoadReport& report);

	// The same for a C++ struct object registered with TypeRegistry::RegisterStruct.
	[[nodiscard]] Result<Json> StructToJson(const StructInfo& info, const void* object, std::string_view path = {});
	// Reads the fields present in `json` into `object`; the others keep the values they have there.
	[[nodiscard]] Result<void>
	StructFromJson(const StructInfo& info, const Json& json, void* object, std::string_view path, LoadReport& report);

	// The error for a JSON value of the wrong kind: "<path>: expected <what>, found <kind>".
	[[nodiscard]] Error JsonTypeError(std::string_view path, std::string_view expected, const Json& found);

	// An integer member: integral floats (3.0) are accepted, other numbers and everything else are errors, as are
	// values outside the range of T. Instantiated for int32_t, uint32_t, int64_t and uint64_t.
	template<typename T>
	[[nodiscard]] Result<T> ReadJsonInteger(const Json& json, std::string_view path);

	// Decodes the members of `object` as values of `fields` and passes each to `onField` with the index of its field.
	// Members that match no field, or a field that is not serialized, are warnings. The caller supplies the defaults
	// of the fields that are missing. Stops at the first malformed value.
	[[nodiscard]] Result<void> ReadFieldMembers(
		std::span<const FieldInfo> fields, const Json& object, std::string_view path, LoadReport& report,
		const std::function<void(size_t index, FieldValue&& value)>& onField);
}
