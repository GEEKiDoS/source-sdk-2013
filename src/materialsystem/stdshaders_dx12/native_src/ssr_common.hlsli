// Screen-space reflections, shared by the ssr_*_cs51 compute passes (README "Compute shaders": constants in b0 space1,
// resources in space 0, 8x8 groups, static samplers s0 point / s1 linear clamp). Scratch textures (view depth, trace,
// ray, resolve, history, color mips) are viewport-local at origin 0; the native G-buffer, scene depth and motion vectors
// are read at absolute scene pixel = local + gSSRViewport.xy. Single-sample scenes only.
//
// Stochastic SSR: each trace texel traces one GGX visible-normal sample of its receiver's lobe; the resolve reuses the
// rays of neighbouring texels weighted by the receiver's own lobe over the tracing texel's pdf; the composite
// accumulates the resolve temporally and writes the scene correction.
//
// Register slots of the G-buffer inputs shared by trace, resolve and composite: normal t2, specular t3, stencil t4.
// The including pass defines SSR_RESPONSE_REGISTER / SSR_SCENE_DEPTH_REGISTER and, when it needs E, D and S
// (SSR_HAS_ENVIRONMENT 1), SSR_ENVIRONMENT_REGISTER / SSR_SPECULAR_REGISTER.
#ifndef SSR_COMMON_HLSLI
#define SSR_COMMON_HLSLI
#include "pbr_oct.hlsli"

// Byte-identical to DX12SSRConstants (screenspace_ssr_dx12.cpp), 368 bytes.
cbuffer DX12SSR : register( b0, space1 )
{
	row_major float4x4 gSSRWorldToClip;     // clip = mul( gSSRWorldToClip, float4( P, 1 ) ), native raster epilogue (half pixel + jitter) applied
	row_major float4x4 gSSRClipToWorld;     // its inverse
	row_major float4x4 gSSRPrevWorldToClip; // the previous SSR frame's gSSRWorldToClip (valid when gSSRTemporal.x != 0)
	float4 gSSREye;                         // xyz eye, w 0
	float4 gSSRForward;                     // xyz unit camera forward, w 0
	float4 gSSRViewport;                    // scene offset x, y, local width, height
	float4 gSSRSizes;                       // source (scene scratch) width, height, trace width, height
	float4 gSSRTrace;                       // maxDistance, thickness, resolve radius (trace texels at roughness 1), startDistance
	uint4 gSSRControl;                      // steps, refinements, colorMipCount, frame index
	uint4 gSSRMip;                          // sourceMip, destinationMip, sourceWidth, sourceHeight (reduce pass)
	float4 gSSRLimits;                      // depthClear, normalBias, edgeFade (pixels), 0
	float4 gSSRTemporal;                    // history valid (0/1), max history frames (rough lobes), scene width, scene height
	uint4 gSSRResolve;                      // resolve neighbour taps, motion vectors valid (0/1), 0, 0
	float4 gSSRAmbient;                     // AO term uv per local pixel x, y, value scale, valid (0/1) (trace)
};

static const float SSR_DEPTH_FAR = 1e30; // view-depth value of a clear (sky) pixel
static const float SSR_PI = 3.14159265;
// Neighbour rays are reused only from texels whose traced surface faces within this cosine of the receiver's.
static const float SSR_REUSE_NORMAL = 0.9;

#if defined( SSR_RESPONSE_REGISTER )
Texture2D<float2> gSSRNormal : register( t2, space0 );
Texture2D<float4> gSSRSpec : register( t3, space0 );
Texture2D<uint2> gSSRStencil : register( t4, space0 );
Texture2D<float4> gSSRResponse : register( SSR_RESPONSE_REGISTER, space0 );
Texture2D<float> gSSRSceneDepth : register( SSR_SCENE_DEPTH_REGISTER, space0 );
#if SSR_HAS_ENVIRONMENT
Texture2D<float4> gSSREnvironment : register( SSR_ENVIRONMENT_REGISTER, space0 );
Texture2D<float4> gSSRSpecular : register( SSR_SPECULAR_REGISTER, space0 );
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

// The full-resolution pixel a trace texel traces this frame: one pixel of its group, rotating over the 2x2 group
// every frame so the temporal accumulation covers every pixel. Trace and resolve derive the same pixel.
static const int2 SSR_TRACE_ORDER[4] = { int2( 0, 0 ), int2( 1, 1 ), int2( 1, 0 ), int2( 0, 1 ) };
int2 SSR_TracePixel( int2 traceTexel )
{
	int2 group = SSR_TraceGroup();
	int2 offset = min( SSR_TRACE_ORDER[gSSRControl.w & 3u], group - 1 );
	return min( traceTexel * group + offset, int2( gSSRViewport.zw ) - 1 );
}

uint SSR_Hash( uint x )
{
	x ^= x >> 16;
	x *= 0x7feb352du;
	x ^= x >> 15;
	x *= 0x846ca68bu;
	x ^= x >> 16;
	return x;
}

// Per-texel uniform pair, decorrelated across texels and advanced each frame along the R2 sequence so the
// temporal accumulation (and the upscaler behind it) sees stratified samples.
float2 SSR_Random2( uint2 texel )
{
	uint h = SSR_Hash( texel.x + SSR_Hash( texel.y ) );
	float2 base = float2( h & 0xffffu, h >> 16 ) * ( 1.0 / 65536.0 );
	return frac( base + float( gSSRControl.w & 1023u ) * float2( 0.7548776662, 0.5698402910 ) );
}

float SSR_Luminance( float3 c )
{
	return dot( c, float3( 0.2126, 0.7152, 0.0722 ) );
}

