// Studio skinning, flex and vertex-light interpolators shared by PBR and NekoToon.
// The TANGENT combo name collides with the TANGENT vertex semantic: capture the value, then release the macro.
#if TANGENT
#define PBR_HAS_TANGENT 1
#else
#define PBR_HAS_TANGENT 0
#endif
#undef TANGENT
// Include the shared material block before the tree helper so its register aliases are visible.
#include "common_vs_fxc.h"
#include "pbr_material.h"
#if LEAFPROBE
#define PBR_LEAN 1
#include "pbr_probe.hlsli"
#endif
#if TREESWAY
#define g_flTime g_TreeSwayParams0.x
#define g_vWindDir g_TreeSwayParams0.yz
#define g_flFastSwaySpeedScale g_TreeSwayParams1.x
#define g_flScrumbleFalloffCurve g_TreeSwayParams1.y
#define g_flSwayFalloffCurve g_TreeSwayParams1.z
#define g_flScrumbleSpeed g_TreeSwayParams1.w
#define g_flHeight g_TreeSwayParams2.x
#define g_flStartHeight g_TreeSwayParams2.y
#define g_flRadius g_TreeSwayParams2.z
#define g_flStartRadius g_TreeSwayParams2.w
#define g_flSwaySpeed g_TreeSwayParams3.x
#define g_flSwayIntensity g_TreeSwayParams3.y
#define g_flScrumbleWaveCount g_TreeSwayParams3.z
#define g_flScrumbleIntensity g_TreeSwayParams3.w
#define g_flWindSpeedLerpStart g_TreeSwayParams4.x
#define g_flWindSpeedLerpEnd g_TreeSwayParams4.y
#endif
#include "tree_sway.h"

Texture2D morphSampler_Texture : register( t0 );
SamplerState morphSampler_Sampler : register( s0 );
static DX12Sampler2D morphSampler = { morphSampler_Texture, morphSampler_Sampler };

struct DX12_VS_INPUT
{
#if PBR_HAS_TANGENT
#if COMPRESSED_VERTS
	uint4 vTangentS : TANGENT;
#else
	float4 vTangentS : TANGENT;
#endif
#endif
	float4 vPos : POSITION;
#if COMPRESSED_VERTS
	int2 vBoneWeights : BLENDWEIGHT;
#else
	float4 vBoneWeights : BLENDWEIGHT;
#endif
	float4 vBoneIndices : BLENDINDICES;
#if COMPRESSED_VERTS
	uint4 vNormal : NORMAL;
#else
	float4 vNormal : NORMAL;
#endif
	float4 vTexCoord0 : TEXCOORD0;
#if VERTEXCOLOR
	float4 vColor : COLOR0;
#endif
	float3 vPosFlex : POSITION1;
	float3 vNormalFlex : NORMAL1;
	float vVertexID : POSITION2;
#if !PBR_HAS_TANGENT && STATIC_LIGHT_VERTEX
	float4 vSpecular : COLOR1;      // static prop vertex lighting (gamma, pre-multiplied by 1 / cOverbright)
#endif
};

struct VS_OUTPUT
{
	float4 projPos : SV_Position;
	float fog : FOG;
	float4 baseBumpUV : TEXCOORD0;    // xy base, zw bump
	float2 detailUV : TEXCOORD1;
#if LOCAL_LIGHTS
	float4 lightAtten : TEXCOORD2;    // draw-local lights 0..3
#endif
	// Model draws are small triangles where every interpolant costs (PIX: horde commons). The pixel stage rebuilds the
	// world position from toEye (g_PbrModelEyePos - toEye); the vertex color exists only with $vertexcolor.
	float projPosZ : TEXCOORD3;
	float3 toEye : TEXCOORD4;
	float3 worldNormal : TEXCOORD5;
#if PBR_HAS_TANGENT
	float4 worldTangent : TEXCOORD6;  // xyz S, w binormal sign
#elif LEAFPROBE
	float4 staticLight : TEXCOORD6;
#else
	float3 staticLight : TEXCOORD6; // decoded linear static vertex irradiance, zero without STATIC_LIGHT_VERTEX
#endif
#if VERTEXCOLOR
	float4 vertexColor : TEXCOORD7;
#endif
#if INFECTED
	float4 woundUV : TEXCOORD8;       // wound atlas UVs of the two packets (before the per-packet region)
	float woundCull : TEXCOORD9;      // second-packet ellipsoid distance + cull type; < 1 inside it
#endif
	DX12ClipDistances dx12Clip;       // SV_ClipDistance0/1 (trailing; pixel inputs omit it)
};

