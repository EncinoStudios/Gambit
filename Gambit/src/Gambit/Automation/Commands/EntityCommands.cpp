#include "gbpch.h"

#include "Gambit/Automation/BuiltinCommands.h"
#include "Gambit/Automation/Commands/CommandSupport.h"
#include "Gambit/Automation/UndoOperations.h"
#include "Gambit/Core/Utf8.h"

namespace Gambit::Commands
{
	namespace
	{
		using namespace CommandSupport;

		constexpr std::string_view TagName = "Tag";
		constexpr std::string_view NameField = "Name";

		FieldValue TextValue(std::string_view text)
		{
			return FieldValue(std::in_place_type<std::string>, std::string(text));
		}

		// The Name field of the Tag component data, if the data has it.
		FieldData* FindNameField(std::vector<ComponentData>& components)
		{
			for (ComponentData& component : components)
			{
				if (component.Name == TagName)
				{
					const auto field = std::ranges::find(component.Fields, NameField, &FieldData::Name);
					return field == component.Fields.end() ? nullptr : &*field;
				}
			}
			return nullptr;
		}

		void SetTagName(std::vector<ComponentData>& components, std::string_view name)
		{
			if (FieldData* field = FindNameField(components); field != nullptr)
			{
				field->Value = TextValue(name);
				return;
			}
			const auto tag = std::ranges::find(components, TagName, &ComponentData::Name);
			if (tag != components.end())
			{
				tag->Fields.push_back({ std::string(NameField), TextValue(name) });
				return;
			}
			components.push_back({ std::string(TagName), { { std::string(NameField), TextValue(name) } } });
		}

		Result<void> CheckName(std::string_view name)
		{
			if (name.empty() || !IsValidUtf8(name))
			{
				return MakeCommandError(CommandErrorCode::InvalidArguments, "an entity name is a non-empty UTF-8 text");
			}
			return {};
		}

		// ---- entity.create ----

		CommandResult EntityCreate(EditorContext& context, const Json& arguments)
		{
			SceneDocument& scene = *context.GetActiveScene();
			World& world = *scene.Scene;
			const TypeRegistry& types = world.GetTypeRegistry();

			Result<Entity> parent = ResolveOptionalEntity(world, arguments, "parent");
			if (!parent)
			{
				return Fail(parent.GetError());
			}

			EntityData data;
			data.Id = UUID(1); // replaced by a new UUID when the entity is spawned
			if (Has(arguments, "components"))
			{
				const Json& components = arguments.at("components");
				if (!components.is_object())
				{
					return Fail(
						CommandErrorCode::InvalidArguments,
						"'components' must be an object: { \"<Component>\": { <fields> } }");
				}
				for (const auto& [name, values] : components.items())
				{
					Result<const ComponentInfo*> info = FindEditableComponentType(types, name);
					if (!info)
					{
						return Fail(info.GetError());
					}
					Result<std::vector<FieldData>> fields = DecodeFieldEdits(**info, nullptr, values);
					if (!fields)
					{
						return Fail(fields.GetError());
					}
					data.Components.push_back({ name, std::move(*fields) });
				}
			}
			if (Has(arguments, "name"))
			{
				const std::string name = GetString(arguments, "name");
				if (Result<void> valid = CheckName(name); !valid)
				{
					return Fail(valid.GetError());
				}
				if (FindNameField(data.Components) != nullptr)
				{
					return Fail(
						CommandErrorCode::InvalidArguments,
						"the name is given twice: as 'name' and as the Name field of the Tag component");
				}
				SetTagName(data.Components, name);
			}
			const FieldData* nameField = FindNameField(data.Components);
			const std::string displayName =
				nameField != nullptr ? std::get<std::string>(nameField->Value) : std::string("Entity");

			const std::optional<int64_t> index = TryGetInteger(arguments, "index");
			const UUID parentUuid = parent->IsValid() ? parent->GetUUID() : UUID();
			const Ref<UUID> created = CreateRef<UUID>();
			SubtreeOperation::Creator creator = [data = std::move(data), parentUuid, index,
			                                     created](World& target) -> Result<Entity> {
				SpawnOptions options;
				options.Ids = UUIDMode::Random; // editors and tools use random UUIDs (D-020 "Simulation start")
				if (parentUuid.IsValid())
				{
					options.Parent = target.TryGetEntity(parentUuid);
				}
				Result<std::vector<Entity>> spawned =
					SpawnEntities(target, std::span<const EntityData>(&data, 1), options);
				if (!spawned)
				{
					return MakeCommandError(CommandErrorCode::InvalidArguments, spawned.GetError().Message);
				}
				const Entity root = spawned->front();
				if (index.has_value())
				{
					target.SetSiblingIndex(root, static_cast<size_t>(*index));
				}
				*created = root.GetUUID();
				return root;
			};
			if (Result<void> done = context.Do(SubtreeOperation::Create(
					scene.Path, std::move(creator), std::format("Create entity '{}'", displayName)));
			    !done)
			{
				return Fail(done.GetError());
			}

			const Entity entity = world.TryGetEntity(*created);
			Json result = DescribeEntity(world, entity);
			result["index"] = world.GetSiblingIndex(entity);
			result["components"] = GetComponentNames(world, entity);
			return CommandResult::Success(
				std::move(result), std::format("Created entity '{}' ({})", displayName, created->ToString()));
		}

