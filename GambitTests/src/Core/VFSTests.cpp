#include <doctest/doctest.h>

#include "TestSupport.h"

#include "Gambit/Core/DirectoryMount.h"
#include "Gambit/Core/VFS.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace Gambit;
namespace fs = std::filesystem;

namespace
{
	// An in-memory mount: shows how pack and remote mounts plug into the IFileMount interface, and lets the VFS tests
	// run without a file system.
	class MemoryMount final : public IFileMount
	{
	public:
		explicit MemoryMount(bool writable = false)
			: m_Writable(writable)
		{}

		void Add(const std::string& path, const std::string& content) { m_Files[path] = content; }

		// Reads of this path fail with an I/O error instead of NotFound.
		void Break(const std::string& path) { m_Broken.insert(path); }

		[[nodiscard]] bool Exists(std::string_view path) const override
		{
			const std::string key(path);
			if (key.empty() || m_Files.contains(key))
			{
				return true;
			}
			const std::string prefix = key + "/";
			return std::ranges::any_of(m_Files, [&](const auto& file) { return file.first.starts_with(prefix); });
		}

		[[nodiscard]] Result<Buffer> Read(std::string_view path) const override
		{
			const std::string key(path);
			if (m_Broken.contains(key))
			{
				return MakeFileError(FileError::Io, "broken " + key);
			}
			const auto found = m_Files.find(key);
			if (found == m_Files.end())
			{
				return MakeFileError(FileError::NotFound, "no " + key);
			}
			return Buffer::Copy(BufferView::FromText(found->second));
		}

		[[nodiscard]] Result<std::vector<DirectoryEntry>>
		List(std::string_view directory, bool recursive) const override
		{
			if (!Exists(directory))
			{
				return MakeFileError(FileError::NotFound, "no directory");
			}
			const std::string prefix = directory.empty() ? "" : std::string(directory) + "/";
			std::map<std::string, bool> found; // path -> is directory
			for (const auto& [path, content] : m_Files)
			{
				if (!path.starts_with(prefix))
				{
					continue;
				}
				const std::string rest = path.substr(prefix.size());
				size_t slash = rest.find('/');
				if (!recursive)
				{
					found[prefix + rest.substr(0, slash)] = slash != std::string::npos;
					continue;
				}
				while (slash != std::string::npos)
				{
					found[prefix + rest.substr(0, slash)] = true;
					slash = rest.find('/', slash + 1);
				}
				found[path] = false;
			}
			std::vector<DirectoryEntry> entries;
			for (const auto& [path, isDirectory] : found)
			{
				entries.push_back({ path, isDirectory });
			}
			return entries;
		}

		[[nodiscard]] bool IsWritable() const override { return m_Writable; }

		[[nodiscard]] Result<void> Write(std::string_view path, BufferView data) override
		{
			if (!m_Writable)
			{
				return MakeFileError(FileError::ReadOnly, "read-only");
			}
			m_Files[std::string(path)].assign(reinterpret_cast<const char*>(data.Data()), data.Size());
			return {};
		}

		[[nodiscard]] Result<void> Remove(std::string_view path) override
		{
			if (!m_Writable)
			{
				return MakeFileError(FileError::ReadOnly, "read-only");
			}
			return m_Files.erase(std::string(path)) > 0 ? Result<void>()
														: Result<void>(MakeFileError(FileError::NotFound, "no file"));
		}

		[[nodiscard]] std::string_view GetMountType() const override { return "memory"; }

		[[nodiscard]] std::string Get(const std::string& path) const
		{
			const auto found = m_Files.find(path);
			return found == m_Files.end() ? "<missing>" : found->second;
		}

	private:
		std::map<std::string, std::string> m_Files;
		std::set<std::string> m_Broken;
		bool m_Writable;
	};

	std::vector<std::string> Paths(const Result<std::vector<DirectoryEntry>>& listing)
	{
		REQUIRE(listing.HasValue());
		std::vector<std::string> paths;
		for (const DirectoryEntry& entry : *listing)
		{
			paths.push_back(entry.IsDirectory ? entry.Path + "/" : entry.Path);
		}
		return paths;
	}

