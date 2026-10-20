#version 460
#extension GL_GOOGLE_include_directive : require

#include "ColorSpace.glsl"

layout(push_constant) uniform LineConstants
{
	mat4 ViewProjection;
	uint EncodeSrgb;
} u_Line;

layout(location = 0) in vec4 v_Color;

layout(location = 0) out vec4 o_Color;

void main()
{
	o_Color = vec4(u_Line.EncodeSrgb != 0u ? LinearToSrgb(v_Color.rgb) : v_Color.rgb, v_Color.a);
}
