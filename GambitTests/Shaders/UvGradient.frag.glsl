#version 460

// Writes the interpolated UV of FullscreenTriangle.vert, so a test can check coverage and orientation numerically.

layout(location = 0) in vec2 v_UV;

layout(location = 0) out vec4 o_Color;

void main()
{
	o_Color = vec4(v_UV, 0.0, 1.0);
}
