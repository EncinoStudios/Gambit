#include "gbpch.h"

#include "Gambit/Automation/BuiltinCommands.h"
#include "Gambit/Automation/Commands/CommandSupport.h"

namespace Gambit::Commands
{
	namespace
	{
		using namespace CommandSupport;

		std::vector<std::string> KindNames()
		{
			return { "Entity", "Component", "Asset", "File", "Feature", "Position" };
		}

		Json TargetToJson(const CommentTarget& target)
		{
			Json json = Json::object();
			json["kind"] = std::string(ToString(target.Kind));
			if (!target.Scene.empty())
			{
				json["scene"] = target.Scene;
			}
			if (target.Entity.IsValid())
			{
				json["entity"] = target.Entity.ToString();
			}
			if (!target.Component.empty())
			{
				json["component"] = target.Component;
			}
			if (!target.Asset.empty())
			{
				json["asset"] = target.Asset;
			}
			if (target.AssetHandle.IsValid())
			{
				json["handle"] = target.AssetHandle.ToString();
			}
			if (!target.File.empty())
			{
				json["file"] = target.File;
				if (target.Line != 0)
				{
					json["line"] = target.Line;
				}
			}
			if (!target.Feature.empty())
			{
				json["feature"] = target.Feature;
			}
			if (target.Kind == CommentTargetKind::Position)
			{
				json["position"] = Json::array({ target.Position.x, target.Position.y, target.Position.z });
			}
			return json;
		}

		Json ThreadToJson(const CommentThread& thread, bool withMessages)
		{
			Json json = Json::object();
			json["id"] = thread.Id;
			json["status"] = std::string(ToString(thread.Status));
			json["author"] = thread.Author;
			json["created"] = thread.Created;
			json["updated"] = thread.Updated;
			json["target"] = TargetToJson(thread.Target);
			json["messageCount"] = thread.Messages.size();
			if (withMessages)
			{
				Json messages = Json::array();
				for (const CommentMessage& message : thread.Messages)
				{
					Json entry = Json::object();
					entry["author"] = message.Author;
					entry["text"] = message.Text;
					entry["timestamp"] = message.Timestamp;
					messages.push_back(std::move(entry));
				}
				json["messages"] = std::move(messages);
			}
			else
			{
				json["text"] = thread.Messages.front().Text;
			}
			return json;
		}

		std::string AuthorOf(EditorContext& context, const Json& arguments)
		{
			return Has(arguments, "author") ? GetString(arguments, "author") : context.GetAuthorName();
		}

		// Fails with NotFound when there is no thread `id`; the store only knows it as an error message.
		Result<void> RequireThread(EditorContext& context, const std::string& id)
		{
			if (context.GetComments().Find(id) == nullptr)
			{
				return MakeCommandError(
					CommandErrorCode::NotFound,
					std::format("there is no comment thread '{}' (comment.list shows them)", id));
			}
			return {};
		}

