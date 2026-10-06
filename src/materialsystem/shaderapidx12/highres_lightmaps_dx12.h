//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef HIGHRES_LIGHTMAPS_DX12_H
#define HIGHRES_LIGHTMAPS_DX12_H
#pragma once
#include "hlight_engine_bridge.h"
#include "shaderapi/ishaderapidx12highres.h"
#include "pipeline_dx12.h"
namespace shaderapidx12
{
class CShaderDeviceDX12;
class CShaderAPIDX12;
// space3: t0 R32_UINT face IDs, t1 faces (160 bytes), t2 tiles (32 bytes),
// t3 RGBA32F native dynamic page, t4..t7 RGBA16F arrays grouped by 2048/4096/8192/16384.
// b0: uint4 route (enabled,pageWidth,pageHeight,0), float4 modelToWorld[3],
// float4 lightstyles[16]; s0 linear-clamp. One immutable table per native page/recording.
struct HlightFaceGpuDX12
{
    uint32 nativeRect[4]; // interior x/y, native extent S/T
    uint32 dimensionsFlags[4]; // high W/H, face flags, model flags
    uint32 styles[4];
    uint32 tiles[16];
    float bakedModelToWorld[12];
};
struct HlightTileGpuDX12
{
    uint32 address[4]; // group,arraySlice,interior x/y
    uint32 size[4]; // high W/H,pageSide,reserved
};
COMPILE_TIME_ASSERT(sizeof(HlightFaceGpuDX12) == 160);
COMPILE_TIME_ASSERT(sizeof(HlightTileGpuDX12) == 32);
class CHighresLightmapsDX12 final : public IHlightNativeSink, public IShaderAPIDX12HighresLightmaps
{
public:
    CHighresLightmapsDX12();
    ~CHighresLightmapsDX12();
    void Initialize(CShaderDeviceDX12 *device, CShaderAPIDX12 *api);
    void ReleaseDevice(); // GPU-idle reset boundary; retain installed bridge and owned map CPU state.
    void Shutdown();
    bool OnNativeDomain(std::shared_ptr<const HlightNativeDomain> domain) override;
    void OnNativeAtlas(std::shared_ptr<const HlightNativeAtlas> atlas) override;
    void OnNativeDynamic(uint64 generation, uint32 face, uint32 mask, uint32 width,
        uint32 height, const float *const rgb[4]) override;
    void OnNativeRetire(uint64 generation) override;
    void OnNativeResourceRelease(uint64 generation) override;
    void OnNativeFailure(const char *reason) override;
    bool RequireMap(const char *mapName, DX12HighresMapStatus &out, char *error, int bytes) override;
    void GetStatus(DX12HighresMapStatus &out, char *error, int bytes) override;
    void BeginClientLevelShutdown() override;
    void EndClientLevelShutdown() override;
    bool BeginClientResourceReadmission() override;
    void EndClientResourceReadmission() override;
    bool EnhancedMap() const;
    bool Rejected() const;
    void ForgetTexture(ShaderAPITextureHandle_t handle, uint64 allocationSerial);
    // Role metadata is native-file metadata, never a material-name whitelist. All roles
    // participating in a draw must name the same original native page association.
    bool PrepareDraw(uint32 samplerMask, const float modelToWorld[12],
        CPipelineCacheDX12::BindingInputDX12 &input);
    void BeginView(uint64 nativeGeneration, const float styles[64]);
    void EndView();
private:
    struct Impl;
    Impl *m_Impl;
};
}
#endif
