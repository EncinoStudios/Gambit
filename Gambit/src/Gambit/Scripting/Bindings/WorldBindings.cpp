#include "gbpch.h"

#include "Gambit/Core/Utf8.h"
#include "Gambit/ECS/World.h"
#include "Gambit/Math/Random.h"
#include "Gambit/Reflection/TypeRegistry.h"
#include "Gambit/Scripting/ApiRegistry.h"
#include "Gambit/Scripting/Bindings/BindingSupport.h"
#include "Gambit/Scripting/Bindings/CoreBindings.h"

#include <entt/entity/runtime_view.hpp>

// World (D-041): what systems receive as `world`. Structural changes from Lua (Spawn, Destroy, Entity:Add/Remove) are
// immediate; queries iterate a snapshot of the matching entities taken when the loop starts and skip the ones that no
// longer match (destroyed, disabled, a component removed) when their turn comes, so changing the world while iterating
// is safe and deterministic.

namespace Gambit
{
	namespace
	{
		struct ScriptWorld
		{
			ScriptEngine* Engine = nullptr;
		};

		struct ScriptRandom
		{
			ScriptEngine* Engine = nullptr;
		};

		World& GetWorld(const ScriptWorld& self)
		{
			return *self.Engine->GetWorld();
		}

		std::vector<const ComponentInfo*>
		ComponentTypes(ScriptEngine& engine, const sol::variadic_args& names, std::string_view function)
		{
			if (names.size() == 0)
			{
				ThrowScriptError("World:{}: name at least one component", function);
			}
			std::vector<const ComponentInfo*> types;
			for (const sol::stack_proxy name : names)
			{
				if (name.get_type() != sol::type::string)
				{
					ThrowScriptError("World:{}: components are named by strings", function);
				}
				types.push_back(&Bindings::FindComponentType(engine, name.as<std::string_view>()));
			}
			return types;
		}

		bool Matches(World& world, Entity entity, std::span<const ComponentInfo* const> types)
		{
			return entity && world.IsActive(entity)
				&& std::ranges::all_of(types, [&](const ComponentInfo* info) { return info->Has(entity); });
		}

		// The active entities that have every component, in the order of the smallest storage (deterministic: it
		// depends only on the operations that built the world).
		std::vector<UUID> Snapshot(World& world, std::span<const ComponentInfo* const> types)
		{
			entt::const_runtime_view view;
			for (const ComponentInfo* info : types)
			{
				const entt::sparse_set* storage = info->FindStorage(std::as_const(world));
				if (storage == nullptr)
				{
					return {};
				}
				view.iterate(*storage);
			}
			const ComponentInfo& inactive = world.GetTypeRegistry().GetComponent<InactiveComponent>();
			if (const entt::sparse_set* storage = inactive.FindStorage(std::as_const(world)); storage != nullptr)
			{
				view.exclude(*storage);
			}
			std::vector<UUID> entities;
			const entt::registry& registry = world.GetRegistry();
			for (const entt::entity handle : view)
			{
				entities.push_back(registry.get<IDComponent>(handle).ID);
			}
			return entities;
		}

		void PushMatch(
			sol::variadic_results& results, sol::state_view lua, ScriptEngine& engine, UUID entity,
			std::span<const ComponentInfo* const> types)
		{
			results.push_back(Bindings::MakeEntityObject(lua, engine, entity));
			for (const ComponentInfo* info : types)
			{
				results.push_back(Bindings::MakeComponentProxy(lua, engine, entity, *info));
			}
		}

		// for entity, a, b in world:Query("A", "B") do ... end
		sol::object Query(const ScriptWorld& self, sol::variadic_args names, sol::this_state lua)
		{
			ScriptEngine& engine = *self.Engine;
			struct State
			{
				std::vector<const ComponentInfo*> Types;
				std::vector<UUID> Entities;
				size_t Next = 0;
			};
			auto state = CreateRef<State>();
			state->Types = ComponentTypes(engine, names, "Query");
			state->Entities = Snapshot(GetWorld(self), state->Types);
			return sol::make_object(lua, [state, &engine](sol::this_state inner) {
				World& world = *engine.GetWorld();
				sol::variadic_results results;
				while (state->Next < state->Entities.size())
				{
					const UUID id = state->Entities[state->Next++];
					if (Matches(world, world.TryGetEntity(id), state->Types))
					{
						PushMatch(results, inner, engine, id, state->Types);
						break;
					}
				}
				return results;
			});
		}

