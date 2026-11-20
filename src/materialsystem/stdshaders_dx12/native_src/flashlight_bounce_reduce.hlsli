// Each of 16 groups writes one column of partial sums. Reduce them in the four
// vertices of the light rectangle, rather than serializing the screen on one SM.
// The next frame also performs this reduction once to advance the smoothed history.
struct BounceLight
{
	float4 positionValid;
	float4 colorDebug;
	float3 hitCenter;
};

BounceLight ReduceBounce( Texture2D<float4> sums )
{
	float4 radiance = 0, center = 0;
	float4 normal = 0, legacy = 0;
	[unroll] for ( int i = 0; i < 16; ++i )
	{
		radiance += sums.Load( int3( i, 0, 0 ) );
		center += sums.Load( int3( i, 1, 0 ) );
		normal += sums.Load( int3( i, 2, 0 ) );
		legacy += sums.Load( int3( i, 4, 0 ) );
	}
	float4 originScale = sums.Load( int3( 2, 3, 0 ) );
	BounceLight light;
	float invWeight = rcp( max( radiance.w + legacy.w, 1e-6 ) );
	light.hitCenter = center.xyz * invWeight;
	float3 toLight = originScale.xyz - light.hitCenter;
	float3 pos = light.hitCenter + normal.xyz * ( 16 / max( length( normal.xyz ), 1e-5 ) ) + toLight * ( 12 / max( length( toLight ), 1 ) );
	float4 tintDebug = sums.Load( int3( 7, 3, 0 ) );
	float meanFlux = normal.w / max( radiance.w, 1e-6 );
	float3 rgb = max( ( radiance.rgb * tintDebug.rgb * meanFlux + legacy.rgb ) * invWeight, 0 ) * ( saturate( center.w / 8 ) * originScale.w );
	rgb /= max( 1, max( rgb.r, max( rgb.g, rgb.b ) ) / 0.8 );
	float4 old = sums.Load( int3( 0, 3, 0 ) );
	if ( center.w > 0 && old.w && distance( old.xyz, pos ) < 72 )
	{
		pos = lerp( old.xyz, pos, 0.25 );
		rgb = lerp( sums.Load( int3( 1, 3, 0 ) ).rgb, rgb, 0.25 );
	}
	light.positionValid = float4( pos, center.w > 0 );
	light.colorDebug = float4( rgb, tintDebug.w );
	return light;
}
