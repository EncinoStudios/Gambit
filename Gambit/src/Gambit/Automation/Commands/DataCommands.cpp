#include "gbpch.h"

#include "Gambit/Asset/DataTable.h"
#include "Gambit/Automation/BuiltinCommands.h"
#include "Gambit/Automation/Commands/CommandSupport.h"
#include "Gambit/Automation/UndoOperations.h"
#include "Gambit/Serialization/FieldSerializer.h"

namespace Gambit::Commands
{
	namespace
	{
		using namespace CommandSupport;

		const std::string TableHelp = "The table's project path (Data/Weapons.csv) or its handle (0x...)";

		struct OpenTable
		{
			std::string Path;
			Ref<DataTable> Table;
		};

		Result<OpenTable> FindTable(EditorContext& context, std::string_view text)
		{
			std::string path(text);
			if (text.starts_with("0x") || text.starts_with("0X"))
			{
				const std::optional<UUID> handle = UUID::Parse(text);
				const AssetRecord* record =
					handle.has_value() ? context.GetProject()->GetAssets().Find(*handle) : nullptr;
				if (record == nullptr)
				{
					return MakeCommandError(
						CommandErrorCode::NotFound,
						std::format("there is no data table {} (data.tables lists them)", text));
				}
				path = record->Path;
			}
			else
			{
				Result<std::string> normalized = NormalizeProjectPath(text);
				if (!normalized)
				{
					return normalized.GetError();
				}
				path = std::move(*normalized);
			}
			Result<Ref<DataTable>> table = context.GetTable(path);
			if (!table)
			{
				return table.GetError();
			}
			return OpenTable{ path, *table };
		}

		std::vector<std::string> ColumnNames(const DataTable& table, bool includeId = true)
		{
			std::vector<std::string> names;
			for (const DataColumn& column : table.GetColumns())
			{
				names.push_back(column.Name);
			}
			if (!includeId && !names.empty())
			{
				names.erase(names.begin());
			}
			return names;
		}

		Json CellToJson(DataColumnType type, const FieldValue& value)
		{
			switch (type)
			{
				case DataColumnType::Int:
					return Json(std::get<int64_t>(value));
				case DataColumnType::Float:
					return Json(std::get<double>(value));
				case DataColumnType::Bool:
					return Json(std::get<bool>(value));
				case DataColumnType::String:
					return Json(std::get<std::string>(value));
				case DataColumnType::Asset:
				case DataColumnType::Entity:
					return Json(std::get<UUID>(value).ToString());
			}
			return Json(nullptr);
		}

		// A cell from JSON: the natural JSON type, or text in the notation of the CSV file.
		Result<FieldValue> CellFromJson(const DataColumn& column, const Json& json)
		{
			const auto mismatch = [&](std::string_view expected) {
				return MakeCommandError(
					CommandErrorCode::InvalidArguments,
					std::format(
						"column '{}' holds {} values: expected {}", column.Name, ToString(column.Type), expected));
			};
			if (json.is_string() && column.Type != DataColumnType::String)
			{
				Result<FieldValue> parsed = DataTable::ParseValue(column.Type, json.get_ref<const std::string&>());
				if (!parsed)
				{
					return MakeCommandError(
						CommandErrorCode::InvalidArguments,
						std::format("column '{}': {}", column.Name, parsed.GetError().Message));
				}
				return parsed;
			}
			switch (column.Type)
			{
				case DataColumnType::Int:
				{
					Result<int64_t> value = ReadJsonInteger<int64_t>(json, {});
					return value ? Result<FieldValue>(FieldValue(*value)) : Result<FieldValue>(mismatch("an integer"));
				}
				case DataColumnType::Float:
					return json.is_number() ? Result<FieldValue>(FieldValue(json.get<double>()))
											: Result<FieldValue>(mismatch("a number"));
				case DataColumnType::Bool:
					return json.is_boolean() ? Result<FieldValue>(FieldValue(json.get<bool>()))
											 : Result<FieldValue>(mismatch("true or false"));
				case DataColumnType::String:
					return json.is_string() ? Result<FieldValue>(FieldValue(json.get<std::string>()))
											: Result<FieldValue>(mismatch("a string"));
				case DataColumnType::Asset:
				case DataColumnType::Entity:
					return json.is_null() ? Result<FieldValue>(FieldValue(UUID()))
										  : Result<FieldValue>(mismatch("a \"0x...\" UUID string"));
			}
			return mismatch("a value");
		}

