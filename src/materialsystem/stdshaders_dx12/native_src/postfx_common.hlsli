// Native SM5.1 DX12 post-processing constants and shared sampling (Froyok's custom bloom and lens flare).
cbuffer PostConstants : register( b0, space1 )
{
	uint srcWidth, srcHeight, dstWidth, dstHeight;
	float texelX, texelY, p0, p1;
};

Texture2D<float4> gSrc : register( t0, space0 );
Texture2D<float4> gSrc2 : register( t1, space0 );
RWTexture2D<float4> gDst : register( u0, space0 );
SamplerState gLinearClamp : register( s1, space0 );

float2 DstUv( uint2 id )
{
	return ( id + 0.5 ) / float2( dstWidth, dstHeight );
}

// Bilinear clamp-to-black-border on the clamp sampler: within half a texel of an edge the border texel blends in
// linearly, so the clamped edge texel keeps weight saturate( texels to the edge + 0.5 ) on each axis.
float3 SampleBorder( Texture2D<float4> tex, float2 uv, float2 size )
{
	float2 p = uv * size;
	float2 w = saturate( min( p, size - p ) + 0.5 );
	return tex.SampleLevel( gLinearClamp, uv, 0 ).rgb * ( w.x * w.y );
}
