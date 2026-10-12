#include "gbpch.h"

#include "Gambit/Asset/DataTable.h"

#include "Gambit/Reflection/TypeRegistry.h"
#include "Gambit/Serialization/Json.h"

#include <charconv>
#include <cmath>
#include <set>
#include <sstream>

namespace Gambit
{
	namespace
	{
		constexpr std::string_view Utf8Bom = "\xEF\xBB\xBF";
		constexpr std::string_view TypeNames = "int, float, bool, string, asset or entity";

		// ---- Diagnostics ----

		Error CsvError(std::string_view source, size_t line, size_t column, std::string_view message)
		{
			std::string position = std::format("line {}", line);
			if (column > 0)
			{
				position += std::format(", column {}", column);
			}
			return Error(
				source.empty() ? std::format("{}: {}", position, message)
							   : std::format("{}: {}: {}", source, position, message));
		}

		// ---- Cells ----

		bool EqualsIgnoringCase(std::string_view text, std::string_view lowerCase) noexcept
		{
			return text.size() == lowerCase.size()
				&& std::equal(text.begin(), text.end(), lowerCase.begin(), [](char a, char b) {
					   return (a >= 'A' && a <= 'Z' ? static_cast<char>(a - 'A' + 'a') : a) == b;
				   });
		}

		bool IsDigit(char character) noexcept
		{
			return character >= '0' && character <= '9';
		}

		// [+-]? ( digits+ [. digits*] | . digits+ ) ( [eE] [+-]? digits+ )?
		bool HasFloatSyntax(std::string_view text) noexcept
		{
			size_t index = 0;
			if (index < text.size() && (text[index] == '+' || text[index] == '-'))
			{
				++index;
			}
			size_t integerDigits = 0;
			while (index < text.size() && IsDigit(text[index]))
			{
				++index;
				++integerDigits;
			}
			size_t fractionDigits = 0;
			if (index < text.size() && text[index] == '.')
			{
				++index;
				while (index < text.size() && IsDigit(text[index]))
				{
					++index;
					++fractionDigits;
				}
			}
			if (integerDigits == 0 && fractionDigits == 0)
			{
				return false;
			}
			if (index < text.size() && (text[index] == 'e' || text[index] == 'E'))
			{
				++index;
				if (index < text.size() && (text[index] == '+' || text[index] == '-'))
				{
					++index;
				}
				size_t exponentDigits = 0;
				while (index < text.size() && IsDigit(text[index]))
				{
					++index;
					++exponentDigits;
				}
				if (exponentDigits == 0)
				{
					return false;
				}
			}
			return index == text.size();
		}

		// An integer written the way FormatValue writes it: it reads back as the same text.
		bool IsCanonicalInt(std::string_view text) noexcept
		{
			if (text.empty() || text == "-0")
			{
				return false;
			}
			const std::string_view digits = text.front() == '-' ? text.substr(1) : text;
			if (digits.empty() || (digits.size() > 1 && digits.front() == '0'))
			{
				return false;
			}
			return std::ranges::all_of(digits, IsDigit);
		}

		Result<int64_t> ParseInt(std::string_view text)
		{
			std::string_view digits = text;
			if (!digits.empty() && digits.front() == '+')
			{
				digits.remove_prefix(1);
			}
			int64_t value = 0;
			const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
			if (digits.empty() || error == std::errc::invalid_argument || end != digits.data() + digits.size())
			{
				return Error::Format("'{}' is not a valid int", text);
			}
			if (error == std::errc::result_out_of_range)
			{
				return Error::Format("'{}' is out of the range of an int (64 bits)", text);
			}
			return value;
		}

		Result<double> ParseFloat(std::string_view text)
		{
			if (!HasFloatSyntax(text))
			{
				return Error::Format("'{}' is not a valid float", text);
			}
			// Not from_chars, which not every standard library has for floating point, and not strtod, which follows
			// the C locale: a stream with the classic locale reads "1.5" the same way everywhere.
			std::istringstream stream{ std::string(text) };
			stream.imbue(std::locale::classic());
			double value = 0.0;
			stream >> value;
			if (stream.fail() || !std::isfinite(value))
			{
				return Error::Format("'{}' is out of the range of a float", text);
			}
			return value;
		}

		Result<bool> ParseBool(std::string_view text)
		{
			if (EqualsIgnoringCase(text, "true"))
			{
				return true;
			}
			if (EqualsIgnoringCase(text, "false"))
			{
				return false;
			}
			return Error::Format("'{}' is not a valid bool (expected true or false)", text);
		}

