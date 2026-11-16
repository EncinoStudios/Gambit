#include "gbpch.h"

#include "Gambit/Core/Utf8.h"
#include "Gambit/ECS/World.h"
#include "Gambit/Reflection/TypeRegistry.h"
#include "Gambit/Scripting/ApiRegistry.h"
#include "Gambit/Scripting/Bindings/BindingSupport.h"
#include "Gambit/Scripting/Bindings/CoreBindings.h"

#include <limits>

// Component proxies (D-041): `entity:Get("Transform")` returns a proxy that reads and writes the entity's component
// through the TypeRegistry, C++ and script components alike. Proxies hold a FieldBinding (entity UUID, component id,
// path), never a pointer, and resolve it on every access. Struct fields return struct proxies, arrays array proxies,
// vectors, quaternions and colors bound math values, so `transform.Translation.x = 5` writes through.

namespace Gambit
{
	namespace
	{
		// A proxy of a component (Depth 0), of a struct value or of an array value.
		struct ScriptComponentRef
		{
			Ref<const FieldBinding> Binding;
		};

		struct ScriptStructRef
		{
			Ref<const FieldBinding> Binding;
		};

		struct ScriptArrayRef
		{
			Ref<const FieldBinding> Binding;
		};

		// The type of the value at some point of a binding path, and its name for messages ("Spin.Axis",
		// "Path.Points[2]").
		struct ValueType
		{
			FieldType Type = FieldType::None;
			FieldType ElementType = FieldType::None;
			const FieldAttributes* Attributes = nullptr;
			std::string Name;
		};

		ValueType TypeOfField(const FieldInfo& field, std::string_view owner)
		{
			return ValueType{ field.Type, field.ElementType, &field.Attributes,
				              std::format("{}.{}", owner, field.Name) };
		}

		struct ResolvedComponent
		{
			Entity Target;
			const ComponentInfo* Info = nullptr;
		};

		ResolvedComponent ResolveComponent(const FieldBinding& binding)
		{
			ScriptEngine& engine = *binding.Engine;
			const ComponentInfo* info = engine.GetTypeRegistry().FindComponent(binding.Component);
			if (info == nullptr)
			{
				ThrowScriptError("the component type of this value is no longer registered");
			}
			World* world = engine.GetWorld();
			const Entity entity = world != nullptr ? world->TryGetEntity(binding.Entity) : Entity();
			if (!entity)
			{
				ThrowScriptError(
					"{} of entity {}: the entity no longer exists", info->GetName(), binding.Entity.ToString());
			}
			if (!info->Has(entity))
			{
				ThrowScriptError(
					"entity {} ('{}') no longer has a {} component", binding.Entity.ToString(), entity.GetName(),
					info->GetName());
			}
			return ResolvedComponent{ entity, info };
		}

		size_t ArraySize(const FieldValue& value)
		{
			return std::visit(
				[]<typename T>(const T& stored) -> size_t {
					if constexpr (Internal::IsStdVector<T>)
					{
						return stored.size();
					}
					else
					{
						return 0;
					}
				},
				value);
		}

		FieldValue GetElement(const FieldValue& array, size_t index)
		{
			return std::visit(
				[&]<typename T>(const T& stored) -> FieldValue {
					if constexpr (Internal::IsStdVector<T>)
					{
						using Element = typename T::value_type;
						return FieldValue(std::in_place_type<Element>, static_cast<Element>(stored[index]));
					}
					else
					{
						GB_CORE_VERIFY(false, "GetElement needs an array value");
						return FieldValue();
					}
				},
				array);
		}

		void SetElement(FieldValue& array, size_t index, const FieldValue& element)
		{
			std::visit(
				[&]<typename T>(T& stored) {
					if constexpr (Internal::IsStdVector<T>)
					{
						stored[index] = *std::get_if<typename T::value_type>(&element);
					}
					else
					{
						GB_CORE_VERIFY(false, "SetElement needs an array value");
					}
				},
				array);
		}

		// The type of the value one step below `type` (a struct field or an array element).
		ValueType StepType(const ValueType& type, uint32_t step)
		{
			if (type.Type == FieldType::Struct)
			{
				const FieldInfo& field = type.Attributes->Struct->GetFields()[step];
				return TypeOfField(field, type.Name);
			}
			GB_CORE_ASSERT(type.Type == FieldType::Array, "only structs and arrays have parts");
			return ValueType{ type.ElementType, FieldType::None, type.Attributes,
				              std::format("{}[{}]", type.Name, step + 1) };
		}

		// The value one step below `value`; throws when an array element is gone.
		FieldValue StepValue(const FieldValue& value, const ValueType& type, uint32_t step)
		{
			if (type.Type == FieldType::Struct)
			{
				return std::get_if<StructValue>(&value)->Values[step];
			}
			const size_t size = ArraySize(value);
			if (step >= size)
			{
				ThrowScriptError("{}[{}] no longer exists (the array has {} elements)", type.Name, step + 1, size);
			}
			return GetElement(value, step);
		}

