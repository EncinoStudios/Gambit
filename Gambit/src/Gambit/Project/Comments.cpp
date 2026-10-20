#include "gbpch.h"

#include "Gambit/Project/Comments.h"

#include "Gambit/Core/FileSystem.h"
#include "Gambit/Core/Utf8.h"
#include "Gambit/Reflection/FieldInfo.h"
#include "Gambit/Reflection/TypeRegistry.h"
#include "Gambit/Serialization/FieldSerializer.h"
#include "Gambit/Serialization/Migration.h"

#include <cctype>
#include <ctime>

namespace Gambit
{
	namespace
	{
		constexpr const char* FormatName = "comment thread";
		constexpr std::string_view FileExtension = ".json";
		constexpr size_t MaxAuthorLength = 128;
		constexpr size_t MaxFeatureLength = 64;

		bool HasControlCharacters(std::string_view text) noexcept
		{
			return std::ranges::any_of(text, [](char c) {
				const auto u = static_cast<unsigned char>(c);
				return u < 0x20 || u == 0x7F;
			});
		}

		Result<void> ValidatePath(std::string_view what, std::string_view path)
		{
			Result<std::string> normalized = FileSystem::NormalizePath(path);
			if (!normalized || normalized->empty() || FileSystem::IsAbsolutePath(path) || *normalized != path)
			{
				return Error::Format("the {} '{}' is not a normalized project-relative path", what, path);
			}
			return {};
		}

		Result<void> ValidateAuthor(std::string_view author)
		{
			if (author.empty() || author.size() > MaxAuthorLength || !IsValidUtf8(author)
			    || HasControlCharacters(author))
			{
				return Error::Format("the author must be 1 to {} characters of printable UTF-8 text", MaxAuthorLength);
			}
			return {};
		}

		Result<void> ValidateText(std::string_view text)
		{
			if (text.empty() || text.size() > CommentStore::MaxTextLength || !IsValidUtf8(text))
			{
				return Error::Format(
					"a comment must be 1 to {} bytes of valid UTF-8 text", CommentStore::MaxTextLength);
			}
			return {};
		}

