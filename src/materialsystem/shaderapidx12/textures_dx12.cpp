#include "pixelwriter.h"
#include "materialsystem/shaderapidx12/shaderapi_dx12.h"
#include <d3dcompiler.h>
#include "materialsystem/shaderapidx12/shaderdevice_dx12.h"
#include "shaderapi/ishaderutil.h"
#include "tier0/dbg.h"
#include "vtf/vtf.h"
#include "tracy_dx12.h"
#include "tier1/keyvalues.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <climits>

namespace shaderapidx12
{
namespace {
DXGI_FORMAT SRVFormat(DXGI_FORMAT format, bool srgb, bool depth);
bool IsDepthFormat(ImageFormat format)
{
    return format == IMAGE_FORMAT_NV_DST16 || format == IMAGE_FORMAT_ATI_DST16 || format == IMAGE_FORMAT_NV_DST24 || format == IMAGE_FORMAT_ATI_DST24 || format == IMAGE_FORMAT_NV_INTZ || format == IMAGE_FORMAT_NV_RAWZ;
}
bool IsFloatFormat(ImageFormat format)
{
    return format == IMAGE_FORMAT_RGB323232F || format == IMAGE_FORMAT_RGBA32323232F || format == IMAGE_FORMAT_RGBA16161616F || format == IMAGE_FORMAT_R32F;
}
D3D12_RESOURCE_DESC NormalizeResolveDesc(const D3D12_RESOURCE_DESC &source)
{
    D3D12_RESOURCE_DESC desc = source;
    desc.Alignment = 0;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    return desc;
}
bool SameResolveDesc(const D3D12_RESOURCE_DESC &a, const D3D12_RESOURCE_DESC &b)
{
    return a.Dimension == b.Dimension && a.Alignment == b.Alignment && a.Width == b.Width && a.Height == b.Height && a.DepthOrArraySize == b.DepthOrArraySize && a.MipLevels == b.MipLevels && a.Format == b.Format && a.SampleDesc.Count == b.SampleDesc.Count && a.SampleDesc.Quality == b.SampleDesc.Quality && a.Layout == b.Layout && a.Flags == b.Flags;
}
constexpr int kResolveTextureCacheCapacity = 16;
}
ImageFormat CShaderAPIDX12::GetNearestSupportedFormat(ImageFormat format, bool filteringRequired) const
{
    if (IsDepthFormat(format)) {
        const DXGI_FORMAT native = ImageFormatToDXGI12(format);
        if (native == DXGI_FORMAT_UNKNOWN) return IMAGE_FORMAT_NV_DST24;
        if (!device_ || !device_->NativeDevice()) return format;
        D3D12_FEATURE_DATA_FORMAT_SUPPORT support{}; support.Format = SRVFormat(native,false,true);
        const UINT required = D3D12_FORMAT_SUPPORT1_TEXTURE2D | (filteringRequired ? D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE : 0);
        return SUCCEEDED(device_->NativeDevice()->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,&support,sizeof(support))) && (support.Support1 & required) == required ? format : IMAGE_FORMAT_NV_DST24;
    }
    if (format == IMAGE_FORMAT_RGB323232F) format = IMAGE_FORMAT_RGBA32323232F;
    else if (format == IMAGE_FORMAT_ABGR8888 || format == IMAGE_FORMAT_ARGB8888 || format == IMAGE_FORMAT_RGB888 || format == IMAGE_FORMAT_BGR888 || format == IMAGE_FORMAT_IA88 || format == IMAGE_FORMAT_BGRA5551 || format == IMAGE_FORMAT_BGR565 || format == IMAGE_FORMAT_BGRX5551 || format == IMAGE_FORMAT_BGRA4444 || format == IMAGE_FORMAT_UV88 || format == IMAGE_FORMAT_UVWQ8888) format = IMAGE_FORMAT_RGBA8888;
    DXGI_FORMAT native = ImageFormatToDXGI12(format);
    if (native == DXGI_FORMAT_UNKNOWN) return IsFloatFormat(format) ? IMAGE_FORMAT_RGBA32323232F : IMAGE_FORMAT_RGBA8888;
    if (!device_ || !device_->NativeDevice()) return format;
    D3D12_FEATURE_DATA_FORMAT_SUPPORT support{}; support.Format = SRVFormat(native,false,false);
    const UINT required = D3D12_FORMAT_SUPPORT1_TEXTURE2D | (filteringRequired ? D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE : 0);
    if (FAILED(device_->NativeDevice()->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,&support,sizeof(support))) || (support.Support1 & required) != required) return IsFloatFormat(format) ? IMAGE_FORMAT_RGBA32323232F : IMAGE_FORMAT_RGBA8888;
    return format;
}
ImageFormat CShaderAPIDX12::GetNearestRenderTargetFormat(ImageFormat format) const
{
    if (IsDepthFormat(format)) {
        const ImageFormat selected = GetNearestSupportedFormat(format,false);
        const DXGI_FORMAT native = ImageFormatToDXGI12(selected);
        if (!device_ || !device_->NativeDevice()) return selected;
        D3D12_FEATURE_DATA_FORMAT_SUPPORT support{};
        support.Format = native == DXGI_FORMAT_R16_TYPELESS ? DXGI_FORMAT_D16_UNORM : DXGI_FORMAT_D24_UNORM_S8_UINT;
        return SUCCEEDED(device_->NativeDevice()->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,&support,sizeof(support))) && (support.Support1 & D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL) ? selected : IMAGE_FORMAT_NV_DST24;
    }
    const ImageFormat selected = GetNearestSupportedFormat(format,false);
    const DXGI_FORMAT native = ImageFormatToDXGI12(selected);
    if (native == DXGI_FORMAT_BC1_TYPELESS || native == DXGI_FORMAT_BC2_TYPELESS || native == DXGI_FORMAT_BC3_TYPELESS || native == DXGI_FORMAT_BC4_UNORM || native == DXGI_FORMAT_BC5_UNORM) return IMAGE_FORMAT_RGBA8888;
    if (!device_ || !device_->NativeDevice()) return selected;
    D3D12_FEATURE_DATA_FORMAT_SUPPORT support{}; support.Format = SRVFormat(native,false,false);
    if (FAILED(device_->NativeDevice()->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,&support,sizeof(support))) || !(support.Support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET)) return IMAGE_FORMAT_RGBA8888;
    return selected;
}
bool CShaderAPIDX12::DoRenderTargetsNeedSeparateDepthBuffer() const { return device_ && device_->SceneSampleCount() > 1; }
namespace
{
size_t BytesPerPixel(ImageFormat f)
{
    switch (f) {
    case IMAGE_FORMAT_A8: case IMAGE_FORMAT_I8: case IMAGE_FORMAT_P8: return 1;
    case IMAGE_FORMAT_NV_DST16: case IMAGE_FORMAT_ATI_DST16: return 2;
    case IMAGE_FORMAT_IA88: case IMAGE_FORMAT_RGB565: case IMAGE_FORMAT_BGR565: case IMAGE_FORMAT_BGRA5551: case IMAGE_FORMAT_BGRX5551: case IMAGE_FORMAT_BGRA4444: return 2;
    case IMAGE_FORMAT_RGB888: case IMAGE_FORMAT_BGR888: return 3;
    case IMAGE_FORMAT_RGBA16161616: case IMAGE_FORMAT_RGBA16161616F: return 8;
    case IMAGE_FORMAT_RGB323232F: return 12;
    case IMAGE_FORMAT_RGBA32323232F: return 16;
    case IMAGE_FORMAT_R32F: return 4;
    default: return 4;
    }
}
bool IsBlockCompressed(ImageFormat f)
{
    return f == IMAGE_FORMAT_DXT1 || f == IMAGE_FORMAT_DXT1_ONEBITALPHA || f == IMAGE_FORMAT_DXT3 || f == IMAGE_FORMAT_DXT5 || f == IMAGE_FORMAT_ATI1N || f == IMAGE_FORMAT_ATI2N;
}
size_t MipBytes(int w, int h, int d, ImageFormat f)
{
    if (IsBlockCompressed(f)) {
        const int bw = std::max(1, (w + 3) / 4), bh = std::max(1, (h + 3) / 4);
        const size_t block = (f == IMAGE_FORMAT_DXT1 || f == IMAGE_FORMAT_DXT1_ONEBITALPHA || f == IMAGE_FORMAT_ATI1N) ? 8u : 16u;
        return static_cast<size_t>(bw) * bh * block * std::max(1, d);
    }
    return static_cast<size_t>(std::max(1, w)) * std::max(1, h) * std::max(1, d) * BytesPerPixel(f);
}
int Faces(const CShaderAPIDX12::TextureRecord &t)
{
    return (t.flags & TEXTURE_CREATE_CUBEMAP) ? 6 : 1;
}
size_t SubresourceOffset(const CShaderAPIDX12::TextureRecord &t, int face, int mip, int copy = -1)
{
    size_t offset = static_cast<size_t>(copy < 0 ? t.currentCopy : copy) * t.bytesPerCopy;
    for (int f = 0; f < face; ++f)
        for (int m = 0; m < t.mipLevels; ++m) {
            const int w = std::max(1, t.width >> m), h = std::max(1, t.height >> m);
            offset += MipBytes(w, h, (t.flags & TEXTURE_CREATE_CUBEMAP) ? 1 : std::max(1, t.depth >> m), t.format);
        }
    for (int m = 0; m < mip; ++m) {
        const int w = std::max(1, t.width >> m), h = std::max(1, t.height >> m);
        offset += MipBytes(w, h, (t.flags & TEXTURE_CREATE_CUBEMAP) ? 1 : std::max(1, t.depth >> m), t.format);
    }
    return offset;
}
DXGI_FORMAT SRVFormat(DXGI_FORMAT format, bool srgb, bool depth)
{
    if (depth) {
        if (format == DXGI_FORMAT_R24G8_TYPELESS) return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        if (format == DXGI_FORMAT_R16_TYPELESS) return DXGI_FORMAT_R16_UNORM;
        if (format == DXGI_FORMAT_R32_TYPELESS) return DXGI_FORMAT_R32_FLOAT;
    }
    switch (format) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: return srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: return srgb ? DXGI_FORMAT_B8G8R8A8_UNORM_SRGB : DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_BC1_TYPELESS: return srgb ? DXGI_FORMAT_BC1_UNORM_SRGB : DXGI_FORMAT_BC1_UNORM;
    case DXGI_FORMAT_BC2_TYPELESS: return srgb ? DXGI_FORMAT_BC2_UNORM_SRGB : DXGI_FORMAT_BC2_UNORM;
    case DXGI_FORMAT_BC3_TYPELESS: return srgb ? DXGI_FORMAT_BC3_UNORM_SRGB : DXGI_FORMAT_BC3_UNORM;
    default: return format;
    }
}
bool CreateUpload(ID3D12Device *device, CCommandRecorderDX12 *list, ID3D12Resource *target, const void *src, size_t srcBytes, UINT subresource, D3D12_RESOURCE_STATES &state, CShaderDeviceDX12 *owner)
{
    ZoneNamedN(___tracy_scoped_zone, "DX12 CreateUpload", DX12_ZONES_ACTIVE);
    if (!device || !list || !target || !src) return false;
    const D3D12_RESOURCE_DESC td = target->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT rows = 0; UINT64 required = 0;
    device->GetCopyableFootprints(&td, subresource, 1, 0, &footprint, &rows, nullptr, &required);
    D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC bd{}; bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = required; bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    Microsoft::WRL::ComPtr<ID3D12Resource> upload;
    if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload)))) return false;
    void *mapped = nullptr; D3D12_RANGE range{};
    if (FAILED(upload->Map(0, &range, &mapped))) return false;
    const UINT slices = std::max<UINT>(1, footprint.Footprint.Depth);
    const UINT srcRow = rows ? static_cast<UINT>(srcBytes / (static_cast<size_t>(rows) * slices)) : 0;
    const UINT copyRow = std::min(srcRow, footprint.Footprint.RowPitch);
    const unsigned char *s = static_cast<const unsigned char *>(src);
    unsigned char *d = static_cast<unsigned char *>(mapped) + footprint.Offset;
    for (UINT slice = 0; slice < slices; ++slice) for (UINT row = 0; row < rows; ++row) std::memcpy(d + (static_cast<size_t>(slice) * rows + row) * footprint.Footprint.RowPitch, s + (static_cast<size_t>(slice) * rows + row) * srcRow, copyRow);
    upload->Unmap(0, nullptr);
    if (state != D3D12_RESOURCE_STATE_COPY_DEST) {
        D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource = target; b.Transition.StateBefore = state; b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST; b.Transition.Subresource = subresource; list->ResourceBarrier(1, &b); state = D3D12_RESOURCE_STATE_COPY_DEST;
    }
    D3D12_TEXTURE_COPY_LOCATION dst{}; dst.pResource = target; dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; dst.SubresourceIndex = subresource;
    D3D12_TEXTURE_COPY_LOCATION srcLoc{}; srcLoc.pResource = upload.Get(); srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; srcLoc.PlacedFootprint = footprint;
    list->CopyTextureRegion(&dst, 0, 0, 0, &srcLoc, nullptr);
    if (owner) owner->RetainResource(upload.Get());
    D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource = target; b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST; b.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE; b.Transition.Subresource = subresource; list->ResourceBarrier(1, &b); state = b.Transition.StateAfter;
    return true;
}
}

