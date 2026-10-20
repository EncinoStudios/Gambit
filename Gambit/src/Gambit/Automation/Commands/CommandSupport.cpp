#include "gbpch.h"

#include "Gambit/Automation/Commands/CommandSupport.h"

#include "Gambit/Serialization/FieldSerializer.h"

#include <cctype>

namespace Gambit::CommandSupport
{
	namespace
	{
		// Failure when `report` holds warnings: commands are strict about their input, so an unknown member in a value
		// is an error and not a warning (the message names it).
		Result<void> RequireNoWarnings(const LoadReport& report)
		{
			if (report.IsEmpty())
			{
				return {};
			}
			const Diagnostic& first = report.GetWarnings().front();
			return MakeCommandError(
				CommandErrorCode::InvalidArguments, std::format("{}: {}", first.Path, first.Message));
		}
	}

	void RegisterOrDie(CommandRegistry& registry, CommandInfo info, CommandHandler handler)
	{
		const std::string name = info.Name;
		const Result<void> registered = registry.Register(std::move(info), std::move(handler));
		GB_CORE_VERIFY(registered.HasValue(), "cannot register command '{}': {}", name, registered.GetError().Message);
	}

	// ---- Reading arguments ----

	bool Has(const Json& arguments, std::string_view name)
	{
		return arguments.is_object() && arguments.contains(std::string(name))
			&& !arguments.at(std::string(name)).is_null();
	}

	std::string GetString(const Json& arguments, std::string_view name)
	{
		return Has(arguments, name) ? arguments.at(std::string(name)).get<std::string>() : std::string();
	}

	std::optional<std::string> TryGetString(const Json& arguments, std::string_view name)
	{
		if (!Has(arguments, name))
		{
			return std::nullopt;
		}
		return arguments.at(std::string(name)).get<std::string>();
	}

	int64_t GetInteger(const Json& arguments, std::string_view name, int64_t fallback)
	{
		return Has(arguments, name) ? arguments.at(std::string(name)).get<int64_t>() : fallback;
	}

	std::optional<int64_t> TryGetInteger(const Json& arguments, std::string_view name)
	{
		if (!Has(arguments, name))
		{
			return std::nullopt;
		}
		return arguments.at(std::string(name)).get<int64_t>();
	}

	uint64_t GetUnsigned(const Json& arguments, std::string_view name, uint64_t fallback)
	{
		return Has(arguments, name) ? arguments.at(std::string(name)).get<uint64_t>() : fallback;
	}

	bool GetBool(const Json& arguments, std::string_view name, bool fallback)
	{
		return Has(arguments, name) ? arguments.at(std::string(name)).get<bool>() : fallback;
	}

	// ---- Results ----

	CommandResult Fail(CommandErrorCode code, std::string message)
	{
		return CommandResult::Failure(code, std::move(message));
	}

	CommandResult Fail(const Error& error, CommandErrorCode fallback)
	{
		return CommandResult::FromError(error, fallback);
	}

	// ---- Entities ----

	Result<Entity> ResolveEntity(World& world, std::string_view text)
	{
		if (text.starts_with("0x") || text.starts_with("0X"))
		{
			const std::optional<UUID> uuid = UUID::Parse(text);
			if (!uuid.has_value())
			{
				return MakeCommandError(
					CommandErrorCode::InvalidArguments,
					std::format("'{}' is not a UUID: expected \"0x\" and up to 16 hexadecimal digits", text));
			}
			const Entity entity = world.TryGetEntity(*uuid);
			if (!entity.IsValid())
			{
				return MakeCommandError(
					CommandErrorCode::NotFound,
					std::format("there is no entity {} in the active scene (entity.list shows the entities)", text));
			}
			return entity;
		}

		std::vector<UUID> matches;
		for (const auto [handle, tag, id] : world.GetRegistry().view<TagComponent, IDComponent>().each())
		{
			if (tag.Name == text)
			{
				matches.push_back(id.ID);
			}
		}
		std::ranges::sort(matches);
		if (matches.empty())
		{
			return MakeCommandError(
				CommandErrorCode::NotFound,
				std::format(
					"there is no entity named '{}' in the active scene (entity.list shows the entities)", text));
		}
		if (matches.size() > 1)
		{
			std::string uuids;
			for (size_t index = 0; index < matches.size() && index < 5; ++index)
			{
				uuids += (index == 0 ? "" : ", ") + matches[index].ToString();
			}
			return MakeCommandError(
				CommandErrorCode::Conflict,
				std::format(
					"{} entities are named '{}' ({}{}): use the UUID", matches.size(), text, uuids,
					matches.size() > 5 ? ", ..." : ""));
		}
		return world.TryGetEntity(matches.front());
	}

