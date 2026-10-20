#include "gbpch.h"

#include "Gambit/Asset/Prefab.h"
#include "Gambit/Automation/BuiltinCommands.h"
#include "Gambit/Automation/Commands/CommandSupport.h"
#include "Gambit/Automation/UndoOperations.h"
#include "Gambit/Core/FileSystem.h"

namespace Gambit::Commands
{
	namespace
	{
		using namespace CommandSupport;

		constexpr std::array<AssetType, 14> AllAssetTypes = {
			AssetType::Scene,    AssetType::Prefab,        AssetType::Texture,   AssetType::Mesh,
			AssetType::Material, AssetType::Environment,   AssetType::Script,    AssetType::Audio,
			AssetType::Font,     AssetType::VectorGraphic, AssetType::DataTable, AssetType::AnimationClip,
			AssetType::Skeleton, AssetType::InputActions,
		};

		std::vector<std::string> AssetTypeNames()
		{
			std::vector<std::string> names;
			for (const AssetType type : AllAssetTypes)
			{
				names.emplace_back(ToString(type));
			}
			return names;
		}

		Json DescribeRecord(const AssetRecord& record)
		{
			Json json = Json::object();
			json["handle"] = record.Handle.ToString();
			json["path"] = record.Path;
			json["type"] = std::string(ToString(record.Type));
			return json;
		}

		// An asset named by its project path or its handle ("0x...").
		Result<const AssetRecord*> FindAsset(EditorContext& context, std::string_view text)
		{
			const AssetManager& assets = context.GetProject()->GetAssets();
			const AssetRecord* record = nullptr;
			if (text.starts_with("0x") || text.starts_with("0X"))
			{
				const std::optional<UUID> handle = UUID::Parse(text);
				if (!handle.has_value())
				{
					return MakeCommandError(
						CommandErrorCode::InvalidArguments, std::format("'{}' is not an asset handle", text));
				}
				record = assets.Find(*handle);
			}
			else
			{
				record = assets.FindByPath(text);
			}
			if (record == nullptr)
			{
				return MakeCommandError(
					CommandErrorCode::NotFound,
					std::format("there is no asset '{}' (asset.list shows the assets)", text));
			}
			return record;
		}

		CommandResult AssetList(EditorContext& context, const Json& arguments)
		{
			AssetType type = AssetType::None;
			if (Has(arguments, "type"))
			{
				type = ParseAssetType(GetString(arguments, "type")).value_or(AssetType::None);
			}
			const std::string prefix = GetString(arguments, "path");
			const auto offset = static_cast<size_t>(GetInteger(arguments, "offset"));
			const auto limit = static_cast<size_t>(GetInteger(arguments, "limit"));

			Json assets = Json::array();
			size_t total = 0;
			for (const AssetRecord& record : context.GetProject()->GetAssets().GetRegistry().List(type))
			{
				if (!record.Path.starts_with(prefix))
				{
					continue;
				}
				if (total >= offset && assets.size() < limit)
				{
					assets.push_back(DescribeRecord(record));
				}
				++total;
			}
			Json data = Json::object();
			data["count"] = assets.size();
			data["total"] = total;
			data["assets"] = std::move(assets);
			return CommandResult::Success(
				std::move(data), std::format("{} {}", total, total == 1 ? "asset" : "assets"));
		}

		CommandResult AssetInfo(EditorContext& context, const Json& arguments)
		{
			Result<const AssetRecord*> record = FindAsset(context, GetString(arguments, "asset"));
			if (!record)
			{
				return Fail(record.GetError());
			}
			AssetManager& assets = context.GetProject()->GetAssets();
			Json data = DescribeRecord(**record);
			const Result<AssetMetadata> metadata = assets.ReadMetadata((*record)->Handle);
			data["importSettings"] = metadata ? metadata->ImportSettings : Json::object();

			Json dependencies = Json::array();
			Result<std::vector<AssetHandle>> found = assets.GetDependencies((*record)->Handle);
			if (found)
			{
				for (const AssetHandle handle : *found)
				{
					Json entry = Json::object();
					entry["handle"] = handle.ToString();
					entry["path"] = assets.GetPath(handle);
					dependencies.push_back(std::move(entry));
				}
			}
			else
			{
				data["dependenciesError"] = found.GetError().Message;
			}
			data["dependencies"] = std::move(dependencies);
			return CommandResult::Success(
				std::move(data), std::format("{} ({})", (*record)->Path, ToString((*record)->Type)));
		}

