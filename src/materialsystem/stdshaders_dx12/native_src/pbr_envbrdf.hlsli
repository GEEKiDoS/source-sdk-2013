#ifndef PBR_ENVBRDF_HLSLI
#define PBR_ENVBRDF_HLSLI

// Karis EnvBRDFApprox (mobile): scale/bias of F0 for the split-sum environment term.
float3 PBR_EnvBRDF( float3 F0, float roughness, float NdotV )
{
	const float4 c0 = float4( -1.0, -0.0275, -0.572, 0.022 );
	const float4 c1 = float4( 1.0, 0.0425, 1.04, -0.04 );
	float4 r = roughness * c0 + c1;
	float a004 = min( r.x * r.x, exp2( -9.28 * NdotV ) ) * r.x + r.y;
	float2 AB = float2( -1.04, 1.04 ) * a004 + r.zw;
	return F0 * AB.x + AB.y;
}

#endif