		bool IsValidThreadId(std::string_view id) noexcept
		{
			if (id.size() != 18 || !id.starts_with("0x"))
			{
				return false;
			}
			return std::ranges::all_of(
					   id.substr(2), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)); })
				&& UUID::Parse(id).has_value();
		}

		// "2026-03-14T09:26:53Z": the shape only, which is all a reader needs to sort and display.
		bool IsValidTimestamp(std::string_view text) noexcept
		{
			constexpr std::string_view Pattern = "dddd-dd-ddTdd:dd:ddZ";
			if (text.size() != Pattern.size())
			{
				return false;
			}
			for (size_t index = 0; index < text.size(); ++index)
			{
				const bool digit = Pattern[index] == 'd';
				if (digit ? !std::isdigit(static_cast<unsigned char>(text[index])) : text[index] != Pattern[index])
				{
					return false;
				}
			}
			return true;
		}

		// A Vec3 field description, so that positions are written and read exactly like component fields.
		const FieldInfo& PositionField()
		{
			static const FieldInfo s_Field = [] {
				FieldInfo field;
				field.Name = "Position";
				field.Type = FieldType::Vec3;
				field.Default = glm::vec3(0.0f);
				return field;
			}();
			return s_Field;
		}

		Result<Json> TargetToJson(const CommentTarget& target)
		{
			Json object = Json::object();
			object["Kind"] = std::string(ToString(target.Kind));
			switch (target.Kind)
			{
				case CommentTargetKind::Entity:
					object["Scene"] = target.Scene;
					object["Entity"] = target.Entity.ToString();
					break;
				case CommentTargetKind::Component:
					object["Scene"] = target.Scene;
					object["Entity"] = target.Entity.ToString();
					object["Component"] = target.Component;
					break;
				case CommentTargetKind::Asset:
					if (!target.Asset.empty())
					{
						object["Asset"] = target.Asset;
					}
					if (target.AssetHandle.IsValid())
					{
						object["Handle"] = target.AssetHandle.ToString();
					}
					break;
				case CommentTargetKind::File:
					object["File"] = target.File;
					if (target.Line != 0)
					{
						object["Line"] = target.Line;
					}
					break;
				case CommentTargetKind::Feature:
					object["Feature"] = target.Feature;
					break;
				case CommentTargetKind::Position:
				{
					object["Scene"] = target.Scene;
					Result<Json> position =
						FieldValueToJson(PositionField(), FieldValue(target.Position), "/Target/Position");
					if (!position)
					{
						return position.GetError();
					}
					object["Position"] = std::move(*position);
					break;
				}
			}
			return object;
		}

		Result<std::string> ReadString(const Json& json, std::string_view path)
		{
			if (!json.is_string())
			{
				return JsonTypeError(path, "a string", json);
			}
			return json.get<std::string>();
		}

		Result<UUID> ReadUuid(const Json& json, std::string_view path)
		{
			if (!json.is_string())
			{
				return JsonTypeError(path, "a UUID string like \"0x00000000DEADBEEF\"", json);
			}
			const std::optional<UUID> uuid = UUID::Parse(json.get_ref<const std::string&>());
			if (!uuid.has_value())
			{
				return MakePathError(path, std::format("'{}' is not a UUID", json.get_ref<const std::string&>()));
			}
			return *uuid;
		}

		Result<CommentTarget> TargetFromJson(const Json& json, std::string_view path, LoadReport& report)
		{
			if (!json.is_object())
			{
				return JsonTypeError(path, "an object", json);
			}
			if (!json.contains("Kind"))
			{
				return MakePathError(path, "the target has no \"Kind\"");
			}
			Result<std::string> kindName = ReadString(json.at("Kind"), JsonPathAppend(path, "Kind"));
			if (!kindName)
			{
				return kindName.GetError();
			}
			const std::optional<CommentTargetKind> kind = ParseCommentTargetKind(*kindName);
			if (!kind.has_value())
			{
				return MakePathError(JsonPathAppend(path, "Kind"), std::format("unknown target kind '{}'", *kindName));
			}

			CommentTarget target;
			target.Kind = *kind;
			for (const auto& [key, member] : json.items())
			{
				const std::string memberPath = JsonPathAppend(path, key);
				if (key == "Kind")
				{
					continue;
				}
				if (key == "Scene")
				{
					Result<std::string> value = ReadString(member, memberPath);
					if (!value)
					{
						return value.GetError();
					}
					target.Scene = std::move(*value);
				}
				else if (key == "Entity")
				{
					Result<UUID> value = ReadUuid(member, memberPath);
					if (!value)
					{
						return value.GetError();
					}
					target.Entity = *value;
				}
				else if (key == "Component")
				{
					Result<std::string> value = ReadString(member, memberPath);
					if (!value)
					{
						return value.GetError();
					}
					target.Component = std::move(*value);
				}
				else if (key == "Asset")
				{
					Result<std::string> value = ReadString(member, memberPath);
					if (!value)
					{
						return value.GetError();
					}
					target.Asset = std::move(*value);
				}
				else if (key == "Handle")
				{
					Result<UUID> value = ReadUuid(member, memberPath);
					if (!value)
					{
						return value.GetError();
					}
					target.AssetHandle = *value;
				}
				else if (key == "File")
				{
					Result<std::string> value = ReadString(member, memberPath);
					if (!value)
					{
						return value.GetError();
					}
					target.File = std::move(*value);
				}
				else if (key == "Line")
				{
					Result<uint32_t> value = ReadJsonInteger<uint32_t>(member, memberPath);
					if (!value)
					{
						return value.GetError();
					}
					target.Line = *value;
				}
				else if (key == "Feature")
				{
					Result<std::string> value = ReadString(member, memberPath);
					if (!value)
					{
						return value.GetError();
					}
					target.Feature = std::move(*value);
				}
				else if (key == "Position")
				{
					Result<FieldValue> value = FieldValueFromJson(PositionField(), member, memberPath, report);
					if (!value)
					{
						return value.GetError();
					}
					target.Position = std::get<glm::vec3>(*value);
				}
				else
				{
					report.Warn(memberPath, "unknown member, ignored");
				}
			}
			if (Result<void> valid = ValidateCommentTarget(target); !valid)
			{
				return MakePathError(path, valid.GetError().Message);
			}
			return target;
		}

		Result<CommentMessage> MessageFromJson(const Json& json, std::string_view path, LoadReport& report)
		{
			if (!json.is_object())
			{
				return JsonTypeError(path, "an object", json);
			}
			CommentMessage message;
			bool hasAuthor = false;
			bool hasText = false;
			bool hasTimestamp = false;
			for (const auto& [key, member] : json.items())
			{
				const std::string memberPath = JsonPathAppend(path, key);
				std::string* destination = key == "Author" ? &message.Author
					: key == "Text"                        ? &message.Text
					: key == "Timestamp"                   ? &message.Timestamp
														   : nullptr;
				if (destination == nullptr)
				{
					report.Warn(memberPath, "unknown member, ignored");
					continue;
				}
				Result<std::string> value = ReadString(member, memberPath);
				if (!value)
				{
					return value.GetError();
				}
				*destination = std::move(*value);
				hasAuthor = hasAuthor || key == "Author";
				hasText = hasText || key == "Text";
				hasTimestamp = hasTimestamp || key == "Timestamp";
			}
			if (!hasAuthor || !hasText || !hasTimestamp)
			{
				return MakePathError(path, "a message needs \"Author\", \"Text\" and \"Timestamp\"");
			}
			if (Result<void> valid = ValidateAuthor(message.Author); !valid)
			{
				return MakePathError(path, valid.GetError().Message);
			}
			if (Result<void> valid = ValidateText(message.Text); !valid)
			{
				return MakePathError(path, valid.GetError().Message);
			}
			if (!IsValidTimestamp(message.Timestamp))
			{
				return MakePathError(
					JsonPathAppend(path, "Timestamp"), "expected a UTC time like 2026-03-14T09:26:53Z");
			}
			return message;
		}

		bool Matches(const CommentThread& thread, const CommentQuery& query) noexcept
		{
			const CommentTarget& target = thread.Target;
			return (!query.Kind.has_value() || *query.Kind == target.Kind)
				&& (!query.Status.has_value() || *query.Status == thread.Status)
				&& (query.Author.empty() || query.Author == thread.Author)
				&& (query.Scene.empty() || query.Scene == target.Scene)
				&& (!query.Entity.IsValid() || query.Entity == target.Entity)
				&& (query.Component.empty() || query.Component == target.Component)
				&& (query.Asset.empty() || query.Asset == target.Asset)
				&& (!query.AssetHandle.IsValid() || query.AssetHandle == target.AssetHandle)
				&& (query.File.empty() || query.File == target.File)
				&& (query.Feature.empty() || query.Feature == target.Feature);
		}
	}

	std::string_view ToString(CommentTargetKind kind) noexcept
	{
		switch (kind)
		{
			case CommentTargetKind::Entity:
				return "Entity";
			case CommentTargetKind::Component:
				return "Component";
			case CommentTargetKind::Asset:
				return "Asset";
			case CommentTargetKind::File:
				return "File";
			case CommentTargetKind::Feature:
				return "Feature";
			case CommentTargetKind::Position:
				return "Position";
		}
		return "Unknown";
	}

	std::optional<CommentTargetKind> ParseCommentTargetKind(std::string_view name) noexcept
	{
		for (const CommentTargetKind kind :
		     { CommentTargetKind::Entity, CommentTargetKind::Component, CommentTargetKind::Asset,
		       CommentTargetKind::File, CommentTargetKind::Feature, CommentTargetKind::Position })
		{
			if (ToString(kind) == name)
			{
				return kind;
			}
		}
		return std::nullopt;
	}

	std::string_view ToString(CommentStatus status) noexcept
	{
		return status == CommentStatus::Open ? "Open" : "Resolved";
	}

	std::optional<CommentStatus> ParseCommentStatus(std::string_view name) noexcept
	{
		if (name == "Open")
		{
			return CommentStatus::Open;
		}
		if (name == "Resolved")
		{
			return CommentStatus::Resolved;
		}
		return std::nullopt;
	}

	std::string FormatTimestampUtc(std::chrono::system_clock::time_point time)
	{
		const std::time_t seconds = std::chrono::system_clock::to_time_t(time);
		std::tm utc{};
#if defined(GB_PLATFORM_WINDOWS)
		gmtime_s(&utc, &seconds);
#else
		gmtime_r(&seconds, &utc);
#endif
		return std::format(
			"{:04}-{:02}-{:02}T{:02}:{:02}:{:02}Z", utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday, utc.tm_hour,
			utc.tm_min, utc.tm_sec);
	}

	Result<void> ValidateCommentTarget(const CommentTarget& target)
	{
		const auto needScene = [&]() -> Result<void> {
			if (target.Scene.empty())
			{
				return Error::Format("{} comments need the scene", ToString(target.Kind));
			}
			return ValidatePath("scene", target.Scene);
		};
		switch (target.Kind)
		{
			case CommentTargetKind::Entity:
			case CommentTargetKind::Component:
			{
				if (Result<void> scene = needScene(); !scene)
				{
					return scene;
				}
				if (!target.Entity.IsValid())
				{
					return Error::Format("{} comments need the entity", ToString(target.Kind));
				}
				if (target.Kind == CommentTargetKind::Component && !TypeRegistry::IsValidName(target.Component))
				{
					return Error("Component comments need the name of a component");
				}
				return {};
			}
			case CommentTargetKind::Asset:
				if (target.Asset.empty() && !target.AssetHandle.IsValid())
				{
					return Error("Asset comments need the asset path or handle");
				}
				return target.Asset.empty() ? Result<void>() : ValidatePath("asset", target.Asset);
			case CommentTargetKind::File:
				if (target.File.empty())
				{
					return Error("File comments need the file");
				}
				return ValidatePath("file", target.File);
			case CommentTargetKind::Feature:
				if (target.Feature.empty() || target.Feature.size() > MaxFeatureLength || !IsValidUtf8(target.Feature)
				    || HasControlCharacters(target.Feature))
				{
					return Error::Format("a feature tag is 1 to {} characters of printable text", MaxFeatureLength);
				}
				return {};
			case CommentTargetKind::Position:
				if (Result<void> scene = needScene(); !scene)
				{
					return scene;
				}
				if (!std::isfinite(target.Position.x) || !std::isfinite(target.Position.y)
				    || !std::isfinite(target.Position.z))
				{
					return Error("the position of a Position comment must be finite");
				}
				return {};
		}
		return Error("unknown comment target kind");
	}

	// ---- CommentThread ----

	Result<Json> CommentThread::ToJson() const
	{
		Result<Json> target = TargetToJson(Target);
		if (!target)
		{
			return target.GetError();
		}
		Json object = Json::object();
		object["Version"] = FormatVersion;
		object["Id"] = Id;
		object["Target"] = std::move(*target);
		object["Author"] = Author;
		object["Status"] = std::string(ToString(Status));
		object["Created"] = Created;
		object["Updated"] = Updated;
		Json messages = Json::array();
		for (const CommentMessage& message : Messages)
		{
			Json entry = Json::object();
			entry["Author"] = message.Author;
			entry["Text"] = message.Text;
			entry["Timestamp"] = message.Timestamp;
			messages.push_back(std::move(entry));
		}
		object["Messages"] = std::move(messages);
		return object;
	}

	Result<CommentThread> CommentThread::FromJson(const Json& json, std::string_view source, LoadReport& report)
	{
		Json document = json;
		if (Result<void> upgraded = GetNoMigrations().Upgrade(document, FormatVersion, FormatName); !upgraded)
		{
			return AddSource(source, upgraded.GetError());
		}

		CommentThread thread;
		bool hasTarget = false;
		bool hasMessages = false;
		for (const auto& [key, member] : document.items())
		{
			const std::string path = JsonPathAppend({}, key);
			if (key == "Version")
			{
				continue;
			}
			if (key == "Id" || key == "Author" || key == "Created" || key == "Updated" || key == "Status")
			{
				Result<std::string> value = ReadString(member, path);
				if (!value)
				{
					return AddSource(source, value.GetError());
				}
				if (key == "Id")
				{
					thread.Id = std::move(*value);
				}
				else if (key == "Author")
				{
					thread.Author = std::move(*value);
				}
				else if (key == "Created")
				{
					thread.Created = std::move(*value);
				}
				else if (key == "Updated")
				{
					thread.Updated = std::move(*value);
				}
				else
				{
					const std::optional<CommentStatus> status = ParseCommentStatus(*value);
					if (!status.has_value())
					{
						return AddSource(source, MakePathError(path, std::format("unknown status '{}'", *value)));
					}
					thread.Status = *status;
				}
			}
			else if (key == "Target")
			{
				Result<CommentTarget> target = TargetFromJson(member, path, report);
				if (!target)
				{
					return AddSource(source, target.GetError());
				}
				thread.Target = std::move(*target);
				hasTarget = true;
			}
			else if (key == "Messages")
			{
				if (!member.is_array())
				{
					return AddSource(source, JsonTypeError(path, "an array", member));
				}
				for (size_t index = 0; index < member.size(); ++index)
				{
					Result<CommentMessage> message =
						MessageFromJson(member[index], JsonPathAppend(path, index), report);
					if (!message)
					{
						return AddSource(source, message.GetError());
					}
					thread.Messages.push_back(std::move(*message));
				}
				hasMessages = true;
			}
			else
			{
				report.Warn(path, "unknown member, ignored");
			}
		}
		if (!IsValidThreadId(thread.Id))
		{
			return AddSource(source, MakePathError("/Id", "expected an id like \"0x00000000DEADBEEF\""));
		}
		if (!hasTarget || !hasMessages || thread.Messages.empty())
		{
			return AddSource(source, Error("a thread needs a \"Target\" and at least one message"));
		}
		if (!IsValidTimestamp(thread.Created) || !IsValidTimestamp(thread.Updated))
		{
			return AddSource(source, Error("\"Created\" and \"Updated\" must be UTC times like 2026-03-14T09:26:53Z"));
		}
		if (Result<void> valid = ValidateAuthor(thread.Author); !valid)
		{
			return AddSource(source, MakePathError("/Author", valid.GetError().Message));
		}
		return thread;
	}

	// ---- CommentStore ----

	CommentStore::CommentStore(VFS& vfs, std::string directory)
		: m_Vfs(vfs), m_Directory(std::move(directory)), m_Clock([] { return std::chrono::system_clock::now(); })
	{}

	void CommentStore::SetClock(Clock clock)
	{
		GB_CORE_ASSERT(clock != nullptr, "the clock of a CommentStore must not be empty");
		m_Clock = std::move(clock);
	}

	std::string CommentStore::Now() const
	{
		return FormatTimestampUtc(m_Clock());
	}

	std::string CommentStore::GetThreadPath(std::string_view id) const
	{
		return std::format("{}/{}{}", m_Directory, id, FileExtension);
	}

	Result<void> CommentStore::Load(LoadReport& report)
	{
		m_Threads.clear();
		if (!m_Vfs.Exists(m_Directory))
		{
			return {};
		}
		Result<std::vector<DirectoryEntry>> entries = m_Vfs.List(m_Directory, false);
		if (!entries)
		{
			return AddSource(m_Directory, entries.GetError());
		}
		for (const DirectoryEntry& entry : *entries)
		{
			if (entry.IsDirectory || !entry.Path.ends_with(FileExtension))
			{
				continue;
			}
			Result<std::string> text = m_Vfs.ReadText(entry.Path);
			if (!text)
			{
				report.Warn(entry.Path, text.GetError().Message);
				continue;
			}
			Result<Json> document = ParseJson(*text, entry.Path);
			if (!document)
			{
				report.Warn(entry.Path, document.GetError().Message);
				continue;
			}
			LoadReport threadReport;
			Result<CommentThread> thread = CommentThread::FromJson(*document, entry.Path, threadReport);
			for (const Diagnostic& warning : threadReport.GetWarnings())
			{
				report.Warn(entry.Path + warning.Path, warning.Message);
			}
			if (!thread)
			{
				report.Warn(entry.Path, thread.GetError().Message);
				continue;
			}
			if (GetThreadPath(thread->Id) != entry.Path)
			{
				report.Warn(
					entry.Path, std::format("the file name does not match the thread id {}, ignored", thread->Id));
				continue;
			}
			m_Threads.emplace(thread->Id, std::move(*thread));
		}
		return {};
	}

	Result<void> CommentStore::Save(const CommentThread& thread)
	{
		if (!m_WritesEnabled)
		{
			return {};
		}
		Result<Json> document = thread.ToJson();
		if (!document)
		{
			return document.GetError();
		}
		Result<std::string> text = DumpJson(*document);
		if (!text)
		{
			return text.GetError();
		}
		return m_Vfs.WriteText(GetThreadPath(thread.Id), *text);
	}

	Result<CommentThread> CommentStore::Add(CommentTarget target, std::string_view author, std::string_view text)
	{
		if (Result<void> valid = ValidateCommentTarget(target); !valid)
		{
			return valid.GetError();
		}
		if (Result<void> valid = ValidateAuthor(author); !valid)
		{
			return valid.GetError();
		}
		if (Result<void> valid = ValidateText(text); !valid)
		{
			return valid.GetError();
		}

		CommentThread thread;
		do
		{
			thread.Id = UUID::Generate().ToString();
		} while (m_Threads.contains(thread.Id));
		thread.Target = std::move(target);
		thread.Author = std::string(author);
		thread.Created = Now();
		thread.Updated = thread.Created;
		thread.Messages.push_back({ std::string(author), std::string(text), thread.Created });
		if (Result<void> saved = Save(thread); !saved)
		{
			return saved.GetError();
		}
		m_Threads.emplace(thread.Id, thread);
		return thread;
	}

	CommentThread* CommentStore::FindMutable(std::string_view id)
	{
		const auto found = m_Threads.find(id);
		return found == m_Threads.end() ? nullptr : &found->second;
	}

	const CommentThread* CommentStore::Find(std::string_view id) const
	{
		const auto found = m_Threads.find(id);
		return found == m_Threads.end() ? nullptr : &found->second;
	}

	Result<CommentThread> CommentStore::Append(
		std::string_view id, std::string_view author, std::string_view text, std::optional<CommentStatus> status)
	{
		CommentThread* thread = FindMutable(id);
		if (thread == nullptr)
		{
			return Error::Format("there is no comment thread '{}'", id);
		}
		if (Result<void> valid = ValidateAuthor(author); !valid)
		{
			return valid.GetError();
		}
		if (!text.empty() || !status.has_value())
		{
			if (Result<void> valid = ValidateText(text); !valid)
			{
				return valid.GetError();
			}
		}

		CommentThread changed = *thread;
		const std::string now = Now();
		if (!text.empty())
		{
			changed.Messages.push_back({ std::string(author), std::string(text), now });
		}
		if (status.has_value())
		{
			changed.Status = *status;
		}
		changed.Updated = now;
		if (Result<void> saved = Save(changed); !saved)
		{
			return saved.GetError();
		}
		*thread = changed;
		return changed;
	}

	Result<CommentThread> CommentStore::Reply(std::string_view id, std::string_view author, std::string_view text)
	{
		return Append(id, author, text, std::nullopt);
	}

	Result<CommentThread> CommentStore::Resolve(std::string_view id, std::string_view author, std::string_view text)
	{
		return Append(id, author, text, CommentStatus::Resolved);
	}

	Result<CommentThread> CommentStore::Reopen(std::string_view id, std::string_view author, std::string_view text)
	{
		return Append(id, author, text, CommentStatus::Open);
	}

	Result<void> CommentStore::Delete(std::string_view id)
	{
		const auto found = m_Threads.find(id);
		if (found == m_Threads.end())
		{
			return Error::Format("there is no comment thread '{}'", id);
		}
		if (m_WritesEnabled)
		{
			if (Result<void> removed = m_Vfs.Remove(GetThreadPath(id)); !removed)
			{
				return removed;
			}
		}
		m_Threads.erase(found);
		return {};
	}

	std::vector<const CommentThread*> CommentStore::Query(const CommentQuery& query) const
	{
		std::vector<const CommentThread*> matches;
		for (const auto& [id, thread] : m_Threads)
		{
			if (Matches(thread, query))
			{
				matches.push_back(&thread);
			}
		}
		std::ranges::stable_sort(matches, [](const CommentThread* a, const CommentThread* b) {
			return a->Created != b->Created ? a->Created < b->Created : a->Id < b->Id;
		});
		return matches;
	}
}
