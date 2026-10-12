// PBR BRDF library for the pbr_* native shaders (PBR-only; included after dx12_preamble.h).
// Source light units: legacy output is albedo * N.L * C, so every lobe here is pre-multiplied by pi
// (GGX D without 1/pi, Lambert/Burley without 1/pi). Radiance, shadow and attenuation are the caller's.
#ifndef PBR_BRDF_HLSLI
#define PBR_BRDF_HLSLI
#include "common_ps_fxc.h"
#include "pbr_material.h"
#include "pbr_oct.hlsli"

static const float PBR_MIN_ROUGHNESS = 0.045;
// Largest specular lobe value handed to the FP16 scene target: near-mirror materials under a sun
// would otherwise overflow to infinity and poison bloom.
static const float PBR_MAX_SPECULAR = 60000.0;

struct PBRSurface
{
	float3 N;
	float3 V;
	float3 T;            // rotated anisotropy tangent; valid only when anisotropy != 0
	float3 B;
	float3 diffuseColor; // baseColor after the workflow split and the (1 - max3(F0)) energy factor
	float3 F0;
	float roughness;     // clamped perceptual roughness
	float a2;            // isotropic GGX alpha^2, alpha = roughness^2
	float2 alphaTB;      // anisotropic alphas along T and B
	float anisotropy;    // 0 selects the isotropic lobe
	float NdotV;
	float ao;
	float sss;           // subsurface * exp2(-4 thickness)
	float back;          // backlight * exp2(-4 thickness)
	float3 subsurfaceTint;
	float3 backlightTint;
	uint angular;        // stored diffuse basis: 0 Lambert, 1 squared half-Lambert
	uint bumped;         // 1: the stored basis is the three world-space bumped-lightmap planes (world highres faces)
	float3 basis0;
	float3 basis1;
	float3 basis2;
	float3 basisWeights;
};

float PBR_Pow5( float x )
{
	float x2 = x * x;
	return x2 * x2 * x;
}
float PBR_Max3( float3 v )
{
	return max( v.x, max( v.y, v.z ) );
}
float PBR_Luminance( float3 c )
{
	return dot( c, float3( 0.2126, 0.7152, 0.0722 ) );
}

// Direct GGX specular gathered for the SSR occlusion payload: the radiance sum S and the specular-luminance-weighted
// mean light distance D = weightedDistance / weight (the sun counts as PBR_SUN_DISTANCE; D is 0 while S is zero).
static const float PBR_SUN_DISTANCE = 60000.0;
struct PBRSpecularSum
{
	float3 radiance;
	float weightedDistance;
	float weight;
};
PBRSpecularSum PBR_NoSpecularSum()
{
	PBRSpecularSum sum;
	sum.radiance = 0.0;
	sum.weightedDistance = 0.0;
	sum.weight = 0.0;
	return sum;
}
void PBR_AddSpecular( inout PBRSpecularSum sum, float3 specular, float lightDistance )
{
	float weight = PBR_Luminance( specular );
	sum.radiance += specular;
	sum.weightedDistance += weight * lightDistance;
	sum.weight += weight;
}
float PBR_SpecularDistance( PBRSpecularSum sum )
{
	return sum.weight > 0.0 ? sum.weightedDistance / sum.weight : 0.0;
}

// Tangent from position/UV derivatives for meshes without a vertex tangent. Call only under a uniform branch.
float3 PBR_DerivativeTangent( float3 N, float3 P, float2 uv )
{
	float3 dp1 = ddx( P ), dp2 = ddy( P );
	float2 duv1 = ddx( uv ), duv2 = ddy( uv );
	return cross( dp2, N ) * duv1.x + cross( N, dp1 ) * duv2.x;
}

