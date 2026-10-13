#pragma once

#include "Gambit/Automation/EditorContext.h"
#include "Gambit/Core/Result.h"
#include "Gambit/Core/UUID.h"
#include "Gambit/ECS/World.h"
#include "Gambit/Reflection/ComponentInfo.h"
#include "Gambit/Reflection/FieldType.h"
#include "Gambit/Serialization/Json.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>

// Bulk component editing (D-027): one component type across all the entities of a scene as a table, rows = entities,
// columns = fields, as JSON for agents and as CSV for spreadsheets. The table is read from and applied to a scene
// through reflection, so it works for every component, scripts' included.

namespace Gambit
{
	// A column of a bulk table: one serialized field of the component.
	struct BulkColumn
	{
		std::string Name;
		FieldType Type = FieldType::None;
		bool ReadOnly = false; // the cells may be read but not changed
	};

	// One entity with the component: its cells in the order of the columns, each the JSON of the field value (the
	// same form as in scene files).
	struct BulkRow
	{
		UUID Entity;
		std::string Name; // the entity's name, for people; not edited
		std::vector<Json> Values;
	};

	struct BulkTable
	{
		std::string Component;
		std::vector<BulkColumn> Columns;
		std::vector<BulkRow> Rows;
		size_t Total = 0; // the number of entities with the component, of which Rows may hold a page
	};

	// The component of every entity of `world` that has it, in hierarchy pre-order. `fields` limits the columns (empty:
	// every stored field); `offset` and `limit` select a page of the rows.
	[[nodiscard]] Result<BulkTable> ReadBulkTable(
		World& world, const ComponentInfo& component, std::span<const std::string> fields, size_t offset, size_t limit);

	// { "component", "columns": [{ "name", "type", "readOnly" }], "rows": [{ "entity", "name", "values": [...] }],
	//   "total" }
	[[nodiscard]] Json BulkTableToJson(const BulkTable& table);
	// The same shape back, for editing: the columns may be names or { "name": ... } objects (a subset of the fields),
	// the rows hold { "entity": "0x...", "values": [...] } with one value per column ("name" is ignored).
	[[nodiscard]] Result<BulkTable> BulkTableFromJson(const ComponentInfo& component, const Json& json);

	// CSV (RFC 4180, the format of data tables): the first column "Entity" holds the UUIDs, "EntityName" the names, the
	// other columns the fields with a type in the header ("Health:float"). Vectors, colors and quaternions get one
	// column per component ("Translation_X"), structs and arrays one column of JSON text.
	[[nodiscard]] Result<std::string> BulkTableToCsv(const ComponentInfo& component, const BulkTable& table);
	// Reads a table back. The columns may be any subset of the fields (all components of a vector together); unknown
	// columns are errors. "EntityName" is ignored.
	[[nodiscard]] Result<BulkTable>
	BulkTableFromCsv(const ComponentInfo& component, std::string_view csv, std::string_view source);

	// Applies the cells to the scene as edits of the component: struct cells merge into the current value, cells that
	// equal the current value change nothing, read-only cells may only repeat their value. Everything is checked before
	// the first change; the changes are done through the undo stack (one entry per entity that changes - group them
	// with UndoStack::BeginGroup). Returns the number of entities changed.
	[[nodiscard]] Result<size_t> ApplyBulkTable(
		EditorContext& context, SceneDocument& scene, const ComponentInfo& component, const BulkTable& table);
}
