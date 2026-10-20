#include <doctest/doctest.h>

#include "AutomationFixture.h"

#include "Gambit/Automation/BulkTable.h"

#include <fstream>

using namespace Gambit;
using namespace Gambit::Tests;

namespace
{
	// Three units with health in a small hierarchy, plus an entity without health.
	struct Army
	{
		AutomationFixture Fixture;
		std::string Alpha;
		std::string Beta;
		std::string Gamma;

		Army()
		{
			Fixture.CreateProject();
			Alpha = Fixture.Create(
				"Alpha",
				J(R"({"components": {"Health": {"Current": 10, "Maximum": 20}, "Transform": {"Translation": [1, 2, 3]}}})"));
			Beta = Fixture.Create("Beta", J(R"({"parent": "Alpha", "components": {"Health": {"Current": 30}}})"));
			Fixture.Create("Scenery");
			Gamma = Fixture.Create("Gamma", J(R"({"components": {"Health": {"Current": 50, "Maximum": 60}}})"));
		}

		HealthComponent& Health(const std::string& uuid)
		{
			return Fixture.Scene().TryGetEntity(*UUID::Parse(uuid)).GetComponent<HealthComponent>();
		}
	};

	Json
	Table(std::initializer_list<std::pair<std::string, std::string>> rows, std::string_view columns = R"(["Current"])")
	{
		Json table = Json::object();
		table["columns"] = Json::parse(columns);
		Json list = Json::array();
		for (const auto& [entity, values] : rows)
		{
			list.push_back(Json{ { "entity", entity }, { "values", Json::parse(values) } });
		}
		table["rows"] = list;
		return table;
	}
}

TEST_CASE("Automation: bulk.get returns one component of all entities as a table")
{
	Army army;
	AutomationFixture& fixture = army.Fixture;

	const Json table = fixture.Succeed("bulk.get", J(R"({"component": "Health"})"));
	CHECK(table.at("component") == "Health");
	CHECK(table.at("total") == 3);
	CHECK(
		table.at("columns")
		== J(
			R"([{"name": "Current", "type": "Float", "readOnly": false}, {"name": "Maximum", "type": "Float", "readOnly": false}])"));
	REQUIRE(table.at("rows").size() == 3);
	CHECK(
		table.at("rows")[0] == Json{ { "entity", army.Alpha }, { "name", "Alpha" }, { "values", J("[10.0, 20.0]") } });
	CHECK(table.at("rows")[1].at("name") == "Beta"); // hierarchy order: the child follows its parent
	CHECK(table.at("rows")[2].at("values") == J("[50.0, 60.0]"));

	const Json subset = fixture.Succeed("bulk.get", J(R"({"component": "Health", "fields": ["Maximum"]})"));
	CHECK(subset.at("columns").size() == 1);
	CHECK(subset.at("rows")[0].at("values") == J("[20.0]"));
	const Json page = fixture.Succeed("bulk.get", J(R"({"component": "Health", "limit": 1, "offset": 2})"));
	CHECK(page.at("rows").size() == 1);
	CHECK(page.at("rows")[0].at("name") == "Gamma");
	CHECK(page.at("total") == 3);

	// Vectors, enums and nested values appear as in files; read-only fields are marked.
	fixture.Succeed("component.add", J(R"({"entity": "Alpha", "component": "AllTypes", "values": {"Mode": "Fast"}})"));
	const Json all = fixture.Succeed(
		"bulk.get", J(R"({"component": "AllTypes", "fields": ["Vec3", "Mode", "Textures", "Int32s"]})"));
	// The columns follow the order of the component's fields, whatever the order of the request.
	CHECK(all.at("columns")[3] == J(R"({"name": "Textures", "type": "Array", "readOnly": true})"));
	CHECK(all.at("rows")[0].at("values") == J(R"([[1.0, 2.0, 3.0], "Fast", [1, -2], ["0x0000000000000002"]])"));
	const Json tags = fixture.Succeed("bulk.get", J(R"({"component": "Tag"})"));
	CHECK(tags.at("total") == 4);

	const Json none = fixture.Succeed("bulk.get", J(R"({"component": "PlayerTag"})"));
	CHECK(none.at("rows").empty());
	fixture.Reject("bulk.get", J(R"({"component": "Nope"})"), CommandErrorCode::NotFound);
	fixture.Reject("bulk.get", J(R"({"component": "Health", "fields": ["Nope"]})"), CommandErrorCode::InvalidArguments);
	fixture.Reject(
		"bulk.get", J(R"({"component": "AllTypes", "fields": ["Cache"]})"),
		CommandErrorCode::InvalidArguments); // not stored
	fixture.Reject("bulk.get", J(R"({})"), CommandErrorCode::InvalidArguments);
}