	std::string Text(const VFS& vfs, std::string_view path)
	{
		return vfs.ReadText(path).ValueOr("<error>");
	}

	void WriteFile(const fs::path& path, const std::string& content)
	{
		REQUIRE(FileSystem::CreateDirectories(path.parent_path()).HasValue());
		REQUIRE(FileSystem::WriteText(path, content).HasValue());
	}

	Ref<MemoryMount> MakeMemory(std::initializer_list<std::pair<std::string, std::string>> files, bool writable = false)
	{
		auto mount = CreateRef<MemoryMount>(writable);
		for (const auto& [path, content] : files)
		{
			mount->Add(path, content);
		}
		return mount;
	}
}

// ---- DirectoryMount ----

TEST_CASE("Core: DirectoryMount reads, lists and reports existence")
{
	Tests::ScopedTempDirectory directory;
	WriteFile(directory.GetPath() / "Assets" / "Meshes" / "Crate.glb", "mesh");
	WriteFile(directory.GetPath() / "readme.txt", "hello");

	DirectoryMount mount(directory.GetPath());
	CHECK(mount.GetMountType() == "directory");
	CHECK(mount.IsWritable());

	CHECK(mount.Exists("readme.txt"));
	CHECK(mount.Exists("Assets"));
	CHECK(mount.Exists("Assets/Meshes/Crate.glb"));
	CHECK(mount.Exists(""));
	CHECK(mount.Exists("./Assets/../readme.txt"));
	CHECK_FALSE(mount.Exists("nope.txt"));
	CHECK_FALSE(mount.Exists("readme.txt/child"));

	const Result<Buffer> read = mount.Read("Assets/Meshes/Crate.glb");
	REQUIRE(read.HasValue());
	CHECK(std::string(reinterpret_cast<const char*>(read->Data()), read->Size()) == "mesh");

	const Result<Buffer> missing = mount.Read("nope.txt");
	REQUIRE_FALSE(missing.HasValue());
	CHECK(HasFileError(missing.GetError(), FileError::NotFound));
	const Result<Buffer> throughFile = mount.Read("readme.txt/child");
	REQUIRE_FALSE(throughFile.HasValue());
	CHECK(HasFileError(throughFile.GetError(), FileError::NotFound));
	const Result<Buffer> directoryRead = mount.Read("Assets");
	REQUIRE_FALSE(directoryRead.HasValue());
	CHECK(HasFileError(directoryRead.GetError(), FileError::InvalidPath));

	CHECK(Paths(mount.List("", false)) == std::vector<std::string>{ "Assets/", "readme.txt" });
	CHECK(
		Paths(mount.List("", true))
		== std::vector<std::string>{ "Assets/", "Assets/Meshes/", "Assets/Meshes/Crate.glb", "readme.txt" });
	// Listed paths are relative to the mount root, not to the listed directory.
	CHECK(Paths(mount.List("Assets", false)) == std::vector<std::string>{ "Assets/Meshes/" });
	CHECK(Paths(mount.List("Assets/Meshes", true)) == std::vector<std::string>{ "Assets/Meshes/Crate.glb" });
	CHECK_FALSE(mount.List("nope", false).HasValue());
}

TEST_CASE("Core: DirectoryMount is confined to its root")
{
	Tests::ScopedTempDirectory outer;
	const fs::path root = outer.GetPath() / "root";
	WriteFile(root / "inside.txt", "in");
	WriteFile(outer.GetPath() / "secret.txt", "secret");
	WriteFile(outer.GetPath() / "root-sibling" / "x.txt", "sibling");

	DirectoryMount mount(root);
	for (const char* path : { "../secret.txt", "a/../../secret.txt", "..\\secret.txt", "../root-sibling/x.txt", "..",
	                          "/etc/passwd", "\\etc\\passwd", "C:/Windows/win.ini", "C:\\secret.txt", "/" })
	{
		const Result<Buffer> read = mount.Read(path);
		CHECK_MESSAGE(!read.HasValue(), "path: ", path);
		if (!read.HasValue())
		{
			CHECK(HasFileError(read.GetError(), FileError::InvalidPath));
		}
		CHECK_FALSE(mount.Exists(path));
		CHECK_FALSE(mount.Write(path, BufferView::FromText("x")).HasValue());
		CHECK_FALSE(mount.Remove(path).HasValue());
		CHECK_FALSE(mount.List(path, false).HasValue());
	}
	// Nothing was written or deleted outside.
	CHECK(FileSystem::ReadText(outer.GetPath() / "secret.txt").ValueOr("") == "secret");
	CHECK(FileSystem::ReadText(root / "inside.txt").ValueOr("") == "in");
	CHECK_FALSE(fs::exists(outer.GetPath() / "etc"));

	// A path that goes up and comes back down is fine.
	CHECK(mount.Exists("sub/../inside.txt"));
}