		// Replaces the value at `steps` below `container` by `leaf`.
		void
		ReplaceAt(FieldValue& container, const ValueType& type, std::span<const uint32_t> steps, const FieldValue& leaf)
		{
			if (steps.empty())
			{
				container = leaf;
				return;
			}
			const uint32_t step = steps.front();
			const ValueType inner = StepType(type, step);
			if (type.Type == FieldType::Struct)
			{
				ReplaceAt(std::get_if<StructValue>(&container)->Values[step], inner, steps.subspan(1), leaf);
				return;
			}
			FieldValue element = StepValue(container, type, step);
			ReplaceAt(element, inner, steps.subspan(1), leaf);
			SetElement(container, step, element);
		}

		struct BoundField
		{
			ResolvedComponent Component;
			const FieldInfo* Field = nullptr;
			ValueType Type; // of the bound value
		};

		BoundField ResolveField(const FieldBinding& binding)
		{
			GB_CORE_ASSERT(binding.Depth >= 1, "a field binding needs a field");
			BoundField bound;
			bound.Component = ResolveComponent(binding);
			bound.Field = &bound.Component.Info->GetFields()[binding.Path[0]];
			bound.Type = TypeOfField(*bound.Field, bound.Component.Info->GetName());
			for (uint8_t level = 1; level < binding.Depth; ++level)
			{
				bound.Type = StepType(bound.Type, binding.Path[level]);
			}
			return bound;
		}

		[[noreturn]] void ThrowMismatch(const ValueType& type, std::string_view expected, const sol::object& object)
		{
			ThrowScriptError(
				"{}: expected {}, got {}", type.Name, expected, sol::type_name(object.lua_state(), object.get_type()));
		}

		// "nan", "inf" or "-inf": the same text on every platform.
		std::string_view DescribeNonFinite(double value) noexcept
		{
			return std::isnan(value) ? "nan" : value > 0.0 ? "inf" : "-inf";
		}

		double CheckFinite(const ValueType& type, double value)
		{
			if (!std::isfinite(value))
			{
				ThrowScriptError("{}: {} is not a finite number", type.Name, DescribeNonFinite(value));
			}
			return value;
		}

		template<typename V>
		V CheckFiniteVector(const ValueType& type, const V& value)
		{
			for (int index = 0; index < V::length(); ++index)
			{
				if (!std::isfinite(value[index]))
				{
					ThrowScriptError(
						"{}: component {} ({}) is not a finite number", type.Name, index + 1,
						DescribeNonFinite(static_cast<double>(value[index])));
				}
			}
			return value;
		}

		glm::quat CheckFiniteQuat(const ValueType& type, const glm::quat& value)
		{
			CheckFiniteVector(type, glm::vec4(value.x, value.y, value.z, value.w));
			return value;
		}

		// The integer a Lua number holds: an integer, or a float with an integral value in the range of int64. Nothing
		// for other values. The same in every build configuration (sol2 checks number precision only with its safeties
		// on).
		template<typename Object>
		std::optional<int64_t> ToInteger(const Object& object)
		{
			if (object.get_type() != sol::type::number)
			{
				return std::nullopt;
			}
			lua_State* lua = object.lua_state();
			object.push(lua);
			int isInteger = 0;
			const lua_Integer integer = lua_tointegerx(lua, -1, &isInteger);
			lua_pop(lua, 1);
			return isInteger != 0 ? std::optional<int64_t>(integer) : std::nullopt;
		}

		// An integer of the range of I: a Lua integer, or a float with an integral value.
		template<typename I>
		I CheckInteger(const ValueType& type, const sol::object& object)
		{
			if (object.get_type() != sol::type::number)
			{
				ThrowMismatch(type, "an integer", object);
			}
			lua_State* lua = object.lua_state();
			object.push(lua);
			const bool isInteger = lua_isinteger(lua, -1) != 0;
			const lua_Integer integer = lua_tointeger(lua, -1);
			const lua_Number number = lua_tonumber(lua, -1);
			lua_pop(lua, 1);
			if (isInteger)
			{
				if (std::in_range<I>(integer))
				{
					return static_cast<I>(integer);
				}
			}
			else
			{
				// 2^digits is the first value above the range and -2^digits the lowest value of a signed one, both
				// exact as doubles: a float in between converts without overflow.
				const double upper = std::ldexp(1.0, std::numeric_limits<I>::digits);
				const double lower = std::is_signed_v<I> ? -upper : 0.0;
				if (number >= lower && number < upper && number == std::floor(number))
				{
					return static_cast<I>(number);
				}
			}
			ThrowScriptError(
				"{}: {} is not an integer from {} to {}", type.Name,
				isInteger ? std::to_string(integer) : std::format("{}", number), std::numeric_limits<I>::min(),
				std::numeric_limits<I>::max());
		}

