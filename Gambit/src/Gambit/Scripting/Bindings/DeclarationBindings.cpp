#include "gbpch.h"

#include "Gambit/Asset/AssetType.h"
#include "Gambit/Core/Utf8.h"
#include "Gambit/Reflection/TypeRegistry.h"
#include "Gambit/Scripting/ApiRegistry.h"
#include "Gambit/Scripting/Bindings/BindingSupport.h"
#include "Gambit/Scripting/Bindings/CoreBindings.h"

#include <limits>

// What scripts declare (D-041, D-023): Component(name, fields) registers a script component in the TypeRegistry,
// System(name, definition) and Test(name, definition) are recorded for the engine (systems run in the world, tests in
// the test runner). Declarations are only allowed while the scripts load.
//
// A field's type comes from its default (a number is a Float, a boolean a Bool, a string a String, a Vec2/Vec3/Vec4/
// Quat that type, a Color a Color4) or from a constructor: Int, UInt, Float, Double, Bool, String, Color3, Color4,
// EntityRef, Asset, Enum, Array. Fields are ordered by name, since Lua tables have no order.

namespace Gambit
{
	namespace
	{
		using Bindings::GetDeclarations;

		// A field type with its default, made by a constructor (Int(5), Enum({...}), ...).
		struct ScriptFieldSpec
		{
			FieldType Type = FieldType::None;
			FieldType ElementType = FieldType::None;
			FieldValue Default;
			FieldAttributes Attributes;
		};

		constexpr const char* TypeOfKey = "Gambit.FieldTypeOf"; // constructor -> type name, for Array(Float, ...)

		void CheckLoading(ScriptEngine& engine, std::string_view function)
		{
			if (engine.IsLoadingFinished())
			{
				ThrowScriptError(
					"{}: declarations are only allowed while the scripts load (at the top level of a script)",
					function);
			}
		}

		std::string_view DescribeLocation(const ScriptLocation& location, std::string& storage)
		{
			storage = location.File.empty() ? std::string("an unknown place")
											: std::format("{}:{}", location.File, location.Line);
			return storage;
		}

		ScriptFieldSpec MakeSpec(ScriptEngine& engine, FieldType type, const sol::object& value, std::string_view name)
		{
			ScriptFieldSpec spec;
			spec.Type = type;
			spec.Default = value.get_type() == sol::type::lua_nil
				? MakeZeroValue(type)
				: Bindings::ConvertToField(engine, value, type, FieldType::None, spec.Attributes, name);
			return spec;
		}

		ScriptFieldSpec MakeColorSpec(ScriptEngine& engine, bool alpha, const sol::variadic_args& args)
		{
			const std::string_view name = alpha ? "Color4" : "Color3";
			const size_t components = alpha ? 4 : 3;
			if (args.size() != 0 && args.size() != components && !(alpha && args.size() == 3))
			{
				ThrowScriptError(
					"{}: expected 0{} or {} numbers, got {}", name, alpha ? ", 3" : "", components, args.size());
			}
			glm::vec4 color(1.0f); // white by default
			for (size_t index = 0; index < args.size(); ++index)
			{
				const sol::object part = args[index];
				const FieldValue value = Bindings::ConvertToField(
					engine, part, FieldType::Float, FieldType::None, {},
					std::format("{} component {}", name, index + 1));
				color[static_cast<int>(index)] = *std::get_if<float>(&value);
			}
			ScriptFieldSpec spec;
			spec.Type = alpha ? FieldType::Color4 : FieldType::Color3;
			spec.Default = alpha ? FieldValue(color) : FieldValue(glm::vec3(color));
			return spec;
		}

		ScriptFieldSpec MakeAssetSpec(std::string_view assetType)
		{
			if (!ParseAssetType(assetType).has_value())
			{
				ThrowScriptError("Asset: '{}' is not an asset type (Texture, Mesh, Material, Prefab, ...)", assetType);
			}
			ScriptFieldSpec spec;
			spec.Type = FieldType::AssetHandle;
			spec.Default = FieldValue(UUID());
			spec.Attributes.AssetType = std::string(assetType);
			return spec;
		}

