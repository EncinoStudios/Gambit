#include "gbpch.h"

#include "Gambit/Automation/BuiltinCommands.h"
#include "Gambit/Automation/Commands/CommandSupport.h"
#include "Gambit/Automation/UndoOperations.h"
#include "Gambit/Serialization/FieldSerializer.h"

namespace Gambit::Commands
{
	namespace
	{
		using namespace CommandSupport;

		const std::string EntityHelp = "UUID (0x...) or the unique name of the entity";

		struct Target
		{
			SceneDocument* Scene = nullptr;
			Entity Subject;
			const ComponentInfo* Component = nullptr;
		};

		// The entity and component type that a command's 'entity' and 'component' arguments name.
		Result<Target> ResolveTarget(EditorContext& context, const Json& arguments, bool editable)
		{
			Target target;
			target.Scene = context.GetActiveScene();
			World& world = *target.Scene->Scene;
			Result<Entity> entity = ResolveEntity(world, GetString(arguments, "entity"));
			if (!entity)
			{
				return entity.GetError();
			}
			target.Subject = *entity;
			const std::string name = GetString(arguments, "component");
			Result<const ComponentInfo*> info = editable ? FindEditableComponentType(world.GetTypeRegistry(), name)
														 : FindComponentType(world.GetTypeRegistry(), name);
			if (!info)
			{
				return info.GetError();
			}
			target.Component = *info;
			return target;
		}

		Error MissingComponent(World& world, Entity entity, const ComponentInfo& info)
		{
			return MakeCommandError(
				CommandErrorCode::NotFound,
				std::format(
					"entity '{}' has no component '{}'; it has: {}", entity.GetName(), info.GetName(),
					JoinNames(GetComponentNames(world, entity))));
		}

		CommandResult DescribeComponent(World& world, const Target& target)
		{
			const void* raw = target.Component->GetRaw(world, target.Subject.GetHandle());
			Result<Json> values = ComponentJson(*target.Component, raw);
			if (!values)
			{
				return Fail(values.GetError());
			}
			Json data = Json::object();
			data["entity"] = target.Subject.GetUUID().ToString();
			data["component"] = target.Component->GetName();
			data["values"] = std::move(*values);
			return CommandResult::Success(std::move(data));
		}

		CommandResult ComponentAdd(EditorContext& context, const Json& arguments)
		{
			Result<Target> target = ResolveTarget(context, arguments, true);
			if (!target)
			{
				return Fail(target.GetError());
			}
			World& world = *target->Scene->Scene;
			if (target->Component->Has(world, target->Subject.GetHandle()))
			{
				return Fail(
					CommandErrorCode::AlreadyExists,
					std::format(
						"entity '{}' has the component '{}' already (component.set changes it)",
						target->Subject.GetName(), target->Component->GetName()));
			}
			Result<std::vector<FieldData>> fields = DecodeFieldEdits(
				*target->Component, nullptr, Has(arguments, "values") ? arguments.at("values") : Json::object());
			if (!fields)
			{
				return Fail(fields.GetError());
			}
			if (Result<void> done = context.Do(CreateScope<ComponentPresenceOperation>(
					target->Scene->Path, target->Subject.GetUUID(), target->Component->GetName(),
					ComponentPresenceOperation::Mode::Add, std::move(*fields),
					std::format("Add {} to '{}'", target->Component->GetName(), target->Subject.GetName())));
			    !done)
			{
				return Fail(done.GetError());
			}
			CommandResult result = DescribeComponent(world, *target);
			if (result.Ok)
			{
				result.Message =
					std::format("Added {} to '{}'", target->Component->GetName(), target->Subject.GetName());
			}
			return result;
		}

		CommandResult ComponentRemove(EditorContext& context, const Json& arguments)
		{
			Result<Target> target = ResolveTarget(context, arguments, true);
			if (!target)
			{
				return Fail(target.GetError());
			}
			World& world = *target->Scene->Scene;
			if (!target->Component->Has(world, target->Subject.GetHandle()))
			{
				return Fail(MissingComponent(world, target->Subject, *target->Component));
			}
			if (!target->Component->HasFlag(ComponentFlags::Removable))
			{
				return Fail(
					CommandErrorCode::Conflict,
					std::format("the component '{}' cannot be removed", target->Component->GetName()));
			}
			const std::string message =
				std::format("Removed {} from '{}'", target->Component->GetName(), target->Subject.GetName());
			if (Result<void> done = context.Do(CreateScope<ComponentPresenceOperation>(
					target->Scene->Path, target->Subject.GetUUID(), target->Component->GetName(),
					ComponentPresenceOperation::Mode::Remove, std::vector<FieldData>{}, message));
			    !done)
			{
				return Fail(done.GetError());
			}
			Json data = Json::object();
			data["entity"] = target->Subject.GetUUID().ToString();
			data["component"] = target->Component->GetName();
			return CommandResult::Success(std::move(data), message);
		}

