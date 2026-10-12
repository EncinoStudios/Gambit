#include "gbpch.h"

#include "Gambit/Project/Project.h"

#include "Gambit/Core/DirectoryMount.h"
#include "Gambit/Core/FileSystem.h"
#include "Gambit/ECS/World.h"
#include "Gambit/Serialization/FieldSerializer.h"
#include "Gambit/Serialization/SceneSerializer.h"

#include <cctype>
#include <mutex>

namespace Gambit
{
	namespace
	{
		constexpr const char* FormatName = "project";
		constexpr std::string_view DefaultSceneName = "Main";

		std::mutex s_ActiveMutex;
		Ref<Project> s_Active;

		bool HasProjectExtension(const std::filesystem::path& path)
		{
			std::string extension = FileSystem::ToUtf8(path.extension());
			std::ranges::transform(extension, extension.begin(), [](char character) {
				return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
			});
			return extension == Project::FileExtension;
		}

		// The one .gproject file of a directory.
		Result<std::filesystem::path> FindProjectFile(const std::filesystem::path& directory)
		{
			Result<std::vector<DirectoryEntry>> entries = FileSystem::ListDirectory(directory, false);
			if (!entries)
			{
				return entries.GetError();
			}
			std::vector<std::filesystem::path> found;
			for (const DirectoryEntry& entry : *entries)
			{
				const std::filesystem::path path = directory / FileSystem::FromUtf8(entry.Path);
				if (!entry.IsDirectory && HasProjectExtension(path))
				{
					found.push_back(path);
				}
			}
			if (found.empty())
			{
				return MakeFileError(
					FileError::NotFound,
					std::format("'{}' contains no {} file", FileSystem::ToUtf8(directory), Project::FileExtension));
			}
			if (found.size() > 1)
			{
				return Error::Format(
					"'{}' contains {} {} files ({} and others): open one of them", FileSystem::ToUtf8(directory),
					found.size(), Project::FileExtension, FileSystem::ToUtf8(found.front().filename()));
			}
			return found.front();
		}

		Result<std::filesystem::path> MakeAbsolute(const std::filesystem::path& path)
		{
			std::error_code error;
			std::filesystem::path absolute = std::filesystem::absolute(path, error);
			if (error)
			{
				return MakeFileError(
					FileSystem::ClassifyError(error),
					std::format("cannot resolve '{}': {}", FileSystem::ToUtf8(path), error.message()));
			}
			return absolute.lexically_normal();
		}

		// The input mapping a new project starts with: WASD and the left stick move, the mouse and the right stick
		// look.
		InputActions MakeDefaultInputActions()
		{
			const auto key = [](InputBindingType type, KeyCode code) {
				InputBinding binding;
				binding.Type = type;
				binding.Key = code;
				return binding;
			};
			const auto wasd = [](KeyCode up, KeyCode down, KeyCode left, KeyCode right) {
				InputBinding binding;
				binding.Type = InputBindingType::Composite2D;
				binding.Up = up;
				binding.Down = down;
				binding.Left = left;
				binding.Right = right;
				return binding;
			};
			const auto stick = [](GamepadStick which, InputBindingType type) {
				InputBinding binding;
				binding.Type = type;
				binding.Stick = which;
				binding.Deadzone = 0.15f;
				return binding;
			};
			const auto padButton = [](GamepadButton button) {
				InputBinding binding;
				binding.Type = InputBindingType::GamepadButton;
				binding.PadButton = button;
				return binding;
			};
			InputBinding mouse;
			mouse.Type = InputBindingType::MouseDelta;

			std::vector<InputAction> actions;
			actions.push_back({ "Move",
			                    InputActionType::Axis2D,
			                    { wasd(KeyCode::W, KeyCode::S, KeyCode::A, KeyCode::D),
			                      wasd(KeyCode::Up, KeyCode::Down, KeyCode::Left, KeyCode::Right),
			                      stick(GamepadStick::Left, InputBindingType::GamepadStick) } });
			actions.push_back({ "Look",
			                    InputActionType::Axis2D,
			                    { mouse, stick(GamepadStick::Right, InputBindingType::GamepadStick) } });
			actions.push_back({ "Jump",
			                    InputActionType::Button,
			                    { key(InputBindingType::Key, KeyCode::Space), padButton(GamepadButton::A) } });
			actions.push_back({ "Interact",
			                    InputActionType::Button,
			                    { key(InputBindingType::Key, KeyCode::E), padButton(GamepadButton::X) } });
			Result<InputActions> created = InputActions::Create(std::move(actions));
			GB_CORE_VERIFY(created.HasValue(), "the default input actions are invalid: {}", created.GetError().Message);
			return std::move(*created);
		}
	}

