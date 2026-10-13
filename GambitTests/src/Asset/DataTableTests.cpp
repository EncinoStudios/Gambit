#include <doctest/doctest.h>

#include "TestUtils/TempDirectory.h"

#include "Gambit/Asset/DataTable.h"

#include <cmath>
#include <limits>
#include <string>
#include <vector>

using namespace Gambit;
using namespace Gambit::Tests;

namespace
{
	Ref<DataTable> ParseOk(std::string_view text)
	{
		Result<Ref<DataTable>> table = DataTable::Parse(text, "table.csv");
		REQUIRE_MESSAGE(table.HasValue(), (table.HasValue() ? std::string() : table.GetError().Message));
		return *table;
	}

	std::string ParseError(std::string_view text)
	{
		const Result<Ref<DataTable>> table = DataTable::Parse(text, "table.csv");
		REQUIRE_FALSE(table.HasValue());
		return table.GetError().Message;
	}

	std::string Text(const DataTable& table, std::string_view row, std::string_view column)
	{
		return std::get<std::string>(*table.GetValue(row, column));
	}
}

TEST_CASE("Asset: a CSV file becomes a table with typed columns")
{
	const Ref<DataTable> table = ParseOk("Id,Damage:float,Ammo:int,Auto:bool,Name:string,Icon:asset,Owner:entity\n"
	                                     "pistol,12.5,12,false,Pistol,0x00000000000000AB,\n"
	                                     "rifle,30,30,TRUE,\"The \"\"Rifle\"\"\",,0x0000000000000007\n");
	REQUIRE(table->GetColumns().size() == 7);
	CHECK(table->GetColumns()[0].Name == "Id");
	CHECK(table->GetColumns()[0].Type == DataColumnType::String);
	CHECK_FALSE(table->GetColumns()[0].TypeDeclared);
	CHECK(table->GetColumns()[1].Type == DataColumnType::Float);
	CHECK(table->GetColumns()[1].TypeDeclared);
	CHECK(table->GetColumns()[5].Type == DataColumnType::Asset);
	CHECK(table->GetColumns()[6].Type == DataColumnType::Entity);
	CHECK(table->GetRowCount() == 2);
	CHECK(table->HasRow("pistol"));
	CHECK_FALSE(table->HasRow("Pistol")); // ids are case-sensitive
	CHECK(table->GetType() == AssetType::DataTable);

	CHECK(*table->GetValue("pistol", "Id") == FieldValue(std::string("pistol")));
	CHECK(*table->GetValue("pistol", "Damage") == FieldValue(12.5));
	CHECK(*table->GetValue("pistol", "Ammo") == FieldValue(int64_t{ 12 }));
	CHECK(*table->GetValue("pistol", "Auto") == FieldValue(false));
	CHECK(*table->GetValue("rifle", "Auto") == FieldValue(true));
	CHECK(*table->GetValue("pistol", "Icon") == FieldValue(UUID(0xAB)));
	CHECK(*table->GetValue("pistol", "Owner") == FieldValue(UUID())); // empty: the null UUID
	CHECK(*table->GetValue("rifle", "Damage") == FieldValue(30.0));
	CHECK(Text(*table, "rifle", "Name") == "The \"Rifle\"");
	CHECK(*table->GetValue("rifle", "Owner") == FieldValue(UUID(7)));
	CHECK(table->GetRow("rifle")->Values.size() == 7);
	CHECK(table->GetRows()[0].Id == "pistol");
	CHECK(table->GetDependencies() == std::vector<AssetHandle>{ UUID(0xAB) });
}

