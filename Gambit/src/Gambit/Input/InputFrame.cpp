#include "gbpch.h"

#include "Gambit/Input/InputFrame.h"

#include "Gambit/Core/BinaryStream.h"

namespace Gambit
{
	namespace
	{
		constexpr uint32_t InputFrameMagic = 0x46494247; // "GBIF" read as little endian bytes G B I F

		// Rounds half up and saturates at the limits of the type; NaN becomes 0.
		template<typename Integer>
		Integer SaturatingRound(double value) noexcept
		{
			constexpr Integer Low = std::numeric_limits<Integer>::min();
			constexpr Integer High = std::numeric_limits<Integer>::max();
			if (value != value)
			{
				return 0;
			}
			if (value <= static_cast<double>(Low))
			{
				return Low;
			}
			if (value >= static_cast<double>(High))
			{
				return High;
			}
			return static_cast<Integer>(std::floor(value + 0.5));
		}
	}

	int32_t QuantizeToFixedPoint(double value) noexcept
	{
		return SaturatingRound<int32_t>(value * static_cast<double>(InputFixedPointScale));
	}

	int16_t QuantizeGamepadAxis(float value) noexcept
	{
		const double scaled = static_cast<double>(value) * static_cast<double>(GamepadAxisMaximum);
		return static_cast<int16_t>(std::clamp(
			SaturatingRound<int32_t>(scaled), -int32_t{ GamepadAxisMaximum }, int32_t{ GamepadAxisMaximum }));
	}

	int16_t QuantizeGamepadTrigger(float value) noexcept
	{
		return std::max(QuantizeGamepadAxis(value), int16_t{ 0 });
	}

	void InputFrame::Write(BinaryWriter& writer) const
	{
		GB_CORE_ASSERT(TextCount <= MaxTextCodepointsPerFrame, "an input frame holds {} code points", TextCount);
		writer.WriteU64(Keys[0]);
		writer.WriteU64(Keys[1]);
		writer.WriteU8(MouseButtons);
		writer.WriteI32(MouseX);
		writer.WriteI32(MouseY);
		writer.WriteI32(MouseDeltaX);
		writer.WriteI32(MouseDeltaY);
		writer.WriteI32(ScrollX);
		writer.WriteI32(ScrollY);
		for (const GamepadFrame& gamepad : Gamepads)
		{
			writer.WriteU8(gamepad.Connected ? 1 : 0);
			writer.WriteU16(gamepad.Buttons);
			for (const int16_t axis : gamepad.Axes)
			{
				writer.WriteI16(axis);
			}
		}
		writer.WriteU8(TextCount);
		for (size_t i = 0; i < TextCount; ++i)
		{
			writer.WriteU32(Text[i]);
		}
	}

	Result<void> InputFrame::Validate() const
	{
		// Bit 0 is KeyCode::None and bits from KeyCodeCount on do not exist.
		constexpr uint64_t LowWordValid = ~uint64_t{ 1 };
		constexpr uint64_t HighWordValid = (uint64_t{ 1 } << (KeyCodeCount - 64)) - 1;
		if ((Keys[0] & ~LowWordValid) != 0 || (Keys[1] & ~HighWordValid) != 0)
		{
			return Error("input frame: unknown key bits");
		}
		for (const GamepadFrame& gamepad : Gamepads)
		{
			const bool idle =
				gamepad.Buttons == 0 && std::ranges::all_of(gamepad.Axes, [](int16_t axis) { return axis == 0; });
			if (!gamepad.Connected && !idle)
			{
				return Error("input frame: state of a disconnected gamepad");
			}
			if ((gamepad.Buttons >> GamepadButtonCount) != 0)
			{
				return Error("input frame: unknown gamepad button bits");
			}
			for (size_t index = 0; index < GamepadAxisCount; ++index)
			{
				const auto axis = static_cast<GamepadAxis>(index);
				const int32_t minimum = IsGamepadTrigger(axis) ? 0 : -int32_t{ GamepadAxisMaximum };
				if (gamepad.Axes[index] < minimum)
				{
					return Error(std::format(
						"input frame: gamepad axis {} is {} (at least {})", ToString(axis), gamepad.Axes[index],
						minimum));
				}
			}
		}
		if (TextCount > MaxTextCodepointsPerFrame)
		{
			return Error(
				std::format("input frame: {} typed characters (at most {})", TextCount, MaxTextCodepointsPerFrame));
		}
		for (size_t i = 0; i < TextCount; ++i)
		{
			if (!IsValidTextCodepoint(Text[i]))
			{
				return Error(std::format("input frame: invalid typed code point 0x{:X}", Text[i]));
			}
		}
		return {};
	}

	Result<InputFrame> InputFrame::Read(BinaryReader& reader)
	{
		InputFrame frame;
		frame.Keys[0] = reader.ReadU64();
		frame.Keys[1] = reader.ReadU64();
		frame.MouseButtons = reader.ReadU8();
		frame.MouseX = reader.ReadI32();
		frame.MouseY = reader.ReadI32();
		frame.MouseDeltaX = reader.ReadI32();
		frame.MouseDeltaY = reader.ReadI32();
		frame.ScrollX = reader.ReadI32();
		frame.ScrollY = reader.ReadI32();
		for (GamepadFrame& gamepad : frame.Gamepads)
		{
			const uint8_t connected = reader.ReadU8();
			gamepad.Buttons = reader.ReadU16();
			for (int16_t& axis : gamepad.Axes)
			{
				axis = reader.ReadI16();
			}
			if (connected > 1)
			{
				return Error("input frame: invalid gamepad connection flag");
			}
			gamepad.Connected = connected == 1;
		}
		const uint8_t textCount = reader.ReadU8();
		if (reader.Failed())
		{
			return Error("input frame: truncated data");
		}
		// Checked before reading: the count decides how many bytes belong to this frame.
		if (textCount > MaxTextCodepointsPerFrame)
		{
			return Error(
				std::format("input frame: {} typed characters (at most {})", textCount, MaxTextCodepointsPerFrame));
		}
		frame.TextCount = textCount;
		for (size_t i = 0; i < textCount; ++i)
		{
			frame.Text[i] = reader.ReadU32();
		}
		if (reader.Failed())
		{
			return Error("input frame: truncated data");
		}

		if (Result<void> valid = frame.Validate(); !valid)
		{
			return valid.GetError();
		}
		return frame;
	}

	Buffer InputFrame::Serialize() const
	{
		BinaryWriter writer;
		writer.WriteU32(InputFrameMagic);
		writer.WriteU16(InputFrameFormatVersion);
		Write(writer);
		return writer.ToBuffer();
	}

	Result<InputFrame> InputFrame::Deserialize(BufferView bytes)
	{
		BinaryReader reader(bytes);
		const uint32_t magic = reader.ReadU32();
		const uint16_t version = reader.ReadU16();
		if (reader.Failed() || magic != InputFrameMagic)
		{
			return Error("input frame: not an input frame (bad header)");
		}
		if (version != InputFrameFormatVersion)
		{
			return Error(std::format(
				"input frame: unsupported format version {} (this build reads {})", version, InputFrameFormatVersion));
		}
		Result<InputFrame> frame = Read(reader);
		if (!frame)
		{
			return frame;
		}
		if (reader.Remaining() != 0)
		{
			return Error("input frame: unexpected data after the frame");
		}
		return frame;
	}
}
