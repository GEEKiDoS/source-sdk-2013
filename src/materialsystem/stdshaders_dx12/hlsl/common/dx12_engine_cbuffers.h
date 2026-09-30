#ifndef DX12_ENGINE_CBUFFERS_H
#define DX12_ENGINE_CBUFFERS_H
// Stage must be declared before including any shared engine helpers. The
// two stages intentionally reuse the legacy names cAmbientCube/cLightInfo.
#if defined(DX12_STAGE_VERTEX) == defined(DX12_STAGE_PIXEL)
#error Define exactly one of DX12_STAGE_VERTEX or DX12_STAGE_PIXEL
#endif

#if defined(DX12_STAGE_VERTEX)
struct DX12LightInfo
{
    float4 color;
    float4 dir;
    float4 pos;
    float4 spotParams;
    float4 atten;
};
cbuffer DX12VSEngine : register(b0, space1)
{
    float4 cConstants0; // @legacy none
    float4 cConstants1; // @legacy none
    float4 cEyePosWaterZ; // @legacy none
    float4 cFlexScale; // @legacy none
    column_major float4x4 cModelViewProj; // @legacy none
    column_major float4x4 cViewProj; // @legacy none
    float4 cModelViewProjZ; // @legacy none
    float4 cViewProjZ; // @legacy none
    float4 cFogParams; // @legacy none
    column_major float4x4 cViewModel; // @legacy none
    float4 cAmbientCube[6]; // @legacy none
    DX12LightInfo cLightInfo[4]; // @legacy none
    int4 cLightCount; // @legacy none
    uint4 cLightEnabled; // @legacy none
    float4 cViewportScale; // @legacy none
    float4 cClipPlanes[6]; // @legacy none
    uint4 cClipMask; // @legacy none
};
cbuffer DX12VSBones : register(b1, space1)
{
    column_major float4x3 cModel[53]; // @legacy none
};
#endif

#if defined(DX12_STAGE_PIXEL)
// Legacy common_vertexlitgeneric_dx9.h type (moved here: the engine block declares the legacy array).
struct PixelShaderLightInfo
{
    float4 color;
    float4 pos;
};
// cPixelFogParams / cAmbientCube / cLightInfo hold what SetPixelShaderFogParams(reg),
// SetPixelShaderStateAmbientLightCube(reg) and CommitPixelShaderLighting(reg) write at the material-chosen
// registers; native sources alias their legacy declarations at those registers to these members.
cbuffer DX12PSEngine : register(b0, space1)
{
    float4 cPixelFogParams; // @legacy none
    float4 cLinearFogColor; // @legacy none
    float4 cLightScale; // @legacy none
    float3 cAmbientCube[6]; // @legacy none
    PixelShaderLightInfo cLightInfo[3]; // @legacy none
    float4 cAlphaTest; // @legacy none
    float4 cRasterFogColor; // @legacy none
    float4 cRasterFogParams; // @legacy none
};
#endif
#endif
