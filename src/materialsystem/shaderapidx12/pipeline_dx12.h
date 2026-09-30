#pragma once
#include "resources_dx12.h"
#include "bindings_dx12.h"
#include "command_recorder_dx12.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>
#include <array>
#include <cstddef>
#include <cstring>
#include "tier1/utlhashtable.h"
#include "tier1/utlvector.h"
namespace shaderapidx12
{
struct PipelineKeyDX12
{
    uint64_t vs=0,ps=0,gs=0,input=0,vsVariant=0,psVariant=0,gsVariant=0;DXGI_FORMAT color=DXGI_FORMAT_B8G8R8A8_UNORM,depth=DXGI_FORMAT_D24_UNORM_S8_UINT;uint32_t samples=1,topology=3,blend=0,depthState=0,raster=0;
    std::array<DXGI_FORMAT,4> colorFormats{};uint32_t colorCount=1,sampleQuality=0;
    bool frontCounterClockwise=false,wireframe=false,scissor=false,depthBias=false;
    int depthBiasValue=0;
    float slopeScaledDepthBias=0.f;
    uint32_t blendSource=1,blendDestination=0,blendAlphaSource=1,blendAlphaDestination=0,blendOperation=0,blendAlphaOperation=0,depthFunction=3;
    uint32_t stencilFunction=8,stencilFail=1,stencilDepthFail=1,stencilPass=1;
    uint8_t stencilReadMask=0xff,stencilWriteMask=0xff;
    bool separateAlpha=false,depthTest=true,depthWrite=true,culling=true,colorWrites=true,alphaWrites=true,alphaToCoverage=false,stencil=false;
    bool operator==(const PipelineKeyDX12 &o)const{return vs==o.vs&&ps==o.ps&&gs==o.gs&&vsVariant==o.vsVariant&&psVariant==o.psVariant&&gsVariant==o.gsVariant&&input==o.input&&color==o.color&&colorFormats==o.colorFormats&&colorCount==o.colorCount&&sampleQuality==o.sampleQuality&&depth==o.depth&&samples==o.samples&&topology==o.topology&&blend==o.blend&&depthState==o.depthState&&raster==o.raster&&blendSource==o.blendSource&&blendDestination==o.blendDestination&&blendAlphaSource==o.blendAlphaSource&&blendAlphaDestination==o.blendAlphaDestination&&blendOperation==o.blendOperation&&blendAlphaOperation==o.blendAlphaOperation&&separateAlpha==o.separateAlpha&&depthFunction==o.depthFunction&&stencilFunction==o.stencilFunction&&stencilFail==o.stencilFail&&stencilDepthFail==o.stencilDepthFail&&stencilPass==o.stencilPass&&stencilReadMask==o.stencilReadMask&&stencilWriteMask==o.stencilWriteMask&&depthTest==o.depthTest&&depthWrite==o.depthWrite&&culling==o.culling&&colorWrites==o.colorWrites&&alphaWrites==o.alphaWrites&&alphaToCoverage==o.alphaToCoverage&&stencil==o.stencil&&frontCounterClockwise==o.frontCounterClockwise&&wireframe==o.wireframe&&scissor==o.scissor&&depthBias==o.depthBias&&depthBiasValue==o.depthBiasValue&&slopeScaledDepthBias==o.slopeScaledDepthBias;}
};
// Owned by the recording thread, like the command list it fills: every entry point runs on the current
// recording owner (draws, uploads, reclamation, shader retirement), so the caches take no locks.
class CPipelineCacheDX12
{
public:
    bool Initialize(ID3D12Device *device);void Shutdown();
    void BindDrawState(CCommandRecorderDX12 *list,const D3D12_VIEWPORT &viewport,const D3D12_RECT &scissor,uint64_t retireFence);
    D3D12_CPU_DESCRIPTOR_HANDLE NullShaderResourceView() const { return nullSrv_; }
    void Reclaim(uint64_t completedFence);
    bool UploadTransient(const void *data,size_t bytes,size_t allocationBytes,size_t alignment,uint64_t retireFence,D3D12_GPU_VIRTUAL_ADDRESS &gpuAddress,const uint32_t *swapOffsets=nullptr,size_t swapCount=0,size_t vertexStride=0);
    bool EnsureGeometryBuffer(CCommandRecorderDX12 *list,CVertexBufferDX12 &buffer,size_t usedBytes,uint64_t retireFence,D3D12_GPU_VIRTUAL_ADDRESS &gpuAddress,const uint32_t *swapOffsets=nullptr,size_t swapCount=0,size_t vertexStride=0);
    bool EnsureIndexBuffer(CCommandRecorderDX12 *list,CIndexBufferDX12 &buffer,size_t usedBytes,uint64_t retireFence,D3D12_GPU_VIRTUAL_ADDRESS &gpuAddress);
    struct BindingInputDX12 {
        explicit BindingInputDX12(D3D12_CPU_DESCRIPTOR_HANDLE nullView) { srvSources.fill(nullView); }
        std::array<const void *,10> constantData{}; std::array<size_t,10> constantSizes{}; std::array<uint64_t,10> constantVersions{};
        std::array<uint64_t,6> constantShaderIds{}; std::array<uint32_t,6> consumedRegisters{};
        // Native space-1 CBVs: slots 0-7 VS b0-b7, 8-15 PS b0-b7; only stages flagged in nativeStage are bound.
        std::array<const void *,16> nativeData{}; std::array<uint32_t,16> nativeSizes{}; std::array<uint64_t,16> nativeVersions{};
        std::array<bool,2> nativeStage{};
        std::array<ID3D12Resource *,32> textures{}; std::array<D3D12_CPU_DESCRIPTOR_HANDLE,32> srvSources;
        // Descriptions are only read for slots whose source handle is zero; those slots must supply a description.
        std::array<D3D12_SHADER_RESOURCE_VIEW_DESC,32> srvDescs;
        std::array<D3D12_SAMPLER_DESC,32> samplerDescs{}; std::array<uint16_t,32> samplerIds{}; DescriptorRangeDX12 samplerTable{}; uint64_t retireFence=0;
        // Whether the draw samples vertex textures / runs a geometry stage; unused root parameters may stay stale.
        bool vertexTextures=true,geometryStage=true;
        // Caller guarantees textures/srvSources equal the previous PrepareBindings call's input in this recording.
        bool texturesUnchanged=false;
    };
    bool PrepareBindings(CCommandRecorderDX12 *list,const BindingInputDX12 &input);
    struct StatsDX12 { uint64_t srvTableHits=0,srvTableCopies=0,constantHits=0,constantUploads=0,transientConstants=0,rootCbvSets=0,rootTableSets=0; };
    const StatsDX12 &Stats() const { return stats_; }
    // Changes whenever a cached PSO may have been released.
    uint64_t PipelineEpoch() const { return pipelineEpoch_; }
    // Uploads dynamic buffer contents once per (content version, recording fence).
    template<class Buffer> bool UploadDynamic(Buffer &buffer,size_t bytes,size_t alignment,uint64_t retireFence,D3D12_GPU_VIRTUAL_ADDRESS &gpuAddress,const uint32_t *swapOffsets=nullptr,size_t swapCount=0,size_t vertexStride=0){
        auto &last=buffer.TransientUpload();
        if(last.fence==retireFence&&last.version==buffer.ContentVersion()&&last.epoch==retainEpoch_&&last.bytes==bytes){gpuAddress=last.address;return true;}
        if(!UploadTransient(buffer.Data().data(),bytes,bytes,alignment,retireFence,gpuAddress,swapOffsets,swapCount,vertexStride))return false;
        last={buffer.ContentVersion(),retireFence,retainEpoch_,bytes,gpuAddress};return true;
    }
    void ResetStats() { stats_={}; }
    // Clear/blit passes that bind their own native graphics state must invalidate this draw state.
    void InvalidateGraphicsBindings() { graphicsBindingsValid_=false; drawStateValid_=false; boundPipeline_=nullptr; boundTargetsValid_=false; iaValid_=false; }
    // Input-assembler state filtered per recording fence. Slots above count keep earlier bindings, as before.
    void BindInputAssembler(CCommandRecorderDX12 *list,const D3D12_VERTEX_BUFFER_VIEW *views,UINT count,const D3D12_VERTEX_BUFFER_VIEW *zero,D3D12_PRIMITIVE_TOPOLOGY topology,uint64_t retireFence) {
        const bool reset=!iaValid_||iaFence_!=retireFence;
        if(reset||boundVertexCount_!=count||std::memcmp(boundVertexViews_.data(),views,sizeof(D3D12_VERTEX_BUFFER_VIEW)*count)){
            if(count)list->IASetVertexBuffers(0,count,views);
            std::memcpy(boundVertexViews_.data(),views,sizeof(D3D12_VERTEX_BUFFER_VIEW)*count);boundVertexCount_=count;
        }
        if(zero&&(reset||!boundZeroValid_||std::memcmp(&boundZeroView_,zero,sizeof(*zero)))){list->IASetVertexBuffers(16,1,zero);boundZeroView_=*zero;boundZeroValid_=true;}
        if(reset)boundZeroValid_=zero!=nullptr;
        if(reset||boundTopology_!=topology){list->IASetPrimitiveTopology(topology);boundTopology_=topology;}
        if(reset){boundIndexValid_=false;}
        iaFence_=retireFence;iaValid_=true;
    }
    void BindIndexBuffer(CCommandRecorderDX12 *list,const D3D12_INDEX_BUFFER_VIEW *view,uint64_t retireFence) {
        // Field compare: the view was just built from separate stores, and a whole-struct reload would stall on them.
        const D3D12_INDEX_BUFFER_VIEW value=view?*view:D3D12_INDEX_BUFFER_VIEW{};
        if(!iaValid_||iaFence_!=retireFence||!boundIndexValid_||boundIndexView_.BufferLocation!=value.BufferLocation||boundIndexView_.SizeInBytes!=value.SizeInBytes||boundIndexView_.Format!=value.Format){list->IASetIndexBuffer(view);boundIndexView_=value;boundIndexValid_=true;}
    }
    // Skips OMSetRenderTargets when the same views and resources are already bound in this recording.
    void BindRenderTargets(CCommandRecorderDX12 *list,UINT count,const D3D12_CPU_DESCRIPTOR_HANDLE *rtvs,ID3D12Resource *const *colors,const D3D12_CPU_DESCRIPTOR_HANDLE *dsv,ID3D12Resource *depth,uint64_t retireFence) {
        bool same=boundTargetsValid_&&boundTargetsFence_==retireFence&&boundTargetCount_==count&&boundDepth_==depth&&boundDsv_.ptr==(dsv?dsv->ptr:0);
        for(UINT i=0;same&&i<count;++i)same=boundRtvs_[i].ptr==rtvs[i].ptr&&boundColors_[i]==colors[i];
        if(same)return;
        list->OMSetRenderTargets(count,count?rtvs:nullptr,FALSE,dsv);
        boundTargetCount_=count;for(UINT i=0;i<count;++i){boundRtvs_[i]=rtvs[i];boundColors_[i]=colors[i];}
        boundDsv_.ptr=dsv?dsv->ptr:0;boundDepth_=depth;boundTargetsFence_=retireFence;boundTargetsValid_=true;
    }
    // Shader-visible transient descriptors retired with the recording fence, plus a persistent linear-clamp sampler.
    DescriptorRangeDX12 AllocateTransientResources(uint32_t count,uint64_t retireFence){return bindings_.AllocateDescriptors(count,retireFence);}
    D3D12_GPU_DESCRIPTOR_HANDLE LinearClampSampler() const { return linearClampSampler_; }
    ID3D12DescriptorHeap *ResourceDescriptorHeap(){return bindings_.ResourceHeap().Heap();}
    ID3D12DescriptorHeap *SamplerDescriptorHeap(){return bindings_.SamplerHeap().Heap();}
    void BindPipelineState(CCommandRecorderDX12 *list,ID3D12PipelineState *pipeline,UINT stencilReference,uint64_t retireFence) {
        const bool reset=!boundPipeline_||boundPipelineFence_!=retireFence;
        if(reset||boundPipeline_!=pipeline)list->SetPipelineState(pipeline);
        if(reset||boundStencilReference_!=stencilReference)list->OMSetStencilRef(stencilReference);
        boundPipeline_=pipeline;boundStencilReference_=stencilReference;boundPipelineFence_=retireFence;
    }
    // Bind the returned table in the same recording batch; a submission requires a new reservation.
    static D3D12_SAMPLER_DESC DefaultSamplerDesc(){return CBindingCacheDX12::DefaultSampler();}
    // Unset descriptions (AddressU==0) canonicalize to the default sampler, id 0.
    uint16_t InternSampler(const D3D12_SAMPLER_DESC &desc){return desc.AddressU==0?0:bindings_.InternSampler(desc);}
    DescriptorRangeDX12 PrepareSamplerTable(const std::array<D3D12_SAMPLER_DESC,32> &samplers,const std::array<uint16_t,32> &ids,uint64_t retireFence);
    D3D12_CPU_DESCRIPTOR_HANDLE AcquireResourceDescriptor(uint64_t lastUseFence);
    void ReleaseResourceDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE descriptor,uint64_t retireFence);
    void NotifyShaderDestroyed(uint64_t shaderIdentity);
    ID3D12PipelineState *GetOrCreate(const PipelineKeyDX12 &key,const D3D12_SHADER_BYTECODE &vs,const D3D12_SHADER_BYTECODE &ps,const D3D12_SHADER_BYTECODE &gs,const D3D12_INPUT_LAYOUT_DESC &layout,uint64_t retireFence);
private:
    struct Entry{PipelineKeyDX12 key;Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;uint64_t lastUseFence=0;bool destroyed=false;};
    struct UploadPage{Microsoft::WRL::ComPtr<ID3D12Resource> resource;unsigned char *mapped=nullptr;D3D12_GPU_VIRTUAL_ADDRESS gpu=0;size_t capacity=0,used=0;uint64_t fence=0;};
    struct RetiredResource{Microsoft::WRL::ComPtr<ID3D12Resource> resource;uint64_t fence=0;};
    // Largest legal cbuffer; also the read slack kept past every upload page used for root CBVs.
    static constexpr size_t kConstantBufferMaxBytes=65536;
    // 0-3 SRV/sampler tables, 4-7 VS b0-b3, 8-13 PS b0-b5, 14-17 GS b0-b3 root CBVs (space 0); 18/19 VS/PS space-1 CBV tables.
    static constexpr UINT kRootVertexConstants=4,kRootPixelConstants=8,kRootGeometryConstants=14,kRootNativeVertex=18,kRootNativePixel=19,kRootParameterCount=20;
    bool AllocateUploadLocked(const void *data,size_t bytes,size_t allocationBytes,size_t alignment,uint64_t retireFence,D3D12_GPU_VIRTUAL_ADDRESS &gpuAddress,ID3D12Resource **source,size_t *sourceOffset,const uint32_t *swapOffsets,size_t swapCount,size_t vertexStride);
    void RetainGeometryLocked(ID3D12Resource *resource, uint64_t retireFence);
    CBindingCacheDX12 bindings_;CUtlVector<UploadPage> uploadPages_;int uploadPageHint_=0;
    CUtlHashtable<ID3D12Resource *,RetiredResource> geometryInFlight_;
    struct RetiredDescriptor { D3D12_CPU_DESCRIPTOR_HANDLE descriptor; uint64_t fence; };
    CUtlVector<RetiredDescriptor> freeResourceDescriptors_; // FIFO, fence order
    struct CachedSrvTable {
        std::array<D3D12_CPU_DESCRIPTOR_HANDLE,32> sources{};
        std::array<ID3D12Resource *,32> resources{};
        D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
        uint64_t fence=0,heapGeneration=0,descriptorEpoch=0;
    };
    std::array<CachedSrvTable,64> srvTables_{};
    uint64_t srvDescriptorEpoch_=1;
    CachedSrvTable lastSrvTable_{};
    struct LastConstantSlot { uint64_t fence=0,version=0,shaderId=0; size_t bytes=0; D3D12_GPU_VIRTUAL_ADDRESS address=0; };
    std::array<LastConstantSlot,10> lastConstantSlots_{};
    static constexpr size_t kRecentConstants=64;
    std::array<std::array<LastConstantSlot,kRecentConstants>,10> recentConstants_{};
    std::array<D3D12_GPU_DESCRIPTOR_HANDLE,4> boundRootTables_{};
    std::array<D3D12_GPU_DESCRIPTOR_HANDLE,2> boundNativeTables_{};
    std::array<LastConstantSlot,16> lastNativeSlots_{};
    struct NativeTableCache { std::array<D3D12_GPU_VIRTUAL_ADDRESS,8> addresses{}; std::array<UINT,8> sizes{}; D3D12_GPU_DESCRIPTOR_HANDLE gpu{}; uint64_t fence=0,heapGeneration=0; };
    std::array<NativeTableCache,2> nativeTableCache_{};
    std::array<D3D12_GPU_VIRTUAL_ADDRESS,kRootNativeVertex-kRootVertexConstants> boundRootConstants_{};
    ID3D12DescriptorHeap *boundResourceHeap_=nullptr,*boundSamplerHeap_=nullptr;
    uint64_t graphicsBindingsFence_=0;
    bool graphicsBindingsValid_=false;
    D3D12_VIEWPORT boundViewport_{};
    D3D12_RECT boundScissor_{};
    uint64_t drawStateFence_=0;
    bool drawStateValid_=false;
    ID3D12PipelineState *boundPipeline_=nullptr;
    uint64_t boundPipelineFence_=0;
    UINT boundStencilReference_=0;
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE,8> boundRtvs_{};
    std::array<ID3D12Resource *,8> boundColors_{};
    D3D12_CPU_DESCRIPTOR_HANDLE boundDsv_{};
    ID3D12Resource *boundDepth_=nullptr;
    UINT boundTargetCount_=0;
    uint64_t boundTargetsFence_=0;
    bool boundTargetsValid_=false;
    bool vertexTablesCurrent_=false,geometryConstantsCurrent_=false;
    D3D12_GPU_DESCRIPTOR_HANDLE linearClampSampler_{};
    StatsDX12 stats_{};
    uint64_t pipelineEpoch_=1;
    std::array<D3D12_VERTEX_BUFFER_VIEW,16> boundVertexViews_{};
    D3D12_VERTEX_BUFFER_VIEW boundZeroView_{};
    D3D12_INDEX_BUFFER_VIEW boundIndexView_{};
    D3D12_PRIMITIVE_TOPOLOGY boundTopology_=D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
    UINT boundVertexCount_=0;
    uint64_t iaFence_=0;
    bool iaValid_=false,boundZeroValid_=false,boundIndexValid_=false;
    uint64_t lastReclaimedFence_=0;
    // Invalidates per-buffer retention marks when geometryInFlight_ is purged.
    uint64_t retainEpoch_=1;
    bool reclaimDirty_=true;
    ID3D12Device *device_=nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE nullSrv_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> zeroConstants_;
    D3D12_GPU_VIRTUAL_ADDRESS zeroConstantAddress_=0;
    D3D12_SHADER_RESOURCE_VIEW_DESC nullSrvDesc_{};
    SIZE_T resourceStride_=0,samplerStride_=0;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_;
    CUtlVector<Entry> entries_;
    int lastPipelineIndex_=-1;
    // Index plus one; a collision only falls back to the complete-key search.
    std::array<uint32_t,256> pipelineHints_{};
    // lastSrvTable_ was produced or matched by the most recent PrepareBindings call.
    bool lastSrvTableIsPrevious_=false;
};
} // namespace shaderapidx12
