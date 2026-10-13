#pragma once

#include "Gambit/Automation/UndoStack.h"
#include "Gambit/Core/UUID.h"
#include "Gambit/ECS/Entity.h"
#include "Gambit/Reflection/FieldType.h"
#include "Gambit/Serialization/EntityData.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

// The generic undoable operations (D-050). They work from reflection and from snapshots of entities as data
// (Serialization/EntityData.h), so a command that edits a scene builds one of these from what it wants to happen and
// needs no undo code of its own: new components, fields and commands are covered automatically. Documents are named by
// their project-relative path; each operation records, on its first Redo, the state it needs for Undo.

namespace Gambit
{
	// Sets fields of one component of one entity (the "component field before/after" operation).
	class SetFieldsOperation final : public IUndoableOperation
	{
	public:
		// `values` must fit the fields (FieldInfo::CanHold) of the component `component`.
		SetFieldsOperation(
			std::string document, UUID entity, std::string component, std::vector<FieldData> values,
			std::string description);

		[[nodiscard]] Result<void> Redo(EditorContext& context) override;
		[[nodiscard]] Result<void> Undo(EditorContext& context) override;
		[[nodiscard]] std::string GetDescription() const override { return m_Description; }
		void GetDocuments(std::vector<std::string>& documents) const override { documents.push_back(m_Document); }
		// Merges edits of the same entity and component; the merged operation sets the union of the fields.
		[[nodiscard]] bool TryMerge(const IUndoableOperation& later) override;

	private:
		std::string m_Document;
		UUID m_Entity;
		std::string m_Component;
		std::vector<FieldData> m_After;
		std::vector<FieldData> m_Before;
		std::string m_Description;
		bool m_Recorded = false;
	};

	// Adds a component (with initial values for some fields; the others take their defaults) to an entity, or
	// removes it. Removing records every field so that Undo brings the component back as it was.
	class ComponentPresenceOperation final : public IUndoableOperation
	{
	public:
		enum class Mode : uint8_t
		{
			Add,
			Remove
		};

		ComponentPresenceOperation(
			std::string document, UUID entity, std::string component, Mode mode, std::vector<FieldData> initialValues,
			std::string description);

		[[nodiscard]] Result<void> Redo(EditorContext& context) override;
		[[nodiscard]] Result<void> Undo(EditorContext& context) override;
		[[nodiscard]] std::string GetDescription() const override { return m_Description; }
		void GetDocuments(std::vector<std::string>& documents) const override { documents.push_back(m_Document); }

	private:
		[[nodiscard]] Result<void> AddComponent(EditorContext& context);
		[[nodiscard]] Result<void> RemoveComponent(EditorContext& context);

		std::string m_Document;
		UUID m_Entity;
		std::string m_Component;
		Mode m_Mode;
		std::vector<FieldData> m_Values; // Add: the initial values; Remove: all fields as they were
		std::string m_Description;
		bool m_Recorded = false;
	};

	// Creates or destroys an entity with all its descendants (the "entity subtree snapshot" operation). Creating runs a
	// function on the first Redo that makes the entities in the world (spawning data, instantiating a prefab, copying
	// a subtree); afterwards the operation holds a snapshot of the result and replays it with the same UUIDs, in the
	// same place among the siblings. Destroying records the snapshot before it removes the entities.
	class SubtreeOperation final : public IUndoableOperation
	{
	public:
		// Makes the entities and returns the root of the new subtree.
		using Creator = std::function<Result<Entity>(World& world)>;

		[[nodiscard]] static Scope<IUndoableOperation>
		Create(std::string document, Creator creator, std::string description);
		[[nodiscard]] static Scope<IUndoableOperation>
		Destroy(std::string document, UUID root, std::string description);

		[[nodiscard]] Result<void> Redo(EditorContext& context) override;
		[[nodiscard]] Result<void> Undo(EditorContext& context) override;
		[[nodiscard]] std::string GetDescription() const override { return m_Description; }
		void GetDocuments(std::vector<std::string>& documents) const override { documents.push_back(m_Document); }

	private:
		enum class Mode : uint8_t
		{
			Create,
			Destroy
		};

