#include "gbpch.h"

#include "Gambit/Automation/BulkTable.h"

#include "Gambit/Asset/DataTable.h"
#include "Gambit/Automation/UndoOperations.h"
#include "Gambit/Serialization/FieldSerializer.h"

#include <charconv>
#include <cmath>
#include <set>

namespace Gambit
{
	namespace
	{
		constexpr std::string_view EntityColumn = "Entity";
		constexpr std::string_view EntityNameColumn = "EntityName";

		Error Invalid(std::string message)
		{
			return MakeCommandError(CommandErrorCode::InvalidArguments, std::move(message));
		}

		std::string JoinList(const std::vector<std::string>& names)
		{
			std::string joined;
			for (size_t index = 0; index < names.size(); ++index)
			{
				if (index == 12)
				{
					joined += std::format(", ... ({} more)", names.size() - index);
					break;
				}
				joined += index == 0 ? names[index] : ", " + names[index];
			}
			return joined;
		}

		std::vector<std::string> StoredFieldNames(const ComponentInfo& info)
		{
			std::vector<std::string> names;
			for (const FieldInfo& field : info.GetFields())
			{
				if (IsSerialized(field))
				{
					names.push_back(field.Name);
				}
			}
			return names;
		}

		const FieldInfo* FindStoredField(const ComponentInfo& info, std::string_view name)
		{
			const FieldInfo* field = info.FindField(name);
			return field != nullptr && IsSerialized(*field) ? field : nullptr;
		}

		Error UnknownField(const ComponentInfo& info, std::string_view name)
		{
			return Invalid(std::format(
				"component '{}' has no stored field '{}'; its fields are: {}", info.GetName(), name,
				JoinList(StoredFieldNames(info))));
		}

		// The entities that have the component in hierarchy pre-order.
		std::vector<Entity> CollectEntities(World& world, const ComponentInfo& info)
		{
			std::vector<Entity> found;
			std::vector<Entity> pending;
			const std::vector<Entity> roots = world.GetRoots();
			pending.assign(roots.rbegin(), roots.rend());
			while (!pending.empty())
			{
				const Entity entity = pending.back();
				pending.pop_back();
				if (info.Has(world, entity.GetHandle()))
				{
					found.push_back(entity);
				}
				const std::vector<Entity> children = world.GetChildren(entity);
				pending.insert(pending.end(), children.rbegin(), children.rend());
			}
			return found;
		}

		// ---- Columns of the CSV form ----

		// Vectors, colors and quaternions are spread over one column per component, so that spreadsheets can edit them.
		std::vector<std::string> PartNames(FieldType type)
		{
			switch (type)
			{
				case FieldType::Vec2:
					return { "X", "Y" };
				case FieldType::Vec3:
					return { "X", "Y", "Z" };
				case FieldType::Vec4:
				case FieldType::Quat:
					return { "X", "Y", "Z", "W" };
				case FieldType::Color3:
					return { "R", "G", "B" };
				case FieldType::Color4:
					return { "R", "G", "B", "A" };
				default:
					return {};
			}
		}

		DataColumnType CellType(FieldType type)
		{
			switch (type)
			{
				case FieldType::Bool:
					return DataColumnType::Bool;
				case FieldType::Int32:
				case FieldType::UInt32:
				case FieldType::Int64:
					return DataColumnType::Int;
				case FieldType::Float:
				case FieldType::Double:
				case FieldType::Vec2:
				case FieldType::Vec3:
				case FieldType::Vec4:
				case FieldType::Quat:
				case FieldType::Color3:
				case FieldType::Color4:
					return DataColumnType::Float;
				case FieldType::Entity:
					return DataColumnType::Entity;
				case FieldType::AssetHandle:
					return DataColumnType::Asset;
				default:
					return DataColumnType::String; // text, enums, 64-bit unsigned numbers, plain UUIDs, JSON
			}
		}

		struct CsvColumn
		{
			std::string Header;
			DataColumnType Type = DataColumnType::String;
			size_t Column = 0; // the column of the bulk table
			int Part = -1;     // the component of a vector; -1 for a cell that holds the whole value
		};

