#ifndef GAMBIT_PBR_GLSL
#define GAMBIT_PBR_GLSL

// The metallic-roughness BRDF of glTF 2.0 (Filament's formulation): GGX normal distribution, height-correlated Smith
// visibility, Schlick Fresnel and Lambert diffuse. Roughness is perceptual (alpha = roughness^2).

const float Pi = 3.14159265358979;
// Below this perceptual roughness GGX highlights of punctual lights alias to single pixels.
const float MinRoughness = 0.045;

float DistributionGGX(float NoH, float alpha)
{
	float a2 = alpha * alpha;
	float f = (NoH * a2 - NoH) * NoH + 1.0;
	return a2 / (Pi * f * f);
}

float VisibilitySmithGGXCorrelated(float NoV, float NoL, float alpha)
{
	float a2 = alpha * alpha;
	float lambdaV = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
	float lambdaL = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
	return 0.5 / max(lambdaV + lambdaL, 1e-7);
}

vec3 FresnelSchlick(float VoH, vec3 f0)
{
	float f = pow(1.0 - VoH, 5.0);
	return f0 + (vec3(1.0) - f0) * f;
}

// The light reflected towards V per unit of illuminance arriving from L (BRDF times cos), for a surface with these
// parameters.
vec3 EvaluateBRDF(vec3 N, vec3 V, vec3 L, vec3 diffuseColor, vec3 f0, float roughness)
{
	vec3 H = normalize(V + L);
	float NoV = max(dot(N, V), 1e-4);
	float NoL = clamp(dot(N, L), 0.0, 1.0);
	float NoH = clamp(dot(N, H), 0.0, 1.0);
	float VoH = clamp(dot(V, H), 0.0, 1.0);
	float alpha = roughness * roughness;
	vec3 specular = DistributionGGX(NoH, alpha) * VisibilitySmithGGXCorrelated(NoV, NoL, alpha) * FresnelSchlick(VoH, f0);
	vec3 diffuse = diffuseColor / Pi;
	return (diffuse + specular) * NoL;
}

// Karis' analytic fit of the split-sum environment BRDF (f0 * A + B), for the uniform ambient light until
// image-based lighting (T09) brings the real lookup table.
vec3 EnvironmentBRDFApprox(vec3 f0, float roughness, float NoV)
{
	const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
	const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
	vec4 r = roughness * c0 + c1;
	float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
	vec2 ab = vec2(-1.04, 1.04) * a004 + r.zw;
	return f0 * ab.x + ab.y;
}

// Inverse-square falloff that fades to zero at `range` (Karis); range 0 is unlimited.
float DistanceAttenuation(float distanceSquared, float range)
{
	float attenuation = 1.0 / max(distanceSquared, 1e-4);
	if (range > 0.0)
	{
		float ratio = distanceSquared / (range * range);
		float window = clamp(1.0 - ratio * ratio, 0.0, 1.0);
		attenuation *= window * window;
	}
	return attenuation;
}

#endif
