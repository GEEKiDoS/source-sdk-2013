// Projected lights (flashlight / env_projectedtexture) in the backend-owned register space 5: the one sampling
// implementation shared by the PBR surface shaders (pbr_lighting.hlsli) and the volumetric pass (volumetric_ps51).
// Stage-neutral: no BRDF, contact rays or shadow-attenuation blend live here.
#ifndef PROJECTED_LIGHTING_HLSLI
#define PROJECTED_LIGHTING_HLSLI

struct PBRSpotGpu // 128 bytes, mirrors DX12ProjectedLightDesc
{
	row_major float4x4 worldToTexture; // ClientShadow_t::m_WorldToShadow, shared by cookie and depth
	float4 originFar;                  // xyz origin, w farZ
	float4 colorConst;                 // rgb radiance (pre-scaled by the client), w constant attenuation
	float4 attnShadow;                 // linear, quadratic, shadowAtten, filterTexels (1 / depth width)
	int4 slotsFlags;                   // cookieSlot, depthSlot (-1 unshadowed), flags (1 world, 2 model), DX12_VOLUMETRIC_LIGHT_ENABLED or 0
};
StructuredBuffer<PBRSpotGpu> g_ProjectedLights : register( t0, space5 );
Texture2D<float4> g_ProjectedCookies[8] : register( t1, space5 );
Texture2D<float> g_ProjectedDepth[8] : register( t9, space5 );
SamplerState g_CookieSampler : register( s0, space5 );
SamplerComparisonState g_ProjectedCmpSampler : register( s1, space5 );

// True when P lies inside the light's depth frustum (w > 0 and 0 <= z <= w): the volume pass requires it in addition
// to the cookie window; surface receivers keep their cookie-window-only behavior.
bool DX12_ProjectedFrustum( PBRSpotGpu light, float3 P )
{
	float4 p = float4( P, 1.0 );
	float z = dot( light.worldToTexture[2], p );
	float w = dot( light.worldToTexture[3], p );
	return w > 0.0 && z >= 0.0 && z <= w;
}

// Incident radiance of one projected light at P. Attenuation order is the flashlight shader's:
// saturate( C + Lin / d + Q / d^2 ) * remap( d, farZ -> 0.6 farZ ), times the cookie. rawVisibility is the four-tap
// comparison PCF of a shadowed light (depthSlot >= 0) and 1 otherwise. Returns false (zero outputs) for a point
// outside the cookie window or receiving no radiance.
bool DX12_ProjectedSample( PBRSpotGpu light, float3 P, out float3 L, out float distanceToLight, out float3 incidentRGB, out float rawVisibility )
{
	L = float3( 0.0, 0.0, 0.0 );
	distanceToLight = 0.0;
	incidentRGB = float3( 0.0, 0.0, 0.0 );
	rawVisibility = 1.0;
	float4 projected = mul( light.worldToTexture, float4( P, 1.0 ) );
	bool lit = projected.w > 0.0;
	float2 uv = float2( 0.0, 0.0 );
	if ( lit )
	{
		uv = projected.xy / projected.w;
		lit = all( uv >= 0.0 ) && all( uv <= 1.0 );
	}
	if ( lit )
	{
		float3 toLight = light.originFar.xyz - P;
		distanceToLight = length( toLight );
		L = toLight / max( distanceToLight, 1e-4 );
		float d = max( distanceToLight, 1.0 );
		float attenuation = saturate( light.colorConst.w + light.attnShadow.x / d + light.attnShadow.y / ( d * d ) ) *
			RemapValClamped( d, light.originFar.w, 0.6 * light.originFar.w, 0.0, 1.0 );
		incidentRGB = light.colorConst.rgb * attenuation * g_ProjectedCookies[light.slotsFlags.x].SampleLevel( g_CookieSampler, uv, 0 ).rgb;
		lit = max( incidentRGB.r, max( incidentRGB.g, incidentRGB.b ) ) > 0.0;
		if ( lit && light.slotsFlags.y >= 0 )
		{
			float reference = projected.z / projected.w;
			float2 offset = 0.5 * light.attnShadow.w;
			float pcf = g_ProjectedDepth[light.slotsFlags.y].SampleCmpLevelZero( g_ProjectedCmpSampler, uv + float2( -offset.x, -offset.y ), reference ) +
				g_ProjectedDepth[light.slotsFlags.y].SampleCmpLevelZero( g_ProjectedCmpSampler, uv + float2( offset.x, -offset.y ), reference ) +
				g_ProjectedDepth[light.slotsFlags.y].SampleCmpLevelZero( g_ProjectedCmpSampler, uv + float2( -offset.x, offset.y ), reference ) +
				g_ProjectedDepth[light.slotsFlags.y].SampleCmpLevelZero( g_ProjectedCmpSampler, uv + float2( offset.x, offset.y ), reference );
			rawVisibility = pcf * 0.25;
		}
	}
	return lit;
}

#endif // PROJECTED_LIGHTING_HLSLI
