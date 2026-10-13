#include <doctest/doctest.h>

#include "TestSupport.h"

#include "Gambit/Automation/EditorContext.h"
#include "Gambit/Automation/UndoStack.h"

#include <string>
#include <vector>

using namespace Gambit;
using namespace Gambit::Tests;

namespace
{
	// Adds a number to a counter; the simplest undoable change.
	class AddOperation final : public IUndoableOperation
	{
	public:
		AddOperation(int& counter, int amount, std::string document = "A.gscene", std::string description = "")
			: m_Counter(counter),
			  m_Amount(amount),
			  m_Document(std::move(document)),
			  m_Description(description.empty() ? "add " + std::to_string(amount) : std::move(description))
		{}

		Result<void> Redo(EditorContext&) override
		{
			if (FailRedo)
			{
				return Error("redo refused");
			}
			m_Counter += m_Amount;
			return {};
		}

		Result<void> Undo(EditorContext&) override
		{
			if (FailUndo)
			{
				return Error("undo refused");
			}
			m_Counter -= m_Amount;
			return {};
		}

		std::string GetDescription() const override { return m_Description; }
		void GetDocuments(std::vector<std::string>& documents) const override { documents.push_back(m_Document); }

		bool TryMerge(const IUndoableOperation& later) override
		{
			const auto* next = dynamic_cast<const AddOperation*>(&later);
			if (next == nullptr || next->m_Document != m_Document)
			{
				return false;
			}
			m_Amount += next->m_Amount;
			m_Description = "add " + std::to_string(m_Amount);
			return true;
		}

		bool FailUndo = false;
		bool FailRedo = false;

	private:
		int& m_Counter;
		int m_Amount;
		std::string m_Document;
		std::string m_Description;
	};

	struct Harness
	{
		EditorContext Context;
		UndoStack Stack;
		int Counter = 0;

		Result<void> Add(int amount, std::string_view mergeKey = {}, const std::string& document = "A.gscene")
		{
			return Stack.Do(Context, CreateScope<AddOperation>(Counter, amount, document), mergeKey);
		}
	};
}

TEST_CASE("Undo: operations are done, undone and redone in order")
{
	Harness h;
	CHECK_FALSE(h.Stack.CanUndo());
	CHECK_FALSE(h.Stack.CanRedo());
	CHECK(h.Stack.Undo(h.Context).GetError().Message == "there is nothing to undo");
	CHECK(h.Stack.Redo(h.Context).GetError().Message == "there is nothing to redo");

	REQUIRE(h.Add(1).HasValue());
	REQUIRE(h.Add(10).HasValue());
	REQUIRE(h.Add(100).HasValue());
	CHECK(h.Counter == 111);
	CHECK(h.Stack.GetUndoDescription() == "add 100");
	CHECK(h.Stack.GetUndoHistory() == std::vector<std::string>{ "add 100", "add 10", "add 1" });

	REQUIRE(h.Stack.Undo(h.Context).HasValue());
	CHECK(h.Counter == 11);
	CHECK(h.Stack.GetRedoDescription() == "add 100");
	REQUIRE(h.Stack.Undo(h.Context).HasValue());
	CHECK(h.Counter == 1);
	CHECK(h.Stack.GetRedoHistory() == std::vector<std::string>{ "add 10", "add 100" });
	REQUIRE(h.Stack.Redo(h.Context).HasValue());
	CHECK(h.Counter == 11);

	// Something new discards what could be redone.
	REQUIRE(h.Add(1000).HasValue());
	CHECK(h.Counter == 1011);
	CHECK_FALSE(h.Stack.CanRedo());
	CHECK(h.Stack.GetUndoHistory() == std::vector<std::string>{ "add 1000", "add 10", "add 1" });
}

TEST_CASE("Undo: an operation that fails is not recorded")
{
	Harness h;
	auto refusing = CreateScope<AddOperation>(h.Counter, 5);
	refusing->FailRedo = true;
	const Result<void> result = h.Stack.Do(h.Context, std::move(refusing));
	REQUIRE_FALSE(result.HasValue());
	CHECK(result.GetError().Message == "redo refused");
	CHECK_FALSE(h.Stack.CanUndo());
	CHECK(h.Stack.GetDirtyDocuments().empty());
}

