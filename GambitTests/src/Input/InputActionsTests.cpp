#include <doctest/doctest.h>

#include "TestSupport.h"

#include "Gambit/Core/DirectoryMount.h"
#include "Gambit/Core/VFS.h"
#include "Gambit/Input/InputActions.h"

#include <string>

using namespace Gambit;

namespace
{
	constexpr const char* FullDocument = R"({
  "Version": 1,
  "Actions": [
    {
      "Name": "Jump",
      "Type": "Button",
      "Bindings": [
        { "Type": "Key", "Key": "Space" },
        { "Type": "GamepadButton", "Button": "A" },
        { "Type": "MouseButton", "Button": "Right" },
        { "Type": "GamepadAxis", "Axis": "RightTrigger", "Threshold": 0.4 }
      ]
    },
    {
      "Name": "Throttle",
      "Type": "Axis1D",
      "Bindings": [
        { "Type": "Composite1D", "Positive": "W", "Negative": "S" },
        { "Type": "GamepadAxis", "Axis": "LeftY", "Deadzone": 0.2, "Invert": true },
        { "Type": "Key", "Key": "LeftShift", "Scale": 2 }
      ]
    },
    {
      "Name": "Move",
      "Type": "Axis2D",
      "Bindings": [
        { "Type": "Composite2D", "Up": "W", "Down": "S", "Left": "A", "Right": "D" },
        { "Type": "GamepadStick", "Stick": "Left", "Deadzone": 0.15, "InvertY": true }
      ]
    },
    {
      "Name": "Look",
      "Type": "Axis2D",
      "Bindings": [
        { "Type": "MouseDelta", "Scale": 0.1, "InvertY": true },
        { "Type": "GamepadStick", "Stick": "Right", "Gamepad": 1, "Scale": 3 }
      ]
    },
    {
      "Name": "Zoom",
      "Type": "Axis1D",
      "Bindings": [
        { "Type": "Scroll", "Axis": "Y", "Scale": 0.5 },
        { "Type": "MouseDelta", "Axis": "X", "Invert": true }
      ]
    }
  ]
})";

	std::string ErrorOf(std::string_view json)
	{
		const Result<InputActions> result = InputActions::Parse(json);
		REQUIRE_MESSAGE(!result.HasValue(), "this document should be rejected: ", std::string(json));
		return result.GetError().Message;
	}

	void CheckRejected(std::string_view json, std::string_view expected)
	{
		const std::string message = ErrorOf(json);
		CHECK_MESSAGE(
			message.find(expected) != std::string::npos, "expected '", std::string(expected), "' in: ", message);
	}

	// One action with one binding, for the binding-level error cases.
	std::string WithBinding(std::string_view actionType, std::string_view binding)
	{
		return std::format(
			R"({{ "Version": 1, "Actions": [ {{ "Name": "A", "Type": "{}", "Bindings": [ {} ] }} ] }})", actionType,
			binding);
	}
}