TEST_CASE("Core: DirectoryMount rejects symbolic links that lead outside the root")
{
	Tests::ScopedTempDirectory outer;
	const fs::path root = outer.GetPath() / "root";
	WriteFile(root / "inside.txt", "in");
	WriteFile(outer.GetPath() / "outside" / "secret.txt", "secret");

	std::error_code error;
	fs::create_directory_symlink(outer.GetPath() / "outside", root / "link", error);
	if (error)
	{
		MESSAGE("symbolic links are not available here: ", error.message());
		return;
	}
	fs::create_symlink(root / "inside.txt", root / "alias.txt", error);
	REQUIRE_FALSE(error);

	DirectoryMount mount(root);
	CHECK_FALSE(mount.Read("link/secret.txt").HasValue());
	CHECK_FALSE(mount.Exists("link/secret.txt"));
	CHECK_FALSE(mount.Write("link/new.txt", BufferView::FromText("x")).HasValue());
	CHECK_FALSE(fs::exists(outer.GetPath() / "outside" / "new.txt"));
	CHECK_FALSE(mount.Remove("link/secret.txt").HasValue());
	CHECK(fs::exists(outer.GetPath() / "outside" / "secret.txt"));

	// A link that stays inside the root works like the file it points to.
	const Result<Buffer> viaAlias = mount.Read("alias.txt");
	REQUIRE(viaAlias.HasValue());
	CHECK(viaAlias->Size() == 2);
}

TEST_CASE("Core: DirectoryMount never writes through dangling symbolic links")
{
	// Links whose targets do not exist yet cannot be resolved, so confinement must hold without resolving them.
	Tests::ScopedTempDirectory outer;
	const fs::path root = outer.GetPath() / "root";
	const fs::path outside = outer.GetPath() / "outside";
	REQUIRE(FileSystem::CreateDirectories(root).HasValue());
	REQUIRE(FileSystem::CreateDirectories(outside).HasValue());

	std::error_code error;
	fs::create_symlink(outside / "new.txt", root / "file-link", error);
	if (error)
	{
		MESSAGE("symbolic links are not available here: ", error.message());
		return;
	}
	fs::create_directory_symlink(outside / "missing", root / "dir-link", error);
	REQUIRE_FALSE(error);

	DirectoryMount mount(root);
	const Result<Buffer> read = mount.Read("file-link");
	REQUIRE_FALSE(read.HasValue());
	CHECK(HasFileError(read.GetError(), FileError::NotFound));

	// Writing the link's own path replaces the link with a file inside the root.
	if (mount.Write("file-link", BufferView::FromText("x")).HasValue())
	{
		CHECK_FALSE(fs::is_symlink(fs::symlink_status(root / "file-link")));
		CHECK(FileSystem::ReadText(root / "file-link").ValueOr("") == "x");
	}
	CHECK_FALSE(fs::exists(outside / "new.txt"));

	CHECK_FALSE(mount.Write("dir-link/child.txt", BufferView::FromText("x")).HasValue());
	CHECK_FALSE(fs::exists(outside / "missing"));
}