TEST_CASE("Automation: bulk.set applies a table of edits as one undo step")
{
	Army army;
	AutomationFixture& fixture = army.Fixture;
	const size_t depth = fixture.Context.GetUndoStack().GetUndoHistory().size();

	fixture.CheckUndoRedo([&] {
		const Json result = fixture.Succeed(
			"bulk.set",
			Json{ { "component", "Health" },
		          { "table", Table({ { army.Alpha, "[1]" }, { army.Beta, "[2]" }, { army.Gamma, "[3]" } }) } });
		CHECK(result.at("rows") == 3);
		CHECK(result.at("changed") == 3);
	});
	CHECK(army.Health(army.Alpha).Current == 1.0f);
	CHECK(army.Health(army.Beta).Current == 2.0f);
	CHECK(army.Health(army.Gamma).Current == 3.0f);
	CHECK(army.Health(army.Alpha).Maximum == 20.0f); // the other column is untouched
	CHECK(fixture.Context.GetUndoStack().GetUndoHistory().size() == depth + 1);
	CHECK(
		fixture.Context.GetUndoStack().GetUndoDescription()
		== "Apply a table of edits to one component of many entities");

	// Rows whose values are what the entity has already do not change anything.
	const Json same = fixture.Succeed(
		"bulk.set",
		Json{ { "component", "Health" }, { "table", Table({ { army.Alpha, "[1]" }, { army.Beta, "[5]" } }) } });
	CHECK(same.at("changed") == 1);
	const Json nothing =
		fixture.Succeed("bulk.set", Json{ { "component", "Health" }, { "table", Table({ { army.Alpha, "[1]" } }) } });
	CHECK(nothing.at("changed") == 0);
	CHECK(fixture.Context.GetUndoStack().GetUndoHistory().size() == depth + 2);

	// Several columns, in any order.
	fixture.Succeed(
		"bulk.set",
		Json{ { "component", "Health" },
	          { "table", Table({ { army.Gamma, "[7, 9]" } }, R"(["Maximum", "Current"])") } });
	CHECK(army.Health(army.Gamma).Maximum == 7.0f);
	CHECK(army.Health(army.Gamma).Current == 9.0f);

	// Columns may be given as the objects bulk.get returns.
	const Json fetched = fixture.Succeed("bulk.get", J(R"({"component": "Health"})"));
	Json edited = fetched;
	edited["rows"][0]["values"][0] = 99.0;
	fixture.Succeed("bulk.set", Json{ { "component", "Health" }, { "table", edited } });
	CHECK(army.Health(army.Alpha).Current == 99.0f);
}

