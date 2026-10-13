#pragma once

#include "Gambit/Math/Bounds.h"
#include "Gambit/Math/Math.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Gambit
{
	// One line of debug drawing, in world space with a linear RGBA color.
	struct DebugLine
	{
		glm::vec3 Start{ 0.0f };
		glm::vec3 End{ 0.0f };
		glm::vec4 Color{ 1.0f };
		// Hidden behind scene geometry (the default), or drawn over everything.
		bool DepthTested = true;
	};

	// A queue of debug lines that systems and scripts fill (Lua's Debug.DrawLine/DrawBox/DrawSphere, physics shapes,
	// gizmos) and the renderer draws. Presentation only: it is not part of the simulation state and never feeds back
	// into it. Shapes are stored as lines. A shape lives for `duration` seconds of ticks (Advance), and at least until
	// the next extraction, so one drawn every tick with duration 0 stays visible.
	class DebugDrawList
	{
	public:
		// Shapes beyond this many lines are dropped (a runaway loop must not exhaust memory).
		static constexpr size_t MaxLines = 1u << 20;

		void DrawLine(
			const glm::vec3& start, const glm::vec3& end, const glm::vec4& color, float duration = 0.0f,
			bool depthTested = true);
		// A box with half extents `extents` around `center`, rotated by `rotation`.
		void DrawBox(
			const glm::vec3& center, const glm::vec3& extents, const glm::quat& rotation, const glm::vec4& color,
			float duration = 0.0f, bool depthTested = true);
		void DrawAABB(const AABB& box, const glm::vec4& color, float duration = 0.0f, bool depthTested = true);
		// Three great circles of `segments` lines each.
		void DrawSphere(
			const glm::vec3& center, float radius, const glm::vec4& color, float duration = 0.0f,
			bool depthTested = true, uint32_t segments = 32);
		// A circle around `normal`.
		void DrawCircle(
			const glm::vec3& center, const glm::vec3& normal, float radius, const glm::vec4& color,
			float duration = 0.0f, bool depthTested = true, uint32_t segments = 32);
		// The X (red), Y (green) and Z (blue) axes of `transform`, `size` long.
		void DrawAxes(const glm::mat4& transform, float size = 1.0f, float duration = 0.0f, bool depthTested = true);
		// The edges of the volume that `viewProjection` maps to the clip cube (depth 0..1).
		void DrawFrustum(
			const glm::mat4& viewProjection, const glm::vec4& color, float duration = 0.0f, bool depthTested = true);

		// The lines to draw now.
		[[nodiscard]] const std::vector<DebugLine>& GetLines() const noexcept { return m_Lines; }
		[[nodiscard]] size_t GetLineCount() const noexcept { return m_Lines.size(); }
		[[nodiscard]] bool IsEmpty() const noexcept { return m_Lines.empty(); }
		// Lines refused because the list was full, since the last Clear.
		[[nodiscard]] uint64_t GetDroppedLineCount() const noexcept { return m_Dropped; }

		// Ages every line by `seconds` and removes those whose time is over.
		void Advance(float seconds);
		void Clear() noexcept;

	private:
		void Add(const DebugLine& line, float duration);

		std::vector<DebugLine> m_Lines;
		std::vector<float> m_Remaining; // seconds left, per line
		uint64_t m_Dropped = 0;
	};
}
