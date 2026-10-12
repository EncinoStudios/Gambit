#include <doctest/doctest.h>

#include "Gambit/Core/BinaryStream.h"
#include "Gambit/Core/Hash.h"
#include "Gambit/Input/InputFrame.h"

#include <cstring>
#include <limits>

using namespace Gambit;

namespace
{
	InputFrame RichFrame()
	{
		InputFrame frame;
		frame.SetKey(KeyCode::W, true);
		frame.SetKey(KeyCode::LeftShift, true);
		frame.SetKey(KeyCode::Menu, true); // the last key: highest bit of the set
		frame.SetKey(KeyCode::Space, true);
		frame.SetMouseButton(MouseButton::Left, true);
		frame.SetMouseButton(MouseButton::Button8, true);
		frame.MouseX = 1280 * 64 + 17;
		frame.MouseY = -5 * 64;
		frame.MouseDeltaX = -123;
		frame.MouseDeltaY = 456;
		frame.ScrollX = 0;
		frame.ScrollY = -64;
		frame.Gamepads[0].Connected = true;
		frame.Gamepads[0].SetButton(GamepadButton::A, true);
		frame.Gamepads[0].SetButton(GamepadButton::DpadLeft, true);
		frame.Gamepads[0].Axes = { 32767, -32767, 0, 1000, 0, 32767 };
		frame.Gamepads[3].Connected = true;
		frame.Gamepads[3].Axes[static_cast<size_t>(GamepadAxis::LeftX)] = -32768;
		frame.AddText('h');
		frame.AddText(0xE9);
		frame.AddText(0x1F600);
		return frame;
	}

	// Serialized bytes with one byte replaced.
	Buffer WithByte(const Buffer& original, size_t offset, uint8_t value)
	{
		Buffer copy = Buffer::Copy(original);
		copy.Data()[offset] = static_cast<std::byte>(value);
		return copy;
	}
}

TEST_CASE("Input: a default InputFrame is empty")
{
	const InputFrame frame;
	for (size_t index = 1; index < KeyCodeCount; ++index)
	{
		CHECK_FALSE(frame.IsKeyDown(static_cast<KeyCode>(index)));
	}
	for (size_t index = 0; index < MouseButtonCount; ++index)
	{
		CHECK_FALSE(frame.IsMouseButtonDown(static_cast<MouseButton>(index)));
	}
	CHECK(frame.TextCount == 0);
	CHECK_FALSE(frame.Gamepads[0].Connected);
	CHECK(frame == InputFrame{});
}

TEST_CASE("Input: InputFrame keys and mouse buttons set and clear independently")
{
	InputFrame frame;
	for (size_t index = 1; index < KeyCodeCount; ++index)
	{
		frame.SetKey(static_cast<KeyCode>(index), true);
	}
	for (size_t index = 1; index < KeyCodeCount; ++index)
	{
		CHECK(frame.IsKeyDown(static_cast<KeyCode>(index)));
	}
	frame.SetKey(KeyCode::M, false);
	CHECK_FALSE(frame.IsKeyDown(KeyCode::M));
	CHECK(frame.IsKeyDown(KeyCode::L));
	CHECK(frame.IsKeyDown(KeyCode::N));
	frame.SetKey(KeyCode::None, true); // ignored
	frame.SetKey(KeyCode::Count, true);
	frame.SetKey(static_cast<KeyCode>(200), true);
	CHECK_FALSE(frame.IsKeyDown(KeyCode::None));
	CHECK_FALSE(frame.IsKeyDown(KeyCode::Count));
	CHECK_FALSE(frame.IsKeyDown(static_cast<KeyCode>(200)));
	CHECK((frame.Keys[1] >> (KeyCodeCount - 64)) == 0); // no bits beyond the last key

	frame.SetMouseButton(MouseButton::Right, true);
	frame.SetMouseButton(MouseButton::Middle, true);
	frame.SetMouseButton(MouseButton::Right, false);
	CHECK_FALSE(frame.IsMouseButtonDown(MouseButton::Right));
	CHECK(frame.IsMouseButtonDown(MouseButton::Middle));
	CHECK_FALSE(frame.IsMouseButtonDown(MouseButton::Left));
}

