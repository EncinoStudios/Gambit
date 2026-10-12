#include "gbpch.h"

#include "Gambit/Core/FileSystem.h"

#include "Gambit/Core/UUID.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <new>
#include <system_error>

#if defined(GB_PLATFORM_WINDOWS)
#	include <windows.h>
#elif defined(GB_PLATFORM_MACOS)
#	include <mach-o/dyld.h>
#endif

namespace Gambit::FileSystem
{
	namespace
	{
		constexpr std::string_view Utf8Bom = "\xEF\xBB\xBF";

		bool IsSeparator(char character) noexcept
		{
			return character == '/' || character == '\\';
		}

		// Name of the temporary file a WriteBinary to `path` goes through. Random, so concurrent writers to the same
		// file never share one.
		std::filesystem::path MakeTemporaryPath(const std::filesystem::path& path)
		{
			std::filesystem::path temporary = path;
			temporary += std::format(".{:016x}.tmp", UUID::Generate().GetValue());
			return temporary;
		}
	}

	Result<Buffer> ReadBinary(const std::filesystem::path& path)
	{
		std::error_code error;
		const std::filesystem::file_status status = std::filesystem::status(path, error);
		if (!std::filesystem::exists(status))
		{
			return MakeFileError(FileError::NotFound, std::format("'{}' does not exist", ToUtf8(path)));
		}
		if (std::filesystem::is_directory(status))
		{
			return MakeFileError(FileError::InvalidPath, std::format("'{}' is a directory", ToUtf8(path)));
		}

		const std::uintmax_t size = std::filesystem::file_size(path, error);
		if (error)
		{
			return MakeFileError(
				FileError::Io, std::format("cannot get the size of '{}': {}", ToUtf8(path), error.message()));
		}

		std::ifstream stream(path, std::ios::binary);
		if (!stream)
		{
			return MakeFileError(FileError::Io, std::format("cannot open '{}' for reading", ToUtf8(path)));
		}

		Buffer buffer;
		try
		{
			buffer.Allocate(static_cast<size_t>(size));
		}
		catch (const std::bad_alloc&)
		{
			return MakeFileError(
				FileError::Io, std::format("not enough memory to read '{}' ({} bytes)", ToUtf8(path), size));
		}

		stream.read(reinterpret_cast<char*>(buffer.Data()), static_cast<std::streamsize>(buffer.Size()));
		if (static_cast<size_t>(stream.gcount()) != buffer.Size())
		{
			return MakeFileError(FileError::Io, std::format("failed to read all of '{}'", ToUtf8(path)));
		}
		return buffer;
	}

	Result<std::string> ReadText(const std::filesystem::path& path)
	{
		Result<Buffer> buffer = ReadBinary(path);
		if (!buffer)
		{
			return buffer.GetError();
		}

		return BytesToText(buffer->View());
	}

	std::string BytesToText(BufferView bytes)
	{
		std::string_view text(reinterpret_cast<const char*>(bytes.Data()), bytes.Size());
		if (text.starts_with(Utf8Bom))
		{
			text.remove_prefix(Utf8Bom.size());
		}
		return std::string(text);
	}

	Result<void> WriteBinary(const std::filesystem::path& path, BufferView data)
	{
		if (!path.has_filename())
		{
			return MakeFileError(FileError::InvalidPath, std::format("'{}' does not name a file", ToUtf8(path)));
		}

		const std::filesystem::path temporary = MakeTemporaryPath(path);
		{
			std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
			if (!stream)
			{
				return MakeFileError(FileError::Io, std::format("cannot create '{}' for writing", ToUtf8(temporary)));
			}
			stream.write(reinterpret_cast<const char*>(data.Data()), static_cast<std::streamsize>(data.Size()));
			stream.flush();
			if (!stream)
			{
				stream.close();
				std::error_code ignored;
				std::filesystem::remove(temporary, ignored);
				return MakeFileError(FileError::Io, std::format("failed to write '{}'", ToUtf8(temporary)));
			}
		}

		std::error_code error;
		std::filesystem::rename(temporary, path, error);
		if (error)
		{
			std::error_code ignored;
			std::filesystem::remove(temporary, ignored);
			return MakeFileError(FileError::Io, std::format("cannot replace '{}': {}", ToUtf8(path), error.message()));
		}
		return {};
	}

	Result<void> WriteText(const std::filesystem::path& path, std::string_view text)
	{
		return WriteBinary(path, BufferView::FromText(text));
	}

	bool Exists(const std::filesystem::path& path)
	{
		std::error_code error;
		return std::filesystem::exists(path, error);
	}

	Result<void> CreateDirectories(const std::filesystem::path& path)
	{
		std::error_code error;
		const std::filesystem::file_status status = std::filesystem::status(path, error);
		if (std::filesystem::exists(status))
		{
			if (std::filesystem::is_directory(status))
			{
				return {};
			}
			return MakeFileError(
				FileError::InvalidPath, std::format("'{}' exists and is not a directory", ToUtf8(path)));
		}

		std::filesystem::create_directories(path, error);
		if (error)
		{
			return MakeFileError(
				FileError::Io, std::format("cannot create directory '{}': {}", ToUtf8(path), error.message()));
		}
		return {};
	}

