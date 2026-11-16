#pragma once

#include "Reflection/ReflectionTestTypes.h"

#include "Gambit/Core/MemoryMount.h"
#include "Gambit/Core/VFS.h"
#include "Gambit/ECS/Entity.h"
#include "Gambit/ECS/World.h"
#include "Gambit/Scripting/ScriptEngine.h"

#include <doctest/doctest.h>

#include <string>
#include <string_view>

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
}
