#pragma once

#include "Gambit/Core/Buffer.h"
#include "Gambit/Core/Result.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace Gambit
{
	// Error::Code values shared by FileSystem, the VFS and its mounts, so that callers (the VFS itself falls through
	// to the next mount on NotFound only) can tell a missing file from a failing one.
	enum class FileError : int
	{
		Unspecified = 0,
		NotFound,        // the file or directory does not exist
		InvalidPath,     // malformed, escaping the root, or of the wrong kind (a directory where a file was expected)
		ReadOnly,        // the operation needs a writable location
		Io,              // the operating system or the storage failed
		PermissionDenied // the operating system refused access (file or directory permissions)
	};

	[[nodiscard]] inline Error MakeFileError(FileError code, std::string message)
	{
		return Error(std::move(message), static_cast<int>(code));
	}

	[[nodiscard]] inline bool HasFileError(const Error& error, FileError code) noexcept
	{
		return error.Code == static_cast<int>(code);
	}

	// One entry of a directory listing. Path uses forward slashes and is relative to the listed directory (for
	// FileSystem::ListDirectory) or to the mount / virtual root (for IFileMount::List and VFS::List).
	struct DirectoryEntry
	{
		std::string Path;
		bool IsDirectory = false;
	};

	// Native file system access. Fallible functions return a Result and never throw; paths are native
	// std::filesystem paths. For engine content use the VFS (D-014), which is built on top of this.
	namespace FileSystem
	{
		// The FileError for an operating system error: NotFound for a missing file or directory (or a file where a
		// directory is expected), PermissionDenied when access was refused, ReadOnly for a read-only file system and
		// Io for everything else. Every FileSystem and DirectoryMount failure is classified this way, so a refused
		// read never passes for a missing file.
		[[nodiscard]] FileError ClassifyError(const std::error_code& error) noexcept;

		// Reads a whole file. NotFound when it does not exist, InvalidPath for a directory, PermissionDenied when it
		// may not be read.
		[[nodiscard]] Result<Buffer> ReadBinary(const std::filesystem::path& path);

		// Reads a whole file as text. A leading UTF-8 byte order mark is dropped; line endings are left alone.
		[[nodiscard]] Result<std::string> ReadText(const std::filesystem::path& path);

		// The text of already loaded bytes, with the same treatment as ReadText (byte order mark dropped).
		[[nodiscard]] std::string BytesToText(BufferView bytes);

		// Replaces the file atomically: the data is written to a temporary file in the same directory, which is then
		// renamed over `path`, so a reader (or a crash) never sees a half-written file. The parent directory must
		// exist (see CreateDirectories). The data is not fsync'ed.
		[[nodiscard]] Result<void> WriteBinary(const std::filesystem::path& path, BufferView data);
		[[nodiscard]] Result<void> WriteText(const std::filesystem::path& path, std::string_view text);

		[[nodiscard]] bool Exists(const std::filesystem::path& path);

		// Creates the directory and its missing parents; succeeds when it already exists.
		[[nodiscard]] Result<void> CreateDirectories(const std::filesystem::path& path);

		// The files and directories below `path` (recursively when asked; symbolic links to directories are listed but
		// not followed), sorted by path so that the order is the same on every platform and file system.
		[[nodiscard]] Result<std::vector<DirectoryEntry>>
		ListDirectory(const std::filesystem::path& path, bool recursive);

		// Lexically normalizes a virtual or relative path: backslashes become slashes (on every platform, so content
		// behaves the same everywhere), empty and "." segments vanish, ".." removes the segment before it, and there is
		// no trailing slash. The result of a relative path is relative and may be empty (the root). A path starting
		// with '/' or a drive ("C:/") stays absolute. Fails with InvalidPath when ".." would climb above the first
		// segment (the "root" of a relative path) or above the absolute root, or when a segment contains a NUL, another
		// control character or ':' (alternate data streams, drive tricks). Names Windows cannot store are rejected on
		// every platform, so content stays portable: segments ending in '.' or ' ' and the reserved device names
		// (CON, PRN, AUX, NUL, COM0-9, LPT0-9 and COM/LPT with a superscript 1-3; any case, with or without an
		// extension, e.g. "nul.txt").
		[[nodiscard]] Result<std::string> NormalizePath(std::string_view path);

		// Whether `segment` (one path component) is a reserved Windows device name in the sense of NormalizePath.
		[[nodiscard]] bool IsReservedDeviceName(std::string_view segment) noexcept;

		// True for paths NormalizePath keeps absolute ("/x", "C:/x").
		[[nodiscard]] bool IsAbsolutePath(std::string_view path) noexcept;

		// UTF-8 conversions for virtual paths: path -> string uses forward slashes on every platform.
		[[nodiscard]] std::string ToUtf8(const std::filesystem::path& path);
		[[nodiscard]] std::filesystem::path FromUtf8(std::string_view text);

		// The directory containing the running executable.
		[[nodiscard]] Result<std::filesystem::path> GetExecutableDirectory();
	}
}