	Result<std::vector<DirectoryEntry>> ListDirectory(const std::filesystem::path& path, bool recursive)
	{
		std::error_code error;
		const std::filesystem::file_status status = std::filesystem::status(path, error);
		if (!std::filesystem::exists(status))
		{
			return MakeFileError(FileError::NotFound, std::format("'{}' does not exist", ToUtf8(path)));
		}
		if (!std::filesystem::is_directory(status))
		{
			return MakeFileError(FileError::InvalidPath, std::format("'{}' is not a directory", ToUtf8(path)));
		}

		std::vector<DirectoryEntry> entries;
		const auto collect = [&](auto iterator) -> bool {
			for (const auto end = decltype(iterator){}; iterator != end; iterator.increment(error))
			{
				if (error)
				{
					return false;
				}
				const std::filesystem::directory_entry& entry = *iterator;
				std::error_code kindError;
				const bool isDirectory = entry.is_directory(kindError);
				const bool isFile = entry.is_regular_file(kindError);
				if (kindError || (!isDirectory && !isFile))
				{
					continue; // sockets, broken links and the like are not content
				}
				entries.push_back({ ToUtf8(entry.path().lexically_relative(path)), isDirectory });
			}
			return !error;
		};

		const bool complete = recursive ? collect(std::filesystem::recursive_directory_iterator(path, error))
										: collect(std::filesystem::directory_iterator(path, error));
		if (!complete || error)
		{
			return MakeFileError(FileError::Io, std::format("cannot list '{}': {}", ToUtf8(path), error.message()));
		}

		std::ranges::sort(entries, [](const DirectoryEntry& a, const DirectoryEntry& b) { return a.Path < b.Path; });
		return entries;
	}

	Result<std::string> NormalizePath(std::string_view path)
	{
		std::string root;
		std::string_view rest = path;
		if (path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':')
		{
			rest = path.substr(2);
			if (!rest.empty() && !IsSeparator(rest.front()))
			{
				return MakeFileError(
					FileError::InvalidPath, std::format("drive-relative path '{}' is not supported", path));
			}
			root = std::format("{}:/", path[0]);
		}
		else if (!path.empty() && IsSeparator(path.front()))
		{
			root = "/";
		}

		std::vector<std::string_view> segments;
		while (!rest.empty())
		{
			const size_t separator = rest.find_first_of("/\\");
			const std::string_view segment = rest.substr(0, separator);
			rest = separator == std::string_view::npos ? std::string_view{} : rest.substr(separator + 1);

			if (segment.empty() || segment == ".")
			{
				continue;
			}
			if (segment == "..")
			{
				if (segments.empty())
				{
					return MakeFileError(FileError::InvalidPath, std::format("'{}' escapes its root", path));
				}
				segments.pop_back();
				continue;
			}
			if (std::ranges::any_of(segment, [](char character) {
					return static_cast<unsigned char>(character) < 0x20 || character == ':';
				}))
			{
				return MakeFileError(
					FileError::InvalidPath, std::format("'{}' contains a control character or ':'", path));
			}
			segments.push_back(segment);
		}

		std::string normalized = std::move(root);
		for (size_t index = 0; index < segments.size(); ++index)
		{
			if (index > 0)
			{
				normalized += '/';
			}
			normalized += segments[index];
		}
		return normalized;
	}

	bool IsAbsolutePath(std::string_view path) noexcept
	{
		if (path.empty())
		{
			return false;
		}
		return IsSeparator(path.front())
			|| (path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':');
	}

	std::string ToUtf8(const std::filesystem::path& path)
	{
		const std::u8string text = path.generic_u8string();
		return std::string(text.begin(), text.end());
	}

	std::filesystem::path FromUtf8(std::string_view text)
	{
		return std::filesystem::path(std::u8string(text.begin(), text.end()));
	}

	Result<std::filesystem::path> GetExecutableDirectory()
	{
#if defined(GB_PLATFORM_WINDOWS)
		std::wstring buffer(MAX_PATH, L'\0');
		for (;;)
		{
			const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
			if (length == 0)
			{
				return MakeFileError(FileError::Io, "GetModuleFileNameW failed");
			}
			if (length < buffer.size())
			{
				buffer.resize(length);
				break;
			}
			buffer.resize(buffer.size() * 2);
		}
		return std::filesystem::path(buffer).parent_path();
#elif defined(GB_PLATFORM_MACOS)
		uint32_t size = 0;
		_NSGetExecutablePath(nullptr, &size); // fails and reports the required size
		std::string buffer(size, '\0');
		if (_NSGetExecutablePath(buffer.data(), &size) != 0)
		{
			return MakeFileError(FileError::Io, "_NSGetExecutablePath failed");
		}
		buffer.resize(std::char_traits<char>::length(buffer.c_str()));
		std::error_code error;
		const std::filesystem::path executable = std::filesystem::weakly_canonical(buffer, error);
		return (error ? std::filesystem::path(buffer) : executable).parent_path();
#else
		std::error_code error;
		const std::filesystem::path executable = std::filesystem::read_symlink("/proc/self/exe", error);
		if (error)
		{
			return MakeFileError(FileError::Io, std::format("cannot resolve /proc/self/exe: {}", error.message()));
		}
		return executable.parent_path();
#endif
	}
}