		ScriptFieldSpec MakeEnumSpec(const sol::table& names, sol::object defaultValue)
		{
			const std::optional<std::string> defaultName =
				GetOptionalArgument<std::string>(defaultValue, "Enum: default", "the name of a value");
			auto info = CreateRef<EnumInfo>();
			const size_t count = names.size();
			if (count == 0)
			{
				ThrowScriptError("Enum: expected a list of value names, such as Enum({{\"Idle\", \"Run\"}})");
			}
			for (size_t index = 1; index <= count; ++index)
			{
				const sol::object name = names.raw_get<sol::object>(index);
				if (name.get_type() != sol::type::string || !TypeRegistry::IsValidName(name.as<std::string_view>()))
				{
					ThrowScriptError("Enum: value {} is not an identifier", index);
				}
				const std::string text = name.as<std::string>();
				if (info->FindValue(text).has_value())
				{
					ThrowScriptError("Enum: the value '{}' is listed twice", text);
				}
				info->Entries.push_back(EnumEntry{ text, static_cast<int64_t>(index - 1) });
			}
			int64_t initial = 0;
			if (defaultName.has_value())
			{
				const std::optional<int64_t> value = info->FindValue(*defaultName);
				if (!value.has_value())
				{
					ThrowScriptError("Enum: the default '{}' is not one of the values", *defaultName);
				}
				initial = *value;
			}
			ScriptFieldSpec spec;
			spec.Type = FieldType::Enum;
			spec.Default = FieldValue(std::in_place_type<int64_t>, initial);
			spec.Attributes.Enum = std::move(info);
			return spec;
		}

		// The element type of Array(type, ...): a constructor (Float, Vec3, ...), its name ("Float", "Vec3", or a
		// FieldType name such as "Int64") or a spec (Asset("Texture"), Int()).
		ScriptFieldSpec ElementSpec(sol::state_view lua, const sol::object& type)
		{
			ScriptFieldSpec element;
			std::string name;
			if (type.is<ScriptFieldSpec>())
			{
				element = type.as<ScriptFieldSpec&>();
			}
			else
			{
				if (type.get_type() == sol::type::string)
				{
					name = type.as<std::string>();
				}
				else
				{
					const sol::object known = lua.registry().get<sol::table>(TypeOfKey).raw_get<sol::object>(type);
					if (known.get_type() != sol::type::string)
					{
						ThrowScriptError("Array: the element type must be a type constructor (Float, Vec3, ...), its "
						                 "name or a field spec");
					}
					name = known.as<std::string>();
				}
				static const std::map<std::string, FieldType, std::less<>> Aliases = { { "Int", FieldType::Int32 },
					                                                                   { "UInt", FieldType::UInt32 },
					                                                                   { "EntityRef",
					                                                                     FieldType::Entity },
					                                                                   { "Color", FieldType::Color4 } };
				const auto alias = Aliases.find(name);
				const std::optional<FieldType> parsed =
					alias != Aliases.end() ? std::optional<FieldType>(alias->second) : ParseFieldType(name);
				if (!parsed.has_value() || *parsed == FieldType::AssetHandle)
				{
					ThrowScriptError(
						"Array: '{}' is not an element type{}", name,
						parsed.has_value() ? " (use Asset(\"Type\") for assets)" : "");
				}
				element.Type = *parsed;
			}
			if (!IsArrayElementType(element.Type) || element.Type == FieldType::Struct)
			{
				ThrowScriptError("Array: {} cannot be an array element", ToString(element.Type));
			}
			return element;
		}

		ScriptFieldSpec
		MakeArraySpec(ScriptEngine& engine, sol::state_view lua, const sol::object& type, const sol::object& values)
		{
			const ScriptFieldSpec element = ElementSpec(lua, type);
			ScriptFieldSpec spec;
			spec.Type = FieldType::Array;
			spec.ElementType = element.Type;
			spec.Attributes.AssetType = element.Attributes.AssetType;
			spec.Default = values.get_type() == sol::type::lua_nil
				? MakeZeroValue(FieldType::Array, element.Type)
				: Bindings::ConvertToField(engine, values, FieldType::Array, element.Type, spec.Attributes, "Array");
			return spec;
		}

