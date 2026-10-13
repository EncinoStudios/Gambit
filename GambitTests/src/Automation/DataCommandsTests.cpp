#include <doctest/doctest.h>

#include "AutomationFixture.h"

#include "Gambit/Asset/DataTable.h"

#include <fstream>

using namespace Gambit;
using namespace Gambit::Tests;

namespace
{
	constexpr const char* WeaponsCsv = "Id,Damage:float,Ammo:int,Automatic:bool,Name\n"
									   "pistol,12.5,12,false,Pistol\n"
									   "rifle,30.0,30,true,Rifle\n"
									   "knife,5.0,0,false,Knife\n";

	struct Weapons
	{
		AutomationFixture Fixture;

		Weapons()
		{
			Fixture.CreateProject();
			const std::filesystem::path source = Fixture.Directory.GetPath() / "Weapons.csv";
			std::ofstream(source, std::ios::binary) << WeaponsCsv;
			Fixture.Succeed(
				"asset.import", J(R"({"source": ")" + source.generic_string() + R"(", "path": "Data/Weapons.csv"})"));
		}

		std::string Csv() const
		{
			Result<Ref<DataTable>> table = const_cast<EditorContext&>(Fixture.Context).GetTable("Data/Weapons.csv");
			REQUIRE(table.HasValue());
			return (*table)->ToCsv();
		}

		std::string FileText() const { return ReadTextFile(Fixture.GetProjectDirectory() / "Data/Weapons.csv"); }
	};
}

TEST_CASE("Automation: data.tables and data.get read data tables")
{
	Weapons weapons;
	AutomationFixture& fixture = weapons.Fixture;

	const Json tables = fixture.Succeed("data.tables");
	CHECK(tables.at("count") == 1);
	CHECK(tables.at("tables")[0].at("path") == "Data/Weapons.csv");
	CHECK(tables.at("tables")[0].at("rows") == 3);
	CHECK(tables.at("tables")[0].at("columns") == J(R"(["Id", "Damage", "Ammo", "Automatic", "Name"])"));

	const Json whole = fixture.Succeed("data.get", J(R"({"table": "Data/Weapons.csv"})"));
	CHECK(whole.at("rowCount") == 3);
	CHECK(whole.at("columns")[1] == J(R"({"name": "Damage", "type": "float"})"));
	CHECK(
		whole.at("rows")[0]
		== J(R"({"id": "pistol", "values": {"Damage": 12.5, "Ammo": 12, "Automatic": false, "Name": "Pistol"}})"));
	CHECK(whole.at("rows").size() == 3);

	const Json page = fixture.Succeed("data.get", J(R"({"table": "Data/Weapons.csv", "limit": 1, "offset": 1})"));
	CHECK(page.at("rows").size() == 1);
	CHECK(page.at("rows")[0].at("id") == "rifle");

	const Json row = fixture.Succeed("data.get", J(R"({"table": "Data/Weapons.csv", "row": "rifle"})"));
	CHECK(row.at("row").at("values").at("Automatic") == true);
	CHECK(
		fixture.Succeed("data.get", J(R"({"table": "Data/Weapons.csv", "row": "rifle", "column": "Damage"})"))
			.at("value")
		== 30.0);

	// The table can be named by its handle.
	const std::string handle = tables.at("tables")[0].at("handle").get<std::string>();
	CHECK(
		fixture.Succeed("data.get", J(R"({"table": ")" + handle + R"(", "row": "knife", "column": "Name"})"))
			.at("value")
		== "Knife");

	fixture.Reject("data.get", J(R"({"table": "Data/Nope.csv"})"), CommandErrorCode::NotFound);
	fixture.Reject("data.get", J(R"({"table": "0x1234"})"), CommandErrorCode::NotFound);
	fixture.Reject("data.get", J(R"({"table": "Scenes/Main.gscene"})"), CommandErrorCode::NotFound); // not a data table
	fixture.Reject("data.get", J(R"({"table": "Data/Weapons.csv", "row": "axe"})"), CommandErrorCode::NotFound);
	fixture.Reject(
		"data.get", J(R"({"table": "Data/Weapons.csv", "column": "Damage"})"), CommandErrorCode::InvalidArguments);
	const CommandResult column = fixture.Reject(
		"data.get", J(R"({"table": "Data/Weapons.csv", "row": "rifle", "column": "Dmg"})"), CommandErrorCode::NotFound);
	CHECK(column.Message.find("its columns are: Id, Damage, Ammo, Automatic, Name") != std::string::npos);
}