		CommandResult AssetImport(EditorContext& context, const Json& arguments)
		{
			const std::string source = GetString(arguments, "source");
			const std::filesystem::path sourcePath = FileSystem::FromUtf8(source);
			std::string target = GetString(arguments, "path");
			if (target.empty())
			{
				target = std::format(
					"{}/{}", context.GetProject()->GetSettings().Directories.Assets,
					FileSystem::ToUtf8(sourcePath.filename()));
			}
			Result<std::string> resolved = context.ResolveAssetPath(target);
			if (!resolved)
			{
				return Fail(resolved.GetError());
			}
			if (DetectAssetType(*resolved) == AssetType::None)
			{
				return Fail(
					CommandErrorCode::InvalidArguments,
					std::format(
						"'{}' is not a kind of file the engine imports (.gscene .gprefab .png .jpg .hdr .glb .gltf "
						".gmat "
						".lua .wav .ogg .ttf .svg .csv, InputActions.json)",
						*resolved));
			}
			VFS& vfs = context.GetProject()->GetVFS();
			if (vfs.Exists(*resolved) && !GetBool(arguments, "overwrite", false))
			{
				return Fail(
					CommandErrorCode::AlreadyExists,
					std::format("'{}' exists already: pass overwrite to replace it", *resolved));
			}
			Result<Buffer> content = FileSystem::ReadBinary(sourcePath);
			if (!content)
			{
				return Fail(AddSource(source, content.GetError()));
			}
			// An open scene or table of the file would keep the old content (and saving it would undo the import).
			Result<bool> closed = context.CloseReplacedDocument(*resolved);
			if (!closed)
			{
				return Fail(closed.GetError());
			}
			if (Result<void> written = vfs.Write(*resolved, content->View()); !written)
			{
				return Fail(AddSource(*resolved, written.GetError()));
			}

			AssetManager& assets = context.GetProject()->GetAssets();
			const Json settings = Has(arguments, "importSettings") ? arguments.at("importSettings") : Json::object();
			Result<AssetHandle> handle = assets.RegisterFile(*resolved, settings); // a known file keeps its .meta
			if (!handle)
			{
				return Fail(AddSource(*resolved, handle.GetError()));
			}
			assets.UnloadAsset(*handle); // an overwritten file must be read again
			// A replaced file keeps its handle (so references stay valid) and its settings unless new ones are given.
			Result<AssetMetadata> metadata = assets.ReadMetadata(*handle);
			if (!metadata)
			{
				return Fail(AddSource(*resolved, metadata.GetError()));
			}
			if (Has(arguments, "importSettings") && metadata->ImportSettings != settings)
			{
				if (Result<void> set = assets.SetImportSettings(*handle, settings); !set)
				{
					return Fail(AddSource(*resolved, set.GetError()));
				}
				metadata->ImportSettings = settings;
			}
			Json data = Json::object();
			data["handle"] = handle->ToString();
			data["path"] = *resolved;
			data["type"] = std::string(ToString(DetectAssetType(*resolved)));
			data["size"] = content->Size();
			data["importSettings"] = std::move(metadata->ImportSettings);
			CommandResult result =
				CommandResult::Success(std::move(data), std::format("Imported {} as '{}'", source, *resolved));
			if (*closed)
			{
				result.Warnings.push_back(std::format(
					"'{}' was open; it was closed because its file was replaced (open it again for the new content)",
					*resolved));
			}
			return result;
		}

		// ---- Prefabs ----

