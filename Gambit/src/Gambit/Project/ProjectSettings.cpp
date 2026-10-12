#include "gbpch.h"

#include "Gambit/Project/ProjectSettings.h"

#include "Gambit/Core/FileSystem.h"
#include "Gambit/Core/FixedTimestepClock.h"
#include "Gambit/Core/Utf8.h"
#include "Gambit/Reflection/TypeRegistry.h"

#include <set>

namespace Gambit
{
	namespace
	{
		constexpr size_t MaxProjectNameLength = 64;
		constexpr std::string_view ForbiddenNameCharacters = "<>:\"/\\|?*";

		// A normalized path below the project root (no "..", no leading slash, no drive).
		Result<void> CheckProjectPath(std::string_view what, std::string_view path, bool allowEmpty)
		{
			if (path.empty())
			{
				if (allowEmpty)
				{
					return {};
				}
				return Error::Format("{} must not be empty", what);
			}
			Result<std::string> normalized = FileSystem::NormalizePath(path);
			if (!normalized || normalized->empty() || FileSystem::IsAbsolutePath(*normalized) || *normalized != path)
			{
				return Error::Format(
					"{} '{}' must be a normalized path relative to the project (forward slashes, no '..')", what, path);
			}
			return {};
		}
	}

	bool IsValidProjectName(std::string_view name) noexcept
	{
		if (name.empty() || name.size() > MaxProjectNameLength || !IsValidUtf8(name))
		{
			return false;
		}
		if (name.back() == '.' || name.back() == ' ')
		{
			return false;
		}
		const bool badCharacter = std::ranges::any_of(name, [](char character) {
			return static_cast<unsigned char>(character) < 0x20 || character == 0x7F
				|| ForbiddenNameCharacters.find(character) != std::string_view::npos;
		});
		return !badCharacter && !FileSystem::IsReservedDeviceName(name) && name != "." && name != "..";
	}

	Result<void> ValidateProjectSettings(const ProjectSettings& settings)
	{
		if (!IsValidProjectName(settings.Name))
		{
			return Error::Format(
				"'{}' is not a valid project name: use 1 to {} characters without control characters or any of "
				"{}, not ending in a dot or space",
				settings.Name, MaxProjectNameLength, ForbiddenNameCharacters);
		}
		if (settings.SimulationHz < 1 || settings.SimulationHz > FixedTimestepClock::MaxSimulationHz)
		{
			return Error::Format(
				"SimulationHz must be between 1 and {}, not {}", FixedTimestepClock::MaxSimulationHz,
				settings.SimulationHz);
		}
		if (!std::isfinite(settings.Rendering.Exposure) || settings.Rendering.Exposure <= 0.0f)
		{
			return Error("Rendering.Exposure must be a positive number");
		}
		if (!settings.Export.OutputName.empty() && !IsValidProjectName(settings.Export.OutputName))
		{
			return Error::Format("Export.OutputName '{}' is not a valid file name", settings.Export.OutputName);
		}

		const ProjectDirectories& directories = settings.Directories;
		const std::pair<std::string_view, const std::string&> named[] = {
			{ "Directories.Assets", directories.Assets },     { "Directories.Scripts", directories.Scripts },
			{ "Directories.Data", directories.Data },         { "Directories.Scenes", directories.Scenes },
			{ "Directories.Comments", directories.Comments },
		};
		std::set<std::string_view> seen;
		for (const auto& [what, path] : named)
		{
			if (Result<void> valid = CheckProjectPath(what, path, false); !valid)
			{
				return valid;
			}
			if (!seen.insert(path).second)
			{
				return Error::Format("{} '{}' is used by another directory", what, path);
			}
		}
		if (Result<void> valid = CheckProjectPath("StartScene", settings.StartScene, true); !valid)
		{
			return valid;
		}
		return CheckProjectPath("InputActions", settings.InputActionsPath, false);
	}

	void RegisterProjectTypes(TypeRegistry& registry)
	{
		registry.RegisterEnum<Tonemapper>("Tonemapper")
			.Value("ACES", Tonemapper::ACES)
			.Value("PBRNeutral", Tonemapper::PBRNeutral);

		registry.RegisterStruct<ProjectDirectories>("ProjectDirectories")
			.Field("Assets", &ProjectDirectories::Assets, Attr::Tooltip{ "Imported assets with their .meta files" })
			.Field("Scripts", &ProjectDirectories::Scripts, Attr::Tooltip{ "Lua scripts" })
			.Field("Data", &ProjectDirectories::Data, Attr::Tooltip{ "Data tables (CSV)" })
			.Field("Scenes", &ProjectDirectories::Scenes, Attr::Tooltip{ "Scenes and prefabs" })
			.Field("Comments", &ProjectDirectories::Comments, Attr::Tooltip{ "Comment threads" });

		registry.RegisterStruct<RenderingSettings>("RenderingSettings")
			.Field("Environment", &RenderingSettings::Environment, Attr::AssetType{ "Environment" })
			.Field("Tonemapper", &RenderingSettings::Tonemap)
			.Field("Exposure", &RenderingSettings::Exposure, Attr::Range{ 0.001, 64.0 });

		registry.RegisterStruct<ExportSettings>("ExportSettings")
			.Field("OutputName", &ExportSettings::OutputName, Attr::Tooltip{ "Name of the exported game" })
			.Field("Compress", &ExportSettings::Compress);

		registry.RegisterStruct<ProjectSettings>("ProjectSettings")
			.Field("Name", &ProjectSettings::Name)
			.Field("StartScene", &ProjectSettings::StartScene, Attr::Tooltip{ "The scene the game starts with" })
			.Field("Directories", &ProjectSettings::Directories)
			.Field("SimulationHz", &ProjectSettings::SimulationHz, Attr::Range{ 1.0, 1000.0 })
			.Field("DefaultSeed", &ProjectSettings::DefaultSeed)
			.Field("InputActions", &ProjectSettings::InputActionsPath)
			.Field("Rendering", &ProjectSettings::Rendering)
			.Field("Export", &ProjectSettings::Export);
	}
}