TEST_CASE("Undo: a group is one step")
{
	Harness h;
	REQUIRE(h.Add(1).HasValue());

	h.Stack.BeginGroup("three adds");
	REQUIRE(h.Add(10).HasValue());
	REQUIRE(h.Add(20).HasValue());
	REQUIRE(h.Add(30).HasValue());
	CHECK(h.Stack.GetGroupDepth() == 1);
	CHECK(h.Stack.Undo(h.Context).GetError().Message == "cannot undo while a group of operations is open");
	CHECK(h.Stack.Redo(h.Context).GetError().Message == "cannot redo while a group of operations is open");
	h.Stack.EndGroup();
	CHECK(h.Stack.GetGroupDepth() == 0);

	CHECK(h.Counter == 61);
	CHECK(h.Stack.GetUndoHistory() == std::vector<std::string>{ "three adds", "add 1" });
	REQUIRE(h.Stack.Undo(h.Context).HasValue());
	CHECK(h.Counter == 1);
	REQUIRE(h.Stack.Redo(h.Context).HasValue());
	CHECK(h.Counter == 61);
}

TEST_CASE("Undo: groups nest, empty groups vanish and a group with one operation is that operation")
{
	Harness h;
	h.Stack.BeginGroup("empty");
	h.Stack.EndGroup();
	CHECK_FALSE(h.Stack.CanUndo());

	h.Stack.BeginGroup("single");
	REQUIRE(h.Add(7).HasValue());
	h.Stack.EndGroup();
	CHECK(h.Stack.GetUndoDescription() == "add 7"); // not "single"

	h.Stack.BeginGroup("outer");
	REQUIRE(h.Add(1).HasValue());
	h.Stack.BeginGroup("inner");
	REQUIRE(h.Add(2).HasValue());
	REQUIRE(h.Add(3).HasValue());
	h.Stack.EndGroup();
	h.Stack.EndGroup();
	CHECK(h.Stack.GetUndoHistory() == std::vector<std::string>{ "outer", "add 7" });
	CHECK(h.Counter == 13);
	REQUIRE(h.Stack.Undo(h.Context).HasValue());
	CHECK(h.Counter == 7);
	REQUIRE(h.Stack.Redo(h.Context).HasValue());
	CHECK(h.Counter == 13);
}

TEST_CASE("Undo: cancelling a group undoes what it did")
{
	Harness h;
	REQUIRE(h.Add(1).HasValue());
	h.Stack.BeginGroup("doomed");
	REQUIRE(h.Add(10).HasValue());
	REQUIRE(h.Add(20).HasValue());
	CHECK(h.Counter == 31);
	REQUIRE(h.Stack.CancelGroup(h.Context).HasValue());
	CHECK(h.Counter == 1);
	CHECK(h.Stack.GetGroupDepth() == 0);
	CHECK(h.Stack.GetUndoHistory() == std::vector<std::string>{ "add 1" });
	CHECK_FALSE(h.Stack.CancelGroup(h.Context).HasValue()); // no group is open
}

TEST_CASE("Undo: operations with the same merge key merge, others do not")
{
	Harness h;
	REQUIRE(h.Add(1, "drag").HasValue());
	REQUIRE(h.Add(2, "drag").HasValue());
	REQUIRE(h.Add(3, "drag").HasValue());
	CHECK(h.Counter == 6);
	CHECK(h.Stack.GetUndoHistory() == std::vector<std::string>{ "add 6" });

	REQUIRE(h.Add(10, "other").HasValue()); // another key
	REQUIRE(h.Add(20).HasValue());          // no key
	REQUIRE(h.Add(30).HasValue());          // no key: never merges
	CHECK(h.Stack.GetUndoHistory().size() == 4);

	REQUIRE(h.Stack.Undo(h.Context).HasValue());
	CHECK(h.Counter == 36);
	REQUIRE(h.Add(1, "other").HasValue()); // after an undo nothing merges
	CHECK(h.Stack.GetUndoHistory().size() == 4);
	REQUIRE(h.Stack.Undo(h.Context).HasValue());
	CHECK(h.Counter == 36);
	REQUIRE(h.Stack.Undo(h.Context).HasValue());
	REQUIRE(h.Stack.Undo(h.Context).HasValue());
	CHECK(h.Counter == 6); // the three drag steps are still one step
	REQUIRE(h.Stack.Undo(h.Context).HasValue());
	CHECK(h.Counter == 0);
}

TEST_CASE("Undo: operations on different documents do not merge, a save ends a merge")
{
	Harness h;
	REQUIRE(h.Add(1, "drag", "A.gscene").HasValue());
	REQUIRE(h.Add(2, "drag", "B.gscene").HasValue());
	CHECK(h.Stack.GetUndoHistory().size() == 2);

	REQUIRE(h.Add(4, "key").HasValue());
	h.Stack.MarkClean("A.gscene");
	REQUIRE(h.Add(8, "key").HasValue()); // a save in between: what was saved must not change
	CHECK(h.Stack.GetUndoHistory().size() == 4);
	CHECK(h.Stack.IsDirty("A.gscene"));
	REQUIRE(h.Stack.Undo(h.Context).HasValue());
	CHECK_FALSE(h.Stack.IsDirty("A.gscene")); // back at the saved state
}

