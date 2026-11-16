#include <doctest/doctest.h>

#include "Gambit/Core/MemoryMount.h"
#include "Gambit/Core/VFS.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace Gambit;

namespace
{
	std::vector<std::string> Paths(const std::vector<DirectoryEntry>& entries)
	{
		std::vector<std::string> paths;
		for (const DirectoryEntry& entry : entries)
		{
			paths.push_back(entry.Path + (entry.IsDirectory ? "/" : ""));
		}
		std::ranges::sort(paths);
		return paths;
	}
}

TEST_CASE("Core: MemoryMount holds files and implicit directories")
{
	MemoryMount mount;
	REQUIRE(mount.AddTextFile("Scripts/A.lua", "a").HasValue());
	REQUIRE(mount.AddTextFile("Scripts/Sub/B.lua", "b").HasValue());
	REQUIRE(mount.AddTextFile("Top.txt", "t").HasValue());

	CHECK(mount.Exists(""));
	CHECK(mount.Exists("Scripts"));
	CHECK(mount.Exists("Scripts/Sub/B.lua"));
	CHECK_FALSE(mount.Exists("Script"));
	CHECK_FALSE(mount.Exists("Scripts/Sub/C.lua"));

	Result<Buffer> read = mount.Read("Scripts/A.lua");
	REQUIRE(read.HasValue());
	CHECK(std::string(reinterpret_cast<const char*>(read->Data()), read->Size()) == "a");
	CHECK(HasFileError(mount.Read("Scripts").GetError(), FileError::InvalidPath));
	CHECK(HasFileError(mount.Read("None").GetError(), FileError::NotFound));

	CHECK(Paths(*mount.List("", false)) == std::vector<std::string>{ "Scripts/", "Top.txt" });
	CHECK(Paths(*mount.List("Scripts", false)) == std::vector<std::string>{ "Scripts/A.lua", "Scripts/Sub/" });
	CHECK(
		Paths(*mount.List("Scripts", true))
		== std::vector<std::string>{ "Scripts/A.lua", "Scripts/Sub/", "Scripts/Sub/B.lua" });
	CHECK(HasFileError(mount.List("Top.txt", false).GetError(), FileError::InvalidPath));
	CHECK(HasFileError(mount.List("Nothing", false).GetError(), FileError::NotFound));

	// A file cannot be where a directory is, nor below a file.
	CHECK(HasFileError(mount.AddTextFile("Scripts", "x").GetError(), FileError::InvalidPath));
	CHECK(HasFileError(mount.AddTextFile("Top.txt/Inner", "x").GetError(), FileError::InvalidPath));
}

TEST_CASE("Core: MemoryMount writes and removes through the VFS")
{
	VFS vfs;
	const Ref<MemoryMount> mount = CreateRef<MemoryMount>();
	REQUIRE(vfs.Mount("", mount).HasValue());
	REQUIRE(vfs.WriteText("Data/Table.csv", "Id\nx\n").HasValue());
	CHECK(*vfs.ReadText("Data/Table.csv") == "Id\nx\n");
	CHECK(HasFileError(vfs.Remove("Data").GetError(), FileError::Io)); // not empty
	REQUIRE(vfs.Remove("Data/Table.csv").HasValue());
	CHECK_FALSE(vfs.Exists("Data")); // the directory went with its last file
	CHECK(HasFileError(vfs.Remove("Data/Table.csv").GetError(), FileError::NotFound));

	const Ref<MemoryMount> readOnly = CreateRef<MemoryMount>(false);
	REQUIRE(readOnly->AddTextFile("Fixed.txt", "f").HasValue());
	CHECK(HasFileError(readOnly->Write("Other.txt", BufferView::FromText("o")).GetError(), FileError::ReadOnly));
	CHECK(HasFileError(readOnly->Remove("Fixed.txt").GetError(), FileError::ReadOnly));
	CHECK(readOnly->GetMountType() == "memory");
}
