#include "gbpch.h"

#include "Gambit/Serialization/FieldSerializer.h"

#include "Gambit/Core/Utf8.h"
#include "Gambit/Reflection/FieldHashing.h"

#include <charconv>
#include <limits>

namespace Gambit
{
	namespace
	{
		constexpr std::string_view NaNText = "NaN";
		constexpr std::string_view InfinityText = "Infinity";
		constexpr std::string_view NegativeInfinityText = "-Infinity";

		// ---- Writing ----

		Json NonFiniteToJson(double value)
		{
			if (std::isnan(value))
			{
				return Json(std::string(NaNText));
			}
			return Json(std::string(value > 0.0 ? InfinityText : NegativeInfinityText));
		}

		Json DoubleToJson(double value)
		{
			return std::isfinite(value) ? Json(value) : NonFiniteToJson(value);
		}

		// The shortest text that reads back as the same float, as a double that the JSON writer prints with the same
		// digits: 0.1f is written "0.1", not "0.10000000149011612".
		Json FloatToJson(float value)
		{
			if (!std::isfinite(value))
			{
				return NonFiniteToJson(static_cast<double>(value));
			}
			if (value == 0.0f)
			{
				return Json(std::signbit(value) ? -0.0 : 0.0);
			}
			std::array<char, 64> buffer{};
			const auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
			GB_CORE_ASSERT(error == std::errc{}, "a float needs more than {} characters", buffer.size());
			// Parsed by the JSON library, which converts without depending on the C locale.
			const Json parsed = Json::parse(buffer.data(), end);
			const double shortest = parsed.get<double>();
			return Json(static_cast<float>(shortest) == value ? shortest : static_cast<double>(value));
		}

		Result<Json> EncodeStruct(const StructInfo& info, const StructValue& value, std::string_view path);

		template<typename T>
		Result<Json> EncodeScalar(
			[[maybe_unused]] const FieldInfo& field, [[maybe_unused]] FieldType type, const T& value,
			[[maybe_unused]] std::string_view path)
		{
			if constexpr (std::is_same_v<T, bool>)
			{
				return Json(value);
			}
			else if constexpr (std::is_same_v<T, int64_t>)
			{
				if (type == FieldType::Enum)
				{
					const std::string_view name = field.Attributes.Enum->FindName(value);
					if (name.empty())
					{
						return MakePathError(
							path, std::format("{} is not a value of enum '{}'", value, field.Attributes.Enum->Name));
					}
					return Json(std::string(name));
				}
				return Json(value);
			}
			else if constexpr (std::is_integral_v<T>)
			{
				return Json(value);
			}
			else if constexpr (std::is_same_v<T, float>)
			{
				return FloatToJson(value);
			}
			else if constexpr (std::is_same_v<T, double>)
			{
				return DoubleToJson(value);
			}
			else if constexpr (std::is_same_v<T, glm::vec2>)
			{
				return Json::array({ FloatToJson(value.x), FloatToJson(value.y) });
			}
			else if constexpr (std::is_same_v<T, glm::vec3>)
			{
				return Json::array({ FloatToJson(value.x), FloatToJson(value.y), FloatToJson(value.z) });
			}
			else if constexpr (std::is_same_v<T, glm::vec4> || std::is_same_v<T, glm::quat>)
			{
				return Json::array(
					{ FloatToJson(value.x), FloatToJson(value.y), FloatToJson(value.z), FloatToJson(value.w) });
			}
			else if constexpr (std::is_same_v<T, std::string>)
			{
				if (!IsValidUtf8(value))
				{
					return MakePathError(path, "the text is not valid UTF-8");
				}
				return Json(value);
			}
			else if constexpr (std::is_same_v<T, UUID>)
			{
				return Json(value.ToString());
			}
			else
			{
				static_assert(std::is_same_v<T, StructValue>, "not a field storage type");
				return EncodeStruct(*field.Attributes.Struct, value, path);
			}
		}

