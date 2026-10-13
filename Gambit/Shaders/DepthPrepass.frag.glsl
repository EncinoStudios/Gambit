#version 460
#extension GL_GOOGLE_include_directive : require

// The depth prepass of alpha-masked materials: discards what the forward pass would discard (opaque materials need no
// pixel shader).

#include "SceneData.glsl"

layout(location = 0) in vec2 v_UV;
layout(location = 1) in float v_Alpha;

void main()
{
	float alpha = u_Material.BaseColor.a * v_Alpha
		* texture(sampler2D(u_BaseColorTexture, u_BaseColorSampler), v_UV).a;
	if (alpha < u_Material.AlphaCutoff)
	{
		discard;
	}
}