TEST_CASE("Input: InputActions parses a full document")
{
	const Result<InputActions> parsed = InputActions::Parse(FullDocument);
	REQUIRE(parsed.HasValue());
	const std::vector<InputAction>& actions = parsed->GetActions();
	REQUIRE(actions.size() == 5);

	CHECK(actions[0].Name == "Jump");
	CHECK(actions[0].Type == InputActionType::Button);
	REQUIRE(actions[0].Bindings.size() == 4);
	CHECK(actions[0].Bindings[0].Type == InputBindingType::Key);
	CHECK(actions[0].Bindings[0].Key == KeyCode::Space);
	CHECK(actions[0].Bindings[1].Type == InputBindingType::GamepadButton);
	CHECK(actions[0].Bindings[1].PadButton == GamepadButton::A);
	CHECK(actions[0].Bindings[1].Gamepad == -1);
	CHECK(actions[0].Bindings[2].Button == MouseButton::Right);
	CHECK(actions[0].Bindings[3].PadAxis == GamepadAxis::RightTrigger);
	CHECK(actions[0].Bindings[3].Threshold == 0.4f);

	CHECK(actions[1].Type == InputActionType::Axis1D);
	CHECK(actions[1].Bindings[0].Type == InputBindingType::Composite1D);
	CHECK(actions[1].Bindings[0].Key == KeyCode::W);
	CHECK(actions[1].Bindings[0].NegativeKey == KeyCode::S);
	CHECK(actions[1].Bindings[1].Deadzone == 0.2f);
	CHECK(actions[1].Bindings[1].Invert);
	CHECK(actions[1].Bindings[2].Scale == 2.0f);

	CHECK(actions[2].Type == InputActionType::Axis2D);
	CHECK(actions[2].Bindings[0].Up == KeyCode::W);
	CHECK(actions[2].Bindings[0].Down == KeyCode::S);
	CHECK(actions[2].Bindings[0].Left == KeyCode::A);
	CHECK(actions[2].Bindings[0].Right == KeyCode::D);
	CHECK(actions[2].Bindings[1].Stick == GamepadStick::Left);
	CHECK(actions[2].Bindings[1].InvertY);
	CHECK_FALSE(actions[2].Bindings[1].InvertX);

	CHECK(actions[3].Bindings[0].Type == InputBindingType::MouseDelta);
	CHECK(actions[3].Bindings[0].Scale == 0.1f);
	CHECK(actions[3].Bindings[1].Stick == GamepadStick::Right);
	CHECK(actions[3].Bindings[1].Gamepad == 1);

	CHECK(actions[4].Bindings[0].Type == InputBindingType::Scroll);
	CHECK(actions[4].Bindings[0].Axis == InputAxis::Y);
	CHECK(actions[4].Bindings[1].Axis == InputAxis::X);
	CHECK(actions[4].Bindings[1].Invert);

	CHECK(parsed->Find("Move") == 2);
	CHECK(parsed->Find("Zoom") == 4);
	CHECK(parsed->Find("Missing") == InvalidInputAction);
	CHECK(parsed->Find("move") == InvalidInputAction); // names are case-sensitive
}

TEST_CASE("Input: InputActions accepts an empty document and empty bindings")
{
	const Result<InputActions> none = InputActions::Parse(R"({ "Version": 1, "Actions": [] })");
	REQUIRE(none.HasValue());
	CHECK(none->GetActions().empty());

	const Result<InputActions> unbound =
		InputActions::Parse(R"({ "Version": 1, "Actions": [ { "Name": "Fire", "Type": "Button", "Bindings": [] } ] })");
	REQUIRE(unbound.HasValue());
	CHECK(unbound->GetActions()[0].Bindings.empty());
}

TEST_CASE("Input: InputActions writes canonical JSON that reads back identically")
{
	const Result<InputActions> parsed = InputActions::Parse(FullDocument);
	REQUIRE(parsed.HasValue());
	const std::string written = parsed->ToJson();

	const Result<InputActions> reread = InputActions::Parse(written);
	REQUIRE(reread.HasValue());
	CHECK(*reread == *parsed);
	CHECK(reread->ToJson() == written); // stable

	CHECK(written.back() == '\n');
	CHECK(written.find("\r") == std::string::npos);
	CHECK(written.starts_with("{\n  \"Version\": 1,\n  \"Actions\": [\n    {\n      \"Name\": \"Jump\","));
}

TEST_CASE("Input: InputActions output omits defaults and keeps the order of properties")
{
	const Result<InputActions> parsed = InputActions::Parse(R"({
		"Version": 1,
		"Actions": [
			{ "Bindings": [ { "Key": "E", "Type": "Key", "Scale": 1 } ], "Type": "Button", "Name": "Use" },
			{ "Name": "Pan", "Type": "Axis1D", "Bindings": [
				{ "Type": "GamepadAxis", "Axis": "RightX", "Gamepad": 2, "Deadzone": 0.15, "Scale": 0.5, "Invert": true, "Threshold": 0.25 } ] }
		]
	})");
	REQUIRE(parsed.HasValue());
	const std::string expected = R"({
  "Version": 1,
  "Actions": [
    {
      "Name": "Use",
      "Type": "Button",
      "Bindings": [
        {
          "Type": "Key",
          "Key": "E"
        }
      ]
    },
    {
      "Name": "Pan",
      "Type": "Axis1D",
      "Bindings": [
        {
          "Type": "GamepadAxis",
          "Axis": "RightX",
          "Gamepad": 2,
          "Deadzone": 0.15,
          "Scale": 0.5,
          "Invert": true,
          "Threshold": 0.25
        }
      ]
    }
  ]
}
)";
	CHECK(parsed->ToJson() == expected);
}

