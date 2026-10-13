#pragma once

#include "Reflection/ReflectionTestTypes.h"
#include "TestSupport.h"

#include "Gambit/Automation/BuiltinCommands.h"
#include "Gambit/Automation/CommandRegistry.h"
#include "Gambit/Automation/EditorContext.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <string>
#include <string_view>

namespace Gambit::Tests
{
	// JSON from text, for readable test data.
	inline Json J(std::string_view text)
	{
		return Json::parse(text);
	}

	// A command registry with every built-in command and an editor context on a registry with the test components
	// (Health, AllTypes, Structs, PlayerTag), plus a temporary directory for projects. Members are destroyed in reverse
	// order: the context (and its worlds) goes before the type registry it uses.
	class AutomationFixture
	{
	public:
		ScopedTempDirectory Directory;
		TypeRegistry Types;
		CommandRegistry Commands;
		EditorContext Context;

		AutomationFixture()
			: Context(Types)
		{
			RegisterTestTypes(Types);
			RegisterBuiltinCommands(Commands);
		}

		[[nodiscard]] CommandResult Run(std::string_view name, const Json& arguments = Json::object())
		{
			return Commands.Execute(Context, name, arguments);
		}

		// Runs a command that must succeed and returns its data.
		Json Succeed(std::string_view name, const Json& arguments = Json::object())
		{
			const CommandResult result = Run(name, arguments);
			INFO(name, ": ", result.Message);
			REQUIRE(result.Ok);
			return result.Data;
		}

		// Runs a command that must fail with `code` and returns the result.
		CommandResult Reject(std::string_view name, const Json& arguments, CommandErrorCode code)
		{
			CommandResult result = Run(name, arguments);
			INFO(name, ": ", result.Message);
			REQUIRE_FALSE(result.Ok);
			CHECK(result.Code == code);
			return result;
		}

		[[nodiscard]] std::filesystem::path GetProjectDirectory() const { return Directory.GetPath() / "Project"; }

		// project.new: a project "Test" with its start scene open.
		void CreateProject()
		{
			Succeed(
				"project.new",
				J(R"({"directory": ")" + GetProjectDirectory().generic_string() + R"(", "name": "Test"})"));
		}

		[[nodiscard]] World& Scene() { return *Context.GetActiveScene()->Scene; }
		[[nodiscard]] uint64_t Hash() { return Scene().ComputeStateHash(); }

		// Runs `change` (one or more commands), then checks that undo brings the scene back to the state hash it had
		// before, that redo reproduces the state after, and that the change was a real one. Leaves the change in place.
		template<typename Change>
		void CheckUndoRedo(Change&& change)
		{
			const uint64_t before = Hash();
			change();
			const uint64_t after = Hash();
			CHECK(after != before);
			Succeed("undo");
			CHECK(Hash() == before);
			Succeed("redo");
			CHECK(Hash() == after);
		}

		// entity.create with a name (and more arguments); returns the UUID text.
		std::string Create(std::string_view name, Json arguments = Json::object())
		{
			arguments["name"] = std::string(name);
			return Succeed("entity.create", arguments)["uuid"].get<std::string>();
		}
	};
}
