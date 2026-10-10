//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef HIGHRES_LIGHTMAPS_DX12_H
#define HIGHRES_LIGHTMAPS_DX12_H
#pragma once
#include "hlight_engine_bridge.h"
#include "shaderapi/ishaderapidx12highres.h"
#include "shaderapi/dx12staticpropvisibility.h"
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
    uint32 dimensionsFlags[4]; // high W/H; low16 face flags + high16 unbaked count; bit0 pose + (raw byteOffset/4)<<1
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
struct StaticPropDirectMetadataDX12
{
    uint32 direct[4] = {~0u,0,0,0}; // radiance byte offset, angular-plane stride, unbaked offset/count
    uint32 styles[4] = {};
    uint32 styleCount = 0;
};
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
    // Attribution of one receiver draw for failure reports (only recorded, never interpreted).
    struct DrawIdentity { const char *vertexShader, *pixelShader; uint64 meshToken; uint32 firstIndex, indexCount; };
    // Role metadata is native-file metadata, never a material-name whitelist. All roles
    // participating in a draw must name the same original native page association.
    bool PrepareDraw(uint32 samplerMask, const float modelToWorld[12], const DrawIdentity &draw,
        CPipelineCacheDX12::BindingInputDX12 &input);
    // Copies immutable map SRVs into the model-neutral space2 view table, independent
    // of native lightmap sampler roles. Resources are retained through the recording fence.
    bool PrepareVisibilityDraw(D3D12_CPU_DESCRIPTOR_HANDLE lightingTable);
    // Binds the space-4 ambient probe pair for a PS that reflects it: the selected mode's grid (made resident on its first
    // consumer draw) when maps/<name>.hprobe validated, otherwise constants with cProbeBricks.w = 0 and nine null views, so
    // the shader takes cAmbientCube. A missing or invalid asset never rejects the map; false only when residency fails.
    bool PrepareProbeDraw(CPipelineCacheDX12::BindingInputDX12 &input);
	bool ResolveStaticPropMesh(const DX12StaticPropReceiver &receiver, uint64 meshToken,
		uint32 directory[4], uint32 &meshIndex);
    bool GetStaticPropDirect(uint32 meshIndex, StaticPropDirectMetadataDX12 &metadata);
    void BeginView(uint64 nativeGeneration, const float styles[64]);
    void EndView();
private:
    struct Impl;
    Impl *m_Impl;
};
}
#endif
