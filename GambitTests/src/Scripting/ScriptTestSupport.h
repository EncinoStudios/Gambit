#pragma once

#include "Reflection/ReflectionTestTypes.h"

#include "Gambit/Core/MemoryMount.h"
#include "Gambit/Core/TimeStep.h"
#include "Gambit/Core/VFS.h"
#include "Gambit/ECS/Entity.h"
#include "Gambit/ECS/World.h"
#include "Gambit/Scripting/ScriptEngine.h"

#include <doctest/doctest.h>

#include <string>
#include <string_view>
#include <vector>

namespace Gambit::Tests
{
	// A registry with the test types, a world on it and an in-memory project VFS for scripts. Members are destroyed in
	// reverse order: the engine (whose systems the world may hold) before the world, the world before the registry.
	class ScriptFixture
	{
	public:
		TypeRegistry Types;
		Ref<MemoryMount> Files = CreateRef<MemoryMount>();
		VFS Vfs;
		Scope<World> Scene;
		Ref<ScriptEngine> Engine;

		ScriptFixture()
		{
			RegisterTestTypes(Types);
			Scene = CreateScope<World>(Types);
			REQUIRE(Vfs.Mount("", Files).HasValue());
		}

		~ScriptFixture()
		{
			Engine.reset();
			Scene.reset();
		}

		ScriptFixture(const ScriptFixture&) = delete;
		ScriptFixture& operator=(const ScriptFixture&) = delete;

		void Write(std::string_view path, std::string_view source)
		{
			REQUIRE(Files->AddTextFile(path, source).HasValue());
		}

		[[nodiscard]] ScriptEngineSpecification MakeSpecification(ScriptMode mode = ScriptMode::Simulation)
		{
			ScriptEngineSpecification specification;
			specification.Mode = mode;
			specification.Scene = mode == ScriptMode::Simulation ? Scene.get() : nullptr;
			specification.Types = &Types;
			specification.Files = &Vfs;
			return specification;
		}

		// Creates the engine (replacing the previous one).
		ScriptEngine& Create(ScriptEngineSpecification specification)
		{
			Engine.reset();
			Result<Ref<ScriptEngine>> created = ScriptEngine::Create(std::move(specification));
			REQUIRE_MESSAGE(created.HasValue(), (created ? "" : created.GetError().Message));
			Engine = std::move(*created);
			return *Engine;
		}

		ScriptEngine& Create(ScriptMode mode = ScriptMode::Simulation) { return Create(MakeSpecification(mode)); }

		// Creates the engine, runs `source` as Scripts/Main.lua, finishes loading and attaches the engine to the world:
		// what a simulation start does.
		ScriptEngine& Start(std::string_view source)
		{
			ScriptEngine& engine = Create();
			const Result<void> loaded = engine.LoadScript("Scripts/Main.lua", source);
			REQUIRE_MESSAGE(loaded.HasValue(), (loaded ? "" : loaded.GetError().Message));
			engine.FinishLoading();
			const Result<void> attached = engine.AttachToWorld();
			REQUIRE_MESSAGE(attached.HasValue(), (attached ? "" : attached.GetError().Message));
			return engine;
		}

		// Runs Play ticks of 1/60 s.
		void Tick(int count = 1)
		{
			for (int index = 0; index < count; ++index)
			{
				Scene->Tick(TimeStep(1.0f / 60.0f));
			}
		}

		// Evaluates code that must succeed.
		ScriptValue Eval(std::string_view code)
		{
			Result<ScriptValue> value = Engine->Evaluate(code);
			REQUIRE_MESSAGE(value.HasValue(), (value ? "" : value.GetError().Message));
			return std::move(*value);
		}

		// Evaluates code that must fail and returns the message.
		std::string EvalError(std::string_view code)
		{
			Result<ScriptValue> value = Engine->Evaluate(code);
			REQUIRE_FALSE(value.HasValue());
			return value.GetError().Message;
		}
	};

	inline bool Contains(std::string_view text, std::string_view part)
	{
		return text.find(part) != std::string_view::npos;
	}

	// The string values of a table, in entry order (a list: in index order).
	inline std::vector<std::string> Strings(const ScriptValue& value)
	{
		std::vector<std::string> strings;
		if (const ScriptTable* table = value.TryGet<ScriptTable>(); table != nullptr)
		{
			for (const ScriptTableEntry& entry : *table)
			{
				if (const std::string* text = entry.Value.TryGet<std::string>(); text != nullptr)
				{
					strings.push_back(*text);
				}
			}
		}
		return strings;
	}

	// An integer or float value as a double.
	inline double Number(const ScriptValue& value)
	{
		if (const double* number = value.TryGet<double>(); number != nullptr)
		{
			return *number;
		}
		REQUIRE(value.Is<int64_t>());
		return static_cast<double>(*value.TryGet<int64_t>());
	}

	// The number values of a table, in entry order.
	inline std::vector<double> Numbers(const ScriptValue& value)
	{
		std::vector<double> numbers;
		if (const ScriptTable* table = value.TryGet<ScriptTable>(); table != nullptr)
		{
			for (const ScriptTableEntry& entry : *table)
			{
				if (entry.Value.Is<double>() || entry.Value.Is<int64_t>())
				{
					numbers.push_back(Number(entry.Value));
				}
			}
		}
		return numbers;
	}
}
