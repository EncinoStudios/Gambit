#include "gbpch.h"

#include "Gambit/Core/Utf8.h"
#include "Gambit/ECS/World.h"
#include "Gambit/Reflection/TypeRegistry.h"
#include "Gambit/Scripting/ApiRegistry.h"
#include "Gambit/Scripting/Bindings/BindingSupport.h"
#include "Gambit/Scripting/Bindings/CoreBindings.h"

// Entity (D-041): a handle by UUID to an entity of the engine's world. Every operation looks the entity up, so a handle
// to a destroyed entity raises a clear error instead of touching another entity (IsValid tells).

namespace Gambit
{
	namespace
	{
		World& GetWorldOf(ScriptEngine& engine)
		{
			World* world = engine.GetWorld();
			if (world == nullptr)
			{
				ThrowScriptError("there is no world: entities exist only while a simulation runs");
			}
			return *world;
		}

		Entity Resolve(const ScriptEntity& self)
		{
			return ResolveEntity(*self.Engine, self);
		}

		sol::object GetComponent(const ScriptEntity& self, std::string_view name, sol::this_state lua)
		{
			const Entity entity = Resolve(self);
			const ComponentInfo& info = Bindings::FindComponentType(*self.Engine, name);
			if (!info.Has(entity))
			{
				return sol::make_object(lua, sol::lua_nil);
			}
			return Bindings::MakeComponentProxy(lua, *self.Engine, self.Id, info);
		}

		bool HasComponent(const ScriptEntity& self, std::string_view name)
		{
			return Bindings::FindComponentType(*self.Engine, name).Has(Resolve(self));
		}

		sol::object
		AddComponent(const ScriptEntity& self, std::string_view name, sol::object values, sol::this_state lua)
		{
			const Entity entity = Resolve(self);
			const ComponentInfo& info = Bindings::FindComponentType(*self.Engine, name);
			if (info.HasFlag(ComponentFlags::Internal))
			{
				ThrowScriptError("Entity:Add: the {} component is managed by the engine", name);
			}
			if (info.Has(entity))
			{
				ThrowScriptError(
					"Entity:Add: entity {} ('{}') already has a {} component", self.Id.ToString(), entity.GetName(),
					name);
			}
			GB_CORE_VERIFY(info.Add(entity) != nullptr, "adding {} failed", name);
			try
			{
				Bindings::AssignComponentFields(*self.Engine, entity, info, values);
			}
			catch (...)
			{
				info.Remove(entity); // a bad value adds nothing
				throw;
			}
			return Bindings::MakeComponentProxy(lua, *self.Engine, self.Id, info);
		}

		bool RemoveComponent(const ScriptEntity& self, std::string_view name)
		{
			const Entity entity = Resolve(self);
			const ComponentInfo& info = Bindings::FindComponentType(*self.Engine, name);
			if (!info.Has(entity))
			{
				return false;
			}
			if (info.HasFlag(ComponentFlags::Internal) || !info.HasFlag(ComponentFlags::Removable))
			{
				ThrowScriptError("Entity:Remove: the {} component cannot be removed", name);
			}
			return info.Remove(entity);
		}

		std::string GetName(const ScriptEntity& self)
		{
			return Resolve(self).GetName();
		}

		void SetName(const ScriptEntity& self, std::string_view name)
		{
			if (!IsValidUtf8(name))
			{
				ThrowScriptError("Entity.Name: the name is not valid UTF-8");
			}
			Resolve(self).SetName(name.empty() ? std::string_view("Entity") : name);
		}

		sol::object GetParent(const ScriptEntity& self, sol::this_state lua)
		{
			const Entity entity = Resolve(self);
			const Entity parent = entity.GetWorld()->GetParent(entity);
			return parent ? Bindings::MakeEntityObject(lua, *self.Engine, parent.GetUUID())
						  : sol::make_object(lua, sol::lua_nil);
		}

		void SetParent(const ScriptEntity& self, sol::object parent, sol::object keepWorldTransform)
		{
			const Entity entity = Resolve(self);
			const std::optional<ScriptEntity> target =
				GetOptionalArgument<ScriptEntity>(parent, "Entity:SetParent: parent", "an Entity");
			const std::optional<bool> keep =
				GetOptionalArgument<bool>(keepWorldTransform, "Entity:SetParent: keepWorldTransform", "a boolean");
			const Entity newParent = target.has_value() ? ResolveEntity(*self.Engine, *target) : Entity();
			if (Result<void> set = entity.GetWorld()->SetParent(entity, newParent, keep.value_or(false)); !set)
			{
				ThrowScriptError("Entity:SetParent: {}", set.GetError().Message);
			}
		}

