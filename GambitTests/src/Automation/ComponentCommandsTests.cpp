#include <doctest/doctest.h>

#include "AutomationFixture.h"

#include <map>

using namespace Gambit;
using namespace Gambit::Tests;

namespace
{
	struct SealedStats
	{
		int32_t Level = 1;
		int32_t Serial = 7; // read-only: tools show it, code writes it
	};

	struct SealedComponent
	{
		SealedStats Stats;
		std::vector<SealedStats> History{ SealedStats{} };
	};

	void RegisterSealed(TypeRegistry& types)
	{
		types.RegisterStruct<SealedStats>("SealedStats")
			.Field("Level", &SealedStats::Level)
			.Field("Serial", &SealedStats::Serial, Attr::ReadOnly{});
		types.RegisterComponent<SealedComponent>("Sealed")
			.Field("Stats", &SealedComponent::Stats)
			.Field("History", &SealedComponent::History);
	}

	struct Subject
	{
		AutomationFixture Fixture;
		std::string Uuid;

		Subject()
		{
			Fixture.CreateProject();
			Uuid = Fixture.Create("Subject");
		}

		Json Arguments(std::string_view component, std::string_view values = {}) const
		{
			Json arguments = Json::object();
			arguments["entity"] = Uuid;
			arguments["component"] = std::string(component);
			if (!values.empty())
			{
				arguments["values"] = Json::parse(values);
			}
			return arguments;
		}

		template<typename T>
		T& Get()
		{
			return Fixture.Scene().TryGetEntity(*UUID::Parse(Uuid)).GetComponent<T>();
		}
	};
}

TEST_CASE("Automation: component.add adds a component with initial values")
{
	Subject subject;
	AutomationFixture& fixture = subject.Fixture;

	fixture.CheckUndoRedo([&] {
		const Json added = fixture.Succeed("component.add", subject.Arguments("Health", R"({"Maximum": 40})"));
		CHECK(added.at("entity") == subject.Uuid);
		CHECK(added.at("component") == "Health");
		CHECK(added.at("values") == J(R"({"Current": 100.0, "Maximum": 40.0})"));
	});
	CHECK(subject.Get<HealthComponent>().Maximum == 40.0f);

	// Without values: the defaults. Tags have no fields.
	fixture.Succeed("component.add", subject.Arguments("AllTypes"));
	CHECK(subject.Get<AllTypesComponent>().Int32 == -7);
	fixture.CheckUndoRedo([&] { fixture.Succeed("component.add", subject.Arguments("PlayerTag")); });

	// Disabling an entity through the component makes it inactive; undo makes it active again.
	REQUIRE(fixture.Scene().IsActive(fixture.Scene().TryGetEntity(*UUID::Parse(subject.Uuid))));
	fixture.Succeed("component.add", subject.Arguments("Disabled"));
	CHECK_FALSE(fixture.Scene().IsActive(fixture.Scene().TryGetEntity(*UUID::Parse(subject.Uuid))));
	fixture.Succeed("undo");
	CHECK(fixture.Scene().IsActive(fixture.Scene().TryGetEntity(*UUID::Parse(subject.Uuid))));
}

TEST_CASE("Automation: component.add refuses what cannot be added")
{
	Subject subject;
	AutomationFixture& fixture = subject.Fixture;
	fixture.Succeed("component.add", subject.Arguments("Health"));
	const uint64_t before = fixture.Hash();

	const CommandResult exists =
		fixture.Reject("component.add", subject.Arguments("Health"), CommandErrorCode::AlreadyExists);
	CHECK(exists.Message.find("component.set changes it") != std::string::npos);
	fixture.Reject("component.add", subject.Arguments("Tag"), CommandErrorCode::AlreadyExists);
	fixture.Reject("component.add", subject.Arguments("Relationship"), CommandErrorCode::Conflict);
	fixture.Reject("component.add", subject.Arguments("ID"), CommandErrorCode::Conflict);
	fixture.Reject("component.add", subject.Arguments("Inactive"), CommandErrorCode::Conflict);
	fixture.Reject("component.add", subject.Arguments("Missing"), CommandErrorCode::NotFound);
	fixture.Reject(
		"component.add", subject.Arguments("AllTypes", R"({"Int32": "x"})"), CommandErrorCode::InvalidArguments);
	fixture.Reject(
		"component.add", subject.Arguments("PlayerTag", R"({"Anything": 1})"), CommandErrorCode::InvalidArguments);
	fixture.Reject("component.add", subject.Arguments("AllTypes", R"([])"), CommandErrorCode::InvalidArguments);
	CHECK(
		fixture.Reject("component.add", J(R"({"entity": "Nobody", "component": "Health"})"), CommandErrorCode::NotFound)
			.Message.find("Nobody")
		!= std::string::npos);
	CHECK(fixture.Hash() == before);
}

