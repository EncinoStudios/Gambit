#include "gbpch.h"

#include "Gambit/ECS/CommandBuffer.h"

#include "Gambit/ECS/Entity.h"
#include "Gambit/ECS/World.h"
#include "Gambit/Reflection/TypeRegistry.h"

namespace Gambit
{
	CommandBuffer::CommandBuffer(World& world)
		: m_World(world)
	{}

	UUID CommandBuffer::CreateEntity(std::string_view name, UUID parent)
	{
		const UUID uuid = m_World.GenerateUUID(); // never one that is already reserved
		m_ReservedUUIDs.insert(uuid);
		m_Commands.emplace_back(CreateCommand{ uuid, std::string(name), parent });
		return uuid;
	}

	void CommandBuffer::DestroyEntity(UUID entity)
	{
		m_Commands.emplace_back(DestroyCommand{ entity });
	}

	void CommandBuffer::AddComponent(UUID entity, const ComponentInfo& component)
	{
		m_Commands.emplace_back(AddComponentCommand{ entity, component.GetId() });
	}

	void CommandBuffer::RemoveComponent(UUID entity, const ComponentInfo& component)
	{
		m_Commands.emplace_back(RemoveComponentCommand{ entity, component.GetId() });
	}

	bool CommandBuffer::SetField(UUID entity, const ComponentInfo& component, std::string_view field, FieldValue value)
	{
		const FieldInfo* info = component.FindField(field);
		if (info == nullptr || !info->CanHold(value))
		{
			return false;
		}
		m_Commands.emplace_back(SetFieldCommand{ entity, component.GetId(), std::string(field), std::move(value) });
		return true;
	}

	void CommandBuffer::SetParent(UUID child, UUID parent, bool keepWorldTransform)
	{
		m_Commands.emplace_back(SetParentCommand{ child, parent, keepWorldTransform });
	}

	void CommandBuffer::Execute(UUID entity, std::function<void(Entity)> function)
	{
		GB_CORE_ASSERT(function != nullptr, "CommandBuffer::Execute needs a function");
		m_Commands.emplace_back(ExecuteCommand{ entity, std::move(function) });
	}

	void CommandBuffer::Clear()
	{
		m_Commands.clear();
		m_ReservedUUIDs.clear();
	}

	CommandBufferFlushResult CommandBuffer::Flush()
	{
		// Taken out first: a command may run code that records more (they wait for the next flush).
		std::vector<Command> commands = std::move(m_Commands);
		m_Commands.clear();
		m_ReservedUUIDs.clear();

		CommandBufferFlushResult result;
		for (Command& command : commands)
		{
			if (Apply(command))
			{
				++result.Executed;
			}
			else
			{
				++result.Skipped;
			}
		}
		return result;
	}

	bool CommandBuffer::Apply(Command& command)
	{
		World& world = m_World;
		const TypeRegistry& types = world.GetTypeRegistry();
		return std::visit(
			[&]<typename C>(C& data) -> bool {
				if constexpr (std::is_same_v<C, CreateCommand>)
				{
					if (world.ContainsEntity(data.EntityId))
					{
						return false; // an entity created outside the buffer took the reserved UUID first
					}
					const Entity entity = world.CreateEntityWithUUID(data.EntityId, data.Name);
					if (const Entity parent = world.TryGetEntity(data.Parent))
					{
						static_cast<void>(world.SetParent(entity, parent)); // a new entity cannot form a cycle
					}
					return true;
				}
				else if constexpr (std::is_same_v<C, DestroyCommand>)
				{
					const Entity entity = world.TryGetEntity(data.EntityId);
					if (!entity)
					{
						return false;
					}
					world.DestroyEntity(entity);
					return true;
				}
				else if constexpr (std::is_same_v<C, AddComponentCommand>)
				{
					const Entity entity = world.TryGetEntity(data.EntityId);
					const ComponentInfo* component = types.FindComponent(data.Component);
					return entity && component != nullptr && component->Add(entity) != nullptr;
				}
				else if constexpr (std::is_same_v<C, RemoveComponentCommand>)
				{
					const Entity entity = world.TryGetEntity(data.EntityId);
					const ComponentInfo* component = types.FindComponent(data.Component);
					return entity && component != nullptr && component->Remove(entity);
				}
				else if constexpr (std::is_same_v<C, SetFieldCommand>)
				{
					const Entity entity = world.TryGetEntity(data.EntityId);
					const ComponentInfo* component = types.FindComponent(data.Component);
					return entity && component != nullptr && component->SetFieldValue(entity, data.Field, data.Value);
				}
				else if constexpr (std::is_same_v<C, SetParentCommand>)
				{
					const Entity child = world.TryGetEntity(data.Child);
					const Entity parent = world.TryGetEntity(data.Parent);
					if (!child || (data.Parent.IsValid() && !parent))
					{
						return false;
					}
					return world.SetParent(child, parent, data.KeepWorldTransform).HasValue();
				}
				else
				{
					static_assert(std::is_same_v<C, ExecuteCommand>, "unhandled command");
					const Entity entity = world.TryGetEntity(data.EntityId);
					if (!entity)
					{
						return false;
					}
					data.Function(entity);
					return true;
				}
			},
			command);
	}
}
