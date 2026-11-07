//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_LIGHTMAPPEDGENERIC_FXC_H
#define L4D2_LIGHTMAPPEDGENERIC_FXC_H

#include "common_ps_fxc.h"

static const float3 g_L4D2BumpBasis[3] =
{
	float3( 0.816496611, 0, 0.577350259 ),
	float3( -0.408248335, 0.707106769, 0.577350259 ),
	float3( -0.408248216, -0.707106829, 0.577350259 )
};

float4 L4D2WorldTexture( sampler textureSampler, float3 uv, float3 weights )
{
#if SEAMLESS
	return weights.x * tex2D( textureSampler, uv.zy ) +
		weights.y * tex2D( textureSampler, uv.xz ) + weights.z * tex2D( textureSampler, uv.xy );
#else
	return tex2D( textureSampler, uv.xy );
#endif
}

float L4D2WorldFog( float3 eye, float3 world, float4 fogParams )
{
	// ShaderApi030 supplies +start/range. Native supplies its negative.
	return min( saturate( length( eye.xy - world.xy ) * fogParams.w - fogParams.x ), fogParams.z );
}

float3 L4D2WorldOutput( float3 color, float fog )
{
	return lerp( color * cLightScale.x, g_LinearFogColor.rgb, fog * fog );
}

#endif
