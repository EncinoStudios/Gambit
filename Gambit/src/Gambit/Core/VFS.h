#pragma once

#include "Gambit/Core/Base.h"
#include "Gambit/Core/Buffer.h"
#include "Gambit/Core/FileSystem.h"
#include "Gambit/Core/Result.h"

#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace Gambit
{
	// A source of files for the VFS: a directory on disk, an exported pack (T23) or a remote content server (T24).
	// All paths are relative to the mount root, already normalized (forward slashes, no "." or ".." segments, no
	// leading or trailing slash); "" is the root itself. A path that does not exist is reported with
	// FileError::NotFound, which lets the VFS fall through to the next mount; any other error stops the lookup.
	// The const methods may be called concurrently from several threads; Write and Remove need external
	// synchronization with everything else (the VFS provides none either).
	class IFileMount
	{
	public:
		virtual ~IFileMount() = default;

		// True for files and directories.
		[[nodiscard]] virtual bool Exists(std::string_view path) const = 0;
		[[nodiscard]] virtual Result<Buffer> Read(std::string_view path) const = 0;
		// The entries below `directory` with paths relative to the mount root, in any order.
		[[nodiscard]] virtual Result<std::vector<DirectoryEntry>>
		List(std::string_view directory, bool recursive) const = 0;

		[[nodiscard]] virtual bool IsWritable() const = 0;
		// Mounts that are not writable return FileError::ReadOnly.
		[[nodiscard]] virtual Result<void> Write(std::string_view path, BufferView data) = 0;
		// Removes a file or an empty directory.
		[[nodiscard]] virtual Result<void> Remove(std::string_view path) = 0;

		// Short lower-case kind for diagnostics: "directory", "pack", "remote".
		[[nodiscard]] virtual std::string_view GetMountType() const = 0;
	};

	// Description of one attached mount, for diagnostics and tools.
	struct MountInfo
	{
		std::string MountPoint;
		std::string Type;
		bool Writable = false;
	};

	// The virtual file system all runtime file access goes through (D-014). Virtual paths are relative, use forward
	// slashes and are case-sensitive (like the exported packs; DirectoryMount cannot enforce this on a
	// case-insensitive file system). Mounts attach at a mount point ("" is the root, "Engine/Shaders" a
	// subdirectory) and are searched newest first, so a later mount shadows an earlier one. Reads (Exists,
	// ReadBinary, ReadText, List) are thread safe and may run concurrently with Mount and Unmount; they use the mount
	// list as it was when they started.
	class VFS
	{
	public:
		VFS() = default;
		VFS(const VFS&) = delete;
		VFS& operator=(const VFS&) = delete;

		// Attaches `mount` at `mountPoint`, above all existing mounts.
		[[nodiscard]] Result<void> Mount(std::string_view mountPoint, Ref<IFileMount> mount);
		// Detaches every attachment of `mount`; false when it was not mounted.
		bool Unmount(const Ref<IFileMount>& mount);
		void UnmountAll();
		// The mounts in search order, oldest first.
		[[nodiscard]] std::vector<MountInfo> GetMounts() const;

		// True for files, directories and the virtual directories leading to a mount point.
		[[nodiscard]] bool Exists(std::string_view path) const;
		[[nodiscard]] Result<Buffer> ReadBinary(std::string_view path) const;
		[[nodiscard]] Result<std::string> ReadText(std::string_view path) const;

		// Entries below `directory` (children only, or the whole subtree when recursive) with full virtual paths,
		// merged over all mounts (the newest mount wins when several provide a path) and sorted by path.
		[[nodiscard]] Result<std::vector<DirectoryEntry>>
		List(std::string_view directory, bool recursive = false) const;

		// Writes go to the newest writable mount whose mount point contains `path`, so that reads see the new file.
		// ReadOnly when there is none, or when a newer read-only mount provides `path` (it would shadow the write);
		// InvalidPath for a mount point or a virtual directory leading to one (those are directories).
		[[nodiscard]] Result<void> Write(std::string_view path, BufferView data);
		[[nodiscard]] Result<void> WriteText(std::string_view path, std::string_view text);
		// Removes a file or an empty directory from the newest mount that has it; ReadOnly when that mount is not
		// writable, InvalidPath for a mount point or a virtual directory leading to one.
		[[nodiscard]] Result<void> Remove(std::string_view path);

	private:
		struct MountEntry
		{
			std::string MountPoint;
			Ref<IFileMount> Mount;
		};
		using MountList = std::vector<MountEntry>;

		[[nodiscard]] Ref<const MountList> GetSnapshot() const;

		// The mount list is immutable once published; Mount/Unmount swap in a new one. Readers copy the pointer under
		// the lock and then work lock-free, so slow I/O never blocks (or is blocked by) mount changes.
		mutable std::mutex m_Mutex;
		Ref<const MountList> m_Mounts = CreateRef<const MountList>();
	};
}
