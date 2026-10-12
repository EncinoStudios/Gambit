#include <doctest/doctest.h>

#include "TestSupport.h"

#include "Gambit/Core/FileSystem.h"

#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using namespace Gambit;
namespace fs = std::filesystem;

namespace
{
	std::string Normalized(std::string_view path)
	{
		const Result<std::string> result = FileSystem::NormalizePath(path);
		REQUIRE_MESSAGE(result.HasValue(), "path: '", std::string(path), "'");
		return *result;
	}

	void CheckRejected(std::string_view path)
	{
		const Result<std::string> result = FileSystem::NormalizePath(path);
		CHECK_MESSAGE(!result.HasValue(), "path: '", std::string(path), "' normalized to '", result.ValueOr(""), "'");
		if (!result.HasValue())
		{
			CHECK(HasFileError(result.GetError(), FileError::InvalidPath));
		}
	}

	std::vector<std::string> Paths(const std::vector<DirectoryEntry>& entries)
	{
		std::vector<std::string> paths;
		for (const DirectoryEntry& entry : entries)
		{
			paths.push_back(entry.IsDirectory ? entry.Path + "/" : entry.Path);
		}
		return paths;
	}
}

TEST_CASE("Core: FileSystem binary files round-trip")
{
	Tests::ScopedTempDirectory directory;
	const fs::path path = directory.GetPath() / "data.bin";

	std::vector<uint8_t> bytes(70000);
	for (size_t i = 0; i < bytes.size(); ++i)
	{
		bytes[i] = static_cast<uint8_t>(i * 31 + 7);
	}
	REQUIRE(FileSystem::WriteBinary(path, BufferView(bytes.data(), bytes.size())).HasValue());
	CHECK(FileSystem::Exists(path));

	const Result<Buffer> read = FileSystem::ReadBinary(path);
	REQUIRE(read.HasValue());
	REQUIRE(read->Size() == bytes.size());
	CHECK(std::memcmp(read->Data(), bytes.data(), bytes.size()) == 0);
}

TEST_CASE("Core: FileSystem handles empty files")
{
	Tests::ScopedTempDirectory directory;
	const fs::path path = directory.GetPath() / "empty";
	REQUIRE(FileSystem::WriteBinary(path, BufferView()).HasValue());

	const Result<Buffer> binary = FileSystem::ReadBinary(path);
	REQUIRE(binary.HasValue());
	CHECK(binary->Empty());
	const Result<std::string> text = FileSystem::ReadText(path);
	REQUIRE(text.HasValue());
	CHECK(text->empty());
}

TEST_CASE("Core: FileSystem text keeps every byte except a leading byte order mark")
{
	Tests::ScopedTempDirectory directory;
	const fs::path plain = directory.GetPath() / "plain.txt";
	REQUIRE(FileSystem::WriteText(plain, "line one\r\nline two\n\xC3\xA9").HasValue());
	CHECK(FileSystem::ReadText(plain).ValueOr("") == "line one\r\nline two\n\xC3\xA9");

	const fs::path withBom = directory.GetPath() / "bom.txt";
	REQUIRE(FileSystem::WriteText(withBom, "\xEF\xBB\xBF{\"a\":1}").HasValue());
	CHECK(FileSystem::ReadText(withBom).ValueOr("") == "{\"a\":1}");
	CHECK(FileSystem::ReadBinary(withBom)->Size() == 10); // the binary read is untouched

	CHECK(FileSystem::BytesToText(BufferView::FromText("\xEF\xBB\xBFx")) == "x");
	CHECK(FileSystem::BytesToText(BufferView::FromText("x\xEF\xBB\xBF")) == "x\xEF\xBB\xBF");
}