TEST_CASE("Input: InputFrame gamepad helpers")
{
	GamepadFrame pad;
	pad.SetButton(GamepadButton::Y, true);
	pad.SetButton(GamepadButton::Guide, true);
	pad.SetButton(GamepadButton::Y, false);
	CHECK_FALSE(pad.IsButtonDown(GamepadButton::Y));
	CHECK(pad.IsButtonDown(GamepadButton::Guide));
	CHECK(pad.Buttons == (1u << static_cast<unsigned>(GamepadButton::Guide)));

	pad.Axes[static_cast<size_t>(GamepadAxis::LeftX)] = 32767;
	pad.Axes[static_cast<size_t>(GamepadAxis::LeftY)] = -32768;
	pad.Axes[static_cast<size_t>(GamepadAxis::RightX)] = 0;
	pad.Axes[static_cast<size_t>(GamepadAxis::RightTrigger)] = 16384;
	CHECK(pad.GetAxis(GamepadAxis::LeftX) == 1.0f);
	CHECK(pad.GetAxis(GamepadAxis::LeftY) == -1.0f); // clamped: -32768 is one step beyond -1
	CHECK(pad.GetAxis(GamepadAxis::RightX) == 0.0f);
	CHECK(pad.GetAxis(GamepadAxis::RightTrigger) == doctest::Approx(0.50002f).epsilon(1e-5));
}

TEST_CASE("Input: InputFrame text is bounded")
{
	InputFrame frame;
	for (size_t i = 0; i < MaxTextCodepointsPerFrame; ++i)
	{
		CHECK(frame.AddText(static_cast<uint32_t>('a' + i)));
	}
	CHECK_FALSE(frame.AddText('z')); // dropped
	CHECK(frame.TextCount == MaxTextCodepointsPerFrame);
	CHECK(frame.Text[0] == 'a');
	CHECK(frame.Text[MaxTextCodepointsPerFrame - 1] == static_cast<uint32_t>('a' + MaxTextCodepointsPerFrame - 1));

	InputFrame invalid;
	CHECK_FALSE(invalid.AddText(0));
	CHECK_FALSE(invalid.AddText(0xD800)); // a surrogate is not text
	CHECK_FALSE(invalid.AddText(0x110000));
	CHECK(invalid.TextCount == 0);
	CHECK(invalid.AddText(0x10FFFF));
}

TEST_CASE("Input: InputFrame equality compares everything that is stored")
{
	const InputFrame base = RichFrame();
	CHECK(base == RichFrame());

	InputFrame other = base;
	other.SetKey(KeyCode::W, false);
	CHECK_FALSE(base == other);
	other = base;
	other.MouseX += 1;
	CHECK_FALSE(base == other);
	other = base;
	other.ScrollY += 1;
	CHECK_FALSE(base == other);
	other = base;
	other.Gamepads[2].Connected = true;
	CHECK_FALSE(base == other);
	other = base;
	other.Gamepads[0].Axes[3] += 1;
	CHECK_FALSE(base == other);
	other = base;
	other.Text[1] = 'x';
	CHECK_FALSE(base == other);
	other = base;
	other.Text[10] = 99; // beyond TextCount: not part of the frame
	CHECK(base == other);
}

