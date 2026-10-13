#pragma once

#include "Gambit/Automation/CommandInfo.h"
#include "Gambit/Automation/CommandRegistry.h"
#include "Gambit/Automation/CommandResult.h"
#include "Gambit/Automation/EditorContext.h"
#include "Gambit/ECS/Entity.h"
#include "Gambit/Serialization/EntityData.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Helpers shared by the built-in commands (Automation/Commands/*.cpp). Not part of the module's public API.

namespace Gambit::CommandSupport
{
	// Registers a command; a bad description is a programmer error.
	void RegisterOrDie(CommandRegistry& registry, CommandInfo info, CommandHandler handler);

	// ---- Reading arguments (already validated and completed by CommandInfo::ParseArguments) ----

	[[nodiscard]] bool Has(const Json& arguments, std::string_view name);
	[[nodiscard]] std::string GetString(const Json& arguments, std::string_view name);
	[[nodiscard]] std::optional<std::string> TryGetString(const Json& arguments, std::string_view name);
	[[nodiscard]] int64_t GetInteger(const Json& arguments, std::string_view name, int64_t fallback = 0);
	[[nodiscard]] std::optional<int64_t> TryGetInteger(const Json& arguments, std::string_view name);
	[[nodiscard]] bool GetBool(const Json& arguments, std::string_view name, bool fallback = false);

	// ---- Results ----

	[[nodiscard]] CommandResult Fail(CommandErrorCode code, std::string message);
	[[nodiscard]] CommandResult Fail(const Error& error, CommandErrorCode fallback = CommandErrorCode::Failed);
	// Failure when `report` holds warnings: commands are strict about their input, so an unknown member in a value is
	// an error and not a warning (the message names it).
	[[nodiscard]] Result<void> RequireNoWarnings(const LoadReport& report);

	// ---- Entities ----

	// Finds the entity an argument names: a UUID ("0x...") or the name of the one entity that has it.
	[[nodiscard]] Result<Entity> ResolveEntity(World& world, std::string_view text);
	// Like ResolveEntity for an optional argument; an absent one gives an invalid Entity.
	[[nodiscard]] Result<Entity> ResolveOptionalEntity(World& world, const Json& arguments, std::string_view name);
	// { "uuid", "name", "parent" (null for roots), "enabled" }
	[[nodiscard]] Json DescribeEntity(World& world, Entity entity);
	// The names of the components the entity has that tools show (not the engine-managed ones), sorted.
	[[nodiscard]] std::vector<std::string> GetComponentNames(const World& world, Entity entity);
	// The number of entities in the subtree below and including `root`.
	[[nodiscard]] size_t CountSubtree(World& world, Entity root);
	// Takes the entities that no longer exist out of the selection (after destroying or undoing).
	void PruneSelection(EditorContext& context);
	// The names of the entity and its ancestors joined by '/', root first.
	[[nodiscard]] std::string GetEntityPath(World& world, Entity entity);
	// Whether `text` contains `part`, ignoring ASCII case.
	[[nodiscard]] bool ContainsIgnoreCase(std::string_view text, std::string_view part);

	// ---- Components ----

	// The component type `name`, or the error that lists the available ones.
	[[nodiscard]] Result<const ComponentInfo*> FindComponentType(const TypeRegistry& types, std::string_view name);
	// The type for a command that changes the component of entities: it must not be engine-managed.
	[[nodiscard]] Result<const ComponentInfo*>
	FindEditableComponentType(const TypeRegistry& types, std::string_view name);
	// Reads the partial object `values` as edits of the fields of `info`, starting from the fields of `current` (the
	// component on an entity) or from the defaults when it is null. Struct values merge (unspecified members keep their
	// values). Unknown, read-only and runtime-only fields, wrong types and unknown members of struct values are errors
	// that name the field.
	[[nodiscard]] Result<std::vector<FieldData>>
	DecodeFieldEdits(const ComponentInfo& info, const void* current, const Json& values);
	// The fields of a component as JSON (ComponentToJson of a live component).
	[[nodiscard]] Result<Json> ComponentJson(const ComponentInfo& info, const void* component);

	// ---- Misc ----

	[[nodiscard]] std::string JoinNames(const std::vector<std::string>& names, size_t limit = 12);
	// "0x%016llX": hashes and counters in JSON results.
	[[nodiscard]] std::string HexText(uint64_t value);
}