		std::vector<CsvColumn> BuildCsvColumns(std::span<const BulkColumn> columns)
		{
			std::vector<CsvColumn> csv;
			for (size_t index = 0; index < columns.size(); ++index)
			{
				const BulkColumn& column = columns[index];
				const std::vector<std::string> parts = PartNames(column.Type);
				if (parts.empty())
				{
					csv.push_back({ column.Name, CellType(column.Type), index, -1 });
					continue;
				}
				for (size_t part = 0; part < parts.size(); ++part)
				{
					csv.push_back(
						{ column.Name + "_" + parts[part], DataColumnType::Float, index, static_cast<int>(part) });
				}
			}
			return csv;
		}

		Result<FieldValue> ToCsvCell(const BulkColumn& column, const CsvColumn& csv, const Json& cell)
		{
			const auto bad = [&](std::string_view why) {
				return Invalid(std::format("column '{}': {}", csv.Header, why));
			};
			const Json* value = &cell;
			if (csv.Part >= 0)
			{
				if (!cell.is_array() || static_cast<size_t>(csv.Part) >= cell.size())
				{
					return bad("expected a vector of numbers");
				}
				value = &cell[static_cast<size_t>(csv.Part)];
			}
			switch (csv.Type)
			{
				case DataColumnType::Bool:
					return value->is_boolean() ? Result<FieldValue>(FieldValue(value->get<bool>()))
											   : Result<FieldValue>(bad("expected true or false"));
				case DataColumnType::Int:
				{
					Result<int64_t> integer = ReadJsonInteger<int64_t>(*value, {});
					return integer ? Result<FieldValue>(FieldValue(*integer))
								   : Result<FieldValue>(bad("expected an integer"));
				}
				case DataColumnType::Float:
					if (!value->is_number())
					{
						return bad("a table of numbers cannot hold NaN or infinity");
					}
					return FieldValue(value->get<double>());
				case DataColumnType::Entity:
				case DataColumnType::Asset:
				{
					const std::optional<UUID> uuid =
						value->is_string() ? UUID::Parse(value->get_ref<const std::string&>()) : std::nullopt;
					return uuid.has_value() ? Result<FieldValue>(FieldValue(*uuid))
											: Result<FieldValue>(bad("expected a UUID"));
				}
				case DataColumnType::String:
					break;
			}
			if (value->is_string())
			{
				return FieldValue(value->get<std::string>());
			}
			if (column.Type == FieldType::UInt64 && value->is_number_unsigned())
			{
				return FieldValue(std::to_string(value->get<uint64_t>()));
			}
			try
			{
				return FieldValue(value->dump()); // structs and arrays as one line of JSON
			}
			catch (const std::exception& exception)
			{
				return bad(exception.what());
			}
		}

		std::optional<double> AsDouble(const FieldValue& cell)
		{
			if (const auto* integer = std::get_if<int64_t>(&cell))
			{
				return static_cast<double>(*integer);
			}
			if (const auto* number = std::get_if<double>(&cell))
			{
				return *number;
			}
			return std::nullopt;
		}

