// Physically based shading for the game-owned L4D2 water (logical l4d2_water_ps20b) on enhanced DX12 maps.
// tools/genhighres.py includes this file into the generated native twins l4d2_water_highres{,_earlydepth}_ps51
// right before main, whose body is `return WaterPBR_Main( dx12In );`. It therefore reads the twin's generated
// names: PS_INPUT / DX12ConvertInput / DX12_PS_INPUT, WaterNormal, the legacy samplers, the g_* game registers
// and the inlined flashlight helper. Every legacy combo, sampler binding, register and $parameter keeps its
// meaning; only the shading model changes:
//  - world-space rippled normals (vertex tangent frame), exact dielectric Fresnel of water (IOR 1.333) including
//    total internal reflection when the camera is below the surface;
//  - energy conservation: what the surface reflects is not also transmitted (lerp, not add);
//  - the planar reflection is looked up along the physically reflected ray and integrated over a small glossy
//    lobe, so ripples stretch reflections vertically at grazing angles (reflection streaks);
//  - the sun's and local lights' GGX specular (shadow-mapped), which the reflection target does not carry;
//  - Beer-Lambert volume: exponential absorption and in-scattering toward $fogcolor along the water path.
#ifndef WATER_PBR_HLSLI
#define WATER_PBR_HLSLI
#include "envmap_filter.hlsli"	// cheap water's cubemap (the generator only adds it for retail envmap lookups)

static const float WATER_IOR = 1.333;
static const float WATER_F0 = 0.0204;			// ((1 - 1.333) / (1 + 1.333))^2
// Perceptual roughness of the capillary ripples below the normal map's resolution.
static const float WATER_ROUGHNESS = 0.08;
// Optical depth at the authored fog end (normalized water depth 1): transmittance there is renormalized to
// zero, so $fogend keeps meaning "fully opaque" while the falloff in between is exponential.
static const float WATER_EXTINCTION = 3.0;
// The planar reflection follows the normal map's slope times this many $reflectamount: retail offset the
// reflection by normal.xy * $reflectamount in screen UV, which a reflected ray only matches looking straight
// down at 0.5. Grazing views then stretch reflections along the view as real ripples do.
static const float WATER_SLOPE_PER_REFLECTAMOUNT = 2.0;

// Unpolarized dielectric Fresnel; eta = n(incident side) / n(transmitted side). Total internal reflection is 1.
float WaterPBR_Fresnel( float cosI, float eta )
{
	float sinT2 = eta * eta * saturate( 1.0 - cosI * cosI );
	float result = 1.0;
	[flatten] if ( sinT2 < 1.0 )
	{
		float cosT = sqrt( 1.0 - sinT2 );
		float rs = ( eta * cosI - cosT ) / ( eta * cosI + cosT );
		float rp = ( cosI - eta * cosT ) / ( cosI + eta * cosT );
		result = 0.5 * ( rs * rs + rp * rp );
	}
	return result;
}

// Beer-Lambert transmittance along a water path of normalized length depth (1 at $fogend).
float WaterPBR_Transmittance( float depth )
{
	const float floorT = exp( -WATER_EXTINCTION );
	return saturate( ( exp( -WATER_EXTINCTION * saturate( depth ) ) - floorT ) / ( 1.0 - floorT ) );
}

// GGX alpha^2 of the rippled normal: the sub-texel ripple roughness plus the normal variance across the pixel
// footprint (Kaplanyan/Tokuyoshi specular antialiasing), so distant ripples widen the lobe instead of aliasing.
// Call in uniform control flow.
float WaterPBR_Alpha2( float3 N )
{
	float3 dx = ddx( N ), dy = ddy( N );
	float variance = 0.25 * ( dot( dx, dx ) + dot( dy, dy ) );
	float alpha = WATER_ROUGHNESS * WATER_ROUGHNESS;
	return saturate( alpha * alpha + min( 2.0 * variance, 0.18 ) );
}