TEST_CASE("Automation: component.remove removes a component and undo restores every field")
{
	Subject subject;
	AutomationFixture& fixture = subject.Fixture;
	fixture.Succeed(
		"component.add",
		subject.Arguments(
			"AllTypes", R"({"String": "kept", "Vec3": [4, 5, 6], "Mode": "Fast", "Floats": [1.5, 2.5]})"));
	fixture.Succeed("component.add", subject.Arguments("Structs", R"({"Main": {"Clip": "run"}})"));
	// The runtime-only field is not stored, but it is part of the component and comes back with it.
	subject.Get<AllTypesComponent>().Cache = 3.0f;

	fixture.CheckUndoRedo([&] {
		const Json removed = fixture.Succeed("component.remove", subject.Arguments("AllTypes"));
		CHECK(removed.at("component") == "AllTypes");
	});
	CHECK_FALSE(fixture.Scene().TryGetEntity(*UUID::Parse(subject.Uuid)).HasComponent<AllTypesComponent>());
	fixture.Succeed("undo");
	const AllTypesComponent& restored = subject.Get<AllTypesComponent>();
	CHECK(restored.String == "kept");
	CHECK(restored.Vec3 == glm::vec3(4.0f, 5.0f, 6.0f));
	CHECK(restored.Mode == TestMode::Fast);
	CHECK(restored.Floats == std::vector<float>{ 1.5f, 2.5f });
	CHECK(restored.Cache == 3.0f);
	fixture.Succeed("redo");

	fixture.Succeed("component.remove", subject.Arguments("Structs"));
	fixture.Succeed("undo");
	CHECK(subject.Get<StructsComponent>().Main.Clip == "run");

	const CommandResult absent =
		fixture.Reject("component.remove", subject.Arguments("Health"), CommandErrorCode::NotFound);
	CHECK(absent.Message == "entity 'Subject' has no component 'Health'; it has: Structs, Tag");
	fixture.Reject("component.remove", subject.Arguments("Tag"), CommandErrorCode::Conflict); // not removable
	fixture.Reject("component.remove", subject.Arguments("Relationship"), CommandErrorCode::Conflict);
	fixture.Reject("component.remove", subject.Arguments("Missing"), CommandErrorCode::NotFound);
}

TEST_CASE("Automation: component.get shows the fields of a component")
{
	Subject subject;
	AutomationFixture& fixture = subject.Fixture;
	fixture.Succeed("component.add", subject.Arguments("Health", R"({"Current": 12.5})"));

	const Json health = fixture.Succeed("component.get", subject.Arguments("Health"));
	CHECK(health.at("values") == J(R"({"Current": 12.5, "Maximum": 100.0})"));
	CHECK(fixture.Succeed("component.get", subject.Arguments("Tag")).at("values") == J(R"({"Name": "Subject"})"));
	CHECK(fixture.Succeed("component.get", J(R"({"entity": "Subject", "component": "Relationship"})"))
	          .at("values")
	          .contains("Parent")); // reading engine-managed ones is fine

	fixture.Reject("component.get", subject.Arguments("Structs"), CommandErrorCode::NotFound);
	const CommandResult unknown =
		fixture.Reject("component.get", subject.Arguments("Helth"), CommandErrorCode::NotFound);
	CHECK(unknown.Message.find("did you mean 'Health'") != std::string::npos);
}