	Result<Entity> ResolveOptionalEntity(World& world, const Json& arguments, std::string_view name)
	{
		const std::optional<std::string> text = TryGetString(arguments, name);
		if (!text.has_value())
		{
			return Entity();
		}
		Result<Entity> entity = ResolveEntity(world, *text);
		if (!entity)
		{
			return MakeCommandError(
				GetCommandErrorCode(entity.GetError()),
				std::format("argument '{}': {}", name, entity.GetError().Message));
		}
		return entity;
	}

	Json DescribeEntity(World& world, Entity entity)
	{
		Json json = Json::object();
		json["uuid"] = entity.GetUUID().ToString();
		json["name"] = entity.GetName();
		const Entity parent = world.GetParent(entity);
		json["parent"] = parent.IsValid() ? Json(parent.GetUUID().ToString()) : Json(nullptr);
		json["enabled"] = world.IsEnabled(entity);
		return json;
	}

	std::vector<std::string> GetComponentNames(const World& world, Entity entity)
	{
		std::vector<std::string> names;
		for (const ComponentInfo* info : world.GetTypeRegistry().GetComponents())
		{
			if (IsStoredInFiles(*info) && info->Has(world, entity.GetHandle()))
			{
				names.push_back(info->GetName());
			}
		}
		return names;
	}

	size_t CountSubtree(World& world, Entity root)
	{
		size_t count = 0;
		std::vector<Entity> pending{ root };
		while (!pending.empty())
		{
			const Entity entity = pending.back();
			pending.pop_back();
			++count;
			const std::vector<Entity> children = world.GetChildren(entity);
			pending.insert(pending.end(), children.begin(), children.end());
		}
		return count;
	}

	void PruneSelection(EditorContext& context)
	{
		const SceneDocument* scene = context.GetActiveScene();
		if (scene == nullptr)
		{
			return;
		}
		std::vector<UUID> alive;
		for (const UUID entity : context.GetSelection())
		{
			if (scene->Scene->ContainsEntity(entity))
			{
				alive.push_back(entity);
			}
		}
		if (alive.size() != context.GetSelection().size())
		{
			[[maybe_unused]] const Result<void> pruned = context.SetSelection(std::move(alive));
		}
	}

	std::string GetEntityPath(World& world, Entity entity)
	{
		std::vector<std::string> names;
		for (Entity current = entity; current.IsValid(); current = world.GetParent(current))
		{
			names.push_back(current.GetName());
		}
		std::string path;
		for (auto name = names.rbegin(); name != names.rend(); ++name)
		{
			path += path.empty() ? *name : "/" + *name;
		}
		return path;
	}

	bool ContainsIgnoreCase(std::string_view text, std::string_view part)
	{
		const auto lower = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
		const auto found = std::ranges::search(text, part, [&](char a, char b) { return lower(a) == lower(b); });
		return !found.empty() || part.empty();
	}

	// ---- Components ----

	std::string JoinNames(const std::vector<std::string>& names, size_t limit)
	{
		std::string joined;
		for (size_t index = 0; index < names.size() && index < limit; ++index)
		{
			joined += index == 0 ? names[index] : ", " + names[index];
		}
		if (names.size() > limit)
		{
			joined += std::format(", ... ({} more)", names.size() - limit);
		}
		return joined;
	}