// Metalness or specular workflow from g_Surface.w. T is the (unnormalized) world tangent, or zero.
PBRSurface PBR_MakeSurface( float3 N, float3 V, float3 T, float3 baseColor, float metalness, float roughness, float ao, float3 specularF0, float thickness, uint angular )
{
	PBRSurface s;
	float3 F0 = specularF0;
	float3 diffuse = baseColor;
	if ( g_Surface.w == 0.0 )
	{
		F0 = lerp( float3( 0.04, 0.04, 0.04 ), baseColor, metalness );
		diffuse = baseColor * ( 1.0 - metalness );
	}
	s.N = N;
	s.V = V;
	s.F0 = F0;
	s.diffuseColor = diffuse * ( 1.0 - PBR_Max3( F0 ) );
	s.roughness = clamp( roughness, PBR_MIN_ROUGHNESS, 1.0 );
	float alpha = s.roughness * s.roughness;
	s.a2 = alpha * alpha;
	s.NdotV = max( dot( N, V ), 1e-4 );
	s.ao = ao;
	float falloff = exp2( -4.0 * thickness );
	s.sss = saturate( g_Options.x * falloff );
	s.back = saturate( g_Options.y * falloff );
	s.subsurfaceTint = g_SubsurfaceTint.rgb;
	s.backlightTint = g_BacklightTint.rgb;
	s.angular = angular;
	s.bumped = 0;
	s.basis0 = s.basis1 = s.basis2 = s.basisWeights = float3( 0.0, 0.0, 0.0 );
	s.T = float3( 1.0, 0.0, 0.0 );
	s.B = float3( 0.0, 1.0, 0.0 );
	s.alphaTB = float2( alpha, alpha );
	s.anisotropy = 0.0;
	[branch] if ( g_Options.w != 0.0 )
	{
		float3 t = T - N * dot( N, T );
		float lengthSquared = dot( t, t );
		// |T| < 1e-6 is degenerate: stay isotropic.
		[branch] if ( lengthSquared > 1e-12 )
		{
			t *= rsqrt( lengthSquared );
			float3 b = cross( N, t );
			float cosine = g_BacklightTint.w, sine = g_Misc.x;
			s.T = cosine * t + sine * b;
			s.B = cosine * b - sine * t;
			float aspect = sqrt( 1.0 - 0.9 * abs( g_Options.w ) );
			float2 tb = float2( alpha / aspect, alpha * aspect );
			s.alphaTB = max( g_Options.w < 0.0 ? tb.yx : tb, 0.001 );
			s.anisotropy = g_Options.w;
		}
	}
	return s;
}

// Declares that the baked carriers of this surface are the three bumped-lightmap planes with these world-space
// basis vectors and plane weights (dp^2 / sum, or the ssbump texel): signed baked-direct deltas must use the same
// reconstruction as the stored data, not the shading normal.
void PBR_SetBumpedBasis( inout PBRSurface s, float3 basis0, float3 basis1, float3 basis2, float3 weights )
{
	s.bumped = 1;
	s.basis0 = basis0;
	s.basis1 = basis1;
	s.basis2 = basis2;
	s.basisWeights = weights;
}

// Diffuse angular factor (float3: subsurface tint). front is the surface's own basis (Lambert or squared
// half-Lambert); subsurface softens it toward the squared half-Lambert and tints it, backlight adds the
// back-facing cosine.
float3 PBR_DiffuseAngular( PBRSurface s, float NdotL )
{
	float soft = saturate( NdotL * 0.5 + 0.5 );
	soft *= soft;
	float front = s.angular != 0 ? soft : saturate( NdotL );
	float3 result = front;
	[branch] if ( s.sss != 0.0 || s.back != 0.0 )
	{
		float angular = lerp( front, soft, s.sss );
		float3 value = angular * lerp( float3( 1.0, 1.0, 1.0 ), s.subsurfaceTint, s.sss );
		result = value * ( 1.0 - s.back ) + saturate( -NdotL ) * s.backlightTint * s.back;
	}
	return result;
}
// The stored baked basis: what lightmaps, highres planes, prop baked direct and the ambient carriers contain.
float3 PBR_DiffuseBasis( PBRSurface s, float3 L )
{
	float angular;
	[branch] if ( s.bumped != 0 )
		angular = dot( s.basisWeights, saturate( float3( dot( s.basis0, L ), dot( s.basis1, L ), dot( s.basis2, L ) ) ) );
	else
	{
		float soft = saturate( dot( s.N, L ) * 0.5 + 0.5 );
		angular = s.angular != 0 ? soft * soft : saturate( dot( s.N, L ) );
	}
	return s.diffuseColor * angular;
}
float3 PBR_DiffuseLambert( PBRSurface s, float3 L )
{
	return s.diffuseColor * PBR_DiffuseAngular( s, dot( s.N, L ) );
}
float3 PBR_DiffuseBurley( PBRSurface s, float3 L )
{
	float NdotL = dot( s.N, L );
	float3 angular = PBR_DiffuseAngular( s, NdotL );
	float3 result = 0.0;
	// Zero angular factor (light behind the surface, no backlight): the Fresnel factors multiply nothing.
	[branch] if ( PBR_Max3( angular ) > 0.0 )
	{
		float LdotH = saturate( dot( L, normalize( L + s.V ) ) );
		float fd90 = 0.5 + 2.0 * s.roughness * LdotH * LdotH;
		float fdV = 1.0 + ( fd90 - 1.0 ) * PBR_Pow5( 1.0 - s.NdotV );
		float fdL = 1.0 + ( fd90 - 1.0 ) * PBR_Pow5( 1.0 - saturate( NdotL ) );
		result = s.diffuseColor * ( fdV * fdL ) * angular;
	}
	return result;
}
// What the subsurface/backlight options add beyond the stored basis (zero when both are off).
float3 PBR_DiffuseExtra( PBRSurface s, float3 L )
{
	float3 extra = 0.0;
	[branch] if ( s.sss != 0.0 || s.back != 0.0 )
		extra = PBR_DiffuseLambert( s, L ) - PBR_DiffuseBasis( s, L );
	return extra;
}