TEST_CASE("Automation: component.set changes fields and undo restores them")
{
	Subject subject;
	AutomationFixture& fixture = subject.Fixture;
	fixture.Succeed("component.add", subject.Arguments("Health"));
	fixture.Succeed("component.add", subject.Arguments("AllTypes"));

	fixture.CheckUndoRedo([&] {
		const Json set = fixture.Succeed("component.set", subject.Arguments("Health", R"({"Current": 80})"));
		CHECK(set.at("changed") == J(R"(["Current"])"));
		CHECK(set.at("values") == J(R"({"Current": 80.0, "Maximum": 100.0})"));
	});
	CHECK(fixture.Context.GetUndoStack().GetUndoDescription() == "Set Health.Current of 'Subject'");

	fixture.CheckUndoRedo([&] {
		const Json set = fixture.Succeed(
			"component.set",
			subject.Arguments(
				"AllTypes",
				R"({"Vec3": [9, 8, 7], "String": "hello world", "Mode": "Reverse", "Int32s": [5, 6], "Bool": false})"));
		CHECK(set.at("changed").size() == 5);
	});
	AllTypesComponent& all = subject.Get<AllTypesComponent>();
	CHECK(all.Vec3 == glm::vec3(9.0f, 8.0f, 7.0f));
	CHECK(all.String == "hello world");
	CHECK(all.Mode == TestMode::Reverse);
	CHECK(all.Int32s == std::vector<int32_t>{ 5, 6 });
	CHECK_FALSE(all.Bool);
	CHECK(all.Int32 == -7); // not named: unchanged
}

TEST_CASE("Automation: component.set merges struct values instead of resetting the members it does not name")
{
	Subject subject;
	AutomationFixture& fixture = subject.Fixture;
	fixture.Succeed(
		"component.add",
		subject.Arguments(
			"Structs",
			R"({"Main": {"Clip": "walk", "Weight": 0.75, "Stats": {"Level": 9, "Label": "kept"}, "Curve": [1, 2, 3]},
		                                 "Layers": [{"Clip": "a", "Weight": 0.25}, {"Clip": "b", "History": [{"Level": 1}, {"Level": 2}]}]})"));

	fixture.CheckUndoRedo([&] {
		const Json set = fixture.Succeed("component.set", subject.Arguments("Structs", R"({"Main": {"Weight": 0.5}})"));
		CHECK(set.at("changed") == J(R"(["Main"])"));
	});
	StructsComponent& structs = subject.Get<StructsComponent>();
	CHECK(structs.Main.Weight == 0.5f);
	CHECK(structs.Main.Clip == "walk"); // the rest of the struct keeps its values
	CHECK(structs.Main.Stats.Level == 9);
	CHECK(structs.Main.Curve == std::vector<float>{ 1.0f, 2.0f, 3.0f });

	fixture.CheckUndoRedo([&] {
		fixture.Succeed("component.set", subject.Arguments("Structs", R"({"Main": {"Stats": {"Level": 3}}})"));
	});
	CHECK(structs.Main.Stats.Level == 3);
	CHECK(structs.Main.Stats.Label == "kept");
	CHECK(structs.Main.Clip == "walk");

	// Elements of an array of structs merge by position.
	fixture.CheckUndoRedo([&] {
		fixture.Succeed(
			"component.set",
			subject.Arguments("Structs", R"({"Layers": [{"Weight": 0.1}, {"History": [{"Label": "changed"}, {}]}]})"));
	});
	REQUIRE(structs.Layers.size() == 2);
	CHECK(structs.Layers[0].Weight == 0.1f);
	CHECK(structs.Layers[0].Clip == "a");
	CHECK(structs.Layers[1].Clip == "b");
	REQUIRE(structs.Layers[1].History.size() == 2);
	CHECK(structs.Layers[1].History[0].Level == 1);
	CHECK(structs.Layers[1].History[0].Label == "changed");
	CHECK(structs.Layers[1].History[1].Level == 2);

	// A shorter array drops the other elements; undo brings them back.
	fixture.CheckUndoRedo(
		[&] { fixture.Succeed("component.set", subject.Arguments("Structs", R"({"Layers": [{}]})")); });
	CHECK(structs.Layers.size() == 1);
	fixture.Succeed("undo");
	CHECK(structs.Layers.size() == 2);
}

