// Sun shadow-chart sky clip (caster side). The sun is emitted by the sky faces: a caster that lies on the
// sun side of the sky entry nearest the ground (a 3D-skybox slab above the map's sky ceiling, the exterior
// gable above a window's sky brush) never shadows the receivers below that sky. Before the caster draws
// of every sun chart pass the backend rasterizes the receiver region's sky ENTRY faces with the chart's
// light projection into g_ShadowSkyClip (MAX light-space depth per texel, cleared to 0 so texels without
// sky clip nothing) and binds this block; every depth-only caster path of that pass discards fragments
// whose light depth is smaller than that sky depth by more than the bias.
//
// Backend-owned register space 6 beside screenspace_view.hlsli; byte-identical twin DX12ShadowSkyClipConstants
// in public/shaderapi/ishaderapidx12lighting.h. Ordinary draws, local-light charts and sun passes without a sky
// map see zero constants (w = 0) and a typed-null texture, so the call is a no-op outside clipped sun passes.
#ifndef SHADOW_SKY_CLIP_HLSLI
#define SHADOW_SKY_CLIP_HLSLI

cbuffer DX12ShadowSkyClip : register( b2, space6 )
{
	float4 cShadowSkyClip;                   // chart rect origin x, y (target texels), depth bias (chart depth units), 1 = enabled
};
Texture2D<float> g_ShadowSkyClip : register( t4, space6 ); // sky entry depth, chart-local texels (rect origin at 0,0)

// svPosition: the caster fragment's SV_Position (xy in target texels, z the light depth the chart would store).
void ShadowSkyClip_Discard( float4 svPosition )
{
	if ( cShadowSkyClip.w != 0.0 )
	{
		float sky = g_ShadowSkyClip.Load( int3( int2( svPosition.xy - cShadowSkyClip.xy ), 0 ) );
		clip( svPosition.z - ( sky - cShadowSkyClip.z ) );
	}
}

#endif // SHADOW_SKY_CLIP_HLSLI