		// ---- entity.destroy ----

		CommandResult EntityDestroy(EditorContext& context, const Json& arguments)
		{
			SceneDocument& scene = *context.GetActiveScene();
			World& world = *scene.Scene;
			Result<Entity> entity = ResolveEntity(world, GetString(arguments, "entity"));
			if (!entity)
			{
				return Fail(entity.GetError());
			}
			const std::string name = entity->GetName();
			const UUID uuid = entity->GetUUID();
			const size_t count = CountSubtree(world, *entity);
			if (Result<void> done =
			        context.Do(SubtreeOperation::Destroy(scene.Path, uuid, std::format("Destroy entity '{}'", name)));
			    !done)
			{
				return Fail(done.GetError());
			}
			PruneSelection(context);
			Json result = Json::object();
			result["destroyed"] = uuid.ToString();
			result["entityCount"] = count;
			return CommandResult::Success(
				std::move(result),
				std::format("Destroyed entity '{}' ({} {})", name, count, count == 1 ? "entity" : "entities"));
		}

		// ---- entity.rename ----

		CommandResult EntityRename(EditorContext& context, const Json& arguments)
		{
			SceneDocument& scene = *context.GetActiveScene();
			World& world = *scene.Scene;
			Result<Entity> entity = ResolveEntity(world, GetString(arguments, "entity"));
			if (!entity)
			{
				return Fail(entity.GetError());
			}
			const std::string name = GetString(arguments, "name");
			if (Result<void> valid = CheckName(name); !valid)
			{
				return Fail(valid.GetError());
			}
			const std::string oldName = entity->GetName();
			std::vector<FieldData> fields;
			fields.push_back({ std::string(NameField), TextValue(name) });
			if (Result<void> done = context.Do(CreateScope<SetFieldsOperation>(
					scene.Path, entity->GetUUID(), std::string(TagName), std::move(fields),
					std::format("Rename '{}' to '{}'", oldName, name)));
			    !done)
			{
				return Fail(done.GetError());
			}
			return CommandResult::Success(
				DescribeEntity(world, *entity), std::format("Renamed '{}' to '{}'", oldName, name));
		}

		// ---- entity.reparent ----

