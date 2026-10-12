#include <doctest/doctest.h>

#include "Reflection/ReflectionTestTypes.h"

#include "Gambit/Math/Random.h"
#include "Gambit/Serialization/FieldSerializer.h"

#include <bit>
#include <cfloat>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

using namespace Gambit;
using namespace Gambit::Tests;

namespace
{
	struct Fixture
	{
		TypeRegistry Registry;
		const ComponentInfo* AllTypes = nullptr;
		const ComponentInfo* Structs = nullptr;

		Fixture()
		{
			RegisterTestTypes(Registry);
			AllTypes = &Registry.GetComponent<AllTypesComponent>();
			Structs = &Registry.GetComponent<StructsComponent>();
		}
	};

	// A throwaway field of the given shape, for values that the test components do not have.
	FieldInfo MakeField(FieldType type, FieldType element = FieldType::None, FieldAttributes attributes = {})
	{
		FieldInfo field;
		field.Name = "Value";
		field.Type = type;
		field.ElementType = element;
		field.Attributes = std::move(attributes);
		field.Default = MakeZeroValue(type, element);
		return field;
	}

	Json Encode(const FieldInfo& field, const FieldValue& value)
	{
		const Result<Json> encoded = FieldValueToJson(field, value);
		REQUIRE_MESSAGE(encoded.HasValue(), (encoded.HasValue() ? std::string() : encoded.GetError().Message));
		return *encoded;
	}

	FieldValue Decode(const FieldInfo& field, const Json& json, LoadReport* report = nullptr)
	{
		LoadReport local;
		const Result<FieldValue> decoded =
			FieldValueFromJson(field, json, "/Value", report != nullptr ? *report : local);
		REQUIRE_MESSAGE(decoded.HasValue(), (decoded.HasValue() ? std::string() : decoded.GetError().Message));
		return *decoded;
	}

	std::string DecodeError(const FieldInfo& field, const Json& json)
	{
		LoadReport report;
		const Result<FieldValue> decoded = FieldValueFromJson(field, json, "/Value", report);
		REQUIRE_FALSE(decoded.HasValue());
		return decoded.GetError().Message;
	}

	bool SameBits(float a, float b)
	{
		return std::bit_cast<uint32_t>(a) == std::bit_cast<uint32_t>(b);
	}
}

TEST_CASE("Serialization: every field type round-trips through JSON")
{
	Fixture fixture;
	for (const ComponentInfo* info : { fixture.AllTypes, fixture.Structs })
	{
		for (const FieldInfo& field : info->GetFields())
		{
			CAPTURE(info->GetName());
			CAPTURE(field.Name);
			for (const FieldValue& value : { field.Default, MakeDifferentValue(field, field.Default) })
			{
				const Json json = Encode(field, value);
				CHECK(Decode(field, json) == value);
				// The text form reads back to the same value, too.
				const Result<Json> reparsed = ParseJson(*DumpJson(json));
				REQUIRE(reparsed.HasValue());
				CHECK(Decode(field, *reparsed) == value);
			}
		}
	}
}

TEST_CASE("Serialization: field values have the documented JSON shapes")
{
	Fixture fixture;
	AllTypesComponent component;
	component.Quat = glm::quat(0.5f, 0.25f, 0.75f, 0.125f); // w, x, y, z
	const auto json = [&](const char* name) {
		const FieldInfo& field = *fixture.AllTypes->FindField(name);
		return Encode(field, field.GetValue(&component));
	};

	CHECK(json("Bool") == Json(true));
	CHECK(json("Int32") == Json(-7));
	CHECK(json("UInt32") == Json(7u));
	CHECK(json("Int64") == Json(-(int64_t{ 1 } << 40)));
	CHECK(json("UInt64") == Json(uint64_t{ 1 } << 60));
	CHECK(json("Float") == Json(1.5));
	CHECK(json("Double") == Json(2.25));
	CHECK(json("Vec2") == Json::array({ 1.0, 2.0 }));
	CHECK(json("Vec3") == Json::array({ 1.0, 2.0, 3.0 }));
	CHECK(json("Vec4") == Json::array({ 1.0, 2.0, 3.0, 4.0 }));
	CHECK(json("Quat") == Json::array({ 0.25, 0.75, 0.125, 0.5 })); // [x, y, z, w]
	CHECK(json("Color3") == Json::array({ 0.5, 0.25, 1.0 }));
	CHECK(json("Color4") == Json::array({ 0.5, 0.25, 1.0, 0.75 }));
	CHECK(json("String") == Json("hello"));
	CHECK(json("Target") == Json("0x0000000000001234"));
	CHECK(json("Texture") == Json("0x000000000000ABCD"));
	CHECK(json("Mode") == Json("Slow")); // enums by name
	CHECK(json("Bools") == Json::array({ true, false }));
	CHECK(json("Strings") == Json::array({ "a", "b" }));
	CHECK(json("Textures") == Json::array({ "0x0000000000000002" }));
	CHECK(json("Vec3s") == Json::array({ Json::array({ 3.0, 3.0, 3.0 }) }));
	CHECK(DumpJson(Json::array({ json("Float"), json("Vec3") })).Value().find("1.5") != std::string::npos);
}