		UUID ParseUUIDText(const ValueType& type, std::string_view text)
		{
			const std::optional<UUID> uuid = UUID::Parse(text);
			if (!uuid.has_value())
			{
				ThrowScriptError("{}: '{}' is not a UUID (\"0x\" and 1 to 16 hex digits)", type.Name, text);
			}
			return *uuid;
		}

		FieldValue
		ToFieldValue(ScriptEngine& engine, const sol::object& object, const ValueType& type, const FieldValue* current);

		FieldValue
		ToStructValue(ScriptEngine& engine, const sol::object& object, const ValueType& type, const FieldValue* current)
		{
			const StructInfo& info = *type.Attributes->Struct;
			if (object.is<ScriptStructRef>())
			{
				const FieldBinding& source = *object.as<ScriptStructRef&>().Binding;
				const BoundField bound = ResolveField(source);
				if (bound.Type.Attributes->Struct != &info)
				{
					ThrowScriptError(
						"{}: expected a {} struct, got a {}", type.Name, info.GetName(),
						bound.Type.Attributes->Struct->GetName());
				}
				return ReadBinding(source);
			}
			if (object.get_type() != sol::type::table)
			{
				ThrowMismatch(type, std::format("a {} struct (a table of its fields)", info.GetName()), object);
			}
			StructValue value = current != nullptr ? *std::get_if<StructValue>(current) : info.GetDefault();
			const sol::table table = object.as<sol::table>();
			std::vector<std::pair<size_t, sol::object>> members;
			for (const auto& [key, member] : table)
			{
				if (key.get_type() != sol::type::string)
				{
					ThrowScriptError(
						"{}: struct fields are named by strings, got a {} key", type.Name,
						sol::type_name(key.lua_state(), key.get_type()));
				}
				const std::string name = key.as<std::string>();
				const std::optional<size_t> index = info.FindFieldIndex(name);
				if (!index.has_value())
				{
					ThrowScriptError("{}: the struct {} has no field '{}'", type.Name, info.GetName(), name);
				}
				members.emplace_back(*index, member);
			}
			std::ranges::sort(members, {}, &std::pair<size_t, sol::object>::first);
			for (const auto& [index, member] : members)
			{
				const FieldInfo& field = info.GetFields()[index];
				value.Values[index] = ToFieldValue(engine, member, TypeOfField(field, type.Name), &value.Values[index]);
			}
			return FieldValue(std::in_place_type<StructValue>, std::move(value));
		}

		FieldValue
		ToArrayValue(ScriptEngine& engine, const sol::object& object, const ValueType& type, const FieldValue* current)
		{
			if (object.is<ScriptArrayRef>())
			{
				const FieldBinding& source = *object.as<ScriptArrayRef&>().Binding;
				const BoundField bound = ResolveField(source);
				if (bound.Type.Type != FieldType::Array || bound.Type.ElementType != type.ElementType
				    || bound.Type.Attributes->Struct != type.Attributes->Struct)
				{
					ThrowScriptError(
						"{}: expected an array of {}, got another kind of array", type.Name,
						ToString(type.ElementType));
				}
				return ReadBinding(source);
			}
			if (object.get_type() != sol::type::table)
			{
				ThrowMismatch(type, std::format("an array of {} (a table)", ToString(type.ElementType)), object);
			}
			const sol::table table = object.as<sol::table>();
			const size_t count = table.size();
			for (const auto& [key, element] : table)
			{
				const std::optional<int64_t> position = ToInteger(key);
				if (!position.has_value() || *position < 1 || static_cast<size_t>(*position) > count)
				{
					ThrowScriptError("{}: an array is a sequence (keys 1 to n without holes)", type.Name);
				}
			}
			const size_t currentSize = current != nullptr ? ArraySize(*current) : 0;
			return Internal::VisitStorageType(type.ElementType, [&]<typename E>(std::type_identity<E>) {
				std::vector<E> elements;
				elements.reserve(count);
				for (size_t index = 0; index < count; ++index)
				{
					const ValueType elementType{ type.ElementType, FieldType::None, type.Attributes,
						                         std::format("{}[{}]", type.Name, index + 1) };
					// Struct elements merge into the element they replace (D-055).
					const std::optional<FieldValue> previous =
						index < currentSize ? std::optional<FieldValue>(GetElement(*current, index)) : std::nullopt;
					const FieldValue element = ToFieldValue(
						engine, table.raw_get<sol::object>(index + 1), elementType, previous ? &*previous : nullptr);
					elements.push_back(*std::get_if<E>(&element));
				}
				return FieldValue(std::in_place_type<std::vector<E>>, std::move(elements));
			});
		}

