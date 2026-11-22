// Backend-owned register space 6: the main-view screen-space camera, volumetric medium and final scene depth.
// Byte-identical twins: DX12ScreenSpaceViewConstants / DX12VolumetricConstants in
// public/shaderapi/ishaderapidx12screenspace.h. Bound on every graphics root; ordinary draws see neutral zero
// constants. The sun-only contact mask has its own declaration in screenspace_contact.hlsli.
#ifndef SCREENSPACE_VIEW_HLSLI
#define SCREENSPACE_VIEW_HLSLI

cbuffer DX12ScreenSpaceView : register( b0, space6 )
{
	row_major float4x4 cScreenWorldToClip;   // native-aligned scene world-to-clip, also used by forward SSR
	row_major float4x4 cScreenClipToWorld;   // scene inverse, or VM world-to-clip in the forward viewmodel binding
	float4 cScreenEyeLength;                 // eye xyz, forward SSR ray length
	float4 cScreenForwardThickness;          // unit camera forward xyz, forward SSR depth thickness
	float4 cScreenViewport;                  // scene-pixel x, y, width, height
	uint4 cScreenControl;                    // legacy enable, scene sample count or AO dimensions, steps or forward flags, SSR capture
	float4 cScreenDepth;                     // depth clear, normal bias, first ray distance, edge fade pixels
	float4 cScreenContactParams;             // Bend surface thickness fraction, edge threshold, contrast, reserved
	float4 cScreenContactDepth;              // receiver projection and valid raw depth range
	uint4 cScreenContactControl;             // mask scope 0 off 1 world 2 viewmodel, samples, trace pixels, debug
};

cbuffer DX12Volumetric : register( b1, space6 )
{
	float4 cVolumeMedium;                    // local/projected: density, strength, HG g, max distance
	float4 cVolumeSunMedium;                 // sun: density, strength, HG g, max distance
	float4 cVolumeBeamMedium;                // paired map spotlights: density, strength, 0, 0
	uint4 cVolumeControl;                    // steps per light segment, sun enabled, master enabled, rotating noise frame
	float4 cVolumeOutputScale;               // x LINEAR_LIGHT_SCALE
};

// Final scene depth for volumetrics; private contact prepass depths are compute-only.
Texture2D<float> g_ScreenSceneDepth : register( t2, space6 );
Texture2DMS<float> g_ScreenSceneDepthMS : register( t3, space6 );
// Absolute scene pixel -> world position for a native depth value, through the native-aligned camera.
float3 Screen_PixelToWorld( float2 scenePixel, float depth )
{
	float2 local = ( scenePixel - cScreenViewport.xy ) / cScreenViewport.zw;
	float4 clip = float4( local.x * 2.0 - 1.0, 1.0 - local.y * 2.0, depth, 1.0 );
	float4 world = mul( cScreenClipToWorld, clip );
	return world.xyz / world.w;
}

// Positive camera-forward distance of a world position.
float Screen_ViewDepth( float3 positionWS )
{
	return dot( positionWS - cScreenEyeLength.xyz, cScreenForwardThickness.xyz );
}

#endif // SCREENSPACE_VIEW_HLSLI
