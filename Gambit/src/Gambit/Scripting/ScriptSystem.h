#pragma once

#include "Gambit/Core/Base.h"
#include "Gambit/ECS/System.h"
#include "Gambit/Scripting/ScriptEngine.h"

#include <cstddef>
#include <string_view>

namespace Gambit
{
	// A system that a script declared with System(...), scheduled by its phase, order and name like any C++ system
	// (D-024). It calls the script's OnCreate, OnUpdate and OnDestroy with the world object (and the time step). An
	// error is recorded by the engine, logged, and disables the system for the rest of the session; it never escapes
	// into the engine. Created by ScriptEngine::AttachToWorld; keeps the engine alive.
	class ScriptSystem final : public ISystem
	{
	public:
		// `index` is the system's position in engine->GetSystems().
		ScriptSystem(Ref<ScriptEngine> engine, size_t index);
		~ScriptSystem() override;

		[[nodiscard]] std::string_view GetName() const override { return m_Declaration.Name; }
		[[nodiscard]] SystemPhase GetPhase() const override { return m_Declaration.Phase; }
		[[nodiscard]] int32_t GetOrder() const override { return m_Declaration.Order; }
		[[nodiscard]] bool RunsInEditMode() const override { return m_Declaration.RunInEditMode; }

		void OnCreate(World& world) override;
		void OnUpdate(World& world, TimeStep timeStep) override;
		void OnDestroy(World& world) override;

		// Whether a callback failed, which disabled the system.
		[[nodiscard]] bool HasFailed() const noexcept { return m_Failed; }

	private:
		enum class Callback : uint8_t
		{
			Create,
			Update,
			Destroy
		};

		void Call(World& world, Callback callback, float deltaTime);

		Ref<ScriptEngine> m_Engine;
		size_t m_Index;
		ScriptSystemDeclaration m_Declaration;
		bool m_Failed = false;
	};

	// The first system of the Input phase of a world with scripts: it owns the world's ScriptEngine (the world's
	// systems keep it alive, and ScriptEngine::Find finds it here) and at the start of every Play tick samples the
	// engine's input source into the InputState that Lua's Input functions read.
	class ScriptRuntimeSystem final : public ISystem
	{
	public:
		static constexpr std::string_view Name = "ScriptRuntime";

		explicit ScriptRuntimeSystem(Ref<ScriptEngine> engine);

		[[nodiscard]] std::string_view GetName() const override { return Name; }
		[[nodiscard]] SystemPhase GetPhase() const override { return SystemPhase::Input; }
		[[nodiscard]] int32_t GetOrder() const override;

		void OnUpdate(World& world, TimeStep timeStep) override;

		[[nodiscard]] ScriptEngine& GetEngine() const noexcept { return *m_Engine; }

	private:
		Ref<ScriptEngine> m_Engine;
	};
}