TEST_CASE("Serialization: floats are written with the shortest text that reads back exactly")
{
	const FieldInfo field = MakeField(FieldType::Float);
	const auto text = [&](float value) { return Encode(field, value).dump(); };
	CHECK(text(0.1f) == "0.1"); // not 0.10000000149011612
	CHECK(text(1.0f) == "1.0");
	CHECK(text(-2.5f) == "-2.5");
	CHECK(text(3.14159274f) == "3.1415927");
	CHECK(text(1e-5f) == "1e-05");
	CHECK(text(16777216.0f) == "16777216.0");
	CHECK(text(0.0f) == "0.0");
	CHECK(text(-0.0f) == "-0.0");
	CHECK(text(FLT_MAX) == "3.4028235e+38");

	// Signed zero, extremes and subnormals survive a round trip bit for bit.
	for (const float value : { 0.0f, -0.0f, FLT_MAX, -FLT_MAX, FLT_MIN, FLT_TRUE_MIN, 1e-45f, 16777217.0f, 0.3f })
	{
		CHECK(SameBits(std::get<float>(Decode(field, Encode(field, value))), value));
	}

	// So does a wide sample of bit patterns, through the text of the document.
	Random random(99);
	for (int index = 0; index < 4000; ++index)
	{
		const float value = std::bit_cast<float>(random.NextU32());
		if (!std::isfinite(value))
		{
			continue;
		}
		const Result<Json> reparsed = ParseJson(Encode(field, value).dump());
		REQUIRE(reparsed.HasValue());
		CHECK_MESSAGE(SameBits(std::get<float>(Decode(field, *reparsed)), value), value);
	}
}

TEST_CASE("Serialization: doubles round-trip exactly")
{
	const FieldInfo field = MakeField(FieldType::Double);
	CHECK(Encode(field, 0.1).dump() == "0.1");
	CHECK(Encode(field, 1.0).dump() == "1.0");
	Random random(7);
	for (int index = 0; index < 4000; ++index)
	{
		const double value = std::bit_cast<double>(random.NextU64());
		if (!std::isfinite(value))
		{
			continue;
		}
		const Result<Json> reparsed = ParseJson(Encode(field, value).dump());
		REQUIRE(reparsed.HasValue());
		CHECK_MESSAGE(
			std::bit_cast<uint64_t>(std::get<double>(Decode(field, *reparsed))) == std::bit_cast<uint64_t>(value),
			value);
	}
}

TEST_CASE("Serialization: NaN and infinities are written as strings and read back")
{
	for (const FieldType type : { FieldType::Float, FieldType::Double })
	{
		const FieldInfo field = MakeField(type);
		const FieldValue nan = type == FieldType::Float ? FieldValue(std::numeric_limits<float>::quiet_NaN())
														: FieldValue(std::numeric_limits<double>::quiet_NaN());
		const FieldValue inf = type == FieldType::Float ? FieldValue(std::numeric_limits<float>::infinity())
														: FieldValue(std::numeric_limits<double>::infinity());
		const FieldValue negInf = type == FieldType::Float ? FieldValue(-std::numeric_limits<float>::infinity())
														   : FieldValue(-std::numeric_limits<double>::infinity());
		CHECK(Encode(field, nan) == Json("NaN"));
		CHECK(Encode(field, inf) == Json("Infinity"));
		CHECK(Encode(field, negInf) == Json("-Infinity"));
		CHECK(Decode(field, Json("Infinity")) == inf);
		CHECK(Decode(field, Json("-Infinity")) == negInf);
		const FieldValue decodedNan = Decode(field, Json("NaN"));
		CHECK(std::isnan(type == FieldType::Float ? std::get<float>(decodedNan) : std::get<double>(decodedNan)));
	}

	// A number that overflows is not an infinity: that has to be written as a string.
	const FieldInfo doubleField = MakeField(FieldType::Double);
	CHECK(
		DecodeError(doubleField, Json(std::numeric_limits<double>::infinity())).find("out of range")
		!= std::string::npos);
	CHECK(
		DecodeError(MakeField(FieldType::Float), Json(1e300)).find("too large for a 32-bit float")
		!= std::string::npos);

	// Vector components, too.
	const FieldInfo vec3 = MakeField(FieldType::Vec3);
	const Json json = Json::array({ 1.0, "Infinity", "-Infinity" });
	const glm::vec3 decoded = std::get<glm::vec3>(Decode(vec3, json));
	CHECK(decoded.x == 1.0f);
	CHECK(std::isinf(decoded.y));
	CHECK(decoded.z < 0.0f);
	CHECK(Encode(vec3, decoded) == json);
}