		sol::variadic_results QueryFirst(const ScriptWorld& self, sol::variadic_args names, sol::this_state lua)
		{
			ScriptEngine& engine = *self.Engine;
			const std::vector<const ComponentInfo*> types = ComponentTypes(engine, names, "QueryFirst");
			sol::variadic_results results;
			const std::vector<UUID> entities = Snapshot(GetWorld(self), types);
			if (!entities.empty())
			{
				PushMatch(results, lua, engine, entities.front(), types);
			}
			else
			{
				results.push_back(sol::make_object(lua, sol::lua_nil));
			}
			return results;
		}

		int64_t Count(const ScriptWorld& self, sol::variadic_args names)
		{
			const std::vector<const ComponentInfo*> types = ComponentTypes(*self.Engine, names, "Count");
			return static_cast<int64_t>(Snapshot(GetWorld(self), types).size());
		}

		std::string CheckName(std::string_view name, std::string_view function)
		{
			if (!IsValidUtf8(name))
			{
				ThrowScriptError("World:{}: the name is not valid UTF-8", function);
			}
			return std::string(name);
		}

		// world:Spawn(name?, components?) or world:Spawn(components)
		sol::object Spawn(const ScriptWorld& self, sol::variadic_args args, sol::this_state lua)
		{
			ScriptEngine& engine = *self.Engine;
			World& world = GetWorld(self);
			if (args.size() > 2)
			{
				ThrowScriptError(
					"World:Spawn: expected a name and/or a table of components, got {} arguments", args.size());
			}
			std::string name;
			sol::object components = sol::make_object(lua, sol::lua_nil);
			for (size_t index = 0; index < args.size(); ++index)
			{
				const sol::type kind = args[index].get_type();
				if (index == 0 && kind == sol::type::string)
				{
					name = CheckName(args[index].as<std::string_view>(), "Spawn");
				}
				else if (kind == sol::type::table && components.get_type() == sol::type::lua_nil)
				{
					components = args[index].as<sol::object>();
				}
				else if (kind != sol::type::lua_nil)
				{
					ThrowScriptError(
						"World:Spawn: expected a name and/or a table of components, got a {} as argument {}",
						sol::type_name(lua, kind), index + 1);
				}
			}

			// Check every component before the entity exists, then add them in name order.
			std::vector<std::pair<const ComponentInfo*, sol::object>> added;
			if (components.get_type() == sol::type::table)
			{
				for (const auto& [key, values] : components.as<sol::table>())
				{
					if (key.get_type() != sol::type::string)
					{
						ThrowScriptError("World:Spawn: components are named by strings");
					}
					const ComponentInfo& info = Bindings::FindComponentType(engine, key.as<std::string_view>());
					if (info.HasFlag(ComponentFlags::Internal))
					{
						ThrowScriptError("World:Spawn: the {} component is managed by the engine", info.GetName());
					}
					added.emplace_back(&info, values);
				}
			}
			std::ranges::sort(added, {}, [](const auto& entry) { return std::string_view(entry.first->GetName()); });

			const Entity entity = world.CreateEntity(name);
			try
			{
				for (const auto& [info, values] : added)
				{
					if (!info->Has(entity)) // every entity has a Tag already
					{
						GB_CORE_VERIFY(info->Add(entity) != nullptr, "adding {} failed", info->GetName());
					}
					Bindings::AssignComponentFields(engine, entity, *info, values);
				}
			}
			catch (...)
			{
				world.DestroyEntity(entity); // a bad component spawns nothing
				throw;
			}
			return Bindings::MakeEntityObject(lua, engine, entity.GetUUID());
		}

		void Destroy(const ScriptWorld& self, const ScriptEntity& entity)
		{
			GetWorld(self).DestroyEntity(ResolveEntity(*self.Engine, entity));
		}

		sol::object Find(const ScriptWorld& self, std::string_view name, sol::this_state lua)
		{
			const Entity entity = GetWorld(self).FindEntityByName(name);
			return entity ? Bindings::MakeEntityObject(lua, *self.Engine, entity.GetUUID())
						  : sol::make_object(lua, sol::lua_nil);
		}