TEST_CASE("Automation: component.set leaves no undo entry when nothing changes")
{
	Subject subject;
	AutomationFixture& fixture = subject.Fixture;
	fixture.Succeed("component.add", subject.Arguments("Health", R"({"Current": 80})"));
	fixture.Succeed("scene.save");
	REQUIRE_FALSE(fixture.Context.GetUndoStack().IsDirty("Scenes/Main.gscene"));
	const size_t depth = fixture.Context.GetUndoStack().GetUndoHistory().size();

	const CommandResult same = fixture.Run("component.set", subject.Arguments("Health", R"({"Current": 80.0})"));
	REQUIRE(same.Ok);
	CHECK(same.Data.at("changed").empty());
	CHECK(same.Message == "Health already has these values");
	CHECK(fixture.Context.GetUndoStack().GetUndoHistory().size() == depth);
	CHECK_FALSE(fixture.Context.GetUndoStack().IsDirty("Scenes/Main.gscene"));

	// Only the fields that differ are recorded.
	const Json partial =
		fixture.Succeed("component.set", subject.Arguments("Health", R"({"Current": 80, "Maximum": 10})"));
	CHECK(partial.at("changed") == J(R"(["Maximum"])"));
}

TEST_CASE("Automation: component.set explains mistakes")
{
	Subject subject;
	AutomationFixture& fixture = subject.Fixture;
	fixture.Succeed("component.add", subject.Arguments("AllTypes"));
	const uint64_t before = fixture.Hash();

	const auto message = [&](std::string_view component, std::string_view values,
	                         CommandErrorCode code = CommandErrorCode::InvalidArguments) {
		return fixture.Reject("component.set", subject.Arguments(component, values), code).Message;
	};
	CHECK(message("AllTypes", R"({"Int33": 1})")
	          .starts_with("component 'AllTypes' has no field 'Int33' (did you mean 'Int32'?); its fields are:"));
	CHECK(message("AllTypes", R"({"Textures": []})") == "the field 'AllTypes.Textures' is read-only");
	CHECK(message("AllTypes", R"({"Cache": 1.0})") == "the field 'AllTypes.Cache' is runtime state and cannot be set");
	CHECK(message("AllTypes", R"({"Int32": "x"})") == "/AllTypes/Int32: expected an integer, found a string");
	CHECK(message("AllTypes", R"({"Int32": 9999999999})") == "/AllTypes/Int32: 9999999999 is out of range");
	CHECK(message("AllTypes", R"({"Vec3": [1, 2]})") == "/AllTypes/Vec3: expected 3 numbers, found 2");
	CHECK(message("AllTypes", R"({"Mode": "Warp"})")
	          .starts_with("/AllTypes/Mode: 'Warp' is not a value of enum 'TestMode'"));
	CHECK(message("AllTypes", R"({"Target": "nope"})").starts_with("/AllTypes/Target: 'nope' is not a UUID"));
	CHECK(
		message("AllTypes", R"({"Int32": 1, "Float": "x"})")
		== "/AllTypes/Float: expected a number, found a string"); // nothing is applied
	CHECK(message("AllTypes", "{}") == "'values' is empty: name the fields to set");
	CHECK(
		message("Relationship", R"({"Parent": "0x1"})", CommandErrorCode::Conflict)
		== "the component 'Relationship' is managed by the engine and cannot be edited directly");
	CHECK(message("Health", R"({"Current": 1})", CommandErrorCode::NotFound)
	          .starts_with("entity 'Subject' has no component 'Health'"));
	CHECK(message("Nothing", R"({"X": 1})", CommandErrorCode::NotFound)
	          .starts_with("there is no component type 'Nothing'"));
	fixture.Reject(
		"component.set", J(R"({"entity": "Subject", "component": "Tag"})"),
		CommandErrorCode::InvalidArguments); // values is required
	CHECK(fixture.Hash() == before);
}