TEST_CASE("Serialization: integers keep their full range and reject what does not fit")
{
	const FieldInfo i32 = MakeField(FieldType::Int32);
	const FieldInfo u32 = MakeField(FieldType::UInt32);
	const FieldInfo i64 = MakeField(FieldType::Int64);
	const FieldInfo u64 = MakeField(FieldType::UInt64);

	CHECK(Decode(i32, Json(INT32_MIN)) == FieldValue(INT32_MIN));
	CHECK(Decode(i32, Json(INT32_MAX)) == FieldValue(INT32_MAX));
	CHECK(Decode(u32, Json(UINT32_MAX)) == FieldValue(UINT32_MAX));
	CHECK(Decode(i64, Json(INT64_MIN)) == FieldValue(INT64_MIN));
	CHECK(Decode(i64, Json(INT64_MAX)) == FieldValue(INT64_MAX));
	CHECK(Decode(u64, Json(UINT64_MAX)) == FieldValue(UINT64_MAX));
	CHECK(Encode(u64, UINT64_MAX).dump() == "18446744073709551615");
	CHECK(Encode(i64, INT64_MIN).dump() == "-9223372036854775808");

	// Whole numbers written as floats are fine (scripts and spreadsheets do that), fractions are not.
	CHECK(Decode(i32, Json(3.0)) == FieldValue(int32_t{ 3 }));
	CHECK(Decode(i32, Json(-2.0)) == FieldValue(int32_t{ -2 }));
	CHECK(Decode(u64, Json(4294967296.0)) == FieldValue(uint64_t{ 4294967296 }));
	CHECK(Decode(u64, Json(9223372036854775808.0)) == FieldValue(uint64_t{ 9223372036854775808ull }));
	CHECK(DecodeError(i32, Json(3.5)).find("expected an integer") != std::string::npos);

	CHECK(DecodeError(i32, Json(int64_t{ INT32_MAX } + 1)).find("out of range") != std::string::npos);
	CHECK(DecodeError(i32, Json(int64_t{ INT32_MIN } - 1)).find("out of range") != std::string::npos);
	CHECK(DecodeError(u32, Json(-1)).find("out of range") != std::string::npos);
	CHECK(DecodeError(u32, Json(int64_t{ UINT32_MAX } + 1)).find("out of range") != std::string::npos);
	CHECK(DecodeError(i64, Json(uint64_t{ INT64_MAX } + 1)).find("out of range") != std::string::npos);
	CHECK(DecodeError(u64, Json(-1)).find("out of range") != std::string::npos);
	CHECK(DecodeError(i32, Json(1e30)).find("out of range") != std::string::npos);
	CHECK(DecodeError(u64, Json(-1.0)).find("out of range") != std::string::npos);
	CHECK(DecodeError(u64, Json(18446744073709551616.0)).find("out of range") != std::string::npos);
	CHECK(DecodeError(i32, Json("3")).find("expected an integer, found a string") != std::string::npos);
	CHECK(DecodeError(i32, Json(true)).find("expected an integer, found a boolean") != std::string::npos);
	CHECK(DecodeError(i32, Json()).find("found null") != std::string::npos);
}

