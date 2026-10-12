#version 460

// The golden-image triangle: three vertices without a vertex buffer, red at the top, green bottom-right, blue
// bottom-left (clip space +Y is up).

layout(location = 0) out vec3 outColor;

const vec2 Positions[3] = vec2[](vec2(0.0, 0.8), vec2(0.8, -0.8), vec2(-0.8, -0.8));
const vec3 Colors[3] = vec3[](vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0), vec3(0.0, 0.0, 1.0));

void main()
{
	outColor = Colors[gl_VertexIndex];
	gl_Position = vec4(Positions[gl_VertexIndex], 0.0, 1.0);
}