TEST_CASE("Automation: component.set steps with the same merge key become one undo step")
{
	Subject subject;
	AutomationFixture& fixture = subject.Fixture;
	fixture.Succeed("component.add", subject.Arguments("Health", R"({"Current": 1})"));
	const uint64_t before = fixture.Hash();
	const size_t depth = fixture.Context.GetUndoStack().GetUndoHistory().size();

	// A gizmo drag sets the same field every frame.
	for (const int value : { 2, 3, 4, 5 })
	{
		Json arguments = subject.Arguments("Health", "{\"Current\": " + std::to_string(value) + "}");
		arguments["mergeKey"] = "drag-1";
		fixture.Succeed("component.set", arguments);
	}
	CHECK(subject.Get<HealthComponent>().Current == 5.0f);
	CHECK(fixture.Context.GetUndoStack().GetUndoHistory().size() == depth + 1);
	fixture.Succeed("undo");
	CHECK(fixture.Hash() == before);
	fixture.Succeed("redo");
	CHECK(subject.Get<HealthComponent>().Current == 5.0f);

	// Without a key (or with another one) every edit is a step of its own.
	fixture.Succeed("component.set", subject.Arguments("Health", R"({"Current": 6})"));
	fixture.Succeed("component.set", subject.Arguments("Health", R"({"Current": 7})"));
	CHECK(fixture.Context.GetUndoStack().GetUndoHistory().size() == depth + 3);
	fixture.Succeed("undo", J(R"({"steps": 2})"));
	CHECK(subject.Get<HealthComponent>().Current == 5.0f);

	// A save ends the drag: the next edit with the key is a new step, so the saved state stays reachable.
	Json first = subject.Arguments("Health", R"({"Current": 8})");
	first["mergeKey"] = "drag-2";
	fixture.Succeed("component.set", first);
	fixture.Succeed("scene.save");
	Json second = subject.Arguments("Health", R"({"Current": 9})");
	second["mergeKey"] = "drag-2";
	fixture.Succeed("component.set", second);
	CHECK(fixture.Context.GetUndoStack().IsDirty("Scenes/Main.gscene"));
	fixture.Succeed("undo");
	CHECK(subject.Get<HealthComponent>().Current == 8.0f);
	CHECK_FALSE(fixture.Context.GetUndoStack().IsDirty("Scenes/Main.gscene"));
}

TEST_CASE("Automation: component.types describes every component the tools can edit")
{
	AutomationFixture fixture;
	REQUIRE(fixture.Types.RegisterDynamicComponent(MakeScriptSchema()).HasValue());
	const Json types = fixture.Succeed("component.types");

	std::map<std::string, Json> byName;
	for (const Json& component : types.at("components"))
	{
		byName.emplace(component.at("name").get<std::string>(), component);
	}
	CHECK(byName.contains("Health"));
	CHECK(byName.contains("Transform"));
	CHECK(byName.contains("ScriptStats")); // declared by a script
	CHECK_FALSE(byName.contains("ID"));    // managed by the engine
	CHECK_FALSE(byName.contains("Relationship"));
	CHECK_FALSE(byName.contains("WorldTransform"));
	CHECK(byName.at("ScriptStats").at("dynamic") == true);
	CHECK(byName.at("Health").at("dynamic") == false);
	CHECK(byName.at("PlayerTag").at("tag") == true);
	CHECK(byName.at("Health").at("flags") == J(R"(["serializable", "editor-visible", "removable"])"));
	CHECK(types.at("count") == byName.size());

	const Json health = byName.at("Health");
	CHECK(
		health.at("fields")[0]
		== J(R"({"name": "Current", "type": "Float", "default": 100.0, "readOnly": false, "stored": true})"));

	// Types, ranges, enums, structs, assets, read-only and runtime-only fields.
	std::map<std::string, Json> fields;
	for (const Json& field : byName.at("AllTypes").at("fields"))
	{
		fields.emplace(field.at("name").get<std::string>(), field);
	}
	CHECK(fields.at("Int32").at("range") == J("[-100.0, 100.0]"));
	CHECK(fields.at("Bool").at("description") == "a flag");
	CHECK(fields.at("Bool").at("category") == "Scalars");
	CHECK(fields.at("Mode").at("type") == "Enum");
	CHECK(fields.at("Mode").at("enum") == J(R"(["Reverse", "Off", "Slow", "Fast"])"));
	CHECK(fields.at("Mode").at("default") == "Slow");
	CHECK(fields.at("Color3").at("type") == "Color3");
	CHECK(fields.at("Texture").at("type") == "AssetHandle");
	CHECK(fields.at("Texture").at("assetType") == "Texture");
	CHECK(fields.at("Floats").at("type") == "Array");
	CHECK(fields.at("Floats").at("elementType") == "Float");
	CHECK(fields.at("Textures").at("readOnly") == true);
	CHECK(fields.at("Cache").at("stored") == false);

	std::map<std::string, Json> structFields;
	for (const Json& field : byName.at("Structs").at("fields"))
	{
		structFields.emplace(field.at("name").get<std::string>(), field);
	}
	CHECK(structFields.at("Main").at("type") == "Struct");
	CHECK(structFields.at("Main").at("struct") == "TestLayer");
	CHECK(structFields.at("Layers").at("elementType") == "Struct");
	bool hasLayer = false;
	for (const Json& entry : types.at("structs"))
	{
		hasLayer = hasLayer || entry.at("name") == "TestLayer";
	}
	CHECK(hasLayer);

	// One type by name, and the engine-managed ones on request.
	const Json only = fixture.Succeed("component.types", J(R"({"name": "Transform"})"));
	CHECK(only.at("count") == 1);
	CHECK(only.at("components")[0].at("fields").size() == 3);
	const Json everything = fixture.Succeed("component.types", J(R"({"includeInternal": true})"));
	CHECK(everything.at("count").get<size_t>() > types.at("count").get<size_t>());
	CHECK(fixture.Succeed("component.types", J(R"({"name": "Relationship"})")).at("count") == 1);
	fixture.Reject("component.types", J(R"({"name": "Nope"})"), CommandErrorCode::NotFound);
}