		Json RowToJson(const DataTable& table, const DataRow& row)
		{
			Json values = Json::object();
			for (size_t column = 1; column < table.GetColumns().size(); ++column)
			{
				values[table.GetColumns()[column].Name] =
					CellToJson(table.GetColumns()[column].Type, row.Values[column]);
			}
			Json json = Json::object();
			json["id"] = row.Id;
			json["values"] = std::move(values);
			return json;
		}

		Json ColumnsToJson(const DataTable& table)
		{
			Json columns = Json::array();
			for (const DataColumn& column : table.GetColumns())
			{
				Json entry = Json::object();
				entry["name"] = column.Name;
				entry["type"] = std::string(ToString(column.Type));
				columns.push_back(std::move(entry));
			}
			return columns;
		}

		CommandResult DataTables(EditorContext& context, const Json&)
		{
			Json tables = Json::array();
			for (const AssetRecord& record : context.GetProject()->GetAssets().GetRegistry().List(AssetType::DataTable))
			{
				Json entry = Json::object();
				entry["path"] = record.Path;
				entry["handle"] = record.Handle.ToString();
				if (Result<Ref<DataTable>> table = context.GetTable(record.Path); table)
				{
					entry["rows"] = (*table)->GetRowCount();
					entry["columns"] = ColumnNames(**table);
				}
				else
				{
					entry["error"] = table.GetError().Message;
				}
				tables.push_back(std::move(entry));
			}
			Json data = Json::object();
			data["count"] = tables.size();
			data["tables"] = std::move(tables);
			return CommandResult::Success(std::move(data));
		}

		CommandResult DataGet(EditorContext& context, const Json& arguments)
		{
			Result<OpenTable> open = FindTable(context, GetString(arguments, "table"));
			if (!open)
			{
				return Fail(open.GetError());
			}
			const DataTable& table = *open->Table;
			Json data = Json::object();
			data["table"] = open->Path;

			if (!Has(arguments, "row"))
			{
				if (Has(arguments, "column"))
				{
					return Fail(CommandErrorCode::InvalidArguments, "'column' needs 'row'");
				}
				const auto offset = static_cast<size_t>(GetInteger(arguments, "offset"));
				const auto limit = static_cast<size_t>(GetInteger(arguments, "limit"));
				Json rows = Json::array();
				for (size_t index = offset; index < table.GetRowCount() && rows.size() < limit; ++index)
				{
					rows.push_back(RowToJson(table, table.GetRows()[index]));
				}
				data["columns"] = ColumnsToJson(table);
				data["rowCount"] = table.GetRowCount();
				data["rows"] = std::move(rows);
				return CommandResult::Success(
					std::move(data), std::format("{}: {} rows", open->Path, table.GetRowCount()));
			}

			const std::string rowId = GetString(arguments, "row");
			const DataRow* row = table.GetRow(rowId);
			if (row == nullptr)
			{
				return Fail(CommandErrorCode::NotFound, std::format("table '{}' has no row '{}'", open->Path, rowId));
			}
			if (!Has(arguments, "column"))
			{
				data["row"] = RowToJson(table, *row);
				return CommandResult::Success(std::move(data));
			}
			const std::string columnName = GetString(arguments, "column");
			const std::optional<size_t> column = table.FindColumnIndex(columnName);
			if (!column.has_value())
			{
				return Fail(
					CommandErrorCode::NotFound,
					std::format(
						"table '{}' has no column '{}'; its columns are: {}", open->Path, columnName,
						JoinNames(ColumnNames(table))));
			}
			data["row"] = rowId;
			data["column"] = columnName;
			data["value"] = CellToJson(table.GetColumns()[*column].Type, row->Values[*column]);
			return CommandResult::Success(std::move(data));
		}

