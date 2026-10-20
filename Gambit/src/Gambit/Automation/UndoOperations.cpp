#include "gbpch.h"

#include "Gambit/Automation/UndoOperations.h"

#include "Gambit/Asset/DataTable.h"
#include "Gambit/Automation/EditorContext.h"

namespace Gambit
{
	namespace
	{
		// The scene, entity and component an operation works on.
		struct Target
		{
			World* Scene = nullptr;
			Entity Subject;
			const ComponentInfo* Component = nullptr;
		};

		Result<World*> FindScene(EditorContext& context, const std::string& document)
		{
			World* world = context.FindWorld(document);
			if (world == nullptr)
			{
				return MakeCommandError(
					CommandErrorCode::NotFound, std::format("the scene '{}' is not open", document));
			}
			return world;
		}

		Result<Entity> FindEntity(World& world, UUID uuid)
		{
			const Entity entity = world.TryGetEntity(uuid);
			if (!entity.IsValid())
			{
				return MakeCommandError(
					CommandErrorCode::NotFound, std::format("there is no entity {} in the scene", uuid.ToString()));
			}
			return entity;
		}

		Result<Target>
		Resolve(EditorContext& context, const std::string& document, UUID entity, const std::string& component)
		{
			Result<World*> world = FindScene(context, document);
			if (!world)
			{
				return world.GetError();
			}
			Result<Entity> subject = FindEntity(**world, entity);
			if (!subject)
			{
				return subject.GetError();
			}
			const ComponentInfo* info = (*world)->GetTypeRegistry().FindComponent(component);
			if (info == nullptr)
			{
				return MakeCommandError(
					CommandErrorCode::NotFound, std::format("there is no component type '{}'", component));
			}
			return Target{ *world, *subject, info };
		}

		// Checks that every field exists and can hold its value, so that writing cannot fail half way.
		Result<void> CheckFields(const ComponentInfo& info, const std::vector<FieldData>& values)
		{
			for (const FieldData& data : values)
			{
				const FieldInfo* field = info.FindField(data.Name);
				if (field == nullptr)
				{
					return Error::Format("component '{}' has no field '{}'", info.GetName(), data.Name);
				}
				if (!field->CanHold(data.Value))
				{
					return Error::Format(
						"the value of '{}.{}' does not fit its type {}", info.GetName(), data.Name,
						ToString(field->Type));
				}
			}
			return {};
		}

		void WriteFields(const ComponentInfo& info, void* component, const std::vector<FieldData>& values)
		{
			for (const FieldData& data : values)
			{
				[[maybe_unused]] const bool written = info.FindField(data.Name)->SetValue(component, data.Value);
				GB_CORE_ASSERT(written, "a checked value was refused");
			}
		}

		std::vector<FieldData> ReadAllFields(const ComponentInfo& info, const void* component)
		{
			std::vector<FieldData> values;
			if (!info.IsTag())
			{
				for (const FieldInfo& field : info.GetFields())
				{
					values.push_back({ field.Name, field.GetValue(component) });
				}
			}
			return values;
		}
	}

	// ---- SetFieldsOperation ----

	SetFieldsOperation::SetFieldsOperation(
		std::string document, UUID entity, std::string component, std::vector<FieldData> values,
		std::string description)
		: m_Document(std::move(document)),
		  m_Entity(entity),
		  m_Component(std::move(component)),
		  m_After(std::move(values)),
		  m_Description(std::move(description))
	{}

	Result<void> SetFieldsOperation::Redo(EditorContext& context)
	{
		Result<Target> target = Resolve(context, m_Document, m_Entity, m_Component);
		if (!target)
		{
			return target.GetError();
		}
		void* component = target->Component->GetRaw(target->Subject);
		if (component == nullptr)
		{
			return MakeCommandError(
				CommandErrorCode::NotFound,
				std::format("entity {} has no component '{}'", m_Entity.ToString(), m_Component));
		}
		if (Result<void> valid = CheckFields(*target->Component, m_After); !valid)
		{
			return valid;
		}
		if (!m_Recorded)
		{
			for (const FieldData& data : m_After)
			{
				m_Before.push_back({ data.Name, target->Component->FindField(data.Name)->GetValue(component) });
			}
			m_Recorded = true;
		}
		WriteFields(*target->Component, component, m_After);
		return {};
	}

	Result<void> SetFieldsOperation::Undo(EditorContext& context)
	{
		Result<Target> target = Resolve(context, m_Document, m_Entity, m_Component);
		if (!target)
		{
			return target.GetError();
		}
		void* component = target->Component->GetRaw(target->Subject);
		if (component == nullptr)
		{
			return MakeCommandError(
				CommandErrorCode::NotFound,
				std::format("entity {} has no component '{}'", m_Entity.ToString(), m_Component));
		}
		WriteFields(*target->Component, component, m_Before);
		return {};
	}

