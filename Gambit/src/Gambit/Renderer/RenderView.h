#pragma once

#include "Gambit/Core/Base.h"
#include "Gambit/Math/Math.h"
#include "Gambit/Renderer/Components/RenderComponents.h"

#include <array>
#include <cstdint>

namespace Gambit
{
	struct RenderCamera;

	// Engine debug shapes the SceneRenderer can add to a view (on top of the debug lines of the scene).
	enum class DebugShapes : uint8_t
	{
		None = 0,
		Bounds = GB_BIT(0),  // the world bounds of every drawn mesh
		Lights = GB_BIT(1),  // light positions, directions and ranges
		Cameras = GB_BIT(2), // the frusta of the scene's cameras
	};

	[[nodiscard]] constexpr DebugShapes operator|(DebugShapes a, DebugShapes b) noexcept
	{
		return static_cast<DebugShapes>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
	}

	[[nodiscard]] constexpr bool HasFlag(DebugShapes shapes, DebugShapes flag) noexcept
	{
		return (static_cast<uint8_t>(shapes) & static_cast<uint8_t>(flag)) != 0;
	}

	// The exposure that maps luminance to output for a camera set to `ev100` (exposure value at ISO 100): the
	// saturation-based sensitivity model, 1 / (1.2 * 2^EV100). Direct sunlight wants about 15, an overcast day 12, a
	// lit interior 7.
	[[nodiscard]] float ExposureFromEV100(float ev100) noexcept;

	// The projection of a camera for an aspect ratio (width / height): reversed depth (the near plane at 1, the far
	// plane at 0, which spreads float depth precision evenly), clip-space +Y up (NVRHI flips the viewport, D-030).
	[[nodiscard]] glm::mat4 MakeProjection(const CameraComponent& camera, float aspectRatio);

	// How to render one view of a scene: where the eye is, the projection, the exposure and what to draw on top. The
	// editor builds views from its own camera; the game's come from scene cameras (FromCamera).
	struct RenderView
	{
		// Which view this is across frames: the SceneRenderer keeps render targets (and, later, temporal state such as
		// auto-exposure) per id, so each view that renders every frame (an editor viewport, the game camera, a
		// preview) needs an id of its own.
		uint64_t ViewId = 0;
		glm::mat4 View{ 1.0f };                         // world to view space (looking along -Z)
		glm::mat4 Projection{ 1.0f };                   // view to clip space, reversed depth (MakeProjection)
		glm::vec4 ClearColor{ 0.0f, 0.0f, 0.0f, 1.0f }; // linear RGBA behind everything
		float ExposureEV100 = 15.0f;
		bool DrawDebugLines = true; // the scene's debug lines
		DebugShapes Shapes = DebugShapes::None;

		// The view of a scene camera at the interpolation factor `alpha` (between the previous and the current
		// tick), for a target with this aspect ratio; its id is the camera entity's UUID.
		[[nodiscard]] static RenderView FromCamera(const RenderCamera& camera, float alpha, float aspectRatio);

		[[nodiscard]] glm::vec3 GetPosition() const;
	};
}