		Result<Json> EncodeValue(const FieldInfo& field, const FieldValue& value, std::string_view path)
		{
			return std::visit(
				[&]<typename S>(const S& stored) -> Result<Json> {
					if constexpr (Internal::IsStdVector<S>)
					{
						Json array = Json::array();
						for (const auto& element : stored)
						{
							const std::string elementPath = JsonPathAppend(path, array.size());
							Result<Json> encoded = [&] {
								if constexpr (std::is_same_v<S, std::vector<bool>>)
								{
									return EncodeScalar<bool>(field, field.ElementType, element, elementPath);
								}
								else
								{
									return EncodeScalar(field, field.ElementType, element, elementPath);
								}
							}();
							if (!encoded)
							{
								return encoded;
							}
							array.push_back(std::move(*encoded));
						}
						return array;
					}
					else
					{
						return EncodeScalar(field, field.Type, stored, path);
					}
				},
				value);
		}

		Result<Json> EncodeStruct(const StructInfo& info, const StructValue& value, std::string_view path)
		{
			const std::span<const FieldInfo> fields = info.GetFields();
			GB_CORE_ASSERT(
				value.Values.size() == fields.size(), "struct '{}': the value has the wrong size", info.GetName());
			Json object = Json::object();
			for (size_t index = 0; index < fields.size(); ++index)
			{
				if (!IsSerialized(fields[index]))
				{
					continue;
				}
				Result<Json> encoded =
					EncodeValue(fields[index], value.Values[index], JsonPathAppend(path, fields[index].Name));
				if (!encoded)
				{
					return encoded;
				}
				object[fields[index].Name] = std::move(*encoded);
			}
			return object;
		}

		// ---- Reading ----

		std::string_view Describe(const Json& json) noexcept
		{
			switch (json.type())
			{
				case Json::value_t::null:
					return "null";
				case Json::value_t::object:
					return "an object";
				case Json::value_t::array:
					return "an array";
				case Json::value_t::string:
					return "a string";
				case Json::value_t::boolean:
					return "a boolean";
				case Json::value_t::number_integer:
				case Json::value_t::number_unsigned:
				case Json::value_t::number_float:
					return "a number";
				default:
					return "unexpected data";
			}
		}

		Result<double> ReadDouble(const Json& json, std::string_view path)
		{
			if (json.is_number())
			{
				// Infinity is spelled as a string: a number that overflows a double is a mistake, not an infinity.
				const double value = json.get<double>();
				if (!std::isfinite(value))
				{
					return MakePathError(path, "the number is out of range");
				}
				return value;
			}
			if (json.is_string())
			{
				const auto& text = json.get_ref<const std::string&>();
				if (text == NaNText)
				{
					return std::numeric_limits<double>::quiet_NaN();
				}
				if (text == InfinityText)
				{
					return std::numeric_limits<double>::infinity();
				}
				if (text == NegativeInfinityText)
				{
					return -std::numeric_limits<double>::infinity();
				}
			}
			return JsonTypeError(path, "a number", json);
		}

		Result<float> ReadFloat(const Json& json, std::string_view path)
		{
			Result<double> value = ReadDouble(json, path);
			if (!value)
			{
				return value.GetError();
			}
			const auto narrowed = static_cast<float>(*value);
			if (std::isfinite(*value) && !std::isfinite(narrowed))
			{
				return MakePathError(path, "the number is too large for a 32-bit float");
			}
			return narrowed;
		}

		template<size_t N>
		Result<std::array<float, N>> ReadFloats(const Json& json, std::string_view path)
		{
			if (!json.is_array())
			{
				return JsonTypeError(path, std::format("an array of {} numbers", N), json);
			}
			if (json.size() != N)
			{
				return MakePathError(path, std::format("expected {} numbers, found {}", N, json.size()));
			}
			std::array<float, N> values{};
			for (size_t index = 0; index < N; ++index)
			{
				Result<float> value = ReadFloat(json[index], JsonPathAppend(path, index));
				if (!value)
				{
					return value.GetError();
				}
				values[index] = *value;
			}
			return values;
		}

