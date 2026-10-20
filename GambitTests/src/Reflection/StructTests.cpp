#include <doctest/doctest.h>

#include "ECS/TestWorld.h"
#include "Reflection/ReflectionTestTypes.h"

#include "Gambit/Core/Hash.h"
#include "Gambit/ECS/Entity.h"
#include "Gambit/ECS/World.h"
#include "Gambit/Reflection/FieldHashing.h"
#include "Gambit/Reflection/TypeRegistry.h"

#include <string>
#include <vector>

using namespace Gambit;
using namespace Gambit::Tests;

namespace
{
	uint64_t DigestOf(const FieldInfo& field, const void* component)
	{
		Hasher hasher(7);
		field.HashValue(hasher, component);
		return hasher.Digest();
	}

	uint64_t DigestOf(const FieldInfo& field, const FieldValue& value)
	{
		Hasher hasher(7);
		HashFieldValue(hasher, field, value);
		return hasher.Digest();
	}

	FieldAttributes StructAttributes(Ref<const StructInfo> info)
	{
		FieldAttributes attributes;
		attributes.Struct = std::move(info);
		return attributes;
	}

	std::string RegistrationError(TypeRegistry& registry, const DynamicComponentSchema& schema)
	{
		const Result<const ComponentInfo*> result = registry.RegisterDynamicComponent(schema);
		REQUIRE_FALSE(result.HasValue());
		return result.GetError().Message;
	}

	DynamicComponentSchema SingleField(DynamicFieldSchema field, std::string name = "Broken")
	{
		DynamicComponentSchema schema;
		schema.Name = std::move(name);
		schema.Fields.push_back(std::move(field));
		return schema;
	}
}

TEST_CASE("Reflection: structs are registered with their fields and defaults")
{
	TypeRegistry registry;
	RegisterTestTypes(registry);

	const Ref<const StructInfo> stats = registry.FindStruct("TestStats");
	const Ref<const StructInfo> layer = registry.FindStruct("TestLayer");
	REQUIRE(stats != nullptr);
	REQUIRE(layer != nullptr);
	CHECK(registry.FindStruct<TestStats>() == stats);
	CHECK(registry.FindStruct<TestLayer>() == layer);
	CHECK(registry.FindStruct("testlayer") == nullptr); // case-sensitive
	CHECK(registry.FindStruct("Missing") == nullptr);
	CHECK(registry.FindStruct<HealthComponent>() == nullptr);

	// Sorted by name, with the engine's own structs (the project settings, materials, texture import settings).
	std::vector<std::string> names;
	for (const StructInfo* info : registry.GetStructs())
	{
		names.push_back(info->GetName());
	}
	CHECK(
		names
		== std::vector<std::string>{ "ExportSettings", "Material", "ProjectDirectories", "ProjectSettings",
	                                 "RenderingSettings", "TestLayer", "TestStats", "TextureImportSettings" });
	CHECK(registry.GetStructs()[5] == layer.get());
	CHECK(registry.GetStructs()[6] == stats.get());

	CHECK(stats->GetName() == "TestStats");
	REQUIRE(stats->GetFields().size() == 3);
	CHECK(stats->GetFields()[0].Name == "Level");
	CHECK(stats->GetFields()[0].Attributes.Range == FieldRange{ 0.0, 99.0 });
	CHECK(stats->FindField("Speed") == &stats->GetFields()[1]);
	CHECK(stats->FindFieldIndex("Label") == size_t{ 2 });
	CHECK_FALSE(stats->FindFieldIndex("label").has_value());
	CHECK(stats->FindField("label") == nullptr);

	// A struct's default is the default of each field, in order.
	const StructValue& defaults = stats->GetDefault();
	REQUIRE(defaults.Values.size() == 3);
	CHECK(defaults.Values[0] == FieldValue(int32_t{ 3 }));
	CHECK(defaults.Values[1] == FieldValue(1.5f));
	CHECK(defaults.Values[2] == FieldValue(std::string("stat")));
	CHECK(MakeZeroValue(FieldType::Struct, FieldType::None, stats.get()) == FieldValue(defaults));

	// Fields of other structs, enums, colors, arrays and arrays of structs.
	CHECK(layer->FindField("Mode")->Type == FieldType::Enum);
	CHECK(layer->FindField("Tint")->Type == FieldType::Color3);
	CHECK(layer->FindField("Stats")->Type == FieldType::Struct);
	CHECK(layer->FindField("Stats")->Attributes.Struct == stats);
	CHECK(layer->FindField("Curve")->Type == FieldType::Array);
	CHECK(layer->FindField("Curve")->ElementType == FieldType::Float);
	CHECK(layer->FindField("History")->Type == FieldType::Array);
	CHECK(layer->FindField("History")->ElementType == FieldType::Struct);
	CHECK(layer->FindField("History")->Attributes.Struct == stats);
	CHECK(layer->FindField("Cache")->HasFlag(FieldFlags::NotHashed));
	CHECK(layer->FindField("Unreflected") == nullptr);
	const auto& nested = std::get<StructValue>(layer->GetDefault().Values[5]);
	CHECK(nested == stats->GetDefault());

	// The component that uses them.
	const ComponentInfo& structs = registry.GetComponent<StructsComponent>();
	CHECK(structs.FindField("Main")->Type == FieldType::Struct);
	CHECK(structs.FindField("Main")->Attributes.Struct == layer);
	CHECK(structs.FindField("Layers")->Type == FieldType::Array);
	CHECK(structs.FindField("Layers")->ElementType == FieldType::Struct);
	CHECK(structs.FindField("Source")->Type == FieldType::UUID);
	CHECK(structs.FindField("Sources")->Type == FieldType::Array);
	CHECK(structs.FindField("Sources")->ElementType == FieldType::UUID);
	CHECK(std::get<std::vector<StructValue>>(structs.FindField("Layers")->Default).empty());
	const auto& defaultStats = std::get<std::vector<StructValue>>(structs.FindField("Stats")->Default);
	REQUIRE(defaultStats.size() == 1);
	CHECK(defaultStats[0].Values[2] == FieldValue(std::string("nine")));

	// Registries are independent.
	TypeRegistry other;
	RegisterEngineTypes(other);
	CHECK(other.FindStruct("TestStats") == nullptr);
	CHECK(other.GetStructs().size() == 6);
}