DXGI_FORMAT ImageFormatToDXGI12(ImageFormat format, int flags, bool depth)
{
    if (depth || IsDepthFormat(format)) {
        if (format == IMAGE_FORMAT_R32F) return DXGI_FORMAT_R32_TYPELESS;
        if (format == IMAGE_FORMAT_NV_DST16 || format == IMAGE_FORMAT_ATI_DST16) return DXGI_FORMAT_R16_TYPELESS;
        if (IsDepthFormat(format)) return DXGI_FORMAT_R24G8_TYPELESS;
        return DXGI_FORMAT_UNKNOWN;
    }
    switch (format) {
    case IMAGE_FORMAT_RGBA8888: return DXGI_FORMAT_R8G8B8A8_TYPELESS;
    case IMAGE_FORMAT_BGRA8888: case IMAGE_FORMAT_BGRX8888: return DXGI_FORMAT_B8G8R8A8_TYPELESS;
    case IMAGE_FORMAT_DXT1: case IMAGE_FORMAT_DXT1_ONEBITALPHA: return DXGI_FORMAT_BC1_TYPELESS;
    case IMAGE_FORMAT_DXT3: return DXGI_FORMAT_BC2_TYPELESS;
    case IMAGE_FORMAT_DXT5: return DXGI_FORMAT_BC3_TYPELESS;
    case IMAGE_FORMAT_ATI1N: return DXGI_FORMAT_BC4_UNORM;
    case IMAGE_FORMAT_ATI2N: return DXGI_FORMAT_BC5_UNORM;
    case IMAGE_FORMAT_RGBA16161616F: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case IMAGE_FORMAT_RGBA16161616: return DXGI_FORMAT_R16G16B16A16_UNORM;
    case IMAGE_FORMAT_R32F: return DXGI_FORMAT_R32_FLOAT;
    case IMAGE_FORMAT_RGBA32323232F: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case IMAGE_FORMAT_A8: return DXGI_FORMAT_R8_UNORM;
    case IMAGE_FORMAT_I8: return DXGI_FORMAT_R8_UNORM;
    case IMAGE_FORMAT_RGB565: return DXGI_FORMAT_B5G6R5_UNORM;
    default: return DXGI_FORMAT_UNKNOWN;
    }
}
ImageFormat DXGI12ToImageFormat(DXGI_FORMAT format)
{
    switch (format) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return IMAGE_FORMAT_RGBA8888;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return IMAGE_FORMAT_BGRA8888;
    case DXGI_FORMAT_BC1_TYPELESS: case DXGI_FORMAT_BC1_UNORM: case DXGI_FORMAT_BC1_UNORM_SRGB: return IMAGE_FORMAT_DXT1;
    case DXGI_FORMAT_BC2_TYPELESS: case DXGI_FORMAT_BC2_UNORM: case DXGI_FORMAT_BC2_UNORM_SRGB: return IMAGE_FORMAT_DXT3;
    case DXGI_FORMAT_BC3_TYPELESS: case DXGI_FORMAT_BC3_UNORM: case DXGI_FORMAT_BC3_UNORM_SRGB: return IMAGE_FORMAT_DXT5;
    case DXGI_FORMAT_BC4_UNORM: return IMAGE_FORMAT_ATI1N;
    case DXGI_FORMAT_BC5_UNORM: return IMAGE_FORMAT_ATI2N;
    case DXGI_FORMAT_R16G16B16A16_UNORM: return IMAGE_FORMAT_RGBA16161616;
    case DXGI_FORMAT_R8_UNORM: return IMAGE_FORMAT_I8;
    case DXGI_FORMAT_B5G6R5_UNORM: return IMAGE_FORMAT_RGB565;
    case DXGI_FORMAT_R16_TYPELESS: case DXGI_FORMAT_R16_UNORM: return IMAGE_FORMAT_NV_DST16;
    case DXGI_FORMAT_R24G8_TYPELESS: case DXGI_FORMAT_R24_UNORM_X8_TYPELESS: return IMAGE_FORMAT_NV_DST24;
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return IMAGE_FORMAT_RGBA16161616F;
    case DXGI_FORMAT_R32_FLOAT: return IMAGE_FORMAT_R32F;
    case DXGI_FORMAT_R32G32B32A32_FLOAT: return IMAGE_FORMAT_RGBA32323232F;
    default: return IMAGE_FORMAT_UNKNOWN;
    }
}

