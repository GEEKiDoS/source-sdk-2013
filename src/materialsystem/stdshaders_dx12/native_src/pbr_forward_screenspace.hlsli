// Screen-space reflections and ambient occlusion for PBR surfaces drawn after the opaque bracket: translucents (car glass,
// windows) are outside its G-buffer, and the first-person viewmodels are drawn after its SSR and GTAO, so neither pass
// ever sees them. The backend binds such draws (screenspace_dx12.cpp PrepareForwardScreenSpaceBinding):
//   t5 the SSR scene copy from mip 1 (the colour before SSR, GTAO and translucents, half resolution) and
//   t6 its positive view depth (opaque only; sky, viewmodel and clear pixels hold 1e30), once that bracket's SSR ran;
//   t7 this frame's GTAO term, viewport-local in the top-left cScreenControl.y (width | height << 16) texels of its
//      target, for viewmodel draws when GTAO saw viewmodel depth.
// The view block keeps the bracket's camera and viewport; cScreenControl.z carries the flags, the march steps and the
// refinements, cScreenEyeLength.w the max distance and cScreenForwardThickness.w the thickness (r_ssr_max_distance /
// r_ssr_thickness). Contacts (control.x) and payload capture (control.w) are off. Every other draw sees neutral
// constants, so both features are off.
//
// Reflection: one mirror ray per pixel, a linear march in clip space (linear along the 3D ray) with interleaved-gradient
// jitter, then binary refinement. A hit replaces the cubemap term like ssr_composite_cs51 does: C * K - confidence * E.
// Occlusion: the term multiplies the pixel's lit colour before fog, exactly what gtao_apply_ps51 does to the scene.
#ifndef PBR_FORWARD_SCREENSPACE_HLSLI
#define PBR_FORWARD_SCREENSPACE_HLSLI
#include "screenspace_view.hlsli"

Texture2D<float4> g_ScreenSSRScene : register( t5, space6 );
Texture2D<float> g_ScreenSSRDepth : register( t6, space6 );
Texture2D<float> g_ScreenAO : register( t7, space6 );

// cScreenControl.z: steps (bits 0-7), refinements (8-15), and these flags.
static const uint PBR_FORWARD_SSR = 0x10000u;
static const uint PBR_FORWARD_AO = 0x20000u;
static const uint PBR_FORWARD_AO_PACKED = 0x40000u; // the term is stored / 1.5 (gtao_main_cs51)

bool PBR_ForwardSSREnabled()
{
	return cScreenControl.x == 0u && ( cScreenControl.z & PBR_FORWARD_SSR ) != 0u;
}

static const float SSR_FORWARD_FAR = 1e30; // view depth of sky, viewmodel and clear pixels in t6
// Below this response (K, what a sampled radiance is multiplied by) the reflection is invisible: no trace.
static const float PBR_FORWARD_MIN_RESPONSE = 0.004;