		template<typename T>
		Result<T> DecodeScalar(
			[[maybe_unused]] const FieldInfo& field, [[maybe_unused]] FieldType type, const Json& json,
			std::string_view path, [[maybe_unused]] LoadReport& report)
		{
			if constexpr (std::is_same_v<T, bool>)
			{
				if (!json.is_boolean())
				{
					return JsonTypeError(path, "true or false", json);
				}
				return json.get<bool>();
			}
			else if constexpr (std::is_same_v<T, int64_t>)
			{
				if (type == FieldType::Enum)
				{
					if (!json.is_string())
					{
						return JsonTypeError(path, "the name of an enum value", json);
					}
					const EnumInfo& info = *field.Attributes.Enum;
					const std::optional<int64_t> value = info.FindValue(json.get_ref<const std::string&>());
					if (!value.has_value())
					{
						std::string names;
						for (const EnumEntry& entry : info.Entries)
						{
							names += names.empty() ? entry.Name : ", " + entry.Name;
						}
						return MakePathError(
							path,
							std::format(
								"'{}' is not a value of enum '{}' (expected one of: {})",
								json.get_ref<const std::string&>(), info.Name, names));
					}
					return *value;
				}
				return ReadJsonInteger<int64_t>(json, path);
			}
			else if constexpr (std::is_integral_v<T>)
			{
				return ReadJsonInteger<T>(json, path);
			}
			else if constexpr (std::is_same_v<T, float>)
			{
				return ReadFloat(json, path);
			}
			else if constexpr (std::is_same_v<T, double>)
			{
				return ReadDouble(json, path);
			}
			else if constexpr (std::is_same_v<T, glm::vec2>)
			{
				Result<std::array<float, 2>> v = ReadFloats<2>(json, path);
				if (!v)
				{
					return v.GetError();
				}
				return glm::vec2((*v)[0], (*v)[1]);
			}
			else if constexpr (std::is_same_v<T, glm::vec3>)
			{
				Result<std::array<float, 3>> v = ReadFloats<3>(json, path);
				if (!v)
				{
					return v.GetError();
				}
				return glm::vec3((*v)[0], (*v)[1], (*v)[2]);
			}
			else if constexpr (std::is_same_v<T, glm::vec4>)
			{
				Result<std::array<float, 4>> v = ReadFloats<4>(json, path);
				if (!v)
				{
					return v.GetError();
				}
				return glm::vec4((*v)[0], (*v)[1], (*v)[2], (*v)[3]);
			}
			else if constexpr (std::is_same_v<T, glm::quat>)
			{
				Result<std::array<float, 4>> v = ReadFloats<4>(json, path);
				if (!v)
				{
					return v.GetError();
				}
				return glm::quat((*v)[3], (*v)[0], (*v)[1], (*v)[2]); // stored as [x, y, z, w]
			}
			else if constexpr (std::is_same_v<T, std::string>)
			{
				if (!json.is_string())
				{
					return JsonTypeError(path, "a string", json);
				}
				return json.get<std::string>();
			}
			else if constexpr (std::is_same_v<T, UUID>)
			{
				if (!json.is_string())
				{
					return JsonTypeError(path, "a UUID string like \"0x00000000DEADBEEF\"", json);
				}
				const std::optional<UUID> value = UUID::Parse(json.get_ref<const std::string&>());
				if (!value.has_value())
				{
					return MakePathError(
						path,
						std::format(
							"'{}' is not a UUID: expected \"0x\" and up to 16 hexadecimal digits",
							json.get_ref<const std::string&>()));
				}
				return *value;
			}
			else
			{
				static_assert(std::is_same_v<T, StructValue>, "not a field storage type");
				return StructValueFromJson(*field.Attributes.Struct, json, path, report);
			}
		}

		Result<FieldValue>
		DecodeValue(const FieldInfo& field, const Json& json, std::string_view path, LoadReport& report)
		{
			if (field.Type == FieldType::Array)
			{
				if (!json.is_array())
				{
					return JsonTypeError(path, "an array", json);
				}
				return Internal::VisitStorageType(
					field.ElementType, [&]<typename T>(std::type_identity<T>) -> Result<FieldValue> {
						std::vector<T> elements;
						elements.reserve(json.size());
						for (size_t index = 0; index < json.size(); ++index)
						{
							Result<T> element = DecodeScalar<T>(
								field, field.ElementType, json[index], JsonPathAppend(path, index), report);
							if (!element)
							{
								return element.GetError();
							}
							elements.push_back(std::move(*element));
						}
						return FieldValue(std::in_place_type<std::vector<T>>, std::move(elements));
					});
			}
			return Internal::VisitStorageType(field.Type, [&]<typename T>(std::type_identity<T>) -> Result<FieldValue> {
				Result<T> value = DecodeScalar<T>(field, field.Type, json, path, report);
				if (!value)
				{
					return value.GetError();
				}
				return FieldValue(std::in_place_type<T>, std::move(*value));
			});
		}