TEST_CASE("Serialization: wrong kinds of values are errors that name the path")
{
	Fixture fixture;
	const auto error = [&](const char* name, const Json& json) {
		return DecodeError(*fixture.AllTypes->FindField(name), json);
	};

	CHECK(error("Bool", Json(1)) == "/Value: expected true or false, found a number");
	CHECK(error("Float", Json("fast")) == "/Value: expected a number, found a string");
	CHECK(error("Float", Json(1e39)).find("too large for a 32-bit float") != std::string::npos);
	CHECK(error("Vec3", Json::array({ 1.0, 2.0 })) == "/Value: expected 3 numbers, found 2");
	CHECK(error("Vec3", Json::array({ 1.0, "x", 3.0 })) == "/Value/1: expected a number, found a string");
	CHECK(error("Vec3", Json(1.0)) == "/Value: expected an array of 3 numbers, found a number");
	CHECK(error("Quat", Json::array({ 0.0, 0.0, 0.0 })) == "/Value: expected 4 numbers, found 3");
	CHECK(error("String", Json(5)) == "/Value: expected a string, found a number");
	CHECK(error("Target", Json(5)).find("expected a UUID string") != std::string::npos);
	CHECK(error("Target", Json("1234")).find("not a UUID") != std::string::npos);
	CHECK(error("Target", Json("0x")).find("not a UUID") != std::string::npos);
	CHECK(error("Target", Json("0x11112222333344445")).find("not a UUID") != std::string::npos); // 17 digits
	CHECK(error("Mode", Json(3)) == "/Value: expected the name of an enum value, found a number");
	const std::string unknownMode = error("Mode", Json("Warp"));
	CHECK(unknownMode.find("'Warp' is not a value of enum 'TestMode'") != std::string::npos);
	CHECK(unknownMode.find("Reverse, Off, Slow, Fast") != std::string::npos);
	CHECK(error("Floats", Json(1.0)) == "/Value: expected an array, found a number");
	CHECK(error("Floats", Json::array({ 1.0, "x" })) == "/Value/1: expected a number, found a string");
	CHECK(error("Int32s", Json::array({ 1, 2.5 })).find("/Value/1: expected an integer") == 0);
	CHECK(error("Strings", Json::array({ "a", 2 })) == "/Value/1: expected a string, found a number");
	CHECK(
		error("Vec3s", Json::array({ Json::array({ 1.0, 2.0, 3.0 }), Json::array({ 1.0 }) }))
		== "/Value/1: expected 3 numbers, found 1");
}

TEST_CASE("Serialization: strings must be UTF-8 to be written")
{
	const FieldInfo field = MakeField(FieldType::String);
	CHECK(Encode(field, std::string("caf\xC3\xA9")) == Json("caf\xC3\xA9"));
	const Result<Json> bad = FieldValueToJson(field, std::string("bad \xFF"), "/Name");
	REQUIRE_FALSE(bad.HasValue());
	CHECK(bad.GetError().Message == "/Name: the text is not valid UTF-8");

	const FieldInfo strings = MakeField(FieldType::Array, FieldType::String);
	const Result<Json> badElement = FieldValueToJson(strings, std::vector<std::string>{ "ok", "\xC3\x28" }, "/Names");
	REQUIRE_FALSE(badElement.HasValue());
	CHECK(badElement.GetError().Message == "/Names/1: the text is not valid UTF-8");
}

TEST_CASE("Serialization: values that do not fit their field are refused when writing")
{
	Fixture fixture;
	const FieldInfo& mode = *fixture.AllTypes->FindField("Mode");
	CHECK_FALSE(FieldValueToJson(mode, int64_t{ 1 }).HasValue()); // not a TestMode value
	CHECK_FALSE(FieldValueToJson(mode, 1.0f).HasValue());
	const FieldInfo& main = *fixture.Structs->FindField("Main");
	CHECK_FALSE(FieldValueToJson(main, StructValue{}).HasValue());
}

