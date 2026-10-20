#pragma once

#include "Gambit/Math/Math.h"

#include <cstdint>

// The C++ side of the data the scene shaders read (Gambit/Shaders/Include/SceneData.glsl), laid out as std140 (uniform
// blocks) and std430 (the light buffer) want it. Keep both in sync; the static_asserts check the sizes.

namespace Gambit::Gpu
{
	// Set 0, binding 0.
	struct FrameConstants
	{
		glm::mat4 View{ 1.0f };
		glm::mat4 Projection{ 1.0f };
		glm::mat4 ViewProjection{ 1.0f };
		glm::vec4 CameraPosition{ 0.0f };
		glm::vec4 AmbientRadiance{ 0.0f };
		float Exposure = 1.0f;
		float Time = 0.0f;
		uint32_t LightCount = 0;
		uint32_t Padding = 0;
	};
	static_assert(sizeof(FrameConstants) == 240);

	enum class LightType : uint32_t
	{
		Directional = 0,
		Point = 1,
		Spot = 2
	};

	// Set 0, binding 1: an element of the light buffer.
	struct Light
	{
		glm::vec4 PositionType{ 0.0f };
		glm::vec4 DirectionRange{ 0.0f };
		glm::vec4 Color{ 0.0f };
		glm::vec4 Spot{ 0.0f };
	};
	static_assert(sizeof(Light) == 64);

	// Push constants of every scene draw.
	struct DrawConstants
	{
		glm::mat4 Model{ 1.0f };
		glm::vec4 NormalMatrix[3] = { glm::vec4(1.0f, 0.0f, 0.0f, 1.0f), glm::vec4(0.0f, 1.0f, 0.0f, 0.0f),
			                          glm::vec4(0.0f, 0.0f, 1.0f, 0.0f) };
	};
	static_assert(sizeof(DrawConstants) == 112);

	// Set 1, binding 0.
	struct MaterialConstants
	{
		glm::vec4 BaseColor{ 1.0f };
		glm::vec4 Emissive{ 0.0f };
		glm::vec4 UVTransform{ 1.0f, 1.0f, 0.0f, 0.0f };
		float Metallic = 0.0f;
		float Roughness = 0.5f;
		float NormalScale = 1.0f;
		float OcclusionStrength = 1.0f;
		float AlphaCutoff = 0.5f;
		uint32_t AlphaMode = 0;
		uint32_t Flags = 0; // MaterialFlag* bits
		uint32_t Padding = 0;
	};
	static_assert(sizeof(MaterialConstants) == 80);

	// MaterialConstants::Flags: the material has a normal map.
	inline constexpr uint32_t MaterialFlagNormalTexture = 1u;

	// Binding slots (D-031: unique per set across resource types).
	inline constexpr uint32_t FrameSet = 0;
	inline constexpr uint32_t MaterialSet = 1;
	inline constexpr uint32_t FrameConstantsSlot = 0;
	inline constexpr uint32_t LightsSlot = 1;
	inline constexpr uint32_t DrawConstantsSlot = 2;
	inline constexpr uint32_t MaterialConstantsSlot = 0;
	// Texture n of a material (base color, metallic-roughness, normal, occlusion, emissive) is at 1 + 2n, its sampler
	// at 2 + 2n.
	inline constexpr uint32_t MaterialTextureCount = 5;
	[[nodiscard]] constexpr uint32_t MaterialTextureSlot(uint32_t index) noexcept
	{
		return 1 + 2 * index;
	}
	[[nodiscard]] constexpr uint32_t MaterialSamplerSlot(uint32_t index) noexcept
	{
		return 2 + 2 * index;
	}

	// Vertex streams: positions alone (depth-only passes read less), then the other attributes interleaved.
	struct VertexAttributes
	{
		glm::vec3 Normal{ 0.0f, 1.0f, 0.0f };
		glm::vec4 Tangent{ 1.0f, 0.0f, 0.0f, 1.0f };
		glm::vec2 UV0{ 0.0f };
		glm::vec2 UV1{ 0.0f };
		glm::vec4 Color{ 1.0f };
	};
	static_assert(sizeof(VertexAttributes) == 60);

	// Vertex of the debug line pass.
	struct LineVertex
	{
		glm::vec3 Position{ 0.0f };
		glm::vec4 Color{ 1.0f };
	};
	static_assert(sizeof(LineVertex) == 28);
}