TEST_CASE("Input: InputActions rejects malformed documents with the path of the problem")
{
	CheckRejected("", "invalid JSON");
	CheckRejected("{", "invalid JSON");
	CheckRejected(R"({ "Version": 1, "Actions": [ , ] })", "invalid JSON");
	CheckRejected("[]", "InputActions: expected an object, found array");
	CheckRejected(R"({ "Actions": [] })", "missing property 'Version'");
	CheckRejected(R"({ "Version": 1 })", "missing property 'Actions'");
	CheckRejected(R"({ "Version": 2, "Actions": [] })", "Version: unsupported version 2");
	CheckRejected(R"({ "Version": "1", "Actions": [] })", "unsupported version");
	CheckRejected(R"({ "Version": 1.5, "Actions": [] })", "unsupported version");
	CheckRejected(R"({ "Version": 1, "Actions": {} })", "Actions: expected an array, found object");
	CheckRejected(R"({ "Version": 1, "Actions": [], "Extra": 1 })", "unknown property 'Extra'");
	CheckRejected(R"({ "Version": 1, "Actions": [ 3 ] })", "Actions[0]: expected an object, found number");
	CheckRejected(
		R"({ "Version": 1, "Actions": [ { "Type": "Button", "Bindings": [] } ] })",
		"Actions[0]: missing property 'Name'");
	CheckRejected(
		R"({ "Version": 1, "Actions": [ { "Name": 5, "Type": "Button", "Bindings": [] } ] })",
		"Actions[0].Name: expected a string");
	CheckRejected(
		R"({ "Version": 1, "Actions": [ { "Name": "", "Type": "Button", "Bindings": [] } ] })", "Actions[0].Name");
	CheckRejected(
		R"({ "Version": 1, "Actions": [ { "Name": "A", "Bindings": [] } ] })", "Actions[0]: missing property 'Type'");
	CheckRejected(
		R"({ "Version": 1, "Actions": [ { "Name": "A", "Type": "Toggle", "Bindings": [] } ] })",
		"Actions[0].Type: unknown action type 'Toggle'");
	CheckRejected(
		R"({ "Version": 1, "Actions": [ { "Name": "A", "Type": "Button" } ] })",
		"Actions[0]: missing property 'Bindings'");
	CheckRejected(
		R"({ "Version": 1, "Actions": [ { "Name": "A", "Type": "Button", "Bindings": {} } ] })",
		"Actions[0].Bindings: expected an array");
	CheckRejected(
		R"({ "Version": 1, "Actions": [ { "Name": "A", "Type": "Button", "Bindings": [], "Tooltip": "x" } ] })",
		"Actions[0]: unknown property 'Tooltip'");
	CheckRejected(
		R"({ "Version": 1, "Actions": [ { "Name": "A", "Type": "Button", "Bindings": [] }, { "Name": "B", "Type": "Button", "Bindings": [] }, { "Name": "A", "Type": "Axis1D", "Bindings": [] } ] })",
		"Actions[2].Name: duplicate action name 'A'");
}

