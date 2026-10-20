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

		CommandResult
		Apply(EditorContext& context, const ComponentInfo& component, const BulkTable& table, std::string_view mergeKey)
		{
			Result<size_t> changed = ApplyBulkTable(context, *context.GetActiveScene(), component, table, mergeKey);
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
			return Apply(context, **component, *table, GetString(arguments, "mergeKey"));
		}

		// The CSV text of one component of all entities of the active scene.
		Result<std::string>
		ExportCsv(EditorContext& context, const ComponentInfo& component, const Json& arguments, size_t& rows)
		{
			Result<BulkTable> table = ReadBulkTable(
				*context.GetActiveScene()->Scene, component, GetFieldList(arguments), 0,
				std::numeric_limits<size_t>::max());
			if (!table)
			{
				return table.GetError();
			}
			rows = table->Rows.size();
			return BulkTableToCsv(component, *table);
		}

		CommandResult BulkExportCsv(EditorContext& context, const Json& arguments)
		{
			Result<const ComponentInfo*> component = FindComponent(context, arguments);
			if (!component)
			{
				return Fail(component.GetError());
			}
			size_t rows = 0;
			Result<std::string> csv = ExportCsv(context, **component, arguments, rows);
			if (!csv)
			{
				return Fail(csv.GetError());
			}
			Json data = Json::object();
			data["component"] = (*component)->GetName();
			data["rows"] = rows;
			data["csv"] = std::move(*csv);
			return CommandResult::Success(std::move(data), std::format("{} rows", rows));
		}

		CommandResult BulkWriteCsv(EditorContext& context, const Json& arguments)
		{
			Result<const ComponentInfo*> component = FindComponent(context, arguments);
			if (!component)
			{
				return Fail(component.GetError());
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
			VFS& vfs = context.GetProject()->GetVFS();
			if (vfs.Exists(*path) && !GetBool(arguments, "overwrite", false))
			{
				return Fail(
					CommandErrorCode::AlreadyExists,
					std::format("'{}' exists already: pass overwrite to replace it", *path));
			}
			size_t rows = 0;
			Result<std::string> csv = ExportCsv(context, **component, arguments, rows);
			if (!csv)
			{
				return Fail(csv.GetError());
			}
			// The file may be a data table: an open one with edits would lose them or later overwrite the export.
			Result<bool> closed = context.CloseReplacedDocument(*path);
			if (!closed)
			{
				return Fail(closed.GetError());
			}
			if (Result<void> written = vfs.WriteText(*path, *csv); !written)
			{
				return Fail(AddSource(*path, written.GetError()));
			}
			AssetManager& assets = context.GetProject()->GetAssets();
			if (const AssetRecord* record = assets.FindByPath(*path); record != nullptr)
			{
				assets.UnloadAsset(record->Handle); // the asset is read again from the new content
			}
			Json data = Json::object();
			data["component"] = (*component)->GetName();
			data["rows"] = rows;
			data["path"] = *path;
			CommandResult result =
				CommandResult::Success(std::move(data), std::format("Wrote {} rows to '{}'", rows, *path));
			if (*closed)
			{
				result.Warnings.push_back(
					std::format("the data table '{}' was open; it was closed because its file was replaced", *path));
			}
			return result;
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
			return Apply(context, **component, *table, {});
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
					"checked first and then applied as one undo step; consecutive calls with the same mergeKey that "
					"change the same entities in the same order become one step (a drag of several entities).",
				.Arguments = { StringArg("component", componentHelp).Require(),
		                       ObjectArg(
								   "table",
								   "{ \"columns\": [\"Current\"], \"rows\": [{ \"entity\": \"0x...\", \"values\": [50] "
								   "}] }")
		                           .Require(),
		                       StringArg(
								   "mergeKey", "Merge with the previous bulk.set of the same key (one undo step)") },
				.ResultDescription = "{ component, rows, changed }",
				.Flags = CommandFlags::Mutating | CommandFlags::Undoable | CommandFlags::RequiresScene,
			},
			BulkSet);

		RegisterOrDie(
			registry,
			CommandInfo{
				.Name = "bulk.export-csv",
				.Summary = "Export one component of all entities as CSV text for a spreadsheet",
				.Description = "The first column holds the entity UUIDs; vectors get one column per component "
							   "(Translation_X), structs and arrays one column of JSON text. bulk.write-csv writes "
							   "the same text to a file.",
				.Arguments = { StringArg("component", componentHelp).Require(),
		                       ArrayArg(
								   "fields", ArgumentType::String, "Only these fields (default: all stored fields)") },
				.ResultDescription = "{ component, rows, csv }",
				.Flags = CommandFlags::RequiresScene,
			},
			BulkExportCsv);

		RegisterOrDie(
			registry,
			CommandInfo{
				.Name = "bulk.write-csv",
				.Summary = "Write one component of all entities to a CSV file of the project",
				.Description = "The text of bulk.export-csv. An existing file is only replaced with overwrite; an open "
							   "data table of the file is closed, or the write refused when it has unsaved changes.",
				.Arguments = { StringArg("component", componentHelp).Require(),
		                       StringArg("path", "Project path of the .csv file, e.g. Exports/Health.csv").Require(),
		                       ArrayArg(
								   "fields", ArgumentType::String, "Only these fields (default: all stored fields)"),
		                       BooleanArg("overwrite", "Replace a file that exists").WithDefault(false) },
				.ResultDescription = "{ component, rows, path }",
				.Flags = CommandFlags::Mutating | CommandFlags::RequiresScene | CommandFlags::WritesFiles,
			},
			BulkWriteCsv);

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
