#include "gbpch.h"

#include "Gambit/Renderer/DebugDraw.h"

#include <numbers>

namespace Gambit
{
	void DebugDrawList::Add(const DebugLine& line, float duration)
	{
		if (m_Lines.size() >= MaxLines)
		{
			++m_Dropped;
			return;
		}
		m_Lines.push_back(line);
		m_Remaining.push_back(std::isfinite(duration) && duration > 0.0f ? duration : 0.0f);
	}

	void DebugDrawList::DrawLine(
		const glm::vec3& start, const glm::vec3& end, const glm::vec4& color, float duration, bool depthTested)
	{
		Add(DebugLine{ start, end, color, depthTested }, duration);
	}

	void DebugDrawList::DrawBox(
		const glm::vec3& center, const glm::vec3& extents, const glm::quat& rotation, const glm::vec4& color,
		float duration, bool depthTested)
	{
		std::array<glm::vec3, 8> corners;
		for (int index = 0; index < 8; ++index)
		{
			const glm::vec3 sign(index & 1 ? 1.0f : -1.0f, index & 2 ? 1.0f : -1.0f, index & 4 ? 1.0f : -1.0f);
			corners[static_cast<size_t>(index)] = center + rotation * (sign * extents);
		}
		// Corners differing in one bit share an edge.
		for (int index = 0; index < 8; ++index)
		{
			for (int bit = 1; bit < 8; bit <<= 1)
			{
				if ((index & bit) == 0)
				{
					DrawLine(
						corners[static_cast<size_t>(index)], corners[static_cast<size_t>(index | bit)], color, duration,
						depthTested);
				}
			}
		}
	}

	void DebugDrawList::DrawAABB(const AABB& box, const glm::vec4& color, float duration, bool depthTested)
	{
		if (!box.IsEmpty())
		{
			DrawBox(box.GetCenter(), box.GetExtents(), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), color, duration, depthTested);
		}
	}

	void DebugDrawList::DrawCircle(
		const glm::vec3& center, const glm::vec3& normal, float radius, const glm::vec4& color, float duration,
		bool depthTested, uint32_t segments)
	{
		const float length = glm::length(normal);
		if (length <= 0.0f || segments < 3)
		{
			return;
		}
		const glm::vec3 axis = normal / length;
		const glm::vec3 helper = std::abs(axis.y) < 0.999f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
		const glm::vec3 u = glm::normalize(glm::cross(helper, axis));
		const glm::vec3 v = glm::cross(axis, u);
		glm::vec3 previous = center + u * radius;
		for (uint32_t segment = 1; segment <= segments; ++segment)
		{
			const float angle =
				2.0f * std::numbers::pi_v<float> * static_cast<float>(segment) / static_cast<float>(segments);
			const glm::vec3 point = center + (u * std::cos(angle) + v * std::sin(angle)) * radius;
			DrawLine(previous, point, color, duration, depthTested);
			previous = point;
		}
	}

	void DebugDrawList::DrawSphere(
		const glm::vec3& center, float radius, const glm::vec4& color, float duration, bool depthTested,
		uint32_t segments)
	{
		DrawCircle(center, glm::vec3(1.0f, 0.0f, 0.0f), radius, color, duration, depthTested, segments);
		DrawCircle(center, glm::vec3(0.0f, 1.0f, 0.0f), radius, color, duration, depthTested, segments);
		DrawCircle(center, glm::vec3(0.0f, 0.0f, 1.0f), radius, color, duration, depthTested, segments);
	}

	void DebugDrawList::DrawAxes(const glm::mat4& transform, float size, float duration, bool depthTested)
	{
		const glm::vec3 origin(transform[3]);
		DrawLine(origin, origin + glm::vec3(transform[0]) * size, { 1.0f, 0.0f, 0.0f, 1.0f }, duration, depthTested);
		DrawLine(origin, origin + glm::vec3(transform[1]) * size, { 0.0f, 1.0f, 0.0f, 1.0f }, duration, depthTested);
		DrawLine(origin, origin + glm::vec3(transform[2]) * size, { 0.0f, 0.0f, 1.0f, 1.0f }, duration, depthTested);
	}

	void DebugDrawList::DrawFrustum(
		const glm::mat4& viewProjection, const glm::vec4& color, float duration, bool depthTested)
	{
		const glm::mat4 inverse = glm::inverse(viewProjection);
		std::array<glm::vec3, 8> corners;
		for (int index = 0; index < 8; ++index)
		{
			const glm::vec4 clip(index & 1 ? 1.0f : -1.0f, index & 2 ? 1.0f : -1.0f, index & 4 ? 1.0f : 0.0f, 1.0f);
			const glm::vec4 world = inverse * clip;
			corners[static_cast<size_t>(index)] = glm::vec3(world) / world.w;
		}
		for (int index = 0; index < 8; ++index)
		{
			for (int bit = 1; bit < 8; bit <<= 1)
			{
				if ((index & bit) == 0)
				{
					DrawLine(
						corners[static_cast<size_t>(index)], corners[static_cast<size_t>(index | bit)], color, duration,
						depthTested);
				}
			}
		}
	}

	void DebugDrawList::Advance(float seconds)
	{
		size_t kept = 0;
		for (size_t index = 0; index < m_Lines.size(); ++index)
		{
			const float remaining = m_Remaining[index] - seconds;
			if (remaining > 0.0f)
			{
				m_Lines[kept] = m_Lines[index];
				m_Remaining[kept] = remaining;
				++kept;
			}
		}
		m_Lines.resize(kept);
		m_Remaining.resize(kept);
	}

	void DebugDrawList::Clear() noexcept
	{
		m_Lines.clear();
		m_Remaining.clear();
		m_Dropped = 0;
	}
}