		// The field a default value or a constructor declares.
		ScriptFieldSpec FieldSpecOf(ScriptEngine& engine, const sol::object& value, const std::string& name)
		{
			if (value.is<ScriptFieldSpec>())
			{
				return value.as<ScriptFieldSpec&>();
			}
			const auto infer = [&](FieldType type) {
				ScriptFieldSpec spec;
				spec.Type = type;
				spec.Default = Bindings::ConvertToField(engine, value, type, FieldType::None, spec.Attributes, name);
				return spec;
			};
			switch (value.get_type())
			{
				case sol::type::number:
					return infer(FieldType::Float);
				case sol::type::boolean:
					return infer(FieldType::Bool);
				case sol::type::string:
					return infer(FieldType::String);
				default:
					break;
			}
			if (value.is<ScriptVec2>())
			{
				return infer(FieldType::Vec2);
			}
			if (value.is<ScriptVec3>())
			{
				return infer(FieldType::Vec3);
			}
			if (value.is<ScriptVec4>())
			{
				return infer(FieldType::Vec4);
			}
			if (value.is<ScriptQuat>())
			{
				return infer(FieldType::Quat);
			}
			if (value.is<ScriptColor>())
			{
				return infer(FieldType::Color4);
			}
			if (value.get_type() == sol::type::table)
			{
				ThrowScriptError("{}: a table is not a field type: declare lists with Array(type, {{ ... }})", name);
			}
			ThrowScriptError(
				"{}: cannot declare a field from a {} (use a default value or a type such as Int(), EntityRef(), "
				"Asset(\"Texture\"))",
				name, sol::type_name(value.lua_state(), value.get_type()));
		}

		void DeclareComponent(std::string_view name, sol::table fields, sol::this_state lua)
		{
			ScriptEngine& engine = GetScriptEngine(lua);
			CheckLoading(engine, "Component");
			Bindings::ScriptDeclarations& declarations = GetDeclarations(engine);
			for (const ScriptComponentDeclaration& existing : declarations.Components)
			{
				if (existing.Name == name)
				{
					std::string where;
					ThrowScriptError(
						"Component: '{}' is declared twice (first at {})", name,
						DescribeLocation(existing.Location, where));
				}
			}

			DynamicComponentSchema schema;
			schema.Name = std::string(name);
			for (const auto& [key, value] : fields)
			{
				if (key.get_type() != sol::type::string)
				{
					ThrowScriptError("Component '{}': fields are named by strings", name);
				}
				const std::string fieldName = key.as<std::string>();
				ScriptFieldSpec spec = FieldSpecOf(engine, value, std::format("{}.{}", name, fieldName));
				schema.Fields.push_back(DynamicFieldSchema{ fieldName, spec.Type, spec.ElementType,
				                                            std::move(spec.Default), std::move(spec.Attributes) });
			}
			std::ranges::sort(schema.Fields, {}, &DynamicFieldSchema::Name);

			Result<const ComponentInfo*> registered = engine.GetTypeRegistry().RegisterDynamicComponent(schema);
			if (!registered)
			{
				ThrowScriptError("Component: {}", registered.GetError().Message);
			}
			declarations.Components.push_back(ScriptComponentDeclaration{ schema.Name, GetCallerLocation(lua) });
		}

		constexpr std::array<std::string_view, 6> SystemKeys = { "Phase",    "Order",     "OnCreate",
			                                                     "OnUpdate", "OnDestroy", "RunInEditMode" };
		constexpr std::array<std::string_view, 5> TestKeys = { "Run", "Scene", "Tags", "Timeout", "Seed" };

		template<size_t N>
		void
		CheckKeys(const sol::table& definition, const std::array<std::string_view, N>& allowed, std::string_view what)
		{
			for (const auto& [key, value] : definition)
			{
				const bool known = key.get_type() == sol::type::string
					&& std::ranges::find(allowed, key.as<std::string_view>()) != allowed.end();
				if (!known)
				{
					std::string list;
					for (const std::string_view name : allowed)
					{
						list += list.empty() ? std::string(name) : ", " + std::string(name);
					}
					ThrowScriptError(
						"{}: unknown key '{}' (known keys: {})", what,
						key.get_type() == sol::type::string ? key.as<std::string>() : std::string("?"), list);
				}
			}
		}

		sol::function OptionalFunction(const sol::table& definition, std::string_view key, std::string_view what)
		{
			const sol::object value = definition.raw_get<sol::object>(key);
			if (value.get_type() == sol::type::lua_nil)
			{
				return sol::function();
			}
			if (value.get_type() != sol::type::function)
			{
				ThrowScriptError("{}: {} must be a function", what, key);
			}
			return value.as<sol::function>();
		}