		// The target a comment.add call describes; entities are resolved in the active scene.
		Result<CommentTarget> ReadTarget(EditorContext& context, const Json& arguments)
		{
			const std::string kindName = GetString(arguments, "kind");
			CommentTarget target;
			target.Kind = ParseCommentTargetKind(kindName).value_or(CommentTargetKind::Feature);
			const auto need = [&](std::string_view name, std::string_view why) -> Result<void> {
				if (!Has(arguments, name))
				{
					return MakeCommandError(
						CommandErrorCode::InvalidArguments,
						std::format("{} comments need '{}'{}", kindName, name, why));
				}
				return {};
			};

			switch (target.Kind)
			{
				case CommentTargetKind::Entity:
				case CommentTargetKind::Component:
				{
					if (Result<void> ok = need("entity", ""); !ok)
					{
						return ok.GetError();
					}
					SceneDocument* scene = Has(arguments, "scenePath")
						? context.FindScene(GetString(arguments, "scenePath"))
						: context.GetActiveScene();
					if (scene == nullptr)
					{
						return MakeCommandError(
							CommandErrorCode::NoScene,
							Has(arguments, "scenePath")
								? std::format("the scene '{}' is not open", GetString(arguments, "scenePath"))
								: std::string("no scene is open: open one and give its path as 'scenePath'"));
					}
					Result<Entity> entity = ResolveEntity(*scene->Scene, GetString(arguments, "entity"));
					if (!entity)
					{
						return entity.GetError();
					}
					target.Scene = scene->Path;
					target.Entity = entity->GetUUID();
					if (target.Kind == CommentTargetKind::Component)
					{
						if (Result<void> ok = need("component", ""); !ok)
						{
							return ok.GetError();
						}
						Result<const ComponentInfo*> info =
							FindComponentType(context.GetTypeRegistry(), GetString(arguments, "component"));
						if (!info)
						{
							return info.GetError();
						}
						target.Component = (*info)->GetName();
					}
					break;
				}
				case CommentTargetKind::Asset:
				{
					if (!Has(arguments, "asset"))
					{
						return MakeCommandError(
							CommandErrorCode::InvalidArguments,
							"Asset comments need 'asset' (its project path or handle)");
					}
					const std::string asset = GetString(arguments, "asset");
					const AssetManager& assets = context.GetProject()->GetAssets();
					const std::optional<UUID> handle = asset.starts_with("0x") ? UUID::Parse(asset) : std::nullopt;
					const AssetRecord* record = handle.has_value() ? assets.Find(*handle) : assets.FindByPath(asset);
					if (record == nullptr)
					{
						return MakeCommandError(
							CommandErrorCode::NotFound,
							std::format("there is no asset '{}' (asset.list shows the assets)", asset));
					}
					target.Asset = record->Path;
					target.AssetHandle = record->Handle;
					break;
				}
				case CommentTargetKind::File:
				{
					if (Result<void> ok = need("file", ""); !ok)
					{
						return ok.GetError();
					}
					Result<std::string> file = NormalizeProjectPath(GetString(arguments, "file"));
					if (!file)
					{
						return file.GetError();
					}
					target.File = std::move(*file);
					target.Line = static_cast<uint32_t>(GetInteger(arguments, "line", 0));
					break;
				}
				case CommentTargetKind::Feature:
					if (Result<void> ok = need("feature", ""); !ok)
					{
						return ok.GetError();
					}
					target.Feature = GetString(arguments, "feature");
					break;
				case CommentTargetKind::Position:
				{
					if (!Has(arguments, "position"))
					{
						return MakeCommandError(
							CommandErrorCode::InvalidArguments, "Position comments need 'position' ([x, y, z])");
					}
					const Json& position = arguments.at("position");
					if (position.size() != 3
					    || !std::ranges::all_of(position, [](const Json& v) { return v.is_number(); }))
					{
						return MakeCommandError(
							CommandErrorCode::InvalidArguments, "'position' is three numbers: [x, y, z]");
					}
					SceneDocument* scene = Has(arguments, "scenePath")
						? context.FindScene(GetString(arguments, "scenePath"))
						: context.GetActiveScene();
					if (scene == nullptr)
					{
						return MakeCommandError(CommandErrorCode::NoScene, "Position comments need an open scene");
					}
					target.Scene = scene->Path;
					target.Position =
						glm::vec3(position[0].get<float>(), position[1].get<float>(), position[2].get<float>());
					break;
				}
			}
			return target;
		}

		CommandResult CommentAdd(EditorContext& context, const Json& arguments)
		{
			Result<CommentTarget> target = ReadTarget(context, arguments);
			if (!target)
			{
				return Fail(target.GetError());
			}
			Result<CommentThread> thread = context.GetComments().Add(
				std::move(*target), AuthorOf(context, arguments), GetString(arguments, "text"));
			if (!thread)
			{
				return Fail(thread.GetError(), CommandErrorCode::InvalidArguments);
			}
			return CommandResult::Success(
				ThreadToJson(*thread, true), std::format("Started comment thread {}", thread->Id));
		}

		CommandResult CommentReply(EditorContext& context, const Json& arguments)
		{
			if (Result<void> found = RequireThread(context, GetString(arguments, "id")); !found)
			{
				return Fail(found.GetError());
			}
			Result<CommentThread> thread = context.GetComments().Reply(
				GetString(arguments, "id"), AuthorOf(context, arguments), GetString(arguments, "text"));
			if (!thread)
			{
				return Fail(thread.GetError(), CommandErrorCode::InvalidArguments);
			}
			return CommandResult::Success(ThreadToJson(*thread, true), std::format("Replied to {}", thread->Id));
		}

