#pragma once

#include "Gambit/Renderer/MeshSource.h"

namespace Gambit
{
	// Operations on MeshData for importers and generators. They keep the submesh structure: functions that change the
	// vertex count rebuild each submesh's vertex range.
	namespace MeshUtils
	{
		// The bounds of every submesh, of the whole mesh and its bounding sphere.
		void ComputeBounds(MeshData& mesh);

		// Gives every triangle corner a vertex of its own with the triangle's face normal (glTF's rule for meshes
		// without normals: flat shading). Degenerate triangles get +Y.
		void GenerateFlatNormals(MeshData& mesh);

		// Tangents with MikkTSpace (D-037), the convention normal maps are baked in: needs normals; with UV0 the
		// tangents follow +U and the sign makes sign * cross(normal, tangent) point up the image (-V, glTF's UVs
		// start at the top left), without UV0 they are an arbitrary unit vector perpendicular to the normal. Vertices
		// whose corners get different tangents (UV seams, mirrored UVs) are split, identical ones merged, so the vertex
		// count can change; the order of first use is kept, which makes the result deterministic. Fails, leaving the
		// mesh as it was, without normals or for a submesh without triangles.
		[[nodiscard]] Result<void> GenerateTangents(MeshData& mesh);

		// Indices of a triangle strip / fan as a triangle list with the winding of the first triangle (glTF 2.0,
		// section 3.7.2.1), skipping degenerate triangles of strips.
		[[nodiscard]] std::vector<uint32_t> TriangleStripToList(std::span<const uint32_t> strip);
		[[nodiscard]] std::vector<uint32_t> TriangleFanToList(std::span<const uint32_t> fan);
	}
}