TEST_CASE("Core: DirectoryMount writes create directories and replace files")
{
	Tests::ScopedTempDirectory directory;
	DirectoryMount mount(directory.GetPath() / "content"); // the root does not exist yet

	CHECK_FALSE(mount.Exists("a.txt"));
	REQUIRE(mount.Write("Scenes/Level1/start.gscene", BufferView::FromText("v1")).HasValue());
	CHECK(
		FileSystem::ReadText(directory.GetPath() / "content" / "Scenes" / "Level1" / "start.gscene").ValueOr("")
		== "v1");
	REQUIRE(mount.Write("Scenes/Level1/start.gscene", BufferView::FromText("v2")).HasValue());
	CHECK(mount.Read("Scenes/Level1/start.gscene")->Size() == 2);
	CHECK(
		FileSystem::ReadText(directory.GetPath() / "content" / "Scenes" / "Level1" / "start.gscene").ValueOr("")
		== "v2");

	CHECK_FALSE(mount.Write("", BufferView::FromText("x")).HasValue());
	CHECK_FALSE(mount.Write(".", BufferView::FromText("x")).HasValue());
	CHECK_FALSE(mount.Write("Scenes", BufferView::FromText("x")).HasValue()); // a directory
}

TEST_CASE("Core: DirectoryMount read-only mounts reject modification")
{
	Tests::ScopedTempDirectory directory;
	WriteFile(directory.GetPath() / "a.txt", "a");

	DirectoryMount mount(directory.GetPath(), false);
	CHECK_FALSE(mount.IsWritable());
	CHECK(mount.Exists("a.txt"));

	const Result<void> write = mount.Write("a.txt", BufferView::FromText("changed"));
	REQUIRE_FALSE(write.HasValue());
	CHECK(HasFileError(write.GetError(), FileError::ReadOnly));
	const Result<void> remove = mount.Remove("a.txt");
	REQUIRE_FALSE(remove.HasValue());
	CHECK(HasFileError(remove.GetError(), FileError::ReadOnly));
	CHECK(FileSystem::ReadText(directory.GetPath() / "a.txt").ValueOr("") == "a");
}

TEST_CASE("Core: DirectoryMount Remove deletes files and empty directories only")
{
	Tests::ScopedTempDirectory directory;
	WriteFile(directory.GetPath() / "file.txt", "x");
	WriteFile(directory.GetPath() / "full" / "child.txt", "x");
	REQUIRE(FileSystem::CreateDirectories(directory.GetPath() / "empty").HasValue());

	DirectoryMount mount(directory.GetPath());
	CHECK(mount.Remove("file.txt").HasValue());
	CHECK_FALSE(mount.Exists("file.txt"));
	CHECK(mount.Remove("empty").HasValue());
	CHECK_FALSE(mount.Exists("empty"));

	CHECK_FALSE(mount.Remove("full").HasValue()); // not empty
	CHECK(mount.Exists("full/child.txt"));
	const Result<void> missing = mount.Remove("nope.txt");
	REQUIRE_FALSE(missing.HasValue());
	CHECK(HasFileError(missing.GetError(), FileError::NotFound));
	CHECK_FALSE(mount.Remove("").HasValue()); // the root itself
	CHECK(fs::exists(directory.GetPath()));
}

TEST_CASE("Core: DirectoryMount Remove deletes a symbolic link, not its target")
{
	Tests::ScopedTempDirectory directory;
	WriteFile(directory.GetPath() / "target.txt", "keep");
	std::error_code error;
	fs::create_symlink(directory.GetPath() / "target.txt", directory.GetPath() / "link.txt", error);
	if (error)
	{
		MESSAGE("symbolic links are not available here: ", error.message());
		return;
	}

	DirectoryMount mount(directory.GetPath());
	REQUIRE(mount.Remove("link.txt").HasValue());
	CHECK_FALSE(fs::is_symlink(fs::symlink_status(directory.GetPath() / "link.txt")));
	CHECK(FileSystem::ReadText(directory.GetPath() / "target.txt").ValueOr("") == "keep");
}

TEST_CASE("Core: DirectoryMount accepts a root with a trailing separator")
{
	Tests::ScopedTempDirectory directory;
	WriteFile(directory.GetPath() / "a.txt", "a");
	DirectoryMount mount(directory.GetPath().string() + "/");
	CHECK(mount.Exists("a.txt"));
	CHECK_FALSE(mount.Exists("../a.txt"));
}