		std::string PhaseNames()
		{
			std::string names;
			for (size_t index = 0; index < SystemPhaseCount; ++index)
			{
				const std::string_view name = ToString(static_cast<SystemPhase>(index));
				names += names.empty() ? std::string(name) : ", " + std::string(name);
			}
			return names;
		}

		void DeclareSystem(std::string_view name, sol::table definition, sol::this_state lua)
		{
			ScriptEngine& engine = GetScriptEngine(lua);
			CheckLoading(engine, "System");
			const std::string what = std::format("System '{}'", name);
			if (!TypeRegistry::IsValidName(name))
			{
				ThrowScriptError("System: '{}' is not a valid name (an identifier)", name);
			}
			Bindings::ScriptDeclarations& declarations = GetDeclarations(engine);
			for (const ScriptSystemDeclaration& existing : declarations.Systems)
			{
				if (existing.Name == name)
				{
					std::string where;
					ThrowScriptError(
						"System: '{}' is declared twice (first at {})", name,
						DescribeLocation(existing.Location, where));
				}
			}
			CheckKeys(definition, SystemKeys, what);

			ScriptSystemDeclaration declaration;
			declaration.Name = std::string(name);
			const sol::object phase = definition.raw_get<sol::object>("Phase");
			if (phase.get_type() != sol::type::lua_nil)
			{
				const std::optional<SystemPhase> parsed = phase.get_type() == sol::type::string
					? ParseSystemPhase(phase.as<std::string_view>())
					: std::nullopt;
				if (!parsed.has_value() || *parsed == SystemPhase::Count)
				{
					ThrowScriptError("{}: Phase must be one of {}", what, PhaseNames());
				}
				declaration.Phase = *parsed;
			}
			const sol::object order = definition.raw_get<sol::object>("Order");
			if (order.get_type() != sol::type::lua_nil)
			{
				const FieldValue value =
					Bindings::ConvertToField(engine, order, FieldType::Int32, FieldType::None, {}, what + " Order");
				declaration.Order = *std::get_if<int32_t>(&value);
			}
			const sol::object editMode = definition.raw_get<sol::object>("RunInEditMode");
			if (editMode.get_type() != sol::type::lua_nil)
			{
				if (editMode.get_type() != sol::type::boolean)
				{
					ThrowScriptError("{}: RunInEditMode must be a boolean", what);
				}
				declaration.RunInEditMode = editMode.as<bool>();
			}
			Bindings::ScriptSystemFunctions functions;
			functions.OnCreate = OptionalFunction(definition, "OnCreate", what);
			functions.OnUpdate = OptionalFunction(definition, "OnUpdate", what);
			functions.OnDestroy = OptionalFunction(definition, "OnDestroy", what);
			if (!functions.OnUpdate.valid())
			{
				ThrowScriptError("{}: OnUpdate (a function of world and dt) is required", what);
			}
			declaration.Location = GetCallerLocation(lua);
			declarations.Systems.push_back(std::move(declaration));
			declarations.SystemFunctions.push_back(std::move(functions));
		}

		void DeclareTest(std::string_view name, sol::table definition, sol::this_state lua)
		{
			ScriptEngine& engine = GetScriptEngine(lua);
			CheckLoading(engine, "Test");
			if (name.empty() || !IsValidUtf8(name))
			{
				ThrowScriptError("Test: the name must be a non-empty UTF-8 string");
			}
			const std::string what = std::format("Test '{}'", name);
			Bindings::ScriptDeclarations& declarations = GetDeclarations(engine);
			for (const ScriptTestDeclaration& existing : declarations.Tests)
			{
				if (existing.Name == name)
				{
					std::string where;
					ThrowScriptError(
						"Test: '{}' is declared twice (first at {})", name, DescribeLocation(existing.Location, where));
				}
			}
			CheckKeys(definition, TestKeys, what);
			if (definition.raw_get<sol::object>("Run").get_type() != sol::type::function)
			{
				ThrowScriptError("{}: Run (a function of the test context) is required", what);
			}

			ScriptTestDeclaration declaration;
			declaration.Name = std::string(name);
			const sol::object scene = definition.raw_get<sol::object>("Scene");
			if (scene.get_type() != sol::type::lua_nil)
			{
				if (scene.get_type() != sol::type::string)
				{
					ThrowScriptError("{}: Scene must be the path of a scene", what);
				}
				declaration.Scene = scene.as<std::string>();
			}
			const sol::object tags = definition.raw_get<sol::object>("Tags");
			if (tags.get_type() != sol::type::lua_nil)
			{
				const FieldValue value =
					Bindings::ConvertToField(engine, tags, FieldType::Array, FieldType::String, {}, what + " Tags");
				declaration.Tags = *std::get_if<std::vector<std::string>>(&value);
			}
			for (const auto& [key, target] :
			     { std::pair{ "Timeout", &declaration.Timeout }, std::pair{ "Seed", &declaration.Seed } })
			{
				const sol::object value = definition.raw_get<sol::object>(key);
				if (value.get_type() != sol::type::lua_nil)
				{
					const FieldValue number = Bindings::ConvertToField(
						engine, value, FieldType::UInt64, FieldType::None, {}, std::format("{} {}", what, key));
					*target = *std::get_if<uint64_t>(&number);
				}
			}
			if (declaration.Timeout == uint64_t{ 0 })
			{
				ThrowScriptError("{}: Timeout must be at least 1 tick", what);
			}
			declaration.Location = GetCallerLocation(lua);
			declarations.Tests.push_back(std::move(declaration));
			declarations.TestDefinitions.push_back(std::move(definition));
		}

