#pragma once

#include "TestSupport.h"

#include "Gambit/Core/DirectoryMount.h"
#include "Gambit/Core/VFS.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace Gambit::Tests
{
	// A fresh temporary directory that tests fill with project files (removed with its contents on destruction) and a
	// VFS whose root is that directory, so tests can use virtual paths such as "Assets/Crate.png".
	class TempDirectory
	{
	public:
		TempDirectory()
			: m_Vfs(CreateRef<VFS>())
		{
			const Result<void> mounted = m_Vfs->Mount("", CreateRef<DirectoryMount>(m_Directory.GetPath()));
			REQUIRE(mounted.HasValue());
		}

		[[nodiscard]] const std::filesystem::path& GetPath() const noexcept { return m_Directory.GetPath(); }
		[[nodiscard]] VFS& GetVFS() noexcept { return *m_Vfs; }
		[[nodiscard]] const Ref<VFS>& GetVFSRef() noexcept { return m_Vfs; }

		// The native path of a virtual path.
		[[nodiscard]] std::filesystem::path Resolve(std::string_view path) const
		{
			return GetPath() / std::string(path);
		}

		// Writes the file, creating its directories.
		void WriteFile(std::string_view path, std::string_view text) const
		{
			const std::filesystem::path file = Resolve(path);
			std::filesystem::create_directories(file.parent_path());
			std::ofstream stream(file, std::ios::binary | std::ios::trunc);
			REQUIRE(stream.good());
			stream.write(text.data(), static_cast<std::streamsize>(text.size()));
		}

		[[nodiscard]] std::string ReadFile(std::string_view path) const { return ReadTextFile(Resolve(path)); }

		[[nodiscard]] bool Exists(std::string_view path) const { return std::filesystem::exists(Resolve(path)); }

		void RemoveFile(std::string_view path) const { std::filesystem::remove(Resolve(path)); }

	private:
		ScopedTempDirectory m_Directory;
		Ref<VFS> m_Vfs;
	};
}