// ---- VFS ----

TEST_CASE("Core: VFS without mounts finds nothing")
{
	VFS vfs;
	CHECK_FALSE(vfs.Exists("a.txt"));
	CHECK_FALSE(vfs.Exists(""));
	const Result<Buffer> read = vfs.ReadBinary("a.txt");
	REQUIRE_FALSE(read.HasValue());
	CHECK(HasFileError(read.GetError(), FileError::NotFound));
	CHECK_FALSE(vfs.List("").HasValue());
	CHECK(vfs.GetMounts().empty());

	const Result<void> write = vfs.WriteText("a.txt", "x");
	REQUIRE_FALSE(write.HasValue());
	CHECK(HasFileError(write.GetError(), FileError::ReadOnly));
}

TEST_CASE("Core: VFS later mounts shadow earlier ones and lookups fall through on NotFound")
{
	VFS vfs;
	REQUIRE(vfs.Mount("", MakeMemory({ { "a.txt", "base a" }, { "b.txt", "base b" }, { "dir/c.txt", "base c" } }))
	            .HasValue());
	REQUIRE(vfs.Mount("", MakeMemory({ { "b.txt", "mod b" }, { "dir/d.txt", "mod d" } })).HasValue());

	CHECK(Text(vfs, "a.txt") == "base a");
	CHECK(Text(vfs, "b.txt") == "mod b");
	CHECK(Text(vfs, "dir/c.txt") == "base c");
	CHECK(Text(vfs, "dir/d.txt") == "mod d");
	CHECK(vfs.Exists("dir"));
	CHECK(vfs.Exists(""));
	CHECK_FALSE(vfs.Exists("e.txt"));

	const Result<Buffer> missing = vfs.ReadBinary("e.txt");
	REQUIRE_FALSE(missing.HasValue());
	CHECK(HasFileError(missing.GetError(), FileError::NotFound));
	CHECK(missing.GetError().Message.find("e.txt") != std::string::npos);
}

TEST_CASE("Core: VFS stops at an error that is not NotFound")
{
	VFS vfs;
	REQUIRE(vfs.Mount("", MakeMemory({ { "x.txt", "lower" } })).HasValue());
	const Ref<MemoryMount> upper = MakeMemory({ { "x.txt", "upper" } });
	upper->Break("x.txt");
	REQUIRE(vfs.Mount("", upper).HasValue());

	const Result<std::string> read = vfs.ReadText("x.txt");
	REQUIRE_FALSE(read.HasValue());
	CHECK(HasFileError(read.GetError(), FileError::Io)); // it does not silently serve the lower file
}

TEST_CASE("Core: VFS paths are case-sensitive, relative and normalized")
{
	VFS vfs;
	REQUIRE(vfs.Mount("", MakeMemory({ { "Assets/Crate.glb", "mesh" } })).HasValue());

	CHECK(vfs.Exists("Assets/Crate.glb"));
	CHECK_FALSE(vfs.Exists("assets/Crate.glb"));
	CHECK_FALSE(vfs.Exists("Assets/crate.glb"));

	CHECK(Text(vfs, "./Assets//Crate.glb") == "mesh");
	CHECK(Text(vfs, "Assets\\Crate.glb") == "mesh");
	CHECK(Text(vfs, "Other/../Assets/Crate.glb") == "mesh");

	for (const char* path : { "/Assets/Crate.glb", "C:/Assets/Crate.glb", "../Assets/Crate.glb", "Assets/../../x" })
	{
		CHECK_MESSAGE(!vfs.Exists(path), "path: ", path);
		const Result<Buffer> read = vfs.ReadBinary(path);
		CHECK_MESSAGE(!read.HasValue(), "path: ", path);
		if (!read.HasValue())
		{
			CHECK(HasFileError(read.GetError(), FileError::InvalidPath));
		}
		CHECK_FALSE(vfs.Write(path, BufferView::FromText("x")).HasValue());
		CHECK_FALSE(vfs.Remove(path).HasValue());
	}
	CHECK_FALSE(vfs.ReadBinary("").HasValue()); // the root is a directory
}