		void BindDeclarations(ScriptBindingContext& context)
		{
			sol::state_view& lua = context.GetLua();
			ScriptEngine& engine = context.GetEngine();

			lua.new_usertype<ScriptFieldSpec>(
				"FieldSpec", sol::no_constructor, sol::meta_function::to_string,
				[](const ScriptFieldSpec& self) { return std::format("FieldSpec({})", ToString(self.Type)); });
			lua.globals()["FieldSpec"] = sol::lua_nil; // made by the constructors only

			lua.set_function("Component", &DeclareComponent);
			lua.set_function("System", &DeclareSystem);
			lua.set_function("Test", &DeclareTest);

			const auto scalar = [&engine](FieldType type, std::string_view name) {
				// An absent argument is a nil object.
				return [&engine, type, name](sol::object value) { return MakeSpec(engine, type, value, name); };
			};
			lua.set_function("Int", scalar(FieldType::Int32, "Int"));
			lua.set_function("UInt", scalar(FieldType::UInt32, "UInt"));
			lua.set_function("Float", scalar(FieldType::Float, "Float"));
			lua.set_function("Double", scalar(FieldType::Double, "Double"));
			lua.set_function("Bool", scalar(FieldType::Bool, "Bool"));
			lua.set_function("String", scalar(FieldType::String, "String"));
			lua.set_function(
				"Color3", [&engine](sol::variadic_args args) { return MakeColorSpec(engine, false, args); });
			lua.set_function(
				"Color4", [&engine](sol::variadic_args args) { return MakeColorSpec(engine, true, args); });
			lua.set_function("EntityRef", []() {
				ScriptFieldSpec spec;
				spec.Type = FieldType::Entity;
				spec.Default = FieldValue(UUID());
				return spec;
			});
			lua.set_function("Asset", &MakeAssetSpec);
			lua.set_function("Enum", &MakeEnumSpec);
			lua.set_function("Array", [&engine](sol::object type, sol::object values, sol::this_state state) {
				return MakeArraySpec(engine, state, type, values);
			});

			// Array(Float, ...) names the element type by its constructor.
			sol::table typeOf = lua.create_table();
			for (const char* name : { "Int", "UInt", "Float", "Double", "Bool", "String", "Color3", "Color4",
			                          "EntityRef", "Vec2", "Vec3", "Vec4", "Quat", "Color" })
			{
				typeOf[lua.globals().raw_get<sol::object>(name)] = name;
			}
			lua.registry()[TypeOfKey] = typeOf;
		}

