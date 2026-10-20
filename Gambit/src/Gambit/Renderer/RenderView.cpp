#include "gbpch.h"

#include "Gambit/Renderer/RenderView.h"

#include "Gambit/Renderer/RenderScene.h"

namespace Gambit
{
	float ExposureFromEV100(float ev100) noexcept
	{
		return 1.0f / (1.2f * std::exp2(ev100));
	}

	glm::mat4 MakeProjection(const CameraComponent& camera, float aspectRatio)
	{
		const float aspect = std::isfinite(aspectRatio) && aspectRatio > 0.0f ? aspectRatio : 1.0f;
		const float nearPlane = std::max(camera.Near, 1e-4f);
		const float farPlane = std::max(camera.Far, nearPlane * 1.001f);
		// Swapping near and far in GLM's zero-to-one projections reverses the depth range.
		if (camera.Projection == ProjectionType::Orthographic)
		{
			const float halfHeight = 0.5f * std::max(camera.OrthoSize, 1e-4f);
			const float halfWidth = halfHeight * aspect;
			return glm::orthoRH_ZO(-halfWidth, halfWidth, -halfHeight, halfHeight, farPlane, nearPlane);
		}
		const float fov = glm::radians(glm::clamp(camera.VerticalFOV, 1.0f, 179.0f));
		return glm::perspectiveRH_ZO(fov, aspect, farPlane, nearPlane);
	}

	RenderView RenderView::FromCamera(const RenderCamera& camera, float alpha, float aspectRatio)
	{
		RenderView view;
		view.ViewId = camera.Entity.GetValue();
		view.View = glm::inverse(camera.Transform.Interpolate(alpha));
		view.Projection = MakeProjection(camera.Camera, aspectRatio);
		view.ClearColor = camera.Camera.ClearColor;
		return view;
	}

	glm::vec3 RenderView::GetPosition() const
	{
		return glm::vec3(glm::inverse(View)[3]);
	}
}
