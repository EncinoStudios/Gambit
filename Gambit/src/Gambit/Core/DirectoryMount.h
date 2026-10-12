#pragma once

#include "Gambit/Core/VFS.h"

#include <filesystem>

namespace Gambit
{
	// Mounts a directory of the native file system. Every access is confined to the root: ".." segments, absolute
	// paths and symbolic links that lead outside of it are rejected. Case sensitivity is whatever the host file
	// system provides.
	class DirectoryMount final : public IFileMount
	{
	public:
		// `root` need not exist yet (a writable mount creates directories on demand). A read-only mount rejects
		// Write and Remove.
		explicit DirectoryMount(const std::filesystem::path& root, bool writable = true);

		[[nodiscard]] const std::filesystem::path& GetRoot() const noexcept { return m_Root; }

		[[nodiscard]] bool Exists(std::string_view path) const override;
		[[nodiscard]] Result<Buffer> Read(std::string_view path) const override;
		[[nodiscard]] Result<std::vector<DirectoryEntry>>
		List(std::string_view directory, bool recursive) const override;
		[[nodiscard]] bool IsWritable() const override { return m_Writable; }
		[[nodiscard]] Result<void> Write(std::string_view path, BufferView data) override;
		[[nodiscard]] Result<void> Remove(std::string_view path) override;
		[[nodiscard]] std::string_view GetMountType() const override { return "directory"; }

	private:
		// The absolute native path of a mount-relative path, symbolic links resolved, or why it is not allowed.
		[[nodiscard]] Result<std::filesystem::path> Resolve(std::string_view path) const;
		// Like Resolve, but a symbolic link as the last segment is kept (Remove deletes the link, not its target).
		[[nodiscard]] Result<std::filesystem::path> ResolveEntry(std::string_view path) const;

		std::filesystem::path m_Root; // absolute, canonical, no trailing separator
		bool m_Writable;
	};
}
