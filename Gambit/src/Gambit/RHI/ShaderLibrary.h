#pragma once

#include "Gambit/Core/Result.h"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace Gambit
{
	// One compiled shader embedded by gb_add_shaders: its name ("PBR.frag", the file name without ".glsl") and its
	// SPIR-V words.
	struct EmbeddedShader
	{
		std::string_view Name;
		std::span<const uint32_t> Code;
	};

	// The SPIR-V of every shader compiled into the program (D-031). The engine's own shaders (Gambit/Shaders) are
	// always present; other targets that embed shaders (tests, tools) add theirs with Register. Thread safe.
	class ShaderLibrary
	{
	public:
		// The SPIR-V of the shader called `name` ("FullscreenTriangle.vert"), or an empty span when there is none.
		[[nodiscard]] static std::span<const uint32_t> Find(std::string_view name);

		// Makes `shaders` (for example Gambit::EmbeddedShaders::GetGambitTestsShaders()) findable. The spans must stay
		// valid for the rest of the program, as generated tables do. Fails without registering anything when a name is
		// already taken or a shader is not SPIR-V; registering the same table twice succeeds once and then fails.
		[[nodiscard]] static Result<void> Register(std::span<const EmbeddedShader> shaders);

		// All shader names, sorted.
		[[nodiscard]] static std::vector<std::string_view> GetNames();
	};
}