		CommandResult ComponentGet(EditorContext& context, const Json& arguments)
		{
			Result<Target> target = ResolveTarget(context, arguments, false);
			if (!target)
			{
				return Fail(target.GetError());
			}
			World& world = *target->Scene->Scene;
			if (!target->Component->Has(world, target->Subject.GetHandle()))
			{
				return Fail(MissingComponent(world, target->Subject, *target->Component));
			}
			return DescribeComponent(world, *target);
		}

		CommandResult ComponentSet(EditorContext& context, const Json& arguments)
		{
			Result<Target> target = ResolveTarget(context, arguments, true);
			if (!target)
			{
				return Fail(target.GetError());
			}
			World& world = *target->Scene->Scene;
			const void* current = target->Component->GetRaw(world, target->Subject.GetHandle());
			if (current == nullptr)
			{
				return Fail(MissingComponent(world, target->Subject, *target->Component));
			}
			const Json& values = arguments.at("values");
			if (values.empty())
			{
				return Fail(CommandErrorCode::InvalidArguments, "'values' is empty: name the fields to set");
			}
			Result<std::vector<FieldData>> edits = DecodeFieldEdits(*target->Component, current, values);
			if (!edits)
			{
				return Fail(edits.GetError());
			}

			// Only what really changes is recorded: setting a field to the value it has leaves no undo entry.
			std::vector<FieldData> changes;
			Json changed = Json::array();
			for (FieldData& edit : *edits)
			{
				if (edit.Value != target->Component->FindField(edit.Name)->GetValue(current))
				{
					changed.push_back(edit.Name);
					changes.push_back(std::move(edit));
				}
			}
			std::string message = std::format("{} already has these values", target->Component->GetName());
			if (!changes.empty())
			{
				message = std::format(
					"Set {}.{} of '{}'", target->Component->GetName(),
					changed.size() == 1 ? changed.front().get<std::string>() : std::string("fields"),
					target->Subject.GetName());
				if (Result<void> done = context.Do(
						CreateScope<SetFieldsOperation>(
							target->Scene->Path, target->Subject.GetUUID(), target->Component->GetName(),
							std::move(changes), message),
						GetString(arguments, "mergeKey"));
				    !done)
				{
					return Fail(done.GetError());
				}
			}
			CommandResult result = DescribeComponent(world, *target);
			if (result.Ok)
			{
				result.Data["changed"] = std::move(changed);
				result.Message = std::move(message);
			}
			return result;
		}

		// ---- component.types ----

		Json FieldToJson(const FieldInfo& field)
		{
			Json json = Json::object();
			json["name"] = field.Name;
			json["type"] = std::string(ToString(field.Type));
			if (field.Type == FieldType::Array)
			{
				json["elementType"] = std::string(ToString(field.ElementType));
			}
			const FieldAttributes& attributes = field.Attributes;
			if (attributes.Struct != nullptr)
			{
				json["struct"] = attributes.Struct->GetName();
			}
			if (attributes.Enum != nullptr)
			{
				Json values = Json::array();
				for (const EnumEntry& entry : attributes.Enum->Entries)
				{
					values.push_back(entry.Name);
				}
				json["enum"] = std::move(values);
			}
			if (!attributes.AssetType.empty())
			{
				json["assetType"] = attributes.AssetType;
			}
			if (const Result<Json> value = FieldValueToJson(field, field.Default); value)
			{
				json["default"] = *value;
			}
			if (attributes.Range.has_value())
			{
				json["range"] = Json::array({ attributes.Range->Min, attributes.Range->Max });
			}
			if (!attributes.Tooltip.empty())
			{
				json["description"] = attributes.Tooltip;
			}
			if (!attributes.Category.empty())
			{
				json["category"] = attributes.Category;
			}
			json["readOnly"] = field.HasFlag(FieldFlags::ReadOnly);
			json["stored"] = IsSerialized(field);
			return json;
		}

		std::vector<std::string> ComponentFlagNames(ComponentFlags flags)
		{
			std::vector<std::string> names;
			const std::pair<ComponentFlags, const char*> known[] = {
				{ ComponentFlags::Serializable, "serializable" }, { ComponentFlags::EditorVisible, "editor-visible" },
				{ ComponentFlags::Removable, "removable" },       { ComponentFlags::Internal, "internal" },
				{ ComponentFlags::Derived, "derived" },
			};
			for (const auto& [flag, name] : known)
			{
				if (HasFlag(flags, flag))
				{
					names.emplace_back(name);
				}
			}
			return names;
		}