	Project::Project(
		ConstructionKey, std::filesystem::path directory, std::filesystem::path file, ProjectSettings settings,
		TypeRegistry& types)
		: m_Directory(std::move(directory)),
		  m_File(std::move(file)),
		  m_Settings(std::move(settings)),
		  m_Types(types),
		  m_Vfs(CreateRef<VFS>())
	{
		const Result<void> mounted = m_Vfs->Mount("", CreateRef<DirectoryMount>(m_Directory));
		GB_CORE_VERIFY(mounted.HasValue(), "cannot mount the project directory: {}", mounted.GetError().Message);
		m_Assets = CreateScope<AssetManager>(m_Vfs, GetAssetDirectories(), m_Types);
	}

	const MigrationTable& Project::GetMigrations()
	{
		return GetNoMigrations();
	}

	Result<ProjectSettings> Project::ParseSettings(
		const TypeRegistry& types, std::string_view text, std::string_view source, LoadReport& report,
		const MigrationTable& migrations)
	{
		Result<Json> document = ParseJson(text, source);
		if (!document)
		{
			return document.GetError();
		}
		if (Result<void> upgraded = migrations.Upgrade(*document, FormatVersion, FormatName); !upgraded)
		{
			return AddSource(source, upgraded.GetError());
		}
		document->erase("Version");

		const Ref<const StructInfo> info = types.FindStruct("ProjectSettings");
		GB_CORE_VERIFY(info != nullptr, "the TypeRegistry lacks the project types (RegisterEngineTypes)");
		ProjectSettings settings;
		if (Result<void> read = StructFromJson(*info, *document, &settings, "", report); !read)
		{
			return AddSource(source, read.GetError());
		}
		if (Result<void> valid = ValidateProjectSettings(settings); !valid)
		{
			return AddSource(source, valid.GetError());
		}
		return settings;
	}

	Result<std::string> Project::SerializeSettings(const TypeRegistry& types, const ProjectSettings& settings)
	{
		const Ref<const StructInfo> info = types.FindStruct("ProjectSettings");
		GB_CORE_VERIFY(info != nullptr, "the TypeRegistry lacks the project types (RegisterEngineTypes)");
		Result<Json> members = StructToJson(*info, &settings);
		if (!members)
		{
			return members.GetError();
		}
		Json document = Json::object();
		document["Version"] = FormatVersion;
		for (const auto& [key, member] : members->items())
		{
			document[key] = member;
		}
		return DumpJson(document);
	}