// D * Vis * F * N.L, GGX with height-correlated Smith; Burley anisotropic D and Vis when anisotropy != 0.
float3 PBR_Specular( PBRSurface s, float3 L )
{
	float NdotL = dot( s.N, L );
	float3 result = 0.0;
	[branch] if ( NdotL > 0.0 )
	{
		float3 H = normalize( L + s.V );
		float NdotH = saturate( dot( s.N, H ) );
		float VdotH = saturate( dot( s.V, H ) );
		float D, Vis;
		[branch] if ( s.anisotropy != 0.0 )
		{
			float3 h = float3( dot( s.T, H ) / s.alphaTB.x, dot( s.B, H ) / s.alphaTB.y, NdotH );
			float d2 = dot( h, h );
			D = 1.0 / ( s.alphaTB.x * s.alphaTB.y * d2 * d2 );
			float lambdaV = NdotL * length( float3( s.alphaTB.x * dot( s.T, s.V ), s.alphaTB.y * dot( s.B, s.V ), s.NdotV ) );
			float lambdaL = s.NdotV * length( float3( s.alphaTB.x * dot( s.T, L ), s.alphaTB.y * dot( s.B, L ), NdotL ) );
			Vis = 0.5 / ( lambdaV + lambdaL );
		}
		else
		{
			float d = NdotH * NdotH * ( s.a2 - 1.0 ) + 1.0;
			D = s.a2 / ( d * d );
			float lambdaV = NdotL * sqrt( s.NdotV * s.NdotV * ( 1.0 - s.a2 ) + s.a2 );
			float lambdaL = s.NdotV * sqrt( NdotL * NdotL * ( 1.0 - s.a2 ) + s.a2 );
			Vis = 0.5 / ( lambdaV + lambdaL );
		}
		float3 F = s.F0 + ( 1.0 - s.F0 ) * PBR_Pow5( 1.0 - VdotH );
		result = min( D * Vis, PBR_MAX_SPECULAR ) * F * NdotL;
	}
	return result;
}

// One light: diffuse + specular. burley is chosen by the calling light loop (realtime-only lights use the
// Burley lobe, terms that must cancel baked data use Lambert), never by material data. specular receives the
// GGX share of the returned sum (same radiance and shadow) for the SSR occlusion payload.
float3 PBR_DirectSplit( PBRSurface s, float3 L, float3 radiance, float shadow, bool burley, out float3 specular )
{
	float3 diffuse = burley ? PBR_DiffuseBurley( s, L ) : PBR_DiffuseLambert( s, L );
	float3 ggx = PBR_Specular( s, L );
	specular = ggx * radiance * shadow;
	return ( diffuse + ggx ) * radiance * shadow;
}
float3 PBR_Direct( PBRSurface s, float3 L, float3 radiance, float shadow, bool burley )
{
	float3 specular;
	return PBR_DirectSplit( s, L, radiance, shadow, burley, specular );
}

// Karis EnvBRDFApprox (mobile): scale/bias of F0 for the split-sum environment term.
float3 PBR_EnvBRDF( float3 F0, float roughness, float NdotV )
{
	const float4 c0 = float4( -1.0, -0.0275, -0.572, 0.022 );
	const float4 c1 = float4( 1.0, 0.0425, 1.04, -0.04 );
	float4 r = roughness * c0 + c1;
	float a004 = min( r.x * r.x, exp2( -9.28 * NdotV ) ) * r.x + r.y;
	float2 AB = float2( -1.04, 1.04 ) * a004 + r.zw;
	return F0 * AB.x + AB.y;
}