TEST_CASE("Core: VFS mount points select the part of the tree a mount serves")
{
	VFS vfs;
	REQUIRE(vfs.Mount("", MakeMemory({ { "Project/a.txt", "project" } })).HasValue());
	REQUIRE(vfs.Mount("Engine/Shaders", MakeMemory({ { "PBR.glsl", "pbr" }, { "sub/Lib.glsl", "lib" } })).HasValue());

	CHECK(Text(vfs, "Engine/Shaders/PBR.glsl") == "pbr");
	CHECK(Text(vfs, "Engine/Shaders/sub/Lib.glsl") == "lib");
	CHECK(Text(vfs, "Project/a.txt") == "project");
	CHECK_FALSE(vfs.Exists("PBR.glsl"));
	CHECK_FALSE(vfs.Exists("Engine/PBR.glsl"));
	CHECK_FALSE(vfs.Exists("Engine/Shadersx/PBR.glsl"));

	// Directories on the way to a mount point exist virtually.
	CHECK(vfs.Exists("Engine"));
	CHECK(vfs.Exists("Engine/Shaders"));
	CHECK(vfs.Exists("Engine/Shaders/sub"));
	CHECK_FALSE(vfs.ReadBinary("Engine").HasValue());
	CHECK_FALSE(vfs.ReadBinary("Engine/Shaders").HasValue());

	CHECK(Paths(vfs.List("", false)) == std::vector<std::string>{ "Engine/", "Project/" });
	CHECK(Paths(vfs.List("Engine", false)) == std::vector<std::string>{ "Engine/Shaders/" });
	CHECK(
		Paths(vfs.List("Engine/Shaders", false))
		== std::vector<std::string>{ "Engine/Shaders/PBR.glsl", "Engine/Shaders/sub/" });
	CHECK(
		Paths(vfs.List("", true))
		== std::vector<std::string>{ "Engine/", "Engine/Shaders/", "Engine/Shaders/PBR.glsl", "Engine/Shaders/sub/",
	                                 "Engine/Shaders/sub/Lib.glsl", "Project/", "Project/a.txt" });
	CHECK(
		Paths(vfs.List("Engine", true))
		== std::vector<std::string>{ "Engine/Shaders/", "Engine/Shaders/PBR.glsl", "Engine/Shaders/sub/",
	                                 "Engine/Shaders/sub/Lib.glsl" });
	CHECK(Paths(vfs.List("Engine/Shaders/sub", true)) == std::vector<std::string>{ "Engine/Shaders/sub/Lib.glsl" });
	CHECK_FALSE(vfs.List("Engine/Nothing").HasValue());
}

TEST_CASE("Core: VFS mount points are validated")
{
	VFS vfs;
	CHECK_FALSE(vfs.Mount("", nullptr).HasValue());
	CHECK_FALSE(vfs.Mount("/abs", MakeMemory({})).HasValue());
	CHECK_FALSE(vfs.Mount("../up", MakeMemory({})).HasValue());
	CHECK_FALSE(vfs.Mount("C:/x", MakeMemory({})).HasValue());
	CHECK(vfs.GetMounts().empty());

	REQUIRE(vfs.Mount("./Engine//Shaders/", MakeMemory({ { "a", "a" } })).HasValue()); // normalized
	REQUIRE(vfs.GetMounts().size() == 1);
	CHECK(vfs.GetMounts()[0].MountPoint == "Engine/Shaders");
	CHECK(vfs.GetMounts()[0].Type == "memory");
	CHECK_FALSE(vfs.GetMounts()[0].Writable);
}