TEST_CASE("Input: quantization to fixed point")
{
	CHECK(QuantizeToFixedPoint(0.0) == 0);
	CHECK(QuantizeToFixedPoint(1.0) == 64);
	CHECK(QuantizeToFixedPoint(-1.0) == -64);
	CHECK(QuantizeToFixedPoint(1280.5) == 81952);
	CHECK(QuantizeToFixedPoint(1.0 / 128.0) == 1); // rounds half up
	CHECK(QuantizeToFixedPoint(0.0078) == 0);
	CHECK(QuantizeToFixedPoint(-1.0 / 128.0) == 0);
	CHECK(QuantizeToFixedPoint(1.0e12) == std::numeric_limits<int32_t>::max());
	CHECK(QuantizeToFixedPoint(-1.0e12) == std::numeric_limits<int32_t>::min());
	CHECK(QuantizeToFixedPoint(std::numeric_limits<double>::infinity()) == std::numeric_limits<int32_t>::max());
	CHECK(QuantizeToFixedPoint(std::numeric_limits<double>::quiet_NaN()) == 0);

	CHECK(QuantizeGamepadAxis(0.0f) == 0);
	CHECK(QuantizeGamepadAxis(1.0f) == 32767);
	CHECK(QuantizeGamepadAxis(-1.0f) == -32767);
	CHECK(QuantizeGamepadAxis(0.5f) == 16384);
	CHECK(QuantizeGamepadAxis(7.0f) == 32767);
	CHECK(QuantizeGamepadAxis(-7.0f) == -32767);
	CHECK(QuantizeGamepadAxis(std::numeric_limits<float>::quiet_NaN()) == 0);
}

TEST_CASE("Input: InputFrame serialization round-trips")
{
	for (const InputFrame& frame : { InputFrame{}, RichFrame() })
	{
		const Buffer bytes = frame.Serialize();
		const Result<InputFrame> read = InputFrame::Deserialize(bytes);
		REQUIRE(read.HasValue());
		CHECK(*read == frame);
		CHECK(read->Serialize().Size() == bytes.Size());
		CHECK(std::memcmp(read->Serialize().Data(), bytes.Data(), bytes.Size()) == 0); // canonical encoding
	}

	// 6 bytes of header, 102 of fixed content, 4 per typed character.
	CHECK(InputFrame{}.Serialize().Size() == 6 + 102);
	CHECK(RichFrame().Serialize().Size() == 6 + 102 + 3 * 4);
}