		sol::table GetChildren(const ScriptEntity& self, sol::this_state lua)
		{
			const Entity entity = Resolve(self);
			sol::state_view view(lua);
			const std::vector<Entity> children = entity.GetWorld()->GetChildren(entity);
			sol::table list = view.create_table(static_cast<int>(children.size()), 0);
			for (size_t index = 0; index < children.size(); ++index)
			{
				list[index + 1] = Bindings::MakeEntityObject(view, *self.Engine, children[index].GetUUID());
			}
			return list;
		}

		void Destroy(const ScriptEntity& self)
		{
			const Entity entity = Resolve(self);
			entity.GetWorld()->DestroyEntity(entity);
		}

		bool IsValid(const ScriptEntity& self)
		{
			World* world = self.Engine->GetWorld();
			return world != nullptr && world->ContainsEntity(self.Id);
		}

		bool IsEnabled(const ScriptEntity& self)
		{
			const Entity entity = Resolve(self);
			return entity.GetWorld()->IsEnabled(entity);
		}

		void SetEnabled(const ScriptEntity& self, bool enabled)
		{
			const Entity entity = Resolve(self);
			entity.GetWorld()->SetEnabled(entity, enabled);
		}

		ScriptVec3 WorldPosition(const ScriptEntity& self)
		{
			const Entity entity = Resolve(self);
			return ScriptVec3{ glm::vec3(entity.GetWorld()->ComputeWorldTransform(entity)[3]) };
		}

		ScriptQuat WorldRotation(const ScriptEntity& self)
		{
			const Entity entity = Resolve(self);
			glm::vec3 translation(0.0f);
			glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
			glm::vec3 scale(1.0f);
			if (!DecomposeTransform(entity.GetWorld()->ComputeWorldTransform(entity), translation, rotation, scale))
			{
				ThrowScriptError(
					"Entity:WorldRotation: the world transform of {} has no rotation (a zero scale?)",
					self.Id.ToString());
			}
			return ScriptQuat{ rotation };
		}

		std::string Describe(const ScriptEntity& self)
		{
			if (!IsValid(self))
			{
				return std::format("Entity({}, destroyed)", self.Id.ToString());
			}
			return std::format("Entity({}, '{}')", self.Id.ToString(), Resolve(self).GetName());
		}

		void BindEntity(ScriptBindingContext& context)
		{
			sol::usertype<ScriptEntity> type =
				context.GetLua().new_usertype<ScriptEntity>("Entity", sol::no_constructor);
			type["Name"] = sol::property(&GetName, &SetName);
			type["UUID"] = sol::property([](const ScriptEntity& self) { return self.Id.ToString(); });
			type["Parent"] = sol::property(&GetParent);
			type["Get"] = &GetComponent;
			type["Has"] = &HasComponent;
			type["Add"] = &AddComponent;
			type["Remove"] = &RemoveComponent;
			type["SetParent"] = &SetParent;
			type["Children"] = &GetChildren;
			type["Destroy"] = &Destroy;
			type["IsValid"] = &IsValid;
			type["IsEnabled"] = &IsEnabled;
			type["SetEnabled"] = &SetEnabled;
			type["WorldPosition"] = &WorldPosition;
			type["WorldRotation"] = &WorldRotation;
			type[sol::meta_function::equal_to] = [](const ScriptEntity& a, const ScriptEntity& b) {
				return a.Engine == b.Engine && a.Id == b.Id;
			};
			type[sol::meta_function::to_string] = &Describe;
		}