// ---- GGX (alpha = perceptual roughness^2, a2 = alpha^2) ----
float SSR_GGXD( float NdotH, float a2 )
{
	float d = NdotH * NdotH * ( a2 - 1.0 ) + 1.0;
	return a2 / ( SSR_PI * d * d );
}
float SSR_SmithG1( float NdotX, float a2 )
{
	return 2.0 * NdotX / ( NdotX + sqrt( a2 + ( 1.0 - a2 ) * NdotX * NdotX ) );
}
// Smallest alpha the lobe math sees: keeps the near-mirror D finite in the weights and pdfs.
float SSR_Alpha( float roughness )
{
	return max( roughness * roughness, 2e-3 );
}

// Orthonormal basis around n (Duff et al. 2017).
void SSR_Basis( float3 n, out float3 t, out float3 b )
{
	float s = n.z >= 0.0 ? 1.0 : -1.0;
	float a = -1.0 / ( s + n.z );
	float c = n.x * n.y * a;
	t = float3( 1.0 + s * n.x * n.x * a, s * c, -s * n.x );
	b = float3( c, s + n.y * n.y * a, -n.y );
}

// GGX visible-normal sample in the local frame (Heitz 2018, "Sampling the GGX Distribution of Visible Normals").
float3 SSR_SampleVNDF( float3 Ve, float alpha, float2 u )
{
	float3 Vh = normalize( float3( alpha * Ve.x, alpha * Ve.y, Ve.z ) );
	float lensq = Vh.x * Vh.x + Vh.y * Vh.y;
	float3 T1 = lensq > 0.0 ? float3( -Vh.y, Vh.x, 0.0 ) * rsqrt( lensq ) : float3( 1.0, 0.0, 0.0 );
	float3 T2 = cross( Vh, T1 );
	float r = sqrt( u.x );
	float phi = 2.0 * SSR_PI * u.y;
	float t1 = r * cos( phi );
	float t2 = r * sin( phi );
	float s = 0.5 * ( 1.0 + Vh.z );
	t2 = ( 1.0 - s ) * sqrt( 1.0 - t1 * t1 ) + s * t2;
	float3 Nh = t1 * T1 + t2 * T2 + sqrt( max( 0.0, 1.0 - t1 * t1 - t2 * t2 ) ) * Vh;
	return normalize( float3( alpha * Nh.x, alpha * Nh.y, max( 0.0, Nh.z ) ) );
}

// One reflection direction L of the receiver's GGX lobe and its solid-angle pdf G1(V) D(H) / ( 4 N.V ). False when
// the reflected direction falls below the surface.
bool SSR_SampleReflection( float3 N, float3 V, float roughness, float2 u, out float3 L, out float pdf )
{
	float alpha = SSR_Alpha( roughness );
	float a2 = alpha * alpha;
	float3 T, B;
	SSR_Basis( N, T, B );
	float3 Ve = normalize( float3( dot( V, T ), dot( V, B ), max( dot( V, N ), 1e-4 ) ) );
	float3 Hl = SSR_SampleVNDF( Ve, alpha, u );
	float3 H = Hl.x * T + Hl.y * B + Hl.z * N;
	L = reflect( -V, H );
	float NdotV = Ve.z;
	pdf = SSR_SmithG1( NdotV, a2 ) * SSR_GGXD( saturate( Hl.z ), a2 ) / ( 4.0 * NdotV );
	return dot( N, L ) > 1e-4 && pdf > 0.0;
}

// The receiver's specular lobe toward L without Fresnel (F and the split-sum scale live in K):
// D * G1(V) * G1(L) / ( 4 N.V ), the BRDF times N.L. Its ratio to SSR_SampleReflection's pdf for the receiver's own
// sample is G1(L).
float SSR_LobeWeight( float3 N, float3 V, float3 L, float roughness )
{
	float NdotL = dot( N, L );
	float NdotV = max( dot( N, V ), 1e-4 );
	float alpha = SSR_Alpha( roughness );
	float a2 = alpha * alpha;
	float NdotH = saturate( dot( N, normalize( V + L ) ) );
	float lobe = SSR_GGXD( NdotH, a2 ) * SSR_SmithG1( NdotV, a2 ) * SSR_SmithG1( max( NdotL, 0.0 ), a2 ) / ( 4.0 * NdotV );
	return NdotL > 0.0 ? lobe : 0.0;
}

#if defined( SSR_RESPONSE_REGISTER )
// The receiver of one full-resolution pixel. valid requires a fresh eligible PBR writer (stencil 0x80, fresh
// response alpha, nonzero K, non-clear depth in front of the camera); every roughness is eligible.
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
	int3 scenePixel = int3( localPixel + int2( gSSRViewport.xy ), 0 );
	float depth = gSSRSceneDepth.Load( scenePixel );
	uint stencil = gSSRStencil.Load( scenePixel ).y;
	float4 response = gSSRResponse.Load( scenePixel );
	[branch] if ( depth != gSSRLimits.x && ( stencil & 0x80u ) != 0 && response.a > 0.0 && any( response.rgb != 0.0 ) )
	{
		r.P = SSR_LocalToWorld( float2( localPixel ) + 0.5, depth );
		r.viewDepth = SSR_ViewDepth( r.P );
		r.N = PBR_UnpackNormal( gSSRNormal.Load( scenePixel ) );
		r.roughness = gSSRSpec.Load( scenePixel ).a;
		r.K = response.rgb;
#if SSR_HAS_ENVIRONMENT
		float4 environment = gSSREnvironment.Load( scenePixel );
		r.E = environment.rgb;
		r.D = environment.a;
		r.S = gSSRSpecular.Load( scenePixel ).rgb;
#endif
		r.valid = r.viewDepth > 0.0;
	}
	return r;
}
#endif

#endif // SSR_COMMON_HLSLI
