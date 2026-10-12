// Screen-space reflections, shared by the ssr_*_cs51 compute passes (README "Compute shaders": constants in b0 space1,
// resources in space 0, 8x8 groups). Scratch textures (view depth, trace, color mips) are viewport-local at origin 0;
// the native G-buffer, scene depth and payload are read at absolute scene pixel = local + gSSRViewport.xy.
//
// Register slots of the G-buffer inputs shared by trace and composite: normal t2, specular t3, stencil t4; the
// including pass defines SSR_RESPONSE_REGISTER / SSR_SCENE_DEPTH_REGISTER (t5/t6 trace, t6/t7 composite) and
// SSR_HAS_ENVIRONMENT (composite: E + D at t5, direct specular S at t8). MSAA 0/1 is the pass's static combo; the
// sample count is runtime.
#ifndef SSR_COMMON_HLSLI
#define SSR_COMMON_HLSLI
#include "pbr_oct.hlsli"

// Byte-identical to DX12SSRConstants (screenspace_ssr_dx12.cpp), 256 bytes.
cbuffer DX12SSR : register( b0, space1 )
{
	row_major float4x4 gSSRWorldToClip;   // clip = mul( gSSRWorldToClip, float4( P, 1 ) ), native raster epilogue applied
	row_major float4x4 gSSRClipToWorld;   // its inverse
	float4 gSSREye;                       // xyz eye, w scene sample count
	float4 gSSRForward;                   // xyz unit camera forward, w 0
	float4 gSSRViewport;                  // scene offset x, y, local width, height
	float4 gSSRSizes;                     // source (scene scratch) width, height, trace width, height
	float4 gSSRTrace;                     // maxDistance, thickness, roughnessLimit, startDistance
	uint4 gSSRControl;                    // steps, refinements, colorMipCount, 0
	uint4 gSSRMip;                        // sourceMip, destinationMip, sourceWidth, sourceHeight (reduce pass)
	float4 gSSRLimits;                    // depthClear, normalBias, edgeFade (pixels), 0
};

static const float SSR_DEPTH_FAR = 1e30; // view-depth value of a clear (sky) pixel
static const float SSR_COHERENT_NORMAL = 0.95;
static const float SSR_COHERENT_ROUGHNESS = 0.1;

#if MSAA
#define SSR_LOAD( tex, p, s ) tex.Load( p, int( s ) )
#define SSR_SAMPLE_COUNT max( uint( gSSREye.w ), 1u )
#else
#define SSR_LOAD( tex, p, s ) tex.Load( int3( p, 0 ) )
#define SSR_SAMPLE_COUNT 1u
#endif

#if defined( SSR_RESPONSE_REGISTER )
#if MSAA
Texture2DMS<float2> gSSRNormal : register( t2, space0 );
Texture2DMS<float4> gSSRSpec : register( t3, space0 );
Texture2DMS<uint2> gSSRStencil : register( t4, space0 );
Texture2DMS<float4> gSSRResponse : register( SSR_RESPONSE_REGISTER, space0 );
Texture2DMS<float> gSSRSceneDepth : register( SSR_SCENE_DEPTH_REGISTER, space0 );
#if SSR_HAS_ENVIRONMENT
Texture2DMS<float4> gSSREnvironment : register( t5, space0 );
Texture2DMS<float4> gSSRSpecular : register( t8, space0 );
#endif
#else
Texture2D<float2> gSSRNormal : register( t2, space0 );
Texture2D<float4> gSSRSpec : register( t3, space0 );
Texture2D<uint2> gSSRStencil : register( t4, space0 );
Texture2D<float4> gSSRResponse : register( SSR_RESPONSE_REGISTER, space0 );
Texture2D<float> gSSRSceneDepth : register( SSR_SCENE_DEPTH_REGISTER, space0 );
#if SSR_HAS_ENVIRONMENT
Texture2D<float4> gSSREnvironment : register( t5, space0 );
Texture2D<float4> gSSRSpecular : register( t8, space0 );
#endif
#endif
#endif

// Continuous viewport-local pixel + native depth -> world position through the native-aligned inverse camera.
float3 SSR_LocalToWorld( float2 localPixel, float depth )
{
	float2 uv = localPixel / gSSRViewport.zw;
	float4 world = mul( gSSRClipToWorld, float4( uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0 ) );
	return world.xyz / world.w;
}

// Positive camera-forward distance of a world position.
float SSR_ViewDepth( float3 P )
{
	return dot( P - gSSREye.xyz, gSSRForward.xyz );
}

// Continuous viewport-local pixel of a clip-space position with w > 0.
float2 SSR_ClipToLocal( float4 clip )
{
	float2 ndc = clip.xy / clip.w;
	return float2( ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5 ) * gSSRViewport.zw;
}

// Fade over the last gSSRLimits.z pixels of the viewport.
float SSR_EdgeFade( float2 localPixel )
{
	float edge = min( min( localPixel.x, localPixel.y ), min( gSSRViewport.z - localPixel.x, gSSRViewport.w - localPixel.y ) );
	return saturate( edge / gSSRLimits.z );
}

// Full-resolution local pixels per trace texel on each axis: 1 at full resolution, 2 at half (ceil( viewport / 2 )).
int2 SSR_TraceGroup()
{
	return int2( ceil( gSSRViewport.zw / gSSRSizes.zw ) );
}