		// The JSON of a field value from a cell of the CSV (the cell types come from the header or are inferred).
		Result<Json> FromCsvCell(const BulkColumn& column, DataColumnType cellType, const FieldValue& cell)
		{
			const auto bad = [&](std::string_view expected) {
				return Invalid(std::format("column '{}': expected {}", column.Name, expected));
			};
			switch (column.Type)
			{
				case FieldType::Bool:
					return std::holds_alternative<bool>(cell) ? Result<Json>(Json(std::get<bool>(cell)))
															  : Result<Json>(bad("true or false"));
				case FieldType::Int32:
				case FieldType::UInt32:
				case FieldType::Int64:
				{
					const std::optional<double> number = AsDouble(cell);
					if (!number.has_value() || *number != std::floor(*number))
					{
						return bad("an integer");
					}
					return std::holds_alternative<int64_t>(cell) ? Json(std::get<int64_t>(cell)) : Json(*number);
				}
				case FieldType::UInt64:
				{
					if (const auto* text = std::get_if<std::string>(&cell))
					{
						uint64_t value = 0;
						const auto [end, error] = std::from_chars(text->data(), text->data() + text->size(), value);
						if (error != std::errc{} || end != text->data() + text->size())
						{
							return bad("a whole number of at least 0");
						}
						return Json(value);
					}
					if (const auto* integer = std::get_if<int64_t>(&cell); integer != nullptr && *integer >= 0)
					{
						return Json(static_cast<uint64_t>(*integer));
					}
					return bad("a whole number of at least 0");
				}
				case FieldType::Float:
				case FieldType::Double:
				{
					const std::optional<double> number = AsDouble(cell);
					return number.has_value() ? Result<Json>(Json(*number)) : Result<Json>(bad("a number"));
				}
				case FieldType::Entity:
				case FieldType::AssetHandle:
				case FieldType::UUID:
					if (const auto* uuid = std::get_if<UUID>(&cell))
					{
						return Json(uuid->ToString());
					}
					return std::holds_alternative<std::string>(cell) ? Result<Json>(Json(std::get<std::string>(cell)))
																	 : Result<Json>(bad("a UUID"));
				case FieldType::Struct:
				case FieldType::Array:
				{
					const auto* text = std::get_if<std::string>(&cell);
					if (text == nullptr || text->empty())
					{
						return bad("JSON text");
					}
					Result<Json> parsed = ParseJson(*text);
					return parsed ? std::move(parsed)
								  : Result<Json>(bad(std::format("JSON text ({})", parsed.GetError().Message)));
				}
				default:
					break;
			}
			// Text and enum names; a column of numbers that holds names (an untyped column) is read as text.
			return std::holds_alternative<std::string>(cell) ? Json(std::get<std::string>(cell))
															 : Json(DataTable::FormatValue(cellType, cell));
		}
	}

	// ---- Reading ----

	Result<BulkTable> ReadBulkTable(
		World& world, const ComponentInfo& component, std::span<const std::string> fields, size_t offset, size_t limit)
	{
		for (const std::string& name : fields)
		{
			if (FindStoredField(component, name) == nullptr)
			{
				return UnknownField(component, name);
			}
		}
		BulkTable table;
		table.Component = component.GetName();
		std::vector<const FieldInfo*> chosen;
		for (const FieldInfo& field : component.GetFields())
		{
			if (IsSerialized(field) && (fields.empty() || std::ranges::find(fields, field.Name) != fields.end()))
			{
				chosen.push_back(&field);
				table.Columns.push_back({ field.Name, field.Type, field.HasFlag(FieldFlags::ReadOnly) });
			}
		}

		const std::vector<Entity> entities = CollectEntities(world, component);
		table.Total = entities.size();
		for (size_t index = offset; index < entities.size() && table.Rows.size() < limit; ++index)
		{
			const Entity entity = entities[index];
			const void* raw = component.GetRaw(world, entity.GetHandle());
			BulkRow row;
			row.Entity = entity.GetUUID();
			row.Name = entity.GetName();
			for (const FieldInfo* field : chosen)
			{
				Result<Json> value = FieldValueToJson(*field, field->GetValue(raw));
				if (!value)
				{
					return value.GetError();
				}
				row.Values.push_back(std::move(*value));
			}
			table.Rows.push_back(std::move(row));
		}
		return table;
	}

	Json BulkTableToJson(const BulkTable& table)
	{
		Json json = Json::object();
		json["component"] = table.Component;
		Json columns = Json::array();
		for (const BulkColumn& column : table.Columns)
		{
			Json entry = Json::object();
			entry["name"] = column.Name;
			entry["type"] = std::string(ToString(column.Type));
			entry["readOnly"] = column.ReadOnly;
			columns.push_back(std::move(entry));
		}
		json["columns"] = std::move(columns);
		Json rows = Json::array();
		for (const BulkRow& row : table.Rows)
		{
			Json entry = Json::object();
			entry["entity"] = row.Entity.ToString();
			entry["name"] = row.Name;
			entry["values"] = row.Values;
			rows.push_back(std::move(entry));
		}
		json["rows"] = std::move(rows);
		json["total"] = table.Total;
		return json;
	}