		CommandResult EntityReparent(EditorContext& context, const Json& arguments)
		{
			SceneDocument& scene = *context.GetActiveScene();
			World& world = *scene.Scene;
			Result<Entity> entity = ResolveEntity(world, GetString(arguments, "entity"));
			if (!entity)
			{
				return Fail(entity.GetError());
			}
			Result<Entity> parent = ResolveOptionalEntity(world, arguments, "parent");
			if (!parent)
			{
				return Fail(parent.GetError());
			}
			if (parent->IsValid() && (*parent == *entity || world.IsAncestorOf(*entity, *parent)))
			{
				return Fail(
					CommandErrorCode::Conflict,
					std::format(
						"cannot make '{}' a child of '{}': the hierarchy would contain a cycle", entity->GetName(),
						parent->GetName()));
			}
			std::optional<size_t> index;
			if (const std::optional<int64_t> given = TryGetInteger(arguments, "index"); given.has_value())
			{
				index = static_cast<size_t>(*given);
			}
			const std::string where =
				parent->IsValid() ? std::format("'{}'", parent->GetName()) : std::string("the root");
			if (Result<void> done = context.Do(CreateScope<ReparentOperation>(
					scene.Path, entity->GetUUID(), parent->IsValid() ? parent->GetUUID() : UUID(), index,
					GetBool(arguments, "keepWorldTransform", false),
					std::format("Move '{}' under {}", entity->GetName(), where)));
			    !done)
			{
				return Fail(done.GetError());
			}
			Json result = DescribeEntity(world, *entity);
			result["index"] = world.GetSiblingIndex(*entity);
			return CommandResult::Success(
				std::move(result), std::format("Moved '{}' under {}", entity->GetName(), where));
		}

		// ---- entity.duplicate ----

		CommandResult EntityDuplicate(EditorContext& context, const Json& arguments)
		{
			SceneDocument& scene = *context.GetActiveScene();
			World& world = *scene.Scene;
			Result<Entity> source = ResolveEntity(world, GetString(arguments, "entity"));
			if (!source)
			{
				return Fail(source.GetError());
			}
			std::optional<std::string> name = TryGetString(arguments, "name");
			if (name.has_value())
			{
				if (Result<void> valid = CheckName(*name); !valid)
				{
					return Fail(valid.GetError());
				}
			}
			const UUID sourceUuid = source->GetUUID();
			const std::string sourceName = source->GetName();
			const Ref<UUID> created = CreateRef<UUID>();
			const Ref<size_t> count = CreateRef<size_t>(0);
			SubtreeOperation::Creator creator = [sourceUuid, name = std::move(name), created,
			                                     count](World& target) -> Result<Entity> {
				const Entity original = target.TryGetEntity(sourceUuid);
				if (!original.IsValid())
				{
					return MakeCommandError(CommandErrorCode::NotFound, "the entity to duplicate is gone");
				}
				std::vector<EntityData> data = CaptureSubtree(target, original);
				if (name.has_value())
				{
					SetTagName(data.front().Components, *name);
				}
				SpawnOptions options;
				options.Ids = UUIDMode::Random;
				options.Parent = target.GetParent(original); // the copy is a sibling of the original, right after it
				Result<std::vector<Entity>> spawned = SpawnEntities(target, data, options);
				if (!spawned)
				{
					return MakeCommandError(CommandErrorCode::Failed, spawned.GetError().Message);
				}
				const Entity root = spawned->front();
				target.SetSiblingIndex(root, target.GetSiblingIndex(original) + 1);
				*created = root.GetUUID();
				*count = spawned->size();
				return root;
			};
			if (Result<void> done = context.Do(SubtreeOperation::Create(
					scene.Path, std::move(creator), std::format("Duplicate entity '{}'", sourceName)));
			    !done)
			{
				return Fail(done.GetError());
			}
			const Entity copy = world.TryGetEntity(*created);
			Json result = DescribeEntity(world, copy);
			result["index"] = world.GetSiblingIndex(copy);
			result["entityCount"] = *count;
			return CommandResult::Success(
				std::move(result),
				std::format("Duplicated '{}' as '{}' ({})", sourceName, copy.GetName(), created->ToString()));
		}

