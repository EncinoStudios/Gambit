#include "gbpch.h"

#include "Gambit/Core/VFS.h"

#include <map>

namespace Gambit
{
	namespace
	{
		// Normalizes a virtual path and rejects absolute ones; "" (the root) is valid.
		Result<std::string> NormalizeVirtualPath(std::string_view path)
		{
			Result<std::string> normalized = FileSystem::NormalizePath(path);
			if (!normalized)
			{
				return normalized;
			}
			if (FileSystem::IsAbsolutePath(*normalized))
			{
				return MakeFileError(
					FileError::InvalidPath, std::format("'{}' is absolute; virtual paths are relative", path));
			}
			return normalized;
		}

		std::string Join(std::string_view directory, std::string_view name)
		{
			if (directory.empty())
			{
				return std::string(name);
			}
			if (name.empty())
			{
				return std::string(directory);
			}
			return std::format("{}/{}", directory, name);
		}

		// Whether `path` lies at or below `mountPoint`; `relative` receives the part below it.
		bool IsWithin(std::string_view mountPoint, std::string_view path, std::string_view& relative)
		{
			if (mountPoint.empty())
			{
				relative = path;
				return true;
			}
			if (path == mountPoint)
			{
				relative = {};
				return true;
			}
			if (path.size() > mountPoint.size() && path.starts_with(mountPoint) && path[mountPoint.size()] == '/')
			{
				relative = path.substr(mountPoint.size() + 1);
				return true;
			}
			return false;
		}

		// Whether `mountPoint` lies strictly below the directory `path`; `relative` receives the part below it.
		bool IsStrictlyBelow(std::string_view path, std::string_view mountPoint, std::string_view& relative)
		{
			if (mountPoint.empty() || mountPoint == path)
			{
				return false;
			}
			return IsWithin(path, mountPoint, relative);
		}

		Error NotFoundError(std::string_view path)
		{
			return MakeFileError(FileError::NotFound, std::format("'{}' was not found in any mount", path));
		}

		// A mount point, or a virtual directory leading to one, is a directory of the virtual tree whatever the mounts
		// below it contain. Writing or removing it in some mount would leave a file and a directory of the same name
		// (one shadowing the other), so such paths are rejected outright.
		template<typename MountList>
		Result<void> CheckNotMountDirectory(const MountList& mounts, std::string_view path)
		{
			for (const auto& mount : mounts)
			{
				std::string_view relative;
				if (mount.MountPoint == path || IsStrictlyBelow(path, mount.MountPoint, relative))
				{
					return MakeFileError(
						FileError::InvalidPath,
						std::format(
							"'{}' is a directory of the virtual tree (mount point '{}')", path, mount.MountPoint));
				}
			}
			return {};
		}
	}

	Result<void> VFS::Mount(std::string_view mountPoint, Ref<IFileMount> mount)
	{
		if (mount == nullptr)
		{
			return MakeFileError(FileError::InvalidPath, "cannot mount a null file mount");
		}
		Result<std::string> normalized = NormalizeVirtualPath(mountPoint);
		if (!normalized)
		{
			return normalized.GetError();
		}

		const std::scoped_lock lock(m_Mutex);
		auto updated = CreateRef<MountList>(*m_Mounts);
		updated->push_back({ std::move(*normalized), std::move(mount) });
		m_Mounts = std::move(updated);
		return {};
	}

	bool VFS::Unmount(const Ref<IFileMount>& mount)
	{
		const std::scoped_lock lock(m_Mutex);
		auto updated = CreateRef<MountList>();
		for (const MountEntry& entry : *m_Mounts)
		{
			if (entry.Mount != mount)
			{
				updated->push_back(entry);
			}
		}
		const bool removed = updated->size() != m_Mounts->size();
		m_Mounts = std::move(updated);
		return removed;
	}

	void VFS::UnmountAll()
	{
		const std::scoped_lock lock(m_Mutex);
		m_Mounts = CreateRef<const MountList>();
	}

	std::vector<MountInfo> VFS::GetMounts() const
	{
		const Ref<const MountList> mounts = GetSnapshot(); // keeps the list alive for the loop
		std::vector<MountInfo> infos;
		for (const MountEntry& entry : *mounts)
		{
			infos.push_back({ entry.MountPoint, std::string(entry.Mount->GetMountType()), entry.Mount->IsWritable() });
		}
		return infos;
	}

	Ref<const VFS::MountList> VFS::GetSnapshot() const
	{
		const std::scoped_lock lock(m_Mutex);
		return m_Mounts;
	}

	bool VFS::Exists(std::string_view path) const
	{
		const Result<std::string> normalized = NormalizeVirtualPath(path);
		if (!normalized)
		{
			return false;
		}

		const Ref<const MountList> mounts = GetSnapshot();
		if (normalized->empty())
		{
			return !mounts->empty();
		}
		for (auto entry = mounts->rbegin(); entry != mounts->rend(); ++entry)
		{
			std::string_view relative;
			if (IsWithin(entry->MountPoint, *normalized, relative) && entry->Mount->Exists(relative))
			{
				return true;
			}
			if (IsStrictlyBelow(*normalized, entry->MountPoint, relative))
			{
				return true; // a virtual directory on the way to a mount point
			}
		}
		return false;
	}

