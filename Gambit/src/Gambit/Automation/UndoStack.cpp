#include "gbpch.h"

#include "Gambit/Automation/UndoStack.h"

#include <set>

namespace Gambit
{
	namespace
	{
		std::vector<std::string> DocumentsOf(const IUndoableOperation& operation)
		{
			std::vector<std::string> documents;
			operation.GetDocuments(documents);
			std::ranges::sort(documents);
			const auto duplicates = std::ranges::unique(documents);
			documents.erase(duplicates.begin(), duplicates.end());
			return documents;
		}

		bool Touches(const IUndoableOperation& operation, std::string_view document)
		{
			const std::vector<std::string> documents = DocumentsOf(operation);
			return std::ranges::find(documents, document) != documents.end();
		}

		// Everything done while a group was open, as one entry of the history.
		class GroupOperation final : public IUndoableOperation
		{
		public:
			GroupOperation(std::string description, std::vector<Scope<IUndoableOperation>> children)
				: m_Description(std::move(description)), m_Children(std::move(children))
			{}

			Result<void> Redo(EditorContext& context) override
			{
				for (size_t index = 0; index < m_Children.size(); ++index)
				{
					if (Result<void> done = m_Children[index]->Redo(context); !done)
					{
						RollBack(context, index, true);
						return done;
					}
				}
				return {};
			}

			Result<void> Undo(EditorContext& context) override
			{
				for (size_t index = m_Children.size(); index > 0; --index)
				{
					if (Result<void> undone = m_Children[index - 1]->Undo(context); !undone)
					{
						RollBack(context, index - 1, false);
						return undone;
					}
				}
				return {};
			}

			std::string GetDescription() const override { return m_Description; }

			void GetDocuments(std::vector<std::string>& documents) const override
			{
				for (const Scope<IUndoableOperation>& child : m_Children)
				{
					child->GetDocuments(documents);
				}
			}

			// Another group of the same shape whose children each merge into this group's child at their position.
			bool CanMerge(const IUndoableOperation& later) const override
			{
				const auto* next = dynamic_cast<const GroupOperation*>(&later);
				if (next == nullptr || next->m_Children.size() != m_Children.size())
				{
					return false;
				}
				for (size_t index = 0; index < m_Children.size(); ++index)
				{
					if (!m_Children[index]->CanMerge(*next->m_Children[index]))
					{
						return false;
					}
				}
				return true;
			}

			void Merge(const IUndoableOperation& later) override
			{
				const auto& next = static_cast<const GroupOperation&>(later);
				for (size_t index = 0; index < m_Children.size(); ++index)
				{
					m_Children[index]->Merge(*next.m_Children[index]);
				}
			}

		private:
			// The child at `position` failed: best effort to put the children that had already been changed back (those
			// before it after a failed Redo, those after it after a failed Undo), so that the failure leaves the
			// documents as they were.
			void RollBack(EditorContext& context, size_t position, bool wasRedo)
			{
				if (wasRedo)
				{
					for (size_t index = position; index > 0; --index)
					{
						[[maybe_unused]] const Result<void> undone = m_Children[index - 1]->Undo(context);
					}
				}
				else
				{
					for (size_t index = position + 1; index < m_Children.size(); ++index)
					{
						[[maybe_unused]] const Result<void> redone = m_Children[index]->Redo(context);
					}
				}
			}

			std::string m_Description;
			std::vector<Scope<IUndoableOperation>> m_Children;
		};
	}

	UndoStack::UndoStack(size_t maxDepth)
		: m_MaxDepth(std::max<size_t>(maxDepth, 1))
	{}

	UndoStack::~UndoStack() = default;

	Result<void> UndoStack::Do(EditorContext& context, Scope<IUndoableOperation> operation, std::string_view mergeKey)
	{
		GB_CORE_ASSERT(operation != nullptr, "UndoStack::Do needs an operation");
		if (Result<void> done = operation->Redo(context); !done)
		{
			return done;
		}
		Entry entry{ std::move(operation), std::string(mergeKey) };
		if (!m_Groups.empty())
		{
			m_Groups.back().Entries.push_back(std::move(entry));
			return {};
		}
		Record(std::move(entry));
		return {};
	}

