#include "pipeline_dx12.h"
#include "shaderapi/ishadershadow.h"
#include "tier0/dbg.h"
#include <cstring>
#include <algorithm>
#include <utility>
#include "tracy_dx12.h"
namespace shaderapidx12
{
bool CPipelineCacheDX12::Initialize(ID3D12Device *device)
{
    Shutdown();if(!device)return false;device_=device;resourceStride_=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);samplerStride_=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);if(!bindings_.Initialize(device))return false;
    ++srvDescriptorEpoch_;
    InvalidateGraphicsBindings();
    // Fence values restart with a new device; drop every fence-keyed reuse record.
    lastSrvTable_={};lastConstantSlots_={};srvTables_={};for(auto &bank:recentConstants_)bank.fill({});uploadPageHint_=0;
    nullSrvDesc_={};nullSrvDesc_.Format=DXGI_FORMAT_R8G8B8A8_UNORM;nullSrvDesc_.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;nullSrvDesc_.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;nullSrvDesc_.Texture2D.MipLevels=1;
    const auto nullViews=bindings_.AllocatePersistentResource(1,0);if(nullViews.count!=1)return false;
    nullSrv_=nullViews.cpu;device_->CreateShaderResourceView(nullptr,&nullSrvDesc_,nullSrv_);
    {
        const auto sampler=bindings_.AllocatePersistentSampler(1,0);if(sampler.count!=1)return false;
        D3D12_SAMPLER_DESC linear{};linear.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR;linear.AddressU=linear.AddressV=linear.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;linear.MaxAnisotropy=1;linear.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;linear.MaxLOD=D3D12_FLOAT32_MAX;
        device_->CreateSampler(&linear,sampler.cpu);linearClampSampler_=sampler.gpu;
    }
    {
        // Empty constant banks bind this zero buffer; it spans the largest possible cbuffer.
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_UPLOAD;D3D12_RESOURCE_DESC zeroDesc{};zeroDesc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;zeroDesc.Width=kConstantBufferMaxBytes;zeroDesc.Height=1;zeroDesc.DepthOrArraySize=1;zeroDesc.MipLevels=1;zeroDesc.SampleDesc.Count=1;zeroDesc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if(FAILED(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&zeroDesc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&zeroConstants_))))return false;
        void *mapped=nullptr;D3D12_RANGE read{};if(FAILED(zeroConstants_->Map(0,&read,&mapped)))return false;std::memset(mapped,0,kConstantBufferMaxBytes);zeroConstants_->Unmap(0,nullptr);
        zeroConstantAddress_=zeroConstants_->GetGPUVirtualAddress();
    }
    D3D12_DESCRIPTOR_RANGE ranges[2]{};ranges[0].RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;ranges[0].NumDescriptors=16;ranges[0].BaseShaderRegister=0;ranges[1].RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;ranges[1].NumDescriptors=16;ranges[1].BaseShaderRegister=0;
    // 0-3: PS SRV/sampler and VS SRV/sampler tables; 4-7 VS b0-b3, 8-13 PS b0-b5 and 14-17 GS b0-b3 root CBVs.
    D3D12_ROOT_PARAMETER params[kRootParameterCount]{};
    for(int i=0;i<4;++i){params[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[i].DescriptorTable.NumDescriptorRanges=1;params[i].DescriptorTable.pDescriptorRanges=&ranges[i&1];params[i].ShaderVisibility=i<2?D3D12_SHADER_VISIBILITY_PIXEL:D3D12_SHADER_VISIBILITY_VERTEX;}
    for(UINT i=kRootVertexConstants;i<kRootParameterCount;++i){
        params[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;
        if(i<kRootPixelConstants){params[i].Descriptor.ShaderRegister=i-kRootVertexConstants;params[i].ShaderVisibility=D3D12_SHADER_VISIBILITY_VERTEX;}
        else if(i<kRootGeometryConstants){params[i].Descriptor.ShaderRegister=i-kRootPixelConstants;params[i].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;}
        else{params[i].Descriptor.ShaderRegister=i-kRootGeometryConstants;params[i].ShaderVisibility=D3D12_SHADER_VISIBILITY_GEOMETRY;}
    }
    D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=kRootParameterCount;desc.pParameters=params;desc.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    Microsoft::WRL::ComPtr<ID3DBlob> blob,error;HRESULT hr=D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error);if(FAILED(hr)){Warning("ShaderAPIDX12: root signature serialization failed 0x%08x: %s\n",static_cast<unsigned>(hr),error?static_cast<const char *>(error->GetBufferPointer()):"unknown");return false;}hr=device_->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root_));if(FAILED(hr))Warning("ShaderAPIDX12: root signature creation failed 0x%08x\n",static_cast<unsigned>(hr));return SUCCEEDED(hr);
}
void CPipelineCacheDX12::Shutdown(){entries_.Purge();++pipelineEpoch_;lastPipelineIndex_=-1;pipelineHints_.fill(0);freeResourceDescriptors_.Purge();geometryInFlight_.Purge();++retainEpoch_;for(auto &page:uploadPages_)if(page.mapped)page.resource->Unmap(0,nullptr);uploadPages_.Purge();zeroConstants_.Reset();zeroConstantAddress_=0;bindings_.Shutdown();root_.Reset();device_=nullptr;nullSrv_={};linearClampSampler_={};resourceStride_=samplerStride_=0;lastReclaimedFence_=0;reclaimDirty_=true;}
D3D12_CPU_DESCRIPTOR_HANDLE CPipelineCacheDX12::AcquireResourceDescriptor(uint64_t lastUseFence)
{
    // Released slots are reused only after the fence that could still replay a copy from them completes.
    if(freeResourceDescriptors_.Count()&&freeResourceDescriptors_.Head().fence<=lastReclaimedFence_){
        const auto descriptor=freeResourceDescriptors_.Head().descriptor;freeResourceDescriptors_.Remove(0);return descriptor;
    }
    return bindings_.AllocatePersistentResource(1,lastUseFence).cpu;
}
void CPipelineCacheDX12::ReleaseResourceDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE descriptor,uint64_t retireFence)
{
    // Recorded table copies read these CPU-only source slots at replay; retire them by fence.
    if(descriptor.ptr){freeResourceDescriptors_.AddToTail({descriptor,retireFence});++srvDescriptorEpoch_;}
}
bool CPipelineCacheDX12::AllocateUploadLocked(const void *data,size_t bytes,size_t allocationBytes,size_t alignment,uint64_t retireFence,D3D12_GPU_VIRTUAL_ADDRESS &gpuAddress,ID3D12Resource **source,size_t *sourceOffset,const uint32_t *swapOffsets,size_t swapCount,size_t vertexStride)
{
    if(!device_||!data||!bytes||allocationBytes<bytes||!alignment||(alignment&(alignment-1)))return false;
    if(swapCount&&(!swapOffsets||!vertexStride||bytes%vertexStride))return false;
    for(size_t i=0;i<swapCount;++i)if(swapOffsets[i]>vertexStride||vertexStride-swapOffsets[i]<4)return false;
    // Pages are cached (write-back) system memory, like readback heaps: the per-draw constant/geometry copies and
    // compare-free rewrites are cheaper than write-combined stores, and the GPU reads them coherently over the bus.
    UploadPage *chosen=nullptr;size_t offset=0;
    // Try the page that served the previous allocation before scanning; pages fill front to back.
    const auto fits=[&](UploadPage &page){const size_t start=(page.used+alignment-1)&~(alignment-1);if(start<=page.capacity&&page.capacity-start>=allocationBytes){chosen=&page;offset=start;return true;}return false;};
    if(uploadPageHint_<uploadPages_.Count()&&fits(uploadPages_[uploadPageHint_])){}
    else for(int i=0;i<uploadPages_.Count();++i)if(fits(uploadPages_[i])){uploadPageHint_=i;break;}
    if(!chosen){D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_CUSTOM;heap.CPUPageProperty=D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;heap.MemoryPoolPreference=D3D12_MEMORY_POOL_L0;D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=std::max<size_t>(4u*1024u*1024u,(allocationBytes+65535u)&~size_t(65535u))+kConstantBufferMaxBytes;desc.Height=1;desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;UploadPage page{};if(FAILED(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&page.resource))))return false;void *mapped=nullptr;D3D12_RANGE read{};if(FAILED(page.resource->Map(0,&read,&mapped)))return false;page.mapped=static_cast<unsigned char *>(mapped);page.gpu=page.resource->GetGPUVirtualAddress();page.capacity=static_cast<size_t>(desc.Width)-kConstantBufferMaxBytes;*uploadPages_.AddToTailGetPtr()=std::move(page);chosen=&uploadPages_.Tail();uploadPageHint_=uploadPages_.Count()-1;}
    std::memcpy(chosen->mapped+offset,data,bytes);
    if(swapCount)
    {
        ZoneNamedN(___tracy_scoped_zone, "DX12 UploadColorSwap", DX12_DRAW_ZONES_ACTIVE);
        // Convert from the CPU source; the destination is only written.
        const auto *sourceBytes=static_cast<const unsigned char *>(data);
        for(size_t vertex=0;vertex<bytes/vertexStride;++vertex)for(size_t i=0;i<swapCount;++i)
        {
            const size_t colorOffset=vertex*vertexStride+swapOffsets[i];
            uint32_t color;std::memcpy(&color,sourceBytes+colorOffset,sizeof(color));
            color=(color&0xff00ff00u)|((color&0x00ff0000u)>>16)|((color&0x000000ffu)<<16);
            std::memcpy(chosen->mapped+offset+colorOffset,&color,sizeof(color));
        }
    }
    if(allocationBytes>bytes)std::memset(chosen->mapped+offset+bytes,0,allocationBytes-bytes);chosen->used=offset+allocationBytes;chosen->fence=std::max(chosen->fence,retireFence);gpuAddress=chosen->gpu+offset;if(source)*source=chosen->resource.Get();if(sourceOffset)*sourceOffset=offset;return true;
}
bool CPipelineCacheDX12::UploadTransient(const void *data,size_t bytes,size_t allocationBytes,size_t alignment,uint64_t retireFence,D3D12_GPU_VIRTUAL_ADDRESS &gpuAddress,const uint32_t *swapOffsets,size_t swapCount,size_t vertexStride)
{
    return AllocateUploadLocked(data,bytes,allocationBytes,alignment,retireFence,gpuAddress,nullptr,nullptr,swapOffsets,swapCount,vertexStride);
}
void CPipelineCacheDX12::RetainGeometryLocked(ID3D12Resource *resource, uint64_t retireFence)
{
    ZoneNamedN(___tracy_scoped_zone, "DX12 RetainGeometry", DX12_DRAW_ZONES_ACTIVE);
    auto index=geometryInFlight_.Find(resource);
    if(index==geometryInFlight_.InvalidHandle())
    {
        index=geometryInFlight_.Insert(resource,RetiredResource{});
        geometryInFlight_[index].resource=resource;
    }
    auto &flight=geometryInFlight_[index];
    flight.fence=std::max(flight.fence,retireFence);
}
bool CPipelineCacheDX12::EnsureGeometryBuffer(CCommandRecorderDX12 *list,CVertexBufferDX12 &buffer,size_t usedBytes,uint64_t retireFence,D3D12_GPU_VIRTUAL_ADDRESS &gpuAddress,const uint32_t *swapOffsets,size_t swapCount,size_t vertexStride)
{
    ZoneNamedN(___tracy_scoped_zone, "DX12 EnsureGeometryBuffer", DX12_DRAW_ZONES_ACTIVE);
    if(!list||!device_||!usedBytes||!buffer.Stride()||usedBytes>buffer.Data().size())return false;
    if(buffer.IsDynamic())return UploadTransient(buffer.Data().data(),usedBytes,usedBytes,16,retireFence,gpuAddress,swapOffsets,swapCount,vertexStride?vertexStride:buffer.Stride());
    const bool current=buffer.NativeResource()&&buffer.NativeDevice()==device_&&buffer.NativeResourceVersion()==buffer.ContentVersion()&&buffer.NativeResourceBytes()>=usedBytes;
    // Retention for this fence already exists; skip the table update.
    if(current&&buffer.IsRetainedFor(retireFence,retainEpoch_)){gpuAddress=buffer.RetainedAddress();return true;}
    if(current){gpuAddress=buffer.NativeResource()->GetGPUVirtualAddress();RetainGeometryLocked(buffer.NativeResource(),retireFence);buffer.MarkRetained(retireFence,retainEpoch_);return true;}
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=(usedBytes+3u)&~size_t(3u);desc.Height=1;desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;Microsoft::WRL::ComPtr<ID3D12Resource> resource;if(FAILED(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&resource))))return false;
    D3D12_GPU_VIRTUAL_ADDRESS uploadGpu=0;ID3D12Resource *uploadSource=nullptr;size_t uploadOffset=0;if(!AllocateUploadLocked(buffer.Data().data(),usedBytes,desc.Width,16,retireFence,uploadGpu,&uploadSource,&uploadOffset,swapOffsets,swapCount,vertexStride?vertexStride:buffer.Stride()))return false;
    list->CopyBufferRegion(resource.Get(),0,uploadSource,uploadOffset,usedBytes);D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition.pResource=resource.Get();barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST;barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;list->ResourceBarrier(1,&barrier);
    buffer.NativeResourceRef()=std::move(resource);buffer.SetNativeResourceVersion(buffer.ContentVersion(),usedBytes,device_);gpuAddress=buffer.NativeResource()->GetGPUVirtualAddress();RetainGeometryLocked(buffer.NativeResource(),retireFence);buffer.MarkRetained(retireFence,retainEpoch_);return true;
}
bool CPipelineCacheDX12::EnsureIndexBuffer(CCommandRecorderDX12 *list,CIndexBufferDX12 &buffer,size_t usedBytes,uint64_t retireFence,D3D12_GPU_VIRTUAL_ADDRESS &gpuAddress)
{
    ZoneNamedN(___tracy_scoped_zone, "DX12 EnsureIndexBuffer", DX12_DRAW_ZONES_ACTIVE);
    if(!list||!device_||!usedBytes||usedBytes>buffer.Data().size())return false;
    if(buffer.IsDynamic())return UploadTransient(buffer.Data().data(),usedBytes,usedBytes,4,retireFence,gpuAddress);
    const bool current=buffer.NativeResource()&&buffer.NativeDevice()==device_&&buffer.NativeResourceVersion()==buffer.ContentVersion()&&buffer.NativeResourceBytes()>=usedBytes;
    if(current&&buffer.IsRetainedFor(retireFence,retainEpoch_)){gpuAddress=buffer.RetainedAddress();return true;}
    if(current){gpuAddress=buffer.NativeResource()->GetGPUVirtualAddress();RetainGeometryLocked(buffer.NativeResource(),retireFence);buffer.MarkRetained(retireFence,retainEpoch_);return true;}
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=(usedBytes+3u)&~size_t(3u);desc.Height=1;desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;Microsoft::WRL::ComPtr<ID3D12Resource> resource;if(FAILED(device_->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&resource))))return false;
    D3D12_GPU_VIRTUAL_ADDRESS uploadGpu=0;ID3D12Resource *uploadSource=nullptr;size_t uploadOffset=0;if(!AllocateUploadLocked(buffer.Data().data(),usedBytes,desc.Width,4,retireFence,uploadGpu,&uploadSource,&uploadOffset,nullptr,0,0))return false;list->CopyBufferRegion(resource.Get(),0,uploadSource,uploadOffset,usedBytes);D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition.pResource=resource.Get();barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST;barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_INDEX_BUFFER;barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;list->ResourceBarrier(1,&barrier);
    buffer.NativeResourceRef()=std::move(resource);buffer.SetNativeResourceVersion(buffer.ContentVersion(),usedBytes,device_);gpuAddress=buffer.NativeResource()->GetGPUVirtualAddress();RetainGeometryLocked(buffer.NativeResource(),retireFence);buffer.MarkRetained(retireFence,retainEpoch_);return true;
}
DescriptorRangeDX12 CPipelineCacheDX12::PrepareSamplerTable(const std::array<D3D12_SAMPLER_DESC,32> &samplers,const std::array<uint16_t,32> &ids,uint64_t retireFence)
{
    ZoneNamedN(___tracy_scoped_zone, "DX12 SamplerDescriptorAllocation", DX12_DRAW_ZONES_ACTIVE);
    if(!device_||!bindings_.SamplerHeap().Heap())return {};
    // Callers supply canonical descriptions: unused slots hold DefaultSamplerDesc with id 0.
    return bindings_.AcquireSamplerTable(samplers,ids,retireFence);
}
void CPipelineCacheDX12::BindDrawState(CCommandRecorderDX12 *list,const D3D12_VIEWPORT &viewport,const D3D12_RECT &scissor,uint64_t retireFence)
{
    const bool reset=!drawStateValid_||drawStateFence_!=retireFence;
    if(reset)list->SetGraphicsRootSignature(root_.Get());
    if(reset||std::memcmp(&boundViewport_,&viewport,sizeof(viewport))){
        list->RSSetViewports(1,&viewport);boundViewport_=viewport;
    }
    if(reset||std::memcmp(&boundScissor_,&scissor,sizeof(scissor))){
        list->RSSetScissorRects(1,&scissor);boundScissor_=scissor;
    }
    drawStateFence_=retireFence;drawStateValid_=true;
}
bool CPipelineCacheDX12::PrepareBindings(CCommandRecorderDX12 *list,const BindingInputDX12 &input)
{
 if(!device_||!list||!bindings_.ResourceHeap().Heap()||!bindings_.SamplerHeap().Heap())return false;
 const DescriptorRangeDX12 &samplerTable=input.samplerTable;
 if(samplerTable.count!=32||!zeroConstantAddress_)return false;
 const SIZE_T resourceStride=resourceStride_;
 const SIZE_T samplerStride=samplerStride_;
 // Constant uploads are fence-scoped: an unchanged (version, shader, extent) slot keeps its address, and a
 // small per-bank table catches versions revisited within the same recording fence (A/B/A material
 // alternation). Uploads come from fence-retired transient pages, so no cross-fence pinning is needed.
 std::array<D3D12_GPU_VIRTUAL_ADDRESS,10> constantAddresses; // every slot is assigned or the call fails
 {
  ZoneNamedN(___tracy_scoped_zone, "DX12 ConstantBindings", DX12_DRAW_ZONES_ACTIVE);
 for(unsigned i=0;i<10;++i)
 {
  // Cache banks 0-5 are shader register files; 6-9 are the four extension buffers.
  const unsigned bank=i<3?i:(i==3?6:(i<=6?i-1:i));
  size_t bytes=input.constantSizes[i];if(bank<6){const size_t registerSize=(i==2||i==6)?sizeof(uint32_t):16;const size_t count=input.consumedRegisters[bank];if(count*registerSize>bytes)return false;bytes=count*registerSize;}
  // Root CBVs have no extent; an empty bank reads the persistent zero buffer.
  if(!bytes){constantAddresses[i]=zeroConstantAddress_;continue;}
  const uint64_t version=input.constantVersions[bank],shaderId=bank<6?input.constantShaderIds[bank]:0;
  auto &last=lastConstantSlots_[i];
  if(version&&last.fence==input.retireFence&&last.version==version&&last.shaderId==shaderId&&last.bytes==bytes){constantAddresses[i]=last.address;++stats_.constantHits;continue;}
  const size_t uploadBytes=std::max<size_t>(bytes,4);const size_t size=(uploadBytes+255)&~size_t(255);if(size>kConstantBufferMaxBytes)return false;
  D3D12_GPU_VIRTUAL_ADDRESS gpu=0;
  if(version)
  {
   auto &recent=recentConstants_[bank][Mix32HashFunctor()(static_cast<uint32_t>(version)^static_cast<uint32_t>(version>>32)^static_cast<uint32_t>(shaderId)*0x9E3779B1u)&(kRecentConstants-1)];
   if(recent.fence==input.retireFence&&recent.version==version&&recent.shaderId==shaderId&&recent.bytes==bytes){gpu=recent.address;++stats_.constantHits;}
   else
   {
    if(!input.constantData[i])return false;
    ++stats_.constantUploads;
    if(!AllocateUploadLocked(input.constantData[i],bytes,size,256,input.retireFence,gpu,nullptr,nullptr,nullptr,0,0))return false;
    recent={input.retireFence,version,shaderId,bytes,gpu};
   }
   last={input.retireFence,version,shaderId,bytes,gpu};
  }
  else {++stats_.transientConstants;last.fence=0;if(!AllocateUploadLocked(input.constantData[i],uploadBytes,size,256,input.retireFence,gpu,nullptr,nullptr,nullptr,0,0))return false;}
  constantAddresses[i]=gpu;
 }
 }
 D3D12_GPU_DESCRIPTOR_HANDLE srvs{};
 {
  ZoneNamedN(___tracy_scoped_zone, "DX12 SRVBindings", DX12_DRAW_ZONES_ACTIVE);
 // Complete-source tables are reusable within their recording fence and heap generation; a CPU source slot
 // can be rewritten for another texture copy, so resources are compared as well as handles.
 const uint64_t generation=bindings_.ResourceHeap().Generation();
 const auto sameTable=[&](const CachedSrvTable &cached){
  return cached.fence==input.retireFence&&cached.heapGeneration==generation&&cached.descriptorEpoch==srvDescriptorEpoch_&&
   !std::memcmp(cached.sources.data(),input.srvSources.data(),sizeof(input.srvSources))&&
   !std::memcmp(cached.resources.data(),input.textures.data(),sizeof(input.textures));
 };
 CachedSrvTable *slot=nullptr;
 const bool previousValid=lastSrvTableIsPrevious_;
 // An unchanged texture set reuses the previous draw's table (whose sources were complete) without scanning
 // or comparing the 32 handles.
 const bool reusePrevious=input.texturesUnchanged&&previousValid&&lastSrvTable_.fence==input.retireFence&&lastSrvTable_.heapGeneration==generation&&lastSrvTable_.descriptorEpoch==srvDescriptorEpoch_;
 const bool sourcesComplete=reusePrevious||[&]{SIZE_T zero=0;for(const auto source:input.srvSources)zero|=source.ptr==0;return zero==0;}();
 lastSrvTableIsPrevious_=false;
 if(sourcesComplete){
  if(reusePrevious){srvs=lastSrvTable_.gpu;++stats_.srvTableHits;}
  else if(sameTable(lastSrvTable_)){srvs=lastSrvTable_.gpu;++stats_.srvTableHits;}
  else{
   const uint64_t identity=input.srvSources[0].ptr^(input.srvSources[1].ptr*1099511628211ull)^input.srvSources[2].ptr;
   slot=&srvTables_[Mix32HashFunctor()(static_cast<uint32_t>(identity)^static_cast<uint32_t>(identity>>32))&(srvTables_.size()-1)];
   if(sameTable(*slot)){srvs=slot->gpu;lastSrvTable_=*slot;++stats_.srvTableHits;slot=nullptr;}
  }
  lastSrvTableIsPrevious_=srvs.ptr!=0;
 }
 if(!srvs.ptr){
  DescriptorRangeDX12 table; { ZoneNamedN(___tracy_scoped_zone, "DX12 ResourceDescriptorAllocation", DX12_DRAW_ZONES_ACTIVE); table=bindings_.AllocateDescriptors(32,input.retireFence); }
  if(table.count!=32)return false;
  srvs=table.gpu;
  if(sourcesComplete){
   // The submission worker performs the copy at replay; source slots are fence-retired (ReleaseResourceDescriptor).
   list->CopyDescriptorTable(table.cpu,32,input.srvSources.data());++stats_.srvTableCopies;
   CachedSrvTable &fresh=lastSrvTable_;fresh.sources=input.srvSources;fresh.resources=input.textures;fresh.gpu=srvs;
   fresh.fence=input.retireFence;fresh.heapGeneration=table.generation;fresh.descriptorEpoch=srvDescriptorEpoch_;
   if(slot)*slot=fresh;
   lastSrvTableIsPrevious_=true;
  }else{
   D3D12_CPU_DESCRIPTOR_HANDLE destinations[32],sources[32];UINT copyCount=0;
   for(unsigned i=0;i<32;++i){
    D3D12_CPU_DESCRIPTOR_HANDLE cpu=table.cpu;cpu.ptr+=i*resourceStride;
    auto source=input.srvSources[i];
    if(!source.ptr){
     D3D12_SHADER_RESOURCE_VIEW_DESC srv=input.srvDescs[i];
     if(!srv.Shader4ComponentMapping)srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
     if(!srv.ViewDimension)srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
     if(srv.Format==DXGI_FORMAT_UNKNOWN)srv.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
     if(srv.ViewDimension==D3D12_SRV_DIMENSION_TEXTURE2D&&!srv.Texture2D.MipLevels)srv.Texture2D.MipLevels=1;
     if(!input.textures[i]&&!std::memcmp(&srv,&nullSrvDesc_,sizeof(srv)))source=nullSrv_;
     else{device_->CreateShaderResourceView(input.textures[i],&srv,cpu);continue;}
    }
    destinations[copyCount]=cpu;sources[copyCount]=source;++copyCount;
   }
   if(copyCount)device_->CopyDescriptors(copyCount,destinations,nullptr,copyCount,sources,nullptr,D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  }
 }
 }
 {
  ZoneNamedN(___tracy_scoped_zone, "DX12 RootTables", DX12_DRAW_ZONES_ACTIVE);
 ID3D12DescriptorHeap *resourceHeap=bindings_.ResourceHeap().Heap(),*samplerHeap=bindings_.SamplerHeap().Heap();
 if(!graphicsBindingsValid_||graphicsBindingsFence_!=input.retireFence||boundResourceHeap_!=resourceHeap||boundSamplerHeap_!=samplerHeap){
  ID3D12DescriptorHeap *heaps[]={resourceHeap,samplerHeap};list->SetDescriptorHeaps(2,heaps);
  graphicsBindingsValid_=false;boundResourceHeap_=resourceHeap;boundSamplerHeap_=samplerHeap;
 }
 D3D12_GPU_DESCRIPTOR_HANDLE vertexSrvs=srvs;vertexSrvs.ptr+=16*resourceStride;D3D12_GPU_DESCRIPTOR_HANDLE vertexSamplers=samplerTable.gpu;vertexSamplers.ptr+=16*samplerStride;
 const D3D12_GPU_DESCRIPTOR_HANDLE rootTables[]={srvs,samplerTable.gpu,vertexSrvs,vertexSamplers};
 // Parameters a draw's shaders never read may stay stale; *Current_ records whether they match this draw.
 if(!graphicsBindingsValid_){vertexTablesCurrent_=false;geometryConstantsCurrent_=false;}
 const UINT tableCount=input.vertexTextures?4u:2u;
 for(UINT i=0;i<tableCount;++i)if(!graphicsBindingsValid_||(i>=2&&!vertexTablesCurrent_)||boundRootTables_[i].ptr!=rootTables[i].ptr){
  ++stats_.rootTableSets;list->SetGraphicsRootDescriptorTable(i,rootTables[i]);boundRootTables_[i]=rootTables[i];
 }
 if(input.vertexTextures)vertexTablesCurrent_=true;
 else if(boundRootTables_[2].ptr!=vertexSrvs.ptr||boundRootTables_[3].ptr!=vertexSamplers.ptr)vertexTablesCurrent_=false;
 // Slots 0-3 feed VS b0-b3 (mirrored to GS b0-b3 when a geometry stage runs); slots 4-9 feed PS b0-b5.
 for(UINT slot=0;slot<10;++slot){
  const UINT root=slot<4?kRootVertexConstants+slot:kRootPixelConstants+(slot-4);
  if(!graphicsBindingsValid_||boundRootConstants_[root-kRootVertexConstants]!=constantAddresses[slot]){++stats_.rootCbvSets;list->SetGraphicsRootConstantBufferView(root,constantAddresses[slot]);boundRootConstants_[root-kRootVertexConstants]=constantAddresses[slot];}
  if(slot<4&&input.geometryStage){
   const UINT geometry=kRootGeometryConstants+slot;
   if(!graphicsBindingsValid_||!geometryConstantsCurrent_||boundRootConstants_[geometry-kRootVertexConstants]!=constantAddresses[slot]){++stats_.rootCbvSets;list->SetGraphicsRootConstantBufferView(geometry,constantAddresses[slot]);boundRootConstants_[geometry-kRootVertexConstants]=constantAddresses[slot];}
  }
 }
 if(input.geometryStage)geometryConstantsCurrent_=true;
 else for(UINT slot=0;slot<4;++slot)if(boundRootConstants_[kRootGeometryConstants+slot-kRootVertexConstants]!=constantAddresses[slot])geometryConstantsCurrent_=false;
 graphicsBindingsValid_=true;graphicsBindingsFence_=input.retireFence;
 return true;
}
}
void CPipelineCacheDX12::Reclaim(uint64_t completedFence)
{
    ZoneNamedN(___tracy_scoped_zone, "DX12 PipelineReclaim", DX12_ZONES_ACTIVE);
    if(!reclaimDirty_&&completedFence<=lastReclaimedFence_)return;
    bindings_.Reclaim(completedFence);
    for(auto &page:uploadPages_)if(page.used&&page.fence<=completedFence)page.used=0;
    uploadPageHint_=0;
 for(auto index=geometryInFlight_.FirstHandle();index!=geometryInFlight_.InvalidHandle();){if(geometryInFlight_[index].fence<=completedFence)index=geometryInFlight_.RemoveAndAdvance(index);else index=geometryInFlight_.NextHandle(index);}
    // Retain completed pages for reuse, as with uploadPages_; Shutdown releases the high-water pool.
    bool removedPipeline=false;
    for(int i=0;i<entries_.Count();)if((entries_[i].destroyed||entries_.Count()>512)&&entries_[i].lastUseFence<=completedFence){entries_.Remove(i);removedPipeline=true;}else ++i;
    if(removedPipeline){lastPipelineIndex_=-1;pipelineHints_.fill(0);++pipelineEpoch_;}
    lastReclaimedFence_=(std::max)(lastReclaimedFence_,completedFence);
    reclaimDirty_=false;
}
void CPipelineCacheDX12::NotifyShaderDestroyed(uint64_t shaderIdentity)
{
    reclaimDirty_=true;
 for(auto &entry:entries_)if(entry.key.vs==shaderIdentity||entry.key.ps==shaderIdentity||entry.key.gs==shaderIdentity)entry.destroyed=true;
 // Fence-scoped constant entries may name this shader until the fence changes; drop them now.
 for(auto &bank:recentConstants_)for(auto &entry:bank)if(entry.shaderId==shaderIdentity)entry.fence=0;
 for(auto &entry:lastConstantSlots_)if(entry.shaderId==shaderIdentity)entry.fence=0;
}
ID3D12PipelineState *CPipelineCacheDX12::GetOrCreate(const PipelineKeyDX12 &key,const D3D12_SHADER_BYTECODE &vs,const D3D12_SHADER_BYTECODE &ps,const D3D12_SHADER_BYTECODE &gs,const D3D12_INPUT_LAYOUT_DESC &layout,uint64_t retireFence)
{
    if(lastPipelineIndex_>=0){auto &entry=entries_[lastPipelineIndex_];if(!entry.destroyed&&entry.key==key){entry.lastUseFence=retireFence;return entry.pso.Get();}}
    const uint64_t identity=key.vs^(key.ps*1099511628211ull)^key.vsVariant^key.psVariant^key.gs^key.gsVariant^key.input^(uint64_t(key.color)<<8)^(uint64_t(key.depth)<<16)^key.samples^key.raster;
    const unsigned hint=Mix32HashFunctor()(static_cast<uint32_t>(identity)^static_cast<uint32_t>(identity>>32))&static_cast<unsigned>(pipelineHints_.size()-1);
    if(pipelineHints_[hint]){const int index=static_cast<int>(pipelineHints_[hint]-1);auto &entry=entries_[index];if(!entry.destroyed&&entry.key==key){lastPipelineIndex_=index;entry.lastUseFence=retireFence;return entry.pso.Get();}}
    { ZoneNamedN(pipelineSearch, "DX12 PSOHintFallback", DX12_DRAW_ZONES_ACTIVE);
    for(int i=0;i<entries_.Count();++i){auto &entry=entries_[i];if(!entry.destroyed&&entry.key==key){lastPipelineIndex_=i;pipelineHints_[hint]=static_cast<uint32_t>(i)+1;entry.lastUseFence=retireFence;return entry.pso.Get();}}
    }
    ZoneNamedN(___tracy_scoped_zone, "DX12 PSOCreateMiss", DX12_ZONES_ACTIVE);
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};desc.pRootSignature=root_.Get();desc.VS=vs;desc.PS=ps;desc.GS=gs;desc.InputLayout=layout;desc.PrimitiveTopologyType=static_cast<D3D12_PRIMITIVE_TOPOLOGY_TYPE>(key.topology);desc.NumRenderTargets=key.colorCount;for(UINT i=0;i<key.colorCount&&i<4;++i)desc.RTVFormats[i]=key.colorFormats[i];desc.DSVFormat=key.depth;desc.SampleDesc.Count=key.samples;desc.SampleDesc.Quality=key.sampleQuality;desc.SampleMask=UINT_MAX;
    desc.RasterizerState.FillMode=key.wireframe?D3D12_FILL_MODE_WIREFRAME:D3D12_FILL_MODE_SOLID;desc.RasterizerState.CullMode=key.culling?D3D12_CULL_MODE_BACK:D3D12_CULL_MODE_NONE;desc.RasterizerState.FrontCounterClockwise=key.frontCounterClockwise;desc.RasterizerState.DepthBias=key.depthBiasValue;desc.RasterizerState.SlopeScaledDepthBias=key.slopeScaledDepthBias;desc.RasterizerState.DepthClipEnable=TRUE;desc.BlendState.AlphaToCoverageEnable=key.alphaToCoverage&&key.samples>1;
    auto blendFactor=[](uint32_t f){switch(f){case SHADER_BLEND_ZERO:return D3D12_BLEND_ZERO;case SHADER_BLEND_ONE:return D3D12_BLEND_ONE;case SHADER_BLEND_DST_COLOR:return D3D12_BLEND_DEST_COLOR;case SHADER_BLEND_ONE_MINUS_DST_COLOR:return D3D12_BLEND_INV_DEST_COLOR;case SHADER_BLEND_SRC_ALPHA:return D3D12_BLEND_SRC_ALPHA;case SHADER_BLEND_ONE_MINUS_SRC_ALPHA:return D3D12_BLEND_INV_SRC_ALPHA;case SHADER_BLEND_DST_ALPHA:return D3D12_BLEND_DEST_ALPHA;case SHADER_BLEND_ONE_MINUS_DST_ALPHA:return D3D12_BLEND_INV_DEST_ALPHA;case SHADER_BLEND_SRC_ALPHA_SATURATE:return D3D12_BLEND_SRC_ALPHA_SAT;case SHADER_BLEND_SRC_COLOR:return D3D12_BLEND_SRC_COLOR;case SHADER_BLEND_ONE_MINUS_SRC_COLOR:return D3D12_BLEND_INV_SRC_COLOR;default:return D3D12_BLEND_ONE;}};
    auto alphaBlendFactor=[&](uint32_t f){switch(f){case SHADER_BLEND_DST_COLOR:return D3D12_BLEND_DEST_ALPHA;case SHADER_BLEND_ONE_MINUS_DST_COLOR:return D3D12_BLEND_INV_DEST_ALPHA;case SHADER_BLEND_SRC_COLOR:return D3D12_BLEND_SRC_ALPHA;case SHADER_BLEND_ONE_MINUS_SRC_COLOR:return D3D12_BLEND_INV_SRC_ALPHA;case SHADER_BLEND_SRC_ALPHA_SATURATE:return D3D12_BLEND_ONE;default:return blendFactor(f);}};
    static constexpr D3D12_BLEND_OP blendOps[]={D3D12_BLEND_OP_ADD,D3D12_BLEND_OP_SUBTRACT,D3D12_BLEND_OP_REV_SUBTRACT,D3D12_BLEND_OP_MIN,D3D12_BLEND_OP_MAX};
    for(UINT i=0;i<key.colorCount&&i<4;++i){auto &target=desc.BlendState.RenderTarget[i];target.RenderTargetWriteMask=(key.colorWrites?D3D12_COLOR_WRITE_ENABLE_RED|D3D12_COLOR_WRITE_ENABLE_GREEN|D3D12_COLOR_WRITE_ENABLE_BLUE:0)|(key.alphaWrites?D3D12_COLOR_WRITE_ENABLE_ALPHA:0);target.BlendEnable=key.blend!=0;target.SrcBlend=blendFactor(key.blendSource);target.DestBlend=blendFactor(key.blendDestination);target.BlendOp=key.blendOperation<5?blendOps[key.blendOperation]:D3D12_BLEND_OP_ADD;target.SrcBlendAlpha=alphaBlendFactor(key.separateAlpha?key.blendAlphaSource:key.blendSource);target.DestBlendAlpha=alphaBlendFactor(key.separateAlpha?key.blendAlphaDestination:key.blendDestination);target.BlendOpAlpha=(key.separateAlpha?key.blendAlphaOperation:key.blendOperation)<5?blendOps[key.separateAlpha?key.blendAlphaOperation:key.blendOperation]:D3D12_BLEND_OP_ADD;}
    desc.BlendState.IndependentBlendEnable=key.colorCount>1;desc.DepthStencilState.DepthEnable=key.depthTest&&key.depth!=DXGI_FORMAT_UNKNOWN;desc.DepthStencilState.DepthWriteMask=key.depthWrite?D3D12_DEPTH_WRITE_MASK_ALL:D3D12_DEPTH_WRITE_MASK_ZERO;static constexpr D3D12_COMPARISON_FUNC depthFuncs[]={D3D12_COMPARISON_FUNC_NEVER,D3D12_COMPARISON_FUNC_LESS,D3D12_COMPARISON_FUNC_EQUAL,D3D12_COMPARISON_FUNC_LESS_EQUAL,D3D12_COMPARISON_FUNC_GREATER,D3D12_COMPARISON_FUNC_NOT_EQUAL,D3D12_COMPARISON_FUNC_GREATER_EQUAL,D3D12_COMPARISON_FUNC_ALWAYS};desc.DepthStencilState.DepthFunc=key.depthFunction<8?depthFuncs[key.depthFunction]:D3D12_COMPARISON_FUNC_LESS_EQUAL;desc.DepthStencilState.StencilEnable=key.stencil&&key.depth!=DXGI_FORMAT_UNKNOWN;desc.DepthStencilState.StencilReadMask=0xff;desc.DepthStencilState.StencilWriteMask=0xff;
    static constexpr D3D12_STENCIL_OP stencilOps[]={D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_ZERO,D3D12_STENCIL_OP_REPLACE,D3D12_STENCIL_OP_INCR_SAT,D3D12_STENCIL_OP_DECR_SAT,D3D12_STENCIL_OP_INVERT,D3D12_STENCIL_OP_INCR,D3D12_STENCIL_OP_DECR};
    auto stencilOp=[&](uint32_t op){return op>=1&&op<=8?stencilOps[op]:D3D12_STENCIL_OP_KEEP;};
    auto stencilFunction=[&](uint32_t func){return func>=1&&func<=8?static_cast<D3D12_COMPARISON_FUNC>(func):D3D12_COMPARISON_FUNC_ALWAYS;};
    desc.DepthStencilState.StencilReadMask=key.stencilReadMask;desc.DepthStencilState.StencilWriteMask=key.stencilWriteMask;
    desc.DepthStencilState.FrontFace.StencilFailOp=stencilOp(key.stencilFail);desc.DepthStencilState.FrontFace.StencilDepthFailOp=stencilOp(key.stencilDepthFail);desc.DepthStencilState.FrontFace.StencilPassOp=stencilOp(key.stencilPass);desc.DepthStencilState.FrontFace.StencilFunc=stencilFunction(key.stencilFunction);desc.DepthStencilState.BackFace=desc.DepthStencilState.FrontFace;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;HRESULT hr=device_->CreateGraphicsPipelineState(&desc,IID_PPV_ARGS(&pso));if(FAILED(hr)){Warning("ShaderAPIDX12: graphics pipeline creation failed 0x%08x\n",static_cast<unsigned>(hr));return nullptr;}auto &entry=*entries_.AddToTailGetPtr();entry.key=key;entry.pso=std::move(pso);entry.lastUseFence=retireFence;lastPipelineIndex_=entries_.Count()-1;pipelineHints_[hint]=static_cast<uint32_t>(entries_.Count());return entry.pso.Get();
}
} // namespace shaderapidx12