		Result<UUID> ParseUuid(std::string_view text)
		{
			if (text.empty())
			{
				return UUID();
			}
			const std::optional<UUID> uuid = UUID::Parse(text);
			if (!uuid.has_value())
			{
				return Error::Format("'{}' is not a UUID (expected \"0x\" and up to 16 hexadecimal digits)", text);
			}
			return *uuid;
		}

		// The shortest text that reads back as the same double, always recognizable as a float ("2.0", not "2"), so
		// that a column of whole numbers keeps its type when it is read again without a declared type.
		std::string FormatFloat(double value)
		{
			std::array<char, 64> buffer{};
			const auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
			GB_CORE_ASSERT(error == std::errc{}, "a double needs more than {} characters", buffer.size());
			std::string text(buffer.data(), end);
			if (text.find_first_of(".eE") == std::string::npos)
			{
				text += ".0";
			}
			return text;
		}

		DataColumnType InferType(const std::vector<std::string_view>& cells)
		{
			const auto all = [&](auto&& predicate) { return !cells.empty() && std::ranges::all_of(cells, predicate); };
			if (all([](std::string_view cell) { return IsCanonicalInt(cell) && ParseInt(cell).HasValue(); }))
			{
				return DataColumnType::Int;
			}
			if (all([](std::string_view cell) {
					return EqualsIgnoringCase(cell, "true") || EqualsIgnoringCase(cell, "false");
				}))
			{
				return DataColumnType::Bool;
			}
			// Whole numbers that are not canonical ("007", "+5") stay text: reading them as numbers would change them.
			const auto isFloat = [](std::string_view cell) {
				return (IsCanonicalInt(cell) || cell.find_first_of(".eE") != std::string_view::npos)
					&& ParseFloat(cell).HasValue();
			};
			if (all(isFloat))
			{
				return DataColumnType::Float;
			}
			return DataColumnType::String;
		}

		// ---- CSV text ----

		struct CsvField
		{
			std::string Text;
			size_t Column = 0; // 1-based, in characters of its line
		};

		struct CsvRecord
		{
			size_t Line = 0;
			std::vector<CsvField> Fields;
		};

		struct CsvDocument
		{
			std::vector<CsvRecord> Records;
			bool HasBom = false;
			bool UsesCrLf = false;
		};

		// RFC 4180, leniently: blank lines are skipped and the last line needs no line break; strictly: a quote inside
		// an unquoted field, text after a closing quote, a quote that never closes and a lone carriage return are
		// errors.
		Result<CsvDocument> ParseCsv(std::string_view text, std::string_view source)
		{
			CsvDocument document;
			size_t position = 0;
			if (text.starts_with(Utf8Bom))
			{
				document.HasBom = true;
				position = Utf8Bom.size();
			}
			const size_t firstBreak = text.find('\n');
			document.UsesCrLf = firstBreak != std::string_view::npos && firstBreak > 0 && text[firstBreak - 1] == '\r';

			size_t line = 1;
			size_t lineStart = position;
			while (position < text.size())
			{
				CsvRecord record;
				record.Line = line;
				for (;;)
				{
					CsvField field;
					field.Column = position - lineStart + 1;
					bool quoted = false;
					if (position < text.size() && text[position] == '"')
					{
						quoted = true;
						const size_t startLine = line;
						const size_t startColumn = field.Column;
						++position;
						for (;;)
						{
							if (position >= text.size())
							{
								return CsvError(source, startLine, startColumn, "the quoted field is never closed");
							}
							const char character = text[position];
							if (character == '"')
							{
								if (position + 1 < text.size() && text[position + 1] == '"')
								{
									field.Text += '"';
									position += 2;
									continue;
								}
								++position;
								break;
							}
							if (character == '\n')
							{
								++line;
								lineStart = position + 1;
							}
							field.Text += character;
							++position;
						}
						if (position < text.size() && text[position] != ',' && text[position] != '\n'
						    && text[position] != '\r')
						{
							return CsvError(
								source, line, position - lineStart + 1,
								"unexpected text after the closing quote of a field");
						}
					}
					else
					{
						while (position < text.size() && text[position] != ',' && text[position] != '\n'
						       && text[position] != '\r')
						{
							if (text[position] == '"')
							{
								return CsvError(
									source, line, position - lineStart + 1,
									"a quote inside an unquoted field: quote the whole field and double the quote");
							}
							field.Text += text[position];
							++position;
						}
					}
					const bool blankLine = !quoted && field.Text.empty() && record.Fields.empty();
					record.Fields.push_back(std::move(field));

					if (position >= text.size())
					{
						break;
					}
					if (text[position] == ',')
					{
						++position;
						if (position >= text.size())
						{
							CsvField last;
							last.Column = position - lineStart + 1;
							record.Fields.push_back(std::move(last)); // a trailing comma ends with an empty field
							break;
						}
						continue;
					}
					if (text[position] == '\r')
					{
						if (position + 1 >= text.size() || text[position + 1] != '\n')
						{
							return CsvError(
								source, line, position - lineStart + 1, "a carriage return without a line feed");
						}
						++position;
					}
					++position; // the line feed
					++line;
					lineStart = position;
					if (blankLine)
					{
						record.Fields.clear(); // an empty line is not a record
					}
					break;
				}
				if (!record.Fields.empty())
				{
					document.Records.push_back(std::move(record));
				}
			}
			return document;
		}