	void UndoStack::Record(Entry entry)
	{
		if (!m_Redo.empty())
		{
			m_Redo.clear();
			// A saved state that lay in the discarded redo history can never be reached again.
			for (auto& [name, state] : m_Documents)
			{
				if (state.Clean.has_value() && *state.Clean > state.Applied)
				{
					state.Clean.reset();
				}
			}
		}
		if (!entry.MergeKey.empty() && m_CanMerge && !m_Undo.empty() && m_Undo.back().MergeKey == entry.MergeKey
		    && m_Undo.back().Operation->CanMerge(*entry.Operation))
		{
			m_Undo.back().Operation->Merge(*entry.Operation);
			return;
		}
		Adjust(*entry.Operation, 1);
		m_Undo.push_back(std::move(entry));
		m_CanMerge = true;
		while (m_Undo.size() > m_MaxDepth)
		{
			DropOldest();
		}
	}

	void UndoStack::Adjust(const IUndoableOperation& operation, int64_t delta)
	{
		for (const std::string& document : DocumentsOf(operation))
		{
			m_Documents[document].Applied += delta;
		}
	}

	void UndoStack::DropOldest()
	{
		for (const std::string& document : DocumentsOf(*m_Undo.front().Operation))
		{
			DocumentState& state = m_Documents[document];
			--state.Applied;
			// The state before the dropped operation can no longer be reached by undoing.
			state.Clean =
				state.Clean.has_value() && *state.Clean > 0 ? std::optional<int64_t>(*state.Clean - 1) : std::nullopt;
		}
		m_Undo.erase(m_Undo.begin());
	}

	Error UndoStack::ClearAfterFailure(std::string_view what, const Error& error, const IUndoableOperation& failed)
	{
		// The documents the operation was working on may be half changed: they differ from what was saved.
		for (const std::string& document : DocumentsOf(failed))
		{
			MarkDirty(document);
		}
		Clear();
		GB_CORE_ERROR("{} failed: {}", what, error.Message);
		return Error::Format("cannot {}: {} (the undo history was cleared)", what, error.Message);
	}

	Result<void> UndoStack::Undo(EditorContext& context)
	{
		if (!m_Groups.empty())
		{
			return Error("cannot undo while a group of operations is open");
		}
		if (m_Undo.empty())
		{
			return Error("there is nothing to undo");
		}
		Entry entry = std::move(m_Undo.back());
		m_Undo.pop_back();
		const std::string description = entry.Operation->GetDescription();
		if (Result<void> undone = entry.Operation->Undo(context); !undone)
		{
			return ClearAfterFailure(std::format("undo '{}'", description), undone.GetError(), *entry.Operation);
		}
		Adjust(*entry.Operation, -1);
		m_Redo.push_back(std::move(entry));
		m_CanMerge = false;
		return {};
	}

	Result<void> UndoStack::Redo(EditorContext& context)
	{
		if (!m_Groups.empty())
		{
			return Error("cannot redo while a group of operations is open");
		}
		if (m_Redo.empty())
		{
			return Error("there is nothing to redo");
		}
		Entry entry = std::move(m_Redo.back());
		m_Redo.pop_back();
		const std::string description = entry.Operation->GetDescription();
		if (Result<void> redone = entry.Operation->Redo(context); !redone)
		{
			return ClearAfterFailure(std::format("redo '{}'", description), redone.GetError(), *entry.Operation);
		}
		Adjust(*entry.Operation, 1);
		m_Undo.push_back(std::move(entry));
		m_CanMerge = false;
		return {};
	}

	std::string UndoStack::GetUndoDescription() const
	{
		return m_Undo.empty() ? std::string() : m_Undo.back().Operation->GetDescription();
	}

	std::string UndoStack::GetRedoDescription() const
	{
		return m_Redo.empty() ? std::string() : m_Redo.back().Operation->GetDescription();
	}

	std::vector<std::string> UndoStack::GetUndoHistory() const
	{
		std::vector<std::string> history;
		history.reserve(m_Undo.size());
		for (auto entry = m_Undo.rbegin(); entry != m_Undo.rend(); ++entry)
		{
			history.push_back(entry->Operation->GetDescription());
		}
		return history;
	}

	std::vector<std::string> UndoStack::GetRedoHistory() const
	{
		std::vector<std::string> history;
		history.reserve(m_Redo.size());
		for (auto entry = m_Redo.rbegin(); entry != m_Redo.rend(); ++entry)
		{
			history.push_back(entry->Operation->GetDescription());
		}
		return history;
	}

	// ---- Groups ----

	void UndoStack::BeginGroup(std::string description)
	{
		m_Groups.push_back({ std::move(description), {} });
	}