TEST_CASE("Core: VFS merged listings let the newest mount win and are sorted")
{
	VFS vfs;
	REQUIRE(vfs.Mount("", MakeMemory({ { "z.txt", "z" }, { "m/old.txt", "old" }, { "clash", "file" } })).HasValue());
	REQUIRE(vfs.Mount("", MakeMemory({ { "a.txt", "a" }, { "m/new.txt", "new" }, { "clash/inner.txt", "dir now" } }))
	            .HasValue());

	CHECK(Paths(vfs.List("", false)) == std::vector<std::string>{ "a.txt", "clash/", "m/", "z.txt" });
	CHECK(
		Paths(vfs.List("", true))
		== std::vector<std::string>{ "a.txt", "clash/", "clash/inner.txt", "m/", "m/new.txt", "m/old.txt", "z.txt" });
	CHECK(Paths(vfs.List("m", false)) == std::vector<std::string>{ "m/new.txt", "m/old.txt" });
}

TEST_CASE("Core: VFS writes go to the newest writable mount that contains the path")
{
	VFS vfs;
	const Ref<MemoryMount> baseContent = MakeMemory({ { "Assets/a.txt", "read-only base" } });
	const Ref<MemoryMount> writableRoot = MakeMemory({}, true);
	const Ref<MemoryMount> writableUser = MakeMemory({}, true);
	const Ref<MemoryMount> readOnlyTop = MakeMemory({ { "locked.txt", "locked" } });
	REQUIRE(vfs.Mount("", baseContent).HasValue());
	REQUIRE(vfs.Mount("", writableRoot).HasValue());
	REQUIRE(vfs.Mount("User", writableUser).HasValue());
	REQUIRE(vfs.Mount("", readOnlyTop).HasValue());

	REQUIRE(vfs.WriteText("Assets/a.txt", "saved").HasValue());
	CHECK(writableRoot->Get("Assets/a.txt") == "saved");
	CHECK(baseContent->Get("Assets/a.txt") == "read-only base");
	CHECK(Text(vfs, "Assets/a.txt") == "saved"); // the writable mount shadows the base

	REQUIRE(vfs.WriteText("User/settings.json", "{}").HasValue());
	CHECK(writableUser->Get("settings.json") == "{}"); // mount-relative path
	CHECK(writableRoot->Get("User/settings.json") == "<missing>");

	CHECK_FALSE(vfs.WriteText("", "x").HasValue());
}

TEST_CASE("Core: VFS write fails when no mount is writable")
{
	VFS vfs;
	REQUIRE(vfs.Mount("", MakeMemory({ { "a.txt", "a" } })).HasValue());
	const Result<void> write = vfs.WriteText("a.txt", "b");
	REQUIRE_FALSE(write.HasValue());
	CHECK(HasFileError(write.GetError(), FileError::ReadOnly));
	CHECK(Text(vfs, "a.txt") == "a");

	REQUIRE(vfs.Mount("Other", MakeMemory({}, true)).HasValue());
	CHECK_FALSE(vfs.WriteText("a.txt", "b").HasValue()); // the writable mount does not contain this path
}

TEST_CASE("Core: VFS Remove acts on the newest mount that has the path")
{
	VFS vfs;
	const Ref<MemoryMount> lower = MakeMemory({ { "a.txt", "lower" }, { "b.txt", "b" } }, true);
	const Ref<MemoryMount> upper = MakeMemory({ { "a.txt", "upper" } }, true);
	REQUIRE(vfs.Mount("", lower).HasValue());
	REQUIRE(vfs.Mount("", upper).HasValue());

	REQUIRE(vfs.Remove("a.txt").HasValue());
	CHECK(Text(vfs, "a.txt") == "lower"); // the shadowed file shows through again
	REQUIRE(vfs.Remove("b.txt").HasValue());
	CHECK_FALSE(vfs.Exists("b.txt"));

	const Result<void> missing = vfs.Remove("zzz.txt");
	REQUIRE_FALSE(missing.HasValue());
	CHECK(HasFileError(missing.GetError(), FileError::NotFound));

	REQUIRE(vfs.Mount("", MakeMemory({ { "fixed.txt", "fixed" } })).HasValue());
	const Result<void> readOnly = vfs.Remove("fixed.txt");
	REQUIRE_FALSE(readOnly.HasValue());
	CHECK(HasFileError(readOnly.GetError(), FileError::ReadOnly));
	CHECK_FALSE(vfs.Remove("").HasValue());
}