TEST_CASE("Automation: data.set changes a cell, is undoable and is saved with the project")
{
	Weapons weapons;
	AutomationFixture& fixture = weapons.Fixture;
	const std::string original = weapons.Csv();

	Json set;
	fixture.Succeed(
		"data.set", J(R"({"table": "Data/Weapons.csv", "row": "pistol", "column": "Damage", "value": 14})"));
	CHECK(weapons.Csv() != original);
	CHECK(
		fixture.Succeed("data.get", J(R"({"table": "Data/Weapons.csv", "row": "pistol", "column": "Damage"})"))
			.at("value")
		== 14.0);
	CHECK(fixture.Context.GetUndoStack().IsDirty("Data/Weapons.csv"));
	CHECK(weapons.FileText() == original); // not written yet

	set = fixture.Succeed(
		"data.set", J(R"({"table": "Data/Weapons.csv", "row": "pistol", "column": "Name", "value": "Hand cannon"})"));
	CHECK(set.at("previous") == "Pistol");
	CHECK(set.at("value") == "Hand cannon");
	fixture.Succeed(
		"data.set",
		J(R"({"table": "Data/Weapons.csv", "row": "pistol", "column": "Ammo", "value": "99"})")); // CSV notation
	fixture.Succeed(
		"data.set", J(R"({"table": "Data/Weapons.csv", "row": "pistol", "column": "Automatic", "value": "TRUE"})"));
	CHECK(
		fixture.Succeed("data.get", J(R"({"table": "Data/Weapons.csv", "row": "pistol"})")).at("row").at("values")
		== J(R"({"Damage": 14.0, "Ammo": 99, "Automatic": true, "Name": "Hand cannon"})"));

	fixture.Succeed("undo", J(R"({"steps": 4})"));
	CHECK(weapons.Csv() == original);
	CHECK_FALSE(fixture.Context.GetUndoStack().IsDirty("Data/Weapons.csv")); // undone back to the file
	fixture.Succeed("redo", J(R"({"steps": 4})"));
	CHECK(fixture.Context.GetUndoStack().IsDirty("Data/Weapons.csv"));

	const Result<std::vector<std::string>> saved = fixture.Context.SaveAll();
	REQUIRE(saved.HasValue());
	CHECK(*saved == std::vector<std::string>{ "Data/Weapons.csv" });
	CHECK(weapons.FileText().find("pistol,14.0,99,true,Hand cannon") != std::string::npos);
	CHECK_FALSE(fixture.Context.GetUndoStack().IsDirty("Data/Weapons.csv"));
}

TEST_CASE("Automation: data.set checks the value against the column")
{
	Weapons weapons;
	AutomationFixture& fixture = weapons.Fixture;
	const std::string original = weapons.Csv();
	const auto reject = [&](std::string_view cell, CommandErrorCode code = CommandErrorCode::InvalidArguments) {
		return fixture
			.Reject("data.set", J(R"({"table": "Data/Weapons.csv", "row": "rifle", )" + std::string(cell) + "}"), code)
			.Message;
	};

	CHECK(reject(R"("column": "Ammo", "value": 12.5)") == "column 'Ammo' holds int values: expected an integer");
	CHECK(
		reject(R"("column": "Ammo", "value": "many")").find("column 'Ammo': 'many' is not a valid int")
		!= std::string::npos);
	CHECK(reject(R"("column": "Damage", "value": true)") == "column 'Damage' holds float values: expected a number");
	CHECK(
		reject(R"("column": "Automatic", "value": 1)")
		== "column 'Automatic' holds bool values: expected true or false");
	CHECK(reject(R"("column": "Name", "value": 5)") == "column 'Name' holds string values: expected a string");
	CHECK(reject(R"("column": "Id", "value": "x")") == "'Id' is the column of the row ids and cannot be set");
	CHECK(
		reject(R"("column": "Ghost", "value": 1)", CommandErrorCode::NotFound)
			.find("its columns are: Damage, Ammo, Automatic, Name")
		!= std::string::npos);
	CHECK(
		reject(R"("column": "Damage")", CommandErrorCode::InvalidArguments).find("missing required argument 'value'")
		!= std::string::npos);
	fixture.Reject(
		"data.set", J(R"({"table": "Data/Weapons.csv", "row": "axe", "column": "Name", "value": "x"})"),
		CommandErrorCode::NotFound);
	fixture.Reject(
		"data.set", J(R"({"table": "Data/Nope.csv", "row": "axe", "column": "Name", "value": "x"})"),
		CommandErrorCode::NotFound);
	CHECK(weapons.Csv() == original);
	CHECK_FALSE(fixture.Context.GetUndoStack().IsDirty("Data/Weapons.csv"));
}

