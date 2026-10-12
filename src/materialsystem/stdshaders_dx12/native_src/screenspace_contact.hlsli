// Screen-space contact shadows: a short deterministic ray from a lit receiver toward its light, tested against the
// private contact depth of the current opaque bracket (screenspace_view.hlsli, space 6). Multiplies only runtime
// direct visibility; it is 1 for every draw whose view block has cScreenControl.x clear (neutral bindings).
#ifndef SCREENSPACE_CONTACT_HLSLI
#define SCREENSPACE_CONTACT_HLSLI
#include "screenspace_view.hlsli"

// Camera-forward distance of the native depth value stored at an absolute scene pixel, through the inverse camera:
// forward . ( clipToWorld * clip ) / w - forward . eye. Reverse or infinite projections are handled by the matrix.
float Contact_PixelViewDepth( float2 scenePixel, float depth )
{
	float2 local = ( scenePixel - cScreenViewport.xy ) / cScreenViewport.zw;
	float4 clip = float4( local.x * 2.0 - 1.0, 1.0 - local.y * 2.0, depth, 1.0 );
	float4 forwardRow = cScreenForwardThickness.x * cScreenClipToWorld[0] + cScreenForwardThickness.y * cScreenClipToWorld[1] + cScreenForwardThickness.z * cScreenClipToWorld[2];
	return dot( forwardRow, clip ) / dot( cScreenClipToWorld[3], clip ) - dot( cScreenEyeLength.xyz, cScreenForwardThickness.xyz );
}

// Closest non-clear positive surface depth at an absolute scene pixel (every sample under MSAA); negative when the
// pixel holds only the clear value (sky / unwritten).
float Contact_SurfaceDepth( int2 scenePixel )
{
	float best = -1.0;
	float2 center = float2( scenePixel ) + 0.5;
	[branch] if ( cScreenControl.y > 1 )
	{
		[loop] for ( uint s = 0; s < cScreenControl.y; ++s )
		{
			float depth = g_ScreenContactDepthMS.Load( scenePixel, int( s ) );
			if ( depth == cScreenDepth.x ) continue;
			float viewDepth = Contact_PixelViewDepth( center, depth );
			if ( viewDepth > 0.0 && ( best < 0.0 || viewDepth < best ) ) best = viewDepth;
		}
	}
	else
	{
		float depth = g_ScreenContactDepth.Load( int3( scenePixel, 0 ) );
		if ( depth != cScreenDepth.x )
		{
			float viewDepth = Contact_PixelViewDepth( center, depth );
			if ( viewDepth > 0.0 ) best = viewDepth;
		}
	}
	return best;
}

// Runtime visibility factor of one light at a receiver: 1 when contacts are disabled, receiverToLight has no length,
// the usable ray is no longer than the first sample distance, or no blocker is found. normalWS offsets the origin by
// cScreenDepth.y (a zero normal applies no offset). Samples t = t0 + ( length - t0 ) * ( ( i + 0.5 ) / N )^2 are
// compared by camera-forward distance; a blocker is accepted when 0 < ray - surface <= thickness, and fades over the
// last 20% of the ray and the last cScreenDepth.w pixels of the viewport. Clear, sky and offscreen points are misses.
float DX12_ContactVisibility( float3 positionWS, float3 normalWS, float3 receiverToLight, float maxDistance )
{
	float visibility = 1.0;
	float lengthSquared = dot( receiverToLight, receiverToLight );
	float rayLength = min( cScreenEyeLength.w, maxDistance );
	float firstDistance = cScreenDepth.z;
	[branch] if ( cScreenControl.x != 0 && lengthSquared > 0.0 && rayLength > firstDistance )
	{
		float3 L = receiverToLight * rsqrt( lengthSquared );
		float3 origin = positionWS + normalWS * cScreenDepth.y;
		float thickness = cScreenForwardThickness.w;
		int2 originPixel = int2( -1, -1 );
		float4 originClip = mul( cScreenWorldToClip, float4( origin, 1.0 ) );
		if ( originClip.w > 0.0 )
		{
			float2 originNdc = originClip.xy / originClip.w;
			originPixel = int2( floor( float2( originNdc.x * 0.5 + 0.5, 0.5 - originNdc.y * 0.5 ) * cScreenViewport.zw ) ) + int2( cScreenViewport.xy );
		}
		uint steps = max( cScreenControl.z, 1u );
		float tHit = -1.0;
		float edgePixels = 0.0;
		[loop] for ( uint i = 0; i < steps; ++i )
		{
			float u = ( float( i ) + 0.5 ) / float( steps );
			float t = firstDistance + ( rayLength - firstDistance ) * u * u;
			float3 p = origin + L * t;
			float4 clip = mul( cScreenWorldToClip, float4( p, 1.0 ) );
			// The projected ray is a straight segment: once it leaves the viewport rectangle or passes w <= 0 every
			// later sample is offscreen too.
			if ( clip.w <= 0.0 ) break;
			float2 ndc = clip.xy / clip.w;
			float2 local = float2( ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5 ) * cScreenViewport.zw;
			if ( any( local < 0.0 ) || any( local >= cScreenViewport.zw ) ) break;
			int2 pixel = int2( floor( local ) ) + int2( cScreenViewport.xy );
			if ( all( pixel == originPixel ) ) continue;
			float surfaceDepth = Contact_SurfaceDepth( pixel );
			if ( surfaceDepth <= 0.0 ) continue;
			float separation = dot( p - cScreenEyeLength.xyz, cScreenForwardThickness.xyz ) - surfaceDepth;
			if ( separation > 0.0 && separation <= thickness )
			{
				tHit = t;
				edgePixels = min( min( local.x, local.y ), min( cScreenViewport.z - local.x, cScreenViewport.w - local.y ) );
				break;
			}
		}
		if ( tHit >= 0.0 )
		{
			float edgeConfidence = saturate( edgePixels / cScreenDepth.w );
			visibility = 1.0 - ( 1.0 - smoothstep( 0.8 * rayLength, rayLength, tHit ) ) * edgeConfidence;
		}
	}
	return visibility;
}

#endif // SCREENSPACE_CONTACT_HLSLI