TEST_CASE("Core: VFS Unmount and UnmountAll")
{
	VFS vfs;
	const Ref<MemoryMount> first = MakeMemory({ { "a.txt", "first" } });
	const Ref<MemoryMount> second = MakeMemory({ { "a.txt", "second" } });
	REQUIRE(vfs.Mount("", first).HasValue());
	REQUIRE(vfs.Mount("", second).HasValue());
	REQUIRE(vfs.Mount("copy", second).HasValue());
	CHECK(Text(vfs, "copy/a.txt") == "second");

	CHECK(vfs.Unmount(second)); // every attachment goes
	CHECK(Text(vfs, "a.txt") == "first");
	CHECK_FALSE(vfs.Exists("copy/a.txt"));
	CHECK_FALSE(vfs.Unmount(second));
	CHECK(vfs.GetMounts().size() == 1);

	vfs.UnmountAll();
	CHECK(vfs.GetMounts().empty());
	CHECK_FALSE(vfs.Exists("a.txt"));
}

TEST_CASE("Core: VFS over directories on disk")
{
	Tests::ScopedTempDirectory engine;
	Tests::ScopedTempDirectory project;
	WriteFile(engine.GetPath() / "Defaults" / "Material.gmat", "engine default");
	WriteFile(engine.GetPath() / "Shared.txt", "engine shared");
	WriteFile(project.GetPath() / "Shared.txt", "project shared");

	VFS vfs;
	REQUIRE(vfs.Mount("", CreateRef<DirectoryMount>(engine.GetPath(), false)).HasValue());
	REQUIRE(vfs.Mount("", CreateRef<DirectoryMount>(project.GetPath())).HasValue());

	CHECK(Text(vfs, "Shared.txt") == "project shared");
	CHECK(Text(vfs, "Defaults/Material.gmat") == "engine default");
	REQUIRE(vfs.WriteText("Scenes/Main.gscene", "{}").HasValue());
	CHECK(FileSystem::ReadText(project.GetPath() / "Scenes" / "Main.gscene").ValueOr("") == "{}");
	CHECK_FALSE(fs::exists(engine.GetPath() / "Scenes"));
	CHECK(
		Paths(vfs.List("", true))
		== std::vector<std::string>{ "Defaults/", "Defaults/Material.gmat", "Scenes/", "Scenes/Main.gscene",
	                                 "Shared.txt" });

	REQUIRE(vfs.Remove("Shared.txt").HasValue());
	CHECK(Text(vfs, "Shared.txt") == "engine shared");
	CHECK_FALSE(vfs.Remove("Defaults/Material.gmat").HasValue()); // read-only engine mount
}

TEST_CASE("Core: VFS reads are safe from many threads while mounts change")
{
	VFS vfs;
	const Ref<MemoryMount> stable = MakeMemory({ { "stable.txt", "always there" }, { "dir/file.txt", "x" } });
	REQUIRE(vfs.Mount("", stable).HasValue());

	std::atomic<bool> stop = false;
	std::atomic<int> failures = 0;
	std::atomic<int> reads = 0;
	std::vector<std::thread> readers;
	for (int i = 0; i < 4; ++i)
	{
		readers.emplace_back([&] {
			while (!stop)
			{
				if (Text(vfs, "stable.txt") != "always there" || !vfs.Exists("dir/file.txt")
				    || !vfs.List("", true).HasValue())
				{
					++failures;
				}
				++reads;
			}
		});
	}

	std::thread mounter([&] {
		for (int i = 0; i < 300; ++i)
		{
			// doctest assertions are not used here: they are not meant for concurrent use.
			const Ref<MemoryMount> transient = MakeMemory({ { "transient.txt", "t" } });
			if (!vfs.Mount("T", transient).HasValue() || !vfs.Unmount(transient))
			{
				++failures;
			}
		}
		stop = true;
	});
	mounter.join();
	for (std::thread& reader : readers)
	{
		reader.join();
	}
	CHECK(reads > 0);
	CHECK(failures == 0);
	CHECK(vfs.GetMounts().size() == 1);
}
