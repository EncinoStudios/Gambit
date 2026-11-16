#include "gbpch.h"

#include "Gambit/Scripting/ApiRegistry.h"
#include "Gambit/Scripting/Bindings/CoreBindings.h"

// The engine Lua library (Gambit/Lua) is Lua code that every script engine loads; its documentation lives here.

namespace Gambit::Bindings
{
	void RegisterLibraryApi(ApiRegistry& registry)
	{
		const auto document = [&](std::string_view name, ApiEntryKind kind, std::string_view signature,
		                          std::string_view description, std::string_view example) {
			registry.Document(ApiEntry{
				.Name = std::string(name),
				.Kind = kind,
				.Signature = std::string(signature),
				.Description = std::string(description),
				.Example = std::string(example),
				.Module = "Library",
			});
		};

		document(
			"SortedPairs", ApiEntryKind::Function,
			"SortedPairs(t: table, compare?: fun(a: any, b: any): boolean) -> iterator",
			"Iterates the entries of a table in a deterministic order: keys by type (booleans, numbers, strings), then "
			"by value, or by `compare`. Use it instead of pairs whenever the order matters (D-011). Also "
			"require(\"Gambit.SortedPairs\").",
			"for name, score in SortedPairs(scores) do print(name, score) end");
		document(
			"Table", ApiEntryKind::Table, "Table", "Helpers for plain-data tables. Also require(\"Gambit.Table\").",
			"local copy = Table.Copy(settings)");
		document(
			"Table.Copy", ApiEntryKind::Function, "Table.Copy(t: table) -> table",
			"A deep copy: nested tables are copied (shared and cyclic ones once), metatables kept, other values "
			"shared.",
			"local copy = Table.Copy(defaults)");
		document(
			"Table.Keys", ApiEntryKind::Function, "Table.Keys(t: table) -> any[]",
			"The keys as an array, in SortedPairs order.", "for _, key in ipairs(Table.Keys(t)) do print(key) end");
		document(
			"Table.Count", ApiEntryKind::Function, "Table.Count(t: table) -> integer",
			"The number of entries, all keys counted (# counts only the sequence).",
			"local n = Table.Count(inventory)");
	}
}
