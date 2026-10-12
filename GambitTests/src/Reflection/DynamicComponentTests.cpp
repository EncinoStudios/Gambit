#include <doctest/doctest.h>

#include "Reflection/ReflectionTestTypes.h"

#include "Gambit/Core/Hash.h"
#include "Gambit/ECS/Entity.h"
#include "Gambit/ECS/World.h"
#include "Gambit/Reflection/DynamicComponent.h"
#include "Gambit/Reflection/TypeRegistry.h"

#include <string>
#include <vector>

using namespace Gambit;
using namespace Gambit::Tests;

namespace
{
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

TEST_CASE("Reflection: script components are registered from a schema")
{
	TypeRegistry registry;
	RegisterTestTypes(registry);
	const uint64_t generation = registry.GetGeneration();

	const Result<const ComponentInfo*> registered = registry.RegisterDynamicComponent(MakeScriptSchema());
	REQUIRE(registered.HasValue());
	const ComponentInfo& info = **registered;
	CHECK(registry.GetGeneration() != generation);
	CHECK(info.GetName() == "ScriptStats");
	CHECK(info.IsDynamic());
	CHECK_FALSE(info.IsTag());
	CHECK(info.GetFlags() == ComponentFlags::Default);
	CHECK(registry.FindComponent("ScriptStats") == &info);
	CHECK(registry.FindComponent(info.GetId()) == &info);
	REQUIRE(info.GetFields().size() == 14);
	CHECK(info.GetFields()[0].Name == "Speed");
	CHECK(info.FindField("Tint")->Type == FieldType::Color4);
	CHECK(info.FindField("Waypoints")->ElementType == FieldType::Vec3);
	CHECK(info.FindField("Icon")->Attributes.AssetType == "Texture");
	CHECK(info.FindField("Mode")->Attributes.Enum->FindValue("World") == 1);

	// Listed among the C++ components in name order.
	const auto components = registry.GetComponents();
	const auto position = std::ranges::find(components, &info);
	REQUIRE(position != components.end());
	CHECK((*(position - 1))->GetName() == "Relationship");
	CHECK((*(position + 1))->GetName() == "Structs");

	// Registering the same schema again (a script reload) returns the same type.
	const Result<const ComponentInfo*> again = registry.RegisterDynamicComponent(MakeScriptSchema());
	REQUIRE(again.HasValue());
	CHECK(*again == &info);
}

TEST_CASE("Reflection: script component instances live in the world")
{
	TypeRegistry registry;
	RegisterTestTypes(registry);
	const ComponentInfo& info = *registry.RegisterDynamicComponent(MakeScriptSchema()).Value();
	World world(registry);
	Entity entity = world.CreateEntity("Spinner");

	CHECK_FALSE(info.Has(entity));
	void* component = info.Add(entity);
	REQUIRE(component != nullptr);
	CHECK(info.Has(entity));
	CHECK(info.GetRaw(entity) == component);
	CHECK(info.Add(entity) == nullptr);

	// New instances start from the schema defaults.
	for (const FieldInfo& field : info.GetFields())
	{
		CAPTURE(field.Name);
		CHECK(field.GetValue(component) == field.Default);
	}
	auto& values = static_cast<DynamicComponent*>(component)->Values;
	REQUIRE(values.size() == info.GetFields().size());
	CHECK(std::get<float>(values[0]) == 1.5f);

	// Every field reads back what was written, and the raw pointer is the stored value itself.
	for (const FieldInfo& field : info.GetFields())
	{
		CAPTURE(field.Name);
		const FieldValue changed = MakeDifferentValue(field, field.GetValue(component));
		REQUIRE(field.SetValue(component, changed));
		CHECK(field.GetValue(component) == changed);
		if (field.Type != FieldType::String)
		{
			CHECK_FALSE(field.SetValue(component, std::string("wrong type")));
			CHECK(field.GetValue(component) == changed);
		}
	}
	auto* axis = static_cast<glm::vec3*>(info.FindField("Axis")->GetRawPointer(component));
	axis->x = 5.0f;
	CHECK(std::get<glm::vec3>(info.FindField("Axis")->GetValue(component)).x == 5.0f);
	CHECK(*static_cast<int64_t*>(info.FindField("Mode")->GetRawPointer(component)) == 0);
	CHECK_FALSE(info.FindField("Mode")->SetValue(component, int64_t{ 2 })); // not a value of the enum

	CHECK(info.SetFieldValue(entity, "Label", std::string("renamed")));
	CHECK(info.GetFieldValue(entity, "Label") == FieldValue(std::string("renamed")));

	// Copies are deep and independent.
	Entity other = world.CreateEntity("Other");
	CHECK(info.CopyTo(entity, other));
	CHECK(info.GetFieldValue(other, "Label") == FieldValue(std::string("renamed")));
	CHECK(info.SetFieldValue(other, "Label", std::string("other")));
	CHECK(info.GetFieldValue(entity, "Label") == FieldValue(std::string("renamed")));
	CHECK(info.CopyTo(entity, other)); // replaces
	CHECK(info.GetFieldValue(other, "Label") == FieldValue(std::string("renamed")));

	CHECK(info.Remove(entity));
	CHECK_FALSE(info.Has(entity));
	CHECK(info.Has(other));
}

TEST_CASE("Reflection: invalid script component schemas are rejected with a reason")
{
	TypeRegistry registry;
	RegisterTestTypes(registry);
	auto twoValues = CreateRef<EnumInfo>();
	twoValues->Entries = { { "A", 0 }, { "B", 1 } };

	CHECK(
		RegistrationError(registry, SingleField({ "Speed", FieldType::Float, FieldType::None, 1.0f, {} }, "1Bad"))
			.find("invalid component name")
		!= std::string::npos);
	CHECK(
		RegistrationError(registry, SingleField({ "bad name", FieldType::Float, FieldType::None, 1.0f, {} }))
			.find("invalid field name")
		!= std::string::npos);
	CHECK(
		RegistrationError(registry, SingleField({ "Speed", FieldType::None, FieldType::None, 1.0f, {} }))
			.find("has no type")
		!= std::string::npos);
	CHECK(
		RegistrationError(registry, SingleField({ "Speed", FieldType::Float, FieldType::None, 1.0, {} }))
			.find("default value")
		!= std::string::npos);
	CHECK(
		RegistrationError(
			registry, SingleField({ "List", FieldType::Array, FieldType::Enum, std::vector<int64_t>{}, {} }))
			.find("element type")
		!= std::string::npos);
	CHECK(
		RegistrationError(registry, SingleField({ "Speed", FieldType::Float, FieldType::Float, 1.0f, {} }))
			.find("element type")
		!= std::string::npos);
	CHECK(
		RegistrationError(registry, SingleField({ "Mode", FieldType::Enum, FieldType::None, int64_t{ 0 }, {} }))
			.find("no enum values")
		!= std::string::npos);
	CHECK(
		RegistrationError(
			registry,
			SingleField({ "Mode", FieldType::Enum, FieldType::None, int64_t{ 5 }, EnumAttributes(twoValues) }))
			.find("not one of its values")
		!= std::string::npos);
	auto clash = CreateRef<EnumInfo>();
	clash->Entries = { { "A", 0 }, { "B", 0 } };
	CHECK(
		RegistrationError(
			registry, SingleField({ "Mode", FieldType::Enum, FieldType::None, int64_t{ 0 }, EnumAttributes(clash) }))
			.find("clash")
		!= std::string::npos);
	CHECK(
		RegistrationError(
			registry, SingleField({ "Speed", FieldType::Float, FieldType::None, 1.0f, EnumAttributes(twoValues) }))
			.find("not an Enum field")
		!= std::string::npos);
	CHECK(
		RegistrationError(registry, SingleField({ "Icon", FieldType::AssetHandle, FieldType::None, UUID(), {} }))
			.find("asset type")
		!= std::string::npos);
	CHECK(
		RegistrationError(
			registry, SingleField({ "Who", FieldType::Entity, FieldType::None, UUID(), AssetAttributes("Mesh") }))
			.find("not an AssetHandle field")
		!= std::string::npos);
	CHECK(
		RegistrationError(
			registry,
			SingleField({ "Name", FieldType::String, FieldType::None, std::string(), RangeAttributes(0.0, 1.0) }))
			.find("cannot have a range")
		!= std::string::npos);
	CHECK(
		RegistrationError(
			registry, SingleField({ "Speed", FieldType::Float, FieldType::None, 1.0f, RangeAttributes(2.0, 1.0) }))
			.find("invalid range")
		!= std::string::npos);

	DynamicComponentSchema twice = SingleField({ "Speed", FieldType::Float, FieldType::None, 1.0f, {} });
	twice.Fields.push_back(twice.Fields.front());
	CHECK(RegistrationError(registry, twice).find("declared twice") != std::string::npos);

	// Names that are taken: by the engine, or by a script component with another schema.
	CHECK(
		RegistrationError(registry, SingleField({ "X", FieldType::Float, FieldType::None, 1.0f, {} }, "Transform"))
			.find("by the engine")
		!= std::string::npos);
	REQUIRE(registry.RegisterDynamicComponent(MakeScriptSchema()).HasValue());
	DynamicComponentSchema changed = MakeScriptSchema();
	std::get<float>(changed.Fields[0].Default) = 2.0f;
	CHECK(RegistrationError(registry, changed).find("different schema") != std::string::npos);

	// Nothing failed half-way: only the valid schema was added.
	CHECK(registry.FindComponent("Broken") == nullptr);
	CHECK(registry.GetComponents().size() == 14);
}

TEST_CASE("Reflection: unregistering a script component removes its instances from every world")
{
	TypeRegistry registry;
	RegisterTestTypes(registry);
	const ComponentInfo* info = *registry.RegisterDynamicComponent(MakeScriptSchema());
	const ComponentId id = info->GetId();
	World first(registry);
	World second(registry);
	Entity a = first.CreateEntity("A");
	Entity b = second.CreateEntity("B");
	REQUIRE(info->Add(a) != nullptr);
	REQUIRE(info->Add(b) != nullptr);
	first.GetCommandBuffer().AddComponent(a.GetUUID(), *info); // pending when the type goes away
	const uint64_t generation = registry.GetGeneration();

	CHECK_FALSE(registry.UnregisterDynamicComponent("Missing").HasValue());
	CHECK_FALSE(registry.UnregisterDynamicComponent("Transform").HasValue()); // C++ types stay
	REQUIRE(registry.UnregisterDynamicComponent("ScriptStats").HasValue());
	CHECK(registry.GetGeneration() != generation);
	CHECK(registry.FindComponent("ScriptStats") == nullptr);
	CHECK(registry.FindComponent(id) == nullptr);
	CHECK(first.GetRegistry().storage(id) == nullptr);
	CHECK(second.GetRegistry().storage(id) == nullptr);

	// The worlds keep working: hashes, copies and pending commands no longer see the type.
	const CommandBufferFlushResult flushed = first.GetCommandBuffer().Flush();
	CHECK(flushed.Executed == 0);
	CHECK(flushed.Skipped == 1);
	const Scope<World> copy = World::Copy(first);
	CHECK(copy->ComputeStateHash() == first.ComputeStateHash());

	// The name can be registered again, even with another schema, and starts empty.
	DynamicComponentSchema smaller =
		SingleField({ "Speed", FieldType::Float, FieldType::None, 4.0f, {} }, "ScriptStats");
	const Result<const ComponentInfo*> reregistered = registry.RegisterDynamicComponent(smaller);
	REQUIRE(reregistered.HasValue());
	CHECK((*reregistered)->GetId() == id); // same name, same storage id
	CHECK_FALSE((*reregistered)->Has(a));
	REQUIRE((*reregistered)->Add(a) != nullptr);
	CHECK((*reregistered)->GetFieldValue(a, "Speed") == FieldValue(4.0f));
}

TEST_CASE("Reflection: C++ and script fields of the same type hash alike")
{
	TypeRegistry registry;
	RegisterTestTypes(registry);
	const ComponentInfo& cpp = registry.GetComponent<AllTypesComponent>();

	// A script twin of AllTypes: same field names, types, attributes and defaults.
	DynamicComponentSchema schema;
	schema.Name = "ScriptAllTypes";
	for (const FieldInfo& field : cpp.GetFields())
	{
		schema.Fields.push_back({ field.Name, field.Type, field.ElementType, field.Default, field.Attributes });
	}
	const ComponentInfo& script = *registry.RegisterDynamicComponent(schema).Value();
	World world(registry);
	Entity entity = world.CreateEntity("Twin");
	entity.AddComponent<AllTypesComponent>();
	REQUIRE(script.Add(entity) != nullptr);

	const auto digest = [&](const ComponentInfo& info) {
		Hasher hasher;
		const void* component = info.GetRaw(entity);
		for (const FieldInfo& field : info.GetFields())
		{
			field.HashValue(hasher, component);
		}
		return hasher.Digest();
	};
	CHECK(digest(cpp) == digest(script));

	// Still alike after changing every field the same way, and different from before.
	const uint64_t before = digest(cpp);
	for (size_t index = 0; index < cpp.GetFields().size(); ++index)
	{
		const FieldInfo& cppField = cpp.GetFields()[index];
		const FieldValue changed = MakeDifferentValue(cppField, cppField.Default);
		REQUIRE(cppField.SetValue(cpp.GetRaw(entity), changed));
		REQUIRE(script.GetFields()[index].SetValue(script.GetRaw(entity), changed));
	}
	CHECK(digest(cpp) == digest(script));
	CHECK(digest(cpp) != before);
}