TEST_CASE("Reflection: struct fields are read and written through FieldValue")
{
	TypeRegistry registry;
	RegisterTestTypes(registry);
	const ComponentInfo& info = registry.GetComponent<StructsComponent>();

	StructsComponent component;
	const StructsComponent defaults;
	for (const FieldInfo& field : info.GetFields())
	{
		CAPTURE(field.Name);
		CHECK(field.GetValue(&component) == field.Default);
		CHECK(field.CanHold(field.Default));

		const FieldValue changed = MakeDifferentValue(field, field.Default);
		CHECK(changed != field.Default);
		REQUIRE(field.SetValue(&component, changed));
		CHECK(field.GetValue(&component) == changed);
		CHECK(field.GetValue(&defaults) == field.Default); // another object is unaffected
	}
	CHECK(component.Main.Clip == "idlex");
	CHECK(component.Layers.size() == 1);
	CHECK(component.Layers[0].Clip == "idle"); // the default struct
	CHECK(component.Layers[0].Unreflected == 5);
	CHECK(component.Stats.size() == 2);
	CHECK(component.Source == UUID(0xBEF0));
	CHECK(component.Sources.size() == 3);
}

TEST_CASE("Reflection: writing a struct value changes the reflected members only")
{
	TypeRegistry registry;
	RegisterTestTypes(registry);
	const ComponentInfo& info = registry.GetComponent<StructsComponent>();
	const FieldInfo& main = *info.FindField("Main");
	const FieldInfo& layers = *info.FindField("Layers");

	StructsComponent component;
	component.Main.Unreflected = 42;
	component.Main.Cache = 9.0f;

	StructValue value = std::get<StructValue>(main.GetValue(&component));
	const StructInfo& layer = *main.Attributes.Struct;
	value.Values[*layer.FindFieldIndex("Clip")] = std::string("run");
	value.Values[*layer.FindFieldIndex("Weight")] = 0.25f;
	value.Values[*layer.FindFieldIndex("Mode")] = int64_t{ 7 };
	value.Values[*layer.FindFieldIndex("Curve")] = std::vector<float>{ 2.0f, 3.0f, 4.0f };
	std::get<StructValue>(value.Values[*layer.FindFieldIndex("Stats")]).Values[0] = int32_t{ 12 };
	REQUIRE(main.SetValue(&component, value));
	CHECK(component.Main.Clip == "run");
	CHECK(component.Main.Weight == 0.25f);
	CHECK(component.Main.Mode == TestMode::Fast);
	CHECK(component.Main.Curve == std::vector<float>{ 2.0f, 3.0f, 4.0f });
	CHECK(component.Main.Stats.Level == 12);
	CHECK(component.Main.Unreflected == 42); // not reflected: untouched
	CHECK(component.Main.Cache == 9.0f);     // NotHashed fields are still part of a struct value

	// Arrays of structs: growing default-constructs, shrinking drops, surviving elements keep unreflected members.
	REQUIRE(layers.SetValue(&component, std::vector<StructValue>{ layer.GetDefault(), layer.GetDefault() }));
	REQUIRE(component.Layers.size() == 2);
	component.Layers[0].Unreflected = 77;
	component.Layers[1].Unreflected = 78;
	std::vector<StructValue> shrunk{ layer.GetDefault() };
	std::get<std::string>(shrunk[0].Values[0]) = "only";
	REQUIRE(layers.SetValue(&component, shrunk));
	REQUIRE(component.Layers.size() == 1);
	CHECK(component.Layers[0].Clip == "only");
	CHECK(component.Layers[0].Unreflected == 77);
	REQUIRE(layers.SetValue(&component, std::vector<StructValue>{ shrunk[0], layer.GetDefault() }));
	CHECK(component.Layers[0].Unreflected == 77);
	CHECK(component.Layers[1].Unreflected == 5);
	REQUIRE(layers.SetValue(&component, std::vector<StructValue>{}));
	CHECK(component.Layers.empty());

	// The struct can be read and written as a whole object too.
	TestLayer object;
	object.Clip = "object";
	const StructValue read = layer.Read(&object);
	TestLayer copy;
	REQUIRE(layer.Write(&copy, read));
	CHECK(copy.Clip == "object");
	CHECK(layer.Read(&copy) == read);
}