TEST_CASE("Automation: bulk.set merges struct cells")
{
	Army army;
	AutomationFixture& fixture = army.Fixture;
	fixture.Succeed(
		"component.add",
		J(R"({"entity": "Alpha", "component": "Structs", "values": {"Main": {"Clip": "a", "Weight": 0.5}}})"));
	fixture.Succeed(
		"component.add",
		J(R"({"entity": "Gamma", "component": "Structs", "values": {"Main": {"Clip": "g", "Weight": 0.25}}})"));

	fixture.CheckUndoRedo([&] {
		fixture.Succeed(
			"bulk.set",
			Json{ { "component", "Structs" },
		          { "table",
		            Table(
						{ { army.Alpha, R"([{"Weight": 1.0}])" }, { army.Gamma, R"([{"Clip": "changed"}])" } },
						R"(["Main"])") } });
	});
	const auto main = [&](const std::string& uuid) {
		return fixture.Scene().TryGetEntity(*UUID::Parse(uuid)).GetComponent<StructsComponent>().Main;
	};
	CHECK(main(army.Alpha).Weight == 1.0f);
	CHECK(main(army.Alpha).Clip == "a");
	CHECK(main(army.Gamma).Weight == 0.25f);
	CHECK(main(army.Gamma).Clip == "changed");
}

TEST_CASE("Automation: bulk.set checks everything before it changes anything")
{
	Army army;
	AutomationFixture& fixture = army.Fixture;
	const uint64_t before = fixture.Hash();
	const auto reject = [&](const Json& table, CommandErrorCode code = CommandErrorCode::InvalidArguments) {
		return fixture.Reject("bulk.set", Json{ { "component", "Health" }, { "table", table } }, code).Message;
	};

	// The first row is fine, the second is wrong: neither is applied.
	CHECK(reject(Table({ { army.Alpha, "[1]" }, { army.Beta, R"(["x"])" } })).starts_with("row 1 ("));
	CHECK(
		reject(Table({ { army.Alpha, "[1]" }, { army.Beta, R"(["x"])" } }))
			.find("/Current: expected a number, found a string")
		!= std::string::npos);
	CHECK(
		reject(Table({ { army.Alpha, "[1]" }, { "0x00000000DEADBEEF", "[2]" } }), CommandErrorCode::NotFound)
			.find("there is no such entity in the scene")
		!= std::string::npos);
	const std::string scenery = fixture.Succeed("entity.get", J(R"({"entity": "Scenery"})")).at("uuid");
	CHECK(
		reject(Table({ { scenery, "[1]" } }), CommandErrorCode::NotFound).find("'Scenery' has no component 'Health'")
		!= std::string::npos);
	CHECK(reject(Table({ { army.Alpha, "[1, 2]" } })).find("2 values for 1 columns") != std::string::npos);
	CHECK(
		reject(Table({ { army.Alpha, "[1]" } }, R"(["Nope"])")).find("has no stored field 'Nope'")
		!= std::string::npos);
	CHECK(
		reject(Table({ { army.Alpha, "[1]" } }, R"(["Current", "Current"])")).find("'Current' appears twice")
		!= std::string::npos);
	CHECK(reject(Table({ { "Alpha", "[1]" } })).find("is not an entity UUID") != std::string::npos);
	CHECK(reject(Json::array()).find("expected an object, found an array") != std::string::npos);
	CHECK(reject(Json::object()).find("with \"columns\" and \"rows\"") != std::string::npos);
	CHECK(reject(Json{ { "columns", 1 }, { "rows", Json::array() } }).find("arrays") != std::string::npos);
	CHECK(
		reject(Json{ { "columns", J("[5]") }, { "rows", Json::array() } }).find("column 0 must be a field name")
		!= std::string::npos);
	fixture.Reject(
		"bulk.set", Json{ { "component", "Relationship" }, { "table", Table({}) } }, CommandErrorCode::Conflict);
	fixture.Reject("bulk.set", Json{ { "component", "Nope" }, { "table", Table({}) } }, CommandErrorCode::NotFound);
	CHECK(fixture.Hash() == before);

	// Read-only cells may repeat their value but not change it.
	fixture.Succeed("component.add", J(R"({"entity": "Alpha", "component": "AllTypes"})"));
	Json repeat = Json{ { "component", "AllTypes" },
		                { "table", Table({ { army.Alpha, R"([["0x0000000000000002"]])" } }, R"(["Textures"])") } };
	CHECK(fixture.Succeed("bulk.set", repeat).at("changed") == 0);
	Json change = Json{ { "component", "AllTypes" },
		                { "table", Table({ { army.Alpha, R"([["0x0000000000000003"]])" } }, R"(["Textures"])") } };
	CHECK(
		fixture.Reject("bulk.set", change, CommandErrorCode::InvalidArguments)
			.Message.find("the field 'Textures' is read-only")
		!= std::string::npos);
}