	bool SetFieldsOperation::CanMerge(const IUndoableOperation& later) const
	{
		const auto* next = dynamic_cast<const SetFieldsOperation*>(&later);
		return next != nullptr && next->m_Document == m_Document && next->m_Entity == m_Entity
			&& next->m_Component == m_Component;
	}

	void SetFieldsOperation::Merge(const IUndoableOperation& later)
	{
		const auto* next = static_cast<const SetFieldsOperation*>(&later);
		for (const FieldData& data : next->m_After)
		{
			const auto existing = std::ranges::find(m_After, data.Name, &FieldData::Name);
			if (existing != m_After.end())
			{
				existing->Value = data.Value;
			}
			else
			{
				m_After.push_back(data);
			}
		}
		for (const FieldData& data : next->m_Before)
		{
			if (std::ranges::find(m_Before, data.Name, &FieldData::Name) == m_Before.end())
			{
				m_Before.push_back(data);
			}
		}
	}

	// ---- ComponentPresenceOperation ----

	ComponentPresenceOperation::ComponentPresenceOperation(
		std::string document, UUID entity, std::string component, Mode mode, std::vector<FieldData> initialValues,
		std::string description)
		: m_Document(std::move(document)),
		  m_Entity(entity),
		  m_Component(std::move(component)),
		  m_Mode(mode),
		  m_Values(std::move(initialValues)),
		  m_Description(std::move(description))
	{}

	Result<void> ComponentPresenceOperation::AddComponent(EditorContext& context)
	{
		Result<Target> target = Resolve(context, m_Document, m_Entity, m_Component);
		if (!target)
		{
			return target.GetError();
		}
		if (target->Component->Has(target->Subject))
		{
			return MakeCommandError(
				CommandErrorCode::AlreadyExists,
				std::format("entity {} has the component '{}' already", m_Entity.ToString(), m_Component));
		}
		if (Result<void> valid = CheckFields(*target->Component, m_Values); !valid)
		{
			return valid;
		}
		void* component = target->Component->Add(target->Subject);
		if (component == nullptr)
		{
			return MakeCommandError(
				CommandErrorCode::Conflict, std::format("the component '{}' is managed by the engine", m_Component));
		}
		WriteFields(*target->Component, component, m_Values);
		return {};
	}

	Result<void> ComponentPresenceOperation::RemoveComponent(EditorContext& context)
	{
		Result<Target> target = Resolve(context, m_Document, m_Entity, m_Component);
		if (!target)
		{
			return target.GetError();
		}
		const void* component = target->Component->GetRaw(*target->Scene, target->Subject.GetHandle());
		if (component == nullptr)
		{
			return MakeCommandError(
				CommandErrorCode::NotFound,
				std::format("entity {} has no component '{}'", m_Entity.ToString(), m_Component));
		}
		if (m_Mode == Mode::Remove && !m_Recorded)
		{
			m_Values = ReadAllFields(*target->Component, component);
			m_Recorded = true;
		}
		// The inverse of an Add must work even for a component that tools may not remove on their own.
		if (m_Mode == Mode::Remove)
		{
			if (!target->Component->Remove(target->Subject))
			{
				return MakeCommandError(
					CommandErrorCode::Conflict, std::format("the component '{}' cannot be removed", m_Component));
			}
		}
		else if (entt::sparse_set* storage = target->Component->FindStorage(*target->Scene); storage != nullptr)
		{
			storage->remove(target->Subject.GetHandle());
		}
		return {};
	}

	Result<void> ComponentPresenceOperation::Redo(EditorContext& context)
	{
		return m_Mode == Mode::Add ? AddComponent(context) : RemoveComponent(context);
	}

	Result<void> ComponentPresenceOperation::Undo(EditorContext& context)
	{
		return m_Mode == Mode::Add ? RemoveComponent(context) : AddComponent(context);
	}

	// ---- SubtreeOperation ----

	SubtreeOperation::SubtreeOperation(
		Key, std::string document, Mode mode, Creator creator, UUID root, std::string description)
		: m_Document(std::move(document)),
		  m_Mode(mode),
		  m_Creator(std::move(creator)),
		  m_Root(root),
		  m_Description(std::move(description))
	{}