TEST_CASE("Automation: data.add-row and data.remove-row change the rows and undo restores their place")
{
	Weapons weapons;
	AutomationFixture& fixture = weapons.Fixture;
	const std::string original = weapons.Csv();

	const Json added = fixture.Succeed(
		"data.add-row",
		J(R"({"table": "Data/Weapons.csv", "row": "bow", "values": {"Damage": 8.25, "Name": "Bow"}, "index": 1})"));
	CHECK(added.at("rowCount") == 4);
	CHECK(
		added.at("row")
		== J(R"({"id": "bow", "values": {"Damage": 8.25, "Ammo": 0, "Automatic": false, "Name": "Bow"}})"));
	CHECK(
		fixture.Succeed("data.get", J(R"({"table": "Data/Weapons.csv", "limit": 2})")).at("rows")[1].at("id") == "bow");
	fixture.Succeed("data.add-row", J(R"({"table": "Data/Weapons.csv", "row": "empty"})")); // defaults, at the end
	CHECK(fixture.Succeed("data.get", J(R"({"table": "Data/Weapons.csv"})")).at("rows")[4].at("id") == "empty");

	fixture.Succeed("undo", J(R"({"steps": 2})"));
	CHECK(weapons.Csv() == original);

	// Removing a row from the middle and undoing puts it back at the same position with the same cells.
	const Json removed = fixture.Succeed("data.remove-row", J(R"({"table": "Data/Weapons.csv", "row": "rifle"})"));
	CHECK(removed.at("rowCount") == 2);
	CHECK(removed.at("removed") == "rifle");
	CHECK(weapons.Csv() != original);
	fixture.Succeed("undo");
	CHECK(weapons.Csv() == original);
	fixture.Succeed("redo");
	fixture.Reject("data.get", J(R"({"table": "Data/Weapons.csv", "row": "rifle"})"), CommandErrorCode::NotFound);
	fixture.Succeed("undo");

	// The first row and the last row too.
	for (const char* row : { "pistol", "knife" })
	{
		fixture.Succeed("data.remove-row", J(std::string(R"({"table": "Data/Weapons.csv", "row": ")") + row + R"("})"));
		fixture.Succeed("undo");
		CHECK(weapons.Csv() == original);
	}
}

TEST_CASE("Automation: data.add-row and data.remove-row explain their refusals")
{
	Weapons weapons;
	AutomationFixture& fixture = weapons.Fixture;
	const std::string original = weapons.Csv();

	fixture.Reject(
		"data.add-row", J(R"({"table": "Data/Weapons.csv", "row": "pistol"})"), CommandErrorCode::AlreadyExists);
	fixture.Reject(
		"data.add-row", J(R"({"table": "Data/Weapons.csv", "row": ""})"), CommandErrorCode::InvalidArguments);
	const CommandResult column = fixture.Reject(
		"data.add-row", J(R"({"table": "Data/Weapons.csv", "row": "x", "values": {"Dmg": 1}})"),
		CommandErrorCode::InvalidArguments);
	CHECK(column.Message.find("its columns are: Damage, Ammo, Automatic, Name") != std::string::npos);
	fixture.Reject(
		"data.add-row", J(R"({"table": "Data/Weapons.csv", "row": "x", "values": {"Id": "y"}})"),
		CommandErrorCode::InvalidArguments);
	fixture.Reject(
		"data.add-row", J(R"({"table": "Data/Weapons.csv", "row": "x", "values": {"Ammo": "lots"}})"),
		CommandErrorCode::InvalidArguments);
	fixture.Reject(
		"data.add-row", J(R"({"table": "Data/Weapons.csv", "row": "x", "index": -1})"),
		CommandErrorCode::InvalidArguments);
	fixture.Reject("data.remove-row", J(R"({"table": "Data/Weapons.csv", "row": "axe"})"), CommandErrorCode::NotFound);
	fixture.Reject("data.remove-row", J(R"({"table": "Data/Nope.csv", "row": "axe"})"), CommandErrorCode::NotFound);
	CHECK(weapons.Csv() == original);
	CHECK_FALSE(fixture.Context.GetUndoStack().IsDirty("Data/Weapons.csv"));
}
