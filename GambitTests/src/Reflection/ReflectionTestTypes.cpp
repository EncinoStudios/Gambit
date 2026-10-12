#include "Reflection/ReflectionTestTypes.h"

#include <doctest/doctest.h>

#include <type_traits>

namespace Gambit::Tests
{
	namespace
	{
		template<typename T>
		T Different(const T& value)
		{
			if constexpr (std::is_same_v<T, bool>)
			{
				return !value;
			}
			else if constexpr (std::is_arithmetic_v<T>)
			{
				return static_cast<T>(value + T(1));
			}
			else if constexpr (std::is_same_v<T, std::string>)
			{
				return value + "x";
			}
			else if constexpr (std::is_same_v<T, UUID>)
			{
				return UUID(value.GetValue() + 1);
			}
			else if constexpr (std::is_same_v<T, glm::quat>)
			{
				return glm::quat(value.w, value.x + 0.5f, value.y, value.z);
			}
			else
			{
				return value + T(1.0f); // glm vectors
			}
		}

		template<typename T>
		struct IsVector : std::false_type
		{};

		template<typename T>
		struct IsVector<std::vector<T>> : std::true_type
		{};
	}

	void RegisterTestTypes(TypeRegistry& registry)
	{
		RegisterEngineTypes(registry);
		registry.RegisterEnum<TestMode>("TestMode")
			.Value("Reverse", TestMode::Reverse)
			.Value("Off", TestMode::Off)
			.Value("Slow", TestMode::Slow)
			.Value("Fast", TestMode::Fast);

		using T = AllTypesComponent;
		registry.RegisterComponent<AllTypesComponent>("AllTypes")
			.Field("Bool", &T::Bool, Attr::Tooltip{ "a flag" }, Attr::Category{ "Scalars" })
			.Field("Int32", &T::Int32, Attr::Range{ -100.0, 100.0 })
			.Field("UInt32", &T::UInt32)
			.Field("Int64", &T::Int64)
			.Field("UInt64", &T::UInt64)
			.Field("Float", &T::Float, Attr::Range{ 0.0, 10.0 })
			.Field("Double", &T::Double)
			.Field("Vec2", &T::Vec2)
			.Field("Vec3", &T::Vec3)
			.Field("Vec4", &T::Vec4)
			.Field("Quat", &T::Quat)
			.Field("Color3", &T::Color3, Attr::Color{})
			.Field("Color4", &T::Color4, Attr::Color{})
			.Field("String", &T::String)
			.Field("Target", &T::Target)
			.Field("Texture", &T::Texture, Attr::AssetType{ "Texture" })
			.Field("Mode", &T::Mode)
			.Field("Bools", &T::Bools)
			.Field("Int32s", &T::Int32s)
			.Field("UInt32s", &T::UInt32s)
			.Field("Int64s", &T::Int64s)
			.Field("UInt64s", &T::UInt64s)
			.Field("Floats", &T::Floats)
			.Field("Doubles", &T::Doubles)
			.Field("Vec2s", &T::Vec2s)
			.Field("Vec3s", &T::Vec3s)
			.Field("Vec4s", &T::Vec4s)
			.Field("Quats", &T::Quats)
			.Field("Color3s", &T::Color3s, Attr::Color{})
			.Field("Color4s", &T::Color4s, Attr::Color{})
			.Field("Strings", &T::Strings)
			.Field("Targets", &T::Targets, Attr::Hidden{})
			.Field("Textures", &T::Textures, Attr::AssetType{ "Texture" }, Attr::ReadOnly{})
			.Field("Cache", &T::Cache, Attr::NotHashed{}, Attr::NotSerialized{});

		registry.RegisterComponent<HealthComponent>("Health")
			.Field("Current", &HealthComponent::Current)
			.Field("Maximum", &HealthComponent::Maximum);
		registry.RegisterComponent<PlayerTagComponent>("PlayerTag");
	}

	FieldAttributes EnumAttributes(Ref<const EnumInfo> info)
	{
		FieldAttributes attributes;
		attributes.Enum = std::move(info);
		return attributes;
	}

	FieldAttributes AssetAttributes(std::string assetType)
	{
		FieldAttributes attributes;
		attributes.AssetType = std::move(assetType);
		return attributes;
	}

	FieldAttributes RangeAttributes(double min, double max)
	{
		FieldAttributes attributes;
		attributes.Range = FieldRange{ min, max };
		return attributes;
	}

	FieldAttributes FlagAttributes(FieldFlags flags)
	{
		FieldAttributes attributes;
		attributes.Flags = flags;
		return attributes;
	}

	DynamicComponentSchema MakeScriptSchema(std::string name)
	{
		auto mode = CreateRef<EnumInfo>();
		mode->Entries = { { "Local", 0 }, { "World", 1 } };

		DynamicComponentSchema schema;
		schema.Name = std::move(name);
		schema.Fields = {
			{ "Speed", FieldType::Float, FieldType::None, 1.5f, {} },
			{ "Count", FieldType::Int32, FieldType::None, int32_t{ 3 }, {} },
			{ "Big", FieldType::UInt64, FieldType::None, uint64_t{ 9 }, {} },
			{ "Alive", FieldType::Bool, FieldType::None, true, {} },
			{ "Axis", FieldType::Vec3, FieldType::None, glm::vec3(0.0f, 1.0f, 0.0f), {} },
			{ "Tint", FieldType::Color4, FieldType::None, glm::vec4(1.0f), {} },
			{ "Facing", FieldType::Quat, FieldType::None, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), {} },
			{ "Label", FieldType::String, FieldType::None, std::string("spin"), {} },
			{ "Target", FieldType::Entity, FieldType::None, UUID(), {} },
			{ "Icon", FieldType::AssetHandle, FieldType::None, UUID(7), AssetAttributes("Texture") },
			{ "Mode", FieldType::Enum, FieldType::None, int64_t{ 1 }, EnumAttributes(mode) },
			{ "Weights", FieldType::Array, FieldType::Float, std::vector<float>{ 0.5f, 0.25f }, {} },
			{ "Waypoints", FieldType::Array, FieldType::Vec3, std::vector<glm::vec3>{ glm::vec3(1.0f) }, {} },
			{ "Debug", FieldType::Double, FieldType::None, 0.0,
			  FlagAttributes(FieldFlags::NotHashed | FieldFlags::NotSerialized) },
		};
		return schema;
	}

	FieldValue MakeDifferentValue(const FieldInfo& field, const FieldValue& current)
	{
		if (field.Type == FieldType::Enum)
		{
			const int64_t value = std::get<int64_t>(current);
			for (const EnumEntry& entry : field.Attributes.Enum->Entries)
			{
				if (entry.Value != value)
				{
					return entry.Value;
				}
			}
			FAIL("enum field '" << field.Name << "' has a single value");
		}
		return std::visit(
			[]<typename T>(const T& value) -> FieldValue {
				if constexpr (IsVector<T>::value)
				{
					T longer = value;
					longer.push_back(typename T::value_type{});
					return longer;
				}
				else
				{
					return Different(value);
				}
			},
			current);
	}
}