bool CShaderAPIDX12::EnsureTextureResident(TextureRecord &texture)
{
    if (texture.resourceResident) return true;
    if (!device_ || !device_->NativeDevice()) return false;
    if (texture.resources.IsEmpty()) return AllocateNativeTexture(texture);
    CUtlVector<ID3D12Pageable *> pages;
    pages.EnsureCapacity(texture.resources.Count());
    for (const auto &copy : texture.resources) if (copy.resource) pages.AddToTail(copy.resource.Get());
    if (pages.IsEmpty() || FAILED(device_->NativeDevice()->MakeResident(static_cast<UINT>(pages.Count()),pages.Base()))) return false;
    texture.resourceResident = true;
    return true;
}
void CShaderAPIDX12::AdvanceTextureCopy(TextureRecord &texture)
{
    if (!texture.switchNeeded || texture.copies <= 1) return;
    texture.currentCopy = (texture.currentCopy+1) % texture.copies; texture.sampledStateValid = false;++textureStateEpoch_;++textureIdentityEpoch_;
    texture.switchNeeded = false;
    if (!texture.resources.IsEmpty()) texture.resource = texture.resources[texture.currentCopy].resource;
    if (device_ && device_->NativeDevice()) {
        if (texture.rtvHeap) {const SIZE_T start=texture.rtvHeap->GetCPUDescriptorHandleForHeapStart().ptr;const SIZE_T stride=device_->NativeDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);texture.rtv.ptr=start+(static_cast<SIZE_T>(texture.currentCopy)*2)*stride;texture.rtvSRGB.ptr=texture.rtv.ptr+stride;}
        if (texture.dsvHeap) texture.dsv.ptr = texture.dsvHeap->GetCPUDescriptorHandleForHeapStart().ptr + static_cast<SIZE_T>(texture.currentCopy)*device_->NativeDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    }
    for (auto &bound : boundTextures_) if (bound == texture.id) bound = 0;
    for (auto &bound : vertexTextures_) if (bound == texture.id) bound = 0;
}
void CShaderAPIDX12::EvictManagedResources()
{
    if (!device_ || !device_->NativeDevice() || !device_->CommandList() || !device_->Submit(true)) return;
    FOR_EACH_HASHTABLE(textures_, entry) {
        TextureRecord &texture = *textures_[entry];
        if (!(texture.flags & TEXTURE_CREATE_MANAGED) || !texture.resourceResident) continue;
        CUtlVector<ID3D12Pageable *> pages;
        pages.EnsureCapacity(texture.resources.Count());
        for (const auto &copy : texture.resources) if (copy.resource) pages.AddToTail(copy.resource.Get());
        // Prepared-slot and render-target caches skip residency checks; invalidate them with the eviction.
        if (!pages.IsEmpty() && SUCCEEDED(device_->NativeDevice()->Evict(static_cast<UINT>(pages.Count()),pages.Base()))) { texture.resourceResident = false; texture.sampledStateValid = false; ++textureStateEpoch_; ++textureIdentityEpoch_; }
    }
}
CShaderAPIDX12::ResolveTextureRecord *CShaderAPIDX12::AcquireResolveTexture(const D3D12_RESOURCE_DESC &desc, HRESULT &creationResult)
{
    creationResult = S_OK;
    if (!device_ || !device_->NativeDevice()) { creationResult = E_FAIL; return nullptr; }
    const uint64_t completedFence = device_->CompletedFenceValue();
    auto trim = [&](ResolveTextureRecord *borrowed) {
        const int excess = resolveTextures_.Count() - kResolveTextureCacheCapacity;
        if (excess <= 0) return;
        int remaining = excess;
        const auto newEnd = std::remove_if(resolveTextures_.begin(), resolveTextures_.end(), [&](ResolveTextureRecord *record) {
            if (remaining <= 0 || record == borrowed || record->lastUseFence > completedFence) return false;
            delete record;
            --remaining;
            return true;
        });
        resolveTextures_.RemoveMultipleFromTail(static_cast<int>(resolveTextures_.end() - newEnd));
    };
    ResolveTextureRecord *match = nullptr;
    for (auto *record : resolveTextures_) if (SameResolveDesc(record->desc, desc)) { match = record; break; }
    trim(match);
    if (match) {
        match->lastUseFence = device_->NextFenceValue();
        return match;
    }
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    {
        ZoneNamedN(___tracy_scoped_zone, "DX12 BlitResolveAllocation", DX12_ZONES_ACTIVE);
        D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        creationResult = device_->NativeDevice()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RESOLVE_DEST, nullptr, IID_PPV_ARGS(&resource));
    }
    if (FAILED(creationResult)) return nullptr;
    auto *record = new ResolveTextureRecord;
    record->resource = std::move(resource);
    record->desc = desc;
    record->state = D3D12_RESOURCE_STATE_RESOLVE_DEST;
    record->lastUseFence = device_->NextFenceValue();
    resolveTextures_.AddToTail(record);
    trim(record);
    return record;
}
void CShaderAPIDX12::ReleaseTextureDeviceResources()
{
    for(auto &slot:preparedTextureSlots_)slot.valid=false; textureSetValid_=false; for(auto &layoutCache:inputLayoutCaches_)layoutCache.valid=false; textureTypeHandles_.fill(0); drawBindingNull_={}; ++pipelineMemoEpoch_;
    preparedSamplerTable_={};preparedSamplerFence_=0;
    for (auto *record : resolveTextures_) delete record;
    resolveTextures_.RemoveAll();
    clearPasses_.RemoveAll(); clearRoot_.Reset(); blitPasses_.RemoveAll(); blitRoot_.Reset(); blitRtvHeap_.Reset(); for (auto &cached : targetDescs_) cached = CachedResourceDescDX12{}; retiredTextureViews_.RemoveAll();
    FOR_EACH_HASHTABLE(textures_, entry) {
        TextureRecord &texture = *textures_[entry];
        for (int sub = 0; sub < texture.dirtySubresources.Count(); ++sub) {
            if (texture.gpuAuthoritativeSubresources[sub]) texture.initializedSubresources[sub] = 0;
            texture.dirtySubresources[sub] = texture.initializedSubresources[sub];
            texture.gpuAuthoritativeSubresources[sub] = 0;
        }
        texture.gpuDirty = std::any_of(texture.dirtySubresources.begin(),texture.dirtySubresources.end(),[](unsigned char dirty){return dirty != 0;});
        for(int slot=0;slot<2;++slot){pipeline_.ReleaseResourceDescriptor(texture.srvSources_[slot],0);texture.srvSources_[slot]={};texture.srvResources_[slot]=nullptr;texture.srvDescriptors_[slot]={};}
        texture.resource.Reset(); texture.resources.RemoveAll(); texture.rtvHeap.Reset(); texture.dsvHeap.Reset(); texture.rtv = {};texture.rtvSRGB={}; texture.dsv = {}; texture.subresourceStates.RemoveAll(); texture.resourceResident = false; texture.sampledStateValid = false;++textureStateEpoch_;++textureIdentityEpoch_;
    }
}
bool CShaderAPIDX12::RefreshTextureStaging(TextureRecord &texture, int face, int mip)
{
    if (!device_ || !device_->CommandList() || !EnsureTextureResident(texture) || face < 0 || face >= Faces(texture) || mip < 0 || mip >= texture.mipLevels) return false;
    const int sub = face * texture.mipLevels + mip;
    const int index = texture.currentCopy * Faces(texture) * texture.mipLevels + sub;
    if (!texture.gpuAuthoritativeSubresources[index]) return true;
    const D3D12_RESOURCE_DESC desc = texture.resource->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{}; UINT rows = 0; UINT64 required = 0;
    device_->NativeDevice()->GetCopyableFootprints(&desc,sub,1,0,&footprint,&rows,nullptr,&required);
    if (!required) return false;
    D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buffer{}; buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; buffer.Width = required; buffer.Height = 1; buffer.DepthOrArraySize = 1; buffer.MipLevels = 1; buffer.SampleDesc.Count = 1; buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    Microsoft::WRL::ComPtr<ID3D12Resource> readback;
    if (FAILED(device_->NativeDevice()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)))) return false;
    D3D12_RESOURCE_STATES &state = texture.subresourceStates[index]; texture.sampledStateValid = false;++textureStateEpoch_;
    if (state != D3D12_RESOURCE_STATE_COPY_SOURCE) { D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource = texture.resource.Get(); b.Transition.Subresource = sub; b.Transition.StateBefore = state; b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE; device_->CommandList()->ResourceBarrier(1,&b); state = D3D12_RESOURCE_STATE_COPY_SOURCE; }
    D3D12_TEXTURE_COPY_LOCATION source{}, destination{}; source.pResource = texture.resource.Get(); source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; source.SubresourceIndex = sub; destination.pResource = readback.Get(); destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; destination.PlacedFootprint = footprint;
    device_->CommandList()->CopyTextureRegion(&destination,0,0,0,&source,nullptr);
    device_->RetainResource(readback.Get());
    if (!device_->Submit(true)) return false;
    void *mapped = nullptr; D3D12_RANGE range{0,static_cast<SIZE_T>(required)}; if (FAILED(readback->Map(0,&range,&mapped))) return false;
    const int width = std::max(1,texture.width >> mip), height = std::max(1,texture.height >> mip), depth = Faces(texture) == 1 ? std::max(1,texture.depth >> mip) : 1;
    const size_t rowBytes = MipBytes(width,1,1,texture.format), rowCount = IsBlockCompressed(texture.format) ? static_cast<size_t>(std::max(1,(height+3)/4)) : static_cast<size_t>(height);
    auto *destinationBytes = texture.pixels.Base() + SubresourceOffset(texture,face,mip);
    const auto *sourceBytes = static_cast<const unsigned char *>(mapped) + footprint.Offset;
    for (int slice = 0; slice < depth; ++slice) for (size_t row = 0; row < rowCount; ++row) std::memcpy(destinationBytes+(static_cast<size_t>(slice)*rowCount+row)*rowBytes,sourceBytes+(static_cast<size_t>(slice)*rows+row)*footprint.Footprint.RowPitch,rowBytes);
    readback->Unmap(0,nullptr); texture.gpuAuthoritativeSubresources[index] = 0; texture.initializedSubresources[index] = 1;
    return true;
}
bool CShaderAPIDX12::AllocateNativeTexture(TextureRecord &texture)
{
    if (!device_ || !device_->NativeDevice()) return false;
    const bool depth = (texture.flags & TEXTURE_CREATE_DEPTHBUFFER) != 0;
    const DXGI_FORMAT format = ImageFormatToDXGI12(texture.format,texture.flags,depth);
    if (format == DXGI_FORMAT_UNKNOWN || (depth && ((texture.flags & TEXTURE_CREATE_CUBEMAP) || texture.depth > 1))) return false;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = texture.depth > 1 ? D3D12_RESOURCE_DIMENSION_TEXTURE3D : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = texture.width; desc.Height = texture.height;
    desc.DepthOrArraySize = static_cast<UINT16>(texture.depth > 1 ? texture.depth : Faces(texture));
    desc.MipLevels = static_cast<UINT16>(texture.mipLevels); desc.Format = format; desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    if (texture.flags & TEXTURE_CREATE_RENDERTARGET) desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if (depth) desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE clear{};
    clear.Format = depth ? (format == DXGI_FORMAT_R32_TYPELESS ? DXGI_FORMAT_D32_FLOAT : format == DXGI_FORMAT_R16_TYPELESS ? DXGI_FORMAT_D16_UNORM : DXGI_FORMAT_D24_UNORM_S8_UINT) : SRVFormat(format,false,false);
    if (depth) { clear.DepthStencil.Depth = 1.0f; clear.DepthStencil.Stencil = 0; } else clear.Color[3] = 1.0f;
    D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    const D3D12_RESOURCE_STATES initial = depth ? D3D12_RESOURCE_STATE_DEPTH_WRITE : (texture.flags & TEXTURE_CREATE_RENDERTARGET) ? D3D12_RESOURCE_STATE_RENDER_TARGET : D3D12_RESOURCE_STATE_COPY_DEST;
    // Recorded OMSetRenderTargets/Clear*View calls read these CPU descriptors at replay; retire them by fence.
    if (texture.rtvHeap || texture.dsvHeap) { RetiredTextureViewsDX12 views{}; views.rtv = texture.rtvHeap; views.dsv = texture.dsvHeap; views.fence = device_->NextFenceValue(); retiredTextureViews_[retiredTextureViews_.AddToTail()]=std::move(views); }
    ++textureStateEpoch_;++textureIdentityEpoch_; texture.resource.Reset(); texture.resources.RemoveAll(); texture.resources.EnsureCapacity(texture.copies); texture.rtvHeap.Reset(); texture.dsvHeap.Reset(); texture.rtv = {};texture.rtvSRGB={}; texture.dsv = {}; texture.resourceResident = false;
    for (int copy = 0; copy < texture.copies; ++copy) {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        if (FAILED(device_->NativeDevice()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,initial,(texture.flags & (TEXTURE_CREATE_RENDERTARGET | TEXTURE_CREATE_DEPTHBUFFER)) ? &clear : nullptr,IID_PPV_ARGS(&resource)))) return false;
        wchar_t debugName[512];if(!texture.name.empty()&&MultiByteToWideChar(CP_UTF8,0,texture.name.c_str(),-1,debugName,512))resource->SetName(debugName);
        texture.resources[texture.resources.AddToTail()].resource = std::move(resource);
    }
    texture.resource = texture.resources[texture.currentCopy].resource;
    if ((texture.flags & TEXTURE_CREATE_RENDERTARGET) && !depth) {
        D3D12_DESCRIPTOR_HEAP_DESC hd{}; hd.NumDescriptors = texture.copies*2; hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        if (FAILED(device_->NativeDevice()->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&texture.rtvHeap)))) return false;
        const auto start = texture.rtvHeap->GetCPUDescriptorHandleForHeapStart();
        const UINT increment = device_->NativeDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        D3D12_RENDER_TARGET_VIEW_DESC view{};view.ViewDimension=D3D12_RTV_DIMENSION_TEXTURE2D;
        for (int copy=0;copy<texture.copies;++copy)for(int encoding=0;encoding<2;++encoding){
            D3D12_CPU_DESCRIPTOR_HANDLE handle{start.ptr+(static_cast<SIZE_T>(copy)*2+encoding)*increment};
            view.Format=SRVFormat(format,encoding!=0,false);
            device_->NativeDevice()->CreateRenderTargetView(texture.resources[copy].resource.Get(),&view,handle);
        }
        texture.rtv.ptr=start.ptr+static_cast<SIZE_T>(texture.currentCopy)*2*increment;
        texture.rtvSRGB.ptr=texture.rtv.ptr+increment;
    }
    if (depth) {
        D3D12_DESCRIPTOR_HEAP_DESC hd{}; hd.NumDescriptors = texture.copies; hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        if (FAILED(device_->NativeDevice()->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&texture.dsvHeap)))) return false;
        const auto start = texture.dsvHeap->GetCPUDescriptorHandleForHeapStart();
        const UINT increment = device_->NativeDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
        D3D12_DEPTH_STENCIL_VIEW_DESC view{}; view.Format = clear.Format; view.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        for (int copy = 0; copy < texture.copies; ++copy) { D3D12_CPU_DESCRIPTOR_HANDLE handle{start.ptr + static_cast<SIZE_T>(copy)*increment}; device_->NativeDevice()->CreateDepthStencilView(texture.resources[copy].resource.Get(),&view,handle); }
        texture.dsv.ptr = start.ptr + static_cast<SIZE_T>(texture.currentCopy)*increment;
    }
    texture.subresourceStates.SetCount(texture.copies*Faces(texture)*texture.mipLevels);
    texture.subresourceStates.FillWithValue(initial);
    texture.resourceResident = true;
    return true;
}
ShaderAPITextureHandle_t CShaderAPIDX12::CreateTexture(int width, int height, int depth, ImageFormat format, int mipLevels, int copies, int flags, const char *debugName, const char *)
{
    ZoneNamedN(___tracy_scoped_zone, "DX12 CreateTexture", DX12_ZONES_ACTIVE);
    if (!device_ || !device_->NativeDevice() || width <= 0 || height <= 0 || depth <= 0 || format == IMAGE_FORMAT_UNKNOWN || width > 16384 || height > 16384 || depth > 2048 || mipLevels > 15 || copies > 64 || ((flags & TEXTURE_CREATE_CUBEMAP) && (width != height || depth != 1)) || (depth > 1 && (flags & TEXTURE_CREATE_RENDERTARGET)) || (IsDepthFormat(format) && !(flags & TEXTURE_CREATE_DEPTHBUFFER))) return 0;
    const ImageFormat nativeFormat = (flags & TEXTURE_CREATE_DEPTHBUFFER) ? format : (flags & TEXTURE_CREATE_RENDERTARGET) ? GetNearestRenderTargetFormat(format) : GetNearestSupportedFormat(format);
    if (nativeFormat == IMAGE_FORMAT_UNKNOWN || (nativeFormat != format && IsBlockCompressed(nativeFormat))) return 0;
    auto record = std::make_unique<TextureRecord>(); record->id = nextTexture_++; record->width = width; record->height = height; record->depth = depth; record->mipLevels = std::max(1,mipLevels); record->copies = std::max(1,copies); record->format = nativeFormat; record->requestedFormat = format; record->flags = flags; record->name = debugName ? debugName : "";
    const int faces = Faces(*record), subresources = faces * record->mipLevels;
    size_t total = 0; for (int face = 0; face < faces; ++face) for (int mip = 0; mip < record->mipLevels; ++mip) total += MipBytes(std::max(1,width >> mip),std::max(1,height >> mip),faces == 1 ? std::max(1,depth >> mip) : 1,nativeFormat);
    if (total > static_cast<size_t>(INT_MAX) / record->copies) return 0;
    record->bytesPerCopy = total;
    record->pixels.SetCount(static_cast<int>(total * record->copies));
    std::memset(record->pixels.Base(), 0, total * record->copies);
    record->dirtySubresources.SetCount(subresources * record->copies);
    record->dirtySubresources.FillWithValue(0);
    record->initializedSubresources.SetCount(subresources * record->copies);
    record->initializedSubresources.FillWithValue(0);
    record->gpuAuthoritativeSubresources.SetCount(subresources * record->copies);
    record->gpuAuthoritativeSubresources.FillWithValue(0);
    if (!AllocateNativeTexture(*record)) return 0;
    const ShaderAPITextureHandle_t id = record->id; textures_.Insert(id,record.release());
    return id;
}
void CShaderAPIDX12::ProcessPendingTextureDeletes()
{
    if (!device_ || !device_->IsRecordingOwner()) return;
    // Draws call this per draw; skip the lock and vector swap while nothing is queued.
    if (!device_->HasTextureDeletionRequests()) return;
    CUtlVector<uintptr_t> handles;
    device_->TakeTextureDeletionRequests(handles);
    for (uintptr_t handle : handles) DeleteTexture(static_cast<ShaderAPITextureHandle_t>(handle));
}
void CShaderAPIDX12::DeleteTexture(ShaderAPITextureHandle_t handle)
{
    if (handle <= 0) return;
    // All texture state, including draw bindings, belongs to the recording thread.
    if (device_ && !device_->IsRecordingOwner()) { device_->QueueTextureDeletion(static_cast<uintptr_t>(handle)); return; }
    auto it = FindTexture(handle); if (it == nullptr) return;
    for (size_t i = 0; i < textureTypeHandles_.size(); ++i) if (textureTypeHandles_[i] == handle) textureTypeHandles_[i] = 0; ++textureStateEpoch_;
    if (device_) {
        if(retiredTextureViews_.Count()){
            const auto remaining=std::remove_if(retiredTextureViews_.begin(),retiredTextureViews_.end(),[&](const RetiredTextureViewsDX12 &views){ return views.fence <= device_->CompletedFenceValue(); });
            retiredTextureViews_.RemoveMultipleFromTail(static_cast<int>(retiredTextureViews_.end()-remaining));
        }
        RetiredTextureViewsDX12 views{}; views.rtv = it->rtvHeap; views.dsv = it->dsvHeap; views.fence = device_->NextFenceValue();
        if (views.rtv || views.dsv) retiredTextureViews_[retiredTextureViews_.AddToTail()]=std::move(views);
        // A recording frame completes after every already submitted frame. Retaining
        // until its fence therefore also protects draws submitted before deletion.
        for (auto &copy : it->resources) if (copy.resource) device_->RetainResource(copy.resource.Get());
    }
    for(auto descriptor:it->srvSources_)pipeline_.ReleaseResourceDescriptor(descriptor,device_?device_->NextFenceValue():0);
    for(auto &slot:preparedTextureSlots_)if(slot.handle==handle){slot.valid=false;slot.record=nullptr;}
    { auto &cached = textureLookup_[static_cast<size_t>(handle) & (textureLookup_.size()-1)]; if (cached.handle == handle) cached = {}; }
    textures_.Remove(handle); delete it; ++textureIdentityEpoch_; if (modifiedTexture_ == handle) modifiedTexture_ = 0;
    if (renderTarget_ == handle) renderTarget_ = SHADER_RENDERTARGET_BACKBUFFER;
    for (size_t slot = 0; slot < renderTargets_.size(); ++slot) if (renderTargets_[slot] == handle) renderTargets_[slot] = slot ? SHADER_RENDERTARGET_NONE : SHADER_RENDERTARGET_BACKBUFFER;
    if (depthTarget_ == handle) depthTarget_ = SHADER_RENDERTARGET_DEPTHBUFFER;
    for (auto &b : boundTextures_) if (b == handle) b = 0;
    for (auto &b : vertexTextures_) if (b == handle) b = 0;
}
ShaderAPITextureHandle_t CShaderAPIDX12::CreateDepthTexture(ImageFormat format, int width, int height, const char *name, bool)
{ return CreateTexture(width, height, 1, format, 1, 1, TEXTURE_CREATE_DEPTHBUFFER | TEXTURE_CREATE_RENDERTARGET, name, "Depth"); }
bool CShaderAPIDX12::IsTexture(ShaderAPITextureHandle_t h) { return h > 0 && FindTexture(h) != nullptr; }
bool CShaderAPIDX12::IsTextureResident(ShaderAPITextureHandle_t h) { auto it = FindTexture(h); return it != nullptr && it->resourceResident; }
void CShaderAPIDX12::ModifyTexture(ShaderAPITextureHandle_t h) { auto it = FindTexture(h); if (it == nullptr) return; modifiedTexture_ = h; if (it->copies > 1) it->switchNeeded = true; }
void CShaderAPIDX12::TexImage2D(int level, int face, ImageFormat dstFormat, int z, int width, int height, ImageFormat srcFormat, bool tiled, void *data)
{ auto it = FindTexture(modifiedTexture_); if (it != nullptr && (it->format == dstFormat || it->requestedFormat == dstFormat)) TexSubImage2D(level, face, 0, 0, z, width, height, srcFormat, 0, tiled, data); }
void CShaderAPIDX12::TexSubImage2D(int level, int face, int x, int y, int z, int width, int height, ImageFormat srcFormat, int srcStride, bool tiled, void *data)
{
    (void)tiled; auto it = FindTexture(modifiedTexture_); if (it == nullptr || !data) return; TextureRecord &t = *it; if (level < 0 || level >= t.mipLevels || face < 0 || face >= Faces(t) || x < 0 || y < 0 || width <= 0 || height <= 0 || x + width > std::max(1, t.width >> level) || y + height > std::max(1, t.height >> level) || z < 0 || z >= std::max(1, t.depth >> level)) return; AdvanceTextureCopy(t); t.sampledStateValid = false;++textureStateEpoch_;
    const bool compressed = IsBlockCompressed(t.format);
    const size_t dstBpp = BytesPerPixel(t.format), srcBpp = BytesPerPixel(srcFormat); const int mw = std::max(1, t.width >> level), mh = std::max(1, t.height >> level); const size_t base = SubresourceOffset(t, face, level);
    const int subresource = t.currentCopy*Faces(t)*t.mipLevels + face * t.mipLevels + level;
    if (t.gpuAuthoritativeSubresources[subresource] && (x || y || z || width != mw || height != mh || (Faces(t) == 1 && t.depth > 1))) {
        if (!RefreshTextureStaging(t,face,level)) return;
    }
    if (compressed) {
        if (srcFormat != t.format || (x & 3) || (y & 3) || ((width & 3) && x + width != mw) || ((height & 3) && y + height != mh)) return;
        const size_t blockBytes = (t.format == IMAGE_FORMAT_DXT1 || t.format == IMAGE_FORMAT_DXT1_ONEBITALPHA || t.format == IMAGE_FORMAT_ATI1N) ? 8 : 16;
        const size_t dstPitch = static_cast<size_t>(std::max(1, (mw + 3) / 4)) * blockBytes, rowBytes = static_cast<size_t>(std::max(1, (width + 3) / 4)) * blockBytes;
        if (srcStride <= 0) srcStride = static_cast<int>(rowBytes);
        const size_t sliceBytes = dstPitch * std::max(1, (mh + 3) / 4);
        for (int row = 0; row < std::max(1, (height + 3) / 4); ++row) std::memcpy(t.pixels.Base() + base + static_cast<size_t>(z) * sliceBytes + (static_cast<size_t>(y / 4 + row) * dstPitch + static_cast<size_t>(x / 4) * blockBytes), static_cast<const unsigned char *>(data) + static_cast<size_t>(row) * srcStride, rowBytes);
    } else {
        if (srcStride <= 0) srcStride = IsBlockCompressed(srcFormat) ? static_cast<int>(MipBytes(width,1,1,srcFormat)) : static_cast<int>(width * srcBpp);
        unsigned char *dst = t.pixels.Base() + base + (static_cast<size_t>(z) * mw * mh + static_cast<size_t>(y) * mw + x) * dstBpp;
        if (srcFormat == t.format) for (int row = 0; row < height; ++row) std::memcpy(dst + static_cast<size_t>(row) * mw * dstBpp, static_cast<const unsigned char *>(data) + static_cast<size_t>(row) * srcStride, static_cast<size_t>(width) * dstBpp);
        else if (!shaderUtil_ || !shaderUtil_->ConvertImageFormat(static_cast<unsigned char *>(data), srcFormat, dst, t.format, width, height, srcStride, static_cast<int>(mw * dstBpp))) return;
    }
    t.gpuAuthoritativeSubresources[subresource] = 0;
    const UINT sub = static_cast<UINT>(face * t.mipLevels + level); t.dirtySubresources[subresource] = 1; t.initializedSubresources[subresource] = 1; t.gpuDirty = true;
    if (!EnsureTextureResident(t) || !device_ || !device_->CommandList() || t.resources.IsEmpty()) return;
    const size_t fullBytes = MipBytes(mw, mh, Faces(t) == 1 ? std::max(1, t.depth >> level) : 1, t.format);
    if (CreateUpload(device_->NativeDevice(),device_->CommandList(),t.resource.Get(),t.pixels.Base()+base,fullBytes,sub,t.subresourceStates[subresource],device_)) t.dirtySubresources[subresource] = 0;
    t.gpuDirty = std::any_of(t.dirtySubresources.begin(),t.dirtySubresources.end(),[](unsigned char dirty){return dirty != 0;});
}
void CShaderAPIDX12::TexImageFromVTF(IVTFTexture *vtf, int frame)
{
    auto it = FindTexture(modifiedTexture_);
    if (it == nullptr || !vtf || frame < 0 || frame >= vtf->FrameCount()) return;
    TextureRecord &texture = *it;
    // Legacy VTF cubemaps include a seventh fallback spheremap; native cubes upload only the six directional faces.
    const int faces = Faces(texture);
    if ((vtf->FaceCount() != faces && !(faces == 6 && vtf->FaceCount() == CUBEMAP_FACE_COUNT)) || vtf->MipCount() < texture.mipLevels || vtf->Format() == IMAGE_FORMAT_UNKNOWN) return;
    AdvanceTextureCopy(texture); texture.sampledStateValid = false;++textureStateEpoch_;
    const int count = Faces(texture) * texture.mipLevels;
    for (int face = 0; face < Faces(texture); ++face) for (int mip = 0; mip < texture.mipLevels; ++mip) {
        int width = 0, height = 0, depth = 0;
        vtf->ComputeMipLevelDimensions(mip,&width,&height,&depth);
        if (width != std::max(1,texture.width >> mip) || height != std::max(1,texture.height >> mip) || depth != (Faces(texture) == 1 ? std::max(1,texture.depth >> mip) : 1)) return;
        const int subresource = face * texture.mipLevels + mip, index = texture.currentCopy * count + subresource;
        const size_t destinationBytes = MipBytes(width,height,depth,texture.format);
        const int sourceBytes = shaderUtil_ ? shaderUtil_->GetMemRequired(width,height,depth,vtf->Format(),false) : vtf->ComputeMipSize(mip);
        if (sourceBytes <= 0 || vtf->ComputeMipSize(mip) < sourceBytes || (shaderUtil_ && !shaderUtil_->ImageFormatInfo(vtf->Format()).m_pName)) return;
        unsigned char *destination = texture.pixels.Base() + SubresourceOffset(texture,face,mip);
        if (vtf->Format() == texture.format) {
            if (static_cast<size_t>(sourceBytes) < destinationBytes) return;
            unsigned char *source = vtf->ImageData(frame,face,mip); if (!source) return;
            std::memcpy(destination,source,destinationBytes);
        } else {
            if (!shaderUtil_ || IsBlockCompressed(texture.format)) return;
            const int destinationStride = width * static_cast<int>(BytesPerPixel(texture.format));
            for (int slice = 0; slice < depth; ++slice) {
                unsigned char *source = vtf->ImageData(frame,face,mip,0,0,slice); if (!source || !shaderUtil_->ConvertImageFormat(source,vtf->Format(),destination+static_cast<size_t>(slice)*height*destinationStride,texture.format,width,height,vtf->RowSizeInBytes(mip),destinationStride)) return;
            }
        }
        texture.initializedSubresources[index] = 1; texture.dirtySubresources[index] = 1; texture.gpuAuthoritativeSubresources[index] = 0;
        if (!EnsureTextureResident(texture) || !device_->CommandList()) return;
        if (!CreateUpload(device_->NativeDevice(),device_->CommandList(),texture.resource.Get(),destination,destinationBytes,subresource,texture.subresourceStates[index],device_)) return;
        texture.dirtySubresources[index] = 0;
    }
    texture.gpuDirty = std::any_of(texture.dirtySubresources.begin(),texture.dirtySubresources.end(),[](unsigned char dirty){return dirty != 0;});
}
bool CShaderAPIDX12::TexLock(int level, int face, int x, int y, int width, int height, CPixelWriter &writer)
{
    auto it = FindTexture(modifiedTexture_); if (it == nullptr || level < 0 || level >= it->mipLevels || face < 0 || face >= Faces(*it) || IsBlockCompressed(it->format) || it->lockLevel >= 0) return false; TextureRecord &t = *it; const int mw = std::max(1, t.width >> level), mh = std::max(1, t.height >> level); if (x < 0 || y < 0 || width <= 0 || height <= 0 || x + width > mw || y + height > mh) return false; AdvanceTextureCopy(t); t.sampledStateValid = false;++textureStateEpoch_;
    const size_t pixelBytes = BytesPerPixel(t.format), rowBytes = static_cast<size_t>(width) * pixelBytes;
    if (!pixelBytes || rowBytes > INT_MAX || static_cast<size_t>(height) > INT_MAX / rowBytes) return false;
    const int subresource = t.currentCopy * Faces(t) * t.mipLevels + face * t.mipLevels + level;
    if (t.gpuAuthoritativeSubresources[subresource] && !RefreshTextureStaging(t,face,level)) return false;
    t.lockData.SetCountNonDestructively(static_cast<int>(rowBytes * height));
    const auto *source = t.pixels.Base() + SubresourceOffset(t,face,level) + (static_cast<size_t>(y) * mw + x) * pixelBytes;
    for (int row = 0; row < height; ++row) std::memcpy(t.lockData.Base() + static_cast<size_t>(row) * rowBytes, source + static_cast<size_t>(row) * mw * pixelBytes, rowBytes);
    t.lockLevel = level; t.lockFace = face; t.lockX = x; t.lockY = y; t.lockWidth = width; t.lockHeight = height; t.lockPitch = static_cast<int>(rowBytes); t.lockWrite = true; t.lockRead = false;
    writer.SetPixelMemory(t.format,t.lockData.Base(),t.lockPitch);
    return true;
}
void CShaderAPIDX12::TexUnlock()
{
    auto it = FindTexture(modifiedTexture_); if (it == nullptr || it->lockLevel < 0) return; TextureRecord &t = *it; TexSubImage2D(t.lockLevel, t.lockFace, t.lockX, t.lockY, 0, t.lockWidth, t.lockHeight, t.format, t.lockPitch, false, t.lockData.Base()); t.lockLevel = -1; t.lockData.RemoveAll();
}
void CShaderAPIDX12::TexSetPriority(int priority) { auto it = FindTexture(modifiedTexture_); if (it != nullptr) it->priority = priority; }
void CShaderAPIDX12::TexMinFilter(ShaderTexFilterMode_t mode) { auto it = FindTexture(modifiedTexture_); if (it != nullptr) { it->minFilter = static_cast<int>(mode); it->samplerDescriptorValid_[0]=it->samplerDescriptorValid_[1]=false;++textureStateEpoch_; } }
void CShaderAPIDX12::TexMagFilter(ShaderTexFilterMode_t mode) { auto it = FindTexture(modifiedTexture_); if (it != nullptr) { it->magFilter = static_cast<int>(mode); it->samplerDescriptorValid_[0]=it->samplerDescriptorValid_[1]=false;++textureStateEpoch_; } }
void CShaderAPIDX12::TexWrap(ShaderTexCoordComponent_t coord, ShaderTexWrapMode_t mode) { auto it = FindTexture(modifiedTexture_); if (it == nullptr) return; if (coord == SHADER_TEXCOORD_S) it->wrapU = static_cast<int>(mode); else if (coord == SHADER_TEXCOORD_T) it->wrapV = static_cast<int>(mode); else it->wrapW = static_cast<int>(mode); it->samplerDescriptorValid_[0]=it->samplerDescriptorValid_[1]=false;++textureStateEpoch_; }
void CShaderAPIDX12::TexLodClamp(int finest) { auto it = FindTexture(modifiedTexture_); if (it != nullptr) { it->lodClamp = finest; it->samplerDescriptorValid_[0]=it->samplerDescriptorValid_[1]=false;++textureStateEpoch_; } }
void CShaderAPIDX12::TexLodBias(float bias) { auto it = FindTexture(modifiedTexture_); if (it != nullptr) { it->lodBias = bias; it->samplerDescriptorValid_[0]=it->samplerDescriptorValid_[1]=false;++textureStateEpoch_; } }
// Like DX9 SetTextureState, count a bind only when the sampler binding actually changes.
void CShaderAPIDX12::BindTexture(Sampler_t sampler, ShaderAPITextureHandle_t h) { if (sampler >= 0 && sampler < static_cast<int>(boundTextures_.size()) && boundTextures_[sampler] != h) { boundTextures_[sampler] = h; const auto &slot=preparedTextureSlots_[sampler]; auto *record=slot.valid&&slot.record&&slot.handle==h?slot.record:FindTexture(h); if(record)++record->binds; } }
void CShaderAPIDX12::SetRenderTarget(ShaderAPITextureHandle_t color, ShaderAPITextureHandle_t depth)
{
    if (color > 0 && color == depth) return;
    renderTarget_ = color; depthTarget_ = depth;
    renderTargets_[0] = color;
    for(auto &bound:boundTextures_)if(bound>0&&(bound==color||bound==depth))bound=0;
    for(auto &bound:vertexTextures_)if(bound>0&&(bound==color||bound==depth))bound=0;
}
void CShaderAPIDX12::SetRenderTargetEx(int index, ShaderAPITextureHandle_t color, ShaderAPITextureHandle_t depth)
{
    if (index < 0 || index >= static_cast<int>(renderTargets_.size())) return;
    if (index == 0) { SetRenderTarget(color,depth); return; }
    renderTargets_[index] = color == SHADER_RENDERTARGET_BACKBUFFER ? SHADER_RENDERTARGET_NONE : color;
    if(color>0){
        for(auto &bound:boundTextures_)if(bound==color)bound=0;
        for(auto &bound:vertexTextures_)if(bound==color)bound=0;
    }
}
void CShaderAPIDX12::CopyRenderTargetToTexture(ShaderAPITextureHandle_t textureHandle) { CopyRenderTargetToTextureEx(textureHandle, 0, nullptr, nullptr); }
ITexture *CShaderAPIDX12::GetRenderTargetEx(int index) { return shaderUtil_ ? shaderUtil_->GetRenderTargetEx(index) : nullptr; }
ITexture *CShaderDynamicDX12::GetRenderTargetEx(int index) { return g_pShaderAPIDX12 ? g_pShaderAPIDX12->GetRenderTargetEx(index) : nullptr; }

