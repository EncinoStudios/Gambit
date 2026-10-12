#include <doctest/doctest.h>

#include "TestSupport.h"

#include "Gambit/Core/BinaryStream.h"
#include "Gambit/Core/FileSystem.h"
#include "Gambit/Core/Hash.h"
#include "Gambit/Input/InputLog.h"
#include "Gambit/Input/InputSource.h"

#include <cstring>

using namespace Gambit;

namespace
{
	InputLog SampleLog(int frames = 3)
	{
		InputLog log({ "FeatureTest", "Scenes/Level 1.gscene", 0x1234567890ABCDEFull, 60 });
		for (int i = 0; i < frames; ++i)
		{
			InputFrame frame;
			frame.SetKey(static_cast<KeyCode>(1 + i * 7 % 120), true);
			frame.SetMouseButton(MouseButton::Left, i % 2 == 0);
			frame.MouseX = i * 100;
			frame.MouseDeltaX = i - 1;
			frame.ScrollY = i * 64;
			frame.Gamepads[i % MaxGamepads].Connected = true;
			frame.Gamepads[i % MaxGamepads].Axes[1] = static_cast<int16_t>(i * 1000);
			if (i % 2 == 1)
			{
				frame.AddText(static_cast<uint32_t>('a' + i));
			}
			log.Record(frame);
		}
		return log;
	}

	// Rewrites the trailing checksum so that a modified file is damaged in content, not in integrity.
	void FixChecksum(Buffer& file)
	{
		const uint64_t checksum = Hash::XXH64(file.View().Subview(0, file.Size() - 8), 0);
		for (size_t i = 0; i < 8; ++i)
		{
			file.Data()[file.Size() - 8 + i] = static_cast<std::byte>((checksum >> (8 * i)) & 0xFF);
		}
	}

	std::string ErrorOf(const Buffer& file)
	{
		const Result<InputLog> result = InputLog::Deserialize(file);
		REQUIRE_FALSE(result.HasValue());
		return result.GetError().Message;
	}

	// Offsets in a serialized SampleLog header: magic 4, versions 2+2, then the strings.
	constexpr size_t VersionOffset = 4;
	constexpr size_t FrameVersionOffset = 6;
}

TEST_CASE("Input: InputLog records and exposes frames")
{
	InputLog log;
	CHECK(log.GetFrameCount() == 0);
	InputFrame frame;
	frame.SetKey(KeyCode::A, true);
	log.Record(frame);
	log.Record(InputFrame{});
	REQUIRE(log.GetFrameCount() == 2);
	CHECK(log.GetFrame(0) == frame);
	CHECK(log.GetFrames().size() == 2);
	log.Clear();
	CHECK(log.GetFrameCount() == 0);

	log.SetHeader({ "P", "S", 7, 30 });
	CHECK(log.GetHeader().Seed == 7);
	CHECK(log.GetHeader() == InputLogHeader{ "P", "S", 7, 30 });
}

TEST_CASE("Input: InputLog serialization round-trips")
{
	for (const int frames : { 0, 1, 3, 200 })
	{
		const InputLog log = SampleLog(frames);
		const Buffer bytes = log.Serialize();
		const Result<InputLog> read = InputLog::Deserialize(bytes);
		REQUIRE_MESSAGE(read.HasValue(), "frames: ", frames);
		CHECK(*read == log);
		CHECK(read->GetHeader().ProjectName == "FeatureTest");
		CHECK(read->GetHeader().ScenePath == "Scenes/Level 1.gscene");
		CHECK(read->GetHeader().Seed == 0x1234567890ABCDEFull);
		CHECK(read->GetHeader().SimulationHz == 60);
		CHECK(read->GetFrameCount() == static_cast<size_t>(frames));
	}

	// Non-ASCII text and empty strings survive.
	InputLog unicode({ "Pr\xC3\xB6jekt", "", 0, 144 });
	const Result<InputLog> read = InputLog::Deserialize(unicode.Serialize());
	REQUIRE(read.HasValue());
	CHECK(*read == unicode);
}