		sol::object FindByUUID(const ScriptWorld& self, std::string_view text, sol::this_state lua)
		{
			const std::optional<UUID> id = UUID::Parse(text);
			if (!id.has_value())
			{
				ThrowScriptError("World:FindByUUID: '{}' is not a UUID (\"0x\" and 1 to 16 hex digits)", text);
			}
			return GetWorld(self).ContainsEntity(*id) ? Bindings::MakeEntityObject(lua, *self.Engine, *id)
													  : sol::make_object(lua, sol::lua_nil);
		}

		Random& GetRandom(const ScriptRandom& self)
		{
			return self.Engine->GetRandom();
		}

		double RandomRange(const ScriptRandom& self, double minimum, double maximum)
		{
			if (!(minimum <= maximum))
			{
				ThrowScriptError("Random:Range: min ({}) is greater than max ({})", minimum, maximum);
			}
			return static_cast<double>(
				GetRandom(self).RangeFloat(static_cast<float>(minimum), static_cast<float>(maximum)));
		}

		int64_t RandomInt(const ScriptRandom& self, int64_t minimum, int64_t maximum)
		{
			if (minimum > maximum)
			{
				ThrowScriptError("Random:Int: min ({}) is greater than max ({})", minimum, maximum);
			}
			return GetRandom(self).RangeInt(minimum, maximum);
		}

		void BindWorld(ScriptBindingContext& context)
		{
			sol::state_view& lua = context.GetLua();
			ScriptEngine& engine = context.GetEngine();

			sol::usertype<ScriptRandom> random = lua.new_usertype<ScriptRandom>("Random", sol::no_constructor);
			random["Float"] = [](const ScriptRandom& self) {
				return static_cast<double>(GetRandom(self).NextFloat01());
			};
			random["Range"] = &RandomRange;
			random["Int"] = &RandomInt;
			random["Bool"] = [](const ScriptRandom& self, sol::object probability) {
				const std::optional<double> chance =
					GetOptionalArgument<double>(probability, "Random:Bool: probability", "a number");
				return GetRandom(self).NextBool(static_cast<float>(chance.value_or(0.5)));
			};
			random["Gaussian"] = [](const ScriptRandom& self, sol::object mean, sol::object deviation) {
				const std::optional<double> center =
					GetOptionalArgument<double>(mean, "Random:Gaussian: mean", "a number");
				const std::optional<double> spread =
					GetOptionalArgument<double>(deviation, "Random:Gaussian: deviation", "a number");
				return static_cast<double>(GetRandom(self).NextGaussian(
					static_cast<float>(center.value_or(0.0)), static_cast<float>(spread.value_or(1.0))));
			};
			random["InsideUnitSphere"] = [](const ScriptRandom& self) {
				return ScriptVec3{ GetRandom(self).InsideUnitSphere() };
			};

			sol::usertype<ScriptWorld> type = lua.new_usertype<ScriptWorld>("World", sol::no_constructor);
			type["Spawn"] = &Spawn;
			type["Destroy"] = &Destroy;
			type["Find"] = &Find;
			type["FindByUUID"] = &FindByUUID;
			type["Query"] = &Query;
			type["QueryFirst"] = &QueryFirst;
			type["Count"] = &Count;
			type["Tick"] =
				sol::property([](const ScriptWorld& self) { return static_cast<int64_t>(GetWorld(self).GetTick()); });
			type["Time"] = sol::property([](const ScriptWorld& self) { return GetWorld(self).GetTime(); });
			type["DeltaTime"] = sol::property(
				[](const ScriptWorld& self) { return static_cast<double>(GetWorld(self).GetDeltaTime()); });
			type["Random"] = sol::property([](const ScriptWorld& self) { return ScriptRandom{ self.Engine }; });

			if (engine.GetWorld() != nullptr)
			{
				lua.registry()[Bindings::WorldObjectKey] = ScriptWorld{ &engine };
			}
		}

