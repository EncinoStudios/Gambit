#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_samplerless_texture_functions : require

// A temporary output pass until the HDR post chain (T09): the fitted ACES curve (Narkowicz 2015) maps the pre-exposed
// scene color to [0, 1], encoded as sRGB for UNORM targets (sRGB targets encode in hardware).

#include "ColorSpace.glsl"

layout(set = 0, binding = 0) uniform texture2D u_SceneColor;

layout(push_constant) uniform TonemapConstants
{
	uint EncodeSrgb;
} u_Tonemap;

layout(location = 0) in vec2 v_UV;

layout(location = 0) out vec4 o_Color;

vec3 TonemapACESFitted(vec3 x)
{
	const float a = 2.51;
	const float b = 0.03;
	const float c = 2.43;
	const float d = 0.59;
	const float e = 0.14;
	return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main()
{
	vec3 color = texelFetch(u_SceneColor, ivec2(gl_FragCoord.xy), 0).rgb;
	// A NaN or infinity from a broken asset must not spread through later passes.
	color = mix(color, vec3(0.0), bvec3(any(isnan(color)) || any(isinf(color))));
	color = TonemapACESFitted(max(color, vec3(0.0)));
	o_Color = vec4(u_Tonemap.EncodeSrgb != 0u ? LinearToSrgb(color) : color, 1.0);
}
