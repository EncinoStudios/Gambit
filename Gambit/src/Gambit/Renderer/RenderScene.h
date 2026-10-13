#pragma once

#include "Gambit/Asset/AssetType.h"
#include "Gambit/Core/UUID.h"
#include "Gambit/Math/Math.h"
#include "Gambit/Renderer/Components/RenderComponents.h"
#include "Gambit/Renderer/DebugDraw.h"

#include <cstdint>
#include <vector>

namespace Gambit
{
	// The transform of an extracted object at the previous and at the current tick: the renderer draws it at
	// Interpolate(alpha), so the render rate is independent of the simulation rate (D-011, D-032).
	struct RenderTransform
	{
		glm::mat4 Previous{ 1.0f };
		glm::mat4 Current{ 1.0f };

		// Translation and scale interpolate linearly, rotation along the shorter arc; alpha 0 is the previous tick,
		// 1 the current one. Matrices that are not translation-rotation-scale (shear from non-uniform parent scale)
		// snap to Current.
		[[nodiscard]] glm::mat4 Interpolate(float alpha) const;
	};

	struct RenderCamera
	{
		UUID Entity;
		RenderTransform Transform; // camera to world
		CameraComponent Camera;
	};

	struct RenderDrawItem
	{
		UUID Entity;
		RenderTransform Transform;
		AssetHandle Mesh; // a Mesh or a Model
		int32_t SubmeshIndex = -1;
		std::vector<AssetHandle> MaterialOverrides;
		bool CastShadows = true;
		bool ReceiveShadows = true;
	};

	struct RenderDirectionalLight
	{
		UUID Entity;
		RenderTransform Transform;
		DirectionalLightComponent Light;
	};

	struct RenderPointLight
	{
		UUID Entity;
		RenderTransform Transform;
		PointLightComponent Light;
	};

	struct RenderSpotLight
	{
		UUID Entity;
		RenderTransform Transform;
		SpotLightComponent Light;
	};

	// Scene-wide lighting that is not a light entity. A uniform ambient light until image-based lighting replaces it
	// (T09).
	struct RenderEnvironment
	{
		glm::vec3 AmbientColor{ 1.0f }; // linear RGB
		float AmbientLuminance = 0.0f;  // cd/m^2 of the uniform sky that lights everything from all directions
	};

	// What the renderer needs to know about a world, copied from its components by the RenderExtractionSystem (D-032).
	// The renderer reads only this, never the World. Lists are in the order of the entities' UUIDs.
	struct RenderScene
	{
		std::vector<RenderCamera> Cameras;
		std::vector<RenderDrawItem> DrawItems;
		std::vector<RenderDirectionalLight> DirectionalLights;
		std::vector<RenderPointLight> PointLights;
		std::vector<RenderSpotLight> SpotLights;
		RenderEnvironment Environment;
		std::vector<DebugLine> DebugLines;
		uint64_t Tick = 0; // the world's tick when it was extracted

		// The primary camera with the smallest UUID; null when there is none.
		[[nodiscard]] const RenderCamera* FindPrimaryCamera() const;
		void Clear();
	};
}