TEST_CASE("Input: InputFrame binary layout is fixed")
{
	// Little-endian fields in a fixed order: a regression guard for the replay file format.
	InputFrame frame;
	frame.SetKey(KeyCode::Space, true); // bit 1
	frame.MouseX = 0x01020304;
	const Buffer bytes = frame.Serialize();
	CHECK(bytes.Data()[0] == std::byte{ 'G' });
	CHECK(bytes.Data()[1] == std::byte{ 'B' });
	CHECK(bytes.Data()[2] == std::byte{ 'I' });
	CHECK(bytes.Data()[3] == std::byte{ 'F' });
	CHECK(bytes.Data()[4] == std::byte{ InputFrameFormatVersion });
	CHECK(bytes.Data()[5] == std::byte{ 0 });
	CHECK(bytes.Data()[6] == std::byte{ 0x02 });      // Keys[0], bit 1
	CHECK(bytes.Data()[6 + 16] == std::byte{ 0 });    // MouseButtons
	CHECK(bytes.Data()[6 + 17] == std::byte{ 0x04 }); // MouseX, little endian
	CHECK(bytes.Data()[6 + 20] == std::byte{ 0x01 });
	CHECK(Hash::XXH64(RichFrame().Serialize().View(), 0) == 0x3CAB'6AF1'33FC'D13Eull);
}

TEST_CASE("Input: InputFrame deserialization rejects bad headers and truncated data")
{
	const Buffer valid = RichFrame().Serialize();
	CHECK_FALSE(InputFrame::Deserialize(BufferView()).HasValue());
	CHECK_FALSE(InputFrame::Deserialize(BufferView::FromText("GBIF")).HasValue());
	CHECK_FALSE(InputFrame::Deserialize(WithByte(valid, 0, 'X')).HasValue()); // magic
	CHECK_FALSE(InputFrame::Deserialize(WithByte(valid, 4, 2)).HasValue());   // version
	CHECK_FALSE(InputFrame::Deserialize(WithByte(valid, 5, 1)).HasValue());

	for (size_t length = 0; length < valid.Size(); ++length)
	{
		CHECK_MESSAGE(
			!InputFrame::Deserialize(valid.View().Subview(0, length)).HasValue(), "truncated to ", length, " bytes");
	}

	Buffer trailing(valid.Size() + 1);
	std::memcpy(trailing.Data(), valid.Data(), valid.Size());
	const Result<InputFrame> withTrailing = InputFrame::Deserialize(trailing);
	REQUIRE_FALSE(withTrailing.HasValue());
	CHECK(withTrailing.GetError().Message.find("after the frame") != std::string::npos);
}

TEST_CASE("Input: InputFrame deserialization rejects values no frame can hold")
{
	const Buffer empty = InputFrame{}.Serialize();
	constexpr size_t Body = 6;
	constexpr size_t KeysHigh = Body + 8;
	constexpr size_t Gamepad0 = Body + 17 + 24;
	constexpr size_t TextCountOffset = Body + 17 + 24 + 60;

	// Key bits that do not exist: bit 0 (None) and everything from KeyCodeCount on.
	CHECK_FALSE(InputFrame::Deserialize(WithByte(empty, Body, 0x01)).HasValue());
	CHECK_FALSE(InputFrame::Deserialize(WithByte(empty, KeysHigh + 7, 0x80)).HasValue());
	CHECK_FALSE(InputFrame::Deserialize(WithByte(empty, KeysHigh + 7, 0x02)).HasValue()); // bit 57
	CHECK(InputFrame::Deserialize(WithByte(empty, KeysHigh + 7, 0x01)).HasValue());       // bit 56: KeyCode::Menu
	CHECK(InputFrame::Deserialize(WithByte(empty, KeysHigh, 0x01)).HasValue());           // bit 64 is a real key

	// Gamepad: a connection flag other than 0/1, state of a disconnected pad, button bits beyond the last button.
	CHECK_FALSE(InputFrame::Deserialize(WithByte(empty, Gamepad0, 2)).HasValue());
	CHECK_FALSE(InputFrame::Deserialize(WithByte(empty, Gamepad0 + 1, 1)).HasValue());
	CHECK_FALSE(InputFrame::Deserialize(WithByte(empty, Gamepad0 + 3, 1)).HasValue());
	Buffer connected = WithByte(empty, Gamepad0, 1);
	connected.Data()[Gamepad0 + 2] = std::byte{ 0x80 }; // button bit 15
	CHECK_FALSE(InputFrame::Deserialize(connected).HasValue());
	connected.Data()[Gamepad0 + 2] = std::byte{ 0x40 }; // button bit 14: DpadLeft
	CHECK(InputFrame::Deserialize(connected).HasValue());

	// Text: too many characters, invalid code points.
	CHECK_FALSE(InputFrame::Deserialize(WithByte(empty, TextCountOffset, 17)).HasValue());
	const auto withText = [&](uint32_t codepoint) {
		BinaryWriter writer;
		writer.WriteBytes(empty.View().Subview(0, TextCountOffset));
		writer.WriteU8(1);
		writer.WriteU32(codepoint);
		return InputFrame::Deserialize(writer.View());
	};
	CHECK(withText('A').HasValue());
	CHECK(withText(0x10FFFF).HasValue());
	CHECK_FALSE(withText(0).HasValue());
	CHECK_FALSE(withText(0xD800).HasValue()); // surrogate
	CHECK_FALSE(withText(0xDFFF).HasValue());
	CHECK_FALSE(withText(0x110000).HasValue());
	CHECK_FALSE(withText(0xFFFFFFFF).HasValue());
}