TEST_CASE("Reflection: struct fields reject values that do not fit")
{
	TypeRegistry registry;
	RegisterTestTypes(registry);
	const ComponentInfo& info = registry.GetComponent<StructsComponent>();
	const FieldInfo& main = *info.FindField("Main");
	const FieldInfo& layers = *info.FindField("Layers");
	const FieldInfo& stats = *info.FindField("Stats");
	const StructValue good = std::get<StructValue>(main.Default);

	StructsComponent component;
	const StructsComponent untouched;
	const auto expectRejected = [&](const FieldInfo& field, const FieldValue& value) {
		CHECK_FALSE(field.CanHold(value));
		CHECK_FALSE(field.SetValue(&component, value));
	};

	expectRejected(main, 1.0f);                             // not a struct value
	expectRejected(main, std::vector<StructValue>{ good }); // an array where a struct is expected
	expectRejected(layers, good);                           // a struct where an array is expected
	StructValue tooFew = good;
	tooFew.Values.pop_back();
	expectRejected(main, tooFew);
	StructValue tooMany = good;
	tooMany.Values.push_back(1.0f);
	expectRejected(main, tooMany);
	StructValue wrongType = good;
	wrongType.Values[0] = 5.0f; // Clip is a string
	expectRejected(main, wrongType);
	StructValue badEnum = good;
	badEnum.Values[2] = int64_t{ 1 }; // no TestMode has the value 1
	expectRejected(main, badEnum);
	StructValue badNested = good;
	std::get<StructValue>(badNested.Values[5]).Values.clear(); // the nested stats lost their fields
	expectRejected(main, badNested);
	StructValue badArray = good;
	badArray.Values[7] = std::vector<StructValue>{ StructValue{} }; // an element of History that fits nothing
	expectRejected(main, badArray);
	expectRejected(stats, std::vector<StructValue>{ StructValue{} });
	expectRejected(layers, std::vector<StructValue>{ good, badEnum });

	CHECK(main.GetValue(&component) == main.GetValue(&untouched));
	CHECK(layers.GetValue(&component) == layers.GetValue(&untouched));
	CHECK(stats.GetValue(&component) == stats.GetValue(&untouched));
}

