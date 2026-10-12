#include <doctest/doctest.h>

#include "Gambit/RHI/ShaderCache.h"
#include "Gambit/RHI/ShaderLibrary.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

using namespace Gambit;

namespace
{
	constexpr uint32_t SpirvMagicNumber = 0x07230203u;

	// A minimal valid-looking SPIR-V header (magic, version 1.5, generator, bound, schema).
	constexpr std::array<uint32_t, 5> FakeSpirv = { SpirvMagicNumber, 0x00010500u, 0u, 1u, 0u };
	constexpr std::array<uint32_t, 5> NotSpirv = { 0x12345678u, 0u, 0u, 0u, 0u };
}

TEST_CASE("RHI: the shader library holds the engine's compiled shaders")
{
	const std::span<const uint32_t> fullscreen = ShaderLibrary::Find("FullscreenTriangle.vert");
	REQUIRE(fullscreen.size() > 5);
	CHECK(fullscreen[0] == SpirvMagicNumber);
	// --target-env vulkan1.2 produces SPIR-V 1.5; the generator word names glslang (Khronos tool id 8).
	CHECK(fullscreen[1] == 0x00010500u);
	CHECK((fullscreen[2] >> 16) == 8u);

	CHECK(ShaderLibrary::Find("FullscreenTriangle").empty());
	CHECK(ShaderLibrary::Find("FullscreenTriangle.frag").empty());
	CHECK(ShaderLibrary::Find("").empty());

	// The test shaders that main() registered are found the same way.
	CHECK(ShaderLibrary::Find("Triangle.vert")[0] == SpirvMagicNumber);
	CHECK(ShaderLibrary::Find("SrgbCurve.comp")[0] == SpirvMagicNumber);

	const std::vector<std::string_view> names = ShaderLibrary::GetNames();
	CHECK(std::ranges::is_sorted(names));
	CHECK(std::ranges::find(names, "FullscreenTriangle.vert") != names.end());
	CHECK(std::ranges::find(names, "UvGradient.frag") != names.end());
}

TEST_CASE("RHI: ShaderLibrary::Register adds shaders and rejects bad or taken names")
{
	static constexpr std::array<EmbeddedShader, 2> Shaders = {
		EmbeddedShader{ "RegisterTestB.comp", FakeSpirv },
		EmbeddedShader{ "RegisterTestA.vert", FakeSpirv },
	};
	REQUIRE(ShaderLibrary::Register(Shaders).HasValue());
	CHECK(ShaderLibrary::Find("RegisterTestA.vert").data() == FakeSpirv.data());
	CHECK(ShaderLibrary::Find("RegisterTestB.comp").size() == FakeSpirv.size());
	CHECK(std::ranges::is_sorted(ShaderLibrary::GetNames()));

	// Registering the same table again fails: the names are taken.
	CHECK_FALSE(ShaderLibrary::Register(Shaders).HasValue());

	static constexpr std::array<EmbeddedShader, 1> Engine = { EmbeddedShader{ "FullscreenTriangle.vert", FakeSpirv } };
	CHECK_FALSE(ShaderLibrary::Register(Engine).HasValue());

	// A failed registration adds nothing, not even its valid entries.
	static constexpr std::array<EmbeddedShader, 2> Mixed = {
		EmbeddedShader{ "RegisterTestC.frag", FakeSpirv },
		EmbeddedShader{ "RegisterTestD.frag", NotSpirv },
	};
	CHECK_FALSE(ShaderLibrary::Register(Mixed).HasValue());
	CHECK(ShaderLibrary::Find("RegisterTestC.frag").empty());

	static constexpr std::array<EmbeddedShader, 1> Unnamed = { EmbeddedShader{ "", FakeSpirv } };
	CHECK_FALSE(ShaderLibrary::Register(Unnamed).HasValue());
	static constexpr std::array<EmbeddedShader, 1> Short = { EmbeddedShader{
		"RegisterTestE.frag", std::span<const uint32_t>(FakeSpirv).first(2) } };
	CHECK_FALSE(ShaderLibrary::Register(Short).HasValue());
}

TEST_CASE("RHI: the shader stage comes from the name's suffix")
{
	CHECK(GetShaderStageFromName("PBR.vert") == nvrhi::ShaderType::Vertex);
	CHECK(GetShaderStageFromName("PBR.frag") == nvrhi::ShaderType::Pixel);
	CHECK(GetShaderStageFromName("Blur.comp") == nvrhi::ShaderType::Compute);
	CHECK(GetShaderStageFromName("Shadow.geom") == nvrhi::ShaderType::Geometry);
	CHECK_FALSE(GetShaderStageFromName("PBR").has_value());
	CHECK_FALSE(GetShaderStageFromName("PBR.tesc").has_value());
	CHECK_FALSE(GetShaderStageFromName(".vert").has_value());
	CHECK_FALSE(GetShaderStageFromName("PBR.frag.glsl").has_value());
}
