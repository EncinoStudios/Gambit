#include "gbpch.h"

#include "Gambit/Automation/BuiltinCommands.h"
#include "Gambit/Automation/BulkTable.h"
#include "Gambit/Automation/Commands/CommandSupport.h"

namespace Gambit::Commands
{
	namespace
	{
		using namespace CommandSupport;

		std::vector<std::string> GetFieldList(const Json& arguments)
		{
			std::vector<std::string> fields;
			if (Has(arguments, "fields"))
			{
				for (const Json& field : arguments.at("fields"))
				{
					fields.push_back(field.get<std::string>());
				}
			}
			return fields;
		}

		Result<const ComponentInfo*> FindComponent(EditorContext& context, const Json& arguments)
		{
			return FindComponentType(context.GetTypeRegistry(), GetString(arguments, "component"));
		}

		CommandResult BulkGet(EditorContext& context, const Json& arguments)
		{
			Result<const ComponentInfo*> component = FindComponent(context, arguments);
			if (!component)
			{
				return Fail(component.GetError());
			}
			const std::vector<std::string> fields = GetFieldList(arguments);
			Result<BulkTable> table = ReadBulkTable(
				*context.GetActiveScene()->Scene, **component, fields,
				static_cast<size_t>(GetInteger(arguments, "offset")),
				static_cast<size_t>(GetInteger(arguments, "limit")));
			if (!table)
			{
				return Fail(table.GetError());
			}
			return CommandResult::Success(
				BulkTableToJson(*table),
				std::format(
					"{}: {} {}", (*component)->GetName(), table->Total, table->Total == 1 ? "entity" : "entities"));
		}

		CommandResult Apply(EditorContext& context, const ComponentInfo& component, const BulkTable& table)
		{
			Result<size_t> changed = ApplyBulkTable(context, *context.GetActiveScene(), component, table);
			if (!changed)
			{
				return Fail(changed.GetError(), CommandErrorCode::InvalidArguments);
			}
			Json data = Json::object();
			data["component"] = component.GetName();
			data["rows"] = table.Rows.size();
			data["changed"] = *changed;
			return CommandResult::Success(
				std::move(data),
				std::format(
					"Changed {} of {} {}", *changed, table.Rows.size(),
					table.Rows.size() == 1 ? "entity" : "entities"));
		}

		CommandResult BulkSet(EditorContext& context, const Json& arguments)
		{
			Result<const ComponentInfo*> component = FindComponent(context, arguments);
			if (!component)
			{
				return Fail(component.GetError());
			}
			if ((*component)->HasFlag(ComponentFlags::Internal) || (*component)->HasFlag(ComponentFlags::Derived))
			{
				return Fail(
					CommandErrorCode::Conflict,
					std::format("the component '{}' is managed by the engine", (*component)->GetName()));
			}
			Result<BulkTable> table = BulkTableFromJson(**component, arguments.at("table"));
			if (!table)
			{
				return Fail(table.GetError());
			}
			return Apply(context, **component, *table);
		}

		CommandResult BulkExportCsv(EditorContext& context, const Json& arguments)
		{
			Result<const ComponentInfo*> component = FindComponent(context, arguments);
			if (!component)
			{
				return Fail(component.GetError());
			}
			const std::vector<std::string> fields = GetFieldList(arguments);
			Result<BulkTable> table = ReadBulkTable(
				*context.GetActiveScene()->Scene, **component, fields, 0, std::numeric_limits<size_t>::max());
			if (!table)
			{
				return Fail(table.GetError());
			}
			Result<std::string> csv = BulkTableToCsv(**component, *table);
			if (!csv)
			{
				return Fail(csv.GetError());
			}

			Json data = Json::object();
			data["component"] = (*component)->GetName();
			data["rows"] = table->Rows.size();
			if (!Has(arguments, "path"))
			{
				data["csv"] = std::move(*csv);
				return CommandResult::Success(std::move(data), std::format("{} rows", table->Rows.size()));
			}
			if (context.IsDryRun())
			{
				return Fail(
					CommandErrorCode::Unsupported, "a dry run writes no files: leave out 'path' to get the CSV text");
			}
			Result<std::string> path = NormalizeProjectPath(GetString(arguments, "path"));
			if (!path)
			{
				return Fail(path.GetError());
			}
			if (!path->ends_with(".csv"))
			{
				return Fail(CommandErrorCode::InvalidArguments, std::format("'{}' must end in .csv", *path));
			}
			if (Result<void> written = context.GetProject()->GetVFS().WriteText(*path, *csv); !written)
			{
				return Fail(AddSource(*path, written.GetError()));
			}
			data["path"] = *path;
			return CommandResult::Success(
				std::move(data), std::format("Wrote {} rows to '{}'", table->Rows.size(), *path));
		}