		// Converts a Lua value to the type of a field (or of part of one). `current` is the value it replaces: structs
		// merge into it.
		FieldValue
		ToFieldValue(ScriptEngine& engine, const sol::object& object, const ValueType& type, const FieldValue* current)
		{
			const sol::type kind = object.get_type();
			switch (type.Type)
			{
				case FieldType::Bool:
					if (kind != sol::type::boolean)
					{
						ThrowMismatch(type, "a boolean", object);
					}
					return FieldValue(object.as<bool>());
				case FieldType::Int32:
					return FieldValue(CheckInteger<int32_t>(type, object));
				case FieldType::UInt32:
					return FieldValue(CheckInteger<uint32_t>(type, object));
				case FieldType::Int64:
					return FieldValue(CheckInteger<int64_t>(type, object));
				case FieldType::UInt64:
					return FieldValue(CheckInteger<uint64_t>(type, object));
				case FieldType::Float:
				{
					if (kind != sol::type::number)
					{
						ThrowMismatch(type, "a number", object);
					}
					const double value = CheckFinite(type, object.as<double>());
					if (std::abs(value) > static_cast<double>(std::numeric_limits<float>::max()))
					{
						ThrowScriptError("{}: {} is too large for a Float", type.Name, value);
					}
					return FieldValue(static_cast<float>(value));
				}
				case FieldType::Double:
					if (kind != sol::type::number)
					{
						ThrowMismatch(type, "a number", object);
					}
					return FieldValue(CheckFinite(type, object.as<double>()));
				case FieldType::Vec2:
					if (!object.is<ScriptVec2>())
					{
						ThrowMismatch(type, "a Vec2", object);
					}
					return FieldValue(CheckFiniteVector(type, object.as<ScriptVec2&>().Get()));
				case FieldType::Vec3:
					if (!object.is<ScriptVec3>())
					{
						ThrowMismatch(type, "a Vec3", object);
					}
					return FieldValue(CheckFiniteVector(type, object.as<ScriptVec3&>().Get()));
				case FieldType::Vec4:
					if (!object.is<ScriptVec4>())
					{
						ThrowMismatch(type, "a Vec4", object);
					}
					return FieldValue(CheckFiniteVector(type, object.as<ScriptVec4&>().Get()));
				case FieldType::Quat:
					if (!object.is<ScriptQuat>())
					{
						ThrowMismatch(type, "a Quat", object);
					}
					return FieldValue(CheckFiniteQuat(type, object.as<ScriptQuat&>().Get()));
				case FieldType::Color3:
					if (object.is<ScriptColor>())
					{
						return FieldValue(CheckFiniteVector(type, glm::vec3(object.as<ScriptColor&>().Get())));
					}
					if (object.is<ScriptVec3>())
					{
						return FieldValue(CheckFiniteVector(type, object.as<ScriptVec3&>().Get()));
					}
					ThrowMismatch(type, "a Color", object);
				case FieldType::Color4:
					if (object.is<ScriptColor>())
					{
						return FieldValue(
							std::in_place_type<glm::vec4>, CheckFiniteVector(type, object.as<ScriptColor&>().Get()));
					}
					if (object.is<ScriptVec4>())
					{
						return FieldValue(
							std::in_place_type<glm::vec4>, CheckFiniteVector(type, object.as<ScriptVec4&>().Get()));
					}
					ThrowMismatch(type, "a Color", object);
				case FieldType::String:
				{
					if (kind != sol::type::string)
					{
						ThrowMismatch(type, "a string", object);
					}
					std::string text = object.as<std::string>();
					if (!IsValidUtf8(text))
					{
						ThrowScriptError("{}: the string is not valid UTF-8", type.Name);
					}
					return FieldValue(std::move(text));
				}
				case FieldType::Entity:
					if (kind == sol::type::lua_nil)
					{
						return FieldValue(UUID());
					}
					if (!object.is<ScriptEntity>())
					{
						ThrowMismatch(type, "an Entity or nil", object);
					}
					if (object.as<ScriptEntity&>().Engine != &engine)
					{
						ThrowScriptError("{}: the entity belongs to another world", type.Name);
					}
					return FieldValue(object.as<ScriptEntity&>().Id);
				case FieldType::AssetHandle:
				case FieldType::UUID:
					if (kind == sol::type::lua_nil)
					{
						return FieldValue(UUID());
					}
					if (kind != sol::type::string)
					{
						ThrowMismatch(type, "a UUID string (\"0x...\") or nil", object);
					}
					return FieldValue(ParseUUIDText(type, object.as<std::string>()));
				case FieldType::Enum:
				{
					const EnumInfo& info = *type.Attributes->Enum;
					if (kind == sol::type::string)
					{
						const std::string name = object.as<std::string>();
						if (const std::optional<int64_t> value = info.FindValue(name); value.has_value())
						{
							return FieldValue(std::in_place_type<int64_t>, *value);
						}
						std::string names;
						for (const EnumEntry& entry : info.Entries)
						{
							names += names.empty() ? entry.Name : ", " + entry.Name;
						}
						ThrowScriptError("{}: '{}' is not one of {}", type.Name, name, names);
					}
					ThrowMismatch(type, "the name of an enum value", object);
				}
				case FieldType::Struct:
					return ToStructValue(engine, object, type, current);
				case FieldType::Array:
					return ToArrayValue(engine, object, type, current);
				case FieldType::None:
					break;
			}
			ThrowScriptError("{}: a field of an unknown type", type.Name);
		}