	Result<BulkTable> BulkTableFromJson(const ComponentInfo& component, const Json& json)
	{
		if (!json.is_object() || !json.contains("columns") || !json.contains("rows"))
		{
			return Invalid("a table is an object with \"columns\" and \"rows\" (the shape bulk.get returns)");
		}
		const Json& columns = json.at("columns");
		const Json& rows = json.at("rows");
		if (!columns.is_array() || !rows.is_array())
		{
			return Invalid("\"columns\" and \"rows\" of a table are arrays");
		}

		BulkTable table;
		table.Component = component.GetName();
		for (size_t index = 0; index < columns.size(); ++index)
		{
			const Json& entry = columns[index];
			const Json* name = entry.is_string()
				? &entry
				: (entry.is_object() && entry.contains("name") ? &entry.at("name") : nullptr);
			if (name == nullptr || !name->is_string())
			{
				return Invalid(std::format("column {} must be a field name or an object with a \"name\"", index));
			}
			const FieldInfo* field = FindStoredField(component, name->get_ref<const std::string&>());
			if (field == nullptr)
			{
				return UnknownField(component, name->get_ref<const std::string&>());
			}
			if (std::ranges::find(table.Columns, field->Name, &BulkColumn::Name) != table.Columns.end())
			{
				return Invalid(std::format("column '{}' appears twice", field->Name));
			}
			table.Columns.push_back({ field->Name, field->Type, field->HasFlag(FieldFlags::ReadOnly) });
		}
		for (size_t index = 0; index < rows.size(); ++index)
		{
			const Json& entry = rows[index];
			if (!entry.is_object() || !entry.contains("entity") || !entry.at("entity").is_string()
			    || !entry.contains("values") || !entry.at("values").is_array())
			{
				return Invalid(std::format("row {} must be {{ \"entity\": \"0x...\", \"values\": [...] }}", index));
			}
			const std::optional<UUID> entity = UUID::Parse(entry.at("entity").get_ref<const std::string&>());
			if (!entity.has_value() || !entity->IsValid())
			{
				return Invalid(std::format(
					"row {}: '{}' is not an entity UUID", index, entry.at("entity").get_ref<const std::string&>()));
			}
			const Json& values = entry.at("values");
			if (values.size() != table.Columns.size())
			{
				return Invalid(
					std::format("row {}: {} values for {} columns", index, values.size(), table.Columns.size()));
			}
			BulkRow row;
			row.Entity = *entity;
			row.Values.assign(values.begin(), values.end());
			table.Rows.push_back(std::move(row));
		}
		table.Total = table.Rows.size();
		return table;
	}

	// ---- CSV ----

	Result<std::string> BulkTableToCsv(const ComponentInfo& component, const BulkTable& table)
	{
		const std::vector<CsvColumn> csvColumns = BuildCsvColumns(table.Columns);
		std::set<std::string_view> headers{ EntityColumn, EntityNameColumn };
		std::vector<DataColumn> columns;
		columns.push_back({ std::string(EntityColumn), DataColumnType::String, false });
		columns.push_back({ std::string(EntityNameColumn), DataColumnType::String, true });
		for (const CsvColumn& column : csvColumns)
		{
			if (!headers.insert(column.Header).second)
			{
				return Invalid(std::format("the column name '{}' is used twice or is reserved", column.Header));
			}
			columns.push_back({ column.Header, column.Type, true });
		}
		Result<Ref<DataTable>> csv = DataTable::Create(std::move(columns));
		if (!csv)
		{
			return Invalid(csv.GetError().Message);
		}

		for (const BulkRow& row : table.Rows)
		{
			std::vector<FieldValue> cells;
			cells.reserve(csvColumns.size() + 1);
			cells.emplace_back(std::in_place_type<std::string>, row.Name);
			for (const CsvColumn& column : csvColumns)
			{
				Result<FieldValue> cell = ToCsvCell(table.Columns[column.Column], column, row.Values[column.Column]);
				if (!cell)
				{
					return AddSource(std::format("{} {}", component.GetName(), row.Entity.ToString()), cell.GetError());
				}
				cells.push_back(std::move(*cell));
			}
			if (Result<void> added = (*csv)->AddRow(row.Entity.ToString(), cells); !added)
			{
				return Invalid(std::format("{}: {}", row.Entity.ToString(), added.GetError().Message));
			}
		}
		return (*csv)->ToCsv();
	}