		Result<StructValue> MergeStruct(
			const StructInfo& info, const StructValue& current, const Json& json, std::string_view path,
			LoadReport& report);

		Result<FieldValue> MergeValue(
			const FieldInfo& field, const FieldValue& current, const Json& json, std::string_view path,
			LoadReport& report)
		{
			if (field.Type == FieldType::Struct)
			{
				const StructInfo& info = *field.Attributes.Struct;
				const auto* base = std::get_if<StructValue>(&current);
				Result<StructValue> merged =
					MergeStruct(info, base != nullptr ? *base : info.GetDefault(), json, path, report);
				if (!merged)
				{
					return merged.GetError();
				}
				return FieldValue(std::in_place_type<StructValue>, std::move(*merged));
			}
			if (field.Type == FieldType::Array && field.ElementType == FieldType::Struct)
			{
				if (!json.is_array())
				{
					return JsonTypeError(path, "an array", json);
				}
				const StructInfo& info = *field.Attributes.Struct;
				const auto* base = std::get_if<std::vector<StructValue>>(&current);
				std::vector<StructValue> elements;
				elements.reserve(json.size());
				for (size_t index = 0; index < json.size(); ++index)
				{
					const StructValue& start =
						base != nullptr && index < base->size() ? (*base)[index] : info.GetDefault();
					Result<StructValue> merged =
						MergeStruct(info, start, json[index], JsonPathAppend(path, index), report);
					if (!merged)
					{
						return merged.GetError();
					}
					elements.push_back(std::move(*merged));
				}
				return FieldValue(std::in_place_type<std::vector<StructValue>>, std::move(elements));
			}
			return DecodeValue(field, json, path, report);
		}

		Result<StructValue> MergeStruct(
			const StructInfo& info, const StructValue& current, const Json& json, std::string_view path,
			LoadReport& report)
		{
			if (!json.is_object())
			{
				return JsonTypeError(path, "an object", json);
			}
			const std::span<const FieldInfo> fields = info.GetFields();
			StructValue value = current.Values.size() == fields.size() ? current : info.GetDefault();
			for (const auto& [name, member] : json.items())
			{
				const std::string memberPath = JsonPathAppend(path, name);
				const auto field = std::ranges::find(fields, name, &FieldInfo::Name);
				if (field == fields.end())
				{
					report.Warn(memberPath, "unknown field, ignored");
					continue;
				}
				if (!IsSerialized(*field))
				{
					report.Warn(memberPath, "this field is not stored in files, ignored");
					continue;
				}
				const size_t index = static_cast<size_t>(field - fields.begin());
				Result<FieldValue> merged = MergeValue(*field, value.Values[index], member, memberPath, report);
				if (!merged)
				{
					return merged.GetError();
				}
				value.Values[index] = std::move(*merged);
			}
			return value;
		}
	}

	Error JsonTypeError(std::string_view path, std::string_view what, const Json& found)
	{
		return MakePathError(path, std::format("expected {}, found {}", what, Describe(found)));
	}

	template<typename T>
	Result<T> ReadJsonInteger(const Json& json, std::string_view path)
	{
		if (json.is_number_unsigned())
		{
			const uint64_t value = json.get<uint64_t>();
			if (!std::in_range<T>(value))
			{
				return MakePathError(path, std::format("{} is out of range", value));
			}
			return static_cast<T>(value);
		}
		if (json.is_number_integer())
		{
			const int64_t value = json.get<int64_t>();
			if (!std::in_range<T>(value))
			{
				return MakePathError(path, std::format("{} is out of range", value));
			}
			return static_cast<T>(value);
		}
		if (json.is_number_float())
		{
			// 3.0 is accepted (spreadsheets and scripts write integers like that), 3.5 is not. The limits are
			// powers of two, so they are exact as doubles.
			const double value = json.get<double>();
			constexpr double Limit = 9223372036854775808.0; // 2^63
			if (!std::isfinite(value) || value != std::floor(value))
			{
				return JsonTypeError(path, "an integer", json);
			}
			if (value < -Limit || value >= 2.0 * Limit)
			{
				return MakePathError(path, "the number is out of range");
			}
			const bool fits = value >= Limit ? std::in_range<T>(static_cast<uint64_t>(value))
											 : std::in_range<T>(static_cast<int64_t>(value));
			if (!fits)
			{
				return MakePathError(path, "the number is out of range");
			}
			return value >= Limit ? static_cast<T>(static_cast<uint64_t>(value))
								  : static_cast<T>(static_cast<int64_t>(value));
		}
		return JsonTypeError(path, "an integer", json);
	}