		// Checks that every float in a value that a binding writes is finite (values from Lua conversions are checked
		// already; component assignments of math values are not).
		void CheckWrittenValue(const ValueType& type, const FieldValue& value)
		{
			std::visit(
				[&]<typename T>(const T& stored) {
					if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>)
					{
						CheckFinite(type, static_cast<double>(stored));
					}
					else if constexpr (
						std::is_same_v<T, glm::vec2> || std::is_same_v<T, glm::vec3> || std::is_same_v<T, glm::vec4>)
					{
						CheckFiniteVector(type, stored);
					}
					else if constexpr (std::is_same_v<T, glm::quat>)
					{
						CheckFiniteQuat(type, stored);
					}
				},
				value);
		}

		Ref<const FieldBinding> Extend(const FieldBinding& binding, uint32_t step)
		{
			if (binding.Depth >= FieldBinding::MaxDepth)
			{
				ThrowScriptError("values are nested deeper than {} levels", FieldBinding::MaxDepth);
			}
			auto extended = CreateRef<FieldBinding>(binding);
			extended->Path[extended->Depth++] = step;
			return extended;
		}

		std::string_view EnumName(const ValueType& type, int64_t value)
		{
			return type.Attributes->Enum != nullptr ? type.Attributes->Enum->FindName(value) : std::string_view();
		}

