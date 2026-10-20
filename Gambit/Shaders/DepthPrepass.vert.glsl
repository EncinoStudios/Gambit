#version 460
#extension GL_GOOGLE_include_directive : require

// The depth prepass: the position exactly as PBR.vert computes it, and the UV and vertex alpha for alpha-masked
// materials. Its input layout holds only these attributes (MeshPipelines), so the locations differ from PBR.vert's.

#include "SceneData.glsl"

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec2 a_UV0;
layout(location = 2) in vec4 a_Color;

layout(location = 0) out vec2 v_UV;
layout(location = 1) out float v_Alpha;

invariant gl_Position;

void main()
{
	vec4 worldPosition = u_Draw.Model * vec4(a_Position, 1.0);
	gl_Position = u_Frame.ViewProjection * worldPosition;
	v_UV = TransformUV(a_UV0);
	v_Alpha = a_Color.a;
}
