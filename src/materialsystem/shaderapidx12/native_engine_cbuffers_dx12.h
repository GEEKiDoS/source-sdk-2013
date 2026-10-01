#ifndef NATIVE_ENGINE_CBUFFERS_DX12_H
#define NATIVE_ENGINE_CBUFFERS_DX12_H

#include "native_cbuffer_dx12.h"
#include <cstddef>
#include <cstdint>

namespace dx12native {
struct DX12LightInfo { float color[4], dir[4], pos[4], spotParams[4], atten[4]; };
struct alignas(16) DX12VSEngine {
    float cConstants0[4];
    float cConstants1[4];
    float cEyePosWaterZ[4];
    float cFlexScale[4];
    float cModelViewProj[16];
    float cViewProj[16];
    float cModelViewProjZ[4];
    float cViewProjZ[4];
    float cFogParams[4];
    float cViewModel[16];
    float cAmbientCube[6][4];
    DX12LightInfo cLightInfo[4];
    int32_t cLightCount[4];
    uint32_t cLightEnabled[4];
    float cViewportScale[4];
    float cClipPlanes[6][4];
    uint32_t cClipMask[4];
};
struct alignas(16) DX12VSBones { float cModel[53][12]; };
struct alignas(16) DX12PSEngine {
    float cPixelFogParams[4];
    float cLinearFogColor[4];
    float cLightScale[4];
    float cAmbientCube[6][4];  // HLSL `float3 cAmbientCube[6]` (legacy type; .w of each row unused)
    float cLightInfo[6][4];    // HLSL `PixelShaderLightInfo cLightInfo[3]` { float4 color, pos; }
    float cAlphaTest[4];
    float cRasterFogColor[4];
    float cRasterFogParams[4];
};

struct EngineCBufferMemberDX12 { const char *name; uint32_t offset, size; };
struct EngineCBufferLayoutDX12 {
    const char *name;
    uint32_t stage, shaderRegister, byteSize, memberCount;
    const EngineCBufferMemberDX12 *members;
};
#define DX12_ENGINE_MEMBER(type, member, expected) \
    ([]() constexpr { static_assert(offsetof(type, member) == expected, #type "::" #member " offset"); \
    return EngineCBufferMemberDX12{ #member, uint32_t(expected), uint32_t(sizeof(((type *)0)->member)) }; }())
// HLSL arrays whose last element is narrower than a row report a shorter Size than the C++ rows.
#define DX12_ENGINE_MEMBER_SIZED(type, member, expected, hlslSize) \
    ([]() constexpr { static_assert(offsetof(type, member) == expected, #type "::" #member " offset"); \
    static_assert(hlslSize <= sizeof(((type *)0)->member), #type "::" #member " size"); \
    return EngineCBufferMemberDX12{ #member, uint32_t(expected), uint32_t(hlslSize) }; }())
static_assert(sizeof(DX12LightInfo) == 80, "DX12LightInfo size");
static_assert(sizeof(DX12VSEngine) == 880, "DX12VSEngine size");
static_assert(sizeof(DX12VSBones) == 2544, "DX12VSBones size");
static_assert(sizeof(DX12PSEngine) == 288, "DX12PSEngine size");
static constexpr EngineCBufferMemberDX12 kDX12VSEngineMembers[] = {
    DX12_ENGINE_MEMBER(DX12VSEngine, cConstants0, 0),
    DX12_ENGINE_MEMBER(DX12VSEngine, cConstants1, 16),
    DX12_ENGINE_MEMBER(DX12VSEngine, cEyePosWaterZ, 32),
    DX12_ENGINE_MEMBER(DX12VSEngine, cFlexScale, 48),
    DX12_ENGINE_MEMBER(DX12VSEngine, cModelViewProj, 64),
    DX12_ENGINE_MEMBER(DX12VSEngine, cViewProj, 128),
    DX12_ENGINE_MEMBER(DX12VSEngine, cModelViewProjZ, 192),
    DX12_ENGINE_MEMBER(DX12VSEngine, cViewProjZ, 208),
    DX12_ENGINE_MEMBER(DX12VSEngine, cFogParams, 224),
    DX12_ENGINE_MEMBER(DX12VSEngine, cViewModel, 240),
    DX12_ENGINE_MEMBER(DX12VSEngine, cAmbientCube, 304),
    DX12_ENGINE_MEMBER(DX12VSEngine, cLightInfo, 400),
    DX12_ENGINE_MEMBER(DX12VSEngine, cLightCount, 720),
    DX12_ENGINE_MEMBER(DX12VSEngine, cLightEnabled, 736),
    DX12_ENGINE_MEMBER(DX12VSEngine, cViewportScale, 752),
    DX12_ENGINE_MEMBER(DX12VSEngine, cClipPlanes, 768),
    DX12_ENGINE_MEMBER(DX12VSEngine, cClipMask, 864),
};
static constexpr EngineCBufferMemberDX12 kDX12VSBonesMembers[] = {
    DX12_ENGINE_MEMBER(DX12VSBones, cModel, 0),
};
static constexpr EngineCBufferMemberDX12 kDX12PSEngineMembers[] = {
    DX12_ENGINE_MEMBER(DX12PSEngine, cPixelFogParams, 0),
    DX12_ENGINE_MEMBER(DX12PSEngine, cLinearFogColor, 16),
    DX12_ENGINE_MEMBER(DX12PSEngine, cLightScale, 32),
    DX12_ENGINE_MEMBER_SIZED(DX12PSEngine, cAmbientCube, 48, 92),
    DX12_ENGINE_MEMBER(DX12PSEngine, cLightInfo, 144),
    DX12_ENGINE_MEMBER(DX12PSEngine, cAlphaTest, 240),
    DX12_ENGINE_MEMBER(DX12PSEngine, cRasterFogColor, 256),
    DX12_ENGINE_MEMBER(DX12PSEngine, cRasterFogParams, 272),
};
struct alignas(16) DX12MotionVS {
    float cPrevViewProj[16];
    float cMotionParams[4];
    float cBaseTexTransform[2][4];
    float cPrevModel[53][12];
};
static_assert(sizeof(DX12MotionVS) == 2656, "DX12MotionVS size");
static constexpr EngineCBufferMemberDX12 kDX12MotionVSMembers[] = {
    DX12_ENGINE_MEMBER(DX12MotionVS, cPrevViewProj, 0),
    DX12_ENGINE_MEMBER(DX12MotionVS, cMotionParams, 64),
    DX12_ENGINE_MEMBER(DX12MotionVS, cBaseTexTransform, 80),
    DX12_ENGINE_MEMBER(DX12MotionVS, cPrevModel, 112),
};
#undef DX12_ENGINE_MEMBER
#undef DX12_ENGINE_MEMBER_SIZED
static constexpr EngineCBufferLayoutDX12 kEngineCBufferLayouts[] = {
    { "DX12VSEngine", kStageVertex, 0, sizeof(DX12VSEngine), sizeof(kDX12VSEngineMembers) / sizeof(*kDX12VSEngineMembers), kDX12VSEngineMembers },
    { "DX12VSBones", kStageVertex, 1, sizeof(DX12VSBones), sizeof(kDX12VSBonesMembers) / sizeof(*kDX12VSBonesMembers), kDX12VSBonesMembers },
    { "DX12PSEngine", kStagePixel, 0, sizeof(DX12PSEngine), sizeof(kDX12PSEngineMembers) / sizeof(*kDX12PSEngineMembers), kDX12PSEngineMembers },
    { "DX12MotionVS", kStageVertex, 7, sizeof(DX12MotionVS), sizeof(kDX12MotionVSMembers) / sizeof(*kDX12MotionVSMembers), kDX12MotionVSMembers },
};
} // namespace dx12native
#endif