		// A Lua value for the value at `binding`: plain for scalars and strings, bound for math values, structs and
		// arrays.
		sol::object PushBound(
			sol::state_view lua, ScriptEngine& engine, const FieldValue& value, const ValueType& type,
			Ref<const FieldBinding> binding)
		{
			switch (type.Type)
			{
				case FieldType::Bool:
					return sol::make_object(lua, *std::get_if<bool>(&value));
				case FieldType::Int32:
					return sol::make_object(lua, static_cast<int64_t>(*std::get_if<int32_t>(&value)));
				case FieldType::UInt32:
					return sol::make_object(lua, static_cast<int64_t>(*std::get_if<uint32_t>(&value)));
				case FieldType::Int64:
					return sol::make_object(lua, *std::get_if<int64_t>(&value));
				case FieldType::UInt64:
				{
					const uint64_t stored = *std::get_if<uint64_t>(&value);
					if (stored <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
					{
						return sol::make_object(lua, static_cast<int64_t>(stored));
					}
					return sol::make_object(lua, static_cast<double>(stored));
				}
				case FieldType::Float:
					return sol::make_object(lua, static_cast<double>(*std::get_if<float>(&value)));
				case FieldType::Double:
					return sol::make_object(lua, *std::get_if<double>(&value));
				case FieldType::Vec2:
					return sol::make_object(lua, ScriptVec2{ *std::get_if<glm::vec2>(&value), std::move(binding) });
				case FieldType::Vec3:
					return sol::make_object(lua, ScriptVec3{ *std::get_if<glm::vec3>(&value), std::move(binding) });
				case FieldType::Vec4:
					return sol::make_object(lua, ScriptVec4{ *std::get_if<glm::vec4>(&value), std::move(binding) });
				case FieldType::Quat:
					return sol::make_object(lua, ScriptQuat{ *std::get_if<glm::quat>(&value), std::move(binding) });
				case FieldType::Color3:
					return sol::make_object(
						lua, ScriptColor{ glm::vec4(*std::get_if<glm::vec3>(&value), 1.0f), std::move(binding) });
				case FieldType::Color4:
					return sol::make_object(lua, ScriptColor{ *std::get_if<glm::vec4>(&value), std::move(binding) });
				case FieldType::String:
					return sol::make_object(lua, *std::get_if<std::string>(&value));
				case FieldType::Entity:
				{
					const UUID id = *std::get_if<UUID>(&value);
					return id.IsValid() ? Bindings::MakeEntityObject(lua, engine, id)
										: sol::make_object(lua, sol::lua_nil);
				}
				case FieldType::AssetHandle:
				{
					const UUID id = *std::get_if<UUID>(&value);
					return id.IsValid() ? sol::make_object(lua, id.ToString()) : sol::make_object(lua, sol::lua_nil);
				}
				case FieldType::UUID:
					return sol::make_object(lua, std::get_if<UUID>(&value)->ToString());
				case FieldType::Enum:
				{
					const int64_t stored = *std::get_if<int64_t>(&value);
					const std::string_view name = EnumName(type, stored);
					return name.empty() ? sol::make_object(lua, stored) : sol::make_object(lua, std::string(name));
				}
				case FieldType::Struct:
					return sol::make_object(lua, ScriptStructRef{ std::move(binding) });
				case FieldType::Array:
					return sol::make_object(lua, ScriptArrayRef{ std::move(binding) });
				case FieldType::None:
					break;
			}
			ThrowScriptError("{}: a field of an unknown type", type.Name);
		}

		std::string ListFields(std::span<const FieldInfo> fields)
		{
			std::string names;
			for (const FieldInfo& field : fields)
			{
				names += names.empty() ? field.Name : ", " + field.Name;
			}
			return names.empty() ? std::string("none") : names;
		}

		// The index of the field a key names (a sol::object or sol::stack_object).
		template<typename Key>
		size_t FindFieldOrThrow(std::span<const FieldInfo> fields, std::string_view owner, const Key& key)
		{
			if (key.get_type() != sol::type::string)
			{
				ThrowScriptError("{}: fields are named by strings", owner);
			}
			const std::string_view name = key.template as<std::string_view>();
			for (size_t index = 0; index < fields.size(); ++index)
			{
				if (fields[index].Name == name)
				{
					return index;
				}
			}
			ThrowScriptError("{} has no field '{}' (fields: {})", owner, name, ListFields(fields));
		}

		// ---- Proxy operations ----

		sol::object IndexComponent(const ScriptComponentRef& self, sol::stack_object key, sol::this_state lua)
		{
			const FieldBinding& binding = *self.Binding;
			const ResolvedComponent resolved = ResolveComponent(binding);
			const size_t index = FindFieldOrThrow(resolved.Info->GetFields(), resolved.Info->GetName(), key);
			const FieldInfo& field = resolved.Info->GetFields()[index];
			const void* raw = resolved.Info->GetRaw(*binding.Engine->GetWorld(), resolved.Target.GetHandle());
			return PushBound(
				lua, *binding.Engine, field.GetValue(raw), TypeOfField(field, resolved.Info->GetName()),
				Extend(binding, static_cast<uint32_t>(index)));
		}

		void AssignField(ScriptEngine& engine, const FieldBinding& binding, const sol::object& value)
		{
			const BoundField bound = ResolveField(binding);
			if (bound.Component.Info->HasFlag(ComponentFlags::Internal))
			{
				ThrowScriptError(
					"{}: the {} component is managed by the engine and cannot be written", bound.Type.Name,
					bound.Component.Info->GetName());
			}
			const FieldValue current = ReadBinding(binding);
			WriteBinding(binding, ToFieldValue(engine, value, bound.Type, &current));
		}

		void NewIndexComponent(const ScriptComponentRef& self, sol::stack_object key, sol::stack_object value)
		{
			const FieldBinding& binding = *self.Binding;
			const ResolvedComponent resolved = ResolveComponent(binding);
			const size_t index = FindFieldOrThrow(resolved.Info->GetFields(), resolved.Info->GetName(), key);
			AssignField(*binding.Engine, *Extend(binding, static_cast<uint32_t>(index)), value);
		}

		sol::object IndexStruct(const ScriptStructRef& self, sol::stack_object key, sol::this_state lua)
		{
			const FieldBinding& binding = *self.Binding;
			const BoundField bound = ResolveField(binding);
			const StructInfo& info = *bound.Type.Attributes->Struct;
			const size_t index = FindFieldOrThrow(info.GetFields(), bound.Type.Name, key);
			const FieldValue value = ReadBinding(binding);
			const FieldInfo& field = info.GetFields()[index];
			return PushBound(
				lua, *binding.Engine, std::get_if<StructValue>(&value)->Values[index],
				TypeOfField(field, bound.Type.Name), Extend(binding, static_cast<uint32_t>(index)));
		}

		void NewIndexStruct(const ScriptStructRef& self, sol::stack_object key, sol::stack_object value)
		{
			const FieldBinding& binding = *self.Binding;
			const BoundField bound = ResolveField(binding);
			const size_t index = FindFieldOrThrow(bound.Type.Attributes->Struct->GetFields(), bound.Type.Name, key);
			AssignField(*binding.Engine, *Extend(binding, static_cast<uint32_t>(index)), value);
		}

		// The 1-based index of an array access, as a 0-based one.
		size_t ArrayIndex(const ValueType& type, const sol::stack_object& key)
		{
			const std::optional<int64_t> index = ToInteger(key);
			if (!index.has_value())
			{
				ThrowScriptError("{}: arrays are indexed by integers from 1", type.Name);
			}
			if (*index < 1)
			{
				ThrowScriptError("{}: index {} is out of range (arrays count from 1)", type.Name, *index);
			}
			return static_cast<size_t>(*index - 1);
		}

		sol::object IndexArray(const ScriptArrayRef& self, sol::stack_object key, sol::this_state lua)
		{
			const FieldBinding& binding = *self.Binding;
			const BoundField bound = ResolveField(binding);
			const size_t index = ArrayIndex(bound.Type, key);
			const FieldValue value = ReadBinding(binding);
			if (index >= ArraySize(value))
			{
				return sol::make_object(lua, sol::lua_nil); // like a table: ipairs stops here
			}
			return PushBound(
				lua, *binding.Engine, GetElement(value, index), StepType(bound.Type, static_cast<uint32_t>(index)),
				Extend(binding, static_cast<uint32_t>(index)));
		}

		// arr[i] = v replaces element i; arr[#arr + 1] = v appends.
		void NewIndexArray(const ScriptArrayRef& self, sol::stack_object key, sol::stack_object value)
		{
			const FieldBinding& binding = *self.Binding;
			const BoundField bound = ResolveField(binding);
			if (bound.Component.Info->HasFlag(ComponentFlags::Internal))
			{
				ThrowScriptError(
					"{}: the {} component is managed by the engine and cannot be written", bound.Type.Name,
					bound.Component.Info->GetName());
			}
			const size_t index = ArrayIndex(bound.Type, key);
			FieldValue array = ReadBinding(binding);
			const size_t size = ArraySize(array);
			if (index > size)
			{
				ThrowScriptError(
					"{}: index {} is out of range (1 to {} to append)", bound.Type.Name, index + 1, size + 1);
			}
			const ValueType elementType = StepType(bound.Type, static_cast<uint32_t>(index));
			const std::optional<FieldValue> previous =
				index < size ? std::optional<FieldValue>(GetElement(array, index)) : std::nullopt;
			const FieldValue element =
				ToFieldValue(*binding.Engine, value, elementType, previous ? &*previous : nullptr);
			std::visit(
				[&]<typename T>(T& stored) {
					if constexpr (Internal::IsStdVector<T>)
					{
						const auto& converted = *std::get_if<typename T::value_type>(&element);
						if (index == size)
						{
							stored.push_back(converted);
						}
						else
						{
							stored[index] = converted;
						}
					}
				},
				array);
			WriteBinding(binding, array);
		}

		size_t ArrayLength(const ScriptArrayRef& self)
		{
			return ArraySize(ReadBinding(*self.Binding));
		}

		std::string DescribeBinding(const FieldBinding& binding)
		{
			const ComponentInfo* info = binding.Engine->GetTypeRegistry().FindComponent(binding.Component);
			return std::format(
				"{} of {}", info != nullptr ? info->GetName() : std::string("?"), binding.Entity.ToString());
		}

		void BindComponents(ScriptBindingContext& context)
		{
			sol::state_view& lua = context.GetLua();

			sol::usertype<ScriptComponentRef> component =
				lua.new_usertype<ScriptComponentRef>("ComponentProxy", sol::no_constructor);
			component[sol::meta_function::index] = &IndexComponent;
			component[sol::meta_function::new_index] = &NewIndexComponent;
			component[sol::meta_function::equal_to] = [](const ScriptComponentRef& a, const ScriptComponentRef& b) {
				return a.Binding->Entity == b.Binding->Entity && a.Binding->Component == b.Binding->Component;
			};
			component[sol::meta_function::to_string] = [](const ScriptComponentRef& self) {
				return std::format("Component({})", DescribeBinding(*self.Binding));
			};

			sol::usertype<ScriptStructRef> structure =
				lua.new_usertype<ScriptStructRef>("StructProxy", sol::no_constructor);
			structure[sol::meta_function::index] = &IndexStruct;
			structure[sol::meta_function::new_index] = &NewIndexStruct;
			structure[sol::meta_function::to_string] = [](const ScriptStructRef& self) {
				return std::format("Struct({})", ResolveField(*self.Binding).Type.Name);
			};

			sol::usertype<ScriptArrayRef> array = lua.new_usertype<ScriptArrayRef>("ArrayProxy", sol::no_constructor);
			array[sol::meta_function::index] = &IndexArray;
			array[sol::meta_function::new_index] = &NewIndexArray;
			array[sol::meta_function::length] = &ArrayLength;
			array[sol::meta_function::to_string] = [](const ScriptArrayRef& self) {
				const BoundField bound = ResolveField(*self.Binding);
				return std::format("Array({}, {} elements)", bound.Type.Name, ArraySize(ReadBinding(*self.Binding)));
			};
			// The proxies are reached only through components; they are not globals.
			lua.globals()["ComponentProxy"] = sol::lua_nil;
			lua.globals()["StructProxy"] = sol::lua_nil;
			lua.globals()["ArrayProxy"] = sol::lua_nil;
		}
	}

