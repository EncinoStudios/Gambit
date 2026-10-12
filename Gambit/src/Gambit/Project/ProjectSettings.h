#pragma once

#include "Gambit/Core/Result.h"
#include "Gambit/Core/UUID.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace Gambit
{
	class TypeRegistry;

	// The tone mapping operator of the final image (D-032).
	enum class Tonemapper : uint8_t
	{
		ACES,
		PBRNeutral
	};

	// The folders of a project, relative to its root (D-014). Scenes, Data and Scripts hold assets next to Assets;
	// Comments holds the comment threads (D-052).
	struct ProjectDirectories
	{
		std::string Assets = "Assets";
		std::string Scripts = "Scripts";
		std::string Data = "Data";
		std::string Scenes = "Scenes";
		std::string Comments = "Comments";
	};

	// Rendering defaults of the project; scenes may override them.
	struct RenderingSettings
	{
		UUID Environment; // the HDRI (an Environment asset); null for the default sky
		Tonemapper Tonemap = Tonemapper::ACES;
		float Exposure = 1.0f;
	};

	// What `gambit export` needs to know (D-070).
	struct ExportSettings
	{
		std::string OutputName; // the name of the exported game; the project name when empty
		bool Compress = true;   // compress the blocks of the content pack
	};

	// The contents of a .gproject file (D-025): plain data, reflected as the struct "ProjectSettings" (see
	// RegisterProjectTypes), so the file, the inspector, the CLI and scripts all read it the same way.
	struct ProjectSettings
	{
		std::string Name;
		std::string StartScene; // project-relative .gscene path; empty when the project has no scene yet
		ProjectDirectories Directories;
		uint32_t SimulationHz = 60; // fixed simulation ticks per second (D-011)
		uint64_t DefaultSeed = 1;   // the seed of new scenes
		std::string InputActionsPath =
			"InputActions.json"; // project-relative path of the input mapping (D-012); "InputActions" in files
		RenderingSettings Rendering;
		ExportSettings Export;
	};

	// Whether `name` can be a project name, which also names the .gproject file: 1 to 64 characters of valid UTF-8,
	// without control characters or any of < > : " / \ | ? *, not ending in a dot or a space and not a reserved
	// Windows device name.
	[[nodiscard]] bool IsValidProjectName(std::string_view name) noexcept;

	// Checks everything the types cannot: names, hz and exposure ranges, and that every path is a normalized
	// relative path inside the project, the five directories being distinct.
	[[nodiscard]] Result<void> ValidateProjectSettings(const ProjectSettings& settings);

	// Registers the enum "Tonemapper" and the structs "ProjectDirectories", "RenderingSettings", "ExportSettings" and
	// "ProjectSettings".
	void RegisterProjectTypes(TypeRegistry& registry);
}