	Result<Buffer> VFS::ReadBinary(std::string_view path) const
	{
		const Result<std::string> normalized = NormalizeVirtualPath(path);
		if (!normalized)
		{
			return normalized.GetError();
		}
		if (normalized->empty())
		{
			return MakeFileError(FileError::InvalidPath, "the virtual root is a directory");
		}

		const Ref<const MountList> mounts = GetSnapshot();
		for (auto entry = mounts->rbegin(); entry != mounts->rend(); ++entry)
		{
			std::string_view relative;
			if (!IsWithin(entry->MountPoint, *normalized, relative))
			{
				continue;
			}
			Result<Buffer> result = entry->Mount->Read(relative);
			if (result || !HasFileError(result.GetError(), FileError::NotFound))
			{
				return result;
			}
		}
		return NotFoundError(*normalized);
	}

	Result<std::string> VFS::ReadText(std::string_view path) const
	{
		const Result<Buffer> buffer = ReadBinary(path);
		if (!buffer)
		{
			return buffer.GetError();
		}
		return FileSystem::BytesToText(buffer->View());
	}

	Result<std::vector<DirectoryEntry>> VFS::List(std::string_view directory, bool recursive) const
	{
		const Result<std::string> normalized = NormalizeVirtualPath(directory);
		if (!normalized)
		{
			return normalized.GetError();
		}

		// Oldest mount first so that newer mounts overwrite what they shadow.
		std::map<std::string, DirectoryEntry> merged;
		const auto add = [&merged](std::string path, bool isDirectory) {
			DirectoryEntry entry{ path, isDirectory };
			merged.insert_or_assign(std::move(path), std::move(entry));
		};

		const Ref<const MountList> mounts = GetSnapshot(); // keeps the list alive for the loop
		bool found = false;
		for (const MountEntry& entry : *mounts)
		{
			std::string_view relative;
			if (IsWithin(entry.MountPoint, *normalized, relative))
			{
				const Result<std::vector<DirectoryEntry>> listed = entry.Mount->List(relative, recursive);
				if (!listed)
				{
					if (HasFileError(listed.GetError(), FileError::NotFound))
					{
						continue;
					}
					return listed.GetError();
				}
				found = true;
				for (const DirectoryEntry& item : *listed)
				{
					add(Join(entry.MountPoint, item.Path), item.IsDirectory);
				}
			}
			else if (IsStrictlyBelow(*normalized, entry.MountPoint, relative))
			{
				// The mount point lies below the listed directory: its parent directories exist virtually.
				found = true;
				std::string current = *normalized;
				while (!relative.empty())
				{
					const size_t separator = relative.find('/');
					current = Join(current, relative.substr(0, separator));
					add(current, true);
					if (!recursive)
					{
						break;
					}
					relative =
						separator == std::string_view::npos ? std::string_view{} : relative.substr(separator + 1);
				}
				if (recursive)
				{
					const Result<std::vector<DirectoryEntry>> listed = entry.Mount->List("", true);
					if (!listed)
					{
						if (HasFileError(listed.GetError(), FileError::NotFound))
						{
							continue;
						}
						return listed.GetError();
					}
					for (const DirectoryEntry& item : *listed)
					{
						add(Join(entry.MountPoint, item.Path), item.IsDirectory);
					}
				}
			}
		}

		if (!found)
		{
			return NotFoundError(*normalized);
		}
		std::vector<DirectoryEntry> entries;
		entries.reserve(merged.size());
		for (auto& [path, entry] : merged)
		{
			entries.push_back(std::move(entry));
		}
		return entries;
	}

	Result<void> VFS::Write(std::string_view path, BufferView data)
	{
		const Result<std::string> normalized = NormalizeVirtualPath(path);
		if (!normalized)
		{
			return normalized.GetError();
		}
		if (normalized->empty())
		{
			return MakeFileError(FileError::InvalidPath, "cannot write to the virtual root");
		}

		const Ref<const MountList> mounts = GetSnapshot();
		if (Result<void> directory = CheckNotMountDirectory(*mounts, *normalized); !directory)
		{
			return directory;
		}
		for (auto entry = mounts->rbegin(); entry != mounts->rend(); ++entry)
		{
			std::string_view relative;
			if (!IsWithin(entry->MountPoint, *normalized, relative))
			{
				continue;
			}
			if (entry->Mount->IsWritable())
			{
				return entry->Mount->Write(relative, data);
			}
			if (entry->Mount->Exists(relative))
			{
				// Writing to an older mount would succeed, but this mount would keep serving its own version.
				return MakeFileError(
					FileError::ReadOnly,
					std::format(
						"'{}' is provided by a newer read-only mount, which would shadow the write", *normalized));
			}
		}
		return MakeFileError(FileError::ReadOnly, std::format("no writable mount contains '{}'", *normalized));
	}

	Result<void> VFS::WriteText(std::string_view path, std::string_view text)
	{
		return Write(path, BufferView::FromText(text));
	}

	Result<void> VFS::Remove(std::string_view path)
	{
		const Result<std::string> normalized = NormalizeVirtualPath(path);
		if (!normalized)
		{
			return normalized.GetError();
		}
		if (normalized->empty())
		{
			return MakeFileError(FileError::InvalidPath, "cannot remove the virtual root");
		}

		const Ref<const MountList> mounts = GetSnapshot();
		if (Result<void> directory = CheckNotMountDirectory(*mounts, *normalized); !directory)
		{
			return directory;
		}
		for (auto entry = mounts->rbegin(); entry != mounts->rend(); ++entry)
		{
			std::string_view relative;
			if (!IsWithin(entry->MountPoint, *normalized, relative) || !entry->Mount->Exists(relative))
			{
				continue;
			}
			if (!entry->Mount->IsWritable())
			{
				return MakeFileError(
					FileError::ReadOnly, std::format("'{}' is provided by a read-only mount", *normalized));
			}
			return entry->Mount->Remove(relative);
		}
		return NotFoundError(*normalized);
	}
}