		void DocumentDeclarations(ApiRegistry& registry)
		{
			registry.Document(
				"Component", ApiEntryKind::Function, "Component(name: string, fields: table)",
				"Declares a script component (D-023): a data-only component whose fields (ordered by name) take their "
				"type from their default (number: Float, boolean: Bool, string: String, Vec2/Vec3/Vec4/Quat, Color: "
				"Color4) or from a type constructor. Script components are saved, inspected, hashed and copied like "
				"C++ "
				"ones. Only at the top level of a script.",
				"Component(\"Spin\", { Speed = 1.0, Axis = Vec3(0, 1, 0), Count = Int(0), Mode = Enum({ \"Local\", "
				"\"World\" }) })");
			registry.Document(
				"System", ApiEntryKind::Function, "System(name: string, definition: table)",
				"Declares a system that runs every tick (D-024): { Phase = \"Update\" (any phase name), Order = 0, "
				"OnCreate = function(world), OnUpdate = function(world, dt) (required), OnDestroy = function(world), "
				"RunInEditMode = false }. Systems run by phase, then order, then name, with the C++ systems. A system "
				"that raises an error is disabled for the session.",
				"System(\"SpinSystem\", { OnUpdate = function(world, dt) end })");
			registry.Document(
				"Test", ApiEntryKind::Function, "Test(name: string, definition: table)",
				"Declares a test that `gambit test` runs: { Run = function(t) (required), Scene = \"Scenes/X.gscene\", "
				"Tags = { ... }, Timeout = ticks, Seed = n }.",
				"Test(\"Spin rotates\", { Run = function(t) t:Tick(60) end })");
			registry.Document(
				"Int", ApiEntryKind::Function, "Int(default?: integer) -> FieldSpec",
				"A 32-bit integer field (default 0).", "Count = Int(3)");
			registry.Document(
				"UInt", ApiEntryKind::Function, "UInt(default?: integer) -> FieldSpec",
				"A 32-bit unsigned integer field (default 0).", "Ammo = UInt(30)");
			registry.Document(
				"Float", ApiEntryKind::Function, "Float(default?: number) -> FieldSpec",
				"A 32-bit float field (default 0); a plain number default declares one too.", "Speed = Float(2.5)");
			registry.Document(
				"Double", ApiEntryKind::Function, "Double(default?: number) -> FieldSpec",
				"A 64-bit float field (default 0).", "Elapsed = Double()");
			registry.Document(
				"Bool", ApiEntryKind::Function, "Bool(default?: boolean) -> FieldSpec",
				"A boolean field (default false).", "Open = Bool()");
			registry.Document(
				"String", ApiEntryKind::Function, "String(default?: string) -> FieldSpec",
				"A string field (default \"\").", "Label = String(\"Door\")");
			registry.Document(
				"Color3", ApiEntryKind::Function, "Color3(r?: number, g?: number, b?: number) -> FieldSpec",
				"A linear RGB color field (default white); its value is a Color whose alpha is always 1.",
				"Tint = Color3(1, 0.5, 0)");
			registry.Document(
				"Color4", ApiEntryKind::Function, "Color4(r?: number, g?: number, b?: number, a?: number) -> FieldSpec",
				"A linear RGBA color field (default white).", "Glow = Color4(1, 1, 0, 0.5)");
			registry.Document(
				"EntityRef", ApiEntryKind::Function, "EntityRef() -> FieldSpec",
				"A reference to an entity (default nil); copies and prefab instances remap it.",
				"Target = EntityRef()");
			registry.Document(
				"Asset", ApiEntryKind::Function, "Asset(type: string) -> FieldSpec",
				"A reference to an asset of the type (Texture, Mesh, Material, Prefab, ...); the value is the handle "
				"as "
				"\"0x...\", nil for none.",
				"Icon = Asset(\"Texture\")");
			registry.Document(
				"Enum", ApiEntryKind::Function, "Enum(names: string[], default?: string) -> FieldSpec",
				"A field holding one of the names (default: the first); read and written as the name.",
				"Mode = Enum({ \"Idle\", \"Run\" }, \"Idle\")");
			registry.Document(
				"Array", ApiEntryKind::Function, "Array(type: any, default?: table) -> FieldSpec",
				"A list field of an element type: a constructor (Float, Vec3, ...), its name or a spec such as "
				"Asset(\"Texture\") (not Enum or Array). Read as a live array proxy (#, [i], [#a + 1] = v appends); a "
				"table replaces it whole.",
				"Waypoints = Array(Vec3, { Vec3(0, 0, 0) })");
			registry.Document(
				"FieldSpec", ApiEntryKind::Type, "FieldSpec",
				"A field type with its default, made by Int, Float, Enum, Array and the other constructors for "
				"Component.",
				"Count = Int(0)");
		}
	}

	namespace Bindings
	{
		void RegisterDeclarationApi(ApiRegistry& registry)
		{
			registry.RegisterBinder("Declarations", &BindDeclarations);
			DocumentDeclarations(registry);
		}
	}
}