TEST_CASE("Reflection: structs are hashed in place exactly like their values")
{
	TypeRegistry registry;
	RegisterTestTypes(registry);
	const ComponentInfo& info = registry.GetComponent<StructsComponent>();
	StructsComponent component;
	component.Layers.resize(2);
	component.Layers[1].History.push_back(TestStats{ 1, 2.0f, "h" });

	for (const FieldInfo& field : info.GetFields())
	{
		CAPTURE(field.Name);
		CHECK(DigestOf(field, &component) == DigestOf(field, field.GetValue(&component)));
	}

	// Every hashed sub-field matters; the NotHashed one does not.
	const FieldInfo& main = *info.FindField("Main");
	const uint64_t original = DigestOf(main, &component);
	const auto changesHash = [&](auto&& mutate, bool expected) {
		StructsComponent changed = component;
		mutate(changed);
		CHECK((DigestOf(main, &changed) != original) == expected);
	};
	changesHash([](StructsComponent& c) { c.Main.Clip = "other"; }, true);
	changesHash([](StructsComponent& c) { c.Main.Mode = TestMode::Off; }, true);
	changesHash([](StructsComponent& c) { c.Main.Stats.Speed = 9.0f; }, true);
	changesHash([](StructsComponent& c) { c.Main.Curve.push_back(2.0f); }, true);
	changesHash([](StructsComponent& c) { c.Main.History.push_back(TestStats{}); }, true);
	changesHash([](StructsComponent& c) { c.Main.Cache = 5.0f; }, false);
	changesHash([](StructsComponent& c) { c.Main.Unreflected = 6; }, false);

	// Arrays hash their size, so [] and [default] differ, and so does the order of elements.
	const FieldInfo& layers = *info.FindField("Layers");
	StructsComponent empty;
	StructsComponent one;
	one.Layers.resize(1);
	CHECK(DigestOf(layers, &empty) != DigestOf(layers, &one));
	StructsComponent ordered;
	ordered.Layers.resize(2);
	ordered.Layers[0].Clip = "a";
	ordered.Layers[1].Clip = "b";
	StructsComponent reversed = ordered;
	std::swap(reversed.Layers[0], reversed.Layers[1]);
	CHECK(DigestOf(layers, &ordered) != DigestOf(layers, &reversed));
}

TEST_CASE("Reflection: structs in components take part in the world state hash and in copies")
{
	TestWorld fixture;
	World& world = *fixture.Scene;
	Entity entity = world.CreateEntityWithUUID(UUID(5), "Holder");
	const ComponentInfo& info = fixture.Registry.GetComponent<StructsComponent>();
	REQUIRE(info.Add(entity) != nullptr);
	const uint64_t before = world.ComputeStateHash();

	auto& component = entity.GetComponent<StructsComponent>();
	component.Main.Stats.Level = 50;
	const uint64_t afterNested = world.ComputeStateHash();
	CHECK(afterNested != before);
	component.Main.Cache = 3.0f; // NotHashed
	CHECK(world.ComputeStateHash() == afterNested);
	component.Layers.resize(1);
	CHECK(world.ComputeStateHash() != afterNested);

	const Scope<World> copy = World::Copy(world);
	CHECK(copy->ComputeStateHash() == world.ComputeStateHash());
	copy->TryGetEntity(UUID(5)).GetComponent<StructsComponent>().Layers[0].Weight = 0.5f;
	CHECK(copy->ComputeStateHash() != world.ComputeStateHash());
	CHECK(component.Layers[0].Weight == 1.0f);
}