bool CShaderAPIDX12::PrepareSampledTexture(ShaderAPITextureHandle_t h, bool srgb, ID3D12Resource **out, D3D12_SHADER_RESOURCE_VIEW_DESC &srv, D3D12_SAMPLER_DESC &sampler, D3D12_CPU_DESCRIPTOR_HANDLE *source, bool comparison)
{ return PrepareSampledTextureDX12(*this,h,srgb,out,srv,sampler,source,comparison); }
bool CShaderAPIDX12::PrepareRenderTargets(RenderTargetBindingDX12 &binding,bool encodeSRGB) { return PrepareRenderTargetsDX12(*this,binding,encodeSRGB); }

bool PrepareSampledTextureDX12(CShaderAPIDX12 &api, ShaderAPITextureHandle_t h, bool srgb, ID3D12Resource **out, D3D12_SHADER_RESOURCE_VIEW_DESC &srv, D3D12_SAMPLER_DESC &sampler, D3D12_CPU_DESCRIPTOR_HANDLE *source, bool comparison)
{
    if (!out) return false;
    if (h <= 0) { *out = nullptr; if(source)*source=comparison?D3D12_CPU_DESCRIPTOR_HANDLE{}:api.pipeline_.NullShaderResourceView(); srv = {}; srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; srv.Format = comparison ? DXGI_FORMAT_R32_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM; srv.Texture2D.MipLevels = 1; sampler = {}; sampler.Filter = comparison ? D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR : D3D12_FILTER_MIN_MAG_MIP_LINEAR; sampler.ComparisonFunc = comparison ? D3D12_COMPARISON_FUNC_LESS_EQUAL : D3D12_COMPARISON_FUNC_ALWAYS; sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP; sampler.MaxLOD = D3D12_FLOAT32_MAX; return true; }
    if (h == api.depthTarget_ || std::find(api.renderTargets_.begin(),api.renderTargets_.end(),h) != api.renderTargets_.end()) {
        auto found=api.FindTexture(h);
        Warning("ShaderAPIDX12: rejecting sampling bound target %lld (%s), primary=%lld depth=%lld shader=%s\n",static_cast<long long>(h),found==nullptr?"unknown":found->name.c_str(),static_cast<long long>(api.renderTarget_),static_cast<long long>(api.depthTarget_),api.activeSnapshot_.pixelShaderName.c_str());
        *out = nullptr; return false;
    }
    auto it = api.FindTexture(h); if (it == nullptr || !api.device_ || !api.device_->CommandList()) { *out = nullptr; return false; }
    if (comparison && !(it->flags & TEXTURE_CREATE_DEPTHBUFFER)) { Warning("ShaderAPIDX12: comparison sampling requires a depth texture (handle %lld)\n",static_cast<long long>(h)); *out = nullptr; return false; }
    auto &t = *it; if (!api.EnsureTextureResident(t)) { *out = nullptr; return false; } *out = t.resource.Get(); const int faces = Faces(t), count = faces * t.mipLevels;
    if (!t.sampledStateValid) {
    for (int sub = 0; sub < count; ++sub) {
        const int mip = sub % t.mipLevels, face = sub / t.mipLevels, stateIndex = t.currentCopy * count + sub;
        if (t.dirtySubresources[stateIndex]) {
            const size_t bytes = MipBytes(std::max(1,t.width >> mip),std::max(1,t.height >> mip),faces == 1 ? std::max(1,t.depth >> mip) : 1,t.format);
            if (!CreateUpload(api.device_->NativeDevice(),api.device_->CommandList(),t.resource.Get(),t.pixels.Base()+SubresourceOffset(t,face,mip),bytes,sub,t.subresourceStates[stateIndex],api.device_)) return false;
            t.dirtySubresources[stateIndex] = 0;
        }
        const D3D12_RESOURCE_STATES sampled = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        if (t.subresourceStates[stateIndex] != sampled) { D3D12_RESOURCE_BARRIER barrier{}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; barrier.Transition.pResource = t.resource.Get(); barrier.Transition.StateBefore = t.subresourceStates[stateIndex]; barrier.Transition.StateAfter = sampled; barrier.Transition.Subresource = sub; api.device_->CommandList()->ResourceBarrier(1, &barrier); t.subresourceStates[stateIndex] = sampled; }
    }
    t.sampledStateValid = true;
    // Uploads above are the only dirty-state change on this path.
    t.gpuDirty = std::any_of(t.dirtySubresources.begin(),t.dirtySubresources.end(),[](unsigned char dirty){return dirty != 0;});
    }
    const int slot = srgb ? 1 : 0;
    // Resource shape and component mapping are immutable; sampler state is rebuilt below.
    if (t.srvSources_[slot].ptr && t.srvResources_[slot] == t.resource.Get()) {
        srv = t.srvDescriptors_[slot];
        if (source) *source = t.srvSources_[slot];
    } else {
    srv = {}; srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; const bool depth = (t.flags & TEXTURE_CREATE_DEPTHBUFFER) != 0; srv.Format = SRVFormat(t.resource->GetDesc().Format, srgb, depth); if (t.flags & TEXTURE_CREATE_CUBEMAP) { srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE; srv.TextureCube.MipLevels = t.mipLevels; } else if (t.depth > 1) { srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D; srv.Texture3D.MipLevels = t.mipLevels; } else { srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; srv.Texture2D.MipLevels = t.mipLevels; }
    if (t.format == IMAGE_FORMAT_BGRX8888 || t.format == IMAGE_FORMAT_I8 || t.format == IMAGE_FORMAT_A8) {
        const auto r = D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_0, one = D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1;
        if (t.format == IMAGE_FORMAT_BGRX8888) srv.Shader4ComponentMapping = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_0,D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_1,D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_2,one);
        else if (t.format == IMAGE_FORMAT_A8) srv.Shader4ComponentMapping = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(one,one,one,r);
        else srv.Shader4ComponentMapping = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(r,r,r,one);
    }
    if (source) {
        // A recorded table copy may still read the old view; never rewrite a published source slot in place.
        if(t.srvSources_[slot].ptr){api.pipeline_.ReleaseResourceDescriptor(t.srvSources_[slot],api.device_->NextFenceValue());t.srvSources_[slot]={};}
        t.srvSources_[slot]=api.pipeline_.AcquireResourceDescriptor(api.device_->NextFenceValue());
        if(!t.srvSources_[slot].ptr)return false;
        api.device_->NativeDevice()->CreateShaderResourceView(t.resource.Get(), &srv, t.srvSources_[slot]);
        t.srvResources_[slot] = t.resource.Get(); t.srvDescriptors_[slot] = srv;
        *source = t.srvSources_[slot];
    }
    }
    const int samplerSlot = comparison ? 1 : 0; const int samplerAnisotropy = std::clamp(api.anisotropy_,1,16);
    const bool anisotropic = t.minFilter == SHADER_TEXFILTERMODE_ANISOTROPIC || t.magFilter == SHADER_TEXFILTERMODE_ANISOTROPIC;
    if (t.samplerDescriptorValid_[samplerSlot] && (!anisotropic || t.samplerDescriptorAnisotropy_ == samplerAnisotropy)) sampler = t.samplerDescriptors_[samplerSlot];
    else {
        sampler = {};
        const bool minLinear = t.minFilter == SHADER_TEXFILTERMODE_LINEAR || t.minFilter == SHADER_TEXFILTERMODE_LINEAR_MIPMAP_NEAREST || t.minFilter == SHADER_TEXFILTERMODE_LINEAR_MIPMAP_LINEAR;
        const bool magLinear = t.magFilter == SHADER_TEXFILTERMODE_LINEAR || t.magFilter == SHADER_TEXFILTERMODE_LINEAR_MIPMAP_NEAREST || t.magFilter == SHADER_TEXFILTERMODE_LINEAR_MIPMAP_LINEAR;
        const bool mipLinear = t.minFilter == SHADER_TEXFILTERMODE_NEAREST_MIPMAP_LINEAR || t.minFilter == SHADER_TEXFILTERMODE_LINEAR_MIPMAP_LINEAR;
        sampler.Filter = anisotropic ? (comparison ? D3D12_FILTER_COMPARISON_ANISOTROPIC : D3D12_FILTER_ANISOTROPIC) : static_cast<D3D12_FILTER>((minLinear ? 0x10 : 0) | (magLinear ? 0x4 : 0) | (mipLinear ? 0x1 : 0) | (comparison ? 0x80 : 0));
        sampler.AddressU = t.wrapU == SHADER_TEXWRAPMODE_CLAMP ? D3D12_TEXTURE_ADDRESS_MODE_CLAMP : t.wrapU == SHADER_TEXWRAPMODE_BORDER ? D3D12_TEXTURE_ADDRESS_MODE_BORDER : D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.AddressV = t.wrapV == SHADER_TEXWRAPMODE_CLAMP ? D3D12_TEXTURE_ADDRESS_MODE_CLAMP : t.wrapV == SHADER_TEXWRAPMODE_BORDER ? D3D12_TEXTURE_ADDRESS_MODE_BORDER : D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.AddressW = t.wrapW == SHADER_TEXWRAPMODE_CLAMP ? D3D12_TEXTURE_ADDRESS_MODE_CLAMP : t.wrapW == SHADER_TEXWRAPMODE_BORDER ? D3D12_TEXTURE_ADDRESS_MODE_BORDER : D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.MipLODBias = t.lodBias; sampler.MaxAnisotropy = anisotropic ? samplerAnisotropy : 1; sampler.ComparisonFunc = comparison ? D3D12_COMPARISON_FUNC_LESS_EQUAL : D3D12_COMPARISON_FUNC_ALWAYS; sampler.MaxLOD = t.minFilter <= SHADER_TEXFILTERMODE_LINEAR ? 0.f : D3D12_FLOAT32_MAX; sampler.MinLOD = std::min(static_cast<float>(std::max(0,t.lodClamp)),sampler.MaxLOD);
        t.samplerDescriptors_[samplerSlot] = sampler; t.samplerDescriptorValid_[samplerSlot] = true; t.samplerDescriptorAnisotropy_ = samplerAnisotropy;
    }
    return true;
}
bool PrepareRenderTargetsDX12(CShaderAPIDX12 &api, RenderTargetBindingDX12 &binding, bool encodeSRGB)
{
    const bool srgb=encodeSRGB&&api.EffectiveSRGBWrite();
    if (!api.device_ || !api.device_->CommandList()) return false;
    // Resolution (records, views, formats, validation) depends only on the bound handles, the sRGB view choice,
    // the view's scene targets and texture identity; state transitions and uploads below still run every call.
    auto &cache = api.renderTargetCache_;
    const bool cacheHit = cache.valid && cache.identityEpoch == api.textureIdentityEpoch_ && cache.srgb == srgb && cache.depthTarget == api.depthTarget_ &&
        cache.handles == api.renderTargets_ && cache.sceneColor == api.device_->SceneColor() && cache.sceneDepth == api.device_->SceneDepth() && cache.sceneRtv == api.device_->SceneRTV(srgb).ptr;
    std::array<CShaderAPIDX12::TextureRecord *,RenderTargetBindingDX12::kMaxColorTargets> &records = cache.records;
    CShaderAPIDX12::TextureRecord *&depth = cache.depth;
    if (cacheHit) binding = cache.binding;
    else {
    cache.valid = false;
    binding = {};
    records = {}; depth = nullptr;
    for (int index = 0; index < RenderTargetBindingDX12::kMaxColorTargets; ++index) {
        const ShaderAPITextureHandle_t handle = api.renderTargets_[index];
        if (handle == SHADER_RENDERTARGET_NONE) {
            for (int later = index+1; later < RenderTargetBindingDX12::kMaxColorTargets; ++later) if (api.renderTargets_[later] != SHADER_RENDERTARGET_NONE) { Warning("ShaderAPIDX12: MRT hole slot %d before occupied slot %d=%lld\n",index,later,static_cast<long long>(api.renderTargets_[later]));return false; }
            break;
        }
        if (handle == SHADER_RENDERTARGET_BACKBUFFER) {
            if (index != 0) { Warning("ShaderAPIDX12: backbuffer cannot occupy secondary MRT slot %d\n",index);return false; }
            binding.colors[index] = api.device_->SceneColor(); binding.rtvs[index] = api.device_->SceneRTV(srgb); binding.colorFormats[index] = api.device_->SceneColorFormat(srgb);
        } else {
            auto it = api.FindTexture(handle);
            if (it == nullptr || !(it->flags & TEXTURE_CREATE_RENDERTARGET) || (it->flags & TEXTURE_CREATE_DEPTHBUFFER) || !it->rtv.ptr || !api.EnsureTextureResident(*it)) { Warning("ShaderAPIDX12: invalid MRT slot %d handle %lld existing=%d\n",index,static_cast<long long>(handle),it!=nullptr);return false; }
            records[index] = it; binding.colors[index] = records[index]->resource.Get(); binding.rtvs[index] = srgb ? records[index]->rtvSRGB : records[index]->rtv;
        }
        if (!binding.colors[index]) return false;
        const auto &desc = api.targetDescs_[index].Get(binding.colors[index]);
        if(records[index])binding.colorFormats[index]=SRVFormat(desc.Format,srgb,false);
        if (!index) { binding.width = static_cast<int>(desc.Width); binding.height = static_cast<int>(desc.Height); binding.sampleCount = desc.SampleDesc.Count; binding.sampleQuality = desc.SampleDesc.Quality; }
        else if (desc.Width != static_cast<UINT64>(binding.width) || desc.Height != static_cast<UINT>(binding.height) || desc.SampleDesc.Count != static_cast<UINT>(binding.sampleCount) || desc.SampleDesc.Quality != static_cast<UINT>(binding.sampleQuality)) { Warning("ShaderAPIDX12: MRT slot %d size/sample mismatch primary=%dx%d sample=%d/%d slot=%llux%u sample=%u/%u handle=%lld\n",index,binding.width,binding.height,binding.sampleCount,binding.sampleQuality,desc.Width,desc.Height,desc.SampleDesc.Count,desc.SampleDesc.Quality,static_cast<long long>(handle));return false; }
        for (int previous = 0; previous < index; ++previous) if (binding.colors[previous] == binding.colors[index]) { Warning("ShaderAPIDX12: duplicate MRT resource in slots %d and %d\n",previous,index);return false; }
        ++binding.colorCount;
    }
    binding.color = binding.colors[0]; binding.rtv = binding.rtvs[0]; binding.colorFormat = binding.colorFormats[0];
    if (api.depthTarget_ == SHADER_RENDERTARGET_DEPTHBUFFER) {
        binding.depth = api.device_->SceneDepth(); binding.dsv = api.device_->SceneDSV(); binding.depthFormat = api.device_->SceneDepthFormat();
    } else if (api.depthTarget_ > 0) {
        auto it = api.FindTexture(api.depthTarget_); if (it == nullptr || !(it->flags & TEXTURE_CREATE_DEPTHBUFFER) || !it->dsv.ptr || !api.EnsureTextureResident(*it)) return false;
        depth = it; binding.depth = depth->resource.Get(); binding.dsv = depth->dsv;
    }
    if (binding.depth) {
        const auto &desc = api.targetDescs_[RenderTargetBindingDX12::kMaxColorTargets].Get(binding.depth);
        if(depth)binding.depthFormat=desc.Format==DXGI_FORMAT_R32_TYPELESS?DXGI_FORMAT_D32_FLOAT:desc.Format==DXGI_FORMAT_R16_TYPELESS?DXGI_FORMAT_D16_UNORM:DXGI_FORMAT_D24_UNORM_S8_UINT;
        if (!binding.colorCount) { binding.width = static_cast<int>(desc.Width); binding.height = static_cast<int>(desc.Height); binding.sampleCount = desc.SampleDesc.Count; binding.sampleQuality = desc.SampleDesc.Quality; }
        // Source shares the full-size depth surface with smaller water and postprocess targets.
        else if (desc.Width < static_cast<UINT64>(binding.width) || desc.Height < static_cast<UINT>(binding.height) || desc.SampleDesc.Count != static_cast<UINT>(binding.sampleCount) || desc.SampleDesc.Quality != static_cast<UINT>(binding.sampleQuality)) {
            static unsigned mismatches=0;if(mismatches++<8)Warning("ShaderAPIDX12: depth/color mismatch color=%dx%d samples=%d/%d depth=%llux%u samples=%u/%u colorHandle=%lld depthHandle=%lld\n",binding.width,binding.height,binding.sampleCount,binding.sampleQuality,desc.Width,desc.Height,desc.SampleDesc.Count,desc.SampleDesc.Quality,static_cast<long long>(api.renderTargets_[0]),static_cast<long long>(api.depthTarget_));
            return false;
        }
        for (int index = 0; index < binding.colorCount; ++index) if (binding.depth == binding.colors[index]) return false;
    }
    if (!binding.colorCount && !binding.depth) return false;
    cache.binding = binding; cache.identityEpoch = api.textureIdentityEpoch_; cache.srgb = srgb; cache.depthTarget = api.depthTarget_; cache.handles = api.renderTargets_;
    cache.sceneColor = api.device_->SceneColor(); cache.sceneDepth = api.device_->SceneDepth(); cache.sceneRtv = api.device_->SceneRTV(srgb).ptr; cache.valid = true;
    }
    auto flush = [&](CShaderAPIDX12::TextureRecord &texture) {
        const int index = texture.currentCopy * Faces(texture) * texture.mipLevels;
        if (!texture.dirtySubresources[index]) return true;
        const size_t bytes = MipBytes(texture.width,texture.height,1,texture.format);
        if (!CreateUpload(api.device_->NativeDevice(),api.device_->CommandList(),texture.resource.Get(),texture.pixels.Base()+SubresourceOffset(texture,0,0),bytes,0,texture.subresourceStates[index],api.device_)) return false;
        texture.dirtySubresources[index] = 0;
        return true;
    };
    auto transition = [&](CShaderAPIDX12::TextureRecord &texture,D3D12_RESOURCE_STATES desired) {
        D3D12_RESOURCE_STATES &state = texture.subresourceStates[texture.currentCopy * Faces(texture) * texture.mipLevels];
        if (state == desired) return;
        texture.sampledStateValid = false;++api.textureStateEpoch_;
        D3D12_RESOURCE_BARRIER barrier{}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; barrier.Transition.pResource = texture.resource.Get(); barrier.Transition.Subresource = 0; barrier.Transition.StateBefore = state; barrier.Transition.StateAfter = desired; api.device_->CommandList()->ResourceBarrier(1,&barrier); state = desired;
    };
    for (int index = 0; index < binding.colorCount; ++index) {
        if (records[index]) { if (!flush(*records[index])) return false; transition(*records[index],D3D12_RESOURCE_STATE_RENDER_TARGET); records[index]->gpuAuthoritativeSubresources[records[index]->currentCopy*Faces(*records[index])*records[index]->mipLevels] = 1; }
        else api.device_->TransitionSceneColor(D3D12_RESOURCE_STATE_RENDER_TARGET);
    }
    if (depth) { if (!flush(*depth)) return false; transition(*depth,D3D12_RESOURCE_STATE_DEPTH_WRITE); depth->gpuAuthoritativeSubresources[depth->currentCopy*Faces(*depth)*depth->mipLevels] = 1; }
    else if (binding.depth) api.device_->TransitionSceneDepth(D3D12_RESOURCE_STATE_DEPTH_WRITE);
    return true;
}