		void DocumentEntity(ApiRegistry& registry)
		{
			registry.Document(
				"Entity", ApiEntryKind::Type, "Entity",
				"An entity of the world, by UUID (from World:Spawn, Find, Query, ...). Every use looks it up again: a "
				"destroyed entity raises an error, except IsValid and UUID. Entities compare equal when they are the "
				"same entity.",
				"local player = world:Find(\"Player\")");
			registry.Document(
				"Entity.Name", ApiEntryKind::Property, "Entity.Name: string", "The entity's name (read and write).",
				"print(entity.Name)");
			registry.Document(
				"Entity.UUID", ApiEntryKind::Property, "Entity.UUID: string",
				"The entity's UUID as \"0x\" and 16 hex digits (also for a destroyed entity).",
				"local id = entity.UUID");
			registry.Document(
				"Entity.Parent", ApiEntryKind::Property, "Entity.Parent: Entity?", "The parent, or nil for a root.",
				"if entity.Parent == nil then print(\"root\") end");
			registry.Document(
				"Entity:Get", ApiEntryKind::Method, "Entity:Get(component: string) -> Component?",
				"A live proxy of the component, nil when the entity does not have it. Reading a field reads the "
				"component, assigning one writes it; vectors and structs it returns write through too.",
				"entity:Get(\"Transform\").Translation.y = 2");
			registry.Document(
				"Entity:Has", ApiEntryKind::Method, "Entity:Has(component: string) -> boolean",
				"Whether the entity has the component.", "if entity:Has(\"Health\") then ... end");
			registry.Document(
				"Entity:Add", ApiEntryKind::Method, "Entity:Add(component: string, values?: table) -> Component",
				"Adds the component with its defaults and the fields `values` names, and returns its proxy. Adding one "
				"the entity has is an error.",
				"entity:Add(\"Health\", { Current = 50 })");
			registry.Document(
				"Entity:Remove", ApiEntryKind::Method, "Entity:Remove(component: string) -> boolean",
				"Removes the component; false when the entity did not have it.", "entity:Remove(\"Burning\")");
			registry.Document(
				"Entity:SetParent", ApiEntryKind::Method,
				"Entity:SetParent(parent: Entity?, keepWorldTransform?: boolean)",
				"Makes the entity the last child of `parent` (a root for nil); with keepWorldTransform its world "
				"transform stays where it is. A cycle is an error.",
				"sword:SetParent(hand)");
			registry.Document(
				"Entity:Children", ApiEntryKind::Method, "Entity:Children() -> Entity[]",
				"The children in sibling order.", "for _, child in ipairs(entity:Children()) do print(child.Name) end");
			registry.Document(
				"Entity:Destroy", ApiEntryKind::Method, "Entity:Destroy()",
				"Destroys the entity and its descendants at once.", "enemy:Destroy()");
			registry.Document(
				"Entity:IsValid", ApiEntryKind::Method, "Entity:IsValid() -> boolean",
				"Whether the entity still exists.", "if target:IsValid() then ... end");
			registry.Document(
				"Entity:IsEnabled", ApiEntryKind::Method, "Entity:IsEnabled() -> boolean",
				"Whether the entity itself is enabled (it may still be inactive through a disabled ancestor).",
				"if door:IsEnabled() then ... end");
			registry.Document(
				"Entity:SetEnabled", ApiEntryKind::Method, "Entity:SetEnabled(enabled: boolean)",
				"Enables or disables the entity; a disabled entity and its subtree are skipped by systems and queries.",
				"door:SetEnabled(false)");
			registry.Document(
				"Entity:WorldPosition", ApiEntryKind::Method, "Entity:WorldPosition() -> Vec3",
				"The position in the world, from the transforms of the entity and its ancestors.",
				"local p = entity:WorldPosition()");
			registry.Document(
				"Entity:WorldRotation", ApiEntryKind::Method, "Entity:WorldRotation() -> Quat",
				"The rotation in the world, from the transforms of the entity and its ancestors.",
				"local r = entity:WorldRotation()");
		}
	}

	Entity ResolveEntity(ScriptEngine& engine, const ScriptEntity& entity)
	{
		if (entity.Engine != &engine)
		{
			ThrowScriptError("the entity {} belongs to another world", entity.Id.ToString());
		}
		const Entity resolved = GetWorldOf(engine).TryGetEntity(entity.Id);
		if (!resolved)
		{
			ThrowScriptError("the entity {} no longer exists", entity.Id.ToString());
		}
		return resolved;
	}

	namespace Bindings
	{
		sol::object MakeEntityObject(sol::state_view lua, ScriptEngine& engine, UUID entity)
		{
			return sol::make_object(lua, ScriptEntity{ &engine, entity });
		}

		void RegisterEntityApi(ApiRegistry& registry)
		{
			registry.RegisterBinder("Entity", &BindEntity);
			DocumentEntity(registry);
		}
	}
}