// Specular-only gather: a water surface has no diffuse; the volume's light is the authored fog color.
ShadowMapShading WaterPBR_Shading( float3 N, float3 V, float alpha2 )
{
	return ShadowMap_WithGGXSpecular( ShadowMap_ShadeBumped( N, N, N, 0.0 ), N, V, alpha2, WATER_F0 );
}

// The analytic glint of a low sun on rippled water is physically tens of times the irradiance of sunlit ground; the
// post bloom turns that into screen-wide glare and lens ghosts. Soft-cap the surface's direct specular luminance at
// this many sun irradiances (at least 1 scene unit, for local lights with no sun): the glitter still saturates
// to white, but its energy, and so the bloom, stays bounded.
static const float WATER_GLINT_LIMIT = 4.0;
float3 WaterPBR_LimitGlint( float3 specular )
{
	const float3 luma = float3( 0.2126, 0.7152, 0.0722 );
	float limit = WATER_GLINT_LIMIT * max( dot( cSunRadiance.rgb, luma ), 1.0 );
	return specular / ( 1.0 + dot( specular, luma ) / limit );
}

#if REFLECT && !FLASHLIGHT
// The planar reflection target holds the mirrored world as the main camera sees it, registered so that a flat
// mirror reads the pixel's own screen position. A reflected ray R from the surface therefore reads the main
// camera's projection of its mirror image M(R), taken at infinity: exact for the sky and distant shores, and the
// flat-mirror ray M(R) = -V lands on the pixel itself for any distance. clip* are cViewProj's direction rows.
float2 WaterPBR_ReflectUV( float3 R, float3 up, float4 clipX, float4 clipY, float4 clipZ )
{
	float3 mirrored = R - 2.0 * dot( R, up ) * up;
	float4 clip = mirrored.x * clipX + mirrored.y * clipY + mirrored.z * clipZ;
	// Retail's reflection lookup is ( ndc + 1 ) / 2 without the Y flip: the target is stored mirrored.
	return saturate( clip.xy / max( clip.w, 1e-5 ) * 0.5 + 0.5 );
}

// Gaussian slope lobe around the resolved normal: xy in units of the per-axis RMS slope, z weight exp(-r^2/2).
static const float3 WATER_LOBE[9] =
{
	float3( 0.0, 0.0, 1.0 ),
	float3( 0.7, 0.7, 0.61 ), float3( -0.7, 0.7, 0.61 ), float3( 0.7, -0.7, 0.61 ), float3( -0.7, -0.7, 0.61 ),
	float3( 1.6, 0.0, 0.28 ), float3( -1.6, 0.0, 0.28 ), float3( 0.0, 1.6, 0.28 ), float3( 0.0, -1.6, 0.28 )
};

// Glossy planar reflection: the unresolved ripples (alpha2) spread the reflected rays; the projection turns an
// isotropic slope lobe into the vertical streaks of real water, long at grazing angles and round looking down.
float3 WaterPBR_Reflection( float3 N, float3 facing, float3 V, float alpha2, float4 clipX, float4 clipY, float4 clipZ )
{
	float3 forward = facing * dot( V, facing ) - V;	// along the surface, away from the viewer
	float forwardLength = length( forward );
	forward = forwardLength > 1e-4 ? forward / forwardLength :
		normalize( cross( facing, abs( facing.z ) < 0.9 ? float3( 0.0, 0.0, 1.0 ) : float3( 1.0, 0.0, 0.0 ) ) );
	float3 side = cross( facing, forward );
	float sigma = 0.7071 * sqrt( alpha2 );	// per-axis RMS slope of a GGX lobe with alpha = sqrt( alpha2 )
	float3 sum = 0.0;
	float weight = 0.0;
	[unroll] for ( int k = 0; k < 9; ++k )
	{
		float3 n = normalize( N + sigma * ( WATER_LOBE[k].x * forward + WATER_LOBE[k].y * side ) );
		float3 r = reflect( -V, n );
		r -= 2.0 * min( dot( r, facing ), 0.0 ) * facing;	// a ray into the water reflects off the surface again
		sum += WATER_LOBE[k].z * ReflectSampler.tex.SampleLevel( ReflectSampler.smp,
			WaterPBR_ReflectUV( r, facing, clipX, clipY, clipZ ), 0 ).rgb;
		weight += WATER_LOBE[k].z;
	}
	return sum / weight;
}
#endif