TEST_CASE("Input: InputActions rejects malformed bindings")
{
	CheckRejected(WithBinding("Button", "5"), "Actions[0].Bindings[0]: expected an object, found number");
	CheckRejected(WithBinding("Button", "{}"), "Actions[0].Bindings[0]: missing property 'Type'");
	CheckRejected(
		WithBinding("Button", R"({ "Type": "Gyro" })"), "Actions[0].Bindings[0].Type: unknown binding type 'Gyro'");
	CheckRejected(WithBinding("Button", R"({ "Type": "Key" })"), "Actions[0].Bindings[0]: missing property 'Key'");
	CheckRejected(
		WithBinding("Button", R"({ "Type": "Key", "Key": "Spacebar" })"),
		"Actions[0].Bindings[0].Key: unknown key 'Spacebar'");
	CheckRejected(WithBinding("Button", R"({ "Type": "Key", "Key": "space" })"), "unknown key 'space'");
	CheckRejected(WithBinding("Button", R"({ "Type": "Key", "Key": "None" })"), "unknown key 'None'");
	CheckRejected(
		WithBinding("Button", R"({ "Type": "Key", "Key": 32 })"), "Actions[0].Bindings[0].Key: expected a string");
	CheckRejected(
		WithBinding("Button", R"({ "Type": "Key", "Key": "A", "Keys": "B" })"),
		"Actions[0].Bindings[0]: unknown property 'Keys'");
	CheckRejected(
		WithBinding("Button", R"({ "Type": "MouseButton", "Button": "Thumb" })"), "unknown mouse button 'Thumb'");
	CheckRejected(
		WithBinding("Button", R"({ "Type": "GamepadButton", "Button": "Cross" })"), "unknown gamepad button 'Cross'");
	CheckRejected(
		WithBinding("Button", R"({ "Type": "GamepadAxis", "Axis": "Trigger" })"), "unknown gamepad axis 'Trigger'");
	CheckRejected(
		WithBinding("Axis2D", R"({ "Type": "GamepadStick", "Stick": "Middle" })"),
		"Stick: expected \"Left\" or \"Right\"");
	CheckRejected(
		WithBinding("Axis1D", R"({ "Type": "Composite1D", "Positive": "W" })"), "missing property 'Negative'");
	CheckRejected(
		WithBinding("Axis2D", R"({ "Type": "Composite2D", "Up": "W", "Down": "S", "Left": "A" })"),
		"missing property 'Right'");
}

TEST_CASE("Input: InputActions rejects bindings that cannot drive the action type")
{
	CheckRejected(
		WithBinding("Button", R"({ "Type": "Composite2D", "Up": "W", "Down": "S", "Left": "A", "Right": "D" })"),
		"a Composite2D binding cannot drive a Button action");
	CheckRejected(
		WithBinding("Button", R"({ "Type": "MouseDelta", "Axis": "X" })"),
		"a MouseDelta binding cannot drive a Button action");
	CheckRejected(
		WithBinding("Axis2D", R"({ "Type": "Key", "Key": "W" })"), "a Key binding cannot drive a Axis2D action");
	CheckRejected(
		WithBinding("Axis2D", R"({ "Type": "GamepadAxis", "Axis": "LeftX" })"),
		"a GamepadAxis binding cannot drive a Axis2D action");
	CheckRejected(
		WithBinding("Axis1D", R"({ "Type": "GamepadStick", "Stick": "Left" })"),
		"a GamepadStick binding cannot drive a Axis1D action");
	CheckRejected(
		WithBinding("Axis1D", R"({ "Type": "Composite2D", "Up": "W", "Down": "S", "Left": "A", "Right": "D" })"),
		"cannot drive a Axis1D action");
	CheckRejected(
		WithBinding("Button", R"({ "Type": "Composite1D", "Positive": "W", "Negative": "S" })"),
		"cannot drive a Button action");
}

TEST_CASE("Input: InputActions checks the axis selection of mouse and scroll bindings")
{
	CheckRejected(WithBinding("Axis1D", R"({ "Type": "MouseDelta" })"), "missing property 'Axis'");
	CheckRejected(
		WithBinding("Axis1D", R"({ "Type": "MouseDelta", "Axis": "Z" })"),
		"Actions[0].Bindings[0].Axis: expected \"X\" or \"Y\"");
	CheckRejected(WithBinding("Axis1D", R"({ "Type": "Scroll", "Axis": 1 })"), "Axis: expected a string");
	CheckRejected(
		WithBinding("Axis1D", R"({ "Type": "Scroll", "Axis": "X", "InvertX": true })"), "unknown property 'InvertX'");
	CheckRejected(WithBinding("Axis2D", R"({ "Type": "MouseDelta", "Axis": "X" })"), "unknown property 'Axis'");
	CheckRejected(WithBinding("Axis2D", R"({ "Type": "Scroll", "Invert": true })"), "unknown property 'Invert'");
	CHECK(InputActions::Parse(WithBinding("Axis2D", R"({ "Type": "Scroll", "InvertX": true, "InvertY": false })"))
	          .HasValue());
}

