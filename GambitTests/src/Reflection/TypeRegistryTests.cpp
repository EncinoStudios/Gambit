#include <doctest/doctest.h>

#include "Reflection/ReflectionTestTypes.h"

#include "Gambit/ECS/Entity.h"
#include "Gambit/ECS/World.h"
#include "Gambit/Reflection/TypeRegistry.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace Gambit;
using namespace Gambit::Tests;

namespace
{
	std::vector<std::string> Names(const TypeRegistry& registry)
	{
		std::vector<std::string> names;
		for (const ComponentInfo* info : registry.GetComponents())
		{
			names.push_back(info->GetName());
		}
		return names;
	}
}

TEST_CASE("Reflection: names are identifiers")
{
	CHECK(TypeRegistry::IsValidName("Transform"));
	CHECK(TypeRegistry::IsValidName("_private"));
	CHECK(TypeRegistry::IsValidName("Health2"));
	CHECK(TypeRegistry::IsValidName(std::string(TypeRegistry::MaxNameLength, 'a')));
	CHECK_FALSE(TypeRegistry::IsValidName(""));
	CHECK_FALSE(TypeRegistry::IsValidName("2D"));
	CHECK_FALSE(TypeRegistry::IsValidName("My Component"));
	CHECK_FALSE(TypeRegistry::IsValidName("Physics.Body"));
	CHECK_FALSE(TypeRegistry::IsValidName("Ünïcode"));
	CHECK_FALSE(TypeRegistry::IsValidName(std::string(TypeRegistry::MaxNameLength + 1, 'a')));
}

TEST_CASE("Reflection: components are found by name, id and type")
{
	TypeRegistry registry;
	CHECK(registry.GetComponents().empty());
	const uint64_t emptyGeneration = registry.GetGeneration();
	RegisterTestTypes(registry);
	CHECK(registry.GetGeneration() != emptyGeneration);

	const ComponentInfo* health = registry.FindComponent("Health");
	REQUIRE(health != nullptr);
	CHECK(health->GetName() == "Health");
	CHECK(registry.FindComponent(health->GetId()) == health);
	CHECK(registry.FindComponent<HealthComponent>() == health);
	CHECK(&registry.GetComponent<HealthComponent>() == health);
	CHECK_FALSE(health->IsDynamic());
	CHECK_FALSE(health->IsTag());
	CHECK(health->GetFlags() == ComponentFlags::Default);
	CHECK(health->HasFlag(ComponentFlags::Serializable));
	CHECK(health->HasFlag(ComponentFlags::EditorVisible));
	CHECK(health->HasFlag(ComponentFlags::Removable));
	CHECK_FALSE(health->HasFlag(ComponentFlags::Internal));
	REQUIRE(health->GetFields().size() == 2);
	CHECK(health->GetFields()[0].Name == "Current");
	CHECK(health->FindField("Maximum") == &health->GetFields()[1]);
	CHECK(health->FindField("maximum") == nullptr);

	CHECK(registry.FindComponent("health") == nullptr); // case-sensitive
	CHECK(registry.FindComponent("Missing") == nullptr);
	CHECK(registry.FindComponent<int>() == nullptr);
	CHECK(registry.FindComponent<PlayerTagComponent>()->IsTag());

	// Sorted by name, byte-wise: the order of hashing and of files.
	const std::vector<std::string> names = Names(registry);
	CHECK(std::ranges::is_sorted(names));
	CHECK(
		names
		== std::vector<std::string>{ "AllTypes", "Disabled", "Health", "ID", "Inactive", "PlayerTag", "Relationship",
	                                 "Tag", "Transform", "WorldTransform" });
}

