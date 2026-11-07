// Per-pixel rotation of the shadow filter patterns: sun PCF/PCSS and local PCF (shadowmap_lighting.hlsli) and the
// projected-light PCF (projected_lighting.hlsli). Rotating the fixed tap patterns turns their banding/staircase into
// noise; under a temporal upscaler the client animates the pattern so the noise integrates away.
// ShadowMap_BeginReceiver sets the rotation from the view packet's cViewForward.w: 0 = off (fixed patterns),
// 1 + n = interleaved gradient noise (Jimenez) at the pixel offset by 5.588238 * n texels (n = 0 static; the
// volumetric/GTAO temporal convention otherwise). Shaders that never begin a receiver keep the identity rotation.
#ifndef DX12_SHADOW_DITHER_HLSLI
#define DX12_SHADOW_DITHER_HLSLI

static float2 g_ShadowDither = float2( 1.0, 0.0 ); // cos, sin of the pattern rotation
static bool g_ShadowDitherActive = false;

void ShadowDither_Begin( float2 svPositionXY, float control )
{
	g_ShadowDither = float2( 1.0, 0.0 );
	g_ShadowDitherActive = control >= 1.0;
	[branch] if ( g_ShadowDitherActive )
	{
		float2 p = floor( svPositionXY ) + 5.588238 * ( control - 1.0 );
		float noise = frac( 52.9829189 * frac( dot( p, float2( 0.06711056, 0.00583715 ) ) ) );
		sincos( 6.28318530718 * noise, g_ShadowDither.y, g_ShadowDither.x );
	}
}
float2 ShadowDither_Rotate( float2 offset )
{
	return float2( offset.x * g_ShadowDither.x - offset.y * g_ShadowDither.y, offset.x * g_ShadowDither.y + offset.y * g_ShadowDither.x );
}
// The 4-tap PCF pattern (+-0.5 texel) has radius sqrt(0.5); rotated it leaves the +-0.5 square its plane preflight
// checks. 1.5 grows the checked square to +-0.75, which contains the whole rotated pattern.
float ShadowDither_PCFPreflightScale()
{
	return g_ShadowDitherActive ? 1.5 : 1.0;
}

#endif // DX12_SHADOW_DITHER_HLSLI