		bool NeedsQuotes(std::string_view field) noexcept
		{
			return field.find_first_of(",\"\r\n") != std::string_view::npos;
		}

		void AppendField(std::string& line, std::string_view field)
		{
			if (!NeedsQuotes(field))
			{
				line += field;
				return;
			}
			line += '"';
			for (const char character : field)
			{
				if (character == '"')
				{
					line += '"';
				}
				line += character;
			}
			line += '"';
		}
	}

	std::string_view ToString(DataColumnType type) noexcept
	{
		switch (type)
		{
			case DataColumnType::Int:
				return "int";
			case DataColumnType::Float:
				return "float";
			case DataColumnType::Bool:
				return "bool";
			case DataColumnType::String:
				return "string";
			case DataColumnType::Asset:
				return "asset";
			case DataColumnType::Entity:
				return "entity";
		}
		return "unknown";
	}

	std::optional<DataColumnType> ParseDataColumnType(std::string_view name) noexcept
	{
		for (const DataColumnType type : { DataColumnType::Int, DataColumnType::Float, DataColumnType::Bool,
		                                   DataColumnType::String, DataColumnType::Asset, DataColumnType::Entity })
		{
			if (EqualsIgnoringCase(name, ToString(type)))
			{
				return type;
			}
		}
		return std::nullopt;
	}

	FieldType ToFieldType(DataColumnType type) noexcept
	{
		switch (type)
		{
			case DataColumnType::Int:
				return FieldType::Int64;
			case DataColumnType::Float:
				return FieldType::Double;
			case DataColumnType::Bool:
				return FieldType::Bool;
			case DataColumnType::String:
				return FieldType::String;
			case DataColumnType::Asset:
				return FieldType::AssetHandle;
			case DataColumnType::Entity:
				return FieldType::Entity;
		}
		return FieldType::None;
	}

	Result<FieldValue> DataTable::ParseValue(DataColumnType type, std::string_view text)
	{
		const bool blank = text.empty();
		switch (type)
		{
			case DataColumnType::Int:
			{
				Result<int64_t> value = blank ? Result<int64_t>(int64_t{ 0 }) : ParseInt(text);
				if (!value)
				{
					return value.GetError();
				}
				return FieldValue(std::in_place_type<int64_t>, *value);
			}
			case DataColumnType::Float:
			{
				Result<double> value = blank ? Result<double>(0.0) : ParseFloat(text);
				if (!value)
				{
					return value.GetError();
				}
				return FieldValue(std::in_place_type<double>, *value);
			}
			case DataColumnType::Bool:
			{
				Result<bool> value = blank ? Result<bool>(false) : ParseBool(text);
				if (!value)
				{
					return value.GetError();
				}
				return FieldValue(std::in_place_type<bool>, *value);
			}
			case DataColumnType::String:
				return FieldValue(std::in_place_type<std::string>, std::string(text));
			case DataColumnType::Asset:
			case DataColumnType::Entity:
			{
				Result<UUID> value = ParseUuid(text);
				if (!value)
				{
					return value.GetError();
				}
				return FieldValue(std::in_place_type<UUID>, *value);
			}
		}
		return Error("unknown column type");
	}