TEST_CASE("Reflection: the process-wide registry has the core components")
{
	TypeRegistry& registry = TypeRegistry::Get();
	CHECK(&registry == &TypeRegistry::Get());
	for (const char* name : { "ID", "Tag", "Transform", "Relationship", "WorldTransform", "Disabled", "Inactive" })
	{
		CHECK_MESSAGE(registry.FindComponent(name) != nullptr, name);
	}

	const ComponentInfo& transform = registry.GetComponent<TransformComponent>();
	CHECK(transform.GetName() == "Transform");
	REQUIRE(transform.GetFields().size() == 3);
	CHECK(transform.GetFields()[0].Type == FieldType::Vec3);
	CHECK(transform.GetFields()[1].Type == FieldType::Quat);
	CHECK(std::get<glm::quat>(transform.GetFields()[1].Default) == glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
	CHECK(std::get<glm::vec3>(transform.GetFields()[2].Default) == glm::vec3(1.0f));

	const ComponentInfo& relationship = registry.GetComponent<RelationshipComponent>();
	CHECK(relationship.FindField("Parent")->Type == FieldType::Entity);
	CHECK(relationship.FindField("Children")->Type == FieldType::Array);
	CHECK(relationship.FindField("Children")->ElementType == FieldType::Entity);
	CHECK(relationship.FindField("Children")->HasFlag(FieldFlags::ReadOnly));
	CHECK(relationship.HasFlag(ComponentFlags::Serializable));
	CHECK(relationship.HasFlag(ComponentFlags::Internal));
	CHECK_FALSE(registry.GetComponent<IDComponent>().HasFlag(ComponentFlags::Serializable));
	CHECK_FALSE(registry.GetComponent<TagComponent>().HasFlag(ComponentFlags::Removable));
	CHECK(registry.GetComponent<DisabledComponent>().IsTag());
	CHECK(registry.GetComponent<WorldTransformComponent>().GetFields().empty());
}

TEST_CASE("Reflection: every field type is read and written through FieldValue")
{
	TypeRegistry registry;
	RegisterTestTypes(registry);
	const ComponentInfo& info = registry.GetComponent<AllTypesComponent>();

	const std::vector<std::pair<std::string, FieldType>> expected = {
		{ "Bool", FieldType::Bool },           { "Int32", FieldType::Int32 },   { "UInt32", FieldType::UInt32 },
		{ "Int64", FieldType::Int64 },         { "UInt64", FieldType::UInt64 }, { "Float", FieldType::Float },
		{ "Double", FieldType::Double },       { "Vec2", FieldType::Vec2 },     { "Vec3", FieldType::Vec3 },
		{ "Vec4", FieldType::Vec4 },           { "Quat", FieldType::Quat },     { "Color3", FieldType::Color3 },
		{ "Color4", FieldType::Color4 },       { "String", FieldType::String }, { "Target", FieldType::Entity },
		{ "Texture", FieldType::AssetHandle }, { "Mode", FieldType::Enum },
	};
	for (const auto& [name, type] : expected)
	{
		const FieldInfo* field = info.FindField(name);
		REQUIRE_MESSAGE(field != nullptr, name);
		CHECK_MESSAGE(field->Type == type, name);
		CHECK_MESSAGE(field->ElementType == FieldType::None, name);
	}
	const std::vector<std::pair<std::string, FieldType>> arrays = {
		{ "Bools", FieldType::Bool },           { "Int32s", FieldType::Int32 },   { "UInt32s", FieldType::UInt32 },
		{ "Int64s", FieldType::Int64 },         { "UInt64s", FieldType::UInt64 }, { "Floats", FieldType::Float },
		{ "Doubles", FieldType::Double },       { "Vec2s", FieldType::Vec2 },     { "Vec3s", FieldType::Vec3 },
		{ "Vec4s", FieldType::Vec4 },           { "Quats", FieldType::Quat },     { "Color3s", FieldType::Color3 },
		{ "Color4s", FieldType::Color4 },       { "Strings", FieldType::String }, { "Targets", FieldType::Entity },
		{ "Textures", FieldType::AssetHandle },
	};
	for (const auto& [name, element] : arrays)
	{
		const FieldInfo* field = info.FindField(name);
		REQUIRE_MESSAGE(field != nullptr, name);
		CHECK_MESSAGE(field->Type == FieldType::Array, name);
		CHECK_MESSAGE(field->ElementType == element, name);
	}

	AllTypesComponent component;
	const AllTypesComponent defaults;
	for (const FieldInfo& field : info.GetFields())
	{
		CAPTURE(field.Name);
		// Defaults come from a default-constructed component.
		CHECK(field.GetValue(&component) == field.Default);
		CHECK(field.CanHold(field.Default));

		const FieldValue changed = MakeDifferentValue(field, field.Default);
		REQUIRE(field.SetValue(&component, changed));
		CHECK(field.GetValue(&component) == changed);
		CHECK(field.GetValue(&defaults) == field.Default);

		// The raw pointer is the member itself (offset-based access into the struct).
		const auto* raw = static_cast<const std::byte*>(field.GetRawPointer(static_cast<const void*>(&component)));
		const auto* base = reinterpret_cast<const std::byte*>(&component);
		CHECK(raw >= base);
		CHECK(raw < base + sizeof(component));
	}
	CHECK(component.Bool == false);
	CHECK(component.Int32 == -6);
	CHECK(component.UInt64 == (uint64_t{ 1 } << 60) + 1);
	CHECK(component.Vec3 == glm::vec3(2.0f, 3.0f, 4.0f));
	CHECK(component.String == "hellox");
	CHECK(component.Target == UUID(0x1235));
	CHECK(component.Mode == TestMode::Reverse);
	CHECK(component.Bools == std::vector<bool>{ true, false, false });
	CHECK(component.Strings == std::vector<std::string>{ "a", "b", "" });

	// Raw access writes through.
	*static_cast<glm::vec3*>(info.FindField("Vec3")->GetRawPointer(&component)) = glm::vec3(9.0f);
	CHECK(component.Vec3 == glm::vec3(9.0f));
	*static_cast<std::vector<UUID>*>(info.FindField("Targets")->GetRawPointer(&component)) = { UUID(3) };
	CHECK(component.Targets == std::vector<UUID>{ UUID(3) });
}

TEST_CASE("Reflection: SetValue rejects values of the wrong type and unknown enum values")
{
	TypeRegistry registry;
	RegisterTestTypes(registry);
	const ComponentInfo& info = registry.GetComponent<AllTypesComponent>();
	AllTypesComponent component;

	CHECK_FALSE(info.FindField("Float")->SetValue(&component, 1.0));              // double, not float
	CHECK_FALSE(info.FindField("Int64")->SetValue(&component, int32_t{ 5 }));     // no implicit widening
	CHECK_FALSE(info.FindField("Color3")->SetValue(&component, glm::vec4(1.0f))); // a Color3 is a vec3
	CHECK_FALSE(info.FindField("String")->SetValue(&component, UUID(1)));
	CHECK_FALSE(info.FindField("Floats")->SetValue(&component, 1.0f));
	CHECK_FALSE(info.FindField("Floats")->SetValue(&component, std::vector<double>{ 1.0 }));
	CHECK(component.Float == 1.5f);
	CHECK(component.Floats == std::vector<float>{ 0.5f });

	const FieldInfo& mode = *info.FindField("Mode");
	CHECK_FALSE(mode.SetValue(&component, int64_t{ 1 })); // no TestMode has the value 1
	CHECK_FALSE(mode.SetValue(&component, int32_t{ 7 })); // enums take int64
	CHECK(component.Mode == TestMode::Slow);
	CHECK(mode.SetValue(&component, int64_t{ -2 }));
	CHECK(component.Mode == TestMode::Reverse);
	CHECK(std::get<int64_t>(mode.GetValue(&component)) == -2);
	CHECK(mode.SetValue(&component, int64_t{ 7 }));
	CHECK(component.Mode == TestMode::Fast);
}

TEST_CASE("Reflection: field attributes and enums")
{
	TypeRegistry registry;
	RegisterTestTypes(registry);
	const ComponentInfo& info = registry.GetComponent<AllTypesComponent>();

	const FieldInfo& flag = *info.FindField("Bool");
	CHECK(flag.Attributes.Tooltip == "a flag");
	CHECK(flag.Attributes.Category == "Scalars");
	CHECK(flag.Attributes.Flags == FieldFlags::None);
	CHECK_FALSE(flag.Attributes.Range.has_value());

	REQUIRE(info.FindField("Int32")->Attributes.Range.has_value());
	CHECK(info.FindField("Int32")->Attributes.Range->Min == -100.0);
	CHECK(info.FindField("Float")->Attributes.Range->Max == 10.0);
	CHECK(info.FindField("Texture")->Attributes.AssetType == "Texture");
	CHECK(info.FindField("Textures")->Attributes.AssetType == "Texture");
	CHECK(info.FindField("Textures")->HasFlag(FieldFlags::ReadOnly));
	CHECK(info.FindField("Targets")->HasFlag(FieldFlags::Hidden));
	CHECK(info.FindField("Targets")->Attributes.AssetType.empty());
	CHECK(info.FindField("Cache")->HasFlag(FieldFlags::NotHashed));
	CHECK(info.FindField("Cache")->HasFlag(FieldFlags::NotSerialized));
	CHECK_FALSE(info.FindField("Cache")->HasFlag(FieldFlags::Hidden));

	const Ref<const EnumInfo> mode = registry.FindEnum<TestMode>();
	REQUIRE(mode != nullptr);
	CHECK(registry.FindEnum("TestMode") == mode);
	CHECK(info.FindField("Mode")->Attributes.Enum == mode);
	CHECK(mode->Name == "TestMode");
	REQUIRE(mode->Entries.size() == 4);
	CHECK(mode->Entries[0] == EnumEntry{ "Reverse", -2 });
	CHECK(mode->FindValue("Fast") == 7);
	CHECK_FALSE(mode->FindValue("fast").has_value());
	CHECK(mode->FindName(3) == "Slow");
	CHECK(mode->FindName(1).empty());
	CHECK(mode->Contains(0));
	CHECK(registry.FindEnum("Missing") == nullptr);
}

TEST_CASE("Reflection: component operations work on a world")
{
	TypeRegistry registry;
	RegisterTestTypes(registry);
	World world(registry);
	const ComponentInfo& health = registry.GetComponent<HealthComponent>();
	const ComponentInfo& player = registry.GetComponent<PlayerTagComponent>();
	Entity first = world.CreateEntity("First");
	Entity second = world.CreateEntity("Second");

	CHECK(health.FindStorage(std::as_const(world)) == nullptr); // nothing stored yet
	CHECK_FALSE(health.Has(first));
	CHECK(health.GetRaw(first) == nullptr);

	auto* added = static_cast<HealthComponent*>(health.Add(first));
	REQUIRE(added != nullptr);
	CHECK(added->Current == 100.0f);
	CHECK(health.Has(first));
	CHECK(first.HasComponent<HealthComponent>());
	CHECK(health.GetRaw(first) == &first.GetComponent<HealthComponent>());
	CHECK(health.Add(first) == nullptr); // already there
	CHECK(health.FindStorage(std::as_const(world)) != nullptr);

	CHECK(health.SetFieldValue(first, "Current", 42.0f));
	CHECK(first.GetComponent<HealthComponent>().Current == 42.0f);
	CHECK(health.GetFieldValue(first, "Current") == FieldValue(42.0f));
	CHECK_FALSE(health.GetFieldValue(first, "Missing").has_value());
	CHECK_FALSE(health.GetFieldValue(second, "Current").has_value());
	CHECK_FALSE(health.SetFieldValue(first, "Current", 42.0));  // wrong type
	CHECK_FALSE(health.SetFieldValue(second, "Current", 1.0f)); // no component

	// CopyTo adds, then replaces in place.
	health.CopyTo(first, second);
	CHECK(second.GetComponent<HealthComponent>().Current == 42.0f);
	first.GetComponent<HealthComponent>().Current = 7.0f;
	health.CopyTo(first, second);
	CHECK(second.GetComponent<HealthComponent>().Current == 7.0f);

	// Tags: presence only.
	CHECK(player.Add(second) != nullptr);
	CHECK(player.Has(second));
	CHECK(player.GetRaw(second) != nullptr);
	CHECK(second.HasComponent<PlayerTagComponent>());
	player.CopyTo(second, first);
	CHECK(first.HasComponent<PlayerTagComponent>());

	std::vector<std::string> visited;
	health.ForEach(world, [&](Entity entity, void* component) {
		visited.push_back(entity.GetName());
		CHECK(component == &entity.GetComponent<HealthComponent>());
	});
	CHECK(visited == std::vector<std::string>{ "First", "Second" }); // storage order

	CHECK(health.Remove(first));
	CHECK_FALSE(health.Remove(first));
	CHECK_FALSE(first.HasComponent<HealthComponent>());

	// Engine-managed components are refused by the generic operations.
	const ComponentInfo& id = registry.GetComponent<IDComponent>();
	const ComponentInfo& tag = registry.GetComponent<TagComponent>();
	const ComponentInfo& inactive = registry.GetComponent<InactiveComponent>();
	CHECK_FALSE(id.Remove(first));
	CHECK_FALSE(tag.Remove(first)); // not Removable
	CHECK(inactive.Add(first) == nullptr);
	CHECK_FALSE(inactive.Has(first));
	CHECK(first.HasComponent<IDComponent>());
	CHECK(id.Has(first));
	CHECK(static_cast<const IDComponent*>(id.GetRaw(first))->ID == first.GetUUID());
}

TEST_CASE("Reflection: ForEach visits a snapshot and tolerates structural changes")
{
	TypeRegistry registry;
	RegisterTestTypes(registry);
	World world(registry);
	const ComponentInfo& health = registry.GetComponent<HealthComponent>();
	std::vector<Entity> entities;
	for (int i = 0; i < 5; ++i)
	{
		entities.push_back(world.CreateEntity(std::to_string(i)));
		entities.back().AddComponent<HealthComponent>();
	}

	std::vector<std::string> visited;
	health.ForEach(world, [&](Entity entity, void*) {
		visited.push_back(entity.GetName());
		if (entity == entities[0])
		{
			entities[2].RemoveComponent<HealthComponent>(); // skipped: gone before its turn
			world.DestroyEntity(entities[3]);               // skipped
			Entity late = world.CreateEntity("late");       // not in the snapshot
			late.AddComponent<HealthComponent>();
		}
	});
	CHECK(visited == std::vector<std::string>{ "0", "1", "4" });
}