TEST_CASE("Automation: read-only members of structs cannot be changed by component.set, bulk.set or a CSV")
{
	AutomationFixture fixture;
	RegisterSealed(fixture.Types);
	fixture.CreateProject();
	const std::string vault = fixture.Create("Vault", J(R"({"components": {"Sealed": {}}})"));
	const uint64_t before = fixture.Hash();

	const auto set = [&](std::string_view values) {
		Json arguments = Json::object();
		arguments["entity"] = vault;
		arguments["component"] = "Sealed";
		arguments["values"] = Json::parse(values);
		return arguments;
	};
	CHECK(
		fixture.Reject("component.set", set(R"({"Stats": {"Serial": 8}})"), CommandErrorCode::InvalidArguments).Message
		== "/Sealed/Stats/Serial: the field 'Serial' is read-only");
	CHECK(
		fixture.Reject("component.set", set(R"({"History": [{"Serial": 9}]})"), CommandErrorCode::InvalidArguments)
			.Message
		== "/Sealed/History/0/Serial: the field 'Serial' is read-only");
	fixture.Reject(
		"entity.create", J(R"({"name": "Other", "components": {"Sealed": {"Stats": {"Serial": 1}}}})"),
		CommandErrorCode::InvalidArguments);

	Json table = Json::object();
	table["columns"] = J(R"(["Stats"])");
	table["rows"] = Json::array({ Json{ { "entity", vault }, { "values", J(R"([{"Serial": 3}])") } } });
	CHECK(
		fixture
			.Reject(
				"bulk.set", Json{ { "component", "Sealed" }, { "table", table } }, CommandErrorCode::InvalidArguments)
			.Message.find("the field 'Serial' is read-only")
		!= std::string::npos);
	const std::string csv = "Entity,Stats:string\n" + vault + ",\"{\"\"Serial\"\": 3}\"\n";
	fixture.Reject(
		"bulk.import-csv", Json{ { "component", "Sealed" }, { "csv", csv } }, CommandErrorCode::InvalidArguments);
	CHECK(fixture.Hash() == before);

	// What component.get returns can be given back, and the other members change.
	const Json values =
		fixture.Succeed("component.get", J(R"({"entity": "Vault", "component": "Sealed"})")).at("values");
	CHECK(fixture.Succeed("component.set", Json{ { "entity", vault }, { "component", "Sealed" }, { "values", values } })
	          .at("changed")
	          .empty());
	fixture.Succeed("component.set", set(R"({"Stats": {"Level": 5, "Serial": 7}})"));
	const Entity entity = fixture.Scene().FindEntityByName("Vault");
	CHECK(entity.GetComponent<SealedComponent>().Stats.Level == 5);
	CHECK(entity.GetComponent<SealedComponent>().Stats.Serial == 7);
}
