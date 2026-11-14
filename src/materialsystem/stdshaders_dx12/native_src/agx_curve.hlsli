// The display tone curve of the DX12 post stack and its inverse.
#ifndef AGX_CURVE_HLSLI
#define AGX_CURVE_HLSLI

// Minimal AgX (Wrensch). The reference matrices are GLSL column-major mat3 literals, so they are listed here as
// HLSL rows of the transpose and applied as row-vector products; mul( M, c ) would transpose them and tint grey red.
static const float3x3 kAgXInset = float3x3( 0.842479062253094, 0.0423282422610123, 0.0423756549057051, 0.0784335999999992, 0.878468636469772, 0.0784336, 0.0792237451477643, 0.0791661274605434, 0.879142973793104 );
static const float3x3 kAgXOutset = float3x3( 1.19687900512017, -0.0528968517574562, -0.0529716355144438, -0.0980208811401368, 1.15190312990417, -0.0980434501171241, -0.0990297440797205, -0.0989611768448433, 1.15107367264116 );
// Scene EV range of the sigmoid for a peak of one; a brighter peak extends the top by log2( peak ).
static const float kAgXMinEv = -12.47393;
static const float kAgXMaxEv = 4.026069;

// Returns the display-encoded value: the sigmoid is AgX's output in display (sRGB-like) code values, so the caller
// decodes it with the same transfer the swap chain encodes with (hardware sRGB). Decoding it with a plain 2.4 power
// instead and letting the hardware apply the piecewise sRGB encode (linear toe of slope 12.92) crushed the shadows:
// a dark code value of 0.045 left the screen at 2/255 instead of 11/255.
float3 AgXEncoded( float3 c, float peak )
{
	float3 v = log2( max( mul( c, kAgXInset ), 1e-10 ) );
	float maxEv = kAgXMaxEv + log2( max( peak, 1e-4 ) );
	float3 x = saturate( ( v - kAgXMinEv ) / max( maxEv - kAgXMinEv, 1e-4 ) );
	float3 sigmoid = 15.5 * pow( x, 6 ) - 40.14 * pow( x, 5 ) + 31.96 * pow( x, 4 ) - 6.868 * pow( x, 3 ) + 0.4298 * pow( x, 2 ) + 0.1191 * x - 0.00232;
	return saturate( mul( sigmoid, kAgXOutset ) );
}

// The scene colour the composite shows as the display-linear colour d (0..1, relative to paper white), for content
// authored for the display instead of the scene. peak and exposureEv are AgXEncoded's peak and the exposure bias
// the composite applies before it. ceiling limits the sigmoid's input: 16.29 * peak is display white, less keeps
// bright cards out of the bloom at the cost of their brightest codes.
// Exact where the colour is inside AgX's gamut; a brighter or purer colour lands on the nearest the curve reaches.
float3 AgXSceneFromDisplay( float3 d, float peak, float exposureEv, float ceiling )
{
	static const float3x3 kInsetInverse = float3x3( 1.19687900512, -0.052896851757, -0.052971635514, -0.09802088114, 1.151903129904, -0.098043450117, -0.09902974408, -0.098961176845, 1.151073672641 );
	static const float3x3 kOutsetInverse = float3x3( 0.842479062253, 0.042328242261, 0.042375654906, 0.0784336, 0.87846863647, 0.0784336, 0.079223745148, 0.079166127461, 0.879142973793 );
	float3 p = d / peak;
	float3 code = lerp( p * 12.92, 1.055 * pow( p, 1.0 / 2.4 ) - 0.055, step( 0.0031308, p ) );
	// The sigmoid spans -0.00232 .. 0.99858 on 0..1 and has no closed inverse: a fitted start (within 0.041 of the
	// root) and one Newton step leave at most 0.0026, in the darkest codes.
	float3 y = clamp( mul( code, kOutsetInverse ), 0.0, 0.9985 );
	float3 y2 = y * y;
	float3 x = saturate( -0.02243577 + 1.01737732 * pow( y, 0.4 ) - 0.32549091 * y + 0.29350239 * y2 * y2 );
	float3 value = ( ( ( ( ( 15.5 * x - 40.14 ) * x + 31.96 ) * x - 6.868 ) * x + 0.4298 ) * x + 0.1191 ) * x - 0.00232;
	float3 slope = ( ( ( ( 93.0 * x - 200.7 ) * x + 127.84 ) * x - 20.604 ) * x + 0.8596 ) * x + 0.1191;
	x = saturate( x - ( value - y ) / slope );
	float range = kAgXMaxEv + log2( peak ) - kAgXMinEv;
	float3 scene = max( mul( min( exp2( x * range + kAgXMinEv ), ceiling ), kInsetInverse ), 0.0 ) * exp2( -exposureEv );
	// The curve's black is a small positive scene value; below a display code the card stays what it was, so a
	// transparent texel adds nothing.
	return lerp( d, scene, saturate( max( d.r, max( d.g, d.b ) ) * 250.0 ) );
}
#endif
