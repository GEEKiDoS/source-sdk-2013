// Fraction of the opaque pixel already replaced by material fog. Callers supply the
// native projected Z (not device Z) and the same range/height/radial constants.
#ifndef DX12_SCENE_FOG_HLSLI
#define DX12_SCENE_FOG_HLSLI

float SceneFog( float clipZ, float3 worldPos, float3 eye, bool vertexFog, int fogType,
    float4 rasterParams, float4 pixelParams )
{
	if ( vertexFog )
		return 1.0 - saturate( max( 1.0 - saturate( rasterParams.w ), ( rasterParams.y - clipZ ) * rasterParams.z ) );
	if ( fogType == PIXEL_FOG_TYPE_HEIGHT )
		return saturate( CalcWaterFogAlpha( pixelParams.y, eye.z, worldPos.z, clipZ, pixelParams.w ) );
	float range = fogType == PIXEL_FOG_TYPE_RANGE ? CalcRangeFog( clipZ, pixelParams.x, pixelParams.z, pixelParams.w ) :
	                                                saturate( CalcRadialFog_NonFixedFunction( worldPos, eye, pixelParams.z, pixelParams.x, pixelParams.w ) );
	return range * range;
}
#endif