	Result<const ComponentInfo*> FindComponentType(const TypeRegistry& types, std::string_view name)
	{
		if (const ComponentInfo* info = types.FindComponent(name); info != nullptr)
		{
			return info;
		}
		std::vector<std::string> names;
		for (const ComponentInfo* info : types.GetComponents())
		{
			if (IsStoredInFiles(*info))
			{
				names.push_back(info->GetName());
			}
		}
		std::string message = std::format("there is no component type '{}'", name);
		if (const std::vector<std::string> close = FindClosestNames(name, names, 1); !close.empty())
		{
			message += std::format(" (did you mean '{}'?)", close.front());
		}
		message += std::format("; component.types lists them: {}", JoinNames(names));
		return MakeCommandError(CommandErrorCode::NotFound, std::move(message));
	}

	Result<const ComponentInfo*> FindEditableComponentType(const TypeRegistry& types, std::string_view name)
	{
		Result<const ComponentInfo*> info = FindComponentType(types, name);
		if (!info)
		{
			return info;
		}
		if ((*info)->HasFlag(ComponentFlags::Internal) || (*info)->HasFlag(ComponentFlags::Derived))
		{
			return MakeCommandError(
				CommandErrorCode::Conflict,
				std::format("the component '{}' is managed by the engine and cannot be edited directly", name));
		}
		return info;
	}

	Result<std::vector<FieldData>> DecodeFieldEdits(const ComponentInfo& info, const void* current, const Json& values)
	{
		if (!values.is_object())
		{
			return MakeCommandError(
				CommandErrorCode::InvalidArguments,
				std::format("the values of '{}' must be an object of field values", info.GetName()));
		}
		std::vector<FieldData> edits;
		LoadReport report;
		const std::string componentPath = JsonPathAppend({}, info.GetName());
		for (const auto& [name, member] : values.items())
		{
			const FieldInfo* field = info.FindField(name);
			if (field == nullptr)
			{
				std::vector<std::string> fieldNames;
				for (const FieldInfo& known : info.GetFields())
				{
					if (IsSerialized(known) && !known.HasFlag(FieldFlags::ReadOnly))
					{
						fieldNames.push_back(known.Name);
					}
				}
				std::string message = std::format("component '{}' has no field '{}'", info.GetName(), name);
				if (const std::vector<std::string> close = FindClosestNames(name, fieldNames, 1); !close.empty())
				{
					message += std::format(" (did you mean '{}'?)", close.front());
				}
				message += fieldNames.empty() ? "; it has no fields"
											  : std::format("; its fields are: {}", JoinNames(fieldNames));
				return MakeCommandError(CommandErrorCode::InvalidArguments, std::move(message));
			}
			if (field->HasFlag(FieldFlags::ReadOnly))
			{
				return MakeCommandError(
					CommandErrorCode::InvalidArguments,
					std::format("the field '{}.{}' is read-only", info.GetName(), name));
			}
			if (!IsSerialized(*field))
			{
				return MakeCommandError(
					CommandErrorCode::InvalidArguments,
					std::format("the field '{}.{}' is runtime state and cannot be set", info.GetName(), name));
			}
			const FieldValue start = current != nullptr ? field->GetValue(current) : field->Default;
			Result<FieldValue> merged = FieldValueMergeFromJson(
				*field, start, member, JsonPathAppend(componentPath, name), report, MergeAccess::Tool);
			if (!merged)
			{
				return MakeCommandError(CommandErrorCode::InvalidArguments, merged.GetError().Message);
			}
			edits.push_back({ name, std::move(*merged) });
		}
		if (Result<void> strict = RequireNoWarnings(report); !strict)
		{
			return strict.GetError();
		}
		return edits;
	}

	Result<Json> ComponentJson(const ComponentInfo& info, const void* component)
	{
		Result<Json> json = ComponentToJson(info, component);
		if (!json)
		{
			return json.GetError();
		}
		return json;
	}

	std::string HexText(uint64_t value)
	{
		return std::format("0x{:016X}", value);
	}
}