		CommandResult DataSet(EditorContext& context, const Json& arguments)
		{
			Result<OpenTable> open = FindTable(context, GetString(arguments, "table"));
			if (!open)
			{
				return Fail(open.GetError());
			}
			DataTable& table = *open->Table;
			const std::string rowId = GetString(arguments, "row");
			const std::string columnName = GetString(arguments, "column");
			if (!table.HasRow(rowId))
			{
				return Fail(CommandErrorCode::NotFound, std::format("table '{}' has no row '{}'", open->Path, rowId));
			}
			const std::optional<size_t> column = table.FindColumnIndex(columnName);
			if (!column.has_value() || *column == 0)
			{
				return Fail(
					column.has_value() ? CommandErrorCode::InvalidArguments : CommandErrorCode::NotFound,
					column.has_value() ? std::format("'{}' is the column of the row ids and cannot be set", columnName)
									   : std::format(
											 "table '{}' has no column '{}'; its columns are: {}", open->Path,
											 columnName, JoinNames(ColumnNames(table, false))));
			}
			const DataColumn& dataColumn = table.GetColumns()[*column];
			Result<FieldValue> value = CellFromJson(dataColumn, arguments.at("value"));
			if (!value)
			{
				return Fail(value.GetError());
			}
			const Json previous = CellToJson(dataColumn.Type, table.GetRow(rowId)->Values[*column]);
			const std::string description = std::format("Set {}[{}].{}", open->Path, rowId, columnName);
			if (Result<void> done = context.Do(
					CreateScope<SetCellOperation>(open->Path, rowId, columnName, std::move(*value), description));
			    !done)
			{
				return Fail(done.GetError(), CommandErrorCode::InvalidArguments);
			}
			Json data = Json::object();
			data["table"] = open->Path;
			data["row"] = rowId;
			data["column"] = columnName;
			data["value"] = CellToJson(dataColumn.Type, table.GetRow(rowId)->Values[*column]);
			data["previous"] = previous;
			return CommandResult::Success(std::move(data), description);
		}

		CommandResult DataAddRow(EditorContext& context, const Json& arguments)
		{
			Result<OpenTable> open = FindTable(context, GetString(arguments, "table"));
			if (!open)
			{
				return Fail(open.GetError());
			}
			DataTable& table = *open->Table;
			const std::string rowId = GetString(arguments, "row");
			if (table.HasRow(rowId))
			{
				return Fail(
					CommandErrorCode::AlreadyExists,
					std::format("table '{}' has a row '{}' already", open->Path, rowId));
			}
			std::vector<FieldValue> values;
			values.reserve(table.GetColumns().size() - 1);
			for (size_t column = 1; column < table.GetColumns().size(); ++column)
			{
				values.push_back(MakeZeroValue(ToFieldType(table.GetColumns()[column].Type)));
			}
			if (Has(arguments, "values"))
			{
				for (const auto& [name, cell] : arguments.at("values").items())
				{
					const std::optional<size_t> column = table.FindColumnIndex(name);
					if (!column.has_value() || *column == 0)
					{
						return Fail(
							CommandErrorCode::InvalidArguments,
							std::format(
								"table '{}' has no column '{}'; its columns are: {}", open->Path, name,
								JoinNames(ColumnNames(table, false))));
					}
					Result<FieldValue> value = CellFromJson(table.GetColumns()[*column], cell);
					if (!value)
					{
						return Fail(value.GetError());
					}
					values[*column - 1] = std::move(*value);
				}
			}
			std::optional<size_t> position;
			if (const std::optional<int64_t> index = TryGetInteger(arguments, "index"); index.has_value())
			{
				position = static_cast<size_t>(*index);
			}
			const std::string description = std::format("Add row '{}' to {}", rowId, open->Path);
			if (Result<void> done = context.Do(CreateScope<TableRowOperation>(
					open->Path, rowId, TableRowOperation::Mode::Add, std::move(values), position, description));
			    !done)
			{
				return Fail(done.GetError(), CommandErrorCode::InvalidArguments);
			}
			Json data = Json::object();
			data["table"] = open->Path;
			data["row"] = RowToJson(table, *table.GetRow(rowId));
			data["rowCount"] = table.GetRowCount();
			return CommandResult::Success(std::move(data), description);
		}