	Scope<IUndoableOperation> SubtreeOperation::Create(std::string document, Creator creator, std::string description)
	{
		GB_CORE_ASSERT(creator != nullptr, "SubtreeOperation::Create needs a creator");
		return CreateScope<SubtreeOperation>(
			Key{}, std::move(document), Mode::Create, std::move(creator), UUID(), std::move(description));
	}

	Scope<IUndoableOperation> SubtreeOperation::Destroy(std::string document, UUID root, std::string description)
	{
		return CreateScope<SubtreeOperation>(
			Key{}, std::move(document), Mode::Destroy, Creator{}, root, std::move(description));
	}

	Result<void> SubtreeOperation::Capture(World& world, Entity root)
	{
		m_Snapshot = CaptureSubtree(world, root, true);
		m_SiblingIndex = world.GetSiblingIndex(root);
		m_Root = root.GetUUID();
		m_Captured = true;
		return {};
	}

	Result<void> SubtreeOperation::Spawn(EditorContext& context)
	{
		Result<World*> world = FindScene(context, m_Document);
		if (!world)
		{
			return world.GetError();
		}
		Result<std::vector<Entity>> spawned = SpawnEntities(**world, m_Snapshot);
		if (!spawned)
		{
			return spawned.GetError();
		}
		(*world)->SetSiblingIndex(spawned->front(), m_SiblingIndex);
		return {};
	}

	Result<void> SubtreeOperation::Remove(EditorContext& context)
	{
		Result<World*> world = FindScene(context, m_Document);
		if (!world)
		{
			return world.GetError();
		}
		Result<Entity> root = FindEntity(**world, m_Root);
		if (!root)
		{
			return root.GetError();
		}
		(*world)->DestroyEntity(*root);
		return {};
	}

	Result<void> SubtreeOperation::Redo(EditorContext& context)
	{
		if (m_Captured)
		{
			return m_Mode == Mode::Create ? Spawn(context) : Remove(context);
		}

		Result<World*> world = FindScene(context, m_Document);
		if (!world)
		{
			return world.GetError();
		}
		if (m_Mode == Mode::Create)
		{
			Result<Entity> created = m_Creator(**world);
			if (!created)
			{
				return created.GetError();
			}
			m_Creator = nullptr; // the snapshot replays it from now on
			return Capture(**world, *created);
		}
		Result<Entity> root = FindEntity(**world, m_Root);
		if (!root)
		{
			return root.GetError();
		}
		if (Result<void> captured = Capture(**world, *root); !captured)
		{
			return captured;
		}
		return Remove(context);
	}

	Result<void> SubtreeOperation::Undo(EditorContext& context)
	{
		return m_Mode == Mode::Create ? Remove(context) : Spawn(context);
	}

	// ---- ReparentOperation ----

	ReparentOperation::ReparentOperation(
		std::string document, UUID entity, UUID newParent, std::optional<size_t> index, bool keepWorldTransform,
		std::string description)
		: m_Document(std::move(document)),
		  m_Entity(entity),
		  m_NewParent(newParent),
		  m_NewIndex(index),
		  m_KeepWorldTransform(keepWorldTransform),
		  m_Description(std::move(description))
	{}

	Result<ReparentOperation::Placement> ReparentOperation::Read(World& world, Entity entity)
	{
		Placement placement;
		const Entity parent = world.GetParent(entity);
		placement.Parent = parent.IsValid() ? parent.GetUUID() : UUID();
		placement.Index = world.GetSiblingIndex(entity);
		if (const auto* transform = entity.TryGetComponent<TransformComponent>(); transform != nullptr)
		{
			placement.Transform = *transform;
		}
		return placement;
	}

	Result<void> ReparentOperation::Apply(EditorContext& context, const Placement& placement)
	{
		Result<World*> world = FindScene(context, m_Document);
		if (!world)
		{
			return world.GetError();
		}
		Result<Entity> entity = FindEntity(**world, m_Entity);
		if (!entity)
		{
			return entity.GetError();
		}
		Entity parent;
		if (placement.Parent.IsValid())
		{
			Result<Entity> found = FindEntity(**world, placement.Parent);
			if (!found)
			{
				return found.GetError();
			}
			parent = *found;
		}
		if (Result<void> moved = (*world)->SetParent(*entity, parent, false); !moved)
		{
			return MakeCommandError(CommandErrorCode::Conflict, moved.GetError().Message);
		}
		(*world)->SetSiblingIndex(*entity, placement.Index);
		if (placement.Transform.has_value())
		{
			if (auto* transform = entity->TryGetComponent<TransformComponent>(); transform != nullptr)
			{
				*transform = *placement.Transform;
			}
		}
		return {};
	}