	Result<BulkTable> BulkTableFromCsv(const ComponentInfo& component, std::string_view csv, std::string_view source)
	{
		Result<Ref<DataTable>> parsed = DataTable::Parse(csv, source);
		if (!parsed)
		{
			return Invalid(parsed.GetError().Message);
		}
		const DataTable& data = **parsed;
		const std::span<const DataColumn> headers = data.GetColumns();
		if (headers.front().Name != EntityColumn)
		{
			return Invalid(std::format(
				"the first column of the CSV must be '{}', found '{}'", EntityColumn, headers.front().Name));
		}

		// The headers that name a field (or a component of a vector field).
		std::vector<BulkColumn> fieldColumns;
		for (const FieldInfo& field : component.GetFields())
		{
			if (IsSerialized(field))
			{
				fieldColumns.push_back({ field.Name, field.Type, field.HasFlag(FieldFlags::ReadOnly) });
			}
		}
		const std::vector<CsvColumn> known = BuildCsvColumns(fieldColumns);

		BulkTable table;
		table.Component = component.GetName();
		struct Source
		{
			std::vector<size_t> Cells; // per part: the column of the CSV
		};
		std::vector<Source> sources; // parallel to table.Columns
		std::vector<std::string> headerNames;
		for (size_t index = 1; index < headers.size(); ++index)
		{
			const std::string& header = headers[index].Name;
			if (header == EntityNameColumn)
			{
				continue;
			}
			const auto match = std::ranges::find(known, header, &CsvColumn::Header);
			if (match == known.end())
			{
				std::vector<std::string> names;
				for (const CsvColumn& column : known)
				{
					names.push_back(column.Header);
				}
				return Invalid(std::format(
					"the CSV has a column '{}' that is no field of '{}'; its columns are: {}", header,
					component.GetName(), JoinList(names)));
			}
			const BulkColumn& field = fieldColumns[match->Column];
			auto existing = std::ranges::find(table.Columns, field.Name, &BulkColumn::Name);
			if (existing == table.Columns.end())
			{
				table.Columns.push_back(field);
				sources.push_back({ std::vector<size_t>(std::max<size_t>(PartNames(field.Type).size(), 1), SIZE_MAX) });
				existing = table.Columns.end() - 1;
			}
			sources[static_cast<size_t>(existing - table.Columns.begin())]
				.Cells[match->Part < 0 ? 0 : static_cast<size_t>(match->Part)] = index;
		}
		for (size_t column = 0; column < table.Columns.size(); ++column)
		{
			if (std::ranges::find(sources[column].Cells, SIZE_MAX) != sources[column].Cells.end())
			{
				const std::vector<std::string> parts = PartNames(table.Columns[column].Type);
				std::string names;
				for (const std::string& part : parts)
				{
					names += (names.empty() ? "" : ", ") + table.Columns[column].Name + "_" + part;
				}
				return Invalid(
					std::format("the field '{}' needs all of the columns {}", table.Columns[column].Name, names));
			}
		}

		for (const DataRow& row : data.GetRows())
		{
			const std::optional<UUID> entity = UUID::Parse(row.Id);
			if (!entity.has_value() || !entity->IsValid())
			{
				return Invalid(std::format("'{}' in the Entity column is not an entity UUID", row.Id));
			}
			BulkRow bulk;
			bulk.Entity = *entity;
			for (size_t column = 0; column < table.Columns.size(); ++column)
			{
				const BulkColumn& field = table.Columns[column];
				const std::vector<size_t>& cells = sources[column].Cells;
				if (PartNames(field.Type).empty())
				{
					Result<Json> value = FromCsvCell(field, headers[cells.front()].Type, row.Values[cells.front()]);
					if (!value)
					{
						return AddSource(std::format("row {}", row.Id), value.GetError());
					}
					bulk.Values.push_back(std::move(*value));
					continue;
				}
				Json vector = Json::array();
				for (const size_t cell : cells)
				{
					const std::optional<double> number = AsDouble(row.Values[cell]);
					if (!number.has_value())
					{
						return Invalid(
							std::format("row {}: column '{}': expected a number", row.Id, headers[cell].Name));
					}
					vector.push_back(*number);
				}
				bulk.Values.push_back(std::move(vector));
			}
			table.Rows.push_back(std::move(bulk));
		}
		table.Total = table.Rows.size();
		return table;
	}

