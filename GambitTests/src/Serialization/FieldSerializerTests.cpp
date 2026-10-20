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

namespace
{
	std::string MergeError(const FieldInfo& field, const FieldValue& current, const Json& json)
	{
		LoadReport report;
		const Result<FieldValue> merged =
			FieldValueMergeFromJson(field, current, json, "/Value", report, MergeAccess::Code);
		REQUIRE_FALSE(merged.HasValue());
		return merged.GetError().Message;
	}

	FieldValue Merge(const FieldInfo& field, const FieldValue& current, const Json& json, LoadReport* report = nullptr)
	{
		LoadReport local;
		const Result<FieldValue> merged = FieldValueMergeFromJson(
			field, current, json, "/Value", report != nullptr ? *report : local, MergeAccess::Code);
		REQUIRE_MESSAGE(merged.HasValue(), (merged.HasValue() ? std::string() : merged.GetError().Message));
		return *merged;
	}
}

TEST_CASE("Serialization: merging a partial struct keeps the members it does not name")
{
	Fixture fixture;
	const FieldInfo& main = *fixture.Structs->FindField("Main");
	StructsComponent component;
	component.Main.Clip = "walk";
	component.Main.Weight = 0.75f;
	component.Main.Stats = TestStats{ 9, 2.5f, "kept" };
	component.Main.Curve = { 1.0f, 2.0f, 3.0f };
	component.Main.History.push_back(TestStats{ 1, 1.0f, "old" });
	const FieldValue current = main.GetValue(&component);

	// {"Weight": 0.5} changes the weight and nothing else (decoding it against the defaults would reset the rest).
	const FieldValue merged = Merge(main, current, Json::parse(R"({"Weight": 0.5})"));
	TestLayer layer;
	REQUIRE(main.Attributes.Struct->Write(&layer, std::get<StructValue>(merged)));
	CHECK(layer.Weight == 0.5f);
	CHECK(layer.Clip == "walk");
	CHECK(layer.Stats.Label == "kept");
	CHECK(layer.Curve == std::vector<float>{ 1.0f, 2.0f, 3.0f });
	REQUIRE(layer.History.size() == 1);
	CHECK(layer.History[0].Label == "old");

	// The decoding that is not for editing resets them.
	const FieldValue replaced = Decode(main, Json::parse(R"({"Weight": 0.5})"));
	REQUIRE(main.Attributes.Struct->Write(&layer, std::get<StructValue>(replaced)));
	CHECK(layer.Clip == "idle");

	// Nested structs merge too.
	const FieldValue nested = Merge(main, current, Json::parse(R"({"Stats": {"Level": 5}})"));
	REQUIRE(main.Attributes.Struct->Write(&layer, std::get<StructValue>(nested)));
	CHECK(layer.Stats.Level == 5);
	CHECK(layer.Stats.Speed == 2.5f);
	CHECK(layer.Stats.Label == "kept");
	CHECK(layer.Clip == "walk");

	// An empty object changes nothing.
	CHECK(Merge(main, current, Json::object()) == current);

	// Arrays that are not arrays of structs are replaced as a whole.
	const FieldValue curve = Merge(main, current, Json::parse(R"({"Curve": [9.0]})"));
	REQUIRE(main.Attributes.Struct->Write(&layer, std::get<StructValue>(curve)));
	CHECK(layer.Curve == std::vector<float>{ 9.0f });
}

TEST_CASE("Serialization: merging arrays of structs merges element by element")
{
	Fixture fixture;
	const FieldInfo& layers = *fixture.Structs->FindField("Layers");
	StructsComponent component;
	component.Layers.resize(2);
	component.Layers[0].Clip = "first";
	component.Layers[0].Weight = 0.25f;
	component.Layers[1].Clip = "second";
	component.Layers[1].Stats.Label = "second stats";
	component.Layers[1].History.push_back(TestStats{ 7, 7.0f, "history" });
	const FieldValue current = layers.GetValue(&component);

	const auto write = [&](const FieldValue& value) {
		StructsComponent target;
		REQUIRE(layers.SetValue(&target, value));
		return target;
	};

	// Element 0 merges; the array has as many elements as the JSON.
	StructsComponent result = write(Merge(layers, current, Json::parse(R"([{"Weight": 0.5}])")));
	REQUIRE(result.Layers.size() == 1);
	CHECK(result.Layers[0].Weight == 0.5f);
	CHECK(result.Layers[0].Clip == "first");

	// Elements beyond the current ones start from the defaults; nested arrays of structs merge as well.
	result = write(Merge(
		layers, current,
		Json::parse(R"([{}, {"Stats": {"Level": 1}, "History": [{"Label": "changed"}]}, {"Weight": 0.1}])")));
	REQUIRE(result.Layers.size() == 3);
	CHECK(result.Layers[0].Clip == "first");
	CHECK(result.Layers[0].Weight == 0.25f);
	CHECK(result.Layers[1].Clip == "second");
	CHECK(result.Layers[1].Stats.Level == 1);
	CHECK(result.Layers[1].Stats.Label == "second stats");
	REQUIRE(result.Layers[1].History.size() == 1);
	CHECK(result.Layers[1].History[0].Level == 7);
	CHECK(result.Layers[1].History[0].Label == "changed");
	CHECK(result.Layers[2].Clip == "idle");
	CHECK(result.Layers[2].Weight == 0.1f);

	CHECK(write(Merge(layers, current, Json::array())).Layers.empty());
}

