#pragma once

#include "Gambit/Asset/Asset.h"
#include "Gambit/Core/Result.h"
#include "Gambit/Core/VFS.h"
#include "Gambit/Reflection/FieldType.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Gambit
{
	// The type of a data table column (D-027); a cell is a FieldValue of ToFieldType(type).
	enum class DataColumnType : uint8_t
	{
		Int,    // "int": a 64-bit integer
		Float,  // "float": a finite double (not a 32-bit float: spreadsheets hold decimals like 0.1 that would change)
		Bool,   // "bool": true or false, in any case
		String, // "string": any text
		Asset,  // "asset": the handle of an asset, "0x..."; empty is the null handle
		Entity  // "entity": the UUID of an entity, "0x..."; empty is the null UUID
	};

	[[nodiscard]] std::string_view ToString(DataColumnType type) noexcept;
	[[nodiscard]] std::optional<DataColumnType> ParseDataColumnType(std::string_view name) noexcept;
	// Int64, Double, Bool, String, AssetHandle, Entity.
	[[nodiscard]] FieldType ToFieldType(DataColumnType type) noexcept;

	struct DataColumn
	{
		std::string Name; // an identifier, so that scripts can write row.Damage
		DataColumnType Type = DataColumnType::String;
		bool TypeDeclared = false; // the header said ":type" (otherwise the type was inferred and is not written back)
	};

	// A row: its id (the first column) and one value per column, the id included as Values[0].
	struct DataRow
	{
		std::string Id;
		std::vector<FieldValue> Values;
	};

	// A table of rows loaded from a CSV file (D-027): bulk content such as weapons, items or dialogue, kept in files
	// that spreadsheets and agents can edit. RFC 4180 in UTF-8: comma separated, fields with commas, quotes or line
	// breaks in double quotes ("" for a quote), CRLF or LF line ends, an optional byte order mark. The first row names
	// the columns, each with an optional type: "Id,Damage:float,Name:string". The first column holds the unique row ids
	// (text, and never typed). Untyped columns get a type from their cells: int if all are canonical integers ("7",
	// "-3", not "007"), else bool if all are true/false, else float if all are numbers, else string; a column with an
	// empty cell stays string (declare ":float" to read blanks as 0). Typed columns read an empty cell as the default
	// value (0, false, "", the null UUID). Saving preserves the line ending and the byte order mark of the file, and
	// writes the types of the columns that declared one.
	//
	// Errors say where: "Data/Weapons.csv: line 7, column 12: column 'Damage': 'abc' is not a valid float".
	class DataTable final : public Asset
	{
	public:
		static constexpr AssetType StaticType = AssetType::DataTable;
		static constexpr size_t MaxColumns = 1024;

		[[nodiscard]] AssetType GetType() const noexcept override { return StaticType; }
		// The non-null handles in asset columns.
		[[nodiscard]] std::vector<AssetHandle> GetDependencies() const override;

		// An empty table. The first column holds the row ids and must be a string column; names must be distinct
		// identifiers.
		[[nodiscard]] static Result<Ref<DataTable>> Create(std::vector<DataColumn> columns);
		// `source` names the text in errors (usually the path).
		[[nodiscard]] static Result<Ref<DataTable>> Parse(std::string_view text, std::string_view source = {});
		[[nodiscard]] static Result<Ref<DataTable>> Load(const VFS& vfs, std::string_view path);

		[[nodiscard]] std::string ToCsv() const;
		[[nodiscard]] Result<void> Save(VFS& vfs, std::string_view path) const;

		[[nodiscard]] std::span<const DataColumn> GetColumns() const noexcept { return m_Columns; }
		[[nodiscard]] std::optional<size_t> FindColumnIndex(std::string_view name) const noexcept;

		[[nodiscard]] size_t GetRowCount() const noexcept { return m_Rows.size(); }
		// In file order.
		[[nodiscard]] std::span<const DataRow> GetRows() const noexcept { return m_Rows; }
		[[nodiscard]] bool HasRow(std::string_view id) const { return GetRow(id) != nullptr; }
		// nullptr when there is no such row.
		[[nodiscard]] const DataRow* GetRow(std::string_view id) const;

		[[nodiscard]] Result<FieldValue> GetValue(std::string_view rowId, std::string_view column) const;
		// The value must be of the column's FieldType (and finite for floats). The id column is changed with RenameRow.
		[[nodiscard]] Result<void> SetValue(std::string_view rowId, std::string_view column, const FieldValue& value);
		// Reads the cell text like the file would.
		[[nodiscard]] Result<void>
		SetValueFromText(std::string_view rowId, std::string_view column, std::string_view text);

		// The position of the row in GetRows(); nullopt when there is no such row.
		[[nodiscard]] std::optional<size_t> FindRowIndex(std::string_view id) const;

		// Appends a row. `values` holds one value per column after the id column, or is empty for the defaults.
		[[nodiscard]] Result<void> AddRow(std::string_view id, std::span<const FieldValue> values = {});
		// Inserts a row before position `position` (a position beyond the last row appends), e.g. to put a removed row
		// back where it was.
		[[nodiscard]] Result<void>
		InsertRow(size_t position, std::string_view id, std::span<const FieldValue> values = {});
		[[nodiscard]] Result<void> RemoveRow(std::string_view id);
		[[nodiscard]] Result<void> RenameRow(std::string_view id, std::string_view newId);

		// One cell of the given type from text, and back. FormatValue(ParseValue(x)) reads back as the same value.
		[[nodiscard]] static Result<FieldValue> ParseValue(DataColumnType type, std::string_view text);
		[[nodiscard]] static std::string FormatValue(DataColumnType type, const FieldValue& value);

	private:
		[[nodiscard]] static Result<void> ValidateColumns(std::span<const DataColumn> columns);
		void RebuildIndex();

		std::vector<DataColumn> m_Columns;
		std::vector<DataRow> m_Rows;
		std::unordered_map<std::string, size_t> m_RowIndex; // id to position; lookups only
		bool m_HasBom = false;
		bool m_UsesCrLf = false;
	};
}
