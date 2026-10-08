//========= Copyright Valve Corporation, All rights reserved. ============//
#include "highres_lightmaps_dx12.h"
#include "shaderapi_dx12.h"
#include "shaderdevice_dx12.h"
#include "staticprop_visibility_dx12.h"
#include "shaderapi/ishaderutil.h"
#include "filesystem.h"
#include "zip_utils.h"
#include "tier1/utlbuffer.h"
#include "tier1/strtools.h"
#include "tier2/tier2.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <mutex>
#include <new>

namespace shaderapidx12
{
namespace
{
using Microsoft::WRL::ComPtr;
const uint32 kNoPage = 0xffffffffu;
struct BspFile
{
    IFileSystem *fs;
    FileHandle_t file = FILESYSTEM_INVALID_HANDLE;
    uint64 bytes = 0;
    BspFile(IFileSystem *f, const char *path) : fs(f)
    {
        if (fs) { file = fs->Open(path, "rb", "GAME"); if (file != FILESYSTEM_INVALID_HANDLE) bytes = fs->Size(file); }
    }
    ~BspFile() { if (file != FILESYSTEM_INVALID_HANDLE) fs->Close(file); }
    static bool Read(void *p, uint32 offset, uint32 count, void *out)
    {
        auto &f = *static_cast<BspFile *>(p);
        if (f.file == FILESYSTEM_INVALID_HANDLE || offset > INT_MAX || count > INT_MAX || uint64(offset)+count > f.bytes) return false;
        f.fs->Seek(f.file, offset, FILESYSTEM_SEEK_HEAD);
        return f.fs->Read(out, count, f.file) == int(count);
    }
};
bool MapPath(const char *map, char path[260])
{
    if (!map || !*map || V_strlen(map) >= 240) return false;
    char name[260]; V_strncpy(name, map, sizeof(name)); V_FixSlashes(name, '/');
    if (name[0] == '/' || V_strstr(name, "..") || V_strchr(name, ':')) return false;
    V_StripExtension(name, name, sizeof(name));
    if (!V_strnicmp(name, "maps/", 5)) V_snprintf(path, 260, "%s.bsp", name);
    else V_snprintf(path, 260, "maps/%s.bsp", name);
    return true;
}
struct Texture
{
    ComPtr<ID3D12Resource> resource;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COPY_DEST;
    uint32 width = 0, height = 0, layers = 1;
};
struct NativePage
{
    ShaderAPITextureHandle_t handle = (ShaderAPITextureHandle_t)-1;
    uint64 serial = 0;
    Texture ids, dynamic;
    std::vector<uint32> owners;
    ComPtr<ID3D12DescriptorHeap> descriptors;
    DescriptorRangeDX12 table{};
    uint64 tableFence = 0;
    uint32 dirtyHead = kNoPage;
    uint64 retainedFence = 0, constantsFence = 0, constantsView = 0;
    float constantsModel[12] = {};
    D3D12_GPU_VIRTUAL_ADDRESS constantsAddress = 0;
};
struct DynamicFace
{
    std::vector<float> rgb; // four packed RGB planes, allocated before the first native build
    uint32 nextDirty = kNoPage;
    bool queued = false;
};
struct ViewStyles { uint64 generation, serial; std::array<float,64> values; };
using DrawConstants = dx12native::DX12HighresDrawConstants;
COMPILE_TIME_ASSERT(sizeof(DrawConstants) == 320);
COMPILE_TIME_ASSERT(hlight::kFaceHasBakedLocalDirect == 16); // highres HLSL dimensionsFlags.z mirror
COMPILE_TIME_ASSERT(hlight::kModelBakedPoseKnown == 1); // dimensionsFlags.w reserves bit 0 for pose
void Transition(CCommandRecorderDX12 *list, ID3D12Resource *resource, D3D12_RESOURCE_STATES &state, D3D12_RESOURCE_STATES next)
{
    if (state == next) return;
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = state; barrier.Transition.StateAfter = next;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    list->ResourceBarrier(1, &barrier); state = next;
}
bool Buffer(ID3D12Device *device, uint64 bytes, D3D12_HEAP_TYPE heapType, D3D12_RESOURCE_FLAGS flags,
    D3D12_RESOURCE_STATES state, ComPtr<ID3D12Resource> &out)
{
    D3D12_HEAP_PROPERTIES heap{}; heap.Type = heapType;
    D3D12_RESOURCE_DESC desc{}; desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = std::max<uint64>(bytes, 256); desc.Height = 1; desc.DepthOrArraySize = 1;
    desc.MipLevels = 1; desc.SampleDesc.Count = 1; desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; desc.Flags = flags;
    return SUCCEEDED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&out)));
}
bool MakeTexture(ID3D12Device *device, Texture &out, uint32 width, uint32 height, uint32 layers, DXGI_FORMAT format)
{
    D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{}; desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width; desc.Height = height; desc.DepthOrArraySize = UINT16(layers); desc.MipLevels = 1;
    desc.Format = format; desc.SampleDesc.Count = 1;
    if (!layers || layers > D3D12_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION || !width || !height ||
        FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&out.resource)))) return false;
    out.width = width; out.height = height; out.layers = layers; return true;
}
bool Heap(ID3D12Device *device, ComPtr<ID3D12DescriptorHeap> &out, uint32 count)
{
    D3D12_DESCRIPTOR_HEAP_DESC desc{}; desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; desc.NumDescriptors = count;
    return SUCCEEDED(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&out)));
}
D3D12_CPU_DESCRIPTOR_HANDLE Slot(ID3D12Device *device, ID3D12DescriptorHeap *heap, uint32 index)
{
    auto h = heap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += SIZE_T(index)*device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); return h;
}
void TextureView(ID3D12Device *device, ID3D12Resource *resource, DXGI_FORMAT format, bool array, uint32 layers, D3D12_CPU_DESCRIPTOR_HANDLE dst)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC desc{}; desc.Format = format; desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    if (array) { desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY; desc.Texture2DArray.MipLevels = 1; desc.Texture2DArray.ArraySize = std::max(1u,layers); }
    else { desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; desc.Texture2D.MipLevels = 1; }
    device->CreateShaderResourceView(resource, &desc, dst);
}
void BufferView(ID3D12Device *device, ID3D12Resource *resource, uint32 count, uint32 stride, D3D12_CPU_DESCRIPTOR_HANDLE dst)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC desc{}; desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER; desc.Buffer.NumElements = std::max(1u,count); desc.Buffer.StructureByteStride = stride;
    device->CreateShaderResourceView(resource, &desc, dst);
}
}
struct CHighresLightmapsDX12::Impl
{
    std::recursive_mutex mutex;
    CShaderDeviceDX12 *device = nullptr;
    CShaderAPIDX12 *api = nullptr;
    DX12HighresMapStatus status{};
    char error[512] = {}, mapPath[260] = {};
    std::shared_ptr<const HlightNativeDomain> domain;
    std::shared_ptr<const HlightNativeAtlas> atlas;
    CUtlBuffer asset;
    hlight::FileView file{};
    const hlight::ModeView *mode = nullptr;
    bool enhancedRequired = false;
    bool bridgeAttempted = false;
    std::vector<DynamicFace> dynamics;
    std::vector<NativePage> pages;
    NativePage neutral; // Explicit native white/unlit standard textures; never a fabricated face.
    struct SpecialPage { ShaderAPITextureHandle_t handle = 0; uint64 serial = 0; };
    SpecialPage specialPages[2];
    std::vector<HlightFaceGpuDX12> faces;
    std::vector<HlightTileGpuDX12> tiles;
    Texture groups[4];
    std::vector<uint32> pageGroup, pageSlice;
    ComPtr<ID3D12Resource> faceBuffer, tileBuffer, failureBuffer;
    D3D12_RESOURCE_STATES faceState = D3D12_RESOURCE_STATE_COPY_DEST, tileState = D3D12_RESOURCE_STATE_COPY_DEST;
    std::vector<std::array<uint32,4>> visibilityFaces, visibilityMeshes;
    ComPtr<ID3D12Resource> visibilityBuffers[4];
    D3D12_RESOURCE_STATES visibilityStates[4] = {D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COPY_DEST};
    ComPtr<ID3D12DescriptorHeap> visibilityDescriptors;
    uint64 visibilityRetainedFence = 0;
    D3D12_RESOURCE_STATES failureState = D3D12_RESOURCE_STATE_COPY_DEST;
    bool metadataUploaded = false, failureUsed = false;
    struct Readback { uint64 fence, generation, sourceAddress; ComPtr<ID3D12Resource> resource; };
    std::vector<Readback> readbacks;
    std::vector<uint8> scratch;
    std::vector<ViewStyles> views;
    uint64 nextViewSerial = 1, commonRetainedFence = 0, polledFence = ~uint64(0);
    void Fail(const char *reason)
    {
        if (status.state != DX12_HIGHRES_REJECTED) V_strncpy(error, reason && *reason ? reason : "Highres lightmaps: native boundary failed", sizeof(error));
        status.state = DX12_HIGHRES_REJECTED;
    }
    void Detach()
    {
        if (!api) return;
        for (auto &page : pages)
        {
            auto *texture = api->FindTexture(page.handle);
            if (texture && texture->allocationSerial == page.serial) { texture->highresPage = kNoPage; texture->highresLayoutGeneration = 0; }
        }
    }
    void ClearGpu()
    {
        Detach(); pages.clear(); faces.clear(); tiles.clear();
        neutral={};
        for (auto &special : specialPages) special = {};
        for (auto &dynamic : dynamics) { dynamic.queued = false; dynamic.nextDirty = kNoPage; }
        for (auto &group : groups) group = {};
        faceBuffer.Reset(); tileBuffer.Reset(); failureBuffer.Reset();
        visibilityFaces.clear(); visibilityMeshes.clear(); visibilityDescriptors.Reset(); visibilityRetainedFence=0;
        for (uint32 i=0;i<4;++i) { visibilityBuffers[i].Reset(); visibilityStates[i]=D3D12_RESOURCE_STATE_COPY_DEST; }
        metadataUploaded = failureUsed = false;
        commonRetainedFence = 0;
        faceState = tileState = failureState = D3D12_RESOURCE_STATE_COPY_DEST;
        status.gpuBytes = 0;
    }
    void Poll()
    {
        if (!device) return;
        const uint64 completed = device->CompletedFenceValue();
        if (completed == polledFence) return;
        polledFence = completed;
        for (size_t i=0; i<readbacks.size();)
        {
            auto &r = readbacks[i]; if (r.fence > completed) { ++i; continue; }
            uint32 *value = nullptr; D3D12_RANGE read{0,4};
            if (FAILED(r.resource->Map(0,&read,reinterpret_cast<void **>(&value)))) Fail("Highres lightmaps: GPU validation readback failed");
            else { const uint32 failure = *value; D3D12_RANGE written{0,0}; r.resource->Unmap(0,&written);
                if (failure && domain && r.generation == domain->mapGeneration) Fail("Highres lightmaps: shader sampled an unowned native lightmap cell"); }
            readbacks.erase(readbacks.begin()+i);
        }
    }
    bool CopyFailure()
    {
        if (!failureUsed || !failureBuffer || !device->CommandList()) return true;
        const uint64 fence = device->NextFenceValue();
        Readback r{}; r.fence = fence; r.generation = domain->mapGeneration;
        r.sourceAddress = failureBuffer->GetGPUVirtualAddress();
        const bool reuse = !readbacks.empty() && readbacks.back().fence == fence &&
            readbacks.back().generation == r.generation && readbacks.back().sourceAddress == r.sourceAddress;
        if (reuse) r.resource = readbacks.back().resource;
        else if (!Buffer(device->NativeDevice(),256,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_COPY_DEST,r.resource)) return false;
        api->m_Pipeline.RetainExternalResource(r.resource.Get(), fence);
        api->m_Pipeline.RetainExternalResource(failureBuffer.Get(),fence);
        Transition(device->CommandList(),failureBuffer.Get(),failureState,D3D12_RESOURCE_STATE_COPY_SOURCE);
        device->CommandList()->CopyBufferRegion(r.resource.Get(),0,failureBuffer.Get(),0,4);
        Transition(device->CommandList(),failureBuffer.Get(),failureState,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        if (!reuse)
        {
            try { readbacks.push_back(std::move(r)); }
            catch (const std::bad_alloc &) { return false; }
        }
        failureUsed = false; return true;
    }
    // Small row chunks avoid a second whole-atlas allocation. UploadStructured provides
    // fence-owned upload storage; the native texture and its old contents remain owned.
    bool Upload(Texture &texture, uint32 slice, uint32 x, uint32 y, uint32 width, uint32 height,
        DXGI_FORMAT format, uint32 pixelBytes, const uint8 *source, size_t sourcePitch)
    {
        const uint32 pitch = (width*pixelBytes+255u)&~255u;
        const uint64 fence = device->NextFenceValue();
        auto *list = device->CommandList(); if (!list) return false;
        api->m_Pipeline.RetainExternalResource(texture.resource.Get(),fence);
        Transition(list,texture.resource.Get(),texture.state,D3D12_RESOURCE_STATE_COPY_DEST);
        for (uint32 row=0; row<height; row+=128)
        {
            const uint32 rows = std::min(128u,height-row); const size_t bytes = (size_t(pitch)*rows+511)&~size_t(511);
            if (bytes > scratch.size()) return false;
            memset(scratch.data(),0,bytes);
            if (source) for (uint32 r=0;r<rows;++r) memcpy(scratch.data()+size_t(r)*pitch,source+size_t(row+r)*sourcePitch,size_t(width)*pixelBytes);
            ID3D12Resource *upload = nullptr; uint64 offset = 0;
            if (!api->m_Pipeline.UploadStructured(scratch.data(),bytes,512,fence,&upload,&offset)) return false;
            D3D12_TEXTURE_COPY_LOCATION src{}, dst{}; src.pResource = upload; src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            src.PlacedFootprint.Offset = offset; src.PlacedFootprint.Footprint = {format,width,rows,1,pitch};
            dst.pResource = texture.resource.Get(); dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; dst.SubresourceIndex = slice;
            list->CopyTextureRegion(&dst,x,y+row,0,&src,nullptr);
        }
        return true;
    }
    bool UploadBuffer(ID3D12Resource *buffer, D3D12_RESOURCE_STATES &state, const void *data, size_t bytes,
        size_t destinationOffset=0, bool finalize=true)
    {
        if (!bytes) return true;
        const uint64 fence = device->NextFenceValue();
        api->m_Pipeline.RetainExternalResource(buffer,fence);
        Transition(device->CommandList(),buffer,state,D3D12_RESOURCE_STATE_COPY_DEST);
        // Keep dense map payloads out of a single oversized transient upload allocation.
        for (size_t copied=0;copied<bytes;)
        {
            const size_t chunk=std::min<size_t>(bytes-copied,8u*1024u*1024u);
            ID3D12Resource *upload=nullptr; uint64 offset=0;
            if (!api->m_Pipeline.UploadStructured(static_cast<const uint8 *>(data)+copied,chunk,1,fence,&upload,&offset)) return false;
            device->CommandList()->CopyBufferRegion(buffer,destinationOffset+copied,upload,offset,chunk);
            copied+=chunk;
        }
        if (finalize) Transition(device->CommandList(),buffer,state,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        return true;
    }
    bool InitializeUploads()
    {
        if (metadataUploaded) return true;
        if (!UploadBuffer(faceBuffer.Get(),faceState,faces.data(),faces.size()*sizeof(faces[0])) ||
            !UploadBuffer(tileBuffer.Get(),tileState,tiles.data(),tiles.size()*sizeof(tiles[0]))) return false;
        const auto &visibility=mode->visibility;
        if (!UploadBuffer(visibilityBuffers[0].Get(),visibilityStates[0],visibilityFaces.data(),visibilityFaces.size()*16) ||
            !UploadBuffer(visibilityBuffers[1].Get(),visibilityStates[1],visibility.entries,size_t(visibility.record->entryCount)*16) ||
            !UploadBuffer(visibilityBuffers[2].Get(),visibilityStates[2],visibility.payload,size_t(visibility.record->payloadBytes),0,false) ||
            !UploadBuffer(visibilityBuffers[3].Get(),visibilityStates[3],visibilityMeshes.data(),visibilityMeshes.size()*16)) return false;
        // Raw loads read whole uints: initialize the last partial word's unused bytes.
        if (visibility.record->payloadBytes & 3)
        {
            uint32 tail=0; const size_t end=size_t(visibility.record->payloadBytes)&~size_t(3);
            memcpy(&tail,visibility.payload+end,size_t(visibility.record->payloadBytes)-end);
            ID3D12Resource *upload=nullptr; uint64 offset=0; const uint64 fence=device->NextFenceValue();
            if (!api->m_Pipeline.UploadStructured(&tail,4,4,fence,&upload,&offset)) return false;
            Transition(device->CommandList(),visibilityBuffers[2].Get(),visibilityStates[2],D3D12_RESOURCE_STATE_COPY_DEST);
            device->CommandList()->CopyBufferRegion(visibilityBuffers[2].Get(),end,upload,offset,4);
        }
        // Append canonical overflow IDs without copying or repacking the dense R8 CPU payload.
        const size_t unbakedOffset=size_t((visibility.record->payloadBytes+3)&~uint64(3));
        if (!UploadBuffer(visibilityBuffers[2].Get(),visibilityStates[2],visibility.unbakedLightIndices,
            size_t(visibility.record->unbakedLightIndexCount)*sizeof(uint32),unbakedOffset,false)) return false;
        for (uint32 i=0;i<4;++i)
            Transition(device->CommandList(),visibilityBuffers[i].Get(),visibilityStates[i],D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        for (uint32 p=0;p<mode->record->pageCount;++p)
        {
            const auto &record = mode->pages[p]; auto &group = groups[pageGroup[p]];
            if (!Upload(group,pageSlice[p],0,0,record.width,record.height,DXGI_FORMAT_R16G16B16A16_FLOAT,8,file.base+record.pixelsOffset,size_t(record.width)*8)) return false;
        }
        for (auto &group : groups) if (group.resource) Transition(device->CommandList(),group.resource.Get(),group.state,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        for (auto &page : pages)
        {
            if (!Upload(page.ids,0,0,0,page.ids.width,page.ids.height,DXGI_FORMAT_R32_UINT,4,reinterpret_cast<const uint8 *>(page.owners.data()),size_t(page.ids.width)*4) ||
                !Upload(page.dynamic,0,0,0,page.dynamic.width,page.dynamic.height,DXGI_FORMAT_R32G32B32A32_FLOAT,16,nullptr,0)) return false;
            Transition(device->CommandList(),page.ids.resource.Get(),page.ids.state,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            Transition(device->CommandList(),page.dynamic.resource.Get(),page.dynamic.state,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        }
        uint32 zero[4] = {}; ID3D12Resource *upload = nullptr; uint64 offset = 0; const uint64 fence = device->NextFenceValue();
        if (!api->m_Pipeline.UploadStructured(zero,sizeof(zero),16,fence,&upload,&offset)) return false;
        api->m_Pipeline.RetainExternalResource(failureBuffer.Get(),fence);
        device->CommandList()->CopyBufferRegion(failureBuffer.Get(),0,upload,offset,sizeof(zero));
        Transition(device->CommandList(),failureBuffer.Get(),failureState,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        metadataUploaded = true; return true;
    }
    bool UploadDynamics(uint32 pageIndex)
    {
        auto &page = pages[pageIndex];
        while (page.dirtyHead != kNoPage)
        {
            const uint32 f = page.dirtyHead;
            auto &dynamic = dynamics[f]; const auto &placement = atlas->placements[f];
            const auto &face = domain->faces[f]; const uint32 width = face.extents[0]+1, height = face.extents[1]+1;
            const uint32 paddedWidth=width+2, paddedHeight=height+2;
            const uint32 pitch=(paddedWidth*16+255)&~255u;
            // Each plane's own replicated border is updated together with its entire interior.
            for (uint32 plane=0;plane<placement.planeCount;++plane)
            {
                const float *rgb=dynamic.rgb.data()+size_t(plane)*width*height*3;
                for (uint32 row=0;row<paddedHeight;row+=128)
                {
                    const uint32 rows=std::min(128u,paddedHeight-row); const size_t bytes=(size_t(pitch)*rows+511)&~size_t(511);
                    if (bytes>scratch.size()) return false; memset(scratch.data(),0,bytes);
                    for (uint32 y=0;y<rows;++y) for (uint32 x=0;x<paddedWidth;++x)
                    {
                        const uint32 sx=std::min(width-1,x?x-1:0), sy=std::min(height-1,row+y?row+y-1:0);
                        float *dst=reinterpret_cast<float *>(scratch.data()+size_t(y)*pitch+x*16);
                        memcpy(dst,rgb+(size_t(sy)*width+sx)*3,12);
                    }
                    ID3D12Resource *upload=nullptr; uint64 offset=0; const uint64 fence=device->NextFenceValue();
                    if (!api->m_Pipeline.UploadStructured(scratch.data(),bytes,512,fence,&upload,&offset)) return false;
                    api->m_Pipeline.RetainExternalResource(page.dynamic.resource.Get(),fence);
                    Transition(device->CommandList(),page.dynamic.resource.Get(),page.dynamic.state,D3D12_RESOURCE_STATE_COPY_DEST);
                    D3D12_TEXTURE_COPY_LOCATION src{},dst{}; src.pResource=upload; src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                    src.PlacedFootprint.Offset=offset; src.PlacedFootprint.Footprint={DXGI_FORMAT_R32G32B32A32_FLOAT,paddedWidth,rows,1,pitch};
                    dst.pResource=page.dynamic.resource.Get(); dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                    device->CommandList()->CopyTextureRegion(&dst,placement.origin[0]-1+plane*paddedWidth,placement.origin[1]-1+row,0,&src,nullptr);
                }
            }
            page.dirtyHead = dynamic.nextDirty;
            dynamic.nextDirty = kNoPage; dynamic.queued = false;
        }
        Transition(device->CommandList(),page.dynamic.resource.Get(),page.dynamic.state,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE); return true;
    }
};
CHighresLightmapsDX12::CHighresLightmapsDX12() : m_Impl(new Impl) {}
CHighresLightmapsDX12::~CHighresLightmapsDX12() { Shutdown(); delete m_Impl; }
void CHighresLightmapsDX12::Initialize(CShaderDeviceDX12 *device,CShaderAPIDX12 *api)
{
    std::lock_guard<std::recursive_mutex> lock(m_Impl->mutex); m_Impl->device=device; m_Impl->api=api;
    if (!m_Impl->bridgeAttempted)
    {
        m_Impl->bridgeAttempted=true;
        HlightEngineBridge::Initialize(g_pMaterialSystem,this);
    }
}
void CHighresLightmapsDX12::ReleaseDevice()
{
    Impl &s=*m_Impl; std::lock_guard<std::recursive_mutex> lock(s.mutex);
    s.Poll(); // Caller has submitted/drained the old GPU device before releasing resources.
    s.ClearGpu(); s.views.clear(); s.readbacks.clear(); s.polledFence=~uint64(0);
    if (s.mode && s.status.state!=DX12_HIGHRES_REJECTED) s.status.state=DX12_HIGHRES_PENDING;
    s.status.layoutGeneration=0; s.status.pageCount=0;
    s.device=nullptr; s.api=nullptr; // Native domains/assets/dynamic spans remain owned through restoration.
}
void CHighresLightmapsDX12::Shutdown()
{
    HlightEngineBridge::Shutdown(); std::lock_guard<std::recursive_mutex> lock(m_Impl->mutex);
    m_Impl->ClearGpu(); m_Impl->domain.reset(); m_Impl->atlas.reset(); m_Impl->asset.Purge();
    m_Impl->dynamics.clear(); m_Impl->views.clear(); m_Impl->readbacks.clear(); m_Impl->mode=nullptr;
    m_Impl->file={}; m_Impl->status={}; m_Impl->error[0]=0; m_Impl->mapPath[0]=0; m_Impl->device=nullptr; m_Impl->api=nullptr;
    m_Impl->enhancedRequired=false;
    m_Impl->bridgeAttempted=false;
}
bool CHighresLightmapsDX12::OnNativeDomain(std::shared_ptr<const HlightNativeDomain> domain)
{
    Impl &s=*m_Impl; std::lock_guard<std::recursive_mutex> lock(s.mutex);
    try
    {
        s.ClearGpu(); s.atlas.reset(); s.asset.Purge(); s.dynamics.clear(); s.mode=nullptr; s.file={}; s.status={}; s.error[0]=0;
        s.enhancedRequired=false;
        s.domain=std::move(domain);
        if (!s.domain || !s.domain->mapGeneration || !MapPath(s.domain->mapName,s.mapPath)) { s.Fail("Highres lightmaps: invalid native map domain"); return false; }
        s.status.nativeMapGeneration=s.domain->mapGeneration;
        s.status.faceLump=s.domain->faceLump; s.status.lightingLump=s.domain->lightingLump;
        IFileSystem *fs=g_pShaderDeviceMgrDX12?g_pShaderDeviceMgrDX12->HostFileSystem():nullptr;
        BspFile bsp(fs,s.mapPath); ShadowMapBspLumpInfo lumps[HEADER_LUMPS]; uint32 offset,bytes,version,flags; bool compressed;
        if (!ShadowMap_ReadBspDirectory(BspFile::Read,&bsp,lumps,offset,bytes,version,compressed,flags)) { s.Fail("Highres lightmaps: unreadable BSP directory"); return false; }
        if (version != hlight::kManifestVersion)
        {
            if (compressed || (!bytes && (flags&(LVLFLAGS_RUNTIME_SHADOWMAP_DIRECT_HDR|LVLFLAGS_RUNTIME_SHADOWMAP_DIRECT_NONHDR)))) s.Fail("Highres lightmaps: missing or compressed lighting manifest");
            else if (!bytes)
            {
                bool orphanAsset=false;
                if (!hlight::FindPakAsset(BspFile::Read,&bsp,lumps[LUMP_PAKFILE],orphanAsset) || orphanAsset)
                    s.Fail("Highres lightmaps: unreadable pak directory or orphan high-resolution asset");
            }
            return false; // Old enhanced manifests are rejected; baked local direct requires a rebake.
        }
        s.status.state=DX12_HIGHRES_PENDING;
        s.enhancedRequired=true;
        if (!bytes || bytes>hlight::kMaxFileBytes) { s.Fail("Highres lightmaps: invalid manifest range"); return false; }
        std::vector<uint8> manifest(bytes);
        if (!BspFile::Read(&bsp,offset,bytes,manifest.data())) { s.Fail("Highres lightmaps: cannot read manifest"); return false; }
        hlight::ManifestView manifestView{};
        if (!hlight::ValidateManifest(manifest.data(),bytes,flags,manifestView,s.error,sizeof(s.error))) { s.status.state=DX12_HIGHRES_REJECTED; return false; }
        const uint32 nativeMode=s.domain->lightingLump==LUMP_LIGHTING?SHADOWMAP_MODE_LDR:
            s.domain->lightingLump==LUMP_LIGHTING_HDR?SHADOWMAP_MODE_HDR:SHADOWMAP_MODE_COUNT;
        if (nativeMode==SHADOWMAP_MODE_COUNT) { s.Fail("Highres lightmaps: unknown selected native lighting lump"); return false; }
        if (!manifestView.mode[nativeMode].runtime)
        {
            s.enhancedRequired=false; s.status.state=DX12_HIGHRES_ORDINARY;
            return false; // This mode retains ordinary real BSP lighting and has no selected omission.
        }
        if (!HlightEngineBridge::Supported()) { s.Fail(HlightEngineBridge::CompatibilityError()); return false; }
        const auto &pak=lumps[LUMP_PAKFILE];
        if (pak.uncompressedSize || !pak.filelen || pak.filelen>hlight::kMaxFileBytes) { s.Fail("Highres lightmaps: invalid BSP pak range"); return false; }
        std::vector<uint8> pakBytes(pak.filelen);
        if (!BspFile::Read(&bsp,pak.fileofs,pak.filelen,pakBytes.data())) { s.Fail("Highres lightmaps: cannot read BSP pak"); return false; }
        std::unique_ptr<IZip,decltype(&IZip::ReleaseZip)> zip(IZip::CreateZip(),&IZip::ReleaseZip);
        if (!zip) { s.Fail("Highres lightmaps: cannot allocate ZIP reader"); return false; }
        zip->ParseFromBuffer(pakBytes.data(),int(pakBytes.size()));
        const bool read=zip->ReadFileFromZip(manifestView.header->assetPath,false,s.asset);
        if (!read || s.asset.TellPut()<=0 || uint32(s.asset.TellPut())>hlight::kMaxFileBytes) { s.Fail("Highres lightmaps: manifest-named BSP pak asset missing"); return false; }
        if (!hlight::ValidateFile(s.asset.Base(),uint32(s.asset.TellPut()),s.file,s.error,sizeof(s.error))) { s.status.state=DX12_HIGHRES_REJECTED; return false; }
        if (!hlight::ValidateManifestAsset(s.file,manifestView,s.error,sizeof(s.error))) { s.status.state=DX12_HIGHRES_REJECTED; return false; }
        s.mode=hlight::FindMode(s.file,s.domain->faceLump,s.domain->lightingLump);
        if (!s.mode || s.mode->record->faceCount!=s.domain->faces.size()) { s.Fail("Highres lightmaps: effective face/lighting domain mismatch"); return false; }
        if (!s.mode->visibility.record || s.mode->visibility.record->faceCount!=s.domain->faces.size())
        { s.Fail("Highres lightmaps: required hybrid visibility domain missing"); return false; }
        bool manifestMode=false;
        for (uint32 m=0;m<SHADOWMAP_MODE_COUNT;++m)
        {
            const auto *record=manifestView.mode[m].record;
            if (manifestView.mode[m].runtime && record->faceLump==s.domain->faceLump && record->lightingLump==s.domain->lightingLump &&
                record->assetMode==uint32(s.mode-s.file.mode) && record->facesCRC32==s.mode->record->facesCRC32 &&
                record->lightingCRC32==s.mode->record->lightingCRC32 && record->lightingBytes==s.mode->record->lightingBytes)
            {
                const uint32 worldlights=record->worldlightsLump;
                if (worldlights>=HEADER_LUMPS || !s.domain->identityPresent[worldlights] ||
                    s.domain->identities[worldlights].lump!=worldlights ||
                    s.domain->identities[worldlights].crc32!=record->worldlightsCRC32)
                { s.Fail("Highres lightmaps: effective worldlight fingerprint missing or mismatched"); return false; }
                manifestMode=true;
            }
        }
        if (!manifestMode) { s.Fail("Highres lightmaps: manifest asset-mode mismatch"); return false; }
        for (uint32 i=0;i<s.mode->record->identityCount;++i)
        {
            const auto &id=s.mode->identities[i];
            if (id.lump>=HEADER_LUMPS || !s.domain->identityPresent[id.lump])
            {
                char message[128];
                V_snprintf(message,sizeof(message),"Highres lightmaps: effective lump %u identity missing",id.lump);
                s.Fail(message); return false;
            }
            const auto &native=s.domain->identities[id.lump];
            if (native.lump!=id.lump || native.version!=id.version || native.bytes!=id.bytes || native.crc32!=id.crc32) { s.Fail("Highres lightmaps: effective lump fingerprint mismatch"); return false; }
        }
        s.dynamics.resize(s.domain->faces.size());
        for (uint32 f=0;f<s.domain->faces.size();++f)
        {
            const auto &native=s.domain->faces[f]; const auto &face=s.mode->faces[f];
            bool matchingExtents=true;
            for (uint32 axis=0;axis<2;++axis)
            {
                // The file canonicalizes the native -1 unlit sentinel to a one-sample
                // zero-extent domain. Actual eligibility is checked against the atlas.
                const uint32 extent=!face.styleCount && native.extents[axis]==hlight::kMissing?0:native.extents[axis];
                matchingExtents=matchingExtents && face.nativeExtents[axis]==extent;
            }
            if (face.faceOrdinal!=f || memcmp(face.nativeMins,native.mins,sizeof(native.mins)) || !matchingExtents ||
                (face.styleCount && (((face.flags^native.flags)&hlight::kFaceDisplacement) ||
                ((native.flags&hlight::kFaceBumped) && !(face.flags&hlight::kFaceBumped)))))
            { s.Fail("Highres lightmaps: immutable face metadata mismatch or missing required bump planes"); return false; }
            // Native styles may be RGBExp-pruned; every remaining native style must
            // occur in authored order in the retained unpruned asset style list.
            uint32 authored=0;
            for (uint32 n=0;face.styleCount && n<4 && native.styles[n]!=255;++n)
            {
                while (authored<face.styleCount && face.styles[authored]!=native.styles[n]) ++authored;
                if (authored==face.styleCount) { s.Fail("Highres lightmaps: native authored-style mismatch"); return false; } ++authored;
            }
            if (face.flags&hlight::kFaceHasLighting)
                s.dynamics[f].rgb.assign(size_t(native.extents[0]+1)*(native.extents[1]+1)*3*((face.flags&hlight::kFaceBumped)?4:1),0.f);
        }
        s.status.density=s.file.header->density; s.status.assetBytes=s.asset.TellPut(); s.status.faceCount=s.mode->record->faceCount; return true;
    }
    catch (const std::bad_alloc &) { s.Fail("Highres lightmaps: insufficient owned CPU memory"); return false; }
}
void CHighresLightmapsDX12::OnNativeAtlas(std::shared_ptr<const HlightNativeAtlas> atlas)
{
    Impl &s=*m_Impl; std::lock_guard<std::recursive_mutex> lock(s.mutex);
    if (!s.mode || s.status.state==DX12_HIGHRES_REJECTED) return;
    try
    {
        if (!atlas || atlas->domain!=s.domain || !atlas->layoutGeneration || atlas->placements.size()!=s.domain->faces.size() || atlas->models.size()!=s.mode->record->modelCount) { s.Fail("Highres lightmaps: incomplete finalized native atlas"); return; }
        if (s.device && s.device->CommandList() && !s.CopyFailure()) { s.Fail("Highres lightmaps: atlas replacement validation retirement failed"); return; }
        s.ClearGpu(); s.atlas=std::move(atlas); s.status.state=DX12_HIGHRES_PENDING;
        ID3D12Device *device=s.device?s.device->NativeDevice():nullptr; if (!device || !s.api) { s.Fail("Highres lightmaps: DX12 device unavailable"); return; }
        // The host does not register every fullbright handle with SetStandardTextureHandle.
        // Resolve explicit public roles in the drained atlas transaction, never from a draw.
        IShaderUtil *util = g_pShaderDeviceMgrDX12 ? g_pShaderDeviceMgrDX12->HostShaderUtil() : nullptr;
        if (!util) { s.Fail("Highres lightmaps: native standard texture resolver unavailable"); return; }
        const StandardTextureId_t specialRoles[] = { TEXTURE_LIGHTMAP_FULLBRIGHT, TEXTURE_LIGHTMAP_BUMPED_FULLBRIGHT };
        const ShaderAPITextureHandle_t savedBinding = s.api->m_BoundTextures[SHADER_SAMPLER15];
        for (uint32 role = 0; role < 2; ++role)
        {
            s.api->BindTexture(SHADER_SAMPLER15, INVALID_SHADERAPI_TEXTURE_HANDLE);
            util->BindStandardTexture(SHADER_SAMPLER15, specialRoles[role]);
            const ShaderAPITextureHandle_t handle = s.api->m_BoundTextures[SHADER_SAMPLER15];
            s.api->BindTexture(SHADER_SAMPLER15, savedBinding);
            const auto *texture = s.api->FindTexture(handle);
            if (!texture || !texture->resource || !texture->allocationSerial)
            { s.Fail("Highres lightmaps: unresolved native fullbright role"); return; }
            for (const auto &page : s.atlas->pages) if (page.handle == handle)
            { s.Fail("Highres lightmaps: native fullbright role aliases an atlas page"); return; }
            s.specialPages[role] = {handle, texture->allocationSerial};
        }
        s.pages.resize(s.atlas->pages.size()); size_t scratchBytes=512;
        for (uint32 p=0;p<s.pages.size();++p)
        {
            const auto &native=s.atlas->pages[p]; auto &page=s.pages[p]; auto *texture=s.api->FindTexture(native.handle);
            if (!texture || !texture->resource || !texture->allocationSerial || texture->width!=int(native.width) || texture->height!=int(native.height) || texture->depth!=1 || native.width>16384 || native.height>16384 || !native.width || !native.height) { s.Fail("Highres lightmaps: stale native lightmap allocation"); return; }
            for (uint32 previous=0;previous<p;++previous) if (s.pages[previous].handle==native.handle) { s.Fail("Highres lightmaps: duplicate native page handle"); return; }
            page.handle=native.handle; page.serial=texture->allocationSerial; page.owners.assign(size_t(native.width)*native.height,0);
            if (!MakeTexture(device,page.ids,native.width,native.height,1,DXGI_FORMAT_R32_UINT) || !MakeTexture(device,page.dynamic,native.width,native.height,1,DXGI_FORMAT_R32G32B32A32_FLOAT) || !Heap(device,page.descriptors,8)) { s.Fail("Highres lightmaps: native companion residency failed"); return; }
            scratchBytes=std::max(scratchBytes,size_t(native.width)*16*128+512);
        }
        s.faces.resize(s.domain->faces.size());
        // Validate full padded bumped allocations, not merely the owner/base rectangle.
        std::vector<std::vector<uint8>> occupied(s.pages.size());
        for (uint32 p=0;p<s.pages.size();++p) occupied[p].assign(s.pages[p].owners.size(),0);
        for (uint32 f=0;f<s.faces.size();++f)
        {
            const auto &face=s.mode->faces[f]; const auto &native=s.domain->faces[f]; const auto &place=s.atlas->placements[f]; auto &gpu=s.faces[f];
            const auto &model=s.mode->models[face.modelIndex]; const auto &range=s.atlas->models[face.modelIndex];
            if (range.firstFace!=model.firstFace || range.faceCount!=model.faceCount) { s.Fail("Highres lightmaps: model face-range mismatch"); return; }
            gpu.nativeRect[0]=place.origin[0]; gpu.nativeRect[1]=place.origin[1]; gpu.nativeRect[2]=native.extents[0]; gpu.nativeRect[3]=native.extents[1];
            gpu.dimensionsFlags[0]=face.highWidth; gpu.dimensionsFlags[1]=face.highHeight; gpu.dimensionsFlags[2]=face.flags; gpu.dimensionsFlags[3]=model.flags;
            memcpy(gpu.styles,face.styles,sizeof(gpu.styles)); memcpy(gpu.tiles,face.tiles,sizeof(gpu.tiles)); memcpy(gpu.bakedModelToWorld,model.bakedModelToWorld,sizeof(gpu.bakedModelToWorld));
            if (place.page<0) { if (place.planeCount) { s.Fail("Highres lightmaps: native special page has an allocation"); return; } continue; }
            const bool baked=(face.flags&hlight::kFaceHasLighting)!=0;
            if ((!baked && native.styles[0]!=255) || uint32(place.page)>=s.pages.size() ||
                (place.planeCount!=1 && place.planeCount!=4) ||
                (baked && place.planeCount==4 && !(face.flags&hlight::kFaceBumped)) ||
                !place.origin[0] || !place.origin[1]) { s.Fail("Highres lightmaps: invalid native face placement or missing required bump planes"); return; }
            auto &page=s.pages[place.page]; const uint32 w=native.extents[0]+3,h=native.extents[1]+3,x=place.origin[0]-1,y=place.origin[1]-1;
            if (uint64(x)+uint64(w)*place.planeCount>page.ids.width || uint64(y)+h>page.ids.height) { s.Fail("Highres lightmaps: padded native allocation out of bounds"); return; }
            for (uint32 row=y;row<y+h;++row) for (uint32 column=x;column<x+w*place.planeCount;++column)
            {
                const size_t cell=size_t(row)*page.ids.width+column;
                if (occupied[place.page][cell]) { s.Fail("Highres lightmaps: overlapping native allocations"); return; }
                occupied[place.page][cell]=1; if (column<x+w) page.owners[cell]=f+1;
            }
            if (baked)
            {
                s.dynamics[f].nextDirty = page.dirtyHead; s.dynamics[f].queued = true;
                page.dirtyHead = f; // restore retains copied executed deltas, including whole-face zero
            }
        }
        uint32 counts[4]={}; s.pageGroup.resize(s.mode->record->pageCount); s.pageSlice.resize(s.mode->record->pageCount);
        for (uint32 p=0;p<s.mode->record->pageCount;++p)
        {
            const auto &page=s.mode->pages[p]; uint32 group=0; while (group<4 && (2048u<<group)!=page.width) ++group;
            if (group==4 || page.height!=page.width) { s.Fail("Highres lightmaps: invalid atlas page group"); return; }
            s.pageGroup[p]=group; s.pageSlice[p]=counts[group]++; scratchBytes=std::max(scratchBytes,size_t(page.width)*8*128+512);
        }
        for (uint32 g=0;g<4;++g) if (counts[g] && !MakeTexture(device,s.groups[g],2048u<<g,2048u<<g,counts[g],DXGI_FORMAT_R16G16B16A16_FLOAT)) { s.Fail("Highres lightmaps: HDR atlas residency failed"); return; }
        s.tiles.resize(s.mode->record->tileCount);
        for (uint32 t=0;t<s.tiles.size();++t)
        {
            const auto &tile=s.mode->tiles[t]; auto &gpu=s.tiles[t]; gpu.address[0]=s.pageGroup[tile.page]; gpu.address[1]=s.pageSlice[tile.page]; gpu.address[2]=tile.x; gpu.address[3]=tile.y;
            gpu.size[0]=tile.width; gpu.size[1]=tile.height; gpu.size[2]=s.mode->pages[tile.page].width; gpu.size[3]=0;
        }
        if (!Buffer(device,s.faces.size()*sizeof(s.faces[0]),D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_COPY_DEST,s.faceBuffer) ||
            !Buffer(device,s.tiles.size()*sizeof(s.tiles[0]),D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_COPY_DEST,s.tileBuffer) ||
            !Buffer(device,256,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST,s.failureBuffer)) { s.Fail("Highres lightmaps: metadata residency failed"); return; }
        const auto &visibility=s.mode->visibility;
        s.visibilityFaces.resize(s.mode->record->faceCount);
        for (uint32 f=0;f<s.visibilityFaces.size();++f)
            s.visibilityFaces[f]={{visibility.faces[f].firstEntry,visibility.faces[f].entryCount,s.mode->faces[f].highWidth,s.mode->faces[f].highHeight}};
        s.visibilityMeshes.resize(visibility.record->meshCount);
        for (uint32 m=0;m<s.visibilityMeshes.size();++m)
            s.visibilityMeshes[m]={{visibility.meshes[m].firstEntry,visibility.meshes[m].entryCount,visibility.meshes[m].vertexCount,
                visibility.meshes[m].directPayloadBytes?hlight::kPropMeshHasBakedLocalDirect:0}};
        const uint64 unbakedBase=(visibility.record->payloadBytes+3)&~uint64(3);
        const uint64 rawBytes=unbakedBase+uint64(visibility.record->unbakedLightIndexCount)*sizeof(uint32);
        if (rawBytes>hlight::kMaxFileBytes) { s.Fail("Highres lightmaps: overflow visibility GPU range too large"); return; }
        for (uint32 i=0;i<visibility.record->unbakedFaceCount;++i)
        {
            const auto &unbaked=visibility.unbakedFaces[i]; auto &gpu=s.faces[unbaked.faceOrdinal];
            if (unbaked.lightCount>0xffffu) { s.Fail("Highres lightmaps: overflow face GPU count too large"); return; }
            const uint64 offset=unbakedBase+uint64(unbaked.firstLightIndex)*sizeof(uint32);
            // Existing owner metadata exposes empty lists without a separate directory fetch.
            gpu.dimensionsFlags[2]|=unbaked.lightCount<<16;
            gpu.dimensionsFlags[3]|=uint32(offset/4)<<1;
        }
        const uint64 visibilityBytes[4]={uint64(s.visibilityFaces.size())*16,uint64(visibility.record->entryCount)*16,rawBytes,uint64(s.visibilityMeshes.size())*16};
        if (!Heap(device,s.visibilityDescriptors,4)) { s.Fail("Highres lightmaps: visibility descriptor allocation failed"); return; }
        for (uint32 i=0;i<4;++i)
        {
            if (!Buffer(device,visibilityBytes[i],D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_COPY_DEST,s.visibilityBuffers[i]))
            { s.Fail("Highres lightmaps: visibility residency failed"); return; }
            if (i!=2) BufferView(device,s.visibilityBuffers[i].Get(),uint32(visibilityBytes[i]/16),16,Slot(device,s.visibilityDescriptors.Get(),i));
            else
            {
                D3D12_SHADER_RESOURCE_VIEW_DESC raw{}; raw.Format=DXGI_FORMAT_R32_TYPELESS;
                raw.ViewDimension=D3D12_SRV_DIMENSION_BUFFER; raw.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                raw.Buffer.NumElements=uint32(std::max<uint64>(1,visibilityBytes[i]/4)); raw.Buffer.Flags=D3D12_BUFFER_SRV_FLAG_RAW;
                device->CreateShaderResourceView(s.visibilityBuffers[i].Get(),&raw,Slot(device,s.visibilityDescriptors.Get(),i));
            }
        }
        s.scratch.resize(scratchBytes);
        const auto committedBytes = [device](ID3D12Resource *resource) -> uint64
        {
            if (!resource) return 0;
            const auto desc = resource->GetDesc();
            return device->GetResourceAllocationInfo(0,1,&desc).SizeInBytes;
        };
        s.status.gpuBytes=committedBytes(s.faceBuffer.Get())+committedBytes(s.tileBuffer.Get())+committedBytes(s.failureBuffer.Get());
        for (const auto &buffer:s.visibilityBuffers) s.status.gpuBytes+=committedBytes(buffer.Get());
        for (const auto &page:s.pages) s.status.gpuBytes+=committedBytes(page.ids.resource.Get())+committedBytes(page.dynamic.resource.Get());
        for (const auto &group:s.groups) s.status.gpuBytes+=committedBytes(group.resource.Get());
        for (uint32 p=0;p<s.pages.size();++p)
        {
            auto &page=s.pages[p]; TextureView(device,page.ids.resource.Get(),DXGI_FORMAT_R32_UINT,false,1,Slot(device,page.descriptors.Get(),0));
            BufferView(device,s.faceBuffer.Get(),uint32(s.faces.size()),sizeof(HlightFaceGpuDX12),Slot(device,page.descriptors.Get(),1));
            BufferView(device,s.tileBuffer.Get(),uint32(s.tiles.size()),sizeof(HlightTileGpuDX12),Slot(device,page.descriptors.Get(),2));
            TextureView(device,page.dynamic.resource.Get(),DXGI_FORMAT_R32G32B32A32_FLOAT,false,1,Slot(device,page.descriptors.Get(),3));
            for (uint32 g=0;g<4;++g) TextureView(device,s.groups[g].resource.Get(),DXGI_FORMAT_R16G16B16A16_FLOAT,true,s.groups[g].layers,Slot(device,page.descriptors.Get(),4+g));
        }
        if (!Heap(device,s.neutral.descriptors,8)) { s.Fail("Highres lightmaps: neutral descriptor allocation failed"); return; }
        s.neutral.ids.width=s.neutral.ids.height=1;
        TextureView(device,nullptr,DXGI_FORMAT_R32_UINT,false,1,Slot(device,s.neutral.descriptors.Get(),0));
        BufferView(device,nullptr,1,sizeof(HlightFaceGpuDX12),Slot(device,s.neutral.descriptors.Get(),1));
        BufferView(device,nullptr,1,sizeof(HlightTileGpuDX12),Slot(device,s.neutral.descriptors.Get(),2));
        TextureView(device,nullptr,DXGI_FORMAT_R32G32B32A32_FLOAT,false,1,Slot(device,s.neutral.descriptors.Get(),3));
        for (uint32 g=0;g<4;++g) TextureView(device,nullptr,DXGI_FORMAT_R16G16B16A16_FLOAT,true,1,Slot(device,s.neutral.descriptors.Get(),4+g));
        // Publish only after every validation/allocation/descriptor succeeds.
        for (uint32 p=0;p<s.pages.size();++p) { auto *texture=s.api->FindTexture(s.pages[p].handle); texture->highresPage=p; texture->highresLayoutGeneration=s.atlas->layoutGeneration; }
        s.status.layoutGeneration=s.atlas->layoutGeneration; s.status.pageCount=uint32(s.pages.size()); s.status.state=DX12_HIGHRES_READY;
    }
    catch (const std::bad_alloc &) { s.ClearGpu(); s.Fail("Highres lightmaps: insufficient atlas CPU memory"); }
}
void CHighresLightmapsDX12::OnNativeDynamic(uint64 generation,uint32 face,uint32 mask,uint32 width,uint32 height,const float *const rgb[4])
{
    Impl &s=*m_Impl; std::lock_guard<std::recursive_mutex> lock(s.mutex);
    if (!s.domain || generation!=s.domain->mapGeneration || !s.mode || s.status.state==DX12_HIGHRES_REJECTED) return;
    if (face>=s.dynamics.size() || width!=s.domain->faces[face].extents[0]+1 || height!=s.domain->faces[face].extents[1]+1) { s.Fail("Highres lightmaps: dynamic face identity mismatch"); return; }
    // Explicitly unlit owners use the untouched native samples, including any
    // executed legacy dynamics. They have no owned RGB planes to update.
    if (!(s.mode->faces[face].flags&hlight::kFaceHasLighting)) return;
    auto &dynamic=s.dynamics[face]; const uint32 planes=(s.mode->faces[face].flags&hlight::kFaceBumped)?4:1; const size_t samples=size_t(width)*height*3;
    if (dynamic.rgb.size()!=samples*planes || mask&~((1u<<planes)-1)) { s.Fail("Highres lightmaps: invalid dynamic plane mask"); return; }
    for (uint32 plane=0;plane<planes;++plane)
    {
        float *out=dynamic.rgb.data()+plane*samples;
        if (!(mask&(1u<<plane))) { memset(out,0,samples*sizeof(float)); continue; }
        if (!rgb || !rgb[plane]) { s.Fail("Highres lightmaps: missing executed dynamic span"); return; }
        for (size_t i=0;i<samples;++i) if (!std::isfinite(rgb[plane][i])) { s.Fail("Highres lightmaps: nonfinite executed dynamic RGB"); return; }
        memcpy(out,rgb[plane],samples*sizeof(float)); // preserve signed deltas; spans expire on return
    }
    if (s.atlas && !dynamic.queued && face < s.atlas->placements.size())
    {
        const int32 page = s.atlas->placements[face].page;
        if (page >= 0 && uint32(page) < s.pages.size())
        {
            dynamic.nextDirty = s.pages[page].dirtyHead; dynamic.queued = true;
            s.pages[page].dirtyHead = face;
        }
    }
}
void CHighresLightmapsDX12::OnNativeRetire(uint64 generation)
{
    Impl &s=*m_Impl; std::lock_guard<std::recursive_mutex> lock(s.mutex);
    if (!s.domain || generation!=s.domain->mapGeneration) return;
    if (s.device && s.device->CommandList() && !s.CopyFailure()) s.Fail("Highres lightmaps: cannot retire GPU validation");
    s.ClearGpu(); s.domain.reset(); s.atlas.reset(); s.asset.Purge(); s.dynamics.clear(); s.mode=nullptr; s.file={}; s.status={}; s.error[0]=0; s.mapPath[0]=0;
    s.enhancedRequired=false; s.views.clear();
}
void CHighresLightmapsDX12::OnNativeResourceRelease(uint64 generation)
{
    Impl &s=*m_Impl; std::lock_guard<std::recursive_mutex> lock(s.mutex);
    if (!s.domain || generation!=s.domain->mapGeneration || !s.mode) return;
    if (s.device && s.device->CommandList() && !s.CopyFailure()) s.Fail("Highres lightmaps: resource release validation retirement failed");
    const bool rejected = s.status.state == DX12_HIGHRES_REJECTED;
    s.ClearGpu(); if (!rejected) s.status.state=DX12_HIGHRES_PENDING;
    s.status.layoutGeneration=0; s.status.pageCount=0;
}
void CHighresLightmapsDX12::OnNativeFailure(const char *reason) { std::lock_guard<std::recursive_mutex> lock(m_Impl->mutex); m_Impl->Fail(reason); }
bool CHighresLightmapsDX12::EnhancedMap() const { std::lock_guard<std::recursive_mutex> lock(m_Impl->mutex); return m_Impl->enhancedRequired; }
bool CHighresLightmapsDX12::Rejected() const { std::lock_guard<std::recursive_mutex> lock(m_Impl->mutex); return m_Impl->status.state==DX12_HIGHRES_REJECTED; }
void CHighresLightmapsDX12::GetStatus(DX12HighresMapStatus &out,char *error,int bytes)
{
    std::lock_guard<std::recursive_mutex> lock(m_Impl->mutex); m_Impl->Poll(); out=m_Impl->status;
    if (error && bytes>0) V_strncpy(error,m_Impl->error,bytes);
}
bool CHighresLightmapsDX12::RequireMap(const char *map,DX12HighresMapStatus &out,char *error,int bytes)
{
    Impl &s=*m_Impl; std::lock_guard<std::recursive_mutex> lock(s.mutex); char path[260];
    if (!MapPath(map,path)) s.Fail("Highres lightmaps: invalid requested map name");
    else if (!s.domain || V_stricmp(path,s.mapPath))
    {
        // Inspect only admission metadata. Never synthesize a domain or attach late.
        IFileSystem *fs=g_pShaderDeviceMgrDX12?g_pShaderDeviceMgrDX12->HostFileSystem():nullptr;
        BspFile bsp(fs,path); ShadowMapBspLumpInfo lumps[HEADER_LUMPS]; uint32 offset,count,version,flags; bool compressed;
        if (!ShadowMap_ReadBspDirectory(BspFile::Read,&bsp,lumps,offset,count,version,compressed,flags)) s.Fail("Highres lightmaps: requested BSP unreadable");
        else if (version==hlight::kManifestVersion)
        {
            s.enhancedRequired=true;
            s.Fail(HlightEngineBridge::Supported()?"Highres lightmaps: native startup metadata unavailable; reload map":HlightEngineBridge::CompatibilityError());
        }
        else
        {
            if (!s.domain) { s.enhancedRequired=false; s.status={}; s.error[0]=0; }
            out={}; if (error && bytes>0) error[0]=0; return true;
        }
    }
    GetStatus(out,error,bytes);
    return out.state==DX12_HIGHRES_ORDINARY || out.state==DX12_HIGHRES_READY ||
        (out.state==DX12_HIGHRES_PENDING && s.mode && s.domain && !V_stricmp(path,s.mapPath));
}
void CHighresLightmapsDX12::BeginClientLevelShutdown() { HlightEngineBridge::BeginClientLevelShutdown(); }
void CHighresLightmapsDX12::EndClientLevelShutdown() { HlightEngineBridge::EndClientLevelShutdown(); }
bool CHighresLightmapsDX12::BeginClientResourceReadmission() { return HlightEngineBridge::BeginClientResourceReadmission(); }
void CHighresLightmapsDX12::EndClientResourceReadmission() { HlightEngineBridge::EndClientResourceReadmission(); }
void CHighresLightmapsDX12::ForgetTexture(ShaderAPITextureHandle_t handle,uint64 serial)
{
    Impl &s=*m_Impl; std::lock_guard<std::recursive_mutex> lock(s.mutex);
    auto *texture=s.api?s.api->FindTexture(handle):nullptr;
    if (!texture || texture->allocationSerial!=serial) return;
    bool associatedSpecial = false;
    for (auto &special : s.specialPages) if (special.handle == handle && special.serial == serial)
    { special = {}; associatedSpecial = true; }
    if (texture->highresPage==kNoPage && !associatedSpecial) return;
    texture->highresPage=kNoPage; texture->highresLayoutGeneration=0;
    // Only an associated native lightmap or explicit fullbright allocation begins replacement.
    if (s.mode && s.status.state==DX12_HIGHRES_READY) { s.status.state=DX12_HIGHRES_PENDING; s.status.layoutGeneration=0; }
}
void CHighresLightmapsDX12::BeginView(uint64 generation,const float styles[64])
{
    Impl &s=*m_Impl; std::lock_guard<std::recursive_mutex> lock(s.mutex); ViewStyles view{}; view.generation=generation;
    view.serial = s.nextViewSerial++;
    if (s.mode && (s.status.state != DX12_HIGHRES_READY || !s.domain || generation!=s.domain->mapGeneration || !styles))
        s.Fail("Highres lightmaps: view outside ready native generation");
    if (styles) for (uint32 i=0;i<64;++i) { if (!std::isfinite(styles[i]) || styles[i]<0) s.Fail("Highres lightmaps: invalid lightstyle snapshot"); view.values[i]=styles[i]; }
    try { s.views.push_back(view); }
    catch (const std::bad_alloc &) { s.Fail("Highres lightmaps: nested view storage allocation failed"); }
}
void CHighresLightmapsDX12::EndView()
{
    Impl &s=*m_Impl; std::lock_guard<std::recursive_mutex> lock(s.mutex);
    if (s.mode && !s.CopyFailure()) s.Fail("Highres lightmaps: GPU validation readback allocation failed");
    if (!s.views.empty()) s.views.pop_back(); else if (s.mode) s.Fail("Highres lightmaps: unbalanced nested view");
}
bool CHighresLightmapsDX12::PrepareVisibilityDraw(D3D12_CPU_DESCRIPTOR_HANDLE lightingTable)
{
    Impl &s=*m_Impl; std::lock_guard<std::recursive_mutex> lock(s.mutex); s.Poll();
    if (!s.mode) return !s.enhancedRequired;
    if (s.status.state!=DX12_HIGHRES_READY || !s.device || !s.api || !s.device->CommandList() ||
        !s.visibilityDescriptors || !s.InitializeUploads())
    { s.Fail("Highres lightmaps: immutable hybrid visibility unavailable"); return false; }
    const uint64 fence=s.device->NextFenceValue();
    if (s.visibilityRetainedFence!=fence)
    {
        for (auto &buffer:s.visibilityBuffers) s.api->m_Pipeline.RetainExternalResource(buffer.Get(),fence);
        s.visibilityRetainedFence=fence;
    }
    lightingTable.ptr+=SIZE_T(DX12_LIGHTING_T_VISIBILITY_FACES)*s.device->NativeDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    s.device->NativeDevice()->CopyDescriptorsSimple(4,lightingTable,s.visibilityDescriptors->GetCPUDescriptorHandleForHeapStart(),D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    return true;
}
bool CHighresLightmapsDX12::ResolveStaticPropMesh(const DX12StaticPropReceiver &receiver,
    uint64 meshToken,uint32 directory[4],uint32 &meshIndex)
{
    Impl &s=*m_Impl; std::lock_guard<std::recursive_mutex> lock(s.mutex);
    if (!s.mode || s.status.state!=DX12_HIGHRES_READY || s.views.empty() ||
        !s.domain || s.views.back().generation!=s.domain->mapGeneration) return false;
    const auto &visibility=s.mode->visibility;
    return ResolveStaticPropMeshDX12(receiver,meshToken,visibility.props,visibility.record->propCount,
        visibility.meshes,visibility.record->meshCount,directory,meshIndex);
}
bool CHighresLightmapsDX12::GetStaticPropDirect(uint32 meshIndex,StaticPropDirectMetadataDX12 &metadata)
{
    Impl &s=*m_Impl; std::lock_guard<std::recursive_mutex> lock(s.mutex);
    metadata={};
    if (!s.mode || s.status.state!=DX12_HIGHRES_READY || !s.mode->visibility.record ||
        meshIndex>=s.mode->visibility.record->meshCount)
    { s.Fail("Highres lightmaps: static-prop direct mesh domain unavailable"); return false; }
    const auto &visibility=s.mode->visibility;
    if (!ReadStaticPropDirectMetadataDX12(visibility,visibility.meshes[meshIndex],
        metadata.direct,metadata.styles,metadata.styleCount))
    { s.Fail("Highres lightmaps: invalid admitted static-prop direct metadata"); return false; }
    return true;
}
bool CHighresLightmapsDX12::PrepareDraw(uint32 samplerMask,const float modelToWorld[12],CPipelineCacheDX12::BindingInputDX12 &input)
{
    Impl &s=*m_Impl; std::lock_guard<std::recursive_mutex> lock(s.mutex); s.Poll();
    if (!s.mode || !samplerMask) return true;
    if (s.status.state!=DX12_HIGHRES_READY || !s.atlas || !s.device->CommandList() || s.views.empty() || s.views.back().generation!=s.domain->mapGeneration)
    {
        char reason[256];
        V_snprintf(reason,sizeof(reason),"Highres lightmaps: receiver outside native/view scope (state=%u atlas=%u list=%u views=%u native=%llu view=%llu)",
            unsigned(s.status.state),unsigned(bool(s.atlas)),unsigned(s.device->CommandList()!=nullptr),unsigned(s.views.size()),
            static_cast<unsigned long long>(s.domain?s.domain->mapGeneration:0),
            static_cast<unsigned long long>(s.views.empty()?0:s.views.back().generation));
        s.Fail(reason); return false;
    }
    uint32 pageIndex=kNoPage; bool neutral=false;
    for (uint32 sampler=0;sampler<16;++sampler) if (samplerMask&(1u<<sampler))
    {
        const ShaderAPITextureHandle_t handle=s.api->m_BoundTextures[sampler];
        const auto *texture=s.api->FindTexture(handle);
        bool white = false;
        if (texture) for (const auto &special : s.specialPages)
            white |= handle == special.handle && texture->allocationSerial == special.serial && special.serial != 0;
        if (white)
        {
            if (!texture || !texture->resource || pageIndex!=kNoPage) { s.Fail("Highres lightmaps: invalid or mixed native special page roles"); return false; }
            neutral=true; continue;
        }
        if (neutral || !texture || texture->highresPage>=s.pages.size() || texture->highresLayoutGeneration!=s.atlas->layoutGeneration ||
            texture->allocationSerial!=s.pages[texture->highresPage].serial || texture->id!=s.pages[texture->highresPage].handle ||
            (pageIndex!=kNoPage && pageIndex!=texture->highresPage))
        {
            s.Fail("Highres lightmaps: missing original native sampler association"); return false;
        }
        pageIndex=texture->highresPage;
    }
    if ((!neutral && pageIndex==kNoPage) || samplerMask&0xffff0000u || !modelToWorld) { s.Fail("Highres lightmaps: invalid native sampler-role metadata"); return false; }
    auto &page=neutral?s.neutral:s.pages[pageIndex]; auto &pipeline=s.api->m_Pipeline; const uint64 fence=s.device->NextFenceValue();
    if (!s.InitializeUploads() || (!neutral && !s.UploadDynamics(pageIndex))) { s.Fail("Highres lightmaps: native upload failed"); return false; }
    if (!pipeline.ReserveResourceDescriptors(8+32+16,fence)) { s.Fail("Highres lightmaps: descriptor residency failed"); return false; }
    const uint64 heap=pipeline.ResourceHeapGeneration();
    if (page.tableFence!=fence || page.table.generation!=heap)
    {
        page.table=pipeline.AllocateTransientResources(8,fence); if (page.table.count!=8) { s.Fail("Highres lightmaps: descriptor table allocation failed"); return false; }
        s.device->NativeDevice()->CopyDescriptorsSimple(8,page.table.cpu,page.descriptors->GetCPUDescriptorHandleForHeapStart(),D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); page.tableFence=fence;
    }
    if (!neutral && page.retainedFence != fence)
    {
        pipeline.RetainExternalResource(page.ids.resource.Get(),fence);
        pipeline.RetainExternalResource(page.dynamic.resource.Get(),fence); page.retainedFence=fence;
    }
    if (s.commonRetainedFence != fence)
    {
        pipeline.RetainExternalResource(s.faceBuffer.Get(),fence);
        pipeline.RetainExternalResource(s.tileBuffer.Get(),fence);
        pipeline.RetainExternalResource(s.failureBuffer.Get(),fence);
        for (auto &group:s.groups) if (group.resource) pipeline.RetainExternalResource(group.resource.Get(),fence);
        s.commonRetainedFence=fence;
    }
    if (page.constantsFence != fence || page.constantsView != s.views.back().serial ||
        memcmp(page.constantsModel,modelToWorld,sizeof(page.constantsModel)))
    {
        DrawConstants constants{}; constants.cHlightRoute[0]=neutral?0:1; constants.cHlightRoute[1]=page.ids.width; constants.cHlightRoute[2]=page.ids.height;
        memcpy(constants.cHlightModelToWorld,modelToWorld,sizeof(constants.cHlightModelToWorld));
        memcpy(constants.cHlightStyles,s.views.back().values.data(),sizeof(constants.cHlightStyles));
        if (!pipeline.UploadTransient(&constants,sizeof(constants),512,256,fence,page.constantsAddress)) { s.Fail("Highres lightmaps: draw constants residency failed"); return false; }
        page.constantsFence=fence; page.constantsView=s.views.back().serial;
        memcpy(page.constantsModel,modelToWorld,sizeof(page.constantsModel));
    }
    input.highresConstants=page.constantsAddress;
    input.highresAbi=true; input.highresTable=page.table;
    input.highresFailure=s.failureBuffer->GetGPUVirtualAddress(); s.failureUsed=true; return true;
}
}
