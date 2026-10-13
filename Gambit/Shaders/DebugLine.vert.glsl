#version 460

// Debug lines (DebugDrawList): world-space positions with linear RGBA colors, drawn over the tonemapped image.

layout(push_constant) uniform LineConstants
{
	mat4 ViewProjection;
	uint EncodeSrgb; // the target stores UNORM values: encode the linear color in the pixel shader
} u_Line;

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec4 a_Color;

layout(location = 0) out vec4 v_Color;

void main()
{
	gl_Position = u_Line.ViewProjection * vec4(a_Position, 1.0);
	v_Color = a_Color;
}