		CommandResult CommentSetStatus(EditorContext& context, const Json& arguments, bool resolve)
		{
			CommentStore& store = context.GetComments();
			const std::string id = GetString(arguments, "id");
			if (Result<void> found = RequireThread(context, id); !found)
			{
				return Fail(found.GetError());
			}
			const std::string author = AuthorOf(context, arguments);
			const std::string text = GetString(arguments, "text");
			Result<CommentThread> thread = resolve ? store.Resolve(id, author, text) : store.Reopen(id, author, text);
			if (!thread)
			{
				return Fail(thread.GetError(), CommandErrorCode::InvalidArguments);
			}
			return CommandResult::Success(
				ThreadToJson(*thread, true), std::format("{} {}", resolve ? "Resolved" : "Reopened", thread->Id));
		}

		CommandResult CommentList(EditorContext& context, const Json& arguments)
		{
			CommentQuery query;
			if (Has(arguments, "status"))
			{
				query.Status = ParseCommentStatus(GetString(arguments, "status"));
			}
			if (Has(arguments, "kind"))
			{
				query.Kind = ParseCommentTargetKind(GetString(arguments, "kind"));
			}
			query.Author = GetString(arguments, "author");
			query.Component = GetString(arguments, "component");
			query.Feature = GetString(arguments, "feature");
			// Paths are stored normalized, so the filters are too.
			const std::pair<std::string_view, std::string*> paths[] = { { "scenePath", &query.Scene },
				                                                        { "asset", &query.Asset },
				                                                        { "file", &query.File } };
			for (const auto& [name, destination] : paths)
			{
				if (!Has(arguments, name))
				{
					continue;
				}
				Result<std::string> path = NormalizeProjectPath(GetString(arguments, name));
				if (!path)
				{
					return Fail(path.GetError());
				}
				*destination = std::move(*path);
			}
			if (Has(arguments, "entity"))
			{
				const std::string entity = GetString(arguments, "entity");
				if (const std::optional<UUID> uuid = UUID::Parse(entity); uuid.has_value())
				{
					query.Entity = *uuid;
				}
				else if (context.GetActiveScene() != nullptr)
				{
					Result<Entity> found = ResolveEntity(*context.GetActiveScene()->Scene, entity);
					if (!found)
					{
						return Fail(found.GetError());
					}
					query.Entity = found->GetUUID();
				}
				else
				{
					return Fail(CommandErrorCode::NoScene, "'entity' by name needs an active scene; use the UUID");
				}
			}

			const std::vector<const CommentThread*> threads = context.GetComments().Query(query);
			const auto limit = static_cast<size_t>(GetInteger(arguments, "limit"));
			Json list = Json::array();
			for (size_t index = 0; index < threads.size() && index < limit; ++index)
			{
				list.push_back(ThreadToJson(*threads[index], false));
			}
			Json data = Json::object();
			data["count"] = list.size();
			data["total"] = threads.size();
			data["threads"] = std::move(list);
			return CommandResult::Success(
				std::move(data), std::format("{} {}", threads.size(), threads.size() == 1 ? "thread" : "threads"));
		}

		CommandResult CommentGet(EditorContext& context, const Json& arguments)
		{
			if (Result<void> found = RequireThread(context, GetString(arguments, "id")); !found)
			{
				return Fail(found.GetError());
			}
			return CommandResult::Success(ThreadToJson(*context.GetComments().Find(GetString(arguments, "id")), true));
		}

		CommandResult CommentDelete(EditorContext& context, const Json& arguments)
		{
			const std::string id = GetString(arguments, "id");
			if (Result<void> found = RequireThread(context, id); !found)
			{
				return Fail(found.GetError());
			}
			if (Result<void> deleted = context.GetComments().Delete(id); !deleted)
			{
				return Fail(deleted.GetError());
			}
			Json data = Json::object();
			data["deleted"] = id;
			return CommandResult::Success(std::move(data), std::format("Deleted comment thread {}", id));
		}
	}