		struct Key
		{
			explicit Key() = default;
		};

	public:
		SubtreeOperation(Key, std::string document, Mode mode, Creator creator, UUID root, std::string description);

	private:
		[[nodiscard]] Result<void> Spawn(EditorContext& context);
		[[nodiscard]] Result<void> Remove(EditorContext& context);
		[[nodiscard]] Result<void> Capture(World& world, Entity root);

		std::string m_Document;
		Mode m_Mode;
		Creator m_Creator;
		UUID m_Root;
		std::vector<EntityData> m_Snapshot; // the root first; the root's Parent is where it is attached
		size_t m_SiblingIndex = 0;
		std::string m_Description;
		bool m_Captured = false;
	};

	// Moves an entity to another parent (or to the roots) and sibling position, optionally keeping its world transform.
	class ReparentOperation final : public IUndoableOperation
	{
	public:
		// `index` is the position among the new siblings; without it the entity becomes the last child.
		ReparentOperation(
			std::string document, UUID entity, UUID newParent, std::optional<size_t> index, bool keepWorldTransform,
			std::string description);

		[[nodiscard]] Result<void> Redo(EditorContext& context) override;
		[[nodiscard]] Result<void> Undo(EditorContext& context) override;
		[[nodiscard]] std::string GetDescription() const override { return m_Description; }
		void GetDocuments(std::vector<std::string>& documents) const override { documents.push_back(m_Document); }

	private:
		// Where an entity is in the hierarchy and what its local transform was.
		struct Placement
		{
			UUID Parent;
			size_t Index = 0;
			std::optional<TransformComponent> Transform;
		};

		[[nodiscard]] static Result<Placement> Read(World& world, Entity entity);
		[[nodiscard]] Result<void> Apply(EditorContext& context, const Placement& placement);

		std::string m_Document;
		UUID m_Entity;
		UUID m_NewParent;
		std::optional<size_t> m_NewIndex;
		bool m_KeepWorldTransform;
		std::string m_Description;
		Placement m_Before;
		Placement m_After;
		bool m_Recorded = false;
	};

	// Sets one cell of a data table (a document named by the table's path).
	class SetCellOperation final : public IUndoableOperation
	{
	public:
		SetCellOperation(
			std::string table, std::string row, std::string column, FieldValue value, std::string description);

		[[nodiscard]] Result<void> Redo(EditorContext& context) override;
		[[nodiscard]] Result<void> Undo(EditorContext& context) override;
		[[nodiscard]] std::string GetDescription() const override { return m_Description; }
		void GetDocuments(std::vector<std::string>& documents) const override { documents.push_back(m_Table); }

	private:
		std::string m_Table;
		std::string m_Row;
		std::string m_Column;
		FieldValue m_After;
		FieldValue m_Before;
		std::string m_Description;
		bool m_Recorded = false;
	};

	// Adds a row to a data table, or removes one; removing puts the row back at its old position on Undo.
	class TableRowOperation final : public IUndoableOperation
	{
	public:
		enum class Mode : uint8_t
		{
			Add,
			Remove
		};

		// Add: `values` are the cells after the id column (empty for the defaults) and `position` where the row goes
		// (the end without one). Remove: both are ignored.
		TableRowOperation(
			std::string table, std::string row, Mode mode, std::vector<FieldValue> values,
			std::optional<size_t> position, std::string description);

		[[nodiscard]] Result<void> Redo(EditorContext& context) override;
		[[nodiscard]] Result<void> Undo(EditorContext& context) override;
		[[nodiscard]] std::string GetDescription() const override { return m_Description; }
		void GetDocuments(std::vector<std::string>& documents) const override { documents.push_back(m_Table); }

	private:
		[[nodiscard]] Result<void> InsertRow(EditorContext& context);
		[[nodiscard]] Result<void> EraseRow(EditorContext& context);

		std::string m_Table;
		std::string m_Row;
		Mode m_Mode;
		std::vector<FieldValue> m_Values;
		std::optional<size_t> m_Position;
		std::string m_Description;
		bool m_Recorded = false;
	};
}