TEST_CASE("Undo: documents are dirty when they differ from the saved state")
{
	Harness h;
	CHECK_FALSE(h.Stack.IsDirty("A.gscene"));
	CHECK(h.Stack.GetDirtyDocuments().empty());

	REQUIRE(h.Add(1).HasValue());
	REQUIRE(h.Add(2, {}, "B.gscene").HasValue());
	CHECK(h.Stack.IsDirty("A.gscene"));
	CHECK(h.Stack.IsDirty("B.gscene"));
	CHECK(h.Stack.GetDirtyDocuments() == std::vector<std::string>{ "A.gscene", "B.gscene" });

	h.Stack.MarkClean("A.gscene");
	CHECK_FALSE(h.Stack.IsDirty("A.gscene"));
	CHECK(h.Stack.IsDirty("B.gscene"));

	REQUIRE(h.Add(3).HasValue());
	CHECK(h.Stack.IsDirty("A.gscene"));
	REQUIRE(h.Stack.Undo(h.Context).HasValue());
	CHECK_FALSE(h.Stack.IsDirty("A.gscene"));    // undone back to the saved state
	REQUIRE(h.Stack.Undo(h.Context).HasValue()); // this is B's edit
	CHECK_FALSE(h.Stack.IsDirty("B.gscene"));
	REQUIRE(h.Stack.Undo(h.Context).HasValue()); // A's first edit: before the save
	CHECK(h.Stack.IsDirty("A.gscene"));
	REQUIRE(h.Stack.Redo(h.Context).HasValue());
	CHECK_FALSE(h.Stack.IsDirty("A.gscene"));
}

TEST_CASE("Undo: a saved state that was discarded can never be reached again")
{
	Harness h;
	REQUIRE(h.Add(1).HasValue());
	REQUIRE(h.Add(2).HasValue());
	REQUIRE(h.Add(3).HasValue());
	h.Stack.MarkClean("A.gscene");
	REQUIRE(h.Stack.Undo(h.Context).HasValue());
	CHECK(h.Stack.IsDirty("A.gscene"));

	REQUIRE(h.Add(4).HasValue()); // as many operations as at the save, but not the same
	CHECK(h.Stack.IsDirty("A.gscene"));
	REQUIRE(h.Stack.Undo(h.Context).HasValue());
	REQUIRE(h.Stack.Redo(h.Context).HasValue());
	CHECK(h.Stack.IsDirty("A.gscene"));
	h.Stack.MarkClean("A.gscene");
	CHECK_FALSE(h.Stack.IsDirty("A.gscene"));
}

TEST_CASE("Undo: new documents are dirty until saved")
{
	Harness h;
	h.Stack.MarkDirty("New.gscene");
	CHECK(h.Stack.IsDirty("New.gscene"));
	REQUIRE(h.Add(1, {}, "New.gscene").HasValue());
	REQUIRE(h.Stack.Undo(h.Context).HasValue());
	CHECK(h.Stack.IsDirty("New.gscene")); // it was never saved
	h.Stack.MarkClean("New.gscene");
	CHECK_FALSE(h.Stack.IsDirty("New.gscene"));
}

TEST_CASE("Undo: the history is bounded and the dirty state stays right")
{
	EditorContext context;
	int counter = 0;
	const auto add = [&](UndoStack& stack, int amount) {
		REQUIRE(stack.Do(context, CreateScope<AddOperation>(counter, amount)).HasValue());
	};

	// Saved after the first operation, which is dropped later: undoing the rest gets back to the saved state.
	UndoStack saved(3);
	add(saved, 1);
	saved.MarkClean("A.gscene");
	add(saved, 2);
	add(saved, 3);
	add(saved, 4);
	CHECK(saved.GetUndoHistory() == std::vector<std::string>{ "add 4", "add 3", "add 2" });
	CHECK(saved.GetMaxDepth() == 3);
	CHECK(saved.IsDirty("A.gscene"));
	while (saved.CanUndo())
	{
		REQUIRE(saved.Undo(context).HasValue());
	}
	CHECK(counter == 1);
	CHECK_FALSE(saved.IsDirty("A.gscene"));

	// Saved before the first operation, which is dropped: that state cannot be reached any more.
	UndoStack unsaved(2);
	add(unsaved, 1);
	add(unsaved, 2);
	add(unsaved, 3);
	while (unsaved.CanUndo())
	{
		REQUIRE(unsaved.Undo(context).HasValue());
	}
	CHECK(unsaved.IsDirty("A.gscene"));

	UndoStack tiny(0); // at least one entry
	CHECK(tiny.GetMaxDepth() == 1);
}

