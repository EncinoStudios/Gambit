#include "gbpch.h"

#include "Gambit/Scripting/ScriptSystem.h"

#include "Gambit/ECS/World.h"
#include "Gambit/Input/InputSource.h"
#include "Gambit/Input/InputState.h"
#include "Gambit/Scripting/Bindings/BindingSupport.h"

#include <limits>

namespace Gambit
{
	ScriptSystem::ScriptSystem(Ref<ScriptEngine> engine, size_t index)
		: m_Engine(std::move(engine)), m_Index(index)
	{
		GB_CORE_ASSERT(m_Engine != nullptr && index < m_Engine->GetSystems().size(), "no such script system");
		m_Declaration = m_Engine->GetSystems()[index];
	}

	ScriptSystem::~ScriptSystem() = default;

	void ScriptSystem::OnCreate(World& world)
	{
		Call(world, Callback::Create, 0.0f);
	}

	void ScriptSystem::OnUpdate(World& world, TimeStep timeStep)
	{
		Call(world, Callback::Update, timeStep.GetSeconds());
	}

	void ScriptSystem::OnDestroy(World& world)
	{
		Call(world, Callback::Destroy, 0.0f);
	}

	void ScriptSystem::Call(World& world, Callback callback, float deltaTime)
	{
		if (m_Failed)
		{
			return;
		}
		const Bindings::ScriptSystemFunctions& functions =
			Bindings::GetDeclarations(*m_Engine).SystemFunctions[m_Index];
		const sol::function& function = callback == Callback::Create ? functions.OnCreate
			: callback == Callback::Update                           ? functions.OnUpdate
																	 : functions.OnDestroy;
		if (!function.valid())
		{
			return;
		}
		lua_State* lua = m_Engine->GetLuaState();
		if (lua_checkstack(lua, 3) == 0)
		{
			GB_CORE_ERROR("system '{}': the Lua stack is full", m_Declaration.Name);
			return;
		}
		function.push(lua);
		lua_getfield(lua, LUA_REGISTRYINDEX, Bindings::WorldObjectKey);
		int argumentCount = 1;
		if (callback == Callback::Update)
		{
			lua_pushnumber(lua, static_cast<lua_Number>(deltaTime));
			++argumentCount;
		}
		const std::string_view name = callback == Callback::Create ? "OnCreate"
			: callback == Callback::Update                         ? "OnUpdate"
																   : "OnDestroy";
		if (const Result<void> called =
		        CallScriptFunction(*m_Engine, argumentCount, 0, std::format("{}.{}", m_Declaration.Name, name));
		    !called)
		{
			m_Failed = true;
			if (callback != Callback::Destroy)
			{
				world.GetScheduler().SetSystemEnabled(m_Declaration.Name, false);
				GB_ERROR("[script] system '{}' is disabled for this session", m_Declaration.Name);
			}
		}
	}

	ScriptRuntimeSystem::ScriptRuntimeSystem(Ref<ScriptEngine> engine)
		: m_Engine(std::move(engine))
	{
		GB_CORE_ASSERT(m_Engine != nullptr, "the script runtime needs an engine");
	}

	int32_t ScriptRuntimeSystem::GetOrder() const
	{
		return std::numeric_limits<int32_t>::min(); // before every other system: the tick's input comes first
	}

	void ScriptRuntimeSystem::OnUpdate(World&, TimeStep)
	{
		InputSource* source = m_Engine->GetInputSource();
		m_Engine->GetInput().Update(source != nullptr ? source->Sample() : InputFrame{});
	}
}