	std::string DataTable::FormatValue(DataColumnType type, const FieldValue& value)
	{
		switch (type)
		{
			case DataColumnType::Int:
				return std::to_string(std::get<int64_t>(value));
			case DataColumnType::Float:
				return FormatFloat(std::get<double>(value));
			case DataColumnType::Bool:
				return std::get<bool>(value) ? "true" : "false";
			case DataColumnType::String:
				return std::get<std::string>(value);
			case DataColumnType::Asset:
			case DataColumnType::Entity:
			{
				const UUID uuid = std::get<UUID>(value);
				return uuid.IsValid() ? uuid.ToString() : std::string();
			}
		}
		return {};
	}

	Result<void> DataTable::ValidateColumns(std::span<const DataColumn> columns)
	{
		if (columns.empty())
		{
			return Error("a table needs at least one column, the row ids");
		}
		if (columns.size() > MaxColumns)
		{
			return Error::Format("a table can have at most {} columns", MaxColumns);
		}
		std::set<std::string_view> names;
		for (const DataColumn& column : columns)
		{
			if (!TypeRegistry::IsValidName(column.Name))
			{
				return Error::Format(
					"'{}' is not a valid column name (letters, digits and underscores, not starting with a digit)",
					column.Name);
			}
			if (!names.insert(column.Name).second)
			{
				return Error::Format("column '{}' appears twice", column.Name);
			}
		}
		if (columns.front().Type != DataColumnType::String)
		{
			return Error::Format(
				"the first column, '{}', holds the row ids and must be a string column", columns.front().Name);
		}
		return {};
	}

	Result<Ref<DataTable>> DataTable::Create(std::vector<DataColumn> columns)
	{
		if (Result<void> valid = ValidateColumns(columns); !valid)
		{
			return valid.GetError();
		}
		auto table = CreateRef<DataTable>();
		table->m_Columns = std::move(columns);
		return table;
	}

	Result<Ref<DataTable>> DataTable::Parse(std::string_view text, std::string_view source)
	{
		Result<CsvDocument> csv = ParseCsv(text, source);
		if (!csv)
		{
			return csv.GetError();
		}
		if (csv->Records.empty())
		{
			return CsvError(source, 1, 0, "the table has no header row");
		}

		// ---- Header ----
		const CsvRecord& header = csv->Records.front();
		std::vector<DataColumn> columns;
		for (size_t index = 0; index < header.Fields.size(); ++index)
		{
			const CsvField& cell = header.Fields[index];
			std::string_view name = cell.Text;
			const auto trim = [](std::string_view value) {
				const size_t first = value.find_first_not_of(" \t");
				const size_t last = value.find_last_not_of(" \t");
				return first == std::string_view::npos ? std::string_view() : value.substr(first, last - first + 1);
			};
			DataColumn column;
			if (const size_t colon = name.rfind(':'); colon != std::string_view::npos)
			{
				const std::string_view typeName = trim(name.substr(colon + 1));
				const std::optional<DataColumnType> type = ParseDataColumnType(typeName);
				if (!type.has_value())
				{
					return CsvError(
						source, header.Line, cell.Column,
						std::format(
							"column '{}': unknown type '{}' (expected {})", trim(name.substr(0, colon)), typeName,
							TypeNames));
				}
				column.Type = *type;
				column.TypeDeclared = true;
				name = name.substr(0, colon);
			}
			column.Name = std::string(trim(name));
			if (index == 0 && column.TypeDeclared && column.Type != DataColumnType::String)
			{
				return CsvError(
					source, header.Line, cell.Column,
					std::format(
						"the first column, '{}', holds the row ids and cannot be {}", column.Name,
						ToString(column.Type)));
			}
			columns.push_back(std::move(column));
		}
		if (Result<void> valid = ValidateColumns(columns); !valid)
		{
			return CsvError(source, header.Line, 0, valid.GetError().Message);
		}

		// ---- Cells: one pass for the shape, one for the types ----
		const size_t rowCount = csv->Records.size() - 1;
		for (size_t row = 1; row < csv->Records.size(); ++row)
		{
			const CsvRecord& record = csv->Records[row];
			if (record.Fields.size() != columns.size())
			{
				return CsvError(
					source, record.Line, 0,
					std::format(
						"expected {} fields (the header has {} columns), found {}", columns.size(), columns.size(),
						record.Fields.size()));
			}
		}
		for (size_t column = 1; column < columns.size(); ++column)
		{
			if (columns[column].TypeDeclared)
			{
				continue;
			}
			std::vector<std::string_view> cells;
			cells.reserve(rowCount);
			for (size_t row = 1; row < csv->Records.size(); ++row)
			{
				cells.push_back(csv->Records[row].Fields[column].Text);
			}
			columns[column].Type = InferType(cells);
		}

		auto table = CreateRef<DataTable>();
		table->m_Columns = std::move(columns);
		table->m_HasBom = csv->HasBom;
		table->m_UsesCrLf = csv->UsesCrLf;
		std::unordered_map<std::string, size_t> firstLine; // row id to the line that has it; lookups only
		for (size_t row = 1; row < csv->Records.size(); ++row)
		{
			const CsvRecord& record = csv->Records[row];
			const CsvField& idCell = record.Fields[0];
			if (idCell.Text.empty())
			{
				return CsvError(source, record.Line, idCell.Column, "the row id is empty");
			}
			if (const auto earlier = firstLine.find(idCell.Text); earlier != firstLine.end())
			{
				return CsvError(
					source, record.Line, idCell.Column,
					std::format("duplicate row id '{}' (first used on line {})", idCell.Text, earlier->second));
			}
			firstLine.emplace(idCell.Text, record.Line);

			DataRow data;
			data.Id = idCell.Text;
			data.Values.reserve(table->m_Columns.size());
			for (size_t column = 0; column < table->m_Columns.size(); ++column)
			{
				const CsvField& cell = record.Fields[column];
				Result<FieldValue> value = ParseValue(table->m_Columns[column].Type, cell.Text);
				if (!value)
				{
					return CsvError(
						source, record.Line, cell.Column,
						std::format("column '{}': {}", table->m_Columns[column].Name, value.GetError().Message));
				}
				data.Values.push_back(std::move(*value));
			}
			table->m_Rows.push_back(std::move(data));
		}
		table->RebuildIndex();
		return table;
	}