TEST_CASE("Asset: untyped columns are inferred from their cells")
{
	const Ref<DataTable> table = ParseOk("Id,Count,Ratio,Mixed,Flag,Code,Blank,Word,Neg\n"
	                                     "a,1,1,1,true,007,1,x,-5\n"
	                                     "b,2,2.5,2.5,False,008,,y,0\n"
	                                     "c,30,3,c,TRUE,009,3,z,12\n");
	const auto type = [&](const char* name) { return table->GetColumns()[*table->FindColumnIndex(name)].Type; };
	CHECK(type("Count") == DataColumnType::Int);
	CHECK(type("Ratio") == DataColumnType::Float);  // a decimal makes the whole column float
	CHECK(type("Mixed") == DataColumnType::String); // a word makes it text
	CHECK(type("Flag") == DataColumnType::Bool);
	CHECK(type("Code") == DataColumnType::String);  // "007" is not a canonical integer: its text must survive
	CHECK(type("Blank") == DataColumnType::String); // a blank cell keeps the column text
	CHECK(type("Word") == DataColumnType::String);
	CHECK(type("Neg") == DataColumnType::Int);
	CHECK_FALSE(table->GetColumns()[*table->FindColumnIndex("Count")].TypeDeclared);
	CHECK(*table->GetValue("b", "Ratio") == FieldValue(2.5));
	CHECK(*table->GetValue("a", "Ratio") == FieldValue(1.0));
	CHECK(*table->GetValue("b", "Flag") == FieldValue(false));
	CHECK(*table->GetValue("c", "Count") == FieldValue(int64_t{ 30 }));
	CHECK(Text(*table, "a", "Code") == "007");

	// Without rows there is nothing to infer from: text.
	CHECK(ParseOk("Id,Value\n")->GetColumns()[1].Type == DataColumnType::String);
	// The first column is always text, even when it looks like numbers.
	const Ref<DataTable> numeric = ParseOk("Id,Value\n1,10\n2,20\n");
	CHECK(numeric->GetColumns()[0].Type == DataColumnType::String);
	CHECK(numeric->HasRow("2"));
	CHECK(numeric->GetColumns()[1].Type == DataColumnType::Int);
}

TEST_CASE("Asset: typed columns read an empty cell as the default value")
{
	const Ref<DataTable> table = ParseOk("Id,I:int,F:float,B:bool,S:string,A:asset\n"
	                                     "x,,,,,\n");
	CHECK(*table->GetValue("x", "I") == FieldValue(int64_t{ 0 }));
	CHECK(*table->GetValue("x", "F") == FieldValue(0.0));
	CHECK(*table->GetValue("x", "B") == FieldValue(false));
	CHECK(*table->GetValue("x", "S") == FieldValue(std::string()));
	CHECK(*table->GetValue("x", "A") == FieldValue(UUID()));
}

TEST_CASE("Asset: CSV quoting, line breaks and byte order marks follow RFC 4180")
{
	// Commas, quotes and line breaks inside quoted fields; CRLF line ends; a byte order mark.
	const Ref<DataTable> table = ParseOk("\xEF\xBB\xBFId,Text\r\n"
	                                     "one,\"Hello, world\"\r\n"
	                                     "two,\"line1\r\nline2\"\r\n"
	                                     "three,\"a \"\"quoted\"\" word\"\r\n"
	                                     "four,\r\n"
	                                     "five,\"\"\r\n"
	                                     "six,  padded  \r\n");
	REQUIRE(table->GetRowCount() == 6);
	CHECK(Text(*table, "one", "Text") == "Hello, world");
	CHECK(Text(*table, "two", "Text") == "line1\r\nline2");
	CHECK(Text(*table, "three", "Text") == "a \"quoted\" word");
	CHECK(Text(*table, "four", "Text").empty());
	CHECK(Text(*table, "five", "Text").empty());
	CHECK(Text(*table, "six", "Text") == "  padded  "); // spaces are data

	// Reading and writing again changes nothing: BOM, CRLF and quoting come back.
	CHECK(table->ToCsv().starts_with("\xEF\xBB\xBF"
	                                 "Id,Text\r\n"));
	const Ref<DataTable> again = ParseOk(table->ToCsv());
	CHECK(again->ToCsv() == table->ToCsv());
	CHECK(Text(*again, "two", "Text") == "line1\r\nline2");
	CHECK(table->ToCsv().find("\"Hello, world\"") != std::string::npos);
	CHECK(table->ToCsv().find("\"a \"\"quoted\"\" word\"") != std::string::npos);

	// LF files stay LF, files without a final line break are fine, and so are blank lines.
	const Ref<DataTable> lf = ParseOk("Id,V\n\na,1\n\n\nb,2");
	CHECK(lf->GetRowCount() == 2);
	CHECK(lf->ToCsv() == "Id,V\na,1\nb,2\n");
	CHECK(ParseOk("Id\nonly\n")->GetRowCount() == 1); // a single column
	CHECK(ParseOk("Id,V\na,\n")->GetRowCount() == 1); // a trailing comma at the end of the text
	CHECK(ParseOk("Id,V\na,")->GetRowCount() == 1);
}

