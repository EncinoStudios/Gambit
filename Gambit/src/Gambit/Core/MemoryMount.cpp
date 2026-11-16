#include "gbpch.h"

#include "Gambit/Core/MemoryMount.h"

#include <set>

namespace Gambit
{
	bool MemoryMount::IsDirectory(std::string_view path) const
	{
		if (path.empty())
		{
			return true;
		}
		// The first file at or after "<path>/" tells whether any file lies below the path.
		const std::string prefix = std::string(path) + "/";
		const auto found = m_Files.lower_bound(prefix);
		return found != m_Files.end() && found->first.starts_with(prefix);
	}

	Result<void> MemoryMount::AddFile(std::string_view path, BufferView data)
	{
		if (path.empty() || IsDirectory(path))
		{
			return MakeFileError(FileError::InvalidPath, std::format("'{}' is a directory", path));
		}
		for (size_t slash = path.find('/'); slash != std::string_view::npos; slash = path.find('/', slash + 1))
		{
			if (m_Files.contains(path.substr(0, slash)))
			{
				return MakeFileError(
					FileError::InvalidPath, std::format("'{}' is a file, not a directory", path.substr(0, slash)));
			}
		}
		m_Files.insert_or_assign(std::string(path), Buffer::Copy(data));
		return {};
	}

	bool MemoryMount::Exists(std::string_view path) const
	{
		return m_Files.contains(path) || IsDirectory(path);
	}

	Result<Buffer> MemoryMount::Read(std::string_view path) const
	{
		if (const auto found = m_Files.find(path); found != m_Files.end())
		{
			return Buffer::Copy(found->second.View());
		}
		if (IsDirectory(path))
		{
			return MakeFileError(FileError::InvalidPath, std::format("'{}' is a directory", path));
		}
		return MakeFileError(FileError::NotFound, std::format("'{}' does not exist", path));
	}

	Result<std::vector<DirectoryEntry>> MemoryMount::List(std::string_view directory, bool recursive) const
	{
		if (m_Files.contains(directory))
		{
			return MakeFileError(FileError::InvalidPath, std::format("'{}' is a file, not a directory", directory));
		}
		if (!IsDirectory(directory))
		{
			return MakeFileError(FileError::NotFound, std::format("'{}' does not exist", directory));
		}

		const std::string prefix = directory.empty() ? std::string() : std::string(directory) + "/";
		std::set<std::string> directories;
		std::vector<DirectoryEntry> entries;
		for (auto file = m_Files.lower_bound(prefix); file != m_Files.end() && file->first.starts_with(prefix); ++file)
		{
			const std::string_view path = file->first;
			// Every directory between the listed one and the file, or only the first one when not recursive.
			for (size_t slash = path.find('/', prefix.size()); slash != std::string_view::npos;
			     slash = path.find('/', slash + 1))
			{
				const std::string_view below = path.substr(0, slash);
				if (directories.insert(std::string(below)).second)
				{
					entries.push_back(DirectoryEntry{ std::string(below), true });
				}
				if (!recursive)
				{
					break;
				}
			}
			if (recursive || path.find('/', prefix.size()) == std::string_view::npos)
			{
				entries.push_back(DirectoryEntry{ std::string(path), false });
			}
		}
		return entries;
	}

	Result<void> MemoryMount::Write(std::string_view path, BufferView data)
	{
		if (!m_Writable)
		{
			return MakeFileError(FileError::ReadOnly, std::format("cannot write '{}': the mount is read-only", path));
		}
		return AddFile(path, data);
	}

	Result<void> MemoryMount::Remove(std::string_view path)
	{
		if (!m_Writable)
		{
			return MakeFileError(FileError::ReadOnly, std::format("cannot remove '{}': the mount is read-only", path));
		}
		if (const auto found = m_Files.find(path); found != m_Files.end())
		{
			m_Files.erase(found);
			return {};
		}
		if (IsDirectory(path))
		{
			return MakeFileError(FileError::Io, std::format("cannot remove '{}': the directory is not empty", path));
		}
		return MakeFileError(FileError::NotFound, std::format("'{}' does not exist", path));
	}
}