VS_OUTPUT main( const DX12_VS_INPUT v )
{
	VS_OUTPUT o = (VS_OUTPUT)0;
	bool bSkinning = SKINNING != 0;

#if COMPRESSED_VERTS
	float4 boneWeights = float4( (float2)v.vBoneWeights, 0, 1 );
	float4 normalIn = (float4)v.vNormal;
#else
	float4 boneWeights = v.vBoneWeights;
	float4 normalIn = v.vNormal;
#endif

	float4 vPosition = v.vPos;
	float3 vNormal;
#if PBR_HAS_TANGENT
	float4 vTangent;
	DecompressVertex_NormalTangent( normalIn, (float4)v.vTangentS, vNormal, vTangent );
#if MORPHING
	ApplyMorph( morphSampler, g_MorphTargetTextureDim, g_MorphSubrect, v.vVertexID, float3( 0, 0, 0 ), vPosition.xyz, vNormal, vTangent.xyz );
#else
	ApplyMorph( v.vPosFlex, v.vNormalFlex, vPosition.xyz, vNormal, vTangent.xyz );
#endif
#else
	DecompressVertex_Normal( normalIn, vNormal );
#if MORPHING
	ApplyMorph( morphSampler, g_MorphTargetTextureDim, g_MorphSubrect, v.vVertexID, float3( 0, 0, 0 ), vPosition.xyz, vNormal );
#else
	ApplyMorph( v.vPosFlex, v.vNormalFlex, vPosition.xyz, vNormal );
#endif
#endif

#if TREESWAY
	vPosition.xyz = ComputeTreeSway( vPosition.xyz, g_flTime );
#endif
	float3 worldNormal, worldPos;
#if PBR_HAS_TANGENT
	float3 worldTangentS, worldTangentT;
	SkinPositionNormalAndTangentSpace( bSkinning, vPosition, vNormal, vTangent, boneWeights, v.vBoneIndices,
		worldPos, worldNormal, worldTangentS, worldTangentT );
	o.worldTangent = float4( normalize( worldTangentS ), vTangent.w );
#else
	SkinPositionAndNormal( bSkinning, vPosition, vNormal, boneWeights, v.vBoneIndices, worldPos, worldNormal );
#endif
	o.worldNormal = normalize( worldNormal );

	float4 projPos = mul( float4( worldPos, 1 ), cViewProj );
	o.dx12Clip = DX12UserClipDistances( projPos );
	o.projPos = DX12Position( projPos );
	projPos.z = dot( float4( worldPos, 1 ), cViewProjZ );
	o.projPosZ = projPos.z;
	o.toEye = cEyePos - worldPos;
	o.fog = CalcFog( worldPos, projPos.xyz, DOWATERFOG );

#if LOCAL_LIGHTS
	o.lightAtten = float4( GetVertexAttenForLight( worldPos, 0, true ), GetVertexAttenForLight( worldPos, 1, true ),
		GetVertexAttenForLight( worldPos, 2, true ), GetVertexAttenForLight( worldPos, 3, true ) );
#endif
	o.baseBumpUV = float4( PBR_TransformUV( v.vTexCoord0.xy, g_BaseTransform ), PBR_TransformUV( v.vTexCoord0.xy, g_BumpTransform ) );
	o.detailUV = PBR_TransformUV( v.vTexCoord0.xy, g_DetailTransform );
#if VERTEXCOLOR
	o.vertexColor = v.vColor;
#endif
#if !PBR_HAS_TANGENT && STATIC_LIGHT_VERTEX
	// DoLighting's static term: the carrier arrives in gamma space, scaled by 1 / cOverbright.
	o.staticLight.rgb = GammaToLinear( v.vSpecular.rgb * cOverbright );
#endif
#if LEAFPROBE
	ProbeSH probe;
	bool resident;
	bool covered = Probe_Decode( worldPos, o.worldNormal, probe, resident );
	if ( resident )
		o.staticLight.rgb = covered ? Probe_Irradiance( probe, o.worldNormal ) : 0.0;
	// Only missing bricks use the engine cube. Interpolate its weight across a brick boundary.
	o.staticLight.a = resident ? 0.0 : 1.0;
#endif
#if INFECTED
	// infected_vs20.fxc WOUNDED: the ellipsoids are in model space (their CPU producer owns the bone transforms), so the
	// unskinned, unmorphed position projects into them. Without a packet the centers sit far away and nothing cuts.
	const float3 delta1 = v.vPos.xyz - g_PbrInfectedWound1[0].xyz;
	const float3 delta2 = v.vPos.xyz - g_PbrInfectedWound2[0].xyz;
	const float3 wound1 = float3( dot( g_PbrInfectedWound1[3].xyz, delta1 ), -dot( g_PbrInfectedWound1[2].xyz, delta1 ), dot( g_PbrInfectedWound1[1].xyz, delta1 ) );
	const float3 wound2 = float3( dot( g_PbrInfectedWound2[3].xyz, delta2 ), -dot( g_PbrInfectedWound2[2].xyz, delta2 ), dot( g_PbrInfectedWound2[1].xyz, delta2 ) );
	const float3 mappedWound2 = lerp( wound1, wound2, g_PbrInfectedWound2[3].w );
	const float2 depth = float2( wound1.z, mappedWound2.z );
	const float2 projectionScale = rsqrt( saturate( 1.0 - depth * depth ) ) / g_PbrInfectedWound1[1].w;
	o.woundUV = float4( wound1.xy * projectionScale.x, mappedWound2.xy * projectionScale.y ) * g_PbrInfectedWoundUVScale + g_PbrInfectedWoundUVBias;
	o.woundCull = length( wound2 ) + g_PbrInfectedWound2[3].w;
#endif
	return o;
}