TEST_CASE("Automation: bulk.set calls with the same merge key are one undo step")
{
	Army army;
	AutomationFixture& fixture = army.Fixture;
	const uint64_t before = fixture.Hash();
	const size_t depth = fixture.Context.GetUndoStack().GetUndoHistory().size();
	// A gizmo drag of two selected units: one bulk.set per frame.
	for (const int frame : { 1, 2, 3, 4 })
	{
		Json arguments = Json::object();
		arguments["component"] = "Health";
		arguments["table"] = Table({ { army.Alpha, "[" + std::to_string(10 + frame) + "]" },
		                             { army.Gamma, "[" + std::to_string(50 + frame) + "]" } });
		arguments["mergeKey"] = "drag-7";
		fixture.Succeed("bulk.set", arguments);
	}
	CHECK(army.Health(army.Alpha).Current == 14.0f);
	CHECK(army.Health(army.Gamma).Current == 54.0f);
	CHECK(fixture.Context.GetUndoStack().GetUndoHistory().size() == depth + 1);
	fixture.Succeed("undo");
	CHECK(fixture.Hash() == before);
	fixture.Succeed("redo");
	CHECK(army.Health(army.Alpha).Current == 14.0f);

	// Without the key every call is a step.
	fixture.Succeed(
		"bulk.set",
		Json{ { "component", "Health" }, { "table", Table({ { army.Alpha, "[1]" }, { army.Gamma, "[1]" } }) } });
	fixture.Succeed(
		"bulk.set",
		Json{ { "component", "Health" }, { "table", Table({ { army.Alpha, "[2]" }, { army.Gamma, "[2]" } }) } });
	CHECK(fixture.Context.GetUndoStack().GetUndoHistory().size() == depth + 3);
}

TEST_CASE("Automation: bulk.export-csv makes a table for spreadsheets")
{
	Army army;
	AutomationFixture& fixture = army.Fixture;
	fixture.Succeed(
		"component.add",
		J(R"({"entity": "Alpha", "component": "AllTypes", "values": {"String": "a, \"b\"", "Float": 0.1, "Mode": "Fast", "UInt64": 18446744073709551615, "Vec3": [1.5, 2, 3], "Quat": [0, 0, 0, 1], "Floats": [1, 2], "Int64": -5}})"));

	const Json exported = fixture.Succeed(
		"bulk.export-csv",
		J(R"({"component": "AllTypes", "fields": ["Bool", "Int64", "UInt64", "Float", "Vec3", "Quat", "Color3", "String", "Mode", "Target", "Texture", "Floats"]})"));
	CHECK(exported.at("rows") == 1);
	const std::string csv = exported.at("csv").get<std::string>();
	const std::string header = csv.substr(0, csv.find('\n'));
	CHECK(
		header
		== "Entity,EntityName:string,Bool:bool,Int64:int,UInt64:string,Float:float,Vec3_X:float,Vec3_Y:float,Vec3_Z:"
		   "float,"
		   "Quat_X:float,Quat_Y:float,Quat_Z:float,Quat_W:float,Color3_R:float,Color3_G:float,Color3_B:float,String:"
		   "string,"
		   "Target:entity,Texture:asset,Mode:string,Floats:string");
	const std::string row = csv.substr(csv.find('\n') + 1);
	CHECK(row.starts_with(
		army.Alpha
		+ ",Alpha,true,-5,18446744073709551615,0.1,1.5,2.0,3.0,0.0,0.0,0.0,1.0,0.5,0.25,1.0,\"a, "
		  "\"\"b\"\"\",0x0000000000001234,0x000000000000ABCD,Fast,\"[1.0,2.0]\""));

	fixture.Reject("bulk.export-csv", J(R"({"component": "Nope"})"), CommandErrorCode::NotFound);
	// Only the text: it changes nothing, so a dry run (and any read-only host) may export.
	const CommandInfo& info = *fixture.Commands.Find("bulk.export-csv");
	CHECK_FALSE(info.HasFlag(CommandFlags::Mutating));
	CHECK_FALSE(info.HasFlag(CommandFlags::WritesFiles));
	fixture.Context.SetDryRun(true);
	CHECK(fixture.Succeed("bulk.export-csv", J(R"({"component": "Health"})")).contains("csv"));
}