TEST_CASE("Asset: writing a table reads back the same table")
{
	const Ref<DataTable> table = ParseOk("Id,Damage:float,Ammo,Note:string,Icon:asset\n"
	                                     "pistol,0.1,12,\"comma, here\",0x000000000000000A\n"
	                                     "rifle,2.0,30,,\n"
	                                     "big,1e300,-5,\"multi\nline\",\n");
	const std::string csv = table->ToCsv();
	const Ref<DataTable> again = ParseOk(csv);
	CHECK(again->ToCsv() == csv);
	CHECK(again->GetColumns().size() == 5);
	CHECK(again->GetColumns()[2].Type == DataColumnType::Int);
	CHECK_FALSE(again->GetColumns()[2].TypeDeclared); // inferred columns are written without a type
	CHECK(csv.starts_with("Id,Damage:float,Ammo,Note:string,Icon:asset\n"));
	CHECK(*again->GetValue("pistol", "Damage") == FieldValue(0.1));
	CHECK(*again->GetValue("big", "Damage") == FieldValue(1e300));
	CHECK(csv.find("pistol,0.1,12,") != std::string::npos);
	CHECK(csv.find("rifle,2.0,30,,") != std::string::npos); // 2.0, not 2: the column stays float when read again

	// An inferred float column of whole numbers stays float after a round trip.
	const Ref<DataTable> whole = ParseOk("Id,Value\na,1.0\nb,2.0\n");
	CHECK(whole->GetColumns()[1].Type == DataColumnType::Float);
	CHECK(ParseOk(whole->ToCsv())->GetColumns()[1].Type == DataColumnType::Float);
}

TEST_CASE("Asset: floats are exact and locale independent")
{
	const Ref<DataTable> table = ParseOk("Id,V:float\n"
	                                     "a,0.1\nb,123456789.123456789\nc,-0.0\nd,.5\ne,5.\nf,+7\ng,1E3\nh,2.5e-5\n");
	CHECK(*table->GetValue("a", "V") == FieldValue(0.1));
	CHECK(*table->GetValue("b", "V") == FieldValue(123456789.123456789));
	CHECK(std::signbit(std::get<double>(*table->GetValue("c", "V"))));
	CHECK(*table->GetValue("d", "V") == FieldValue(0.5));
	CHECK(*table->GetValue("e", "V") == FieldValue(5.0));
	CHECK(*table->GetValue("f", "V") == FieldValue(7.0));
	CHECK(*table->GetValue("g", "V") == FieldValue(1000.0));
	CHECK(*table->GetValue("h", "V") == FieldValue(2.5e-5));
	const Ref<DataTable> again = ParseOk(table->ToCsv());
	for (const char* row : { "a", "b", "c", "d", "e", "f", "g", "h" })
	{
		CHECK_MESSAGE(*again->GetValue(row, "V") == *table->GetValue(row, "V"), row);
	}
}