TEST_CASE("Input: InputActions checks numbers and flags")
{
	CheckRejected(
		WithBinding("Axis1D", R"({ "Type": "Key", "Key": "W", "Scale": "2" })"),
		"Bindings[0].Scale: expected a number, found string");
	CheckRejected(
		WithBinding("Axis1D", R"({ "Type": "Key", "Key": "W", "Scale": 1e40 })"), "Scale: the number is out of range");
	CheckRejected(
		WithBinding("Axis1D", R"({ "Type": "Key", "Key": "W", "Scale": 1e9 })"),
		"Scale must be a finite number of reasonable size");
	CheckRejected(
		WithBinding("Axis1D", R"({ "Type": "GamepadAxis", "Axis": "LeftX", "Deadzone": 1 })"),
		"Deadzone must be in [0, 1)");
	CheckRejected(
		WithBinding("Axis1D", R"({ "Type": "GamepadAxis", "Axis": "LeftX", "Deadzone": -0.1 })"),
		"Deadzone must be in [0, 1)");
	CheckRejected(
		WithBinding("Axis1D", R"({ "Type": "GamepadAxis", "Axis": "LeftX", "Deadzone": "0.1" })"),
		"Deadzone: expected a number");
	CheckRejected(
		WithBinding("Button", R"({ "Type": "GamepadAxis", "Axis": "LeftX", "Threshold": 0 })"),
		"Threshold must be in (0, 1]");
	CheckRejected(
		WithBinding("Button", R"({ "Type": "GamepadAxis", "Axis": "LeftX", "Threshold": 1.5 })"),
		"Threshold must be in (0, 1]");
	CheckRejected(
		WithBinding("Axis1D", R"({ "Type": "GamepadAxis", "Axis": "LeftX", "Invert": "yes" })"),
		"Invert: expected true or false");
	CheckRejected(
		WithBinding("Button", R"({ "Type": "GamepadButton", "Button": "A", "Gamepad": 4 })"),
		"Gamepad: expected an integer from 0 to 3");
	CheckRejected(
		WithBinding("Button", R"({ "Type": "GamepadButton", "Button": "A", "Gamepad": -1 })"),
		"Gamepad: expected an integer from 0 to 3");
	CheckRejected(
		WithBinding("Button", R"({ "Type": "GamepadButton", "Button": "A", "Gamepad": 1.5 })"),
		"Gamepad: expected an integer");
	CHECK(InputActions::Parse(WithBinding("Button", R"({ "Type": "GamepadButton", "Button": "A", "Gamepad": 3 })"))
	          .HasValue());
	CHECK(InputActions::Parse(WithBinding("Axis1D", R"({ "Type": "GamepadAxis", "Axis": "LeftX", "Deadzone": 0.999 })"))
	          .HasValue());
}

TEST_CASE("Input: InputActions enforces limits")
{
	std::string manyBindings;
	for (int i = 0; i < 65; ++i)
	{
		manyBindings += std::string(i == 0 ? "" : ",") + R"({ "Type": "Key", "Key": "A" })";
	}
	CheckRejected(WithBinding("Button", manyBindings), "65 bindings (at most 64)");

	std::string manyActions = R"({ "Version": 1, "Actions": [)";
	for (int i = 0; i < 1025; ++i)
	{
		manyActions += std::format(R"({}{{ "Name": "A{}", "Type": "Button", "Bindings": [] }})", i == 0 ? "" : ",", i);
	}
	manyActions += "] }";
	CheckRejected(manyActions, "1025 actions (at most 1024)");

	CheckRejected(
		std::format(
			R"({{ "Version": 1, "Actions": [ {{ "Name": "{}", "Type": "Button", "Bindings": [] }} ] }})",
			std::string(129, 'n')),
		"Actions[0].Name");
}

