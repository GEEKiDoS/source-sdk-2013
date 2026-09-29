#include "bindings_dx12.h"
#include <cstring>
#include <algorithm>
#include <utility>
namespace shaderapidx12
{
bool CDescriptorAllocatorDX12::Initialize(ID3D12Device *device,D3D12_DESCRIPTOR_HEAP_TYPE type,uint32_t capacity,bool shaderVisible)
{
    Shutdown(); if(!device||capacity==0)return false; device_=device;type_=type;capacity_=capacity;shaderVisible_=shaderVisible;stride_=device->GetDescriptorHandleIncrementSize(type);
    D3D12_DESCRIPTOR_HEAP_DESC desc{};desc.Type=type;desc.NumDescriptors=capacity;desc.Flags=shaderVisible?D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE:D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    return SUCCEEDED(device->CreateDescriptorHeap(&desc,IID_PPV_ARGS(&heap_)));
}
void CDescriptorAllocatorDX12::Shutdown()
{
    heap_.Reset(); retired_.Purge(); pending_ = {}; retiredHeaps_.Purge();
    for(auto *heap:availableHeaps_)heap->Release();
    availableHeaps_.Purge(); device_.Reset();
    type_={}; stride_=capacity_=head_=used_=0; heapFence_=0; generation_=1;
}
DescriptorRangeDX12 CDescriptorAllocatorDX12::Allocate(uint32_t count,uint64_t retireFence)
{
    DescriptorRangeDX12 out{};
    if (!heap_ || !count || count > capacity_) return out;
    if (!used_) head_=0;
    if (count > capacity_ - head_)
    {
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> next;
        if (!availableHeaps_.IsEmpty()) { next.Attach(availableHeaps_.Tail()); availableHeaps_.RemoveMultipleFromTail(1); }
        else { D3D12_DESCRIPTOR_HEAP_DESC desc{};desc.Type=type_;desc.NumDescriptors=capacity_;desc.Flags=shaderVisible_?D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE:D3D12_DESCRIPTOR_HEAP_FLAG_NONE;if(FAILED(device_->CreateDescriptorHeap(&desc,IID_PPV_ARGS(&next))))return out; }
        auto &retiredHeap=retiredHeaps_[retiredHeaps_.AddToTail()];retiredHeap.heap=std::move(heap_);retiredHeap.fence=heapFence_;heap_=std::move(next);retired_.RemoveAll();pending_={};head_=used_=0;heapFence_=0;++generation_;
    }
    out.index=head_;out.count=count;out.generation=generation_;out.cpu=heap_->GetCPUDescriptorHandleForHeapStart();out.cpu.ptr+=static_cast<SIZE_T>(head_)*stride_;if(shaderVisible_){out.gpu=heap_->GetGPUDescriptorHandleForHeapStart();out.gpu.ptr+=static_cast<UINT64>(head_)*stride_;}head_+=count;used_+=count;heapFence_=(std::max)(heapFence_,retireFence);// Consecutive allocations for one fence retire together; the queue receives one entry per run.
    if(pending_.count&&pending_.fence==retireFence&&pending_.first+pending_.count==out.index)pending_.count+=count;
    else{if(pending_.count)retired_.Insert(pending_);pending_={out.index,count,retireFence};}
    return out;
}
DescriptorRangeDX12 CDescriptorAllocatorDX12::AllocatePersistent(uint32_t count,uint64_t lastUseFence)
{
    DescriptorRangeDX12 out{};if(!heap_||!count||count>capacity_-head_)return out;
    out.index=head_;out.count=count;out.generation=generation_;out.cpu=heap_->GetCPUDescriptorHandleForHeapStart();out.cpu.ptr+=static_cast<SIZE_T>(head_)*stride_;if(shaderVisible_){out.gpu=heap_->GetGPUDescriptorHandleForHeapStart();out.gpu.ptr+=static_cast<UINT64>(head_)*stride_;}head_+=count;used_+=count;heapFence_=(std::max)(heapFence_,lastUseFence);return out;
}
void CDescriptorAllocatorDX12::Reclaim(uint64_t completedFence)
{
    if (pending_.count && pending_.fence <= completedFence) { retired_.Insert(pending_); pending_ = {}; }
    while (!retired_.IsEmpty() && retired_.Head().fence <= completedFence)
    {
        used_-=retired_.Head().count;
        retired_.RemoveAtHead();
    }
    while (!retiredHeaps_.IsEmpty() && retiredHeaps_[retiredHeaps_.Head()].fence <= completedFence)
    {
        const auto head=retiredHeaps_.Head();
        availableHeaps_.AddToTail(retiredHeaps_[head].heap.Detach());
        retiredHeaps_.Remove(head);
    }
    if (!used_) { head_=0; heapFence_=0; }
}
bool CBindingCacheDX12::Initialize(ID3D12Device *device){samplerCache_.RemoveAll();samplerIds_.fill({});lastSamplerIndex_=-1;device_=device;completedFence_=touchClock_=0;return resources_.Initialize(device,D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,262144,true)&&persistentResources_.Initialize(device,D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,65536,false)&&samplers_.Initialize(device,D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,2048,true);}
void CBindingCacheDX12::Shutdown(){samplerCache_.RemoveAll();samplerIds_.fill({});lastSamplerIndex_=-1;resources_.Shutdown();persistentResources_.Shutdown();samplers_.Shutdown();device_=nullptr;completedFence_=touchClock_=0;}
DescriptorRangeDX12 CBindingCacheDX12::AllocatePersistentResource(uint32_t count,uint64_t fence){return persistentResources_.AllocatePersistent(count,fence);}
DescriptorRangeDX12 CBindingCacheDX12::AllocatePersistentSampler(uint32_t count,uint64_t fence){return samplers_.AllocatePersistent(count,fence);}
uint16_t CBindingCacheDX12::InternSampler(const D3D12_SAMPLER_DESC &desc)
{
    // Id 0 is the canonical default sampler; ids are stable for the process lifetime.
    if(internedSamplers_.IsEmpty())internedSamplers_.AddToTail(DefaultSampler());
    for(int i=0;i<internedSamplers_.Count();++i)if(!std::memcmp(&internedSamplers_[i],&desc,sizeof(desc)))return static_cast<uint16_t>(i);
    if(internedSamplers_.Count()>=0xffff)return kUninternedSampler;
    return static_cast<uint16_t>(internedSamplers_.AddToTail(desc));
}
DescriptorRangeDX12 CBindingCacheDX12::AcquireSamplerTable(const std::array<D3D12_SAMPLER_DESC,32> &samplers,const std::array<uint16_t,32> &ids,uint64_t fence)
{
    if(!device_)return {};
    const auto touch=[&](int index){auto &entry=samplerCache_[index];entry.lastUseFence=(std::max)(entry.lastUseFence,fence);entry.lastTouch=++touchClock_;lastSamplerIndex_=index;return entry.range;};
    // Interned ids identify each descriptor exactly; a 64-byte id table replaces hashing 32 descriptions.
    bool interned=true;for(uint16_t id:ids)if(id==kUninternedSampler){interned=false;break;}
    SamplerIdEntry *idEntry=nullptr;
    if(interned){
        uint64_t hash=1469598103934665603ull;{const auto *words=reinterpret_cast<const uint64_t *>(ids.data());for(size_t i=0;i<sizeof(ids)/sizeof(uint64_t);++i){hash^=words[i];hash*=1099511628211ull;}}
        idEntry=&samplerIds_[static_cast<size_t>(hash^(hash>>29))&(samplerIds_.size()-1)];
        if(idEntry->ids==ids&&idEntry->index>=0&&idEntry->index<samplerCache_.Count()&&samplerCache_[idEntry->index].generation==idEntry->generation)return touch(idEntry->index);
    }
    const auto remember=[&](int index){if(idEntry){idEntry->ids=ids;idEntry->index=index;idEntry->generation=samplerCache_[index].generation;}return touch(index);};
    // Equal tables compare quickly; the previous table is the common hit, so try it before hashing.
    if(lastSamplerIndex_>=0&&!std::memcmp(samplerCache_[lastSamplerIndex_].samplers.data(),samplers.data(),sizeof(samplers)))return remember(lastSamplerIndex_);
    // Four independent lanes keep the word hash latency-bound work short; a byte compare still decides equality.
    uint64_t lanes[4]={1469598103934665603ull,0x9E3779B97F4A7C15ull,0xC2B2AE3D27D4EB4Full,0x165667B19E3779F9ull};
    {const auto *words=reinterpret_cast<const uint64_t *>(samplers.data());constexpr size_t count=sizeof(samplers)/sizeof(uint64_t);static_assert(count%4==0,"sampler table size");
     for(size_t i=0;i<count;i+=4)for(size_t l=0;l<4;++l){lanes[l]^=words[i+l];lanes[l]*=1099511628211ull;}}
    const uint64_t hash=lanes[0]^(lanes[1]<<1)^(lanes[2]<<2)^(lanes[3]<<3);
    for(int i=0;i<samplerCache_.Count();++i)if(i!=lastSamplerIndex_&&samplerCache_[i].hash==hash&&!std::memcmp(samplerCache_[i].samplers.data(),samplers.data(),sizeof(samplers)))return remember(i);
    DescriptorRangeDX12 range=samplers_.AllocatePersistent(32,fence);
    int index=-1;
    if(range.count!=32)
    {
        auto victim=samplerCache_.end();for(auto it=samplerCache_.begin();it!=samplerCache_.end();++it)if(it->lastUseFence<=completedFence_&&(victim==samplerCache_.end()||it->lastTouch<victim->lastTouch))victim=it;
        if(victim==samplerCache_.end())return {};range=victim->range;*victim={samplers,range,fence,++touchClock_,hash,++generationClock_};index=static_cast<int>(victim-samplerCache_.begin());
    }
    else index=samplerCache_.AddToTail({samplers,range,fence,++touchClock_,hash,++generationClock_});
    const UINT stride=device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);D3D12_CPU_DESCRIPTOR_HANDLE cpu=range.cpu;for(unsigned i=0;i<32;++i){device_->CreateSampler(&samplers[i],cpu);cpu.ptr+=stride;}
    return remember(index);
}
void CBindingCacheDX12::Reclaim(uint64_t fence){if(fence<=completedFence_)return;completedFence_=fence;resources_.Reclaim(fence);}
} // namespace shaderapidx12
