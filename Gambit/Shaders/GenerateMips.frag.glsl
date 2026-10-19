#version 460
#extension GL_EXT_samplerless_texture_functions : require

// One level of a mip chain from the level above (RenderResources): a box filter over the 2x2 source texels of each
// destination texel, or over 3 texels along an axis of odd size, weighted by their overlap (non-power-of-two textures
// stay centered). Fetching from an sRGB texture decodes and rendering to one encodes, so the average is linear.

layout(set = 0, binding = 0) uniform texture2D u_Source; // a view of the level above only

layout(location = 0) in vec2 v_UV;

layout(location = 0) out vec4 o_Color;

// The source texels along one axis covering destination texel `index`, and their weights.
void Footprint(int index, int sourceSize, int destinationSize, out int first, out int count, out vec3 weights)
{
	first = index * 2;
	if (sourceSize == 1)
	{
		first = 0;
		count = 1;
		weights = vec3(1.0, 0.0, 0.0);
	}
	else if ((sourceSize & 1) == 0)
	{
		count = 2;
		weights = vec3(0.5, 0.5, 0.0);
	}
	else
	{
		// sourceSize = 2 * destinationSize + 1: each destination texel covers 2 + 1 / destinationSize source texels.
		float n = float(destinationSize);
		float i = float(index);
		count = 3;
		weights = vec3(n - i, n, i + 1.0) / (2.0 * n + 1.0);
	}
}

void main()
{
	ivec2 sourceSize = textureSize(u_Source, 0);
	ivec2 destination = ivec2(gl_FragCoord.xy);
	ivec2 destinationSize = max(sourceSize / 2, ivec2(1));
	int firstX;
	int countX;
	vec3 weightsX;
	int firstY;
	int countY;
	vec3 weightsY;
	Footprint(destination.x, sourceSize.x, destinationSize.x, firstX, countX, weightsX);
	Footprint(destination.y, sourceSize.y, destinationSize.y, firstY, countY, weightsY);
	vec4 sum = vec4(0.0);
	for (int y = 0; y < countY; ++y)
	{
		for (int x = 0; x < countX; ++x)
		{
			vec4 texel = texelFetch(u_Source, ivec2(firstX + x, firstY + y), 0);
			sum += texel * (weightsX[x] * weightsY[y]);
		}
	}
	o_Color = sum;
}
