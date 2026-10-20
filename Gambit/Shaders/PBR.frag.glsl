#version 460
#extension GL_GOOGLE_include_directive : require

// Forward PBR shading (glTF 2.0 metallic-roughness) of punctual lights in physical units plus a uniform ambient
// light. The output is pre-exposed HDR luminance; alpha follows the material's alpha mode.

#include "PBR.glsl"
#include "SceneData.glsl"

layout(location = 0) in vec3 v_WorldPosition;
layout(location = 1) in vec3 v_Normal;
layout(location = 2) in vec4 v_Tangent;
layout(location = 3) in vec2 v_UV;
layout(location = 4) in vec4 v_Color;

layout(location = 0) out vec4 o_Color;

vec4 Sample(texture2D image, sampler imageSampler)
{
	return texture(sampler2D(image, imageSampler), v_UV);
}

void main()
{
	vec4 baseColor = u_Material.BaseColor * v_Color * Sample(u_BaseColorTexture, u_BaseColorSampler);
	if (u_Material.AlphaMode == AlphaModeMask && baseColor.a < u_Material.AlphaCutoff)
	{
		discard;
	}
	float alpha = u_Material.AlphaMode == AlphaModeBlend ? baseColor.a : 1.0;

	vec4 metallicRoughness = Sample(u_MetallicRoughnessTexture, u_MetallicRoughnessSampler);
	float metallic = clamp(u_Material.Metallic * metallicRoughness.b, 0.0, 1.0);
	float roughness = clamp(u_Material.Roughness * metallicRoughness.g, MinRoughness, 1.0);
	vec3 diffuseColor = baseColor.rgb * (1.0 - metallic);
	vec3 f0 = mix(vec3(0.04), baseColor.rgb, metallic);

	// The tangent frame (MikkTSpace): the bitangent comes from the interpolated, unnormalized normal and tangent.
	vec3 N = v_Normal;
	if ((u_Material.Flags & MaterialFlagNormalTexture) != 0u)
	{
		vec3 bitangent = v_Tangent.w * cross(v_Normal, v_Tangent.xyz);
		vec3 tangentNormal = Sample(u_NormalTexture, u_NormalSampler).xyz * 2.0 - 1.0;
		tangentNormal.xy *= u_Material.NormalScale;
		N = tangentNormal.x * v_Tangent.xyz + tangentNormal.y * bitangent + tangentNormal.z * v_Normal;
	}
	N = normalize(N);
	if (any(isnan(N)))
	{
		N = normalize(v_Normal);
	}
	// Back faces (of double-sided materials) show the surface from the other side: the whole frame flips.
	if (!gl_FrontFacing)
	{
		N = -N;
	}

	vec3 V = normalize(u_Frame.CameraPosition.xyz - v_WorldPosition);
	vec3 color = vec3(0.0);
	for (uint index = 0u; index < u_Frame.LightCount; ++index)
	{
		GpuLight light = u_Lights.Lights[index];
		uint type = uint(light.PositionType.w);
		vec3 L;
		float attenuation = 1.0;
		if (type == LightTypeDirectional)
		{
			L = -light.DirectionRange.xyz;
		}
		else
		{
			vec3 toLight = light.PositionType.xyz - v_WorldPosition;
			float distanceSquared = dot(toLight, toLight);
			L = toLight * inversesqrt(max(distanceSquared, 1e-8));
			attenuation = DistanceAttenuation(distanceSquared, light.DirectionRange.w);
			if (type == LightTypeSpot)
			{
				float cosAngle = dot(-L, light.DirectionRange.xyz);
				float spot = clamp((cosAngle - light.Spot.x) * light.Spot.y, 0.0, 1.0);
				attenuation *= spot * spot;
			}
		}
		color += EvaluateBRDF(N, V, L, diffuseColor, f0, roughness) * light.Color.rgb * attenuation;
	}

	float occlusion = mix(1.0, Sample(u_OcclusionTexture, u_OcclusionSampler).r, u_Material.OcclusionStrength);
	float NoV = max(dot(N, V), 1e-4);
	vec3 ambient = (diffuseColor + EnvironmentBRDFApprox(f0, roughness, NoV)) * u_Frame.AmbientRadiance.rgb;
	color += ambient * occlusion;
	color += u_Material.Emissive.rgb * Sample(u_EmissiveTexture, u_EmissiveSampler).rgb * u_Frame.Exposure;

	o_Color = vec4(color, alpha);
}
