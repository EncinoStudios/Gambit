#pragma once

#include "Reflection/ReflectionTestTypes.h"

#include "Gambit/ECS/Entity.h"
#include "Gambit/ECS/World.h"

namespace Gambit::Tests
{
	// A registry with the test types and a world on it. Members are destroyed in reverse order, so the world goes
	// before the registry it uses.
	struct TestWorld
	{
		TypeRegistry Registry;
		Scope<World> Scene;

		TestWorld()
		{
			RegisterTestTypes(Registry);
			Scene = CreateScope<World>(Registry);
		}
	};
}
