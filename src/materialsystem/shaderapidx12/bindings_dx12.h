#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include "tier1/utlvector.h"
#include "tier1/utlqueue.h"
#include "tier1/utllinkedlist.h"
#include <array>
#include <cstdint>

namespace shaderapidx12
{
struct DescriptorRangeDX12 { D3D12_CPU_DESCRIPTOR_HANDLE cpu{}; D3D12_GPU_DESCRIPTOR_HANDLE gpu{}; uint32_t index=0,count=0; uint64_t generation=0; };
// Recording-thread owned (see CPipelineCacheDX12); no internal locking.
class CDescriptorAllocatorDX12
{
public:
    ~CDescriptorAllocatorDX12() { Shutdown(); }
    bool Initialize(ID3D12Device *device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t capacity, bool shaderVisible);
    void Shutdown();
    DescriptorRangeDX12 Allocate(uint32_t count, uint64_t retireFence);
    DescriptorRangeDX12 AllocatePersistent(uint32_t count, uint64_t lastUseFence);
    void Reclaim(uint64_t completedFence);
    ID3D12DescriptorHeap *Heap() const { return heap_.Get(); }
    uint32_t Capacity() const { return capacity_; }
    uint32_t Used() const { return used_; }
    // Changes whenever Allocate switches to a fresh heap.
    uint64_t Generation() const { return generation_; }
private:
    struct Retired { uint32_t first,count; uint64_t fence; };
    struct RetiredHeap { Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap; uint64_t fence; };
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    CUtlLinkedList<RetiredHeap,uint32_t> retiredHeaps_;
    // Each pooled pointer owns one COM reference, transferred with Attach/Detach.
    CUtlVector<ID3D12DescriptorHeap *> availableHeaps_;
    uint64_t generation_ = 1;
    uint64_t heapFence_ = 0;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap_;
    D3D12_DESCRIPTOR_HEAP_TYPE type_{};
    uint32_t stride_=0,capacity_=0,head_=0,used_=0;
    bool shaderVisible_=false;
    CUtlQueue<Retired> retired_;
    // Newest retirement run, not yet queued (see Allocate).
    Retired pending_{};
};
class CBindingCacheDX12
{
public:
    bool Initialize(ID3D12Device *device);
    void Shutdown();
    DescriptorRangeDX12 AllocatePersistentResource(uint32_t count,uint64_t lastUseFence);
    DescriptorRangeDX12 AllocatePersistentSampler(uint32_t count,uint64_t lastUseFence);
    DescriptorRangeDX12 AllocateDescriptors(uint32_t count,uint64_t retireFence){return resources_.Allocate(count,retireFence);}
    CDescriptorAllocatorDX12 &ResourceHeap(){return resources_;}
    CDescriptorAllocatorDX12 &SamplerHeap(){return samplers_;}
    static constexpr uint16_t kUninternedSampler=0xffff;
    static D3D12_SAMPLER_DESC DefaultSampler(){D3D12_SAMPLER_DESC sampler{};sampler.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR;sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_WRAP;sampler.MaxLOD=D3D12_FLOAT32_MAX;sampler.MaxAnisotropy=1;return sampler;}
    uint16_t InternSampler(const D3D12_SAMPLER_DESC &desc);
    // ids[i] must be InternSampler(samplers[i]) or kUninternedSampler.
    DescriptorRangeDX12 AcquireSamplerTable(const std::array<D3D12_SAMPLER_DESC,32> &samplers,const std::array<uint16_t,32> &ids,uint64_t fence);
    void Reclaim(uint64_t completedFence);
private:
    struct SamplerEntry { std::array<D3D12_SAMPLER_DESC,32> samplers{}; DescriptorRangeDX12 range{}; uint64_t lastUseFence=0,lastTouch=0,hash=0,generation=0; };
    struct SamplerIdEntry { std::array<uint16_t,32> ids{}; int index=-1; uint64_t generation=0; };
    std::array<SamplerIdEntry,256> samplerIds_{};
    CUtlVector<D3D12_SAMPLER_DESC> internedSamplers_;
    uint64_t generationClock_=0;
    CUtlVector<SamplerEntry> samplerCache_;
    int lastSamplerIndex_=-1;
    CDescriptorAllocatorDX12 resources_,persistentResources_,samplers_;
    ID3D12Device *device_ = nullptr;
    uint64_t completedFence_=0,touchClock_=0;
};
} // namespace shaderapidx12
