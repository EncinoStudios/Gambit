#include "gbpch.h"

#include "Gambit/Renderer/Components/RenderComponents.h"

#include "Gambit/Reflection/TypeRegistry.h"

namespace Gambit
{
	void RegisterRenderComponents(TypeRegistry& registry)
	{
		registry.RegisterEnum<ProjectionType>("ProjectionType")
			.Value("Perspective", ProjectionType::Perspective)
			.Value("Orthographic", ProjectionType::Orthographic);

		registry.RegisterComponent<MeshRendererComponent>("MeshRenderer")
			.Field(
				"Mesh", &MeshRendererComponent::Mesh, Attr::AssetType{ "Mesh" },
				Attr::Tooltip{ "A mesh, or a model whose meshes are all drawn" })
			.Field(
				"SubmeshIndex", &MeshRendererComponent::SubmeshIndex, Attr::Range{ -1.0, 1024.0 },
				Attr::Tooltip{ "The submesh to draw; -1 draws all" })
			.Field(
				"MaterialOverrides", &MeshRendererComponent::MaterialOverrides, Attr::AssetType{ "Material" },
				Attr::Tooltip{ "Per material slot; null keeps the mesh's material" })
			.Field("CastShadows", &MeshRendererComponent::CastShadows)
			.Field("ReceiveShadows", &MeshRendererComponent::ReceiveShadows)
			.Field("Visible", &MeshRendererComponent::Visible);

		registry.RegisterComponent<CameraComponent>("Camera")
			.Field("Projection", &CameraComponent::Projection)
			.Field(
				"VerticalFOV", &CameraComponent::VerticalFOV, Attr::Range{ 1.0, 179.0 },
				Attr::Tooltip{ "Degrees (perspective)" })
			.Field("Near", &CameraComponent::Near, Attr::Range{ 0.001, 1000.0 })
			.Field("Far", &CameraComponent::Far, Attr::Range{ 0.01, 1000000.0 })
			.Field(
				"OrthoSize", &CameraComponent::OrthoSize, Attr::Range{ 0.01, 10000.0 },
				Attr::Tooltip{ "Height of the view in world units (orthographic)" })
			.Field("Primary", &CameraComponent::Primary, Attr::Tooltip{ "The camera the game renders through" })
			.Field("ClearColor", &CameraComponent::ClearColor, Attr::Color{});

		registry.RegisterComponent<DirectionalLightComponent>("DirectionalLight")
			.Field("Color", &DirectionalLightComponent::Color, Attr::Color{})
			.Field(
				"Illuminance", &DirectionalLightComponent::Illuminance, Attr::Range{ 0.0, 200000.0 },
				Attr::Tooltip{ "Lux; direct sunlight is about 100000" })
			.Field("CastShadows", &DirectionalLightComponent::CastShadows);

		registry.RegisterComponent<PointLightComponent>("PointLight")
			.Field("Color", &PointLightComponent::Color, Attr::Color{})
			.Field(
				"Intensity", &PointLightComponent::Intensity, Attr::Range{ 0.0, 1000000.0 },
				Attr::Tooltip{ "Lumens; a 60 W bulb is about 800" })
			.Field(
				"Range", &PointLightComponent::Range, Attr::Range{ 0.0, 10000.0 },
				Attr::Tooltip{ "Where the light fades out; 0 means no limit" });

		registry.RegisterComponent<SpotLightComponent>("SpotLight")
			.Field("Color", &SpotLightComponent::Color, Attr::Color{})
			.Field(
				"Intensity", &SpotLightComponent::Intensity, Attr::Range{ 0.0, 1000000.0 },
				Attr::Tooltip{ "Lumens, as a point light of the same power" })
			.Field(
				"Range", &SpotLightComponent::Range, Attr::Range{ 0.0, 10000.0 },
				Attr::Tooltip{ "Where the light fades out; 0 means no limit" })
			.Field(
				"InnerConeAngle", &SpotLightComponent::InnerConeAngle, Attr::Range{ 0.0, 89.0 },
				Attr::Tooltip{ "Degrees from the axis where the light starts to fade" })
			.Field(
				"OuterConeAngle", &SpotLightComponent::OuterConeAngle, Attr::Range{ 0.0, 89.0 },
				Attr::Tooltip{ "Degrees from the axis where the light has faded out" });
	}
}