		// ---- entity.list ----

		struct ListNode
		{
			Entity Subject;
			size_t Depth = 0;
			size_t Parent = std::numeric_limits<size_t>::max(); // index of the parent node
			bool Matches = false;
			bool Included = false;
		};

		Json NodeToJson(World& world, const ListNode& node, bool tree)
		{
			Json json = Json::object();
			json["uuid"] = node.Subject.GetUUID().ToString();
			json["name"] = node.Subject.GetName();
			if (!tree)
			{
				const Entity parent = world.GetParent(node.Subject);
				json["parent"] = parent.IsValid() ? Json(parent.GetUUID().ToString()) : Json(nullptr);
				json["depth"] = node.Depth;
			}
			json["components"] = GetComponentNames(world, node.Subject);
			return json;
		}

		CommandResult EntityList(EditorContext& context, const Json& arguments)
		{
			World& world = *context.GetActiveScene()->Scene;
			const bool tree = GetString(arguments, "layout") == "tree";
			const std::optional<std::string> nameFilter = TryGetString(arguments, "name");
			const std::optional<std::string> componentFilter = TryGetString(arguments, "component");
			const ComponentInfo* component = nullptr;
			if (componentFilter.has_value())
			{
				Result<const ComponentInfo*> info = FindComponentType(world.GetTypeRegistry(), *componentFilter);
				if (!info)
				{
					return Fail(info.GetError());
				}
				component = *info;
			}
			Result<Entity> within = ResolveOptionalEntity(world, arguments, "parent");
			if (!within)
			{
				return Fail(within.GetError());
			}

			// Every entity below the start (the roots, or the children of `parent`) in hierarchy pre-order.
			std::vector<ListNode> nodes;
			std::vector<std::pair<Entity, size_t>> pending; // entity and the index of its parent node
			const std::vector<Entity> starts = within->IsValid() ? world.GetChildren(*within) : world.GetRoots();
			for (auto start = starts.rbegin(); start != starts.rend(); ++start)
			{
				pending.emplace_back(*start, std::numeric_limits<size_t>::max());
			}
			while (!pending.empty())
			{
				const auto [entity, parentIndex] = pending.back();
				pending.pop_back();
				ListNode node;
				node.Subject = entity;
				node.Parent = parentIndex;
				node.Depth = parentIndex == std::numeric_limits<size_t>::max() ? 0 : nodes[parentIndex].Depth + 1;
				node.Matches = (!nameFilter.has_value() || ContainsIgnoreCase(entity.GetName(), *nameFilter))
					&& (component == nullptr || component->Has(world, entity.GetHandle()));
				nodes.push_back(node);
				const size_t index = nodes.size() - 1;
				const std::vector<Entity> children = world.GetChildren(entity);
				for (auto child = children.rbegin(); child != children.rend(); ++child)
				{
					pending.emplace_back(*child, index);
				}
			}
			for (size_t index = 0; index < nodes.size(); ++index)
			{
				if (!nodes[index].Matches)
				{
					continue;
				}
				nodes[index].Included = true;
				for (size_t ancestor = tree ? nodes[index].Parent : std::numeric_limits<size_t>::max();
				     ancestor != std::numeric_limits<size_t>::max() && !nodes[ancestor].Included;
				     ancestor = nodes[ancestor].Parent)
				{
					nodes[ancestor].Included = true;
				}
			}

			const auto offset = static_cast<size_t>(GetInteger(arguments, "offset"));
			const auto limit = static_cast<size_t>(GetInteger(arguments, "limit"));
			std::vector<size_t> shown;
			size_t total = 0;
			for (size_t index = 0; index < nodes.size(); ++index)
			{
				if (!nodes[index].Included)
				{
					continue;
				}
				if (total >= offset && shown.size() < limit)
				{
					shown.push_back(index);
				}
				++total;
			}

			Json data = Json::object();
			if (!tree)
			{
				Json entities = Json::array();
				for (const size_t index : shown)
				{
					entities.push_back(NodeToJson(world, nodes[index], false));
				}
				data["entities"] = std::move(entities);
			}
			else
			{
				// Nested: the children of a node are the shown nodes whose parent it is.
				std::vector<Json> built(nodes.size());
				std::vector<bool> isShown(nodes.size(), false);
				for (const size_t index : shown)
				{
					built[index] = NodeToJson(world, nodes[index], true);
					built[index]["children"] = Json::array();
					isShown[index] = true;
				}
				Json roots = Json::array();
				for (auto index = shown.rbegin(); index != shown.rend(); ++index)
				{
					const size_t parent = nodes[*index].Parent;
					if (parent != std::numeric_limits<size_t>::max() && isShown[parent])
					{
						// Walking backwards puts the last child first: insert in front to keep sibling order.
						Json& children = built[parent]["children"];
						children.insert(children.begin(), std::move(built[*index]));
					}
					else
					{
						roots.insert(roots.begin(), std::move(built[*index]));
					}
				}
				data["roots"] = std::move(roots);
			}
			data["count"] = shown.size();
			data["total"] = total;
			data["truncated"] = offset + shown.size() < total;
			return CommandResult::Success(
				std::move(data), std::format("{} of {} {}", shown.size(), total, total == 1 ? "entity" : "entities"));
		}