TEST_CASE("Automation: bulk.write-csv writes the table to a file and never replaces one by accident")
{
	Army army;
	AutomationFixture& fixture = army.Fixture;
	const CommandInfo& info = *fixture.Commands.Find("bulk.write-csv");
	CHECK(info.HasFlag(CommandFlags::Mutating));
	CHECK(info.HasFlag(CommandFlags::WritesFiles));
	CHECK_FALSE(info.HasFlag(CommandFlags::Undoable));

	const Json written =
		fixture.Succeed("bulk.write-csv", J(R"({"component": "Health", "path": "Exports/Health.csv"})"));
	CHECK(written.at("path") == "Exports/Health.csv");
	CHECK(written.at("rows") == 3);
	CHECK_FALSE(written.contains("csv"));
	const std::string file = ReadTextFile(fixture.GetProjectDirectory() / "Exports/Health.csv");
	CHECK(file == fixture.Succeed("bulk.export-csv", J(R"({"component": "Health"})")).at("csv").get<std::string>());
	CHECK(file.starts_with("Entity,EntityName:string,Current:float,Maximum:float\n"));
	CHECK(file.find(army.Gamma + ",Gamma,50.0,60.0") != std::string::npos);

	// An existing file is replaced only on request.
	const CommandResult exists = fixture.Reject(
		"bulk.write-csv", J(R"({"component": "Tag", "path": "Exports/Health.csv"})"), CommandErrorCode::AlreadyExists);
	CHECK(exists.Message.find("pass overwrite") != std::string::npos);
	CHECK(ReadTextFile(fixture.GetProjectDirectory() / "Exports/Health.csv") == file);
	fixture.Succeed("bulk.write-csv", J(R"({"component": "Tag", "path": "Exports/Health.csv", "overwrite": true})"));
	CHECK(ReadTextFile(fixture.GetProjectDirectory() / "Exports/Health.csv")
	          .starts_with("Entity,EntityName:string,Name"));

	fixture.Reject(
		"bulk.write-csv", J(R"({"component": "Health", "path": "Data/Health.txt"})"),
		CommandErrorCode::InvalidArguments);
	fixture.Reject(
		"bulk.write-csv", J(R"({"component": "Health", "path": "../Health.csv"})"), CommandErrorCode::InvalidArguments);
	fixture.Reject("bulk.write-csv", J(R"({"component": "Health"})"), CommandErrorCode::InvalidArguments); // no path
	fixture.Context.SetDryRun(true);
	fixture.Reject(
		"bulk.write-csv", J(R"({"component": "Health", "path": "Data/Dry.csv"})"), CommandErrorCode::Unsupported);
	CHECK_FALSE(std::filesystem::exists(fixture.GetProjectDirectory() / "Data/Dry.csv"));
}

