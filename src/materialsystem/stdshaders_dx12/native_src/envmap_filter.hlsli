#ifndef ENVMAP_FILTER_HLSLI
#define ENVMAP_FILTER_HLSLI

// Pair the four positive cubic B-spline weights on each axis into two bilinear taps.
void Envmap_CubicAxis( float f, out float2 offset, out float blend )
{
	float g = 1.0 - f;
	float w0 = g * g * g / 6.0;
	float w1 = 2.0 / 3.0 - f * f + 0.5 * f * f * f;
	float w2 = 2.0 / 3.0 - g * g + 0.5 * g * g * g;
	float w3 = f * f * f / 6.0;
	offset = float2( -1.0 - f + w1 / ( w0 + w1 ), 1.0 - f + w3 / ( w2 + w3 ) );
	blend = w2 + w3;
}

// The two non-dominant axes define a face plane. Their order/sign versus D3D's face UV does not matter
// for this symmetric separable kernel. Lift each offset back into a direction (rather than clamping UVs)
// so that hardware resolves any taps that land on another face. At the edge fade to the hardware's own
// seamless result: the two face planes need not approximate the same cubic footprint there.
float4 Envmap_SampleSmooth( TextureCube cube, SamplerState smp, float3 direction, uint size, float lod, float cubic )
{
	[branch] if ( cubic <= 0.0 || size < 4u || size > 128u )
		return cube.SampleLevel( smp, direction, lod );

	float3 a = abs( direction );
	float3 faceU, faceV;
	if ( a.x >= max( a.y, a.z ) )
	{
		faceU = float3( 0, 1, 0 );
		faceV = float3( 0, 0, 1 );
	}
	else if ( a.y >= a.z )
	{
		faceU = float3( 1, 0, 0 );
		faceV = float3( 0, 0, 1 );
	}
	else
	{
		faceU = float3( 1, 0, 0 );
		faceV = float3( 0, 1, 0 );
	}
	float3 face = direction / max( max( a.x, max( a.y, a.z ) ), 1e-8 );
	float2 plane = float2( dot( face, faceU ), dot( face, faceV ) );
	cubic *= smoothstep( 0.0, 1.0, ( 1.0 - max( abs( plane.x ), abs( plane.y ) ) ) * ( 0.5 * size ) );
	[branch] if ( cubic <= 0.0 )
		return cube.SampleLevel( smp, direction, lod );

	float2 subTexel = frac( ( plane * 0.5 + 0.5 ) * size - 0.5 );
	float2 offsetU, offsetV;
	float u, v;
	Envmap_CubicAxis( subTexel.x, offsetU, u );
	Envmap_CubicAxis( subTexel.y, offsetV, v );
	float3 stepU = faceU * ( 2.0 / size );
	float3 stepV = faceV * ( 2.0 / size );
	float3 row0 = face + offsetV.x * stepV;
	float3 row1 = face + offsetV.y * stepV;
	float4 c00 = cube.SampleLevel( smp, row0 + offsetU.x * stepU, 0 );
	float4 c10 = cube.SampleLevel( smp, row0 + offsetU.y * stepU, 0 );
	float4 c01 = cube.SampleLevel( smp, row1 + offsetU.x * stepU, 0 );
	float4 c11 = cube.SampleLevel( smp, row1 + offsetU.y * stepU, 0 );
	float4 filtered = lerp( lerp( c00, c10, u ), lerp( c01, c11, u ), v );
	[branch] if ( cubic < 1.0 )
		return lerp( cube.SampleLevel( smp, direction, lod ), filtered, cubic );
	return filtered;
}

// The legacy reflection paths have no roughness. Use their implicit footprint to gate the same mip-0
// reconstruction and preserve their original gradient-driven sample when minified.
float4 Envmap_SampleLegacy( TextureCube cube, SamplerState smp, float3 direction )
{
	float lod = cube.CalculateLevelOfDetailUnclamped( smp, direction );
	float3 dx = ddx( direction ), dy = ddy( direction );
	[branch] if ( lod >= 1.0 )
		return cube.SampleGrad( smp, direction, dx, dy );
	uint width, height, mips;
	cube.GetDimensions( 0, width, height, mips );
	[branch] if ( width < 4u || width > 128u )
		return cube.SampleGrad( smp, direction, dx, dy );
	return Envmap_SampleSmooth( cube, smp, direction, width, max( lod, 0.0 ), 1.0 - smoothstep( 0.35, 1.0, lod ) );
}

#endif