TEST_CASE("Asset: malformed tables are errors with line and column")
{
	CHECK(ParseError("").find("no header row") != std::string::npos);
	CHECK(ParseError("\n\n").find("no header row") != std::string::npos);

	// Header problems.
	CHECK(
		ParseError("Id,Dmg:flot\na,1\n")
		== "table.csv: line 1, column 4: column 'Dmg': unknown type 'flot' (expected int, float, bool, string, asset "
		   "or entity)");
	CHECK(
		ParseError("Id:int,V\n1,2\n").find("the first column, 'Id', holds the row ids and cannot be int")
		!= std::string::npos);
	CHECK(ParseError("Id,Id\na,b\n").find("column 'Id' appears twice") != std::string::npos);
	CHECK(ParseError("Id,2fast\na,b\n").find("'2fast' is not a valid column name") != std::string::npos);
	CHECK(ParseError("Id,two words\na,b\n").find("not a valid column name") != std::string::npos);
	CHECK(ParseError(",V\na,b\n").find("not a valid column name") != std::string::npos);

	// Rows: shape.
	CHECK(ParseError("Id,A,B\nx,1\n") == "table.csv: line 2: expected 3 fields (the header has 3 columns), found 2");
	CHECK(ParseError("Id,A\nx,1,2\n").find("line 2: expected 2 fields") != std::string::npos);

	// Rows: ids.
	CHECK(ParseError("Id,A\n,1\n") == "table.csv: line 2, column 1: the row id is empty");
	CHECK(
		ParseError("Id,A\nx,1\ny,2\nx,3\n")
		== "table.csv: line 4, column 1: duplicate row id 'x' (first used on line 2)");

	// Rows: values.
	CHECK(ParseError("Id,A:int\nx,abc\n") == "table.csv: line 2, column 3: column 'A': 'abc' is not a valid int");
	CHECK(ParseError("Id,A:int\nx,1.5\n").find("'1.5' is not a valid int") != std::string::npos);
	CHECK(ParseError("Id,A:int\nx,99999999999999999999\n").find("out of the range of an int") != std::string::npos);
	CHECK(ParseError("Id,A:float\nx,1.2.3\n").find("'1.2.3' is not a valid float") != std::string::npos);
	CHECK(ParseError("Id,A:float\nx,nan\n").find("not a valid float") != std::string::npos);
	CHECK(ParseError("Id,A:float\nx,inf\n").find("not a valid float") != std::string::npos);
	CHECK(ParseError("Id,A:float\nx,1e999\n").find("out of the range of a float") != std::string::npos);
	CHECK(ParseError("Id,A:bool\nx,yes\n").find("'yes' is not a valid bool") != std::string::npos);
	CHECK(ParseError("Id,A:asset\nx,1234\n").find("'1234' is not a UUID") != std::string::npos);
	CHECK(ParseError("Id,A:entity\nx,0xZZ\n").find("not a UUID") != std::string::npos);
	CHECK(ParseError("Id,A:int\nx,1\ny,2\nz,oops\n").find("line 4, column 3") != std::string::npos);

	// Quoting.
	CHECK(ParseError("Id,A\nx,\"never closed\n") == "table.csv: line 2, column 3: the quoted field is never closed");
	CHECK(
		ParseError("Id,A\nx,ab\"c\n").find("line 2, column 5: a quote inside an unquoted field") != std::string::npos);
	CHECK(ParseError("Id,A\nx,\"ab\"c\n").find("unexpected text after the closing quote") != std::string::npos);
	CHECK(ParseError("Id,A\nx,1\ry,2\n").find("carriage return without a line feed") != std::string::npos);

	// Positions count the lines inside quoted fields, too, also for the cells that follow one in its record.
	CHECK(ParseError("Id,A\nx,\"multi\nline\"\ny,fine\nz\n").find("line 5: expected 2 fields") != std::string::npos);
	CHECK(
		ParseError("Id,Text,N:int\nx,\"two\nlines\",abc\n")
		== "table.csv: line 3, column 8: column 'N': 'abc' is not a valid int");

	// Text must be UTF-8 (spreadsheets also save "ANSI" CSV files, whose accents would be garbled).
	CHECK(
		ParseError("Id,Name\nx,caf\xE9"
	               "\n")
		== "table.csv: line 2, column 3: the text is not valid UTF-8 (save the file as UTF-8)");
	CHECK(ParseError("Id,Na\xFFme\nx,1\n").find("line 1, column 4: the text is not valid UTF-8") != std::string::npos);
	CHECK(Text(*ParseOk("Id,Name\nx,caf\xC3\xA9\n"), "x", "Name") == "caf\xC3\xA9");
}