		CommandResult PrefabCreate(EditorContext& context, const Json& arguments)
		{
			World& world = *context.GetActiveScene()->Scene;
			Result<Entity> entity = ResolveEntity(world, GetString(arguments, "entity"));
			if (!entity)
			{
				return Fail(entity.GetError());
			}
			Result<std::string> path = context.ResolveAssetPath(GetString(arguments, "path"), ".gprefab");
			if (!path)
			{
				return Fail(path.GetError());
			}
			VFS& vfs = context.GetProject()->GetVFS();
			if (vfs.Exists(*path) && !GetBool(arguments, "overwrite", false))
			{
				return Fail(
					CommandErrorCode::AlreadyExists,
					std::format("'{}' exists already: pass overwrite to replace it", *path));
			}
			Result<Ref<Prefab>> prefab = PrefabUtils::CreateFromEntity(world, *entity, GetString(arguments, "name"));
			if (!prefab)
			{
				return Fail(prefab.GetError());
			}
			if (Result<void> saved = PrefabUtils::Save(world.GetTypeRegistry(), **prefab, vfs, *path); !saved)
			{
				return Fail(saved.GetError());
			}
			AssetManager& assets = context.GetProject()->GetAssets();
			Result<AssetHandle> handle = assets.RegisterFile(*path);
			if (!handle)
			{
				return Fail(AddSource(*path, handle.GetError()));
			}
			assets.UnloadAsset(*handle);
			Json data = Json::object();
			data["handle"] = handle->ToString();
			data["path"] = *path;
			data["name"] = (*prefab)->GetName();
			data["entityCount"] = (*prefab)->GetEntities().size();
			return CommandResult::Success(
				std::move(data), std::format("Created prefab '{}' from '{}'", *path, entity->GetName()));
		}

		CommandResult PrefabInstantiate(EditorContext& context, const Json& arguments)
		{
			SceneDocument& scene = *context.GetActiveScene();
			World& world = *scene.Scene;
			Result<const AssetRecord*> record = FindAsset(context, GetString(arguments, "prefab"));
			if (!record)
			{
				return Fail(record.GetError());
			}
			if ((*record)->Type != AssetType::Prefab)
			{
				return Fail(
					CommandErrorCode::InvalidArguments,
					std::format("'{}' is a {}, not a prefab", (*record)->Path, ToString((*record)->Type)));
			}
			const Ref<Prefab> prefab = context.GetProject()->GetAssets().GetAsset<Prefab>((*record)->Handle);
			if (prefab == nullptr)
			{
				return Fail(
					CommandErrorCode::Failed,
					std::format("cannot load the prefab '{}' (see the log)", (*record)->Path));
			}
			Result<Entity> parent = ResolveOptionalEntity(world, arguments, "parent");
			if (!parent)
			{
				return Fail(parent.GetError());
			}

			std::optional<TransformComponent> transform;
			if (Has(arguments, "transform"))
			{
				const ComponentInfo& info = world.GetTypeRegistry().GetComponent<TransformComponent>();
				Result<std::vector<FieldData>> edits = DecodeFieldEdits(info, nullptr, arguments.at("transform"));
				if (!edits)
				{
					return Fail(edits.GetError());
				}
				transform = TransformComponent{};
				for (const FieldData& edit : *edits)
				{
					[[maybe_unused]] const bool written = info.FindField(edit.Name)->SetValue(&*transform, edit.Value);
				}
			}

			const std::optional<int64_t> index = TryGetInteger(arguments, "index");
			const UUID parentUuid = parent->IsValid() ? parent->GetUUID() : UUID();
			const Ref<UUID> created = CreateRef<UUID>();
			const Ref<size_t> count = CreateRef<size_t>(0);
			SubtreeOperation::Creator creator = [prefab, parentUuid, transform, index, created,
			                                     count](World& target) -> Result<Entity> {
				PrefabUtils::InstantiateOptions options;
				options.Ids = UUIDMode::Random;
				const Entity parentEntity = parentUuid.IsValid() ? target.TryGetEntity(parentUuid) : Entity();
				Result<Entity> root = PrefabUtils::Instantiate(target, *prefab, parentEntity, transform, options);
				if (!root)
				{
					return MakeCommandError(CommandErrorCode::Failed, root.GetError().Message);
				}
				if (index.has_value())
				{
					target.SetSiblingIndex(*root, static_cast<size_t>(*index));
				}
				*created = root->GetUUID();
				*count = prefab->GetEntities().size();
				return root;
			};
			const std::string description = std::format("Instantiate prefab '{}'", (*record)->Path);
			if (Result<void> done = context.Do(SubtreeOperation::Create(scene.Path, std::move(creator), description));
			    !done)
			{
				return Fail(done.GetError());
			}
			const Entity root = world.TryGetEntity(*created);
			Json data = DescribeEntity(world, root);
			data["index"] = world.GetSiblingIndex(root);
			data["entityCount"] = *count;
			return CommandResult::Success(
				std::move(data),
				std::format("Instantiated '{}' as '{}' ({})", (*record)->Path, root.GetName(), created->ToString()));
		}
	}

