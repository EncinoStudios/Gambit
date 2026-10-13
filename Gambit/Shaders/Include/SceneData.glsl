#ifndef GAMBIT_SCENE_DATA_GLSL
#define GAMBIT_SCENE_DATA_GLSL

// The data every scene pass shares (SceneRenderer): set 0 holds the view's constants and lights, set 1 the material,
// push constants the draw. The C++ mirrors are in Renderer/GpuTypes.h; keep both in sync.

const uint LightTypeDirectional = 0u;
const uint LightTypePoint = 1u;
const uint LightTypeSpot = 2u;

layout(set = 0, binding = 0) uniform FrameConstants
{
	mat4 View;
	mat4 Projection;
	mat4 ViewProjection;
	vec4 CameraPosition;  // xyz: world position of the eye
	vec4 AmbientRadiance; // rgb: luminance of the uniform ambient light (cd/m^2), pre-exposed
	float Exposure;       // multiplies every luminance: 1 / (1.2 * 2^EV100)
	float Time;           // seconds of simulated time
	uint LightCount;
	uint FramePadding;
} u_Frame;

// One punctual light. Colors carry the intensity in physical units, pre-exposed: lux for directional lights, candela
// for point and spot lights.
struct GpuLight
{
	vec4 PositionType;   // xyz: world position (point, spot); w: light type
	vec4 DirectionRange; // xyz: unit direction the light travels (directional, spot); w: range (0: unlimited)
	vec4 Color;          // rgb: color times intensity times exposure
	vec4 Spot;           // x: cos(outer angle), y: 1 / max(cos(inner) - cos(outer), 1e-4)
};

layout(std430, set = 0, binding = 1) readonly buffer LightBuffer
{
	GpuLight Lights[];
} u_Lights;

layout(push_constant) uniform DrawConstants
{
	mat4 Model;
	// The inverse transpose of the model's 3x3 part, by columns; NormalMatrix[0].w is the sign of the model's
	// determinant (-1 for mirrored transforms, which flip the bitangent).
	vec4 NormalMatrix[3];
} u_Draw;

layout(set = 1, binding = 0) uniform MaterialConstants
{
	vec4 BaseColor;
	vec4 Emissive; // rgb: emissive color times strength
	vec4 UVTransform; // xy: tiling, zw: offset
	float Metallic;
	float Roughness;
	float NormalScale;
	float OcclusionStrength;
	float AlphaCutoff;
	uint AlphaMode; // 0 opaque, 1 mask, 2 blend
	uint Flags;     // MaterialFlag* bits
	uint MaterialPadding;
} u_Material;

layout(set = 1, binding = 1) uniform texture2D u_BaseColorTexture;
layout(set = 1, binding = 2) uniform sampler u_BaseColorSampler;
layout(set = 1, binding = 3) uniform texture2D u_MetallicRoughnessTexture;
layout(set = 1, binding = 4) uniform sampler u_MetallicRoughnessSampler;
layout(set = 1, binding = 5) uniform texture2D u_NormalTexture;
layout(set = 1, binding = 6) uniform sampler u_NormalSampler;
layout(set = 1, binding = 7) uniform texture2D u_OcclusionTexture;
layout(set = 1, binding = 8) uniform sampler u_OcclusionSampler;
layout(set = 1, binding = 9) uniform texture2D u_EmissiveTexture;
layout(set = 1, binding = 10) uniform sampler u_EmissiveSampler;

const uint AlphaModeOpaque = 0u;
const uint AlphaModeMask = 1u;
const uint AlphaModeBlend = 2u;

// The material has a normal map (without one the interpolated normal is used as it is).
const uint MaterialFlagNormalTexture = 1u;

mat3 GetNormalMatrix()
{
	return mat3(u_Draw.NormalMatrix[0].xyz, u_Draw.NormalMatrix[1].xyz, u_Draw.NormalMatrix[2].xyz);
}

vec2 TransformUV(vec2 uv)
{
	return uv * u_Material.UVTransform.xy + u_Material.UVTransform.zw;
}

#endif