void CShaderAPIDX12::DrawMaskedClear(bool rgb, bool alpha, bool depth, const D3D12_RECT *rect, bool stencilOnly)
{
    if (!device_ || !device_->CommandList() || (!rgb && !alpha && !depth && !stencilOnly)) return;
    RenderTargetBindingDX12 target{}; if (!PrepareRenderTargets(target,false) || (depth && !target.depth)) return;
    if (stencilOnly && (!stencilEnabled_ || !target.depth || target.depthFormat != DXGI_FORMAT_D24_UNORM_S8_UINT)) return;
    const UINT8 stencilWriteMask = stencilOnly ? stencilWriteMask_ : 0;
    const auto stencilOperation = [](StencilOperation_t operation) {
        return operation >= STENCILOPERATION_KEEP && operation <= STENCILOPERATION_DECR ? static_cast<D3D12_STENCIL_OP>(operation) : D3D12_STENCIL_OP_KEEP;
    };
    const D3D12_STENCIL_OP stencilPass = stencilOnly ? stencilOperation(stencilPassOp_) : D3D12_STENCIL_OP_KEEP;
    const D3D12_STENCIL_OP stencilFail = stencilOnly ? stencilOperation(stencilFailOp_) : D3D12_STENCIL_OP_KEEP;
    ID3D12Device *native = device_->NativeDevice(); auto *list = device_->CommandList();
    const UINT8 mask = (rgb ? 7 : 0) | (alpha ? 8 : 0);
    const D3D12_COMPARISON_FUNC compare = stencilEnabled_ && target.depth && target.depthFormat == DXGI_FORMAT_D24_UNORM_S8_UINT ? static_cast<D3D12_COMPARISON_FUNC>(StencilCompare()) : D3D12_COMPARISON_FUNC_ALWAYS;
    if (!clearRoot_) {
        D3D12_ROOT_PARAMETER parameter{}; parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; parameter.Constants.Num32BitValues = 4; parameter.Constants.ShaderRegister = 0; parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC desc{}; desc.NumParameters = 1; desc.pParameters = &parameter; desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        Microsoft::WRL::ComPtr<ID3DBlob> blob, error; if (FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error)) || FAILED(native->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&clearRoot_)))) return;
    }
    ID3D12PipelineState *pipeline = nullptr;
    for (auto &pass : clearPasses_) if (pass.colors == target.colorFormats && pass.colorCount == target.colorCount && pass.depth == target.depthFormat && pass.samples == static_cast<UINT>(target.sampleCount) && pass.quality == static_cast<UINT>(target.sampleQuality) && pass.mask == mask && pass.stencilCompare == compare && pass.stencilReadMask == StencilReadMask() && pass.depthWrite == depth && pass.stencilOnly == stencilOnly && pass.stencilWriteMask == stencilWriteMask && pass.stencilPass == stencilPass && pass.stencilFail == stencilFail) { pipeline = pass.pipeline.Get(); break; }
    if (!pipeline) {
        static const char vsSource[] = "float4 main(uint id:SV_VertexID):SV_Position { float2 p=float2((id<<1)&2,id&2); return float4(p*float2(2,-2)+float2(-1,1),1,1); }";
        static const char psSource[] = "cbuffer Color:register(b0){float4 color;}\n#if TARGET_COUNT == 0\nvoid main(){}\n#else\nstruct Output{float4 a:SV_Target0;\n#if TARGET_COUNT > 1\nfloat4 b:SV_Target1;\n#endif\n#if TARGET_COUNT > 2\nfloat4 c:SV_Target2;\n#endif\n#if TARGET_COUNT > 3\nfloat4 d:SV_Target3;\n#endif\n}; Output main(){Output o;o.a=color;\n#if TARGET_COUNT > 1\no.b=color;\n#endif\n#if TARGET_COUNT > 2\no.c=color;\n#endif\n#if TARGET_COUNT > 3\no.d=color;\n#endif\nreturn o;}\n#endif\n";
        Microsoft::WRL::ComPtr<ID3DBlob> vs, ps, error;
        const std::string colorCount = std::to_string(target.colorCount); const D3D_SHADER_MACRO defines[] = {{"TARGET_COUNT",colorCount.c_str()},{nullptr,nullptr}};
        if (FAILED(D3DCompile(vsSource, sizeof(vsSource)-1, nullptr, nullptr, nullptr, "main", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vs, &error)) || (target.colorCount && FAILED(D3DCompile(psSource, sizeof(psSource)-1, nullptr, defines, nullptr, "main", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &ps, &error)))) return;
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{}; desc.pRootSignature = clearRoot_.Get(); desc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() }; if (ps) desc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() }; desc.SampleMask = UINT_MAX; desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; desc.NumRenderTargets = target.colorCount; for (int i = 0; i < target.colorCount; ++i) { desc.RTVFormats[i] = target.colorFormats[i]; desc.BlendState.RenderTarget[i].RenderTargetWriteMask = mask; } desc.DSVFormat = target.depth ? target.depthFormat : DXGI_FORMAT_UNKNOWN; desc.SampleDesc.Count = target.sampleCount; desc.SampleDesc.Quality = target.sampleQuality; desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID; desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; desc.RasterizerState.DepthClipEnable = TRUE; desc.RasterizerState.MultisampleEnable = target.sampleCount > 1; desc.DepthStencilState.DepthEnable = depth; desc.DepthStencilState.DepthWriteMask = depth ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO; desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        desc.DepthStencilState.StencilEnable = target.depth && target.depthFormat == DXGI_FORMAT_D24_UNORM_S8_UINT && stencilEnabled_; desc.DepthStencilState.StencilReadMask = StencilReadMask(); desc.DepthStencilState.StencilWriteMask = stencilWriteMask; desc.DepthStencilState.FrontFace.StencilFunc = compare; desc.DepthStencilState.FrontFace.StencilFailOp = stencilFail; desc.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP; desc.DepthStencilState.FrontFace.StencilPassOp = stencilPass; desc.DepthStencilState.BackFace = desc.DepthStencilState.FrontFace;
        ClearPassDX12 pass{}; pass.color = target.colorFormat; pass.colors = target.colorFormats; pass.colorCount = target.colorCount; pass.depth = target.depthFormat; pass.samples = target.sampleCount; pass.quality = target.sampleQuality; pass.mask = mask; pass.stencilCompare = compare; pass.stencilReadMask = StencilReadMask(); pass.depthWrite = depth; pass.stencilOnly = stencilOnly; pass.stencilWriteMask = stencilWriteMask; pass.stencilPass = stencilPass; pass.stencilFail = stencilFail; if (FAILED(native->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pass.pipeline)))) return; pipeline = pass.pipeline.Get(); clearPasses_[clearPasses_.AddToTail()]=std::move(pass);
    }
    D3D12_VIEWPORT viewport{0,0,static_cast<float>(target.width),static_cast<float>(target.height),0,1}; D3D12_RECT scissor = rect ? *rect : D3D12_RECT{0,0,target.width,target.height};
    list->RSSetViewports(1, &viewport); list->RSSetScissorRects(1, &scissor); list->OMSetRenderTargets(target.colorCount, target.colorCount ? target.rtvs.data() : nullptr, FALSE, target.depth ? &target.dsv : nullptr); list->OMSetStencilRef(StencilReference()); list->SetGraphicsRootSignature(clearRoot_.Get()); list->SetGraphicsRoot32BitConstants(0, 4, clearColor_, 0); list->SetPipelineState(pipeline); list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST); list->DrawInstanced(3, 1, 0, 0);
    pipeline_.InvalidateGraphicsBindings();
}
void CShaderAPIDX12::ClearBuffers(bool color, bool depth, bool stencil, int width, int height)
{
    if (!device_ || !device_->CommandList()) return; RenderTargetBindingDX12 binding{}; if (!PrepareRenderTargets(binding,false)) return;
    D3D12_RECT rect{0,0,width > 0 ? std::min(width, binding.width) : binding.width,height > 0 ? std::min(height, binding.height) : binding.height};
    if (color) for (int index = 0; index < binding.colorCount; ++index) device_->CommandList()->ClearRenderTargetView(binding.rtvs[index], clearColor_, 1, &rect);
    if (binding.depth) { UINT flags = (depth ? D3D12_CLEAR_FLAG_DEPTH : 0) | (stencil && binding.depthFormat == DXGI_FORMAT_D24_UNORM_S8_UINT ? D3D12_CLEAR_FLAG_STENCIL : 0); if (flags) device_->CommandList()->ClearDepthStencilView(binding.dsv, static_cast<D3D12_CLEAR_FLAGS>(flags), 1.0f, 0, 1, &rect); }
}
void CShaderAPIDX12::ClearBuffersObeyStencil(bool color, bool depth) { DrawMaskedClear(color, color, depth); }
void CShaderAPIDX12::ClearBuffersObeyStencilEx(bool color, bool alpha, bool depth) { DrawMaskedClear(color, alpha, depth); }
void CShaderAPIDX12::ClearStencilBufferRectangle(int x0, int y0, int x1, int y1, int value) { if (!device_ || !device_->CommandList()) return; RenderTargetBindingDX12 target{}; if (!PrepareRenderTargets(target) || !target.depth || target.depthFormat != DXGI_FORMAT_D24_UNORM_S8_UINT) return; D3D12_RECT rect{std::max(0,x0),std::max(0,y0),std::min(target.width,x1),std::min(target.height,y1)}; if (rect.left < rect.right && rect.top < rect.bottom) device_->CommandList()->ClearDepthStencilView(target.dsv, D3D12_CLEAR_FLAG_STENCIL, 1.0f, static_cast<UINT8>(value), 1, &rect); }