		CommandResult ComponentTypes(EditorContext& context, const Json& arguments)
		{
			const TypeRegistry& types = context.GetTypeRegistry();
			const std::optional<std::string> only = TryGetString(arguments, "name");
			const bool includeInternal = GetBool(arguments, "includeInternal", false);

			Json components = Json::array();
			for (const ComponentInfo* info : types.GetComponents())
			{
				if (only.has_value() ? info->GetName() != *only : !includeInternal && !IsStoredInFiles(*info))
				{
					continue;
				}
				Json entry = Json::object();
				entry["name"] = info->GetName();
				entry["dynamic"] = info->IsDynamic();
				entry["tag"] = info->IsTag();
				entry["flags"] = ComponentFlagNames(info->GetFlags());
				Json fields = Json::array();
				for (const FieldInfo& field : info->GetFields())
				{
					fields.push_back(FieldToJson(field));
				}
				entry["fields"] = std::move(fields);
				components.push_back(std::move(entry));
			}
			if (only.has_value() && components.empty())
			{
				return Fail(FindComponentType(types, *only).GetError());
			}

			Json structs = Json::array();
			for (const StructInfo* info : types.GetStructs())
			{
				Json entry = Json::object();
				entry["name"] = info->GetName();
				Json fields = Json::array();
				for (const FieldInfo& field : info->GetFields())
				{
					fields.push_back(FieldToJson(field));
				}
				entry["fields"] = std::move(fields);
				structs.push_back(std::move(entry));
			}
			Json data = Json::object();
			data["count"] = components.size();
			data["components"] = std::move(components);
			data["structs"] = std::move(structs);
			return CommandResult::Success(std::move(data));
		}
	}

	void RegisterComponentCommands(CommandRegistry& registry)
	{
		RegisterOrDie(
			registry,
			CommandInfo{
				.Name = "component.add",
				.Summary = "Add a component to an entity",
				.Arguments = { StringArg("entity", EntityHelp).Require(),
		                       StringArg(
								   "component", "Component type name, e.g. Transform (component.types lists them)")
		                           .Require(),
		                       ObjectArg(
								   "values",
								   "Initial field values: { \"Maximum\": 50 }; the others keep their defaults") },
				.ResultDescription = "{ entity, component, values: { fields } }",
				.Flags = CommandFlags::Mutating | CommandFlags::Undoable | CommandFlags::RequiresScene,
			},
			ComponentAdd);

		RegisterOrDie(
			registry,
			CommandInfo{
				.Name = "component.remove",
				.Summary = "Remove a component from an entity",
				.Arguments = { StringArg("entity", EntityHelp).Require(),
		                       StringArg("component", "Component type name").Require() },
				.ResultDescription = "{ entity, component }",
				.Flags = CommandFlags::Mutating | CommandFlags::Undoable | CommandFlags::RequiresScene,
			},
			ComponentRemove);

		RegisterOrDie(
			registry,
			CommandInfo{
				.Name = "component.get",
				.Summary = "Show the field values of one component of an entity",
				.Arguments = { StringArg("entity", EntityHelp).Require(),
		                       StringArg("component", "Component type name").Require() },
				.ResultDescription = "{ entity, component, values: { fields } }",
				.Flags = CommandFlags::RequiresScene,
			},
			ComponentGet);

		RegisterOrDie(
			registry,
			CommandInfo{
				.Name = "component.set",
				.Summary = "Change field values of a component",
				.Description =
					"'values' is a partial object: fields it does not name keep their values, and so do the "
					"members of struct fields it does not name ({ \"Layers\": [{ \"Weight\": 0.5 }] } changes "
					"only the weight of layer 0). Other fields (vectors, lists of numbers) are replaced as a "
					"whole. Values that already match leave no undo entry.",
				.Arguments = { StringArg("entity", EntityHelp).Require(),
		                       StringArg("component", "Component type name").Require(),
		                       ObjectArg("values", "Field values: { \"Current\": 80 }").Require(),
		                       StringArg(
								   "mergeKey",
								   "Consecutive edits with the same key and target become one undo step "
								   "(for drags)") },
				.ResultDescription = "{ entity, component, values: { all fields now }, changed: [names] }",
				.Flags = CommandFlags::Mutating | CommandFlags::Undoable | CommandFlags::RequiresScene,
			},
			ComponentSet);

		RegisterOrDie(
			registry,
			CommandInfo{
				.Name = "component.types",
				.Summary = "Describe the component types: fields, types, defaults, ranges and enum values",
				.Description =
					"Includes the components that scripts declare. The structs that fields use are listed with "
					"their own fields.",
				.Arguments = { StringArg("name", "Only this component type"),
		                       BooleanArg("includeInternal", "Also list the components the engine manages itself")
		                           .WithDefault(false) },
				.ResultDescription =
					"{ count, components: [{ name, dynamic, tag, flags, fields: [{ name, type, default, "
					"range, description, enum, struct, assetType, readOnly }] }], structs: [...] }",
				.Flags = CommandFlags::None,
			},
			ComponentTypes);
	}
}
