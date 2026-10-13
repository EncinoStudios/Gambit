#version 460
#extension GL_GOOGLE_include_directive : require

// The forward pass's vertex shader: world-space position, normal and tangent frame for lighting, material UVs.

#include "SceneData.glsl"

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec4 a_Tangent; // xyz: tangent, w: bitangent sign (MikkTSpace)
layout(location = 3) in vec2 a_UV0;
layout(location = 4) in vec2 a_UV1;
layout(location = 5) in vec4 a_Color;

layout(location = 0) out vec3 v_WorldPosition;
layout(location = 1) out vec3 v_Normal;
layout(location = 2) out vec4 v_Tangent;
layout(location = 3) out vec2 v_UV;
layout(location = 4) out vec4 v_Color;

// The depth prepass computes the same position: depth testing against it needs identical results.
invariant gl_Position;

void main()
{
	vec4 worldPosition = u_Draw.Model * vec4(a_Position, 1.0);
	gl_Position = u_Frame.ViewProjection * worldPosition;
	v_WorldPosition = worldPosition.xyz;
	// Not normalized: MikkTSpace's convention reconstructs the bitangent from the interpolated vectors as they are.
	v_Normal = GetNormalMatrix() * a_Normal;
	v_Tangent = vec4(mat3(u_Draw.Model) * a_Tangent.xyz, a_Tangent.w * u_Draw.NormalMatrix[0].w);
	v_UV = TransformUV(a_UV0);
	v_Color = a_Color;
}
