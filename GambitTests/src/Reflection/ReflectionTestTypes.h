#pragma once

#include "Gambit/Core/UUID.h"
#include "Gambit/Math/Math.h"
#include "Gambit/Reflection/TypeRegistry.h"

#include <cstdint>
#include <string>
#include <vector>

// Test-only component types shared by the Reflection and ECS tests.

namespace Gambit::Tests
{
	enum class TestMode : int16_t
	{
		Reverse = -2,
		Off = 0,
		Slow = 3,
		Fast = 7
	};

	// One field of every FieldType and one array of every element type, all with non-zero defaults where possible.
	struct AllTypesComponent
	{
		bool Bool = true;
		int32_t Int32 = -7;
		uint32_t UInt32 = 7;
		int64_t Int64 = -(int64_t{ 1 } << 40);
		uint64_t UInt64 = uint64_t{ 1 } << 60;
		float Float = 1.5f;
		double Double = 2.25;
		glm::vec2 Vec2{ 1.0f, 2.0f };
		glm::vec3 Vec3{ 1.0f, 2.0f, 3.0f };
		glm::vec4 Vec4{ 1.0f, 2.0f, 3.0f, 4.0f };
		glm::quat Quat{ 1.0f, 0.0f, 0.0f, 0.0f };
		glm::vec3 Color3{ 0.5f, 0.25f, 1.0f };
		glm::vec4 Color4{ 0.5f, 0.25f, 1.0f, 0.75f };
		std::string String = "hello";
		UUID Target{ 0x1234 };
		UUID Texture{ 0xABCD };
		TestMode Mode = TestMode::Slow;
		std::vector<bool> Bools{ true, false };
		std::vector<int32_t> Int32s{ 1, -2 };
		std::vector<uint32_t> UInt32s{ 3 };
		std::vector<int64_t> Int64s{ -4 };
		std::vector<uint64_t> UInt64s{ 4, 5 };
		std::vector<float> Floats{ 0.5f };
		std::vector<double> Doubles{ 0.25 };
		std::vector<glm::vec2> Vec2s{ glm::vec2(1.0f, 2.0f) };
		std::vector<glm::vec3> Vec3s{ glm::vec3(3.0f) };
		std::vector<glm::vec4> Vec4s{ glm::vec4(4.0f) };
		std::vector<glm::quat> Quats{ glm::quat(1.0f, 0.0f, 0.0f, 0.0f) };
		std::vector<glm::vec3> Color3s{ glm::vec3(0.1f) };
		std::vector<glm::vec4> Color4s{ glm::vec4(0.2f) };
		std::vector<std::string> Strings{ "a", "b" };
		std::vector<UUID> Targets{ UUID(1) };
		std::vector<UUID> Textures{ UUID(2) };
		float Cache = 0.0f; // NotHashed, NotSerialized
	};

	// Reflected structs: a leaf, and one that nests it, an enum, a color, an array and an array of structs.
	struct TestStats
	{
		int32_t Level = 3;
		float Speed = 1.5f;
		std::string Label = "stat";
	};

	struct TestLayer
	{
		std::string Clip = "idle";
		float Weight = 1.0f;
		TestMode Mode = TestMode::Slow;
		glm::vec3 Tint{ 1.0f, 0.5f, 0.25f };
		UUID Target{ 0x77 };
		TestStats Stats;
		std::vector<float> Curve{ 0.0f, 1.0f };
		std::vector<TestStats> History;
		float Cache = 0.0f;      // NotHashed, NotSerialized
		int32_t Unreflected = 5; // not registered: keeps its value when a struct value is written
	};

	// A struct, an array of structs of the nested kind, and a plain identifier.
	struct StructsComponent
	{
		TestLayer Main;
		std::vector<TestLayer> Layers;
		std::vector<TestStats> Stats{ TestStats{ 9, 2.0f, "nine" } };
		UUID Source{ 0xBEEF };
		std::vector<UUID> Sources{ UUID(1), UUID(2) };
	};

	// A plain gameplay-like component.
	struct HealthComponent
	{
		float Current = 100.0f;
		float Maximum = 100.0f;
	};

	// A tag.
	struct PlayerTagComponent
	{};

	// RegisterEngineTypes plus "AllTypes", "Structs", "Health", "PlayerTag", the structs "TestStats" and "TestLayer"
	// and the enum "TestMode".
	void RegisterTestTypes(TypeRegistry& registry);

	// Attributes with a single property set.
	[[nodiscard]] FieldAttributes EnumAttributes(Ref<const EnumInfo> info);
	[[nodiscard]] FieldAttributes AssetAttributes(std::string assetType);
	[[nodiscard]] FieldAttributes RangeAttributes(double min, double max);
	[[nodiscard]] FieldAttributes FlagAttributes(FieldFlags flags);

	// A schema with one field of every non-array type and a few arrays (script component "ScriptStats").
	[[nodiscard]] DynamicComponentSchema MakeScriptSchema(std::string name = "ScriptStats");

	// A value of the field's type that differs from `current` (another enum value for enums).
	[[nodiscard]] FieldValue MakeDifferentValue(const FieldInfo& field, const FieldValue& current);
}