	void UndoStack::EndGroup()
	{
		GB_CORE_ASSERT(!m_Groups.empty(), "EndGroup without BeginGroup");
		OpenGroup group = std::move(m_Groups.back());
		m_Groups.pop_back();
		if (group.Entries.empty())
		{
			return;
		}

		Entry entry;
		if (group.Entries.size() == 1)
		{
			entry = std::move(group.Entries.front());
		}
		else
		{
			// Operations that were all pushed with one key make a group that merges with the next one like it.
			const std::string& key = group.Entries.front().MergeKey;
			const bool sameKey =
				std::ranges::all_of(group.Entries, [&](const Entry& child) { return child.MergeKey == key; });
			entry.MergeKey = sameKey ? key : std::string();
			std::vector<Scope<IUndoableOperation>> children;
			children.reserve(group.Entries.size());
			for (Entry& child : group.Entries)
			{
				children.push_back(std::move(child.Operation));
			}
			entry.Operation = CreateScope<GroupOperation>(std::move(group.Description), std::move(children));
		}
		if (!m_Groups.empty())
		{
			m_Groups.back().Entries.push_back(std::move(entry));
		}
		else
		{
			Record(std::move(entry));
		}
	}

	Result<void> UndoStack::CancelGroup(EditorContext& context)
	{
		if (m_Groups.empty())
		{
			return Error("there is no open group to cancel");
		}
		OpenGroup group = std::move(m_Groups.back());
		m_Groups.pop_back();
		for (auto entry = group.Entries.rbegin(); entry != group.Entries.rend(); ++entry)
		{
			if (Result<void> undone = entry->Operation->Undo(context); !undone)
			{
				return ClearAfterFailure("roll back a failed command", undone.GetError(), *entry->Operation);
			}
		}
		return {};
	}

	// ---- Documents ----

	bool UndoStack::IsDirty(std::string_view document) const
	{
		const auto found = m_Documents.find(document);
		return found != m_Documents.end()
			&& (!found->second.Clean.has_value() || *found->second.Clean != found->second.Applied);
	}

	void UndoStack::MarkClean(std::string_view document)
	{
		DocumentState& state = m_Documents[std::string(document)];
		state.Clean = state.Applied;
		m_CanMerge = false; // an edit after the save must not change what was saved
	}

	void UndoStack::MarkDirty(std::string_view document)
	{
		m_Documents[std::string(document)].Clean.reset();
	}

	std::vector<std::string> UndoStack::GetDirtyDocuments() const
	{
		std::vector<std::string> dirty;
		for (const auto& [name, state] : m_Documents)
		{
			if (IsDirty(name))
			{
				dirty.push_back(name);
			}
		}
		return dirty;
	}

	void UndoStack::DiscardDocument(std::string_view document)
	{
		std::set<std::string> affected;
		const auto drop = [&](std::vector<Entry>& entries) {
			std::erase_if(entries, [&](const Entry& entry) {
				if (!Touches(*entry.Operation, document))
				{
					return false;
				}
				for (const std::string& other : DocumentsOf(*entry.Operation))
				{
					affected.insert(other);
				}
				return true;
			});
		};
		drop(m_Undo);
		drop(m_Redo);
		if (const auto state = m_Documents.find(document); state != m_Documents.end())
		{
			m_Documents.erase(state);
		}
		affected.erase(std::string(document));
		for (const std::string& other : affected)
		{
			// The dropped operations are gone from the middle of the history: what the other document looked like when
			// it was saved is not tracked any more.
			DocumentState& state = m_Documents[other];
			state.Applied = static_cast<int64_t>(
				std::ranges::count_if(m_Undo, [&](const Entry& entry) { return Touches(*entry.Operation, other); }));
			state.Clean.reset();
		}
		m_CanMerge = false;
	}

	void UndoStack::Reset()
	{
		m_Undo.clear();
		m_Redo.clear();
		m_Groups.clear();
		m_Documents.clear();
		m_CanMerge = false;
	}

	void UndoStack::Clear()
	{
		for (auto& [name, state] : m_Documents)
		{
			const bool dirty = !state.Clean.has_value() || *state.Clean != state.Applied;
			state.Applied = 0;
			state.Clean = dirty ? std::nullopt : std::optional<int64_t>(0);
		}
		m_Undo.clear();
		m_Redo.clear();
		m_CanMerge = false;
	}
}