#if defined( SSR_RESPONSE_REGISTER )
// The receiver of one full-resolution pixel. valid requires every scene sample to be a fresh eligible PBR writer
// (stencil 0x80, fresh response alpha, nonzero K, non-clear depth, roughness below the profile cutoff) and the
// samples to be coherent (depth spread <= thickness, normals within SSR_COHERENT_NORMAL of sample 0, roughness within
// SSR_COHERENT_ROUGHNESS). Position, normal and roughness are the closest sample's; K (and E, the direct specular S
// and its light distance D) are averaged.
struct SSRReceiver
{
	bool valid;
	int2 localPixel;
	float viewDepth;
	float3 P;
	float3 N;
	float roughness;
	float3 K;
	float3 E;
	float3 S;
	float D;
};

SSRReceiver SSR_LoadReceiver( int2 localPixel )
{
	SSRReceiver r = (SSRReceiver)0;
	r.localPixel = localPixel;
	int2 scenePixel = localPixel + int2( gSSRViewport.xy );
	const uint sampleCount = SSR_SAMPLE_COUNT;
	bool valid = true;
	float3 firstNormal = float3( 0.0, 0.0, 0.0 );
	float firstRoughness = 0.0;
	float nearest = SSR_DEPTH_FAR, farthest = 0.0;
	[loop] for ( uint s = 0; s < sampleCount; ++s )
	{
		float depth = SSR_LOAD( gSSRSceneDepth, scenePixel, s );
		uint stencil = SSR_LOAD( gSSRStencil, scenePixel, s ).y;
		float4 response = SSR_LOAD( gSSRResponse, scenePixel, s );
		float4 spec = SSR_LOAD( gSSRSpec, scenePixel, s );
		valid = depth != gSSRLimits.x && ( stencil & 0x80u ) != 0 && response.a > 0.0 && any( response.rgb != 0.0 ) && spec.a < gSSRTrace.z;
		if ( !valid ) break;
		float3 N = PBR_UnpackNormal( SSR_LOAD( gSSRNormal, scenePixel, s ) );
		float3 P = SSR_LocalToWorld( float2( localPixel ) + 0.5, depth );
		float viewDepth = SSR_ViewDepth( P );
		valid = viewDepth > 0.0;
		if ( s == 0 )
		{
			firstNormal = N;
			firstRoughness = spec.a;
		}
		else
			valid = valid && dot( N, firstNormal ) >= SSR_COHERENT_NORMAL && abs( spec.a - firstRoughness ) <= SSR_COHERENT_ROUGHNESS;
		if ( !valid ) break;
		if ( viewDepth < nearest )
		{
			nearest = viewDepth;
			r.P = P;
			r.N = N;
			r.roughness = spec.a;
		}
		farthest = max( farthest, viewDepth );
		r.K += response.rgb;
#if SSR_HAS_ENVIRONMENT
		float4 environment = SSR_LOAD( gSSREnvironment, scenePixel, s );
		r.E += environment.rgb;
		r.D += environment.a;
		r.S += SSR_LOAD( gSSRSpecular, scenePixel, s ).rgb;
#endif
	}
	r.valid = valid && farthest - nearest <= gSSRTrace.y;
	r.viewDepth = nearest;
	r.K /= float( sampleCount );
	r.E /= float( sampleCount );
	r.S /= float( sampleCount );
	r.D /= float( sampleCount );
	return r;
}

// The receiver traced for one trace texel: the closest valid receiver of its full-resolution pixel group, ties in
// raster order. Trace and composite derive the same selection from the same inputs.
SSRReceiver SSR_SelectReceiver( int2 traceTexel )
{
	SSRReceiver best = (SSRReceiver)0;
	int2 group = SSR_TraceGroup();
	int2 size = int2( gSSRViewport.zw );
	[loop] for ( int y = 0; y < group.y; ++y )
	{
		[loop] for ( int x = 0; x < group.x; ++x )
		{
			int2 local = traceTexel * group + int2( x, y );
			if ( any( local >= size ) ) continue;
			SSRReceiver r = SSR_LoadReceiver( local );
			if ( r.valid && ( !best.valid || r.viewDepth < best.viewDepth ) ) best = r;
		}
	}
	return best;
}

// Index of the closest non-clear scene sample at an absolute scene pixel (0 for a single-sample scene), or -1 when
// the pixel is clear.
int SSR_ClosestSample( int2 scenePixel )
{
	int closest = -1;
	float nearest = SSR_DEPTH_FAR;
	const uint sampleCount = SSR_SAMPLE_COUNT;
	[loop] for ( uint s = 0; s < sampleCount; ++s )
	{
		float depth = SSR_LOAD( gSSRSceneDepth, scenePixel, s );
		if ( depth == gSSRLimits.x ) continue;
		float viewDepth = SSR_ViewDepth( SSR_LocalToWorld( float2( scenePixel ) - gSSRViewport.xy + 0.5, depth ) );
		if ( viewDepth > 0.0 && viewDepth < nearest )
		{
			nearest = viewDepth;
			closest = int( s );
		}
	}
	return closest;
}
#endif

#endif // SSR_COMMON_HLSLI