	void RegisterAssetCommands(CommandRegistry& registry)
	{
		RegisterOrDie(
			registry,
			CommandInfo{
				.Name = "asset.list",
				.Summary = "List the assets of the project",
				.Arguments = { EnumArg("type", AssetTypeNames(), "Only assets of this type"),
		                       StringArg("path", "Only assets whose path starts with this text, e.g. Assets/Textures/"),
		                       IntegerArg("limit", "At most this many assets").WithRange(1, 100000).WithDefault(500),
		                       IntegerArg("offset", "Skip this many assets").WithMinimum(0).WithDefault(0) },
				.ResultDescription = "{ count, total, assets: [{ handle, path, type }] } sorted by path",
				.Flags = CommandFlags::RequiresProject,
			},
			AssetList);

		RegisterOrDie(
			registry,
			CommandInfo{
				.Name = "asset.info",
				.Summary = "Show an asset: its handle, type, import settings and the assets it refers to",
				.Arguments = { StringArg("asset", "The asset's project path or its handle (0x...)").Require() },
				.ResultDescription = "{ handle, path, type, importSettings, dependencies: [{ handle, path }] }",
				.Flags = CommandFlags::RequiresProject,
			},
			AssetInfo);

		RegisterOrDie(
			registry,
			CommandInfo{
				.Name = "asset.import",
				.Summary = "Copy a file into the project and make it an asset",
				.Description =
					"Writes the file and its .meta file, which gives the asset its handle. The kind of asset "
					"follows from the file name. Replacing a file (overwrite) keeps its handle, and its import "
					"settings unless importSettings is given; an open scene or table of the file is closed, or the "
					"import refused when it has unsaved changes.",
				.Arguments = { StringArg("source", "Path of the file on disk").Require(),
		                       StringArg(
								   "path",
								   "Project path of the copy below an asset directory (default: Assets/<file name>)"),
		                       ObjectArg("importSettings", "Settings for the importer of the asset type"),
		                       BooleanArg("overwrite", "Replace a file that exists").WithDefault(false) },
				.ResultDescription = "{ handle, path, type, size, importSettings }",
				.Flags = CommandFlags::Mutating | CommandFlags::RequiresProject | CommandFlags::WritesFiles,
			},
			AssetImport);

		RegisterOrDie(
			registry,
			CommandInfo{
				.Name = "prefab.create",
				.Summary = "Save an entity with its children as a prefab asset",
				.Description =
					"References to entities outside the subtree are cleared; PrefabInstance components are dropped.",
				.Arguments = { StringArg("entity", "UUID (0x...) or the unique name of the root entity").Require(),
		                       StringArg("path", "Project path of the .gprefab file below an asset directory")
		                           .Require(),
		                       StringArg("name", "Name of the prefab (default: the entity's name)"),
		                       BooleanArg("overwrite", "Replace a file that exists").WithDefault(false) },
				.ResultDescription = "{ handle, path, name, entityCount }",
				.Flags = CommandFlags::Mutating | CommandFlags::RequiresScene | CommandFlags::WritesFiles,
			},
			PrefabCreate);

		RegisterOrDie(
			registry,
			CommandInfo{
				.Name = "prefab.instantiate",
				.Summary = "Add a copy of a prefab to the active scene",
				.Arguments = { StringArg("prefab", "The prefab's project path or handle").Require(),
		                       StringArg("parent", "Parent entity (UUID or unique name; default: a root entity)"),
		                       ObjectArg("transform", "Local transform of the root: { \"Translation\": [0, 1, 0] }"),
		                       IntegerArg("index", "Position among the siblings (default: last)").WithMinimum(0) },
				.ResultDescription = "{ uuid, name, parent, enabled, index, entityCount }",
				.Flags = CommandFlags::Mutating | CommandFlags::Undoable | CommandFlags::RequiresScene,
			},
			PrefabInstantiate);
	}
}