TEST_CASE("Automation: bulk.write-csv over a data table refuses unsaved edits and reloads a clean table")
{
	Army army;
	AutomationFixture& fixture = army.Fixture;
	const std::filesystem::path source = fixture.Directory.GetPath() / "Weapons.csv";
	std::ofstream(source, std::ios::binary) << "Id,Damage:float\npistol,12.5\n";
	fixture.Succeed(
		"asset.import", J(R"({"source": ")" + source.generic_string() + R"(", "path": "Data/Weapons.csv"})"));
	fixture.Succeed(
		"data.set", J(R"({"table": "Data/Weapons.csv", "row": "pistol", "column": "Damage", "value": 20})"));

	// The table has unsaved edits: replacing its file would lose them, or saving them later would undo the export.
	const std::string original = ReadTextFile(fixture.GetProjectDirectory() / "Data/Weapons.csv");
	const CommandResult refused = fixture.Reject(
		"bulk.write-csv", J(R"({"component": "Health", "path": "Data/Weapons.csv", "overwrite": true})"),
		CommandErrorCode::Conflict);
	CHECK(refused.Message.find("unsaved changes") != std::string::npos);
	CHECK(ReadTextFile(fixture.GetProjectDirectory() / "Data/Weapons.csv") == original);
	CHECK(fixture.Context.GetUndoStack().IsDirty("Data/Weapons.csv"));

	// Saved, the open table is closed and read again from the new file.
	fixture.Succeed("scene.save", J(R"({"all": true})"));
	const size_t history = fixture.Context.GetUndoStack().GetUndoHistory().size();
	const CommandResult replaced =
		fixture.Run("bulk.write-csv", J(R"({"component": "Health", "path": "Data/Weapons.csv", "overwrite": true})"));
	REQUIRE(replaced.Ok);
	CHECK(replaced.Warnings.size() == 1);
	CHECK_FALSE(fixture.Context.IsTableOpen("Data/Weapons.csv"));
	// The edit of the replaced table is gone with it; the scene's history stays.
	CHECK(fixture.Context.GetUndoStack().GetUndoHistory().size() == history - 1);
	const Json table = fixture.Succeed("data.get", J(R"({"table": "Data/Weapons.csv"})"));
	CHECK(table.at("rowCount") == 3); // the Health rows of the army
	CHECK(fixture.Context.GetUndoStack().GetDirtyDocuments().empty());
}