// Scene radiance at the reflection hit (final scene units, rgb) and its confidence (a); a = 0 is a miss.
float4 PBR_ForwardSSR( float3 P, float3 N, float3 V, float roughness, float2 svPos )
{
	// Rough lobes would need the stochastic resolve; past 0.5 the cubemap stays.
	const float roughFade = 1.0 - smoothstep( 0.3, 0.5, roughness );
	[branch] if ( roughFade <= 0.0 )
		return 0.0;
	const uint steps = max( cScreenControl.z & 0xffu, 1u );
	const uint refinements = ( cScreenControl.z >> 8u ) & 0xffu;
	const float maxDistance = cScreenEyeLength.w, thickness = cScreenForwardThickness.w;
	const float3 R = reflect( -V, N );
	const float3 start = P + N * cScreenDepth.y + R * cScreenDepth.z;
	float3 end = start + R * maxDistance;
	// Keep the ray in front of the camera so its clip w stays positive.
	const float nearDepth = 1.0;
	const float startDepth = Screen_ViewDepth( start ), endDepth = Screen_ViewDepth( end );
	if ( endDepth < nearDepth )
		end = lerp( start, end, saturate( ( startDepth - nearDepth ) / max( startDepth - endDepth, 1e-4 ) ) );
	const float4 clipStart = mul( cScreenWorldToClip, float4( start, 1.0 ) );
	const float4 clipEnd = mul( cScreenWorldToClip, float4( end, 1.0 ) );
	const float jitter = frac( 52.9829189 * frac( dot( svPos, float2( 0.06711056, 0.00583715 ) ) ) );
	const float2 viewport = cScreenViewport.zw;
	// Like ssr_trace_cs51: find a front-to-back crossing of the scene depth, binary-refine it keeping the behind-side
	// end, and accept it only within the thickness (a thin occluder the ray passes behind does not stop the march).
	float previousT = 0.0, previousSeparation = -SSR_FORWARD_FAR, hitT = -1.0;
	float2 hitLocal = 0.0;
	[loop] for ( uint step = 1u; step <= steps && hitT < 0.0; ++step )
	{
		const float t = ( float( step ) - jitter ) / float( steps );
		const float4 clip = lerp( clipStart, clipEnd, t );
		if ( clip.w <= 0.0 )
			break;
		const float2 local = float2( clip.x / clip.w * 0.5 + 0.5, 0.5 - clip.y / clip.w * 0.5 ) * viewport;
		if ( any( local < 0.0 ) || any( local >= viewport ) )
			break;
		const float separation = clip.w - g_ScreenSSRDepth.Load( int3( local, 0 ) );
		[branch] if ( previousSeparation < 0.0 && separation >= 0.0 )
		{
			float lo = previousT, hi = t, hiSeparation = separation;
			float2 hiLocal = local;
			[loop] for ( uint r = 0u; r < refinements; ++r )
			{
				const float mid = 0.5 * ( lo + hi );
				const float4 midClip = lerp( clipStart, clipEnd, mid );
				const float2 midLocal = clamp( float2( midClip.x / midClip.w * 0.5 + 0.5, 0.5 - midClip.y / midClip.w * 0.5 ) * viewport, 0.0, viewport - 1.0 );
				const float midSeparation = midClip.w - g_ScreenSSRDepth.Load( int3( midLocal, 0 ) );
				if ( midSeparation >= 0.0 )
				{
					hi = mid;
					hiSeparation = midSeparation;
					hiLocal = midLocal;
				}
				else
					lo = mid;
			}
			if ( hiSeparation <= thickness )
			{
				hitT = hi;
				hitLocal = hiLocal;
			}
		}
		previousT = t;
		previousSeparation = separation;
	}
	[branch] if ( hitT < 0.0 )
		return 0.0;
	uint mipWidth, mipHeight;
	g_ScreenSSRScene.GetDimensions( mipWidth, mipHeight );
	const float3 radiance = g_ScreenSSRScene.Load( int3( min( uint2( hitLocal * 0.5 ), uint2( mipWidth, mipHeight ) - 1u ), 0 ) ).rgb;
	// Fade at the viewport edge (cScreenDepth.w pixels) and over the last fifth of the ray, as the compute trace does.
	const float edge = min( min( hitLocal.x, hitLocal.y ), min( viewport.x - hitLocal.x, viewport.y - hitLocal.y ) );
	const float confidence = roughFade * saturate( edge / max( cScreenDepth.w, 1.0 ) ) * ( 1.0 - smoothstep( 0.8, 1.0, hitT ) );
	return float4( radiance, confidence );
}

// The GTAO visibility at this pixel (1 when no term is bound).
float PBR_ForwardAO( float2 svPos )
{
	[branch] if ( cScreenControl.x != 0u || ( cScreenControl.z & PBR_FORWARD_AO ) == 0u )
		return 1.0;
	const uint2 aoSize = max( uint2( cScreenControl.y & 0xffffu, cScreenControl.y >> 16u ), 1u );
	const float2 local = svPos - cScreenViewport.xy;
	const uint2 texel = min( uint2( max( local, 0.0 ) * float2( aoSize ) / cScreenViewport.zw ), aoSize - 1u );
	const float ao = g_ScreenAO.Load( int3( texel, 0 ) );
	return saturate( ( cScreenControl.z & PBR_FORWARD_AO_PACKED ) != 0u ? ao * 1.5 : ao );
}
#endif