	void RegisterCommentCommands(CommandRegistry& registry)
	{
		const ArgumentInfo author =
			StringArg("author", "Who writes it: a user or an agent name (default: the author of this session)");
		const ArgumentInfo id = StringArg("id", "The thread id, 0x... (comment.list shows them)").Require();
		// Not WritesFiles: in a dry run the store keeps its changes in memory (CommentStore::SetWritesEnabled), so the
		// commands still answer as they would.
		const CommandFlags writes = CommandFlags::Mutating | CommandFlags::RequiresProject;

		RegisterOrDie(
			registry,
			CommandInfo{
				.Name = "comment.add",
				.Summary = "Start a comment thread about an entity, component, asset, file, feature or position",
				.Description =
					"Threads are stored in the project's Comments folder (one JSON file each) for people and agents "
					"to find; they are not part of exports. Use them for open questions, follow-ups and review notes.",
				.Arguments = { EnumArg("kind", KindNames(), "What the comment is about").Require(),
		                       StringArg("text", "The comment").Require(),
		                       StringArg("entity", "Entity: UUID or unique name (kinds Entity and Component)"),
		                       StringArg(
								   "scenePath",
								   "Project path of the open scene of the entity or position (default: the active "
								   "scene)"),
		                       StringArg("component", "Component type name (kind Component)"),
		                       StringArg("asset", "Asset project path or handle (kind Asset)"),
		                       StringArg("file", "Project file path (kind File)"),
		                       IntegerArg("line", "Line of the file, from 1 (kind File)")
		                           .WithRange(1, std::numeric_limits<uint32_t>::max()),
		                       StringArg("feature", "Free tag of the feature or topic (kind Feature)"),
		                       ArrayArg("position", ArgumentType::Number, "World position [x, y, z] (kind Position)"),
		                       author },
				.ResultDescription = "The thread: { id, status, author, created, updated, target, messages }",
				.Flags = writes,
			},
			CommentAdd);

		RegisterOrDie(
			registry,
			CommandInfo{
				.Name = "comment.reply",
				.Summary = "Add a message to a comment thread",
				.Arguments = { id, StringArg("text", "The message").Require(), author },
				.ResultDescription = "The thread",
				.Flags = writes,
			},
			CommentReply);

		RegisterOrDie(
			registry,
			CommandInfo{
				.Name = "comment.resolve",
				.Summary = "Mark a comment thread as resolved",
				.Arguments = { id, StringArg("text", "A closing message (optional)"), author },
				.ResultDescription = "The thread",
				.Flags = writes,
			},
			[](EditorContext& context, const Json& arguments) { return CommentSetStatus(context, arguments, true); });

		RegisterOrDie(
			registry,
			CommandInfo{
				.Name = "comment.reopen",
				.Summary = "Open a resolved comment thread again",
				.Arguments = { id, StringArg("text", "A message saying why (optional)"), author },
				.ResultDescription = "The thread",
				.Flags = writes,
			},
			[](EditorContext& context, const Json& arguments) { return CommentSetStatus(context, arguments, false); });

		RegisterOrDie(
			registry,
			CommandInfo{
				.Name = "comment.list",
				.Summary = "List comment threads, oldest first",
				.Arguments = { EnumArg("status", { "Open", "Resolved" }, "Only threads with this status"),
		                       EnumArg("kind", KindNames(), "Only threads about this kind of target"),
		                       StringArg("author", "Only threads started by this author"),
		                       StringArg("scenePath", "Only threads about this scene (its project path)"),
		                       StringArg("entity", "Only threads about this entity (UUID or unique name)"),
		                       StringArg("component", "Only threads about this component type"),
		                       StringArg("asset", "Only threads about this asset path"),
		                       StringArg("file", "Only threads about this file"),
		                       StringArg("feature", "Only threads about this feature tag"),
		                       IntegerArg("limit", "At most this many threads").WithRange(1, 100000).WithDefault(200) },
				.ResultDescription =
					"{ count, total, threads: [{ id, status, author, created, updated, target, messageCount, text }] }",
				.Flags = CommandFlags::RequiresProject,
			},
			CommentList);

		RegisterOrDie(
			registry,
			CommandInfo{
				.Name = "comment.get",
				.Summary = "Show a comment thread with all its messages",
				.Arguments = { id },
				.ResultDescription = "The thread",
				.Flags = CommandFlags::RequiresProject,
			},
			CommentGet);

		RegisterOrDie(
			registry,
			CommandInfo{
				.Name = "comment.delete",
				.Summary = "Delete a comment thread and its file",
				.Arguments = { id },
				.ResultDescription = "{ deleted: id }",
				.Flags = writes,
			},
			CommentDelete);
	}
}
