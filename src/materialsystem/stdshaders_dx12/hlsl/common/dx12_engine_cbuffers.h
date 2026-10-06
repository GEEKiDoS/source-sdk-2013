#ifndef DX12_ENGINE_CBUFFERS_H
#define DX12_ENGINE_CBUFFERS_H
// Stage must be declared before including any shared engine helpers. The
// two stages intentionally reuse the legacy names cAmbientCube/cLightInfo.
#if defined(DX12_STAGE_VERTEX) == defined(DX12_STAGE_PIXEL)
#error Define exactly one of DX12_STAGE_VERTEX or DX12_STAGE_PIXEL
#endif

// Folded combo values keep the material's original static/dynamic index ABI.
#if defined(DX12_COMBO_FOLD)
#if defined(DX12_STAGE_PIXEL)
cbuffer DX12ComboFoldPS : register(b2, space1)
{
	uint4 cComboFold[16]; // @legacy none
};
#elif defined(DX12_STAGE_VERTEX)
cbuffer DX12ComboFoldVS : register(b3, space1)
{
	uint4 cComboFold[16]; // @legacy none
};
#endif
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

// Lighting ABI 4 preserves the V1 space2 constants for selected direct-light evaluation.
// CPU twins/reflection tables: public/shaderapi/ishaderapidx12lighting.h and
// shaderapidx12/native_engine_cbuffers_dx12.h. Explicit space3 highres draw/styles
// constants and face/tile mirrors live in native_src/highres_lightmaps.hlsli.
#if defined(DX12_SHADOWMAPS)
#define DX12_LIGHTING_SHADER_ABI 4
#define DX12_SHADOW_PCSS_MAX_TEXELS 16
#define DX12_SHADOW_GUARD_TEXELS 18
#define DX12_SHADOW_PCSS_BLOCKER_SAMPLES 16
#define DX12_SHADOW_PCSS_FILTER_SAMPLES 32
#define DX12_SHADOW_CSM_CASCADES 4
#define DX12_SHADOW_SUN_MAP_COUNT 5
#define DX12_SHADOW_MAX_FACES 6
#define DX12_SHADOW_MAX_LOCAL_PAGES 1024
#define DX12_SHADOW_TILE_PIXELS 16
#define DX12_SHADOW_FILTER_PCF 0
#define DX12_SHADOW_FILTER_PCSS 1
#define DX12_SHADOW_VIEW_HAS_SUN 0x1
#define DX12_SHADOW_VIEW_CSM_VALID 0x2
#define DX12_SHADOW_VIEW_STATIC_SUN_VALID 0x4
#define DX12_SHADOW_LIGHT_SUN 0
#define DX12_SHADOW_LIGHT_POINT 1
#define DX12_SHADOW_LIGHT_SPOT 2
cbuffer DX12LightingViewConstantsV1 : register(b0, space2)
{
    uint4 cShadowView0; // mapGeneration, viewGeneration, filterMode, debugMode
    uint4 cShadowView1; // tileCountX, tileCountY, localLightCount, DX12_SHADOW_VIEW_* flags
    float4 cShadowViewport; // viewportX, viewportY, 1/width, 1/height
    float4 cSunRadiance; // rgb styled linear irradiance, w = tan(sun angular radius)
    float4 cSunTravel; // xyz light->receiver, w = CSM far f
    float4 cSunBasisX; // xyz, w = 0.9*f
    float4 cSunBasisY; // xyz, w = 0
    float4 cEyePosition; // xyz, w = CSM near n
    float4 cViewForward; // xyz, w = 0
    float4 cCascadeSplits; // s1..s4
    float4 cCascadeBlend; // w1..w3, 0
    float4 cShadowDepthRecords[DX12_SHADOW_SUN_MAP_COUNT]; // near, far, worldUnitsPerTexel, 0
    row_major float4x4 cSunWorldToClip[DX12_SHADOW_CSM_CASCADES];
    row_major float4x4 cStaticSunWorldToClip;
    uint4 cCascadeRects[DX12_SHADOW_CSM_CASCADES]; // slotX, slotY, slotSize, 0
    uint4 cStaticSunRect; // 0, 0, 4096, 0
    uint4 cSunIdentity; // sun lightId (0xFFFFFFFF none), sunStyle, 0, 0
};
struct RuntimeShadowLightGpu // 592 bytes, StructuredBuffer at t1026 space2
{
    uint lightId;
    uint type;
    uint style;
    uint faceCount;
    float3 origin;
    float attenuationRadius;
    float3 travelDirection;
    float innerConeCos;
    float3 radiance;
    float outerConeCos;
    float constantAttn;
    float linearAttn;
    float quadraticAttn;
    float exponent;
    float fadeStart;
    float fadeEnd;
    float capDist;
    float shadowSourceRadius;
    float shadowNear;
    float shadowFar;
    float planeToTexel;
    float tanRenderedHalfFov;
    row_major float4x4 worldToClip[DX12_SHADOW_MAX_FACES];
    uint4 faces[DX12_SHADOW_MAX_FACES]; // page, slotX, slotY, slotSize
};
#endif
#endif
#endif