TEST_CASE("Asset: table cells are read and changed with checks")
{
	const Ref<DataTable> table = ParseOk("Id,Damage:float,Ammo:int,Name\n"
	                                     "pistol,10,12,Pistol\n"
	                                     "rifle,30,30,Rifle\n");
	REQUIRE(table->SetValue("pistol", "Damage", FieldValue(11.5)).HasValue());
	CHECK(*table->GetValue("pistol", "Damage") == FieldValue(11.5));
	REQUIRE(table->SetValueFromText("pistol", "Ammo", "15").HasValue());
	CHECK(*table->GetValue("pistol", "Ammo") == FieldValue(int64_t{ 15 }));
	REQUIRE(table->SetValueFromText("rifle", "Name", "Big gun").HasValue());
	CHECK(Text(*table, "rifle", "Name") == "Big gun");
	CHECK(table->ToCsv() == "Id,Damage:float,Ammo:int,Name\npistol,11.5,15,Pistol\nrifle,30.0,30,Big gun\n");

	const auto errorOf = [](const Result<void>& result) {
		REQUIRE_FALSE(result.HasValue());
		return result.GetError().Message;
	};
	CHECK(errorOf(table->SetValue("nobody", "Damage", FieldValue(1.0))).find("no row 'nobody'") != std::string::npos);
	CHECK(errorOf(table->SetValue("pistol", "Armor", FieldValue(1.0))).find("no column 'Armor'") != std::string::npos);
	CHECK(
		errorOf(table->SetValue("pistol", "Damage", FieldValue(int64_t{ 1 }))).find("holds float values")
		!= std::string::npos);
	CHECK(
		errorOf(table->SetValue("pistol", "Damage", FieldValue(INFINITY))).find("holds float values")
		!= std::string::npos);
	CHECK(
		errorOf(table->SetValue("pistol", "Damage", FieldValue(std::numeric_limits<double>::infinity()))).find("finite")
		!= std::string::npos);
	CHECK(
		errorOf(table->SetValue("pistol", "Id", FieldValue(std::string("x")))).find("RenameRow") != std::string::npos);
	CHECK(
		errorOf(table->SetValue("pistol", "Name", FieldValue(std::string("bad\xFF")))).find("UTF-8")
		!= std::string::npos);
	CHECK(
		errorOf(table->SetValueFromText("pistol", "Ammo", "many")).find("column 'Ammo': 'many' is not a valid int")
		!= std::string::npos);
	CHECK(errorOf(table->SetValueFromText("pistol", "Ghost", "1")).find("no column") != std::string::npos);
	CHECK(*table->GetValue("pistol", "Ammo") == FieldValue(int64_t{ 15 })); // unchanged
	CHECK_FALSE(table->GetValue("nobody", "Damage").HasValue());
	CHECK_FALSE(table->GetValue("pistol", "Ghost").HasValue());
	CHECK(table->GetRow("nobody") == nullptr);
}

