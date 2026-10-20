#pragma once

#include "Gambit/Core/Base.h"
#include "Gambit/Core/Result.h"
#include "Gambit/Core/UUID.h"
#include "Gambit/Core/VFS.h"
#include "Gambit/Math/Math.h"
#include "Gambit/Serialization/Json.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Gambit
{
	// What a comment thread is attached to (D-052).
	enum class CommentTargetKind : uint8_t
	{
		Entity,    // an entity of a scene
		Component, // one component of an entity
		Asset,     // an asset, by path and/or handle
		File,      // a project file (a script, say), optionally one line
		Feature,   // a free tag for a feature or a topic
		Position   // a position in the world of a scene
	};

	[[nodiscard]] std::string_view ToString(CommentTargetKind kind) noexcept;
	[[nodiscard]] std::optional<CommentTargetKind> ParseCommentTargetKind(std::string_view name) noexcept;

	enum class CommentStatus : uint8_t
	{
		Open,
		Resolved
	};

	[[nodiscard]] std::string_view ToString(CommentStatus status) noexcept;
	[[nodiscard]] std::optional<CommentStatus> ParseCommentStatus(std::string_view name) noexcept;

	// The target of a thread. Only the members of its Kind are used (and stored); ValidateCommentTarget says which.
	struct CommentTarget
	{
		CommentTargetKind Kind = CommentTargetKind::Feature;
		std::string Scene;          // Entity, Component, Position: project-relative path of the scene
		UUID Entity;                // Entity, Component
		std::string Component;      // Component: the registered component name
		std::string Asset;          // Asset: project-relative path (empty when only the handle is known)
		UUID AssetHandle;           // Asset: the handle (null when only the path is known)
		std::string File;           // File: project-relative path
		uint32_t Line = 0;          // File: 1-based line; 0 for the file as a whole
		std::string Feature;        // Feature: a tag such as "inventory"
		glm::vec3 Position{ 0.0f }; // Position: world position in the scene

		friend bool operator==(const CommentTarget&, const CommentTarget&) noexcept = default;
	};

	// Whether the members the kind needs are set and well formed: Entity needs a scene and an entity, Component
	// additionally a component name, Asset a path or a handle, File a path, Feature a tag of 1 to 64 printable
	// characters, Position a scene and a finite position. Paths are normalized project paths.
	[[nodiscard]] Result<void> ValidateCommentTarget(const CommentTarget& target);

	struct CommentMessage
	{
		std::string Author; // a user or an agent
		std::string Text;
		std::string Timestamp; // UTC, ISO 8601: 2026-03-14T09:26:53Z

		friend bool operator==(const CommentMessage&, const CommentMessage&) noexcept = default;
	};

	// A conversation about one target: the opening comment is the first message. Stored as one JSON file,
	// <Comments directory>/<Id>.json (D-052):
	//   { "Version": 1, "Id": "0x...", "Target": { "Kind": "Entity", "Scene": "...", "Entity": "0x..." },
	//     "Author": "claude", "Status": "Open", "Created": "...", "Updated": "...",
	//     "Messages": [ { "Author": "claude", "Text": "...", "Timestamp": "..." } ] }
	// Comments are project data for people and agents, never part of exports.
	struct CommentThread
	{
		static constexpr uint32_t FormatVersion = 1;

		std::string Id; // "0x" and 16 hexadecimal digits; also the file name
		CommentTarget Target;
		std::string Author; // who opened the thread
		CommentStatus Status = CommentStatus::Open;
		std::string Created;
		std::string Updated; // time of the latest message or status change
		std::vector<CommentMessage> Messages;

		[[nodiscard]] Result<Json> ToJson() const;
		[[nodiscard]] static Result<CommentThread>
		FromJson(const Json& json, std::string_view source, LoadReport& report);
	};

	// Which threads a query returns: every member that is set must match.
	struct CommentQuery
	{
		std::optional<CommentTargetKind> Kind;
		std::optional<CommentStatus> Status;
		std::string Author; // the author of the thread
		std::string Scene;
		UUID Entity;
		std::string Component;
		std::string Asset; // a path
		UUID AssetHandle;
		std::string File;
		std::string Feature;
	};

	// The comment threads of a project, one file each (D-052). The store keeps all threads in memory (they are
	// small) and writes a thread's file whenever it changes. Not thread safe.
	class CommentStore
	{
	public:
		using Clock = std::function<std::chrono::system_clock::time_point()>;

		// `directory` is the project-relative folder of the thread files (ProjectDirectories::Comments). The VFS must
		// outlive the store.
		CommentStore(VFS& vfs, std::string directory);

		// Replaces the threads by those of the files in the directory (none when the directory does not exist). Files
		// that cannot be read are not threads: they are warnings in `report` and stay untouched.
		[[nodiscard]] Result<void> Load(LoadReport& report);

		// The wall clock that stamps messages (comments are not simulation); tests inject a fixed one.
		void SetClock(Clock clock);
		// In-memory mode for dry runs: threads change and queries see the changes, but no file is written or removed.
		void SetWritesEnabled(bool enabled) noexcept { m_WritesEnabled = enabled; }

		// Starts a thread. Fails for an invalid target, an empty author or text (at most MaxTextLength bytes of valid
		// UTF-8).
		[[nodiscard]] Result<CommentThread> Add(CommentTarget target, std::string_view author, std::string_view text);
		[[nodiscard]] Result<CommentThread> Reply(std::string_view id, std::string_view author, std::string_view text);
		// Closes the thread, with an optional last message (`text` empty: none).
		[[nodiscard]] Result<CommentThread>
		Resolve(std::string_view id, std::string_view author, std::string_view text = {});
		[[nodiscard]] Result<CommentThread>
		Reopen(std::string_view id, std::string_view author, std::string_view text = {});
		[[nodiscard]] Result<void> Delete(std::string_view id);

		// nullptr when there is no such thread.
		[[nodiscard]] const CommentThread* Find(std::string_view id) const;
		[[nodiscard]] size_t GetCount() const noexcept { return m_Threads.size(); }
		// The threads that match, oldest first (by creation time, then id).
		[[nodiscard]] std::vector<const CommentThread*> Query(const CommentQuery& query = {}) const;

		[[nodiscard]] const std::string& GetDirectory() const noexcept { return m_Directory; }
		// The virtual path of the file of thread `id`.
		[[nodiscard]] std::string GetThreadPath(std::string_view id) const;

		static constexpr size_t MaxTextLength = 16 * 1024;

	private:
		[[nodiscard]] Result<void> Save(const CommentThread& thread);
		[[nodiscard]] CommentThread* FindMutable(std::string_view id);
		[[nodiscard]] Result<CommentThread> Append(
			std::string_view id, std::string_view author, std::string_view text, std::optional<CommentStatus> status);
		[[nodiscard]] std::string Now() const;

		VFS& m_Vfs;
		std::string m_Directory;
		std::map<std::string, CommentThread, std::less<>> m_Threads; // by id
		Clock m_Clock;
		bool m_WritesEnabled = true;
	};

	// "2026-03-14T09:26:53Z" for a time point (UTC, whole seconds).
	[[nodiscard]] std::string FormatTimestampUtc(std::chrono::system_clock::time_point time);
}