TEST_CASE("Serialization: structs are objects keyed by field name")
{
	Fixture fixture;
	const FieldInfo& main = *fixture.Structs->FindField("Main");
	StructsComponent component;
	component.Main.Clip = "walk";
	component.Main.Stats.Level = 12;
	component.Main.Cache = 4.0f; // NotSerialized
	component.Main.History.push_back(TestStats{ 1, 0.5f, "old" });

	const Json json = Encode(main, main.GetValue(&component));
	REQUIRE(json.is_object());
	std::vector<std::string> keys;
	for (const auto& [key, member] : json.items())
	{
		keys.push_back(key);
	}
	CHECK(keys == std::vector<std::string>{ "Clip", "Weight", "Mode", "Tint", "Target", "Stats", "Curve", "History" });
	CHECK(json["Clip"] == "walk");
	CHECK(json["Mode"] == "Slow");
	CHECK(json["Tint"] == Json::array({ 1.0, 0.5, 0.25 }));
	CHECK(json["Stats"] == Json::parse(R"({"Level": 12, "Speed": 1.5, "Label": "stat"})"));
	CHECK(json["History"] == Json::parse(R"([{"Level": 1, "Speed": 0.5, "Label": "old"}])"));
	CHECK_FALSE(json.contains("Cache"));

	// Reading: missing fields take the struct's defaults, unknown ones are warnings, nested problems name the path.
	LoadReport report;
	TestLayer layer;
	const Result<void> read = StructFromJson(
		*main.Attributes.Struct,
		Json::parse(R"({"Clip": "run", "Stats": {"Level": 3, "Extra": 1}, "Bogus": 2, "Cache": 1.0})"), &layer, "/Main",
		report);
	REQUIRE(read.HasValue());
	CHECK(layer.Clip == "run");
	CHECK(layer.Stats.Level == 3);
	CHECK(layer.Weight == 1.0f); // absent from the JSON: the object keeps its value
	CHECK(layer.Cache == 0.0f);  // not stored in files: ignored
	REQUIRE(report.GetWarnings().size() == 3);
	std::vector<std::string> warned;
	for (const Diagnostic& warning : report.GetWarnings())
	{
		warned.push_back(warning.Path);
	}
	CHECK(std::ranges::find(warned, "/Main/Bogus") != warned.end());
	CHECK(std::ranges::find(warned, "/Main/Stats/Extra") != warned.end());
	CHECK(std::ranges::find(warned, "/Main/Cache") != warned.end());

	LoadReport errors;
	const Result<StructValue> bad =
		StructValueFromJson(*main.Attributes.Struct, Json::parse(R"({"Stats": {"Level": "high"}})"), "/Main", errors);
	REQUIRE_FALSE(bad.HasValue());
	CHECK(bad.GetError().Message == "/Main/Stats/Level: expected an integer, found a string");
	CHECK_FALSE(StructValueFromJson(*main.Attributes.Struct, Json::array(), "/Main", errors).HasValue());
	const Result<StructValue> badElement = StructValueFromJson(
		*main.Attributes.Struct, Json::parse(R"({"History": [{"Level": 1}, {"Level": "x"}]})"), "/Main", errors);
	REQUIRE_FALSE(badElement.HasValue());
	CHECK(badElement.GetError().Message == "/Main/History/1/Level: expected an integer, found a string");

	// Missing members are the struct defaults.
	const Result<StructValue> defaults = StructValueFromJson(*main.Attributes.Struct, Json::object(), "", errors);
	REQUIRE(defaults.HasValue());
	CHECK(*defaults == main.Attributes.Struct->GetDefault());
}

TEST_CASE("Serialization: struct objects convert to JSON and back")
{
	Fixture fixture;
	const StructInfo& stats = *fixture.Registry.FindStruct("TestStats");
	TestStats original{ 8, 2.5f, "round" };
	const Result<Json> json = StructToJson(stats, &original);
	REQUIRE(json.HasValue());
	CHECK(*json == Json::parse(R"({"Level": 8, "Speed": 2.5, "Label": "round"})"));

	TestStats restored;
	LoadReport report;
	REQUIRE(StructFromJson(stats, *json, &restored, "", report).HasValue());
	CHECK(restored.Level == 8);
	CHECK(restored.Speed == 2.5f);
	CHECK(restored.Label == "round");
	CHECK(report.IsEmpty());

	// Members that are absent keep the value the object has.
	TestStats partial{ 1, 1.0f, "keep" };
	REQUIRE(StructFromJson(stats, Json::parse(R"({"Level": 5})"), &partial, "", report).HasValue());
	CHECK(partial.Level == 5);
	CHECK(partial.Label == "keep");

	// A failed read leaves the object alone.
	TestStats untouched{ 1, 1.0f, "same" };
	CHECK_FALSE(
		StructFromJson(stats, Json::parse(R"({"Level": 5, "Speed": "fast"})"), &untouched, "", report).HasValue());
	CHECK(untouched.Level == 1);
}