TEST_CASE("Asset: rows are added, renamed and removed")
{
	const Ref<DataTable> table = ParseOk("Id,Damage:float,Name\na,1,A\nb,2,B\nc,3,C\n");
	const std::vector<FieldValue> values = { FieldValue(9.5), FieldValue(std::string("D")) };
	REQUIRE(table->AddRow("d", values).HasValue());
	REQUIRE(table->AddRow("e").HasValue()); // defaults
	CHECK(table->GetRowCount() == 5);
	CHECK(*table->GetValue("d", "Damage") == FieldValue(9.5));
	CHECK(*table->GetValue("e", "Damage") == FieldValue(0.0));
	CHECK(Text(*table, "e", "Name").empty());
	CHECK(Text(*table, "e", "Id") == "e");

	REQUIRE(table->RenameRow("b", "bee").HasValue());
	CHECK_FALSE(table->HasRow("b"));
	CHECK(table->HasRow("bee"));
	CHECK(Text(*table, "bee", "Id") == "bee");
	CHECK(table->GetRows()[1].Id == "bee"); // keeps its place
	REQUIRE(table->RenameRow("bee", "bee").HasValue());

	REQUIRE(table->RemoveRow("a").HasValue());
	CHECK(table->GetRowCount() == 4);
	CHECK(table->GetRows()[0].Id == "bee");
	CHECK(table->HasRow("e")); // the index follows the rows
	CHECK(*table->GetValue("d", "Damage") == FieldValue(9.5));

	CHECK_FALSE(table->AddRow("d").HasValue());    // duplicate
	CHECK_FALSE(table->AddRow("").HasValue());     // empty id
	CHECK_FALSE(table->AddRow("\xFF").HasValue()); // not UTF-8
	CHECK_FALSE(table->AddRow("f", std::vector<FieldValue>{ FieldValue(1.0), FieldValue(std::string("\xC3")) })
	                .HasValue()); // a string that is not UTF-8
	CHECK_FALSE(table->AddRow("f", std::vector<FieldValue>{ FieldValue(1.0) }).HasValue()); // too few values
	CHECK_FALSE(table->AddRow("f", std::vector<FieldValue>{ FieldValue(1), FieldValue(std::string("x")) })
	                .HasValue()); // wrong type
	CHECK_FALSE(table->HasRow("f"));
	CHECK_FALSE(table->RemoveRow("nope").HasValue());
	CHECK_FALSE(table->RenameRow("nope", "x").HasValue());
	CHECK_FALSE(table->RenameRow("d", "bee").HasValue());
	CHECK_FALSE(table->RenameRow("d", "").HasValue());
	CHECK_FALSE(table->RenameRow("d", "\xFF").HasValue());

	const Ref<DataTable> again = ParseOk(table->ToCsv());
	CHECK(again->GetRowCount() == 4);
	CHECK(again->ToCsv() == table->ToCsv());
}

TEST_CASE("Asset: tables can be created empty")
{
	CHECK_FALSE(DataTable::Create({}).HasValue());
	CHECK_FALSE(DataTable::Create({ { "Id", DataColumnType::Int, false } }).HasValue()); // the id column is text
	CHECK_FALSE(DataTable::Create({ { "Id", DataColumnType::String, false }, { "Id", DataColumnType::Int, false } })
	                .HasValue());
	CHECK_FALSE(DataTable::Create({ { "bad name", DataColumnType::String, false } }).HasValue());
	std::vector<DataColumn> tooMany;
	for (size_t index = 0; index <= DataTable::MaxColumns; ++index)
	{
		tooMany.push_back({ "C" + std::to_string(index), DataColumnType::String, false });
	}
	CHECK_FALSE(DataTable::Create(tooMany).HasValue());

	const Result<Ref<DataTable>> created =
		DataTable::Create({ { "Id", DataColumnType::String, true }, { "Score", DataColumnType::Int, true } });
	REQUIRE(created.HasValue());
	REQUIRE((*created)->AddRow("a", std::vector<FieldValue>{ FieldValue(int64_t{ 5 }) }).HasValue());
	CHECK((*created)->ToCsv() == "Id:string,Score:int\na,5\n");
}

TEST_CASE("Asset: tables are saved and loaded through the VFS")
{
	TempDirectory directory;
	const Ref<DataTable> original = ParseOk("\xEF\xBB\xBFId,Damage:float,Name\r\npistol,10,\"Pi,stol\"\r\n");
	REQUIRE(original->Save(directory.GetVFS(), "Data/Weapons.csv").HasValue());
	CHECK(directory.ReadFile("Data/Weapons.csv") == original->ToCsv()); // bytes: BOM and CRLF kept
	CHECK(directory.ReadFile("Data/Weapons.csv").starts_with("\xEF\xBB\xBF"));

	const Result<Ref<DataTable>> loaded = DataTable::Load(directory.GetVFS(), "Data/Weapons.csv");
	REQUIRE(loaded.HasValue());
	CHECK((*loaded)->ToCsv() == original->ToCsv());
	CHECK(Text(**loaded, "pistol", "Name") == "Pi,stol");

	const Result<Ref<DataTable>> missing = DataTable::Load(directory.GetVFS(), "Data/Nope.csv");
	REQUIRE_FALSE(missing.HasValue());
	CHECK(missing.GetError().Message.find("Data/Nope.csv") != std::string::npos);
	CHECK(HasFileError(missing.GetError(), FileError::NotFound));

	directory.WriteFile("Data/Broken.csv", "Id,V:int\na,oops\n");
	const Result<Ref<DataTable>> broken = DataTable::Load(directory.GetVFS(), "Data/Broken.csv");
	REQUIRE_FALSE(broken.HasValue());
	CHECK(broken.GetError().Message.starts_with("Data/Broken.csv: line 2, column 3: "));
}

