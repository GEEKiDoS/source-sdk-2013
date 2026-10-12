// Backend-owned register space 6: the main-view screen-space camera, the volumetric medium and the screen depth
// planes. Byte-identical twins: DX12ScreenSpaceViewConstants / DX12VolumetricConstants in
// public/shaderapi/ishaderapidx12screenspace.h. Bound on every graphics root; ordinary draws see neutral (zero)
// constants and typed null depth views, so contact reads are disabled unless control.x is set.
#ifndef SCREENSPACE_VIEW_HLSLI
#define SCREENSPACE_VIEW_HLSLI

cbuffer DX12ScreenSpaceView : register( b0, space6 )
{
	row_major float4x4 cScreenWorldToClip;   // clip = mul( cScreenWorldToClip, float4( P, 1 ) ), native raster epilogue applied
	row_major float4x4 cScreenClipToWorld;   // inverse of cScreenWorldToClip
	float4 cScreenEyeLength;                 // eye xyz, contact length
	float4 cScreenForwardThickness;          // unit camera forward xyz, contact thickness
	float4 cScreenViewport;                  // scene-pixel x, y, width, height
	uint4 cScreenControl;                    // contact enabled, scene sample count, contact steps, SSR capture enabled
	float4 cScreenDepth;                     // depth-clear value, normal bias, first ray distance, edge fade (pixels)
};

cbuffer DX12Volumetric : register( b1, space6 )
{
	float4 cVolumeMedium;                    // local/projected: density, strength, HG g, max distance
	float4 cVolumeSunMedium;                 // sun: density, strength, HG g, max distance
	uint4 cVolumeControl;                    // steps per light segment, sun enabled, master enabled, noise index (0 static dither)
	float4 cVolumeOutputScale;               // x LINEAR_LIGHT_SCALE
};

// Private contact depth (single / multisampled) and final scene depth (volume pass only). Unused shapes are typed nulls.
Texture2D<float> g_ScreenContactDepth : register( t0, space6 );
Texture2DMS<float> g_ScreenContactDepthMS : register( t1, space6 );
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