TEST_CASE("Input: InputActions Create applies the same checks to hand-built actions")
{
	InputAction good;
	good.Name = "Jump";
	good.Type = InputActionType::Button;
	good.Bindings.push_back({ .Type = InputBindingType::Key, .Key = KeyCode::Space });
	const Result<InputActions> created = InputActions::Create({ good });
	REQUIRE(created.HasValue());
	CHECK(created->Find("Jump") == 0);

	InputAction invalidKey = good;
	invalidKey.Bindings[0].Key = KeyCode::None;
	CHECK_FALSE(InputActions::Create({ invalidKey }).HasValue());

	InputAction wrongType = good;
	wrongType.Bindings[0] = { .Type = InputBindingType::GamepadStick };
	const Result<InputActions> rejected = InputActions::Create({ wrongType });
	REQUIRE_FALSE(rejected.HasValue());
	CHECK(rejected.GetError().Message.find("Actions[0].Bindings[0]") != std::string::npos);

	InputAction badDeadzone = good;
	badDeadzone.Bindings[0].Deadzone = 1.5f;
	CHECK_FALSE(InputActions::Create({ badDeadzone }).HasValue());
	InputAction nanScale = good;
	nanScale.Bindings[0].Scale = std::numeric_limits<float>::quiet_NaN();
	CHECK_FALSE(InputActions::Create({ nanScale }).HasValue());

	CHECK_FALSE(InputActions::Create({ good, good }).HasValue()); // duplicate name
	InputAction unnamed = good;
	unnamed.Name.clear();
	CHECK_FALSE(InputActions::Create({ unnamed }).HasValue());
	InputAction control = good;
	control.Name = "bad\nname";
	CHECK_FALSE(InputActions::Create({ control }).HasValue());
	CHECK(InputActions::Create({}).HasValue());

	// Names must be UTF-8 (JSON text), otherwise ToJson could not write them. Valid multi-byte names are fine.
	for (const char* invalid :
	     { "\xFF", "bad\xC3", "\xC0\xAF", "\xE0\x80\xAF", "\xED\xA0\x80", "\xF4\x90\x80\x80", "\x80start" })
	{
		InputAction encoded = good;
		encoded.Name = invalid;
		CHECK_FALSE(InputActions::Create({ encoded }).HasValue());
	}
	InputAction unicode = good;
	unicode.Name = "Springen \xC3\xBC \xE2\x82\xAC \xF0\x9F\x98\x80"; // u-umlaut, euro sign, an emoji
	const Result<InputActions> withUnicode = InputActions::Create({ unicode });
	REQUIRE(withUnicode.HasValue());
	const Result<InputActions> reparsed = InputActions::Parse(withUnicode->ToJson());
	REQUIRE(reparsed.HasValue());
	CHECK(*reparsed == *withUnicode);
}

TEST_CASE("Input: InputActions loads from the virtual file system")
{
	Tests::ScopedTempDirectory directory;
	REQUIRE(FileSystem::WriteText(directory.GetPath() / "InputActions.json", FullDocument).HasValue());
	REQUIRE(FileSystem::WriteText(
				directory.GetPath() / "Broken.json", R"({ "Version": 1, "Actions": [ { "Name": "A" } ] })")
	            .HasValue());
	REQUIRE(FileSystem::WriteText(directory.GetPath() / "WithBom.json", std::string("\xEF\xBB\xBF") + FullDocument)
	            .HasValue());

	VFS vfs;
	REQUIRE(vfs.Mount("", CreateRef<DirectoryMount>(directory.GetPath(), false)).HasValue());

	const Result<InputActions> loaded = InputActions::Load(vfs, "InputActions.json");
	REQUIRE(loaded.HasValue());
	CHECK(loaded->GetActions().size() == 5);
	CHECK(InputActions::Load(vfs, "WithBom.json").HasValue());

	const Result<InputActions> missing = InputActions::Load(vfs, "Nope.json");
	REQUIRE_FALSE(missing.HasValue());
	CHECK(missing.GetError().Message.find("Nope.json") != std::string::npos);

	const Result<InputActions> broken = InputActions::Load(vfs, "Broken.json");
	REQUIRE_FALSE(broken.HasValue());
	CHECK(broken.GetError().Message.find("Broken.json") != std::string::npos);
	CHECK(broken.GetError().Message.find("Actions[0]") != std::string::npos);
}
