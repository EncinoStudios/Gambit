#ifndef GAMBIT_COLOR_SPACE_GLSL
#define GAMBIT_COLOR_SPACE_GLSL

// The exact piecewise sRGB transfer functions (IEC 61966-2-1), for shaders that write to UNORM targets or read sRGB data
// stored without an sRGB format. They match Gambit::LinearToSrgb / SrgbToLinear on the CPU (RHI/Image.h).

vec3 LinearToSrgb(vec3 linearColor)
{
	vec3 c = clamp(linearColor, 0.0, 1.0);
	return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, greaterThan(c, vec3(0.0031308)));
}

vec3 SrgbToLinear(vec3 srgbColor)
{
	vec3 c = clamp(srgbColor, 0.0, 1.0);
	return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), greaterThan(c, vec3(0.04045)));
}

#endif
