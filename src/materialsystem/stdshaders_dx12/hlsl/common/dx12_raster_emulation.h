#ifndef DX12_RASTER_EMULATION_H
#define DX12_RASTER_EMULATION_H
#include "dx12_engine_cbuffers.h"

#if defined(DX12_STAGE_VERTEX)
// vsconv.cpp:998-1029 emits clip distances from the ORIGINAL position and
// then mad position.xy, viewportScale.xy, position.w, position.xy.
float4 DX12Position(float4 clipPosition)
{
    float4 corrected = clipPosition;
    corrected.xy = mad(cViewportScale.xy, clipPosition.ww, clipPosition.xy);
    return corrected;
}

// context.cpp:4665-4685 emits dp4(original position, clip plane), compacting
// enabled planes into SV_ClipDistance0/1. Every disabled distance is positive.
struct DX12ClipDistances
{
    float4 first : SV_ClipDistance0;
    float2 second : SV_ClipDistance1;
};
DX12ClipDistances DX12UserClipDistances(float4 uncorrectedClipPosition)
{
    DX12ClipDistances result;
    [unroll] for (uint i = 0; i < 6; ++i)
    {
        float distance = ((cClipMask.x & (1u << i)) != 0u)
            ? dot(uncorrectedClipPosition, cClipPlanes[i]) : 1.0f;
        if (i < 4) result.first[i] = distance;
        else result.second[i - 4] = distance;
    }
    return result;
}
#endif

#if defined(DX12_STAGE_PIXEL)
// D3D9 compare indices are one-based (NEVER=1 ... ALWAYS=8). psconv.cpp
// :1231-1295 compares the shader's UNQUANTIZED output alpha directly with
// PSCBExtension::fAlphaRef (shader_translate_dx12.h:88-92). D3D9's 8-bit
// reference is computed by the state setter BEFORE it populates cAlphaTest.z;
// rounding the shader alpha here would diverge from the translator.
void DX12AlphaTest(float alpha)
{
    if (cAlphaTest.x == 0.0f) return;
    int comparison = (int)cAlphaTest.y;
    float reference = cAlphaTest.z;
    bool passes = (comparison == 2 && alpha < reference) ||
                  (comparison == 3 && alpha == reference) ||
                  (comparison == 4 && alpha <= reference) ||
                  (comparison == 5 && alpha > reference) ||
                  (comparison == 6 && alpha != reference) ||
                  (comparison == 7 && alpha >= reference) ||
                  comparison == 8;
    if (!passes) discard;
}

// psconv.cpp:1303-1362: implicit raster fog blends (color-fogColor)*
// saturate(vertexFogFactor)+fogColor when fog table mode is D3DFOG_NONE.
float4 DX12RasterFog(float4 color, float vertexFogFactor)
{
    if (cRasterFogColor.w != 0.0f)
        color.rgb = mad(color.rgb - cRasterFogColor.rgb,
                        saturate(vertexFogFactor), cRasterFogColor.rgb);
    return color;
}
// psconv.cpp:1370-1513: for explicit pixel fog D3DFOG_LINEAR=3,
// D3DFOG_EXP=1, D3DFOG_EXP2=2; choose SV_Position.z or .w per W-fog state.
float DX12RasterPixelFogFactor(float fogIndex, uint fogMode)
{
    if (fogMode == 3u)
    {
        if (fogIndex >= cRasterFogParams.y) return 0.0f;
        if (cRasterFogParams.x >= fogIndex) return 1.0f;
        return (cRasterFogParams.y - fogIndex) * cRasterFogParams.z;
    }
    float d = fogIndex * cRasterFogParams.w;
    if (fogMode == 1u) return exp2(d * -1.44269504088896f);
    if (fogMode == 2u) return exp2(d * d * -1.44269504088896f);
    return 1.0f;
}

// Everything the translator appends after a legacy pixel shader's own code, in its order (psconv.cpp:1175-1186):
// alpha test on oC0.a, then (ps < 3.0 only, IsImplicitFogCalculationNeeded) the raster fog blend whose vertex
// fog factor is the paired vertex shader's FOG output, else COLOR1.w, else 0 (psconv.cpp:132-167, 1316-1344).
float4 DX12FinishPixel(float4 color, float vertexFog)
{
    DX12AlphaTest(color.a);
#if !defined(DX12_LEGACY_PS30)
    color = DX12RasterFog(color, vertexFog);
#endif
    return color;
}
#endif
#endif