	Result<void> ReparentOperation::Redo(EditorContext& context)
	{
		if (m_Recorded)
		{
			return Apply(context, m_After);
		}

		Result<World*> world = FindScene(context, m_Document);
		if (!world)
		{
			return world.GetError();
		}
		Result<Entity> entity = FindEntity(**world, m_Entity);
		if (!entity)
		{
			return entity.GetError();
		}
		Entity parent;
		if (m_NewParent.IsValid())
		{
			Result<Entity> found = FindEntity(**world, m_NewParent);
			if (!found)
			{
				return found.GetError();
			}
			parent = *found;
		}
		Result<Placement> before = Read(**world, *entity);
		if (!before)
		{
			return before.GetError();
		}
		if (Result<void> moved = (*world)->SetParent(*entity, parent, m_KeepWorldTransform); !moved)
		{
			return MakeCommandError(CommandErrorCode::Conflict, moved.GetError().Message);
		}
		if (m_NewIndex.has_value())
		{
			(*world)->SetSiblingIndex(*entity, *m_NewIndex);
		}
		Result<Placement> after = Read(**world, *entity);
		if (!after)
		{
			return after.GetError();
		}
		m_Before = std::move(*before);
		m_After = std::move(*after);
		m_Recorded = true;
		return {};
	}

	Result<void> ReparentOperation::Undo(EditorContext& context)
	{
		return Apply(context, m_Before);
	}

	// ---- SetCellOperation ----

	SetCellOperation::SetCellOperation(
		std::string table, std::string row, std::string column, FieldValue value, std::string description)
		: m_Table(std::move(table)),
		  m_Row(std::move(row)),
		  m_Column(std::move(column)),
		  m_After(std::move(value)),
		  m_Description(std::move(description))
	{}

	Result<void> SetCellOperation::Redo(EditorContext& context)
	{
		Result<Ref<DataTable>> table = context.GetTable(m_Table);
		if (!table)
		{
			return table.GetError();
		}
		if (!m_Recorded)
		{
			Result<FieldValue> before = (*table)->GetValue(m_Row, m_Column);
			if (!before)
			{
				return MakeCommandError(CommandErrorCode::NotFound, before.GetError().Message);
			}
			m_Before = std::move(*before);
			m_Recorded = true;
		}
		return (*table)->SetValue(m_Row, m_Column, m_After);
	}

	Result<void> SetCellOperation::Undo(EditorContext& context)
	{
		Result<Ref<DataTable>> table = context.GetTable(m_Table);
		if (!table)
		{
			return table.GetError();
		}
		return (*table)->SetValue(m_Row, m_Column, m_Before);
	}

	// ---- TableRowOperation ----

	TableRowOperation::TableRowOperation(
		std::string table, std::string row, Mode mode, std::vector<FieldValue> values, std::optional<size_t> position,
		std::string description)
		: m_Table(std::move(table)),
		  m_Row(std::move(row)),
		  m_Mode(mode),
		  m_Values(std::move(values)),
		  m_Position(position),
		  m_Description(std::move(description))
	{}

	Result<void> TableRowOperation::InsertRow(EditorContext& context)
	{
		Result<Ref<DataTable>> table = context.GetTable(m_Table);
		if (!table)
		{
			return table.GetError();
		}
		const size_t position = m_Position.value_or((*table)->GetRowCount());
		if (Result<void> added = (*table)->InsertRow(position, m_Row, m_Values); !added)
		{
			return added;
		}
		m_Position = position;
		return {};
	}

	Result<void> TableRowOperation::EraseRow(EditorContext& context)
	{
		Result<Ref<DataTable>> table = context.GetTable(m_Table);
		if (!table)
		{
			return table.GetError();
		}
		const std::optional<size_t> index = (*table)->FindRowIndex(m_Row);
		if (!index.has_value())
		{
			return MakeCommandError(
				CommandErrorCode::NotFound, std::format("there is no row '{}' in '{}'", m_Row, m_Table));
		}
		if (m_Mode == Mode::Remove && !m_Recorded)
		{
			const DataRow& row = (*table)->GetRows()[*index];
			m_Values.assign(row.Values.begin() + 1, row.Values.end());
			m_Position = *index;
			m_Recorded = true;
		}
		return (*table)->RemoveRow(m_Row);
	}

	Result<void> TableRowOperation::Redo(EditorContext& context)
	{
		return m_Mode == Mode::Add ? InsertRow(context) : EraseRow(context);
	}

	Result<void> TableRowOperation::Undo(EditorContext& context)
	{
		return m_Mode == Mode::Add ? EraseRow(context) : InsertRow(context);
	}
}
