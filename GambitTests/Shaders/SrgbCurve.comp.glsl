#version 460
#extension GL_GOOGLE_include_directive : require

// Samples the sRGB transfer functions of the shared ColorSpace.glsl at Count evenly spaced inputs, so a test can
// compare the GPU results with the CPU versions (and exercise compute, push constants, storage buffers and includes).

#include "ColorSpace.glsl"

layout(local_size_x = 64) in;

layout(push_constant) uniform Parameters
{
	uint Count;
} u_Parameters;

// Per input i: [2 * i] = LinearToSrgb(i / (Count - 1)), [2 * i + 1] = SrgbToLinear(i / (Count - 1)).
layout(set = 0, binding = 0) buffer Output
{
	float Values[];
} o_Output;

void main()
{
	uint i = gl_GlobalInvocationID.x;
	if (i >= u_Parameters.Count)
	{
		return;
	}
	float x = float(i) / float(u_Parameters.Count - 1u);
	o_Output.Values[2u * i] = LinearToSrgb(vec3(x)).r;
	o_Output.Values[2u * i + 1u] = SrgbToLinear(vec3(x)).r;
}