TEST_CASE("Input: InputLog binary format is fixed")
{
	// A regression guard for the .ginput format: these bytes must stay readable and identical.
	const Buffer bytes = SampleLog(3).Serialize();
	CHECK(bytes.Data()[0] == std::byte{ 'G' });
	CHECK(bytes.Data()[1] == std::byte{ 'B' });
	CHECK(bytes.Data()[2] == std::byte{ 'I' });
	CHECK(bytes.Data()[3] == std::byte{ 'L' });
	CHECK(bytes.Data()[VersionOffset] == std::byte{ InputLogFormatVersion });
	CHECK(bytes.Data()[FrameVersionOffset] == std::byte{ InputFrameFormatVersion });
	CHECK(bytes.Size() == 8 + (4 + 11) + (4 + 21) + 8 + 4 + 8 + 3 * 102 + 1 * 4 + 8);
	CHECK(Hash::XXH64(bytes.View(), 0) == 0xF73E'E5CA'6819'4C31ull);
}

TEST_CASE("Input: InputLog rejects every truncation")
{
	const Buffer bytes = SampleLog(3).Serialize();
	for (size_t length = 0; length < bytes.Size(); ++length)
	{
		CHECK_MESSAGE(
			!InputLog::Deserialize(bytes.View().Subview(0, length)).HasValue(), "truncated to ", length, " of ",
			bytes.Size(), " bytes");
	}
}

TEST_CASE("Input: InputLog rejects every single corrupted byte")
{
	const Buffer bytes = SampleLog(3).Serialize();
	for (size_t offset = 0; offset < bytes.Size(); ++offset)
	{
		for (const uint8_t flip : { uint8_t{ 0x01 }, uint8_t{ 0x80 } })
		{
			Buffer damaged = Buffer::Copy(bytes);
			damaged.Data()[offset] ^= static_cast<std::byte>(flip);
			CHECK_MESSAGE(!InputLog::Deserialize(damaged).HasValue(), "byte ", offset, " xor ", static_cast<int>(flip));
		}
	}
}

TEST_CASE("Input: InputLog reports why a file is rejected")
{
	const Buffer valid = SampleLog(3).Serialize();
	CHECK(ErrorOf(Buffer()).find("too short") != std::string::npos);
	CHECK(
		ErrorOf(Buffer::Copy(BufferView::FromText("hello world, this is no log"))).find("bad magic")
		!= std::string::npos);

	Buffer damaged = Buffer::Copy(valid);
	damaged.Data()[40] ^= std::byte{ 0xFF };
	CHECK(ErrorOf(damaged).find("checksum") != std::string::npos);

	Buffer version = Buffer::Copy(valid);
	version.Data()[VersionOffset] = std::byte{ 9 };
	FixChecksum(version);
	CHECK(ErrorOf(version).find("unsupported format version 9/") != std::string::npos);

	Buffer frameVersion = Buffer::Copy(valid);
	frameVersion.Data()[FrameVersionOffset] = std::byte{ 7 };
	FixChecksum(frameVersion);
	CHECK(ErrorOf(frameVersion).find("unsupported format version") != std::string::npos);
}

TEST_CASE("Input: InputLog validates the content even when the checksum matches")
{
	const Buffer valid = SampleLog(3).Serialize();
	// Offsets after the header strings: seed 8, rate 4, count 8.
	constexpr size_t StringsEnd = 8 + (4 + 11) + (4 + 21);
	constexpr size_t RateOffset = StringsEnd + 8;
	constexpr size_t CountOffset = RateOffset + 4;
	constexpr size_t FramesOffset = CountOffset + 8;

	Buffer zeroRate = Buffer::Copy(valid);
	std::memset(zeroRate.Data() + RateOffset, 0, 4);
	FixChecksum(zeroRate);
	CHECK(ErrorOf(zeroRate).find("simulation rate 0 Hz") != std::string::npos);

	Buffer hugeRate = Buffer::Copy(valid);
	hugeRate.Data()[RateOffset + 1] = std::byte{ 0x10 }; // 4156 Hz
	FixChecksum(hugeRate);
	CHECK(ErrorOf(hugeRate).find("invalid simulation rate") != std::string::npos);

	Buffer absurdCount = Buffer::Copy(valid);
	std::memset(absurdCount.Data() + CountOffset, 0xFF, 8);
	FixChecksum(absurdCount);
	CHECK(ErrorOf(absurdCount).find("do not fit") != std::string::npos);

	Buffer fewerFrames = Buffer::Copy(valid);
	fewerFrames.Data()[CountOffset] = std::byte{ 2 };
	FixChecksum(fewerFrames);
	CHECK(ErrorOf(fewerFrames).find("unexpected data after the last frame") != std::string::npos);

	Buffer moreFrames = Buffer::Copy(valid);
	moreFrames.Data()[CountOffset] = std::byte{ 4 };
	FixChecksum(moreFrames);
	CHECK_FALSE(InputLog::Deserialize(moreFrames).HasValue());

	Buffer badFrame = Buffer::Copy(valid);
	badFrame.Data()[FramesOffset + 102] = std::byte{ 0x01 }; // bit 0 of the key set of frame 1 is no key
	FixChecksum(badFrame);
	const std::string message = ErrorOf(badFrame);
	CHECK(message.find("frame 1:") != std::string::npos);
	CHECK(message.find("unknown key bits") != std::string::npos);

	Buffer hugeString = Buffer::Copy(valid);
	hugeString.Data()[8 + 2] = std::byte{ 0x01 }; // project name length 0x0001000B
	FixChecksum(hugeString);
	CHECK(ErrorOf(hugeString).find("damaged header") != std::string::npos);
}

TEST_CASE("Input: InputLog validates its header before it is written")
{
	InputLog tooLong({ std::string(MaxInputLogStringLength + 1, 'p'), "", 0, 60 });
	CHECK_FALSE(tooLong.Validate().HasValue());
	InputLog noRate({ "p", "s", 0, 0 });
	CHECK_FALSE(noRate.Validate().HasValue());
	InputLog fastest({ std::string(MaxInputLogStringLength, 'p'), std::string(MaxInputLogStringLength, 's'), 0, 1000 });
	REQUIRE(fastest.Validate().HasValue());
	CHECK(InputLog::Deserialize(fastest.Serialize()).HasValue());

	Tests::ScopedTempDirectory directory;
	CHECK_FALSE(tooLong.Save(directory.GetPath() / "bad.ginput").HasValue());
	CHECK_FALSE(FileSystem::Exists(directory.GetPath() / "bad.ginput"));
}

TEST_CASE("Input: InputLog saves and loads files")
{
	Tests::ScopedTempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "Run1.ginput";
	const InputLog log = SampleLog(50);
	REQUIRE(log.Save(path).HasValue());

	const Result<InputLog> loaded = InputLog::Load(path);
	REQUIRE(loaded.HasValue());
	CHECK(*loaded == log);

	const Result<InputLog> missing = InputLog::Load(directory.GetPath() / "Nope.ginput");
	REQUIRE_FALSE(missing.HasValue());
	CHECK(HasFileError(missing.GetError(), FileError::NotFound));

	REQUIRE(
		FileSystem::WriteText(directory.GetPath() / "Garbage.ginput", "this is not an input log at all").HasValue());
	const Result<InputLog> garbage = InputLog::Load(directory.GetPath() / "Garbage.ginput");
	REQUIRE_FALSE(garbage.HasValue());
	CHECK(garbage.GetError().Message.find("Garbage.ginput") != std::string::npos);
}

TEST_CASE("Input: ReplayInputSource plays a log back and then reports it finished")
{
	auto log = CreateRef<InputLog>(SampleLog(3));
	ReplayInputSource source(log);
	CHECK_FALSE(source.IsFinished());
	CHECK(source.GetPosition() == 0);
	CHECK(&source.GetLog() == log.get());

	CHECK(source.Sample() == log->GetFrame(0));
	CHECK(source.GetPosition() == 1);
	CHECK(source.Sample() == log->GetFrame(1));
	CHECK_FALSE(source.IsFinished());
	CHECK(source.Sample() == log->GetFrame(2));
	CHECK(source.IsFinished());

	CHECK(source.Sample() == InputFrame{}); // past the end: nothing pressed
	CHECK(source.GetPosition() == 3);
	CHECK(source.IsFinished());

	ReplayInputSource empty(CreateRef<InputLog>());
	CHECK(empty.IsFinished());
	CHECK(empty.Sample() == InputFrame{});
}