		void DocumentWorld(ApiRegistry& registry)
		{
			registry.Document(
				"World", ApiEntryKind::Type, "World",
				"The simulated world, which systems receive as their first argument. Structural changes (Spawn, "
				"Destroy, Entity:Add/Remove) happen at once; queries iterate a snapshot and skip entities that stopped "
				"matching.",
				"OnUpdate = function(world, dt) print(world.Tick) end");
			registry.Document(
				"World:Spawn", ApiEntryKind::Method, "World:Spawn(name?: string, components?: table) -> Entity",
				"Creates an entity (its UUID comes from the world's Random) with the components the table names, each "
				"with its defaults and the fields given. Nothing is created when a component or value is wrong.",
				"local e = world:Spawn(\"Bullet\", { Transform = { Translation = Vec3(0, 1, 0) }, Velocity = {} })");
			registry.Document(
				"World:Destroy", ApiEntryKind::Method, "World:Destroy(entity: Entity)",
				"Destroys the entity and its descendants at once.", "world:Destroy(bullet)");
			registry.Document(
				"World:Find", ApiEntryKind::Method, "World:Find(name: string) -> Entity?",
				"The entity with this name (the one with the smallest UUID when several have it), or nil.",
				"local player = world:Find(\"Player\")");
			registry.Document(
				"World:FindByUUID", ApiEntryKind::Method, "World:FindByUUID(uuid: string) -> Entity?",
				"The entity with this UUID (\"0x...\"), or nil.", "local e = world:FindByUUID(saved.UUID)");
			registry.Document(
				"World:Query", ApiEntryKind::Method, "World:Query(...: string) -> iterator",
				"Iterates the active entities that have every named component: each step yields the entity and a proxy "
				"of each component. Iterates a snapshot; entities destroyed or changed during the loop are skipped "
				"when "
				"they no longer match.",
				"for entity, transform, spin in world:Query(\"Transform\", \"Spin\") do ... end");
			registry.Document(
				"World:QueryFirst", ApiEntryKind::Method, "World:QueryFirst(...: string) -> Entity?, ...",
				"The first entity of World:Query and its component proxies, or nil.",
				"local camera, transform = world:QueryFirst(\"Camera\", \"Transform\")");
			registry.Document(
				"World:Count", ApiEntryKind::Method, "World:Count(...: string) -> integer",
				"The number of active entities with every named component (at least one).",
				"local enemies = world:Count(\"Enemy\")");
			registry.Document(
				"World.Tick", ApiEntryKind::Property, "World.Tick: integer",
				"The number of the current simulation tick (the first is 1).", "if world.Tick % 60 == 0 then ... end");
			registry.Document(
				"World.Time", ApiEntryKind::Property, "World.Time: number",
				"Simulated seconds so far, this tick included.", "local t = world.Time");
			registry.Document(
				"World.DeltaTime", ApiEntryKind::Property, "World.DeltaTime: number",
				"The fixed time step of the current tick in seconds.",
				"position = position + velocity * world.DeltaTime");
			registry.Document(
				"World.Random", ApiEntryKind::Property, "World.Random: Random",
				"The world's random generator (D-011): the only source of randomness of a simulation.",
				"local roll = world.Random:Int(1, 6)");
			registry.Document(
				"Random", ApiEntryKind::Type, "Random",
				"The world's deterministic random generator (PCG32), reached through world.Random; math.random draws "
				"from it too.",
				"local x = world.Random:Range(-1, 1)");
			registry.Document(
				"Random:Float", ApiEntryKind::Method, "Random:Float() -> number", "A float in [0, 1) (one draw).",
				"if world.Random:Float() < 0.1 then ... end");
			registry.Document(
				"Random:Range", ApiEntryKind::Method, "Random:Range(min: number, max: number) -> number",
				"A float in [min, max) (one draw).", "local angle = world.Random:Range(0, 2 * Math.Pi)");
			registry.Document(
				"Random:Int", ApiEntryKind::Method, "Random:Int(min: integer, max: integer) -> integer",
				"An integer in [min, max], both included.", "local roll = world.Random:Int(1, 6)");
			registry.Document(
				"Random:Bool", ApiEntryKind::Method, "Random:Bool(probability?: number) -> boolean",
				"True with the probability (default 0.5).", "if world.Random:Bool(0.25) then ... end");
			registry.Document(
				"Random:Gaussian", ApiEntryKind::Method, "Random:Gaussian(mean?: number, deviation?: number) -> number",
				"A normally distributed number (default mean 0, standard deviation 1).",
				"local spread = world.Random:Gaussian(0, 0.1)");
			registry.Document(
				"Random:InsideUnitSphere", ApiEntryKind::Method, "Random:InsideUnitSphere() -> Vec3",
				"A point uniformly distributed in the unit ball.",
				"local offset = world.Random:InsideUnitSphere() * 2");
		}
	}

	namespace Bindings
	{
		void RegisterWorldApi(ApiRegistry& registry)
		{
			registry.RegisterBinder("World", &BindWorld);
			DocumentWorld(registry);
		}
	}
}