	Result<Ref<DataTable>> DataTable::Load(const VFS& vfs, std::string_view path)
	{
		Result<Buffer> bytes = vfs.ReadBinary(path);
		if (!bytes)
		{
			return AddSource(path, bytes.GetError());
		}
		// Not ReadText: the byte order mark has to reach the parser so that Save can write it back.
		return Parse(std::string_view(reinterpret_cast<const char*>(bytes->Data()), bytes->Size()), path);
	}

	std::string DataTable::ToCsv() const
	{
		const std::string_view lineEnd = m_UsesCrLf ? "\r\n" : "\n";
		std::string text;
		if (m_HasBom)
		{
			text += Utf8Bom;
		}
		for (size_t column = 0; column < m_Columns.size(); ++column)
		{
			if (column > 0)
			{
				text += ',';
			}
			std::string name = m_Columns[column].Name;
			if (m_Columns[column].TypeDeclared)
			{
				name += ':';
				name += ToString(m_Columns[column].Type);
			}
			AppendField(text, name);
		}
		text += lineEnd;
		for (const DataRow& row : m_Rows)
		{
			for (size_t column = 0; column < m_Columns.size(); ++column)
			{
				if (column > 0)
				{
					text += ',';
				}
				AppendField(text, FormatValue(m_Columns[column].Type, row.Values[column]));
			}
			text += lineEnd;
		}
		return text;
	}

	Result<void> DataTable::Save(VFS& vfs, std::string_view path) const
	{
		const std::string text = ToCsv();
		return vfs.Write(path, BufferView::FromText(text));
	}

	std::vector<AssetHandle> DataTable::GetDependencies() const
	{
		std::set<AssetHandle> handles;
		for (size_t column = 0; column < m_Columns.size(); ++column)
		{
			if (m_Columns[column].Type != DataColumnType::Asset)
			{
				continue;
			}
			for (const DataRow& row : m_Rows)
			{
				if (const UUID handle = std::get<UUID>(row.Values[column]); handle.IsValid())
				{
					handles.insert(handle);
				}
			}
		}
		return std::vector<AssetHandle>(handles.begin(), handles.end());
	}

	std::optional<size_t> DataTable::FindColumnIndex(std::string_view name) const noexcept
	{
		for (size_t index = 0; index < m_Columns.size(); ++index)
		{
			if (m_Columns[index].Name == name)
			{
				return index;
			}
		}
		return std::nullopt;
	}

	const DataRow* DataTable::GetRow(std::string_view id) const
	{
		const auto found = m_RowIndex.find(std::string(id));
		return found == m_RowIndex.end() ? nullptr : &m_Rows[found->second];
	}

