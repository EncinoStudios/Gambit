#pragma once

#include "Gambit/Core/UUID.h"
#include "Gambit/Reflection/ComponentInfo.h"
#include "Gambit/Reflection/FieldType.h"

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace Gambit
{
	class Entity;
	class World;

	struct CommandBufferFlushResult
	{
		size_t Executed = 0;
		size_t Skipped = 0;
	};

	// Structural changes recorded while systems run and applied at fixed points (D-011): the scheduler flushes after
	// every phase, and World::Tick before the first one. Commands address entities by UUID and run in recording order.
	// A command that cannot apply when it runs (its entity was destroyed or never created, or see below) is skipped,
	// so recording never fails because of what other systems do in the meantime.
	class CommandBuffer
	{
	public:
		explicit CommandBuffer(World& world);
		CommandBuffer(const CommandBuffer&) = delete;
		CommandBuffer& operator=(const CommandBuffer&) = delete;

		// Creates an entity at the next flush, as the last child of `parent` if that entity exists then (a root
		// otherwise). The UUID is drawn from the world RNG now (World::GenerateUUID), so later commands can address the
		// new entity and the outcome only depends on the order of recording.
		UUID CreateEntity(std::string_view name = "Entity", UUID parent = {});
		// Destroys the entity and its descendants.
		void DestroyEntity(UUID entity);

		// Adds the component, or replaces its value when the entity already has it.
		template<typename T>
		void AddComponent(UUID entity, T component = {})
		{
			// Generic lambdas, so that Entity only needs to be complete where this template is instantiated.
			if constexpr (std::is_empty_v<T>)
			{
				Execute(entity, [](auto target) {
					if (!target.template HasComponent<T>())
					{
						target.template AddComponent<T>(); // a tag has no value to replace
					}
				});
			}
			else
			{
				Execute(entity, [component = std::move(component)](auto target) {
					target.template AddOrReplaceComponent<T>(component);
				});
			}
		}
		// Adds the component with its default values; skipped when the entity already has it or it is Internal.
		void AddComponent(UUID entity, const ComponentInfo& component);

		template<typename T>
		void RemoveComponent(UUID entity)
		{
			Execute(entity, [](auto target) {
				if (target.template HasComponent<T>())
				{
					target.template RemoveComponent<T>();
				}
			});
		}
		// Skipped when the entity lacks the component or ComponentInfo::Remove refuses it.
		void RemoveComponent(UUID entity, const ComponentInfo& component);

		// Sets one field. False, recording nothing, when the component has no such field or the value does not fit it
		// (FieldInfo::CanHold); skipped at flush when the entity lacks the component.
		[[nodiscard]] bool
		SetField(UUID entity, const ComponentInfo& component, std::string_view field, FieldValue value);

		// World::SetParent at flush time; a null parent makes the child a root. Skipped when the parent no longer
		// exists or the change would create a cycle.
		void SetParent(UUID child, UUID parent, bool keepWorldTransform = false);

		// Runs `function` on the entity at flush time (engine code; the typed component commands use it). The function
		// must reach the world through the Entity it receives, never through captured references: World::Copy copies
		// pending commands, and the copy flushes them on itself.
		void Execute(UUID entity, std::function<void(Entity)> function);

		[[nodiscard]] size_t GetCommandCount() const noexcept { return m_Commands.size(); }
		[[nodiscard]] bool IsEmpty() const noexcept { return m_Commands.empty(); }
		// Drops every recorded command; UUIDs reserved by CreateEntity are released.
		void Clear();

		// Applies the commands in recording order and empties the buffer.
		CommandBufferFlushResult Flush();

	private:
		friend class World; // World::Copy copies pending commands, World::GenerateUUID avoids reserved UUIDs

		struct CreateCommand
		{
			UUID EntityId;
			std::string Name;
			UUID Parent;
		};
		struct DestroyCommand
		{
			UUID EntityId;
		};
		struct AddComponentCommand
		{
			UUID EntityId;
			ComponentId Component = 0;
		};
		struct RemoveComponentCommand
		{
			UUID EntityId;
			ComponentId Component = 0;
		};
		struct SetFieldCommand
		{
			UUID EntityId;
			ComponentId Component = 0;
			std::string Field;
			FieldValue Value;
		};
		struct SetParentCommand
		{
			UUID Child;
			UUID Parent;
			bool KeepWorldTransform = false;
		};
		struct ExecuteCommand
		{
			UUID EntityId;
			std::function<void(Entity)> Function;
		};
		using Command = std::variant<
			CreateCommand, DestroyCommand, AddComponentCommand, RemoveComponentCommand, SetFieldCommand,
			SetParentCommand, ExecuteCommand>;

		[[nodiscard]] bool Apply(Command& command);

		World& m_World;
		std::vector<Command> m_Commands;
		std::unordered_set<UUID> m_ReservedUUIDs; // created at the next flush; lookups only, never iterated
	};
}