	Result<Ref<Project>>
	Project::New(const std::filesystem::path& directory, std::string_view name, TypeRegistry& types)
	{
		if (!IsValidProjectName(name))
		{
			return Error::Format("'{}' is not a valid project name", name);
		}
		Result<std::filesystem::path> root = MakeAbsolute(directory);
		if (!root)
		{
			return root.GetError();
		}
		if (Result<void> created = FileSystem::CreateDirectories(*root); !created)
		{
			return created.GetError();
		}
		const Result<std::filesystem::path> existing = FindProjectFile(*root);
		if (existing)
		{
			return Error::Format(
				"'{}' already contains the project file '{}'", FileSystem::ToUtf8(*root),
				FileSystem::ToUtf8(existing->filename()));
		}
		if (!HasFileError(existing.GetError(), FileError::NotFound))
		{
			return existing.GetError(); // several project files, or a directory that cannot be listed
		}

		ProjectSettings settings;
		settings.Name = std::string(name);
		settings.StartScene = std::format("{}/{}.gscene", settings.Directories.Scenes, DefaultSceneName);
		settings.Export.OutputName = std::string(name);
		if (Result<void> valid = ValidateProjectSettings(settings); !valid)
		{
			return valid.GetError();
		}

		const std::filesystem::path file = *root / FileSystem::FromUtf8(std::format("{}{}", name, FileExtension));
		auto project = CreateRef<Project>(ConstructionKey{}, *root, file, settings, types);
		for (const std::string& folder :
		     { settings.Directories.Assets, settings.Directories.Scripts, settings.Directories.Data,
		       settings.Directories.Scenes, settings.Directories.Comments })
		{
			if (Result<void> created = FileSystem::CreateDirectories(*root / FileSystem::FromUtf8(folder)); !created)
			{
				return created.GetError();
			}
		}

		VFS& vfs = project->GetVFS();
		if (!vfs.Exists(settings.InputActionsPath))
		{
			if (Result<void> written = vfs.WriteText(settings.InputActionsPath, MakeDefaultInputActions().ToJson());
			    !written)
			{
				return AddSource(settings.InputActionsPath, written.GetError());
			}
		}
		if (!vfs.Exists(settings.StartScene))
		{
			World world(types);
			world.SetSeed(settings.DefaultSeed);
			SceneMetadata metadata;
			metadata.Name = std::string(DefaultSceneName);
			if (Result<void> saved = SceneSerializer::Save(world, metadata, vfs, settings.StartScene); !saved)
			{
				return saved.GetError();
			}
		}
		if (Result<void> saved = project->Save(); !saved)
		{
			return saved.GetError();
		}
		GB_CORE_INFO("Created project '{}' in {}", name, FileSystem::ToUtf8(*root));
		return project;
	}

	Result<Ref<Project>> Project::Load(const std::filesystem::path& path, TypeRegistry& types)
	{
		Result<std::filesystem::path> absolute = MakeAbsolute(path);
		if (!absolute)
		{
			return absolute.GetError();
		}
		std::error_code error;
		std::filesystem::path file = *absolute;
		if (std::filesystem::is_directory(file, error))
		{
			Result<std::filesystem::path> found = FindProjectFile(file);
			if (!found)
			{
				return found.GetError();
			}
			file = std::move(*found);
		}
		else if (!HasProjectExtension(file))
		{
			return Error::Format(
				"'{}' is not a {} file or a project directory", FileSystem::ToUtf8(file), FileExtension);
		}

		const std::string source = FileSystem::ToUtf8(file);
		Result<std::string> text = FileSystem::ReadText(file);
		if (!text)
		{
			return AddSource(source, text.GetError());
		}
		LoadReport report;
		Result<ProjectSettings> settings = ParseSettings(types, *text, source, report);
		if (!settings)
		{
			return settings.GetError();
		}
		report.Log(source);
		return CreateRef<Project>(ConstructionKey{}, file.parent_path(), file, std::move(*settings), types);
	}

	Ref<Project> Project::GetActive()
	{
		const std::scoped_lock lock(s_ActiveMutex);
		return s_Active;
	}

	void Project::SetActive(Ref<Project> project)
	{
		const std::scoped_lock lock(s_ActiveMutex);
		s_Active = std::move(project);
	}

	Result<void> Project::SetSettings(ProjectSettings settings)
	{
		if (Result<void> valid = ValidateProjectSettings(settings); !valid)
		{
			return valid;
		}
		m_Settings = std::move(settings);
		m_Assets->SetAssetDirectories(GetAssetDirectories());
		return {};
	}

	Result<void> Project::Save() const
	{
		Result<std::string> text = SerializeSettings(m_Types, m_Settings);
		if (!text)
		{
			return text.GetError();
		}
		return FileSystem::WriteText(m_File, *text);
	}

	std::vector<std::string> Project::GetAssetDirectories() const
	{
		const ProjectDirectories& directories = m_Settings.Directories;
		return { directories.Assets, directories.Scenes, directories.Data, directories.Scripts };
	}

	Result<InputActions> Project::LoadInputActions() const
	{
		return InputActions::Load(*m_Vfs, m_Settings.InputActionsPath);
	}
}
