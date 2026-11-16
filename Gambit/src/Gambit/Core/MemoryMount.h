#pragma once

#include "Gambit/Core/VFS.h"

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace Gambit
{
	// Files held in memory, mountable in a VFS (D-014): content that tools generate on the fly and tests that need a
	// project without touching the disk. Directories exist implicitly: a directory is every path prefix of a file, so
	// removing the last file of a directory removes the directory too. Writable unless created read-only. Like every
	// mount, the const methods may run concurrently; Write and Remove need external synchronization.
	class MemoryMount final : public IFileMount
	{
	public:
		explicit MemoryMount(bool writable = true)
			: m_Writable(writable)
		{}

		// Adds or replaces a file whatever the mount's writability (to fill a read-only mount). Fails, changing
		// nothing, for the root, a directory and a path below a file.
		[[nodiscard]] Result<void> AddFile(std::string_view path, BufferView data);
		[[nodiscard]] Result<void> AddTextFile(std::string_view path, std::string_view text)
		{
			return AddFile(path, BufferView::FromText(text));
		}

		[[nodiscard]] bool Exists(std::string_view path) const override;
		[[nodiscard]] Result<Buffer> Read(std::string_view path) const override;
		[[nodiscard]] Result<std::vector<DirectoryEntry>>
		List(std::string_view directory, bool recursive) const override;
		[[nodiscard]] bool IsWritable() const override { return m_Writable; }
		[[nodiscard]] Result<void> Write(std::string_view path, BufferView data) override;
		// Removes a file; a directory (it holds files, or it would not exist) is refused as not empty.
		[[nodiscard]] Result<void> Remove(std::string_view path) override;
		[[nodiscard]] std::string_view GetMountType() const override { return "memory"; }

	private:
		[[nodiscard]] bool IsDirectory(std::string_view path) const;

		std::map<std::string, Buffer, std::less<>> m_Files; // by path
		bool m_Writable;
	};
}