	FieldValue ReadBinding(const FieldBinding& binding)
	{
		const BoundField bound = ResolveField(binding);
		const void* raw = bound.Component.Info->GetRaw(*binding.Engine->GetWorld(), bound.Component.Target.GetHandle());
		FieldValue value = bound.Field->GetValue(raw);
		ValueType type = TypeOfField(*bound.Field, bound.Component.Info->GetName());
		for (uint8_t level = 1; level < binding.Depth; ++level)
		{
			value = StepValue(value, type, binding.Path[level]);
			type = StepType(type, binding.Path[level]);
		}
		return value;
	}

	void WriteBinding(const FieldBinding& binding, const FieldValue& value)
	{
		const BoundField bound = ResolveField(binding);
		void* raw = bound.Component.Info->GetRaw(bound.Component.Target);
		if (raw == nullptr)
		{
			ThrowScriptError(
				"{}: the {} component is managed by the engine and cannot be written", bound.Type.Name,
				bound.Component.Info->GetName());
		}
		CheckWrittenValue(bound.Type, value);
		FieldValue top = bound.Field->GetValue(raw);
		ReplaceAt(
			top, TypeOfField(*bound.Field, bound.Component.Info->GetName()),
			std::span<const uint32_t>(binding.Path.data() + 1, binding.Depth - 1u), value);
		if (!bound.Field->SetValue(raw, top))
		{
			ThrowScriptError("{}: the value does not fit the field", bound.Type.Name);
		}
	}