void CShaderAPIDX12::ReadPixels(int x, int y, int width, int height, unsigned char *data, ImageFormat dst) { Rect_t src{x,y,width,height}, out{0,0,width,height}; ReadPixels(&src, &out, data, dst, width * static_cast<int>(BytesPerPixel(dst))); }
void CShaderAPIDX12::ReadPixels(Rect_t *srcRect, Rect_t *dstRect, unsigned char *data, ImageFormat format, int stride)
{
    ZoneNamedN(___tracy_scoped_zone, "DX12 ReadPixels", DX12_ZONES_ACTIVE);
    if (!srcRect || !dstRect || !data || !device_ || !device_->CommandList() || srcRect->width <= 0 || srcRect->height <= 0 || dstRect->width <= 0 || dstRect->height <= 0) return;
    RenderTargetBindingDX12 binding{}; if (!PrepareRenderTargets(binding) || !binding.color) return;
    if (srcRect->x < 0 || srcRect->y < 0 || srcRect->x + srcRect->width > binding.width || srcRect->y + srcRect->height > binding.height || dstRect->x < 0 || dstRect->y < 0 || stride < (dstRect->x + dstRect->width) * static_cast<int>(BytesPerPixel(format))) return;
    const bool scene = renderTarget_ == SHADER_RENDERTARGET_BACKBUFFER;
    TextureRecord *record = nullptr; if (!scene) { auto it = FindTexture(renderTarget_); if (it == nullptr) return; record = it; }
    const ImageFormat sourceFormat = scene ? IMAGE_FORMAT_BGRX8888 : record->format;
    ID3D12Resource *source = binding.color;
    D3D12_RESOURCE_STATES sceneState = D3D12_RESOURCE_STATE_RENDER_TARGET, temporaryState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    D3D12_RESOURCE_STATES *state = record ? &record->subresourceStates[record->currentCopy*Faces(*record)*record->mipLevels] : &sceneState;
    Microsoft::WRL::ComPtr<ID3D12Resource> temporary;
    Rect_t readRect = *srcRect;
    if (source->GetDesc().SampleDesc.Count > 1 || srcRect->width != dstRect->width || srcRect->height != dstRect->height) {
        D3D12_RESOURCE_DESC desc = source->GetDesc(); desc.Width = dstRect->width; desc.Height = dstRect->height; desc.DepthOrArraySize = 1; desc.MipLevels = 1; desc.SampleDesc.Count = 1; desc.SampleDesc.Quality = 0; desc.Alignment = 0; desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        const HRESULT hr = device_->NativeDevice()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,temporaryState,nullptr,IID_PPV_ARGS(&temporary)); if (FAILED(hr)) { Warning("ShaderAPIDX12: ReadPixels temporary render texture creation failed (0x%08x), alignment %llu format %u\n",static_cast<unsigned>(hr),static_cast<unsigned long long>(desc.Alignment),static_cast<unsigned>(desc.Format)); return; }
        const Rect_t region{0,0,dstRect->width,dstRect->height};
        if (!BlitTexture(source,*state,scene,sourceFormat,temporary.Get(),temporaryState,false,sourceFormat,*srcRect,region,false,false)) { Warning("ShaderAPIDX12: ReadPixels MSAA resolve or scaling blit failed\n"); return; }
        device_->RetainResource(temporary.Get()); source = temporary.Get(); state = &temporaryState; readRect = region;
    }
    const D3D12_RESOURCE_DESC td = source->GetDesc(); UINT rows = 0; UINT64 bytes = 0; D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{}; device_->NativeDevice()->GetCopyableFootprints(&td,0,1,0,&fp,&rows,nullptr,&bytes);
    D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_READBACK; D3D12_RESOURCE_DESC bd{}; bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = bytes; bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; Microsoft::WRL::ComPtr<ID3D12Resource> readback; if (FAILED(device_->NativeDevice()->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)))) return;
    if (scene && !temporary) device_->TransitionSceneColor(D3D12_RESOURCE_STATE_COPY_SOURCE);
    else if (*state != D3D12_RESOURCE_STATE_COPY_SOURCE) { D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource = source; b.Transition.Subresource = 0; b.Transition.StateBefore = *state; b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE; device_->CommandList()->ResourceBarrier(1,&b); *state = D3D12_RESOURCE_STATE_COPY_SOURCE; }
    D3D12_TEXTURE_COPY_LOCATION from{}, to{}; from.pResource = source; from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; to.pResource = readback.Get(); to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; to.PlacedFootprint = fp; device_->CommandList()->CopyTextureRegion(&to,0,0,0,&from,nullptr);
    if (scene) device_->TransitionSceneColor(D3D12_RESOURCE_STATE_RENDER_TARGET);
    device_->RetainResource(readback.Get()); if (!device_->Submit(true)) return;
    void *mapped = nullptr; D3D12_RANGE range{0,bytes}; if (FAILED(readback->Map(0,&range,&mapped))) return;
    const ImageFormat nativeFormat = DXGI12ToImageFormat(td.Format); if (nativeFormat == IMAGE_FORMAT_UNKNOWN) { Warning("ShaderAPIDX12: ReadPixels unsupported native source format %u\n",static_cast<unsigned>(td.Format)); readback->Unmap(0,nullptr); return; }
    const size_t srcPixelBytes = BytesPerPixel(nativeFormat), dstPixelBytes = BytesPerPixel(format); const auto *base = static_cast<const unsigned char *>(mapped) + fp.Offset;
    for (int row = 0; row < dstRect->height; ++row) {
        const unsigned char *sourceRow = base + static_cast<size_t>(readRect.y + row) * fp.Footprint.RowPitch + static_cast<size_t>(readRect.x) * srcPixelBytes;
        unsigned char *destinationRow = data + static_cast<size_t>(dstRect->y + row) * stride + static_cast<size_t>(dstRect->x) * dstPixelBytes;
        if (format == nativeFormat) std::memcpy(destinationRow,sourceRow,static_cast<size_t>(dstRect->width)*dstPixelBytes);
        else if (!shaderUtil_ || !shaderUtil_->ConvertImageFormat(const_cast<unsigned char *>(sourceRow),nativeFormat,destinationRow,format,dstRect->width,1,static_cast<int>(fp.Footprint.RowPitch),stride)) { readback->Unmap(0,nullptr); return; }
    }
    readback->Unmap(0,nullptr);
}
void CShaderAPIDX12::LockRect(void **outBits, int *pitch, ShaderAPITextureHandle_t handle, int mip, int x, int y, int width, int height, bool write, bool read)
{
    if (outBits) *outBits = nullptr;
    if (pitch) *pitch = 0;
    auto it = FindTexture(handle);
    if (it == nullptr) return;
    TextureRecord &texture = *it;
    if (texture.lockLevel >= 0 || mip < 0 || mip >= texture.mipLevels || x < 0 || y < 0 || width <= 0 || height <= 0 || IsBlockCompressed(texture.format)) return;
    const int fullWidth = std::max(1,texture.width >> mip), fullHeight = std::max(1,texture.height >> mip);
    if (x + width > fullWidth || y + height > fullHeight) return;
    const int index = texture.currentCopy * Faces(texture) * texture.mipLevels + mip;
    if (read) { texture.gpuAuthoritativeSubresources[index] = 1; if (!RefreshTextureStaging(texture,0,mip)) return; }
    else if (texture.gpuAuthoritativeSubresources[index] && !RefreshTextureStaging(texture,0,mip)) return;
    const size_t bytesPerPixel = BytesPerPixel(texture.format), rowBytes = static_cast<size_t>(width) * bytesPerPixel;
    texture.lockData.SetCountNonDestructively(static_cast<int>(rowBytes * height));
    const auto *source = texture.pixels.Base() + SubresourceOffset(texture,0,mip) + (static_cast<size_t>(y)*fullWidth+x)*bytesPerPixel;
    for (int row = 0; row < height; ++row) std::memcpy(texture.lockData.Base()+static_cast<size_t>(row)*rowBytes,source+static_cast<size_t>(row)*fullWidth*bytesPerPixel,rowBytes);
    texture.lockLevel = mip; texture.lockFace = 0; texture.lockX = x; texture.lockY = y; texture.lockWidth = width; texture.lockHeight = height; texture.lockPitch = static_cast<int>(rowBytes); texture.lockWrite = write; texture.lockRead = read;
    if (outBits) *outBits = texture.lockData.Base(); if (pitch) *pitch = texture.lockPitch;
}
void CShaderAPIDX12::UnlockRect(ShaderAPITextureHandle_t h, int) { auto it = FindTexture(h); if (it == nullptr || it->lockLevel < 0) return; modifiedTexture_ = h; if (it->lockWrite) TexSubImage2D(it->lockLevel, it->lockFace, it->lockX, it->lockY, 0, it->lockWidth, it->lockHeight, it->format, it->lockPitch, false, it->lockData.Base()); it->lockLevel = -1; it->lockData.RemoveAll(); }

