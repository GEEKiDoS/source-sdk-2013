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
// Reflection: one ray per pixel, clipped to the opaque camera and marched in screen pixels with perspective-correct
// depth, then binary-refined. A hit replaces the cubemap term like ssr_composite_cs51 does: C * K - confidence * E.
// Occlusion: the term multiplies the pixel's lit colour before fog, exactly what gtao_apply_ps51 does to the scene.
#ifndef PBR_FORWARD_SCREENSPACE_HLSLI
#define PBR_FORWARD_SCREENSPACE_HLSLI
#include "screenspace_view.hlsli"
#include "ssr_ray.hlsli"

Texture2D<float4> g_ScreenSSRScene : register( t5, space6 );
Texture2D<float> g_ScreenSSRDepth : register( t6, space6 );
Texture2D<float> g_ScreenAO : register( t7, space6 );

// cScreenControl.z: steps (bits 0-7), refinements (8-15), and these flags.
static const uint PBR_FORWARD_SSR = 0x10000u;
static const uint PBR_FORWARD_AO = 0x20000u;
static const uint PBR_FORWARD_AO_PACKED = 0x40000u; // the term is stored / 1.5 (gtao_main_cs51)
static const uint PBR_FORWARD_SSR_DEBUG = 0x80000u;

bool PBR_ForwardSSREnabled()
{
	return cScreenControl.x == 0u && ( cScreenControl.z & PBR_FORWARD_SSR ) != 0u;
}

float3 PBR_ForwardSSRDebug( float3 color, float3 response, float confidence )
{
	return ( cScreenControl.z & PBR_FORWARD_SSR_DEBUG ) != 0u
		? ( any( response > 0.0 ) ? float3( 1.0 - confidence, confidence, 0.0 ) : 0.0 )
		: color;
}

float SSR_ForwardSeparation( float2 local, float invW )
{
	const int2 texel = clamp( int2( floor( local ) ), 0, int2( cScreenViewport.zw ) - 1 );
	return rcp( invW ) - g_ScreenSSRDepth.Load( int3( texel, 0 ) );
}

// Scene radiance at the reflection hit (final scene units, rgb) and its confidence (a); a = 0 is a miss.
float4 PBR_ForwardSSR( float3 P, float3 N, float3 V, float roughness, float2 svPos )
{
	const uint steps = max( cScreenControl.z & 0xffu, 1u );
	const uint refinements = ( cScreenControl.z >> 8u ) & 0xffu;
	const float maxDistance = cScreenEyeLength.w, thickness = cScreenForwardThickness.w;
	const float3 R = reflect( -V, N );
	const float3 start = P + N * cScreenDepth.y + R * cScreenDepth.z;
	// P and R are in world space, even for a viewmodel. Its raster FOV and compressed device depth are
	// irrelevant here: trace with the opaque projection and compare to opaque linear view depth.
	const float4 clipStart = mul( cScreenWorldToClip, float4( start, 1.0 ) );
	const float4 clipEnd = mul( cScreenWorldToClip, float4( start + R * maxDistance, 1.0 ) );
	float t0, t1;
	if ( !SSR_ClipSegment( clipStart, clipEnd, t0, t1 ) )
		return 0.0;
	const float4 q0 = lerp( clipStart, clipEnd, t0 ), q1 = lerp( clipStart, clipEnd, t1 );
	const float2 viewport = cScreenViewport.zw;
	const float2 pixel0 = SSR_ClipToPixel( q0, viewport ), pixel1 = SSR_ClipToPixel( q1, viewport );
	const float invW0 = rcp( q0.w ), invW1 = rcp( q1.w );
	const float2 span = abs( pixel1 - pixel0 );
	const uint count = min( steps, max( 1u, uint( ceil( max( span.x, span.y ) ) ) ) );
	const float jitter = frac( 52.9829189 * frac( dot( svPos, float2( 0.06711056, 0.00583715 ) ) ) );
	// Keep an actual front-side sample. A clipped entry may be behind the first opaque surface.
	float previousU = 0.0, previousSeparation = SSR_ForwardSeparation( pixel0, invW0 ), hitU = -1.0;
	float2 hitLocal = 0.0;
	[loop] for ( uint step = 1u; step <= count && hitU < 0.0; ++step )
	{
		// Spend the step budget on the visible part of the ray, not on world units behind the near
		// receiver. Include the interval endpoint so subpixel rays still test the complete depth span.
		const float u = step == count ? 1.0 : ( float( step ) - jitter ) / float( count );
		const float2 local = lerp( pixel0, pixel1, u );
		const float separation = SSR_ForwardSeparation( local, lerp( invW0, invW1, u ) );
		[branch] if ( previousSeparation <= 0.0 && separation > 0.0 )
		{
			float lo = previousU, hi = u, hiSeparation = separation;
			[loop] for ( uint r = 0u; r < refinements; ++r )
			{
				const float mid = 0.5 * ( lo + hi );
				const float midSeparation = SSR_ForwardSeparation( lerp( pixel0, pixel1, mid ), lerp( invW0, invW1, mid ) );
				if ( midSeparation > 0.0 )
				{
					hi = mid;
					hiSeparation = midSeparation;
				}
				else
					lo = mid;
			}
			if ( hiSeparation <= thickness )
			{
				hitU = hi;
				hitLocal = lerp( pixel0, pixel1, hi );
			}
		}
		previousU = u;
		previousSeparation = separation;
	}
	[branch] if ( hitU < 0.0 )
		return 0.0;
	// A wider lobe sees a larger footprint around the same mirror hit. t5 starts at scene mip 1;
	// mip 0 of the underlying texture has already been reused for the opaque composite delta.
	const float hitInvW = lerp( invW0, invW1, hitU );
	const float hitT = lerp( t0 * invW0, t1 * invW1, hitU ) / hitInvW;
	const float footprint = roughness * roughness * hitT * maxDistance * hitInvW * viewport.y;
	uint mipWidth, mipHeight, mipCount;
	g_ScreenSSRScene.GetDimensions( 0, mipWidth, mipHeight, mipCount );
	const uint mip = uint( clamp( log2( max( footprint * 0.5, 1.0 ) ), 0.0, float( mipCount - 1u ) ) + 0.5 );
	const uint2 mipSize = max( uint2( mipWidth, mipHeight ) >> mip, 1u );
	const uint2 texel = min( uint2( max( hitLocal, 0.0 ) ) >> ( mip + 1u ), mipSize - 1u );
	const float3 radiance = g_ScreenSSRScene.Load( int3( texel, mip ) ).rgb;
	// Fade at the viewport edge (cScreenDepth.w pixels) and over the last fifth of the ray, as the compute trace does.
	const float edge = min( min( hitLocal.x, hitLocal.y ), min( viewport.x - hitLocal.x, viewport.y - hitLocal.y ) );
	const float confidence = saturate( edge / max( cScreenDepth.w, 1.0 ) ) * ( 1.0 - smoothstep( 0.8, 1.0, hitT ) );
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