TEST_CASE("Core: FileSystem read errors say what is wrong")
{
	Tests::ScopedTempDirectory directory;

	const Result<Buffer> missing = FileSystem::ReadBinary(directory.GetPath() / "missing.bin");
	REQUIRE_FALSE(missing.HasValue());
	CHECK(HasFileError(missing.GetError(), FileError::NotFound));
	CHECK(missing.GetError().Message.find("missing.bin") != std::string::npos);

	const Result<std::string> missingText = FileSystem::ReadText(directory.GetPath() / "missing.txt");
	REQUIRE_FALSE(missingText.HasValue());
	CHECK(HasFileError(missingText.GetError(), FileError::NotFound));

	const Result<Buffer> isDirectory = FileSystem::ReadBinary(directory.GetPath());
	REQUIRE_FALSE(isDirectory.HasValue());
	CHECK(HasFileError(isDirectory.GetError(), FileError::InvalidPath));
}

TEST_CASE("Core: FileSystem writes replace files atomically and leave no temporary files")
{
	Tests::ScopedTempDirectory directory;
	const fs::path path = directory.GetPath() / "config.json";

	REQUIRE(FileSystem::WriteText(path, "first version, quite long").HasValue());
	REQUIRE(FileSystem::WriteText(path, "second").HasValue());
	CHECK(FileSystem::ReadText(path).ValueOr("") == "second");

	const Result<std::vector<DirectoryEntry>> entries = FileSystem::ListDirectory(directory.GetPath(), false);
	REQUIRE(entries.HasValue());
	REQUIRE(entries->size() == 1);
	CHECK(entries->front().Path == "config.json");
}

TEST_CASE("Core: FileSystem write failures are reported and clean up")
{
	Tests::ScopedTempDirectory directory;

	const Result<void> noParent = FileSystem::WriteText(directory.GetPath() / "missing" / "file.txt", "x");
	REQUIRE_FALSE(noParent.HasValue());
	CHECK(HasFileError(noParent.GetError(), FileError::Io));

	const Result<void> noName = FileSystem::WriteText(directory.GetPath() / "dir" / "", "x");
	REQUIRE_FALSE(noName.HasValue());
	CHECK(HasFileError(noName.GetError(), FileError::InvalidPath));

	// The target is a directory: the rename fails and the temporary file is removed again.
	REQUIRE(FileSystem::CreateDirectories(directory.GetPath() / "target").HasValue());
	const Result<void> overDirectory = FileSystem::WriteText(directory.GetPath() / "target", "x");
	CHECK_FALSE(overDirectory.HasValue());
	const Result<std::vector<DirectoryEntry>> entries = FileSystem::ListDirectory(directory.GetPath(), false);
	REQUIRE(entries.HasValue());
	CHECK(Paths(*entries) == std::vector<std::string>{ "target/" });
}

// Replacing a file that another handle has open fails on Windows (sharing violation), so readers racing with a
// writer cannot be exercised there.
#if !defined(_WIN32)
TEST_CASE("Core: FileSystem readers never observe a partially written file")
{
	Tests::ScopedTempDirectory directory;
	const fs::path path = directory.GetPath() / "shared.bin";
	constexpr size_t Size = 256 * 1024;
	REQUIRE(FileSystem::WriteText(path, std::string(Size, 'A')).HasValue());

	std::atomic<bool> stop = false;
	std::atomic<int> torn = 0;
	std::atomic<int> reads = 0;
	std::thread reader([&] {
		while (!stop)
		{
			const Result<std::string> text = FileSystem::ReadText(path);
			if (!text.HasValue() || text->size() != Size || text->find_first_not_of(text->front()) != std::string::npos)
			{
				++torn;
			}
			++reads;
		}
	});

	for (int i = 0; i < 40; ++i)
	{
		REQUIRE(FileSystem::WriteText(path, std::string(Size, i % 2 == 0 ? 'B' : 'C')).HasValue());
	}
	stop = true;
	reader.join();
	CHECK(reads > 0);
	CHECK(torn == 0);
}
#endif

