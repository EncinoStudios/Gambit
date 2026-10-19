#version 460

// One triangle that covers the whole viewport, for post-processing passes: draw 3 vertices without a vertex buffer
// (DrawFullscreenTriangle in RHI/PipelineUtils.h). UV (0, 0) is the top-left corner of the render target and (1, 1) the
// bottom-right one, matching the target's own texture coordinates. Clip space has +Y up: NVRHI flips the Vulkan viewport.

layout(location = 0) out vec2 v_UV;

void main()
{
	v_UV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
	gl_Position = vec4(v_UV.x * 2.0 - 1.0, 1.0 - v_UV.y * 2.0, 0.0, 1.0);
}