		// ---- entity.get ----

		CommandResult EntityGet(EditorContext& context, const Json& arguments)
		{
			World& world = *context.GetActiveScene()->Scene;
			Result<Entity> entity = ResolveEntity(world, GetString(arguments, "entity"));
			if (!entity)
			{
				return Fail(entity.GetError());
			}
			Json data = DescribeEntity(world, *entity);
			Json children = Json::array();
			for (const Entity child : world.GetChildren(*entity))
			{
				children.push_back(child.GetUUID().ToString());
			}
			data["children"] = std::move(children);
			data["index"] = world.GetSiblingIndex(*entity);
			data["active"] = world.IsActive(*entity);
			Json components = Json::object();
			for (const std::string& name : GetComponentNames(world, *entity))
			{
				const ComponentInfo* info = world.GetTypeRegistry().FindComponent(name);
				Result<Json> encoded = ComponentJson(*info, info->GetRaw(world, entity->GetHandle()));
				if (!encoded)
				{
					return Fail(encoded.GetError());
				}
				components[name] = std::move(*encoded);
			}
			data["components"] = std::move(components);
			return CommandResult::Success(std::move(data));
		}

		// ---- entity.find ----

		CommandResult EntityFind(EditorContext& context, const Json& arguments)
		{
			World& world = *context.GetActiveScene()->Scene;
			const std::optional<std::string> exact = TryGetString(arguments, "name");
			const std::optional<std::string> contains = TryGetString(arguments, "nameContains");
			const std::optional<std::string> componentName = TryGetString(arguments, "component");
			const std::optional<std::string> fieldName = TryGetString(arguments, "field");
			if (!exact.has_value() && !contains.has_value() && !componentName.has_value())
			{
				return Fail(
					CommandErrorCode::InvalidArguments, "give at least one of 'name', 'nameContains' and 'component'");
			}
			if (fieldName.has_value() != Has(arguments, "equals")
			    || (fieldName.has_value() && !componentName.has_value()))
			{
				return Fail(
					CommandErrorCode::InvalidArguments, "'field' and 'equals' go together and need 'component'");
			}

			const ComponentInfo* component = nullptr;
			const FieldInfo* field = nullptr;
			if (componentName.has_value())
			{
				Result<const ComponentInfo*> info = FindComponentType(world.GetTypeRegistry(), *componentName);
				if (!info)
				{
					return Fail(info.GetError());
				}
				component = *info;
				if (fieldName.has_value())
				{
					field = component->FindField(*fieldName);
					if (field == nullptr)
					{
						return Fail(
							CommandErrorCode::InvalidArguments,
							std::format("component '{}' has no field '{}'", *componentName, *fieldName));
					}
				}
			}

			std::vector<UUID> matches;
			for (const UUID uuid : world.GetEntityUUIDs())
			{
				const Entity entity = world.TryGetEntity(uuid);
				if (exact.has_value() && entity.GetName() != *exact)
				{
					continue;
				}
				if (contains.has_value() && !ContainsIgnoreCase(entity.GetName(), *contains))
				{
					continue;
				}
				if (component != nullptr)
				{
					const void* raw = component->GetRaw(world, entity.GetHandle());
					if (raw == nullptr)
					{
						continue;
					}
					if (field != nullptr)
					{
						Result<Json> encoded = FieldValueToJson(*field, field->GetValue(raw));
						if (!encoded || *encoded != arguments.at("equals"))
						{
							continue;
						}
					}
				}
				matches.push_back(uuid);
			}

			const auto limit = static_cast<size_t>(GetInteger(arguments, "limit"));
			Json entities = Json::array();
			for (size_t index = 0; index < matches.size() && index < limit; ++index)
			{
				const Entity entity = world.TryGetEntity(matches[index]);
				Json entry = Json::object();
				entry["uuid"] = matches[index].ToString();
				entry["name"] = entity.GetName();
				entry["path"] = GetEntityPath(world, entity);
				entities.push_back(std::move(entry));
			}
			Json data = Json::object();
			data["count"] = entities.size();
			data["total"] = matches.size();
			data["entities"] = std::move(entities);
			return CommandResult::Success(
				std::move(data),
				std::format("{} {} found", matches.size(), matches.size() == 1 ? "entity" : "entities"));
		}
	}