		CommandResult DataRemoveRow(EditorContext& context, const Json& arguments)
		{
			Result<OpenTable> open = FindTable(context, GetString(arguments, "table"));
			if (!open)
			{
				return Fail(open.GetError());
			}
			const std::string rowId = GetString(arguments, "row");
			if (!open->Table->HasRow(rowId))
			{
				return Fail(CommandErrorCode::NotFound, std::format("table '{}' has no row '{}'", open->Path, rowId));
			}
			const std::string description = std::format("Remove row '{}' from {}", rowId, open->Path);
			if (Result<void> done = context.Do(CreateScope<TableRowOperation>(
					open->Path, rowId, TableRowOperation::Mode::Remove, std::vector<FieldValue>{}, std::nullopt,
					description));
			    !done)
			{
				return Fail(done.GetError());
			}
			Json data = Json::object();
			data["table"] = open->Path;
			data["removed"] = rowId;
			data["rowCount"] = open->Table->GetRowCount();
			return CommandResult::Success(std::move(data), description);
		}
	}

	void RegisterDataCommands(CommandRegistry& registry)
	{
		RegisterOrDie(
			registry,
			CommandInfo{
				.Name = "data.tables",
				.Summary = "List the data tables (CSV files) of the project",
				.ResultDescription = "{ count, tables: [{ path, handle, rows, columns }] }",
				.Flags = CommandFlags::RequiresProject,
			},
			DataTables);

		RegisterOrDie(
			registry,
			CommandInfo{
				.Name = "data.get",
				.Summary = "Read a data table, one row or one cell",
				.Arguments = { StringArg("table", TableHelp).Require(),
		                       StringArg("row", "Only this row (its id, the first column)"),
		                       StringArg("column", "With 'row': only this cell"),
		                       IntegerArg("limit", "At most this many rows of a whole table")
		                           .WithRange(1, 100000)
		                           .WithDefault(1000),
		                       IntegerArg("offset", "Skip this many rows of a whole table")
		                           .WithMinimum(0)
		                           .WithDefault(0) },
				.ResultDescription = "{ table, columns, rowCount, rows: [{ id, values }] }, { table, row } or { table, "
									 "row, column, value }",
				.Flags = CommandFlags::RequiresProject,
			},
			DataGet);

		RegisterOrDie(
			registry,
			CommandInfo{
				.Name = "data.set",
				.Summary = "Change one cell of a data table",
				.Description =
					"The value is JSON of the column's type; text in the notation of the CSV file is accepted too.",
				.Arguments = { StringArg("table", TableHelp).Require(), StringArg("row", "The row id").Require(),
		                       StringArg("column", "The column name").Require(),
		                       AnyArg("value", "The new value, e.g. 12.5 or true or \"Pistol\"").Require() },
				.ResultDescription = "{ table, row, column, value, previous }",
				.Flags = CommandFlags::Mutating | CommandFlags::Undoable | CommandFlags::RequiresProject,
			},
			DataSet);

		RegisterOrDie(
			registry,
			CommandInfo{
				.Name = "data.add-row",
				.Summary = "Add a row to a data table",
				.Arguments = { StringArg("table", TableHelp).Require(),
		                       StringArg("row", "The id of the new row").Require(),
		                       ObjectArg(
								   "values",
								   "Cell values by column name: { \"Damage\": 12.5 }; the others are 0, false or "
								   "empty"),
		                       IntegerArg("index", "Position of the row (default: last)").WithMinimum(0) },
				.ResultDescription = "{ table, row: { id, values }, rowCount }",
				.Flags = CommandFlags::Mutating | CommandFlags::Undoable | CommandFlags::RequiresProject,
			},
			DataAddRow);

		RegisterOrDie(
			registry,
			CommandInfo{
				.Name = "data.remove-row",
				.Summary = "Remove a row from a data table",
				.Arguments = { StringArg("table", TableHelp).Require(), StringArg("row", "The row id").Require() },
				.ResultDescription = "{ table, removed, rowCount }",
				.Flags = CommandFlags::Mutating | CommandFlags::Undoable | CommandFlags::RequiresProject,
			},
			DataRemoveRow);
	}
}