TEST_CASE("Automation: bulk.import-csv applies an edited CSV and round-trips every field type")
{
	Army army;
	AutomationFixture& fixture = army.Fixture;
	fixture.Succeed("component.add", J(R"({"entity": "Alpha", "component": "AllTypes"})"));
	fixture.Succeed(
		"component.add",
		J(R"({"entity": "Gamma", "component": "AllTypes", "values": {"String": "text, with \"quotes\"", "Vec4": [1, 2, 3, 4], "Mode": "Off"}})"));
	fixture.Succeed(
		"component.add",
		J(R"({"entity": "Gamma", "component": "Structs", "values": {"Main": {"Clip": "run"}, "Layers": [{"Weight": 0.5}]}})"));

	// Exporting and importing the same table changes nothing, for every stored field of the components.
	for (const char* component : { "AllTypes", "Structs", "Health", "Transform", "Tag" })
	{
		const uint64_t before = fixture.Hash();
		const Json exported =
			fixture.Succeed("bulk.export-csv", J(std::string(R"({"component": ")") + component + R"("})"));
		const Json imported =
			fixture.Succeed("bulk.import-csv", Json{ { "component", component }, { "csv", exported.at("csv") } });
		INFO(component);
		CHECK(imported.at("changed") == 0);
		CHECK(fixture.Hash() == before);
	}

	// Edit cells of the exported text.
	const std::string csv =
		"Entity,EntityName,Current,Maximum\n" + army.Alpha + ",Alpha,15,25\n" + army.Gamma + ",Gamma,50,70\n";
	fixture.CheckUndoRedo([&] {
		const Json imported = fixture.Succeed("bulk.import-csv", Json{ { "component", "Health" }, { "csv", csv } });
		CHECK(imported.at("rows") == 2);
		CHECK(imported.at("changed") == 2);
	});
	CHECK(army.Health(army.Alpha).Current == 15.0f);
	CHECK(army.Health(army.Alpha).Maximum == 25.0f);
	CHECK(army.Health(army.Gamma).Maximum == 70.0f);
	CHECK(army.Health(army.Beta).Current == 30.0f); // not in the CSV

	// Subsets of columns, vector components, text and JSON cells.
	const std::string vectors = "Entity,Translation_X,Translation_Y,Translation_Z\n" + army.Alpha + ",4,5.5,6\n";
	fixture.Succeed("bulk.import-csv", Json{ { "component", "Transform" }, { "csv", vectors } });
	CHECK(
		fixture.Scene().TryGetEntity(*UUID::Parse(army.Alpha)).GetComponent<TransformComponent>().Translation
		== glm::vec3(4.0f, 5.5f, 6.0f));
	const std::string names = "Entity,Name\n" + army.Gamma + ",Renamed\n";
	fixture.Succeed("bulk.import-csv", Json{ { "component", "Tag" }, { "csv", names } });
	CHECK(fixture.Scene().TryGetEntity(*UUID::Parse(army.Gamma)).GetName() == "Renamed");
	const std::string structs = "Entity,Main:string\n" + army.Gamma + ",\"{\"\"Weight\"\": 0.125}\"\n";
	fixture.Succeed("bulk.import-csv", Json{ { "component", "Structs" }, { "csv", structs } });
	const Entity gamma = fixture.Scene().TryGetEntity(*UUID::Parse(army.Gamma));
	CHECK(gamma.GetComponent<StructsComponent>().Main.Weight == 0.125f);
	CHECK(gamma.GetComponent<StructsComponent>().Main.Clip == "run"); // merged
}

TEST_CASE("Automation: bulk.import-csv reads a file of the project and explains problems")
{
	Army army;
	AutomationFixture& fixture = army.Fixture;
	fixture.Succeed("bulk.write-csv", J(R"({"component": "Health", "path": "Data/Health.csv"})"));
	std::string text = ReadTextFile(fixture.GetProjectDirectory() / "Data/Health.csv");
	text.replace(text.find(",10.0,"), 6, ",11.0,");
	REQUIRE(fixture.Context.GetProject()->GetVFS().WriteText("Data/Health.csv", text).HasValue());
	CHECK(
		fixture.Succeed("bulk.import-csv", J(R"({"component": "Health", "path": "Data/Health.csv"})")).at("changed")
		== 1);
	CHECK(army.Health(army.Alpha).Current == 11.0f);

	const uint64_t before = fixture.Hash();
	const auto reject = [&](const std::string& csv, CommandErrorCode code = CommandErrorCode::InvalidArguments) {
		return fixture.Reject("bulk.import-csv", Json{ { "component", "Health" }, { "csv", csv } }, code).Message;
	};
	CHECK(reject("Name,Current\nx,1\n").find("the first column of the CSV must be 'Entity'") != std::string::npos);
	CHECK(
		reject("Entity,Colour\n" + army.Alpha + ",1\n")
			.find("the CSV has a column 'Colour' that is no field of 'Health'; its columns are: Current, Maximum")
		!= std::string::npos);
	CHECK(
		reject("Entity,Current\nnot-a-uuid,1\n").find("'not-a-uuid' in the Entity column is not an entity UUID")
		!= std::string::npos);
	CHECK(reject("Entity,Current\n" + army.Alpha + ",lots\n").find("expected a number") != std::string::npos);
	CHECK(reject("Entity,Current:bool\n" + army.Alpha + ",true\n").find("expected a number") != std::string::npos);
	CHECK(
		reject("Entity,Current\n0x00000000DEADBEEF,1\n", CommandErrorCode::NotFound).find("there is no such entity")
		!= std::string::npos);
	CHECK(reject("Entity,Current\n" + army.Alpha + ",1,2\n").find("column") != std::string::npos);
	CHECK(fixture.Hash() == before);

	fixture.Reject("bulk.import-csv", J(R"({"component": "Health"})"), CommandErrorCode::InvalidArguments);
	fixture.Reject(
		"bulk.import-csv", J(R"({"component": "Health", "csv": "x", "path": "Data/Health.csv"})"),
		CommandErrorCode::InvalidArguments);
	fixture.Reject(
		"bulk.import-csv", J(R"({"component": "Health", "path": "Data/Missing.csv"})"), CommandErrorCode::NotFound);
	fixture.Reject(
		"bulk.import-csv", J(R"({"component": "Relationship", "csv": "Entity\n"})"), CommandErrorCode::Conflict);

	// Vector columns must come as a whole.
	fixture.Reject(
		"bulk.import-csv", J(R"({"component": "Transform", "csv": "Entity,Translation_X,Translation_Y\n"})"),
		CommandErrorCode::InvalidArguments);
	const CommandResult partial = fixture.Run(
		"bulk.import-csv",
		Json{ { "component", "Transform" },
	          { "csv", "Entity,Translation_X,Translation_Y\n" + army.Alpha + ",1,2\n" } });
	CHECK_FALSE(partial.Ok);
	CHECK(
		partial.Message.find(
			"the field 'Translation' needs all of the columns Translation_X, Translation_Y, Translation_Z")
		!= std::string::npos);
}

