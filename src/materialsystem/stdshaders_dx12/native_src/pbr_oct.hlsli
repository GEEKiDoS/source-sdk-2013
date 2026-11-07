// Octahedral unit-vector encoding of the PBR G-buffer normal (stage-neutral: included by pbr_brdf.hlsli and by
// the compute debug view). PBR_PSOut stores PBR_PackNormal( N ) as float2 in [0,1] on R16G16_UNORM.
#ifndef PBR_OCT_HLSLI
#define PBR_OCT_HLSLI
float2 PBR_OctEncode( float3 n )
{
	float2 p = n.xy / ( abs( n.x ) + abs( n.y ) + abs( n.z ) );
	return n.z >= 0.0 ? p : ( 1.0 - abs( p.yx ) ) * ( p >= 0.0 ? 1.0 : -1.0 );
}
float2 PBR_PackNormal( float3 n )
{
	return saturate( PBR_OctEncode( n ) * 0.5 + 0.5 );
}
float3 PBR_OctDecode( float2 f )
{
	float3 n = float3( f.x, f.y, 1.0 - abs( f.x ) - abs( f.y ) );
	float t = saturate( -n.z );
	n.xy += n.xy >= 0.0 ? -t : t;
	return normalize( n );
}
float3 PBR_UnpackNormal( float2 packed )
{
	return PBR_OctDecode( packed * 2.0 - 1.0 );
}
#endif
