#include <doctest/doctest.h>

#include "Gambit.h"

TEST_CASE("Core: Gambit.h includes the public engine headers")
{
	using namespace Gambit::Literals;
	// The umbrella header is for applications; this checks that it compiles on its own and that the names it promises
	// are visible.
	const Gambit::UUID id(7);
	const Gambit::Random random(1);
	const Gambit::InputFrame frame;
	const Gambit::ApplicationSpecification specification;
	const Gambit::WindowSpecification window;
	const Gambit::GraphicsDeviceSpecification graphics;
	CHECK(id.IsValid());
	CHECK(graphics.FramesInFlight == 2);
	CHECK_FALSE(Gambit::ShaderLibrary::Find("FullscreenTriangle.vert").empty());
	CHECK(random.GetState().Increment == 1);
	CHECK(frame.TextCount == 0);
	CHECK(specification.SimulationHz == 60);
	CHECK(window.Visible);
	CHECK(Gambit::DMath::Sin(0.0) == 0.0);
	CHECK(Gambit::Hash::FNV1a("a") == "a"_hash);
	CHECK(std::string(Gambit::VersionString).empty() == false);
}