// Split-sum response of the environment term for the effective envmap mask: what a reflected radiance is multiplied
// by before it reaches the scene (tint, mask, material AO, EnvBRDF). Shared by the cube term and the SSR payload.
float3 PBR_EnvironmentResponse( PBRSurface s, float3 effectiveMask )
{
	return g_ReflectionTint.rgb * effectiveMask * s.ao * PBR_EnvBRDF( s.F0, s.roughness, s.NdotV );
}

// Parallax-corrected cubemap reflection (box rows map world to the unit cube; g_ReflectionTint.w enables),
// sampled at roughness * (mips - 1). mask is the envmap mask; response receives PBR_EnvironmentResponse( s, mask ).
float3 PBR_Environment( TextureCube cube, SamplerState smp, PBRSurface s, float3 P, float3 mask, out float3 response )
{
	float3 R = reflect( -s.V, s.N );
	[branch] if ( g_ReflectionTint.w > 0.0 )
	{
		float3x4 box = float3x4( g_Parallax0, g_Parallax1, g_Parallax2 );
		float3 p = mul( box, float4( P, 1.0 ) );
		float3 d = mul( box, float4( R, 0.0 ) );
		[branch] if ( all( p > 0.0 ) && all( p < 1.0 ) )
		{
			d = abs( d ) < 1e-6 ? ( d >= 0.0 ? 1e-6 : -1e-6 ) : d;
			float3 t = ( step( 0.0, d ) - p ) / d;
			float tMin = min( min( t.x, t.y ), t.z );
			R = ( P + tMin * R ) - g_EnvmapOrigin.xyz;
		}
	}
	uint width, height, mips;
	cube.GetDimensions( 0, width, height, mips );
	float3 radiance = cube.SampleLevel( smp, R, s.roughness * ( mips - 1 ) ).rgb;
	response = PBR_EnvironmentResponse( s, mask );
	return radiance * ENV_MAP_SCALE * response;
}

// Fraction of this pixel's own radiance that survives its fog (FinalOutput's BlendPixelFog for pixelFogType, then
// DX12RasterFog's vertex fog when the raster fog state is enabled). The SSR payload stores E and K through it so a
// screen-space replacement is fogged exactly like the cube term it replaces. Fog color is not part of it.
float PBR_FogTransmission( int pixelFogType, float fogFactor, float vertexFog )
{
	float transmission = 1.0;
	if ( pixelFogType == PIXEL_FOG_TYPE_RANGE || pixelFogType == PIXEL_FOG_TYPE_RANGE_RADIAL )
	{
		float f = saturate( fogFactor );
		transmission = 1.0 - f * f;
	}
	else if ( pixelFogType == PIXEL_FOG_TYPE_HEIGHT )
		transmission = 1.0 - saturate( fogFactor );
	if ( cRasterFogColor.w != 0.0 )
		transmission *= saturate( vertexFog );
	return transmission;
}

// Scene colour plus the G-buffer: world-space shading normal (PBR_PackNormal, octahedral [0,1]), F0.rgb + roughness,
// and the SSR payload: environmentScene is the environment term as it reached the scene (E, final scene units),
// reflectionResponse what a sampled scene radiance must be multiplied by to replace it (K); reflectionValid is 1 for
// a fresh eligible writer. directSpecular is the pixel's direct GGX specular in the same final scene units (S) and
// specularDistance its specular-luminance-weighted light distance (D, target 3 alpha), so a screen-space hit closer
// than D along the reflection removes S. Targets 3/4/5 are attached only while the bracket captures SSR; every
// unbound target is discarded by the pipeline. Validity of normal/F0 is stencil bit 0x80, forced on every
// depth-writing draw in the pass (a PBR writer sets it, anything else clears it).
struct PBR_PSOut
{
	float4 color : SV_Target0;
	float2 normal : SV_Target1;
	float4 spec : SV_Target2;
	float4 environment : SV_Target3;
	float4 response : SV_Target4;
	float4 specular : SV_Target5;
};
PBR_PSOut PBR_Output( float4 color, PBRSurface s, float3 environmentScene, float3 reflectionResponse, float reflectionValid,
	float3 directSpecular, float specularDistance )
{
	PBR_PSOut o;
	o.color = color;
	o.normal = PBR_PackNormal( s.N );
	o.spec = float4( s.F0, s.roughness );
	o.environment = float4( environmentScene, specularDistance );
	o.response = float4( reflectionResponse, reflectionValid );
	o.specular = float4( directSpecular, 0.0 );
	return o;
}

#endif
