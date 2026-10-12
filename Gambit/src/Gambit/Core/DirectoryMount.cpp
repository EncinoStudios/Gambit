#include "gbpch.h"

#include "Gambit/Core/DirectoryMount.h"

#include <system_error>

namespace Gambit
{
	DirectoryMount::DirectoryMount(const std::filesystem::path& root, bool writable)
		: m_Writable(writable)
	{
		GB_CORE_ASSERT(!root.empty(), "a DirectoryMount needs a root directory");
		std::error_code error;
		m_Root = std::filesystem::weakly_canonical(std::filesystem::absolute(root, error), error);
		if (error)
		{
			GB_CORE_WARN("DirectoryMount: cannot resolve '{}': {}", FileSystem::ToUtf8(root), error.message());
			m_Root = root.lexically_normal();
		}
		if (!m_Root.has_filename())
		{
			m_Root = m_Root.parent_path(); // "dir/" has an empty last component, which would break the prefix test
		}
	}

	Result<std::filesystem::path> DirectoryMount::Resolve(std::string_view path) const
	{
		Result<std::string> normalized = FileSystem::NormalizePath(path);
		if (!normalized)
		{
			return normalized.GetError();
		}
		if (FileSystem::IsAbsolutePath(*normalized))
		{
			return MakeFileError(
				FileError::InvalidPath, std::format("'{}' is absolute; mount paths are relative", path));
		}

		const std::filesystem::path joined = normalized->empty() ? m_Root : m_Root / FileSystem::FromUtf8(*normalized);
		std::error_code error;
		std::filesystem::path resolved = std::filesystem::weakly_canonical(joined, error);
		if (error)
		{
			// A file where a directory is expected ("file.txt/child") simply does not exist.
			const bool missing = error == std::errc::not_a_directory || error == std::errc::no_such_file_or_directory;
			return MakeFileError(
				missing ? FileError::NotFound : FileError::Io,
				std::format("cannot resolve '{}': {}", path, error.message()));
		}

		// Symbolic links are resolved by now, so a prefix test on whole path components proves confinement.
		const auto [rootEnd, resolvedEnd] =
			std::mismatch(m_Root.begin(), m_Root.end(), resolved.begin(), resolved.end());
		static_cast<void>(resolvedEnd);
		if (rootEnd != m_Root.end())
		{
			return MakeFileError(
				FileError::InvalidPath, std::format("'{}' resolves outside of the mounted directory", path));
		}
		return resolved;
	}

	Result<std::filesystem::path> DirectoryMount::ResolveEntry(std::string_view path) const
	{
		const Result<std::string> normalized = FileSystem::NormalizePath(path);
		if (!normalized)
		{
			return normalized.GetError();
		}
		const std::filesystem::path relative = FileSystem::FromUtf8(*normalized);
		Result<std::filesystem::path> parent = Resolve(FileSystem::ToUtf8(relative.parent_path()));
		if (!parent)
		{
			return parent;
		}
		return relative.has_filename() ? *parent / relative.filename() : *parent;
	}

	bool DirectoryMount::Exists(std::string_view path) const
	{
		const Result<std::filesystem::path> resolved = Resolve(path);
		return resolved && FileSystem::Exists(*resolved);
	}

	Result<Buffer> DirectoryMount::Read(std::string_view path) const
	{
		const Result<std::filesystem::path> resolved = Resolve(path);
		if (!resolved)
		{
			return resolved.GetError();
		}
		return FileSystem::ReadBinary(*resolved);
	}

	Result<std::vector<DirectoryEntry>> DirectoryMount::List(std::string_view directory, bool recursive) const
	{
		const Result<std::filesystem::path> resolved = Resolve(directory);
		if (!resolved)
		{
			return resolved.GetError();
		}
		Result<std::vector<DirectoryEntry>> listed = FileSystem::ListDirectory(*resolved, recursive);
		if (!listed)
		{
			return listed;
		}

		// ListDirectory is relative to the listed directory; the mount interface is relative to the mount root.
		const Result<std::string> prefix = FileSystem::NormalizePath(directory);
		if (prefix && !prefix->empty())
		{
			for (DirectoryEntry& entry : *listed)
			{
				entry.Path = std::format("{}/{}", *prefix, entry.Path);
			}
		}
		return listed;
	}

	Result<void> DirectoryMount::Write(std::string_view path, BufferView data)
	{
		if (!m_Writable)
		{
			return MakeFileError(FileError::ReadOnly, "the directory mount is read-only");
		}
		const Result<std::filesystem::path> resolved = Resolve(path);
		if (!resolved)
		{
			return resolved.GetError();
		}
		if (*resolved == m_Root)
		{
			return MakeFileError(FileError::InvalidPath, "cannot write to the mount root");
		}

		if (const Result<void> created = FileSystem::CreateDirectories(resolved->parent_path()); !created)
		{
			return created;
		}
		return FileSystem::WriteBinary(*resolved, data);
	}

	Result<void> DirectoryMount::Remove(std::string_view path)
	{
		if (!m_Writable)
		{
			return MakeFileError(FileError::ReadOnly, "the directory mount is read-only");
		}
		const Result<std::filesystem::path> resolved = ResolveEntry(path);
		if (!resolved)
		{
			return resolved.GetError();
		}
		if (*resolved == m_Root)
		{
			return MakeFileError(FileError::InvalidPath, "cannot remove the mount root");
		}

		std::error_code error;
		if (!std::filesystem::exists(std::filesystem::symlink_status(*resolved, error)))
		{
			return MakeFileError(FileError::NotFound, std::format("'{}' does not exist", path));
		}
		std::filesystem::remove(*resolved, error);
		if (error)
		{
			return MakeFileError(FileError::Io, std::format("cannot remove '{}': {}", path, error.message()));
		}
		return {};
	}
}
