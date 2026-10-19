#pragma once

#include "Gambit/Core/UUID.h"
#include "Gambit/Math/Math.h"

#include <cstdint>
#include <numbers>
#include <vector>

// The components the renderer reads (D-020: plain data). Render extraction copies them into a RenderScene every tick
// (D-032); the renderer never reads the World. Directions follow glTF: cameras look and lights shine along the
// entity's local -Z axis, +Y is up.

namespace Gambit
{
	class TypeRegistry;

	// Draws a mesh with the entity's world transform.
	struct MeshRendererComponent
	{
		// A Mesh asset (or a Model asset: then every mesh of the model, placed by its nodes).
		UUID Mesh;
		// The submesh to draw; -1 draws all of them.
		int32_t SubmeshIndex = -1;
		// Replaces the mesh's material of slot i with MaterialOverrides[i] where that is not null.
		std::vector<UUID> MaterialOverrides;
		bool CastShadows = true;
		bool ReceiveShadows = true;
		bool Visible = true;
	};

	enum class ProjectionType : uint8_t
	{
		Perspective,
		Orthographic
	};

	// A camera looking along the entity's -Z axis. The aspect ratio comes from the view it renders.
	struct CameraComponent
	{
		ProjectionType Projection = ProjectionType::Perspective;
		float VerticalFOV = 60.0f; // degrees, perspective only
		float Near = 0.1f;
		float Far = 1000.0f;
		float OrthoSize = 10.0f; // the height of the view volume in world units, orthographic only
		// The camera the game renders through (the one with the smallest UUID when several are primary).
		bool Primary = true;
		glm::vec4 ClearColor{ 0.05f, 0.05f, 0.06f, 1.0f }; // linear RGBA, what is behind everything
	};

	// Lumens of a point or spot light per candela of luminous intensity in every direction: 4 pi steradians (the light
	// components hold lumens, glTF and the shaders candela).
	inline constexpr float LumensPerCandela = 4.0f * std::numbers::pi_v<float>;

	// Sunlight: parallel rays along the entity's -Z axis.
	struct DirectionalLightComponent
	{
		glm::vec3 Color{ 1.0f };       // linear RGB
		float Illuminance = 100000.0f; // lux on a surface facing the light (direct sun is about 100000)
		bool CastShadows = true;
	};

	// Light from a point in all directions.
	struct PointLightComponent
	{
		glm::vec3 Color{ 1.0f };
		float Intensity = 800.0f; // luminous power in lumens (a 60 W incandescent bulb is about 800)
		float Range = 10.0f;      // the distance at which the light fades out completely; 0 means no limit
	};

	// A cone of light along the entity's -Z axis.
	struct SpotLightComponent
	{
		glm::vec3 Color{ 1.0f };
		float Intensity = 800.0f;     // lumens, as a point light of the same power (the cone masks it)
		float Range = 10.0f;          // 0 means no limit
		float InnerConeAngle = 20.0f; // degrees from the axis where the light starts to fade
		float OuterConeAngle = 30.0f; // degrees from the axis where it has faded out
	};

	// Registers the enum "ProjectionType" and the components "MeshRenderer", "Camera", "DirectionalLight",
	// "PointLight" and "SpotLight".
	void RegisterRenderComponents(TypeRegistry& registry);
}
