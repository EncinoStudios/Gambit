#pragma once

#include "Gambit/Core/Base.h"
#include "Gambit/Core/Result.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Gambit
{
	class EditorContext;

	// One undoable change of a document (a scene, a data table) - the command pattern of D-050. The first Redo
	// performs the change and records what Undo needs from the state it finds, so a command builds an operation from
	// what it wants and does not capture anything itself; later Redos replay the change deterministically. Operations
	// refer to their document by its project-relative path, never by pointer: documents can be closed and reopened.
	class IUndoableOperation
	{
	public:
		virtual ~IUndoableOperation() = default;

		[[nodiscard]] virtual Result<void> Redo(EditorContext& context) = 0;
		[[nodiscard]] virtual Result<void> Undo(EditorContext& context) = 0;
		[[nodiscard]] virtual std::string GetDescription() const = 0;
		// Appends the documents (project-relative paths) this operation changes.
		virtual void GetDocuments(std::vector<std::string>& documents) const = 0;

		// Whether this operation can absorb `later`, the operation done right after it: both are the same kind of edit
		// of the same target (a gizmo drag sets the same transform every frame). Only asked for operations that were
		// pushed with the same merge key. Changes nothing, so that a group can check all its children first.
		[[nodiscard]] virtual bool CanMerge(const IUndoableOperation& later) const
		{
			static_cast<void>(later);
			return false;
		}
		// Absorbs `later` (CanMerge said yes): this operation now stands for both, and `later` is dropped.
		virtual void Merge(const IUndoableOperation& later) { static_cast<void>(later); }
	};

	// The undo and redo history of a project's documents (D-050): operations are done through the stack, which keeps
	// them for Undo and Redo. Features:
	//  - groups (BeginGroup/EndGroup): everything done while a group is open becomes one entry;
	//  - merging: consecutive operations pushed with the same non-empty merge key are merged if the operations agree
	//    (IUndoableOperation::CanMerge); never across a save, an undo or a redo. A group whose operations all have
	//    the same key gets that key, and merges with the next such group child by child (a drag of several entities);
	//  - dirty tracking per document: a document is dirty when its state differs from the state it was saved in, and
	//    becomes clean again when its operations are undone back to that state.
	// The history is bounded: the oldest entries are dropped beyond MaxDepth. Not thread safe.
	class UndoStack
	{
	public:
		static constexpr size_t DefaultMaxDepth = 1000;

		explicit UndoStack(size_t maxDepth = DefaultMaxDepth);
		~UndoStack();
		UndoStack(const UndoStack&) = delete;
		UndoStack& operator=(const UndoStack&) = delete;

		// Performs the operation (its first Redo) and records it. A failing operation is not recorded. Doing something
		// new discards the redo history.
		[[nodiscard]] Result<void>
		Do(EditorContext& context, Scope<IUndoableOperation> operation, std::string_view mergeKey = {});

		// Fail when there is nothing to undo or redo, or while a group is open. A failure inside an operation means the
		// documents no longer match the history (a file was changed behind its back): the history is cleared, the
		// documents stay dirty.
		[[nodiscard]] Result<void> Undo(EditorContext& context);
		[[nodiscard]] Result<void> Redo(EditorContext& context);

		[[nodiscard]] bool CanUndo() const noexcept { return !m_Undo.empty(); }
		[[nodiscard]] bool CanRedo() const noexcept { return !m_Redo.empty(); }
		// Empty when there is nothing to undo or redo.
		[[nodiscard]] std::string GetUndoDescription() const;
		[[nodiscard]] std::string GetRedoDescription() const;
		// Descriptions, the next one to undo or redo first.
		[[nodiscard]] std::vector<std::string> GetUndoHistory() const;
		[[nodiscard]] std::vector<std::string> GetRedoHistory() const;
		[[nodiscard]] size_t GetMaxDepth() const noexcept { return m_MaxDepth; }

		// ---- Groups ----

		// Groups nest. A group with exactly one entry becomes that entry, its description and merge key included (so
		// merging still works); an empty one is dropped; otherwise the group is recorded as one entry with its own
		// description. So the entry on the stack is named by the innermost group that holds several entries.
		void BeginGroup(std::string description);
		void EndGroup();
		// Undoes what the innermost open group did and drops the group (a command failed half way).
		[[nodiscard]] Result<void> CancelGroup(EditorContext& context);
		[[nodiscard]] size_t GetGroupDepth() const noexcept { return m_Groups.size(); }

		// ---- Documents ----

		[[nodiscard]] bool IsDirty(std::string_view document) const;
		// The state of the document is the saved one now.
		void MarkClean(std::string_view document);
		// The document changed in a way the history cannot undo (a new, unsaved document): it stays dirty until saved.
		void MarkDirty(std::string_view document);
		// The dirty documents, sorted.
		[[nodiscard]] std::vector<std::string> GetDirtyDocuments() const;
		// Forgets the document and every operation that changed it (the document was closed). Documents that those
		// operations also changed become dirty.
		void DiscardDocument(std::string_view document);

		// Drops the whole history; documents that were dirty stay dirty.
		void Clear();
		// Forgets everything: the history, the open groups and what is known about every document.
		void Reset();

	private:
		struct Entry
		{
			Scope<IUndoableOperation> Operation;
			std::string MergeKey;
		};

		struct OpenGroup
		{
			std::string Description;
			std::vector<Entry> Entries;
		};

		// How many of its operations a document has applied now, and how many it had when it was clean (none: the
		// clean state can no longer be reached).
		struct DocumentState
		{
			int64_t Applied = 0;
			std::optional<int64_t> Clean = 0;
		};

		void Record(Entry entry);
		void Adjust(const IUndoableOperation& operation, int64_t delta);
		void DropOldest();
		[[nodiscard]] Error
		ClearAfterFailure(std::string_view what, const Error& error, const IUndoableOperation& failed);

		std::vector<Entry> m_Undo;
		std::vector<Entry> m_Redo; // the next operation to redo is the last
		std::vector<OpenGroup> m_Groups;
		std::map<std::string, DocumentState, std::less<>> m_Documents;
		size_t m_MaxDepth;
		bool m_CanMerge = false;
	};
}
