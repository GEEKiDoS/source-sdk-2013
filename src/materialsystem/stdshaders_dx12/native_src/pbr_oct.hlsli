// Octahedral unit-vector encoding of the PBR G-buffer normal (stage-neutral: included by pbr_brdf.hlsli and by
// the compute debug view). PBR_PSOut stores PBR_PackNormal( N ) (the oct coordinates as two UNORM16, x high) in the
// x channel of its R32G32_UINT target and asuint( SV_Position.z ) in y.
#ifndef PBR_OCT_HLSLI
#define PBR_OCT_HLSLI
float2 PBR_OctEncode( float3 n )
{
	float2 p = n.xy / ( abs( n.x ) + abs( n.y ) + abs( n.z ) );
	return n.z >= 0.0 ? p : ( 1.0 - abs( p.yx ) ) * ( p >= 0.0 ? 1.0 : -1.0 );
}
uint PBR_PackNormal( float3 n )
{
	uint2 q = uint2( saturate( PBR_OctEncode( n ) * 0.5 + 0.5 ) * 65535.0 + 0.5 );
	return ( q.x << 16 ) | q.y;
}
float3 PBR_OctDecode( float2 f )
{
	float3 n = float3( f.x, f.y, 1.0 - abs( f.x ) - abs( f.y ) );
	float t = saturate( -n.z );
	n.xy += n.xy >= 0.0 ? -t : t;
	return normalize( n );
}
float3 PBR_UnpackNormal( uint packed )
{
	return PBR_OctDecode( float2( packed >> 16, packed & 0xffff ) * ( 2.0 / 65535.0 ) - 1.0 );
}
#endif