	Result<FieldValue> DataTable::GetValue(std::string_view rowId, std::string_view column) const
	{
		const DataRow* row = GetRow(rowId);
		if (row == nullptr)
		{
			return Error::Format("there is no row '{}'", rowId);
		}
		const std::optional<size_t> index = FindColumnIndex(column);
		if (!index.has_value())
		{
			return Error::Format("there is no column '{}'", column);
		}
		return row->Values[*index];
	}

	Result<void> DataTable::SetValue(std::string_view rowId, std::string_view column, const FieldValue& value)
	{
		const DataRow* row = GetRow(rowId);
		if (row == nullptr)
		{
			return Error::Format("there is no row '{}'", rowId);
		}
		const std::optional<size_t> index = FindColumnIndex(column);
		if (!index.has_value())
		{
			return Error::Format("there is no column '{}'", column);
		}
		if (*index == 0)
		{
			return Error("the id column is changed with RenameRow");
		}
		const DataColumnType type = m_Columns[*index].Type;
		if (!IsValueOfType(value, ToFieldType(type), FieldType::None))
		{
			return Error::Format("column '{}' holds {} values", column, ToString(type));
		}
		if (type == DataColumnType::Float && !std::isfinite(std::get<double>(value)))
		{
			return Error::Format("column '{}' holds finite numbers", column);
		}
		m_Rows[m_RowIndex.at(std::string(rowId))].Values[*index] = value;
		return {};
	}

	Result<void> DataTable::SetValueFromText(std::string_view rowId, std::string_view column, std::string_view text)
	{
		const std::optional<size_t> index = FindColumnIndex(column);
		if (!index.has_value())
		{
			return Error::Format("there is no column '{}'", column);
		}
		Result<FieldValue> value = ParseValue(m_Columns[*index].Type, text);
		if (!value)
		{
			return Error::Format("column '{}': {}", column, value.GetError().Message);
		}
		return SetValue(rowId, column, *value);
	}

	Result<void> DataTable::AddRow(std::string_view id, std::span<const FieldValue> values)
	{
		if (id.empty())
		{
			return Error("the row id is empty");
		}
		if (HasRow(id))
		{
			return Error::Format("there is already a row '{}'", id);
		}
		if (!values.empty() && values.size() != m_Columns.size() - 1)
		{
			return Error::Format(
				"expected {} values (one per column after the id), got {}", m_Columns.size() - 1, values.size());
		}
		DataRow row;
		row.Id = std::string(id);
		row.Values.reserve(m_Columns.size());
		row.Values.emplace_back(std::in_place_type<std::string>, std::string(id));
		for (size_t column = 1; column < m_Columns.size(); ++column)
		{
			if (values.empty())
			{
				row.Values.push_back(MakeZeroValue(ToFieldType(m_Columns[column].Type)));
				continue;
			}
			const FieldValue& value = values[column - 1];
			if (!IsValueOfType(value, ToFieldType(m_Columns[column].Type), FieldType::None)
			    || (m_Columns[column].Type == DataColumnType::Float && !std::isfinite(std::get<double>(value))))
			{
				return Error::Format(
					"column '{}' holds {} values", m_Columns[column].Name, ToString(m_Columns[column].Type));
			}
			row.Values.push_back(value);
		}
		m_RowIndex.emplace(row.Id, m_Rows.size());
		m_Rows.push_back(std::move(row));
		return {};
	}

	Result<void> DataTable::RemoveRow(std::string_view id)
	{
		const auto found = m_RowIndex.find(std::string(id));
		if (found == m_RowIndex.end())
		{
			return Error::Format("there is no row '{}'", id);
		}
		m_Rows.erase(m_Rows.begin() + static_cast<std::ptrdiff_t>(found->second));
		RebuildIndex();
		return {};
	}

	Result<void> DataTable::RenameRow(std::string_view id, std::string_view newId)
	{
		const auto found = m_RowIndex.find(std::string(id));
		if (found == m_RowIndex.end())
		{
			return Error::Format("there is no row '{}'", id);
		}
		if (newId.empty())
		{
			return Error("the row id is empty");
		}
		if (id != newId && HasRow(newId))
		{
			return Error::Format("there is already a row '{}'", newId);
		}
		DataRow& row = m_Rows[found->second];
		row.Id = std::string(newId);
		row.Values[0] = std::string(newId);
		RebuildIndex();
		return {};
	}

	void DataTable::RebuildIndex()
	{
		m_RowIndex.clear();
		m_RowIndex.reserve(m_Rows.size());
		for (size_t index = 0; index < m_Rows.size(); ++index)
		{
			m_RowIndex.emplace(m_Rows[index].Id, index);
		}
	}
}
