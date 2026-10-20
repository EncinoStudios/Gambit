#pragma once

#include "TestUtils/TempDirectory.h"

#include "Gambit/Asset/AssetManager.h"
#include "Gambit/Reflection/TypeRegistry.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <string>
#include <string_view>

namespace Gambit::Tests
{
	// The generated test content of GambitTests/Assets (tools/gen_test_gltf.py).
	[[nodiscard]] inline std::filesystem::path GetTestAssetPath(std::string_view relativePath)
	{
		return std::filesystem::path(GB_TEST_ASSETS_DIR) / std::string(relativePath);
	}

	// A TypeRegistry with the engine's types (RegisterEngineTypes).
	struct EngineTypes
	{
		TypeRegistry Registry;

		EngineTypes() { RegisterEngineTypes(Registry); }
	};

	// A temporary project with the engine's types and an AssetManager over "Assets". Test content is copied in, so
	// the .meta files that imports write never touch the source tree.
	struct RendererProject
	{
		TempDirectory Directory;
		EngineTypes Types;
		AssetManager Assets;

		RendererProject()
			: Assets(Directory.GetVFSRef(), { "Assets" }, Types.Registry)
		{}

		// Copies GambitTests/Assets/<source> to the project path `destination`.
		void CopyTestAsset(std::string_view source, std::string_view destination) const
		{
			const std::filesystem::path target = Directory.Resolve(destination);
			std::filesystem::create_directories(target.parent_path());
			std::error_code error;
			std::filesystem::copy_file(
				GetTestAssetPath(source), target, std::filesystem::copy_options::overwrite_existing, error);
			REQUIRE_MESSAGE(!error, "cannot copy the test asset ", std::string(source), ": ", error.message());
		}

		// Copies the files of a test model (the .gltf with its .bin and images) into Assets/Models/.
		void CopyTexturedCube() const
		{
			for (const char* file : { "TexturedCube.gltf", "TexturedCube.bin", "Checker.png", "Bumps.png" })
			{
				CopyTestAsset(std::string("Models/") + file, std::string("Assets/Models/") + file);
			}
		}
	};
}