TEST_CASE("Reflection: script components can hold structs")
{
	TypeRegistry registry;
	RegisterTestTypes(registry);
	const Ref<const StructInfo> stats = registry.FindStruct("TestStats");
	const Ref<const StructInfo> layer = registry.FindStruct("TestLayer");

	DynamicComponentSchema schema;
	schema.Name = "ScriptLayers";
	schema.Fields = {
		{ "Main", FieldType::Struct, FieldType::None, layer->GetDefault(), StructAttributes(layer) },
		{ "Stats", FieldType::Array, FieldType::Struct, std::vector<StructValue>{ stats->GetDefault() },
		  StructAttributes(stats) },
	};
	const Result<const ComponentInfo*> registered = registry.RegisterDynamicComponent(schema);
	REQUIRE(registered.HasValue());
	const ComponentInfo& script = **registered;
	CHECK(script.FindField("Main")->Type == FieldType::Struct);
	CHECK(script.FindField("Stats")->ElementType == FieldType::Struct);

	// Registering the same schema again is a reload; the same struct counts as the same.
	CHECK(registry.RegisterDynamicComponent(schema).Value() == &script);

	World world(registry);
	Entity entity = world.CreateEntityWithUUID(UUID(1), "Scripted");
	void* component = script.Add(entity);
	REQUIRE(component != nullptr);
	const FieldInfo& main = *script.FindField("Main");
	const FieldInfo& list = *script.FindField("Stats");
	CHECK(main.GetValue(component) == main.Default);
	CHECK(list.GetValue(component) == list.Default);

	StructValue changed = std::get<StructValue>(main.Default);
	changed.Values[0] = std::string("scripted");
	std::get<StructValue>(changed.Values[5]).Values[1] = 4.0f;
	REQUIRE(main.SetValue(component, changed));
	CHECK(main.GetValue(component) == FieldValue(changed));
	CHECK_FALSE(main.SetValue(component, StructValue{}));
	CHECK(list.SetValue(component, std::vector<StructValue>{ stats->GetDefault(), stats->GetDefault() }));
	CHECK(std::get<std::vector<StructValue>>(list.GetValue(component)).size() == 2);

	// The script field hashes like the same value in a C++ component.
	StructsComponent cpp;
	const ComponentInfo& cppInfo = registry.GetComponent<StructsComponent>();
	REQUIRE(cppInfo.FindField("Main")->SetValue(&cpp, changed));
	CHECK(DigestOf(main, component) == DigestOf(*cppInfo.FindField("Main"), &cpp));
	CHECK(DigestOf(list, component) != DigestOf(list, list.Default));

	// Copies carry the struct values.
	Entity other = world.CreateEntityWithUUID(UUID(2), "Copy");
	REQUIRE(script.CopyTo(entity, other));
	CHECK(main.GetValue(script.GetRaw(other)) == FieldValue(changed));
}

TEST_CASE("Reflection: invalid struct fields in script components are rejected with a reason")
{
	TypeRegistry registry;
	RegisterTestTypes(registry);
	const Ref<const StructInfo> stats = registry.FindStruct("TestStats");

	CHECK(
		RegistrationError(registry, SingleField({ "S", FieldType::Struct, FieldType::None, stats->GetDefault(), {} }))
			.find("has no struct")
		!= std::string::npos);
	CHECK(
		RegistrationError(
			registry, SingleField({ "S", FieldType::Struct, FieldType::None, StructValue{}, StructAttributes(stats) }))
			.find("does not fit the struct")
		!= std::string::npos);
	CHECK(
		RegistrationError(
			registry,
			SingleField({ "S", FieldType::Array, FieldType::Struct, std::vector<StructValue>{ StructValue{} },
	                      StructAttributes(stats) }))
			.find("does not fit the struct")
		!= std::string::npos);
	CHECK(
		RegistrationError(
			registry, SingleField({ "S", FieldType::Float, FieldType::None, 1.0f, StructAttributes(stats) }))
			.find("not a Struct field")
		!= std::string::npos);
	CHECK(
		RegistrationError(
			registry, SingleField({ "S", FieldType::Struct, FieldType::None, 1.0f, StructAttributes(stats) }))
			.find("default value")
		!= std::string::npos);

	// A struct that this registry does not know (another registry's, for example).
	TypeRegistry other;
	RegisterEngineTypes(other);
	struct Foreign
	{
		int32_t Value = 1;
	};
	other.RegisterStruct<Foreign>("Foreign").Field("Value", &Foreign::Value);
	const Ref<const StructInfo> foreign = other.FindStruct("Foreign");
	CHECK(
		RegistrationError(
			registry,
			SingleField({ "S", FieldType::Struct, FieldType::None, foreign->GetDefault(), StructAttributes(foreign) }))
			.find("another registry")
		!= std::string::npos);
	CHECK(registry.FindComponent("Broken") == nullptr);
}