TEST_CASE("Core: FileSystem CreateDirectories")
{
	Tests::ScopedTempDirectory directory;
	const fs::path nested = directory.GetPath() / "a" / "b" / "c";

	CHECK_FALSE(FileSystem::Exists(nested));
	REQUIRE(FileSystem::CreateDirectories(nested).HasValue());
	CHECK(fs::is_directory(nested));
	CHECK(FileSystem::CreateDirectories(nested).HasValue()); // idempotent

	REQUIRE(FileSystem::WriteText(directory.GetPath() / "file", "x").HasValue());
	const Result<void> overFile = FileSystem::CreateDirectories(directory.GetPath() / "file");
	REQUIRE_FALSE(overFile.HasValue());
	CHECK(HasFileError(overFile.GetError(), FileError::InvalidPath));
	CHECK_FALSE(FileSystem::CreateDirectories(directory.GetPath() / "file" / "sub").HasValue());
}

TEST_CASE("Core: FileSystem ListDirectory is sorted and relative")
{
	Tests::ScopedTempDirectory directory;
	const fs::path root = directory.GetPath();
	REQUIRE(FileSystem::CreateDirectories(root / "Assets" / "Meshes").HasValue());
	REQUIRE(FileSystem::CreateDirectories(root / "Empty").HasValue());
	REQUIRE(FileSystem::WriteText(root / "b.txt", "b").HasValue());
	REQUIRE(FileSystem::WriteText(root / "a.txt", "a").HasValue());
	REQUIRE(FileSystem::WriteText(root / "Assets" / "Meshes" / "Crate.glb", "m").HasValue());
	REQUIRE(FileSystem::WriteText(root / "Assets" / "Readme.md", "r").HasValue());

	const Result<std::vector<DirectoryEntry>> shallow = FileSystem::ListDirectory(root, false);
	REQUIRE(shallow.HasValue());
	CHECK(Paths(*shallow) == std::vector<std::string>{ "Assets/", "Empty/", "a.txt", "b.txt" });

	const Result<std::vector<DirectoryEntry>> deep = FileSystem::ListDirectory(root, true);
	REQUIRE(deep.HasValue());
	CHECK(
		Paths(*deep)
		== std::vector<std::string>{ "Assets/", "Assets/Meshes/", "Assets/Meshes/Crate.glb", "Assets/Readme.md",
	                                 "Empty/", "a.txt", "b.txt" });

	const Result<std::vector<DirectoryEntry>> sub = FileSystem::ListDirectory(root / "Assets", false);
	REQUIRE(sub.HasValue());
	CHECK(Paths(*sub) == std::vector<std::string>{ "Meshes/", "Readme.md" });

	const Result<std::vector<DirectoryEntry>> empty = FileSystem::ListDirectory(root / "Empty", true);
	REQUIRE(empty.HasValue());
	CHECK(empty->empty());
}

TEST_CASE("Core: FileSystem ListDirectory errors")
{
	Tests::ScopedTempDirectory directory;
	const Result<std::vector<DirectoryEntry>> missing = FileSystem::ListDirectory(directory.GetPath() / "nope", false);
	REQUIRE_FALSE(missing.HasValue());
	CHECK(HasFileError(missing.GetError(), FileError::NotFound));

	REQUIRE(FileSystem::WriteText(directory.GetPath() / "file", "x").HasValue());
	const Result<std::vector<DirectoryEntry>> notDirectory =
		FileSystem::ListDirectory(directory.GetPath() / "file", false);
	REQUIRE_FALSE(notDirectory.HasValue());
	CHECK(HasFileError(notDirectory.GetError(), FileError::InvalidPath));
}

TEST_CASE("Core: FileSystem NormalizePath resolves segments")
{
	CHECK(Normalized("") == "");
	CHECK(Normalized(".") == "");
	CHECK(Normalized("./") == "");
	CHECK(Normalized("a") == "a");
	CHECK(Normalized("a/b/c.txt") == "a/b/c.txt");
	CHECK(Normalized("a//b///c") == "a/b/c");
	CHECK(Normalized("./a/./b/.") == "a/b");
	CHECK(Normalized("a/b/../c") == "a/c");
	CHECK(Normalized("a/b/..") == "a");
	CHECK(Normalized("a/..") == "");
	CHECK(Normalized("a/b/../../c") == "c");
	CHECK(Normalized("a/b/") == "a/b");
	CHECK(Normalized("a\\b\\c") == "a/b/c");
	CHECK(Normalized("a\\b/../c") == "a/c");
	CHECK(Normalized("Assets/Meshes/Crate.glb") == "Assets/Meshes/Crate.glb");
	CHECK(Normalized("Assets/My Files/crate (1).glb") == "Assets/My Files/crate (1).glb");
	CHECK(Normalized("a/..b/.c/d..") == "a/..b/.c/d.."); // dots inside names are ordinary characters
	CHECK(Normalized("Ünï/cödé.txt") == "Ünï/cödé.txt");
}