	// ---- Applying ----

	Result<size_t> ApplyBulkTable(
		EditorContext& context, SceneDocument& scene, const ComponentInfo& component, const BulkTable& table,
		std::string_view mergeKey)
	{
		World& world = *scene.Scene;
		struct RowEdit
		{
			UUID Entity;
			std::vector<FieldData> Edits;
		};
		std::vector<RowEdit> plan;
		LoadReport report;

		for (size_t rowIndex = 0; rowIndex < table.Rows.size(); ++rowIndex)
		{
			const BulkRow& row = table.Rows[rowIndex];
			const std::string where = std::format("row {} ({})", rowIndex, row.Entity.ToString());
			const Entity entity = world.TryGetEntity(row.Entity);
			if (!entity.IsValid())
			{
				return MakeCommandError(
					CommandErrorCode::NotFound, std::format("{}: there is no such entity in the scene", where));
			}
			const void* current = component.GetRaw(world, entity.GetHandle());
			if (current == nullptr)
			{
				return MakeCommandError(
					CommandErrorCode::NotFound,
					std::format(
						"{}: the entity '{}' has no component '{}'", where, entity.GetName(), component.GetName()));
			}

			RowEdit edit;
			edit.Entity = row.Entity;
			for (size_t column = 0; column < table.Columns.size(); ++column)
			{
				const FieldInfo* field = FindStoredField(component, table.Columns[column].Name);
				if (field == nullptr)
				{
					return UnknownField(component, table.Columns[column].Name);
				}
				const FieldValue now = field->GetValue(current);
				Result<FieldValue> merged = FieldValueMergeFromJson(
					*field, now, row.Values[column], JsonPathAppend({}, field->Name), report, MergeAccess::Tool);
				if (!merged)
				{
					return Invalid(std::format("{}: {}", where, merged.GetError().Message));
				}
				if (*merged == now)
				{
					continue;
				}
				if (field->HasFlag(FieldFlags::ReadOnly))
				{
					return Invalid(std::format("{}: the field '{}' is read-only", where, field->Name));
				}
				edit.Edits.push_back({ field->Name, std::move(*merged) });
			}
			if (!report.IsEmpty())
			{
				const Diagnostic& first = report.GetWarnings().front();
				return Invalid(std::format("{}: {}: {}", where, first.Path, first.Message));
			}
			if (!edit.Edits.empty())
			{
				plan.push_back(std::move(edit));
			}
		}

		for (RowEdit& edit : plan)
		{
			const Entity entity = world.TryGetEntity(edit.Entity);
			if (Result<void> done = context.Do(
					CreateScope<SetFieldsOperation>(
						scene.Path, edit.Entity, component.GetName(), std::move(edit.Edits),
						std::format("Edit {} of '{}'", component.GetName(), entity.GetName())),
					mergeKey);
			    !done)
			{
				return done.GetError();
			}
		}
		return plan.size();
	}
}