	void RegisterEntityCommands(CommandRegistry& registry)
	{
		const std::string entityHelp = "UUID (0x...) or the unique name of the entity";

		RegisterOrDie(
			registry,
			CommandInfo{
				.Name = "entity.create",
				.Summary = "Create an entity in the active scene",
				.Description = "The entity gets a random UUID. 'components' adds components with initial field values; "
							   "fields that are not listed keep their defaults.",
				.Arguments = { StringArg("name", "Name of the entity (default: \"Entity\")"),
		                       StringArg("parent", "Parent entity: " + entityHelp + " (default: a root entity)"),
		                       ObjectArg(
								   "components",
								   "Components to add: { \"Transform\": { \"Translation\": [0, 1, 0] } }"),
		                       IntegerArg("index", "Position among the siblings (default: last)").WithMinimum(0) },
				.ResultDescription = "{ uuid, name, parent, enabled, index, components: [names] }",
				.Flags = CommandFlags::Mutating | CommandFlags::Undoable | CommandFlags::RequiresScene,
			},
			EntityCreate);

		RegisterOrDie(
			registry,
			CommandInfo{
				.Name = "entity.destroy",
				.Summary = "Destroy an entity and its children",
				.Arguments = { StringArg("entity", entityHelp).Require() },
				.ResultDescription = "{ destroyed: uuid, entityCount }",
				.Flags = CommandFlags::Mutating | CommandFlags::Undoable | CommandFlags::RequiresScene,
			},
			EntityDestroy);

		RegisterOrDie(
			registry,
			CommandInfo{
				.Name = "entity.rename",
				.Summary = "Change the name of an entity",
				.Arguments = { StringArg("entity", entityHelp).Require(), StringArg("name", "The new name").Require() },
				.ResultDescription = "{ uuid, name, parent, enabled }",
				.Flags = CommandFlags::Mutating | CommandFlags::Undoable | CommandFlags::RequiresScene,
			},
			EntityRename);

		RegisterOrDie(
			registry,
			CommandInfo{
				.Name = "entity.reparent",
				.Summary = "Move an entity under another entity, or to the roots",
				.Arguments = { StringArg("entity", entityHelp).Require(),
		                       StringArg(
								   "parent", "The new parent: " + entityHelp + " (omit it to make the entity a root)"),
		                       IntegerArg("index", "Position among the new siblings (default: last)").WithMinimum(0),
		                       BooleanArg(
								   "keepWorldTransform",
								   "Change the local transform so that the entity stays where it is in the world")
		                           .WithDefault(false) },
				.ResultDescription = "{ uuid, name, parent, enabled, index }",
				.Flags = CommandFlags::Mutating | CommandFlags::Undoable | CommandFlags::RequiresScene,
			},
			EntityReparent);

		RegisterOrDie(
			registry,
			CommandInfo{
				.Name = "entity.duplicate",
				.Summary = "Copy an entity with its children and place the copy after it",
				.Description =
					"The copy and its descendants get new UUIDs; references between them follow, references to "
					"other entities stay.",
				.Arguments = { StringArg("entity", entityHelp).Require(),
		                       StringArg("name", "Name of the copy (default: the same name)") },
				.ResultDescription = "{ uuid, name, parent, enabled, index, entityCount }",
				.Flags = CommandFlags::Mutating | CommandFlags::Undoable | CommandFlags::RequiresScene,
			},
			EntityDuplicate);

		RegisterOrDie(
			registry,
			CommandInfo{
				.Name = "entity.list",
				.Summary = "List the entities of the active scene as a flat list or a tree",
				.Description = "Filters combine. In a tree, the ancestors of matching entities are listed too.",
				.Arguments = { EnumArg("layout", { "flat", "tree" }, "Hierarchy pre-order list, or nested")
		                           .WithDefault("flat"),
		                       StringArg("component", "Only entities that have this component"),
		                       StringArg("name", "Only entities whose name contains this text (ignoring case)"),
		                       StringArg("parent", "Only the descendants of this entity: " + entityHelp),
		                       IntegerArg("limit", "At most this many entities").WithRange(1, 100000).WithDefault(500),
		                       IntegerArg("offset", "Skip this many entities").WithMinimum(0).WithDefault(0) },
				.ResultDescription =
					"{ count, total, truncated, entities: [{ uuid, name, parent, depth, components }] } or "
					"{ count, total, truncated, roots: [{ uuid, name, components, children }] }",
				.Flags = CommandFlags::RequiresScene,
			},
			EntityList);

		RegisterOrDie(
			registry,
			CommandInfo{
				.Name = "entity.get",
				.Summary = "Show an entity with all its components",
				.Arguments = { StringArg("entity", entityHelp).Require() },
				.ResultDescription = "{ uuid, name, parent, enabled, active, index, children: [uuids], components: { "
									 "\"<Name>\": { fields } } }",
				.Flags = CommandFlags::RequiresScene,
			},
			EntityGet);

		RegisterOrDie(
			registry,
			CommandInfo{
				.Name = "entity.find",
				.Summary = "Find entities by name, component or the value of a component field",
				.Arguments = { StringArg("name", "Entities with exactly this name"),
		                       StringArg("nameContains", "Entities whose name contains this text (ignoring case)"),
		                       StringArg("component", "Entities that have this component"),
		                       StringArg("field", "With 'component': a field of it whose value must equal 'equals'"),
		                       AnyArg("equals", "The JSON value of the field, e.g. 100 or \"Slow\" or [0, 1, 0]"),
		                       IntegerArg("limit", "At most this many entities")
		                           .WithRange(1, 100000)
		                           .WithDefault(100) },
				.ResultDescription = "{ count, total, entities: [{ uuid, name, path }] }",
				.Flags = CommandFlags::RequiresScene,
			},
			EntityFind);
	}
}