TEST_CASE("Core: FileSystem NormalizePath keeps absolute paths absolute")
{
	CHECK(Normalized("/") == "/");
	CHECK(Normalized("/a/b") == "/a/b");
	CHECK(Normalized("/a/../b") == "/b");
	CHECK(Normalized("//a///b") == "/a/b");
	CHECK(Normalized("\\a\\b") == "/a/b");
	CHECK(Normalized("C:/") == "C:/");
	CHECK(Normalized("C:") == "C:/");
	CHECK(Normalized("c:\\Users\\me\\..\\you") == "c:/Users/you");

	CHECK(FileSystem::IsAbsolutePath("/a"));
	CHECK(FileSystem::IsAbsolutePath("\\a"));
	CHECK(FileSystem::IsAbsolutePath("C:/a"));
	CHECK(FileSystem::IsAbsolutePath("c:"));
	CHECK_FALSE(FileSystem::IsAbsolutePath(""));
	CHECK_FALSE(FileSystem::IsAbsolutePath("a/b"));
	CHECK_FALSE(FileSystem::IsAbsolutePath("./a"));
	CHECK_FALSE(FileSystem::IsAbsolutePath("1:/a"));
}

TEST_CASE("Core: FileSystem NormalizePath rejects escapes and unsafe characters")
{
	CheckRejected("..");
	CheckRejected("../a");
	CheckRejected("a/../..");
	CheckRejected("a/../../b");
	CheckRejected("..\\a");
	CheckRejected("a\\..\\..\\b");
	CheckRejected("/..");
	CheckRejected("/a/../..");
	CheckRejected("C:/..");
	CheckRejected("C:foo"); // drive-relative
	CheckRejected("a:b");   // colon inside a segment (alternate data streams)
	CheckRejected("file.txt:stream");
	CheckRejected(std::string_view("a\0b", 3));
	CheckRejected("a/b\nc");
	CheckRejected("a/\tb");
}

TEST_CASE("Core: FileSystem UTF-8 conversions use forward slashes")
{
	const fs::path path = FileSystem::FromUtf8("Ünï/cödé.txt");
	CHECK(FileSystem::ToUtf8(path) == "Ünï/cödé.txt");
	CHECK(FileSystem::ToUtf8(fs::path("a") / "b" / "c.txt") == "a/b/c.txt");
	CHECK(FileSystem::ToUtf8(fs::path()) == "");

	Tests::ScopedTempDirectory directory;
	const fs::path unicode = directory.GetPath() / FileSystem::FromUtf8("Ünï.txt");
	REQUIRE(FileSystem::WriteText(unicode, "ok").HasValue());
	CHECK(FileSystem::ReadText(unicode).ValueOr("") == "ok");
	const Result<std::vector<DirectoryEntry>> entries = FileSystem::ListDirectory(directory.GetPath(), false);
	REQUIRE(entries.HasValue());
	REQUIRE(entries->size() == 1);
	CHECK(entries->front().Path == "Ünï.txt");
}

TEST_CASE("Core: FileSystem GetExecutableDirectory names the directory of the running program")
{
	const Result<fs::path> directory = FileSystem::GetExecutableDirectory();
	REQUIRE(directory.HasValue());
	CHECK(directory->is_absolute());
	CHECK(fs::is_directory(*directory));
#if defined(_WIN32)
	CHECK(fs::exists(*directory / "GambitTests.exe"));
#else
	CHECK(fs::exists(*directory / "GambitTests"));
#endif
}
