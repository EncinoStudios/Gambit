#include "gbpch.h"

#include "Gambit/Renderer/RenderScene.h"

namespace Gambit
{
	glm::mat4 RenderTransform::Interpolate(float alpha) const
	{
		if (alpha >= 1.0f || Previous == Current)
		{
			return Current;
		}
		glm::vec3 previousTranslation;
		glm::quat previousRotation;
		glm::vec3 previousScale;
		glm::vec3 currentTranslation;
		glm::quat currentRotation;
		glm::vec3 currentScale;
		if (!DecomposeTransform(Previous, previousTranslation, previousRotation, previousScale)
		    || !DecomposeTransform(Current, currentTranslation, currentRotation, currentScale))
		{
			return Current;
		}
		const float t = std::max(alpha, 0.0f);
		// The shorter arc: q and -q are the same rotation.
		if (glm::dot(previousRotation, currentRotation) < 0.0f)
		{
			currentRotation = -currentRotation;
		}
		return ComposeTransform(
			glm::mix(previousTranslation, currentTranslation, t),
			glm::normalize(glm::slerp(previousRotation, currentRotation, t)), glm::mix(previousScale, currentScale, t));
	}

	const RenderCamera* RenderScene::FindPrimaryCamera() const
	{
		const RenderCamera* found = nullptr;
		for (const RenderCamera& camera : Cameras)
		{
			if (camera.Camera.Primary && (found == nullptr || camera.Entity < found->Entity))
			{
				found = &camera;
			}
		}
		return found;
	}

	void RenderScene::Clear()
	{
		Cameras.clear();
		DrawItems.clear();
		DirectionalLights.clear();
		PointLights.clear();
		SpotLights.clear();
		Environment = {};
		DebugLines.clear();
		Tick = 0;
	}
}