	template Result<int32_t> ReadJsonInteger<int32_t>(const Json&, std::string_view);
	template Result<uint32_t> ReadJsonInteger<uint32_t>(const Json&, std::string_view);
	template Result<int64_t> ReadJsonInteger<int64_t>(const Json&, std::string_view);
	template Result<uint64_t> ReadJsonInteger<uint64_t>(const Json&, std::string_view);

	Result<Json> FieldValueToJson(const FieldInfo& field, const FieldValue& value, std::string_view path)
	{
		if (!field.CanHold(value))
		{
			return MakePathError(
				path, std::format("the value does not fit field '{}' ({})", field.Name, ToString(field.Type)));
		}
		return EncodeValue(field, value, path);
	}

	Result<FieldValue>
	FieldValueFromJson(const FieldInfo& field, const Json& json, std::string_view path, LoadReport& report)
	{
		return DecodeValue(field, json, path, report);
	}

	Result<FieldValue> FieldValueMergeFromJson(
		const FieldInfo& field, const FieldValue& current, const Json& json, std::string_view path, LoadReport& report)
	{
		return MergeValue(field, current, json, path, report);
	}

	Result<StructValue> StructValueMergeFromJson(
		const StructInfo& info, const StructValue& current, const Json& json, std::string_view path, LoadReport& report)
	{
		return MergeStruct(info, current, json, path, report);
	}

	Result<Json> StructValueToJson(const StructInfo& info, const StructValue& value, std::string_view path)
	{
		if (!info.CanHold(value))
		{
			return MakePathError(path, std::format("the value does not fit struct '{}'", info.GetName()));
		}
		return EncodeStruct(info, value, path);
	}

	Result<void> ReadFieldMembers(
		std::span<const FieldInfo> fields, const Json& object, std::string_view path, LoadReport& report,
		const std::function<void(size_t index, FieldValue&& value)>& onField)
	{
		if (!object.is_object())
		{
			return JsonTypeError(path, "an object", object);
		}
		for (const auto& [name, member] : object.items())
		{
			const std::string memberPath = JsonPathAppend(path, name);
			const auto field = std::ranges::find(fields, name, &FieldInfo::Name);
			if (field == fields.end())
			{
				report.Warn(memberPath, "unknown field, ignored");
				continue;
			}
			if (!IsSerialized(*field))
			{
				report.Warn(memberPath, "this field is not stored in files, ignored");
				continue;
			}
			Result<FieldValue> value = DecodeValue(*field, member, memberPath, report);
			if (!value)
			{
				return value.GetError();
			}
			onField(static_cast<size_t>(field - fields.begin()), std::move(*value));
		}
		return {};
	}

	Result<StructValue>
	StructValueFromJson(const StructInfo& info, const Json& json, std::string_view path, LoadReport& report)
	{
		StructValue value = info.GetDefault();
		Result<void> read =
			ReadFieldMembers(info.GetFields(), json, path, report, [&](size_t index, FieldValue&& decoded) {
				value.Values[index] = std::move(decoded);
			});
		if (!read)
		{
			return read.GetError();
		}
		return value;
	}

	Result<Json> StructToJson(const StructInfo& info, const void* object, std::string_view path)
	{
		return EncodeStruct(info, info.Read(object), path);
	}

	Result<void>
	StructFromJson(const StructInfo& info, const Json& json, void* object, std::string_view path, LoadReport& report)
	{
		StructValue value = info.Read(object);
		Result<void> read =
			ReadFieldMembers(info.GetFields(), json, path, report, [&](size_t index, FieldValue&& decoded) {
				value.Values[index] = std::move(decoded);
			});
		if (!read)
		{
			return read.GetError();
		}
		// Decoded values fit their fields (enum names resolved, structs complete), so the write cannot be refused.
		[[maybe_unused]] const bool written = info.Write(object, value);
		GB_CORE_ASSERT(written, "struct '{}': a decoded value does not fit", info.GetName());
		return {};
	}
}