TEST_CASE("Automation: bulk tables convert between JSON, CSV and the scene")
{
	Army army;
	AutomationFixture& fixture = army.Fixture;
	const ComponentInfo& health = *fixture.Types.FindComponent("Health");

	// The table functions work without commands.
	Result<BulkTable> table = ReadBulkTable(fixture.Scene(), health, {}, 0, 100);
	REQUIRE(table.HasValue());
	CHECK(table->Rows.size() == 3);
	const Result<std::string> csv = BulkTableToCsv(health, *table);
	REQUIRE(csv.HasValue());
	const Result<BulkTable> parsed = BulkTableFromCsv(health, *csv, "test");
	REQUIRE(parsed.HasValue());
	CHECK(parsed->Rows.size() == 3);
	CHECK(parsed->Columns.size() == 2);
	CHECK(parsed->Rows[0].Entity == table->Rows[0].Entity);
	CHECK(parsed->Rows[0].Values == table->Rows[0].Values);
	const Result<BulkTable> viaJson = BulkTableFromJson(health, BulkTableToJson(*table));
	REQUIRE(viaJson.HasValue());
	CHECK(viaJson->Rows[2].Values == table->Rows[2].Values);

	// A float that has no short text still survives (0.1f reads back as 0.1f).
	fixture.Succeed("component.set", J(R"({"entity": "Alpha", "component": "Health", "values": {"Current": 0.1}})"));
	const uint64_t before = fixture.Hash();
	const Json exported = fixture.Succeed("bulk.export-csv", J(R"({"component": "Health"})"));
	CHECK(exported.at("csv").get<std::string>().find(",0.1,") != std::string::npos);
	CHECK(
		fixture.Succeed("bulk.import-csv", Json{ { "component", "Health" }, { "csv", exported.at("csv") } })
			.at("changed")
		== 0);
	CHECK(fixture.Hash() == before);

	// Numbers that spreadsheets cannot hold are refused on export.
	fixture.Succeed("component.set", J(R"({"entity": "Alpha", "component": "Health", "values": {"Current": "NaN"}})"));
	const CommandResult nan =
		fixture.Reject("bulk.export-csv", J(R"({"component": "Health"})"), CommandErrorCode::InvalidArguments);
	CHECK(nan.Message.find("cannot hold NaN or infinity") != std::string::npos);
}