	namespace Bindings
	{
		const ComponentInfo& FindComponentType(ScriptEngine& engine, std::string_view name)
		{
			const ComponentInfo* info = engine.GetTypeRegistry().FindComponent(name);
			if (info == nullptr)
			{
				ThrowScriptError("there is no component type '{}'", name);
			}
			return *info;
		}

		sol::object
		MakeComponentProxy(sol::state_view lua, ScriptEngine& engine, UUID entity, const ComponentInfo& info)
		{
			auto binding = CreateRef<FieldBinding>();
			binding->Engine = &engine;
			binding->Entity = entity;
			binding->Component = info.GetId();
			return sol::make_object(lua, ScriptComponentRef{ std::move(binding) });
		}

		void
		AssignComponentFields(ScriptEngine& engine, Entity entity, const ComponentInfo& info, const sol::object& values)
		{
			if (values.get_type() == sol::type::lua_nil)
			{
				return;
			}
			if (values.get_type() != sol::type::table)
			{
				ThrowScriptError(
					"the values of a {} component must be a table of its fields, got {}", info.GetName(),
					sol::type_name(values.lua_state(), values.get_type()));
			}
			if (info.HasFlag(ComponentFlags::Internal))
			{
				ThrowScriptError("the {} component is managed by the engine and cannot be written", info.GetName());
			}
			void* raw = info.GetRaw(entity);
			GB_CORE_ASSERT(raw != nullptr, "AssignComponentFields needs the component");

			// Every value is converted before the first is written, so a bad one changes nothing.
			std::vector<std::pair<size_t, FieldValue>> converted;
			for (const auto& [key, value] : values.as<sol::table>())
			{
				const size_t index = FindFieldOrThrow(info.GetFields(), info.GetName(), key);
				const FieldInfo& field = info.GetFields()[index];
				const FieldValue current = field.GetValue(raw);
				converted.emplace_back(
					index, ToFieldValue(engine, value, TypeOfField(field, info.GetName()), &current));
			}
			std::ranges::sort(converted, {}, &std::pair<size_t, FieldValue>::first);
			for (const auto& [index, value] : converted)
			{
				[[maybe_unused]] const bool written = info.GetFields()[index].SetValue(raw, value);
				GB_CORE_ASSERT(written, "a converted value must fit its field");
			}
		}

		FieldValue ConvertToField(
			ScriptEngine& engine, const sol::object& value, FieldType type, FieldType elementType,
			const FieldAttributes& attributes, std::string_view name, const FieldValue* current)
		{
			return ToFieldValue(engine, value, ValueType{ type, elementType, &attributes, std::string(name) }, current);
		}

		void RegisterComponentApi(ApiRegistry& registry)
		{
			registry.RegisterBinder("Components", &BindComponents);
		}
	}
}