bool CShaderAPIDX12::BlitTexture(ID3D12Resource *source, D3D12_RESOURCE_STATES &sourceState, bool sourceIsScene, ImageFormat sourceFormat, ID3D12Resource *destination, D3D12_RESOURCE_STATES &destinationState, bool destinationIsScene, ImageFormat destinationFormat, Rect_t sourceRect, Rect_t destinationRect, bool sourceSRGB, bool destinationSRGB, const float *gammaCoefficients)
{
    if (!device_ || !device_->CommandList() || !source || !destination || source == destination || sourceRect.width <= 0 || sourceRect.height <= 0 || destinationRect.width <= 0 || destinationRect.height <= 0) return false;
    auto *native = device_->NativeDevice(); auto *list = device_->CommandList();
    const auto sourceDesc = source->GetDesc(), destinationDesc = destination->GetDesc();
    if (sourceRect.x < 0 || sourceRect.y < 0 || sourceRect.x + sourceRect.width > static_cast<int>(sourceDesc.Width) || sourceRect.y + sourceRect.height > static_cast<int>(sourceDesc.Height) || destinationRect.x < 0 || destinationRect.y < 0 || destinationRect.x + destinationRect.width > static_cast<int>(destinationDesc.Width) || destinationRect.y + destinationRect.height > static_cast<int>(destinationDesc.Height)) return false;
    auto transition = [&](ID3D12Resource *resource, D3D12_RESOURCE_STATES &state, bool scene, D3D12_RESOURCE_STATES desired, bool isSource) {
        if (scene) { device_->TransitionSceneColor(desired); return; }
        if (state == desired) return;
        D3D12_RESOURCE_BARRIER barrier{}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; barrier.Transition.pResource = resource; barrier.Transition.Subresource = 0; barrier.Transition.StateBefore = state; barrier.Transition.StateAfter = desired; list->ResourceBarrier(1, &barrier); state = desired;
    };
    ResolveTextureRecord *resolvedRecord = nullptr;
    D3D12_RESOURCE_STATES *activeSourceState = &sourceState;
    if (sourceDesc.SampleDesc.Count > 1) {
        ZoneNamedN(___tracy_scoped_zone, "DX12 BlitResolve", DX12_ZONES_ACTIVE);
        const D3D12_RESOURCE_DESC desc = NormalizeResolveDesc(sourceDesc);
        HRESULT hr = S_OK;
        resolvedRecord = AcquireResolveTexture(desc, hr);
        if (!resolvedRecord) { Warning("ShaderAPIDX12: MSAA resolve texture creation failed (0x%08x), alignment %llu format %u\n",static_cast<unsigned>(hr),static_cast<unsigned long long>(desc.Alignment),static_cast<unsigned>(desc.Format)); return false; }
        transition(source, sourceState, sourceIsScene, D3D12_RESOURCE_STATE_RESOLVE_SOURCE, true);
        transition(resolvedRecord->resource.Get(), resolvedRecord->state, false, D3D12_RESOURCE_STATE_RESOLVE_DEST, false);
        list->ResolveSubresource(resolvedRecord->resource.Get(), 0, source, 0, SRVFormat(sourceDesc.Format,false,false));
        source = resolvedRecord->resource.Get(); activeSourceState = &resolvedRecord->state; sourceIsScene = false;
    }
    const bool sameSize = sourceRect.width == destinationRect.width && sourceRect.height == destinationRect.height;
    if (!gammaCoefficients && sameSize && source->GetDesc().Format == destinationDesc.Format && destinationDesc.SampleDesc.Count == 1 && sourceSRGB == destinationSRGB) {
        transition(source, *activeSourceState, sourceIsScene, D3D12_RESOURCE_STATE_COPY_SOURCE, true);
        transition(destination, destinationState, destinationIsScene, D3D12_RESOURCE_STATE_COPY_DEST, false);
        D3D12_TEXTURE_COPY_LOCATION src{}, dst{}; src.pResource = source; src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; dst.pResource = destination; dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_BOX box{static_cast<UINT>(sourceRect.x), static_cast<UINT>(sourceRect.y), 0, static_cast<UINT>(sourceRect.x + sourceRect.width), static_cast<UINT>(sourceRect.y + sourceRect.height), 1};
        list->CopyTextureRegion(&dst, destinationRect.x, destinationRect.y, 0, &src, &box);
        transition(destination, destinationState, destinationIsScene, destinationIsScene ? D3D12_RESOURCE_STATE_RENDER_TARGET : D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, false);
        return true;
    }
    const DXGI_FORMAT renderFormat = SRVFormat(destinationDesc.Format, destinationSRGB, false);
    if (renderFormat == DXGI_FORMAT_UNKNOWN || IsBlockCompressed(destinationFormat) || IsBlockCompressed(sourceFormat)) return false;
    Microsoft::WRL::ComPtr<ID3D12Resource> scratch;
    const bool direct = (destinationDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) != 0;
    ID3D12Resource *renderResource = destination;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
    if (!direct) {
        ZoneNamedN(___tracy_scoped_zone, "DX12 BlitScratchAllocation", DX12_ZONES_ACTIVE);
        D3D12_RESOURCE_DESC desc = destinationDesc; desc.Width = destinationRect.width; desc.Height = destinationRect.height; desc.DepthOrArraySize = 1; desc.MipLevels = 1; desc.SampleDesc.Count = 1; desc.SampleDesc.Quality = 0; desc.Alignment = 0; desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        if (FAILED(native->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(&scratch)))) return false;
        renderResource = scratch.Get(); device_->RetainResource(renderResource);
    } else transition(destination, destinationState, destinationIsScene, D3D12_RESOURCE_STATE_RENDER_TARGET, false);
    if (!blitRtvHeap_) {
        D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{}; rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; rtvDesc.NumDescriptors = kBlitRtvSlots;
        if (FAILED(native->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&blitRtvHeap_)))) return false;
        blitRtvSlot_ = 0;
    }
    // Replay reads the RTV later; wrapping the ring first replays every recorded blit that names its slots.
    if (blitRtvSlot_ == kBlitRtvSlots) { device_->DrainRecording(); blitRtvSlot_ = 0; }
    rtv = blitRtvHeap_->GetCPUDescriptorHandleForHeapStart(); rtv.ptr += static_cast<SIZE_T>(blitRtvSlot_++) * native->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV); D3D12_RENDER_TARGET_VIEW_DESC view{}; view.Format = renderFormat; view.ViewDimension = destinationDesc.SampleDesc.Count > 1 && direct ? D3D12_RTV_DIMENSION_TEXTURE2DMS : D3D12_RTV_DIMENSION_TEXTURE2D; native->CreateRenderTargetView(renderResource, &view, rtv);
    if (!blitRoot_) {
        D3D12_DESCRIPTOR_RANGE ranges[2]{}; ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; ranges[0].NumDescriptors = 1; ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER; ranges[1].NumDescriptors = 1;
        D3D12_ROOT_PARAMETER params[3]{}; params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; params[0].DescriptorTable.NumDescriptorRanges = 1; params[0].DescriptorTable.pDescriptorRanges = &ranges[0]; params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; params[1].DescriptorTable.NumDescriptorRanges = 1; params[1].DescriptorTable.pDescriptorRanges = &ranges[1]; params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; params[2].Constants.Num32BitValues = 8; params[2].Constants.ShaderRegister = 0; params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC desc{}; desc.NumParameters = 3; desc.pParameters = params; desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT; Microsoft::WRL::ComPtr<ID3DBlob> blob, error;
        HRESULT hr = D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error);
        if (FAILED(hr)) { Warning("ShaderAPIDX12: native blit root signature serialization failed (0x%08x): %s\n",static_cast<unsigned>(hr),error ? static_cast<const char *>(error->GetBufferPointer()) : "unknown"); return false; }
        hr = native->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&blitRoot_));
        if (FAILED(hr)) { Warning("ShaderAPIDX12: native blit root signature creation failed (0x%08x)\n",static_cast<unsigned>(hr)); return false; }
    }
    const UINT samples = direct ? destinationDesc.SampleDesc.Count : 1, quality = direct ? destinationDesc.SampleDesc.Quality : 0;
    ID3D12PipelineState *pso = nullptr; for (auto &pass : blitPasses_) if (pass.color == renderFormat && pass.samples == samples && pass.quality == quality && pass.gamma == (gammaCoefficients != nullptr)) { pso = pass.pipeline.Get(); break; }
    if (!pso) {
        static const char vsSource[] = "float4 main(uint id:SV_VertexID):SV_Position { float2 p=float2((id<<1)&2,id&2); return float4(p*float2(2,-2)+float2(-1,1),0,1); }";
        static const char psSource[] = "Texture2D image:register(t0); SamplerState sampleState:register(s0); cbuffer Region:register(b0){float4 uv;\n#ifdef PRESENT_GAMMA\nfloat4 gamma;\n#endif\n} float4 main(float4 pos:SV_Position):SV_Target {float4 color=image.SampleLevel(sampleState, pos.xy*uv.xy+uv.zw,0);\n#ifdef PRESENT_GAMMA\ncolor.rgb=pow(saturate(color.rgb),gamma.xxx); color.rgb=pow(saturate(color.rgb),gamma.yyy); color.rgb=saturate(color.rgb*gamma.z+gamma.w);\n#endif\nreturn color;}";
        Microsoft::WRL::ComPtr<ID3DBlob> vs, ps, error; const D3D_SHADER_MACRO defines[] = {{"PRESENT_GAMMA","1"},{nullptr,nullptr}};
        HRESULT hr = D3DCompile(vsSource, sizeof(vsSource)-1, nullptr,nullptr,nullptr,"main","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&vs,&error);
        if (FAILED(hr)) { Warning("ShaderAPIDX12: native blit VS compile failed (0x%08x): %s\n",static_cast<unsigned>(hr),error ? static_cast<const char *>(error->GetBufferPointer()) : "unknown"); return false; }
        error.Reset(); hr = D3DCompile(psSource, sizeof(psSource)-1, nullptr, gammaCoefficients ? defines : nullptr,nullptr,"main","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&ps,&error);
        if (FAILED(hr)) { Warning("ShaderAPIDX12: native blit PS compile failed (0x%08x): %s\n",static_cast<unsigned>(hr),error ? static_cast<const char *>(error->GetBufferPointer()) : "unknown"); return false; }
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{}; desc.pRootSignature = blitRoot_.Get(); desc.VS = {vs->GetBufferPointer(), vs->GetBufferSize()}; desc.PS = {ps->GetBufferPointer(), ps->GetBufferSize()}; desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; desc.NumRenderTargets = 1; desc.RTVFormats[0] = renderFormat; desc.SampleDesc.Count = samples; desc.SampleDesc.Quality = quality; desc.SampleMask = UINT_MAX; desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID; desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; desc.RasterizerState.DepthClipEnable = TRUE; desc.RasterizerState.MultisampleEnable = samples > 1; desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        desc.DepthStencilState.DepthEnable = FALSE; desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO; desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        desc.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP; desc.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP; desc.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP; desc.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS; desc.DepthStencilState.BackFace = desc.DepthStencilState.FrontFace;
        auto &blend = desc.BlendState.RenderTarget[0]; blend.SrcBlend = D3D12_BLEND_ONE; blend.DestBlend = D3D12_BLEND_ZERO; blend.BlendOp = D3D12_BLEND_OP_ADD; blend.SrcBlendAlpha = D3D12_BLEND_ONE; blend.DestBlendAlpha = D3D12_BLEND_ZERO; blend.BlendOpAlpha = D3D12_BLEND_OP_ADD; blend.LogicOp = D3D12_LOGIC_OP_NOOP;
        BlitPassDX12 pass{}; pass.color = renderFormat; pass.samples = samples; pass.quality = quality; pass.gamma = gammaCoefficients != nullptr; hr = native->CreateGraphicsPipelineState(&desc,IID_PPV_ARGS(&pass.pipeline)); if (FAILED(hr)) { Warning("ShaderAPIDX12: native blit PSO failed (0x%08x), format %u samples %u gamma %d\n",static_cast<unsigned>(hr),static_cast<unsigned>(renderFormat),samples,pass.gamma ? 1 : 0); return false; } pso = pass.pipeline.Get(); blitPasses_[blitPasses_.AddToTail()]=std::move(pass);
    }
    DescriptorRangeDX12 srvRange;
    {
        ZoneNamedN(___tracy_scoped_zone, "DX12 BlitDescriptorAllocation", DX12_ZONES_ACTIVE);
        srvRange = pipeline_.AllocateTransientResources(1, device_->NextFenceValue());
        if (srvRange.count != 1 || !pipeline_.LinearClampSampler().ptr) return false;
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{}; srv.Format = SRVFormat(source->GetDesc().Format, sourceSRGB, false); srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; srv.Texture2D.MipLevels = 1; native->CreateShaderResourceView(source, &srv, srvRange.cpu);
    transition(source, *activeSourceState, sourceIsScene, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, true);
    D3D12_VIEWPORT viewport{static_cast<float>(direct ? destinationRect.x : 0),static_cast<float>(direct ? destinationRect.y : 0),static_cast<float>(destinationRect.width),static_cast<float>(destinationRect.height),0,1}; D3D12_RECT scissor{static_cast<LONG>(viewport.TopLeftX),static_cast<LONG>(viewport.TopLeftY),static_cast<LONG>(viewport.TopLeftX + viewport.Width),static_cast<LONG>(viewport.TopLeftY + viewport.Height)};
    const float uv[] = {static_cast<float>(sourceRect.width) / (sourceDesc.Width * destinationRect.width), static_cast<float>(sourceRect.height) / (sourceDesc.Height * destinationRect.height), static_cast<float>(sourceRect.x) / sourceDesc.Width - viewport.TopLeftX * static_cast<float>(sourceRect.width) / (sourceDesc.Width * destinationRect.width), static_cast<float>(sourceRect.y) / sourceDesc.Height - viewport.TopLeftY * static_cast<float>(sourceRect.height) / (sourceDesc.Height * destinationRect.height)};
    ID3D12DescriptorHeap *heaps[] = { pipeline_.ResourceDescriptorHeap(), pipeline_.SamplerDescriptorHeap() }; list->SetDescriptorHeaps(2, heaps); list->RSSetViewports(1,&viewport); list->RSSetScissorRects(1,&scissor); list->OMSetRenderTargets(1,&rtv,FALSE,nullptr); list->SetGraphicsRootSignature(blitRoot_.Get()); list->SetGraphicsRootDescriptorTable(0,srvRange.gpu); list->SetGraphicsRootDescriptorTable(1,pipeline_.LinearClampSampler()); list->SetGraphicsRoot32BitConstants(2,4,uv,0); if (gammaCoefficients) list->SetGraphicsRoot32BitConstants(2,4,gammaCoefficients,4); list->SetPipelineState(pso); list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST); list->DrawInstanced(3,1,0,0);
    pipeline_.InvalidateGraphicsBindings();
    if (scratch) {
        D3D12_RESOURCE_STATES scratchState = D3D12_RESOURCE_STATE_RENDER_TARGET; transition(scratch.Get(), scratchState, false, D3D12_RESOURCE_STATE_COPY_SOURCE, true); transition(destination, destinationState, destinationIsScene, D3D12_RESOURCE_STATE_COPY_DEST, false);
        D3D12_TEXTURE_COPY_LOCATION src{}, dst{}; src.pResource = scratch.Get(); src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; dst.pResource = destination; dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; list->CopyTextureRegion(&dst,destinationRect.x,destinationRect.y,0,&src,nullptr);
        transition(destination,destinationState,destinationIsScene,destinationIsScene ? D3D12_RESOURCE_STATE_RENDER_TARGET : D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,false);
    }
    return true;
}
bool CShaderAPIDX12::PresentGamma(ID3D12Resource *back, float gamma, float tvMin, float tvMax, float tvExponent, bool tvEnabled)
{
    if (!device_ || !device_->CommandList() || !device_->SceneColor() || !back || !std::isfinite(gamma) || !std::isfinite(tvMin) || !std::isfinite(tvMax) || (tvEnabled && (!std::isfinite(tvExponent) || tvExponent <= 0.f))) return false;
    const auto sceneDesc = device_->SceneColor()->GetDesc(), backDesc = back->GetDesc();
    if (sceneDesc.Width != backDesc.Width || sceneDesc.Height != backDesc.Height || backDesc.SampleDesc.Count != 1 || backDesc.Format != device_->SceneColorFormat()) return false;
    const float correction[4] = {gamma / 2.2f, tvEnabled ? 2.2f / tvExponent : 1.f, tvEnabled ? (tvMax-tvMin)/255.f : 1.f, tvEnabled ? tvMin/255.f : 0.f};
    D3D12_RESOURCE_STATES sceneState = D3D12_RESOURCE_STATE_RENDER_TARGET, backState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    Rect_t area{0,0,static_cast<int>(backDesc.Width),static_cast<int>(backDesc.Height)};
    const bool result = BlitTexture(device_->SceneColor(),sceneState,true,IMAGE_FORMAT_BGRA8888,back,backState,false,IMAGE_FORMAT_BGRA8888,area,area,false,false,correction);
    device_->TransitionSceneColor(D3D12_RESOURCE_STATE_RENDER_TARGET);
    return result;
}

void CShaderAPIDX12::CopyTextureRegionDX12(ShaderAPITextureHandle_t from, ShaderAPITextureHandle_t to, Rect_t *sourceRect, Rect_t *destinationRect)
{
    if (!device_ || !device_->CommandList() || from == to) return;
    ID3D12Resource *srcResource = nullptr, *dstResource = nullptr;
    TextureRecord *srcRecord = nullptr, *dstRecord = nullptr;
    const bool sceneSource = from == SHADER_RENDERTARGET_BACKBUFFER, sceneDestination = to == SHADER_RENDERTARGET_BACKBUFFER;
    if (sceneSource) srcResource = device_->SceneColor(); else { auto it = FindTexture(from); if (it == nullptr) return; srcRecord = it; if (!EnsureTextureResident(*srcRecord)) return; srcResource = srcRecord->resource.Get(); }
    if (sceneDestination) dstResource = device_->SceneColor(); else { auto it = FindTexture(to); if (it == nullptr) return; dstRecord = it; if (!EnsureTextureResident(*dstRecord)) return; dstResource = dstRecord->resource.Get(); }
    if (!srcResource || !dstResource || srcResource == dstResource) return;
    const auto srcDesc = srcResource->GetDesc(), dstDesc = dstResource->GetDesc();
    if (srcDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || dstDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || srcDesc.DepthOrArraySize != 1 || dstDesc.DepthOrArraySize != 1 || (srcRecord && (srcRecord->flags & TEXTURE_CREATE_DEPTHBUFFER)) || (dstRecord && (dstRecord->flags & TEXTURE_CREATE_DEPTHBUFFER))) return;
    if (srcRecord && srcRecord->gpuDirty && std::find(renderTargets_.begin(),renderTargets_.end(),from) == renderTargets_.end()) {
        const int subCount = Faces(*srcRecord) * srcRecord->mipLevels;
        const int active = srcRecord->currentCopy, base = active * subCount;
        for (int sub = 0; sub < subCount; ++sub) if (srcRecord->dirtySubresources[base+sub]) {
            const int mip = sub % srcRecord->mipLevels, face = sub / srcRecord->mipLevels;
            const size_t bytes = MipBytes(std::max(1,srcRecord->width >> mip),std::max(1,srcRecord->height >> mip),1,srcRecord->format);
            if (!CreateUpload(device_->NativeDevice(),device_->CommandList(),srcResource,srcRecord->pixels.Base()+SubresourceOffset(*srcRecord,face,mip),bytes,sub,srcRecord->subresourceStates[base+sub],device_)) return;
            srcRecord->dirtySubresources[base+sub] = 0;
        }
        srcRecord->gpuDirty = std::any_of(srcRecord->dirtySubresources.begin(),srcRecord->dirtySubresources.end(),[](unsigned char dirty){return dirty != 0;});
    }
    Rect_t s = sourceRect ? *sourceRect : Rect_t{0,0,static_cast<int>(srcDesc.Width),static_cast<int>(srcDesc.Height)};
    Rect_t d = destinationRect ? *destinationRect : Rect_t{0,0,static_cast<int>(dstDesc.Width),static_cast<int>(dstDesc.Height)};
    D3D12_RESOURCE_STATES sceneSourceState = D3D12_RESOURCE_STATE_RENDER_TARGET, sceneDestinationState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    D3D12_RESOURCE_STATES &fromState = srcRecord ? srcRecord->subresourceStates[srcRecord->currentCopy*Faces(*srcRecord)*srcRecord->mipLevels] : sceneSourceState;
    D3D12_RESOURCE_STATES &toState = dstRecord ? dstRecord->subresourceStates[dstRecord->currentCopy*Faces(*dstRecord)*dstRecord->mipLevels] : sceneDestinationState;
    const ImageFormat fromFormat = srcRecord ? srcRecord->format : IMAGE_FORMAT_BGRX8888, toFormat = dstRecord ? dstRecord->format : IMAGE_FORMAT_BGRX8888;
    if (BlitTexture(srcResource,fromState,sceneSource,fromFormat,dstResource,toState,sceneDestination,toFormat,s,d,(srcRecord && (srcRecord->flags & TEXTURE_CREATE_SRGB) != 0),(dstRecord && (dstRecord->flags & TEXTURE_CREATE_SRGB) != 0))) {
        if (dstRecord) { const int sub = dstRecord->currentCopy*Faces(*dstRecord)*dstRecord->mipLevels; dstRecord->dirtySubresources[sub] = 0; dstRecord->initializedSubresources[sub] = 0; dstRecord->gpuAuthoritativeSubresources[sub] = 1; dstRecord->gpuDirty = std::any_of(dstRecord->dirtySubresources.begin(),dstRecord->dirtySubresources.end(),[](unsigned char dirty){return dirty != 0;}); }
    }
}
void CShaderAPIDX12::CopyRenderTargetToTextureEx(ShaderAPITextureHandle_t destination, int targetID, Rect_t *sourceRect, Rect_t *destinationRect) { if (targetID >= 0 && targetID < static_cast<int>(renderTargets_.size())) CopyTextureRegionDX12(renderTargets_[targetID],destination,sourceRect,destinationRect); }
void CShaderAPIDX12::CopyTextureToRenderTargetEx(int targetID, ShaderAPITextureHandle_t source, Rect_t *sourceRect, Rect_t *destinationRect) { if (targetID >= 0 && targetID < static_cast<int>(renderTargets_.size())) CopyTextureRegionDX12(source,renderTargets_[targetID],sourceRect,destinationRect); }
void CShaderAPIDX12::CopyRenderTargetToScratchTexture(ShaderAPITextureHandle_t source, ShaderAPITextureHandle_t destination, Rect_t *sourceRect, Rect_t *destinationRect) { CopyTextureRegionDX12(source,destination,sourceRect,destinationRect); }
void CShaderAPIDX12::CopyTextureToTexture(ShaderAPITextureHandle_t source, ShaderAPITextureHandle_t destination)
{
    if (source == destination || !device_ || !device_->CommandList()) return;
    auto from = FindTexture(source), to = FindTexture(destination);
    if (from == nullptr || to == nullptr) { CopyTextureRegionDX12(source,destination,nullptr,nullptr); return; }
    TextureRecord &src = *from, &dst = *to;
    if (!EnsureTextureResident(src) || !EnsureTextureResident(dst)) return;
    const D3D12_RESOURCE_DESC a = src.resource->GetDesc(), b = dst.resource->GetDesc();
    if (a.Dimension != b.Dimension || a.Format != b.Format || a.Width != b.Width || a.Height != b.Height || a.DepthOrArraySize != b.DepthOrArraySize || a.MipLevels != b.MipLevels || a.SampleDesc.Count != b.SampleDesc.Count || src.copies != dst.copies) { CopyTextureRegionDX12(source,destination,nullptr,nullptr); return; }
    const int count = Faces(src) * src.mipLevels;
    if (source != depthTarget_ && std::find(renderTargets_.begin(),renderTargets_.end(),source) == renderTargets_.end()) {
        for (int copy = 0; copy < src.copies; ++copy) for (int subresource = 0; subresource < count; ++subresource) {
            const int stateIndex = copy*count+subresource;
            if (!src.dirtySubresources[stateIndex]) continue;
            const int mip = subresource % src.mipLevels, face = subresource / src.mipLevels;
            const size_t bytes = MipBytes(std::max(1,src.width >> mip),std::max(1,src.height >> mip),Faces(src) == 1 ? std::max(1,src.depth >> mip) : 1,src.format);
            if (!CreateUpload(device_->NativeDevice(),device_->CommandList(),src.resources[copy].resource.Get(),src.pixels.Base()+SubresourceOffset(src,face,mip,copy),bytes,subresource,src.subresourceStates[stateIndex],device_)) return;
            src.dirtySubresources[stateIndex] = 0;
        }
    }
    auto transition = [&](TextureRecord &texture, int copy, int subresource, D3D12_RESOURCE_STATES next) {
        D3D12_RESOURCE_STATES &state = texture.subresourceStates[copy*count+subresource];
        if (state == next) return;
        texture.sampledStateValid = false;++textureStateEpoch_;
        D3D12_RESOURCE_BARRIER barrier{}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; barrier.Transition.pResource = texture.resources[copy].resource.Get(); barrier.Transition.Subresource = subresource; barrier.Transition.StateBefore = state; barrier.Transition.StateAfter = next; device_->CommandList()->ResourceBarrier(1,&barrier); state = next;
    };
    for (int copy = 0; copy < src.copies; ++copy) {
        for (int subresource = 0; subresource < count; ++subresource) { transition(src,copy,subresource,D3D12_RESOURCE_STATE_COPY_SOURCE); transition(dst,copy,subresource,D3D12_RESOURCE_STATE_COPY_DEST); }
        device_->CommandList()->CopyResource(dst.resources[copy].resource.Get(),src.resources[copy].resource.Get());
        for (int subresource = 0; subresource < count; ++subresource) transition(dst,copy,subresource,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    dst.gpuDirty = false;
    std::fill(dst.dirtySubresources.begin(),dst.dirtySubresources.end(),0);
    std::fill(dst.initializedSubresources.begin(),dst.initializedSubresources.end(),0);
    std::fill(dst.gpuAuthoritativeSubresources.begin(),dst.gpuAuthoritativeSubresources.end(),1);
}
void CShaderAPIDX12::EnableDebugTextureList(bool enable) { debugList_ = enable; if (!enable && debugTextureEntries_) { debugTextureEntries_->deleteThis(); debugTextureEntries_ = nullptr; } }
void CShaderAPIDX12::EnableGetAllTextures(bool enable) { debugAll_ = enable; }
KeyValues *CShaderAPIDX12::GetDebugTextureList() { if (!debugList_) return nullptr; if (debugTextureEntries_) debugTextureEntries_->deleteThis(); debugTextureEntries_ = new KeyValues("Textures"); FOR_EACH_HASHTABLE(textures_, entry) { const auto &t = *textures_[entry]; if (!debugAll_ && !t.binds) continue; KeyValues *kv = debugTextureEntries_->CreateNewKey(); kv->SetString("Name", t.name.c_str()); kv->SetInt("Binds", t.binds); kv->SetInt("Format", t.format); kv->SetInt("Width", t.width); kv->SetInt("Height", t.height); } debugListFrame_ = frameCounter_; return debugTextureEntries_; }
int CShaderAPIDX12::GetTextureMemoryUsed(TextureMemoryType kind) { size_t total = 0; FOR_EACH_HASHTABLE(textures_, entry) { const auto &t = *textures_[entry]; if (kind == MEMORY_BOUND_LAST_FRAME && !t.binds) continue; if (kind == MEMORY_ESTIMATE_PICMIP_1 || kind == MEMORY_ESTIMATE_PICMIP_2) { const int mip = kind == MEMORY_ESTIMATE_PICMIP_1 ? 1 : 2; for (int i = std::min(mip, t.mipLevels - 1); i < t.mipLevels; ++i) total += MipBytes(std::max(1, t.width >> i), std::max(1, t.height >> i), t.depth, t.format) * Faces(t) * t.copies; } else for (const auto &copy : t.resources) if (copy.resource) { const D3D12_RESOURCE_DESC desc = copy.resource->GetDesc(); total += static_cast<size_t>(device_->NativeDevice()->GetResourceAllocationInfo(0, 1, &desc).SizeInBytes); } } return static_cast<int>(std::min<size_t>(total, INT_MAX)); }
bool CShaderAPIDX12::IsDebugTextureListFresh(int frames) { return debugList_ && debugTextureEntries_ && frames >= 0 && frameCounter_ - debugListFrame_ <= static_cast<uint64_t>(frames); }
bool CShaderAPIDX12::SetDebugTextureRendering(bool enable) { const bool old = debugRender_; debugRender_ = enable; return old; }
}