		CommandResult BulkImportCsv(EditorContext& context, const Json& arguments)
		{
			Result<const ComponentInfo*> component = FindComponent(context, arguments);
			if (!component)
			{
				return Fail(component.GetError());
			}
			if (Has(arguments, "path") == Has(arguments, "csv"))
			{
				return Fail(
					CommandErrorCode::InvalidArguments,
					"give exactly one of 'path' (a CSV file of the project) and 'csv' (the text)");
			}
			if ((*component)->HasFlag(ComponentFlags::Internal) || (*component)->HasFlag(ComponentFlags::Derived))
			{
				return Fail(
					CommandErrorCode::Conflict,
					std::format("the component '{}' is managed by the engine", (*component)->GetName()));
			}
			std::string text = GetString(arguments, "csv");
			std::string source = "csv";
			if (Has(arguments, "path"))
			{
				Result<std::string> path = NormalizeProjectPath(GetString(arguments, "path"));
				if (!path)
				{
					return Fail(path.GetError());
				}
				Result<std::string> content = context.GetProject()->GetVFS().ReadText(*path);
				if (!content)
				{
					return Fail(AddSource(*path, content.GetError()));
				}
				text = std::move(*content);
				source = *path;
			}
			Result<BulkTable> table = BulkTableFromCsv(**component, text, source);
			if (!table)
			{
				return Fail(table.GetError());
			}
			return Apply(context, **component, *table);
		}
	}

	void RegisterBulkCommands(CommandRegistry& registry)
	{
		const std::string componentHelp = "Component type name, e.g. Health";

		RegisterOrDie(
			registry,
			CommandInfo{
				.Name = "bulk.get",
				.Summary = "Read one component of all entities of the active scene as a table",
				.Description =
					"Rows are the entities that have the component (hierarchy order), columns its fields. The "
					"result can be edited and given to bulk.set.",
				.Arguments = { StringArg("component", componentHelp).Require(),
		                       ArrayArg(
								   "fields", ArgumentType::String, "Only these fields (default: all stored fields)"),
		                       IntegerArg("limit", "At most this many rows").WithRange(1, 1000000).WithDefault(1000),
		                       IntegerArg("offset", "Skip this many rows").WithMinimum(0).WithDefault(0) },
				.ResultDescription = "{ component, columns: [{ name, type, readOnly }], rows: [{ entity, name, values: "
									 "[...] }], total }",
				.Flags = CommandFlags::RequiresScene,
			},
			BulkGet);

		RegisterOrDie(
			registry,
			CommandInfo{
				.Name = "bulk.set",
				.Summary = "Apply a table of edits to one component of many entities",
				.Description =
					"The table has the shape bulk.get returns; columns may be a subset of the fields. Struct cells "
					"merge like component.set, cells that already have their value change nothing. The edits are "
					"checked first and then applied as one undo step.",
				.Arguments = { StringArg("component", componentHelp).Require(),
		                       ObjectArg(
								   "table",
								   "{ \"columns\": [\"Current\"], \"rows\": [{ \"entity\": \"0x...\", \"values\": [50] "
								   "}] }")
		                           .Require() },
				.ResultDescription = "{ component, rows, changed }",
				.Flags = CommandFlags::Mutating | CommandFlags::Undoable | CommandFlags::RequiresScene,
			},
			BulkSet);

		RegisterOrDie(
			registry,
			CommandInfo{
				.Name = "bulk.export-csv",
				.Summary = "Export one component of all entities as CSV for a spreadsheet",
				.Description = "The first column holds the entity UUIDs; vectors get one column per component "
							   "(Translation_X), structs and arrays one column of JSON text.",
				.Arguments = { StringArg("component", componentHelp).Require(),
		                       ArrayArg(
								   "fields", ArgumentType::String, "Only these fields (default: all stored fields)"),
		                       StringArg("path", "Project path of the .csv file to write (default: return the text)") },
				.ResultDescription = "{ component, rows, csv } or { component, rows, path }",
				.Flags = CommandFlags::RequiresScene,
			},
			BulkExportCsv);

		RegisterOrDie(
			registry,
			CommandInfo{
				.Name = "bulk.import-csv",
				.Summary = "Apply an edited CSV from bulk.export-csv to the active scene",
				.Description = "Columns may be any subset of the fields; the edits are applied as one undo step.",
				.Arguments = { StringArg("component", componentHelp).Require(),
		                       StringArg("path", "Project path of the CSV file"),
		                       StringArg("csv", "The CSV text, instead of a file") },
				.ResultDescription = "{ component, rows, changed }",
				.Flags = CommandFlags::Mutating | CommandFlags::Undoable | CommandFlags::RequiresScene,
			},
			BulkImportCsv);
	}
}