float4 WaterPBR_Main( DX12_PS_INPUT dx12In )
{
	PS_INPUT i = DX12ConvertInput( dx12In );
#if FLOWMAP && FLOW_DEBUG == 1
	return DX12FinishPixel( float4( FlowmapSampler.tex.Sample( FlowmapSampler.smp, i.bump * g_FlowParams1.x ).rgb, 1.0f ), dx12In.fog );
#elif FLOWMAP && FLOW_DEBUG == 2
	float debugNoise = FlowNoiseSampler.tex.Sample( FlowNoiseSampler.smp, i.world.xy * float2( 1.0f, -1.0f ) * g_FlowParams2.z ).g;
	return DX12FinishPixel( float4( debugNoise, debugNoise, debugNoise, 1.0f ), dx12In.fog );
#else
	// Geometry: the camera may be on either side of the surface (the $bottommaterial pass is the same plane).
	float3 V = normalize( i.eye );
	float3 up = normalize( dx12In.highresNormal );
	const bool fromBelow = dot( up, V ) < 0.0;
	const float3 facing = fromBelow ? -up : up;	// geometric normal toward the eye
#if !FLASHLIGHT
#if BASETEXTURE && REFLECT && !REFRACT
	HlightReceiver hlr = HighresLightmap_Begin( dx12In.shadowBaseLightmapUV, dx12In.shadowVertexLightmapUV );
	ShadowMapReceiver hls = ShadowMap_BeginReceiver( dx12In.highresPosition, dx12In.highresNormal, dx12In.position.xy );
	ShadowMap_SetBakedSun( hls, hlr.sun, hlr.sunDilated );
	ShadowMap_SetBakedFace( hls, hlr.faceId, hlr.q, hlr.localVisibilityEligible, hlr.bakedDirectEligible, hlr.unbakedLocalRange );
#else
	ShadowMapReceiver hls = ShadowMap_BeginReceiver( dx12In.highresPosition, facing, dx12In.position.xy );
#endif
	// Water is drawn after the opaque pass: the screen-space contact masks at its pixels were traced for the
	// underwater scene behind it, and printed that geometry's contact shadows onto the surface.
	hls.noScreenMasks = 1;
#endif

	float4 normal = WaterNormal( i );
	float3 N = normalize( normal.x * dx12In.highresTangentS + normal.y * dx12In.highresTangentT + normal.z * up );
	if ( fromBelow ) N = -N;
	float alpha2 = WaterPBR_Alpha2( N );
	const float eta = fromBelow ? WATER_IOR : 1.0 / WATER_IOR;
	float F = WaterPBR_Fresnel( saturate( dot( N, V ) ), eta );
	float3 result;

#if CHEAP
	// Cubemap water (no reflection/refraction targets): the envmap stands in for the reflected scene.
	float3 reflection = Envmap_SampleLegacy( EnvmapSampler.tex, EnvmapSampler.smp, reflect( -V, N ) ).rgb * ENV_MAP_SCALE * g_ReflectTint.rgb;
	float3 fogColor = g_WaterFogColor.rgb;
#if HDRENABLED
	fogColor = pow( fogColor, 2.2f );
#endif
#if FRESNEL
	float reflectance = F;
#else
	float reflectance = g_ReflectTint.a;	// $nofresnel: the authored $reflectblendfactor
#endif
	ShadowMapDirect direct = ShadowMap_GatherDirect( hls, WaterPBR_Shading( N, V, alpha2 ) );
	result = lerp( fogColor, reflection, reflectance ) * LINEAR_LIGHT_SCALE + WaterPBR_LimitGlint( direct.specular );
#else
	float4 screenUV = i.reflectRefract / i.projected.w;
	float2 distortion = float2( dot( g_WorldToViewWater0, normal.xyz ), dot( g_WorldToViewWater1, normal.xyz ) ) * normal.a;
	float depth = 1.0f;
#if REFRACT && ABOVEWATER
	float4 unwarped = RefractSampler.tex.Sample( RefractSampler.smp, screenUV.wz * g_InvViewportTransform.xy + g_InvViewportTransform.zw );
	// The FP16 scene copy can contain alpha above one where the background
	// did not write water depth. Retail's normalized target clamps it.
	depth = saturate( unwarped.a );
#endif
	float4 scales = g_ReflectRefractScale;
#if REFRACT && !BASETEXTURE
	scales *= depth;
#endif
	float4 warpedUV = screenUV + distortion.xyyx * scales;
	float tintRamp = 1.0f;
	float edge = 1.0f;
	float3 fogColor = g_WaterFogColor.rgb;
#if LIGHTMAPWATERFOG
	// The volume's in-scattered light follows the lightmap wherever the body is visible.
	fogColor *= LightmapSampler.tex.Sample( LightmapSampler.smp, i.lightmap ).rgb * LIGHT_MAP_SCALE * LINEAR_LIGHT_SCALE;
#endif
	// What the surface transmits toward the eye: without a refraction target, the opaque water body.
	float3 transmitted = fogColor;
#if REFRACT
	float4 refraction = RefractSampler.tex.Sample( RefractSampler.smp, warpedUV.wz * g_InvViewportTransform.xy + g_InvViewportTransform.zw );
#if ABOVEWATER
	// Reject distortion across the shore instead of pulling unfogged objects
	// over the water. The native shader falls back to the unwarped sample.
	refraction = refraction.a >= 0.05f ? refraction : unwarped;
	depth = saturate( refraction.a );
	tintRamp = saturate( depth * 3.5f );
	// Chromatic absorption: $refracttint is the transmittance of the first 1/3.5 of the fog range, as retail's
	// ramp reached it; exponential in between. Then extinction toward the in-scattered fog color.
	float3 absorbed = refraction.rgb * pow( max( g_RefractTint.rgb, 1e-4 ), tintRamp );
	transmitted = lerp( fogColor, absorbed, WaterPBR_Transmittance( depth ) );
	// The surface ends where the water does: fade it over the first 5% of the fog range (sub-unit at the shore).
	edge = saturate( depth * 20.0f );
#else
	transmitted = refraction.rgb;	// from below: the world above, through Snell's window
#endif
#endif

#if !FLASHLIGHT
#if REFLECT
	float3 Nr = normalize( facing * dot( N, facing ) + ( N - facing * dot( N, facing ) ) * ( WATER_SLOPE_PER_REFLECTAMOUNT * g_ReflectRefractScale.x * normal.a ) );
	float3 reflected = WaterPBR_Reflection( Nr, facing, V, WaterPBR_Alpha2( Nr ), dx12In.waterClipX, dx12In.waterClipY, dx12In.waterClipZ ) * g_ReflectTint.rgb;
#else
	// No reflection target: from below, total internal reflection shows the water body; from above nothing
	// is reflected that the material can sample, so the surface keeps its transmitted light.
	float3 reflected = fromBelow ? fogColor : transmitted;
#endif
#if BASETEXTURE && REFLECT && !REFRACT
	// Lightmapped base under a reflective film: $basetexture alpha is the film's coverage.
	float4 base = BaseTextureSampler.tex.Sample( BaseTextureSampler.smp, i.bump );
	float3 dp = saturate( float3( dot( normal.xyz, bumpBasis[0] ), dot( normal.xyz, bumpBasis[1] ), dot( normal.xyz, bumpBasis[2] ) ) );
	dp *= dp;
	float3 lighting = HighresLightmap_Plane( hlr, 1, LightmapSampler.tex.Sample( LightmapSampler.smp, i.lightmap12.xy ) ).rgb * dp.x +
		HighresLightmap_Plane( hlr, 2, LightmapSampler.tex.Sample( LightmapSampler.smp, i.lightmap12.wz ) ).rgb * dp.y +
		HighresLightmap_Plane( hlr, 3, LightmapSampler.tex.Sample( LightmapSampler.smp, i.lightmap3 ) ).rgb * dp.z;
	float sum = dot( dp, float3( 1, 1, 1 ) );
	float3x3 tangentFrame = float3x3( dx12In.highresTangentS, dx12In.highresTangentT, dx12In.highresNormal );
	ShadowMapShading shading = ShadowMap_WithGGXSpecular( ShadowMap_ShadeBumped( mul( bumpBasis[0], tangentFrame ),
		mul( bumpBasis[1], tangentFrame ), mul( bumpBasis[2], tangentFrame ), dp / sum ), N, V, alpha2, WATER_F0 );
	ShadowMapDirect direct = ShadowMap_GatherDirect( hls, shading );
	// Unknown owners and neutral native fullbright pages receive no selected direct.
	float3 lit = base.rgb * ( lighting * ( ( cHlightRoute.x && !hlr.nativeOnly ? 1 : LIGHT_MAP_SCALE ) / sum ) + direct.diffuse * hlr.valid );
	float coverage = base.a;
	result = lit * ( 1.0 - F * coverage ) + ( reflected * F + WaterPBR_LimitGlint( direct.specular ) * hlr.valid ) * coverage;
#else
	ShadowMapDirect direct = ShadowMap_GatherDirect( hls, WaterPBR_Shading( N, V, alpha2 ) );
	result = lerp( transmitted, reflected, F * edge ) + WaterPBR_LimitGlint( direct.specular ) * edge;
#endif
#if REFRACT && !ABOVEWATER
	// Seen from below, everything on the surface is behind the water between it and the eye.
	float waterFog = saturate( ( i.projected.z - g_WaterFogParams.x ) / g_WaterFogParams.y );
	result = lerp( fogColor, result, WaterPBR_Transmittance( waterFog ) );
#endif
#else
	// Additive flashlight pass: the light refracted into the body (scattered by the authored fog color,
	// retail's $flashlighttint scale) plus the GGX glint off the surface.
	float2 screenPos = ( i.projected.xy / i.projected.w ) * 0.5f + 0.5f;
	float3 flashlight = DoFlashlight( g_FlashlightPos, i.world, i.flashlight, N, g_FlashlightAttenuation.xyz, g_FlashlightAttenuation.w,
		FlashlightSampler, FlashlightDepthSampler, RandomRotationSampler, 0, FLASHLIGHTSHADOWS != 0, true, screenPos, false,
		cFlashlightColor, cFlashlightScreenScale, g_ShadowTweaks, false );
	flashlight *= i.flashlight.w > 0.0f ? 1.0f : 0.0f;
	float3 L = normalize( g_FlashlightPos - i.world );
	float lambert = saturate( dot( N, L ) + flFlashlightNoLambertValue );
	float transmission = ( 1.0 - WaterPBR_Fresnel( saturate( dot( N, L ) ), eta ) ) * ( 1.0 - F );
	float3 body = flashlight * lambert * transmission * tintRamp * g_WaterFogColor.rgb * g_FlashlightTint;
	float3 glint = WaterPBR_LimitGlint( flashlight * ShadowMap_GGX( L, N, V, alpha2, WATER_F0 ) );
	result = ( body + glint ) * LINEAR_LIGHT_SCALE;
#endif
#endif
	// SDK SetPixelShaderFogParams stores +start/range; native stores its
	// negative. Both yield the native horizontal radial, squared fog blend.
	float fog = min( g_PixelFogParams.z,
		saturate( length( g_EyePos.xy - i.world.xy ) * g_PixelFogParams.w - g_PixelFogParams.x ) );
#if FLASHLIGHT
	result *= 1.0f - fog * fog;
	return DX12FinishPixel( float4( result, 0.0f ), dx12In.fog );
#else
	return DX12FinishPixel( float4( lerp( result, g_LinearFogColor.rgb, fog * fog ), 1.0f ), dx12In.fog );
#endif
#endif
}

#endif // WATER_PBR_HLSLI