TEST_CASE("Asset: column type names")
{
	for (const DataColumnType type : { DataColumnType::Int, DataColumnType::Float, DataColumnType::Bool,
	                                   DataColumnType::String, DataColumnType::Asset, DataColumnType::Entity })
	{
		CHECK(ParseDataColumnType(ToString(type)) == type);
	}
	CHECK(ParseDataColumnType("FLOAT") == DataColumnType::Float);
	CHECK_FALSE(ParseDataColumnType("double").has_value());
	CHECK(ToFieldType(DataColumnType::Int) == FieldType::Int64);
	CHECK(ToFieldType(DataColumnType::Float) == FieldType::Double);
	CHECK(ToFieldType(DataColumnType::Asset) == FieldType::AssetHandle);
	CHECK(ToFieldType(DataColumnType::Entity) == FieldType::Entity);

	// Values format as the file writes them and read back.
	CHECK(DataTable::FormatValue(DataColumnType::Float, FieldValue(3.0)) == "3.0");
	CHECK(DataTable::FormatValue(DataColumnType::Int, FieldValue(int64_t{ -4 })) == "-4");
	CHECK(DataTable::FormatValue(DataColumnType::Bool, FieldValue(true)) == "true");
	CHECK(DataTable::FormatValue(DataColumnType::Asset, FieldValue(UUID())).empty());
	CHECK(DataTable::FormatValue(DataColumnType::Asset, FieldValue(UUID(1))) == "0x0000000000000001");
	CHECK(*DataTable::ParseValue(DataColumnType::Bool, "False") == FieldValue(false));
}

TEST_CASE("Asset: rows can be inserted at a position, which undoing a removal needs")
{
	const Ref<DataTable> table = ParseOk("Id,Damage:float,Name\na,1,A\nb,2,B\nc,3,C\n");
	CHECK(table->FindRowIndex("a") == size_t{ 0 });
	CHECK(table->FindRowIndex("c") == size_t{ 2 });
	CHECK_FALSE(table->FindRowIndex("nope").has_value());

	const std::vector<FieldValue> values = { FieldValue(7.5), FieldValue(std::string("X")) };
	REQUIRE(table->InsertRow(1, "x", values).HasValue());
	REQUIRE(table->InsertRow(0, "first").HasValue());
	REQUIRE(table->InsertRow(100, "last").HasValue()); // beyond the end: appended
	CHECK(table->GetRowCount() == 6);
	std::string order;
	for (const DataRow& row : table->GetRows())
	{
		order += row.Id + ",";
	}
	CHECK(order == "first,a,x,b,c,last,");
	CHECK(table->FindRowIndex("x") == size_t{ 2 });
	CHECK(*table->GetValue("x", "Damage") == FieldValue(7.5)); // the index follows the rows
	CHECK(*table->GetValue("c", "Damage") == FieldValue(3.0));

	// Removing a row and putting it back where it was restores the file text.
	const std::string before = table->ToCsv();
	const size_t position = *table->FindRowIndex("b");
	const DataRow removed = *table->GetRow("b");
	REQUIRE(table->RemoveRow("b").HasValue());
	REQUIRE(table->InsertRow(position, "b", std::span<const FieldValue>(removed.Values).subspan(1)).HasValue());
	CHECK(table->ToCsv() == before);

	CHECK_FALSE(table->InsertRow(0, "a").HasValue());
	CHECK_FALSE(table->InsertRow(0, "").HasValue());
}