TEST_CASE("Undo: discarding a document drops its operations")
{
	Harness h;
	REQUIRE(h.Add(1, {}, "A.gscene").HasValue());
	REQUIRE(h.Add(2, {}, "B.gscene").HasValue());
	REQUIRE(h.Add(4, {}, "A.gscene").HasValue());
	REQUIRE(h.Stack.Undo(h.Context).HasValue()); // 4 can be redone

	h.Stack.DiscardDocument("A.gscene");
	CHECK_FALSE(h.Stack.IsDirty("A.gscene"));
	CHECK(h.Stack.IsDirty("B.gscene"));
	CHECK(h.Stack.GetUndoHistory() == std::vector<std::string>{ "add 2" });
	CHECK_FALSE(h.Stack.CanRedo());
	h.Stack.DiscardDocument("Never.gscene"); // unknown documents are fine
}

TEST_CASE("Undo: discarding a document makes the other documents of its groups dirty")
{
	Harness h;
	REQUIRE(h.Add(1, {}, "B.gscene").HasValue());
	h.Stack.MarkClean("B.gscene");
	h.Stack.BeginGroup("both");
	REQUIRE(h.Add(2, {}, "A.gscene").HasValue());
	REQUIRE(h.Add(4, {}, "B.gscene").HasValue());
	h.Stack.EndGroup();
	h.Stack.MarkClean("B.gscene");

	h.Stack.DiscardDocument("A.gscene");
	CHECK(h.Stack.GetUndoHistory() == std::vector<std::string>{ "add 1" });
	CHECK(h.Stack.IsDirty("B.gscene")); // the group changed B too and is gone from the history
}

TEST_CASE("Undo: a failing undo clears the history and keeps the documents dirty")
{
	ScopedLogCapture log;
	Harness h;
	REQUIRE(h.Add(1).HasValue());
	auto fragile = CreateScope<AddOperation>(h.Counter, 2);
	fragile->FailUndo = true;
	REQUIRE(h.Stack.Do(h.Context, std::move(fragile)).HasValue());
	h.Stack.MarkClean("A.gscene");

	const Result<void> result = h.Stack.Undo(h.Context);
	REQUIRE_FALSE(result.HasValue());
	CHECK(result.GetError().Message == "cannot undo 'add 2': undo refused (the undo history was cleared)");
	CHECK_FALSE(h.Stack.CanUndo());
	CHECK_FALSE(h.Stack.CanRedo());
	CHECK(h.Stack.IsDirty("A.gscene"));
	CHECK(log.Contains(LogLevel::Error, "undo refused"));
}

TEST_CASE("Undo: a failing redo clears the history too")
{
	ScopedLogCapture log;
	Harness h;
	auto fragile = CreateScope<AddOperation>(h.Counter, 2);
	REQUIRE(h.Stack.Do(h.Context, std::move(fragile)).HasValue());
	REQUIRE(h.Stack.Undo(h.Context).HasValue());
	// Another operation of the same history refuses to run again: simulate by a second operation that fails on redo.
	auto second = CreateScope<AddOperation>(h.Counter, 3);
	AddOperation* secondPointer = second.get();
	REQUIRE(h.Stack.Do(h.Context, std::move(second)).HasValue());
	REQUIRE(h.Stack.Undo(h.Context).HasValue());
	secondPointer->FailRedo = true;
	const Result<void> result = h.Stack.Redo(h.Context);
	REQUIRE_FALSE(result.HasValue());
	CHECK(result.GetError().Message == "cannot redo 'add 3': redo refused (the undo history was cleared)");
	CHECK_FALSE(h.Stack.CanUndo());
}

TEST_CASE("Undo: Clear keeps dirty documents dirty, Reset forgets everything")
{
	Harness h;
	REQUIRE(h.Add(1, {}, "A.gscene").HasValue());
	REQUIRE(h.Add(2, {}, "B.gscene").HasValue());
	h.Stack.MarkClean("B.gscene");
	h.Stack.Clear();
	CHECK_FALSE(h.Stack.CanUndo());
	CHECK(h.Stack.IsDirty("A.gscene"));
	CHECK_FALSE(h.Stack.IsDirty("B.gscene"));

	h.Stack.Reset();
	CHECK_FALSE(h.Stack.IsDirty("A.gscene"));
	CHECK(h.Stack.GetDirtyDocuments().empty());
}