namespace
{
	struct SealedStats
	{
		int32_t Level = 1;
		int32_t Serial = 7; // read-only: tools show it, code writes it
	};

	struct SealedComponent
	{
		SealedStats Stats;
		std::vector<SealedStats> History{ SealedStats{} };
	};
}

TEST_CASE("Serialization: a merge for tools refuses to change read-only struct members")
{
	TypeRegistry registry;
	RegisterEngineTypes(registry);
	registry.RegisterStruct<SealedStats>("SealedStats")
		.Field("Level", &SealedStats::Level)
		.Field("Serial", &SealedStats::Serial, Attr::ReadOnly{});
	registry.RegisterComponent<SealedComponent>("Sealed")
		.Field("Stats", &SealedComponent::Stats)
		.Field("History", &SealedComponent::History);
	const ComponentInfo& sealed = registry.GetComponent<SealedComponent>();
	const FieldInfo& stats = *sealed.FindField("Stats");
	const FieldInfo& history = *sealed.FindField("History");

	const auto merge = [](const FieldInfo& field, const Json& json, MergeAccess access) {
		LoadReport report;
		return FieldValueMergeFromJson(field, field.Default, json, "/Value", report, access);
	};
	const Result<FieldValue> refused = merge(stats, Json::parse(R"({"Serial": 8})"), MergeAccess::Tool);
	REQUIRE_FALSE(refused.HasValue());
	CHECK(refused.GetError().Message == "/Value/Serial: the field 'Serial' is read-only");
	const Result<FieldValue> inArray = merge(history, Json::parse(R"([{}, {"Serial": 9}])"), MergeAccess::Tool);
	REQUIRE_FALSE(inArray.HasValue());
	CHECK(inArray.GetError().Message == "/Value/1/Serial: the field 'Serial' is read-only");

	// Repeating the current value is no change; the other members merge as usual.
	const Result<FieldValue> repeated = merge(stats, Json::parse(R"({"Serial": 7, "Level": 4})"), MergeAccess::Tool);
	REQUIRE(repeated.HasValue());
	CHECK(std::get<StructValue>(*repeated).Values[0] == FieldValue(int32_t{ 4 }));

	// Code may write it.
	const Result<FieldValue> written = merge(stats, Json::parse(R"({"Serial": 8})"), MergeAccess::Code);
	REQUIRE(written.HasValue());
	CHECK(std::get<StructValue>(*written).Values[1] == FieldValue(int32_t{ 8 }));
}

TEST_CASE("Serialization: merging reports unknown members and mistakes with their path")
{
	Fixture fixture;
	const FieldInfo& main = *fixture.Structs->FindField("Main");
	const FieldInfo& layers = *fixture.Structs->FindField("Layers");
	const FieldValue current = main.Default;

	LoadReport report;
	static_cast<void>(Merge(main, current, Json::parse(R"({"Bogus": 1, "Stats": {"Nope": 2}, "Cache": 3})"), &report));
	REQUIRE(report.GetWarnings().size() == 3);
	CHECK(report.GetWarnings()[0].Path == "/Value/Bogus");

	CHECK(MergeError(main, current, Json::parse("[]")) == "/Value: expected an object, found an array");
	CHECK(
		MergeError(main, current, Json::parse(R"({"Stats": {"Level": "high"}})"))
		== "/Value/Stats/Level: expected an integer, found a string");
	CHECK(MergeError(layers, layers.Default, Json::parse("{}")) == "/Value: expected an array, found an object");
	CHECK(
		MergeError(layers, layers.Default, Json::parse(R"([{}, 5])"))
		== "/Value/1: expected an object, found a number");
	CHECK(MergeError(layers, layers.Default, Json::parse(R"([{"Mode": "Warp"}])"))
	          .starts_with("/Value/0/Mode: 'Warp' is not a value of enum"));

	// Everything that is not a struct reads exactly as FieldValueFromJson does.
	const FieldInfo& level = *fixture.AllTypes->FindField("Int32");
	CHECK(Merge(level, FieldValue(int32_t{ 3 }), Json(40)) == FieldValue(int32_t{ 40 }));
	CHECK(MergeError(level, FieldValue(int32_t{ 3 }), Json("x")) == "/Value: expected an integer, found a string");

	// A current value of the wrong shape (a stale value) is replaced by the defaults before merging.
	const FieldValue merged = Merge(main, FieldValue(1.0f), Json::parse(R"({"Clip": "x"})"));
	CHECK(std::get<StructValue>(merged).Values.size() == main.Attributes.Struct->GetFields().size());
}
