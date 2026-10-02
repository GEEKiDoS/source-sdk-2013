//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Fence-retired D3D12 descriptor heaps and the interned sampler table cache.
//
//=============================================================================//

#ifndef BINDINGS_DX12_H
#define BINDINGS_DX12_H
#pragma once

#include "tier1/utlvector.h"
#include "tier1/utlqueue.h"
#include "tier1/utllinkedlist.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>

namespace shaderapidx12
{
struct DescriptorRangeDX12
{
	D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
	D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
	uint32_t index = 0, count = 0;
	uint64_t generation = 0;
};

// Recording-thread owned (see CPipelineCacheDX12); no internal locking.
class CDescriptorAllocatorDX12
{
public:
	~CDescriptorAllocatorDX12() { Shutdown(); }

	bool Initialize( ID3D12Device *pDevice, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t nCapacity, bool bShaderVisible );
	void Shutdown();
	DescriptorRangeDX12 Allocate( uint32_t nCount, uint64_t nRetireFence );
	DescriptorRangeDX12 AllocatePersistent( uint32_t nCount, uint64_t nLastUseFence );
	void Reclaim( uint64_t nCompletedFence );

	ID3D12DescriptorHeap *Heap() const { return m_pHeap.Get(); }

	// Changes whenever Allocate switches to a fresh heap.
	uint64_t Generation() const { return m_nGeneration; }

private:
	struct Retired
	{
		uint32_t first, count;
		uint64_t fence;
	};

	struct RetiredHeap
	{
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
		uint64_t fence;
	};

	Microsoft::WRL::ComPtr<ID3D12Device> m_pDevice;
	CUtlLinkedList<RetiredHeap, uint32_t> m_RetiredHeaps;
	// Each pooled pointer owns one COM reference, transferred with Attach/Detach.
	CUtlVector<ID3D12DescriptorHeap *> m_AvailableHeaps;
	uint64_t m_nGeneration = 1;
	uint64_t m_nHeapFence = 0;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_pHeap;
	D3D12_DESCRIPTOR_HEAP_TYPE m_Type{};
	uint32_t m_nStride = 0, m_nCapacity = 0, m_nHead = 0, m_nUsed = 0;
	bool m_bShaderVisible = false;
	CUtlQueue<Retired> m_Retired;
	// Newest retirement run, not yet queued (see Allocate).
	Retired m_Pending{};
};

class CBindingCacheDX12
{
public:
	bool Initialize( ID3D12Device *pDevice );
	void Shutdown();
	DescriptorRangeDX12 AllocatePersistentResource( uint32_t nCount, uint64_t nLastUseFence );
	DescriptorRangeDX12 AllocatePersistentSampler( uint32_t nCount, uint64_t nLastUseFence );

	DescriptorRangeDX12 AllocateDescriptors( uint32_t nCount, uint64_t nRetireFence ) { return m_Resources.Allocate( nCount, nRetireFence ); }

	CDescriptorAllocatorDX12 &ResourceHeap() { return m_Resources; }

	CDescriptorAllocatorDX12 &SamplerHeap() { return m_Samplers; }

	static constexpr uint16_t kUninternedSampler = 0xffff;

	static D3D12_SAMPLER_DESC DefaultSampler()
	{
		D3D12_SAMPLER_DESC sampler{};
		sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		sampler.MaxLOD = D3D12_FLOAT32_MAX;
		sampler.MaxAnisotropy = 1;
		return sampler;
	}

	uint16_t InternSampler( const D3D12_SAMPLER_DESC &desc );
	// ids[i] must be InternSampler(samplers[i]) or kUninternedSampler.
	DescriptorRangeDX12 AcquireSamplerTable( const D3D12_SAMPLER_DESC ( &samplers )[32], const uint16_t ( &ids )[32], uint64_t nFence );
	void Reclaim( uint64_t nCompletedFence );

private:
	struct SamplerEntry
	{
		D3D12_SAMPLER_DESC samplers[32] = {};
		DescriptorRangeDX12 range{};
		uint64_t lastUseFence = 0, lastTouch = 0, hash = 0, generation = 0;
	};

	struct SamplerIdEntry
	{
		uint16_t ids[32] = {};
		int index = -1;
		uint64_t generation = 0;
	};

	SamplerIdEntry m_SamplerIds[256];
	CUtlVector<D3D12_SAMPLER_DESC> m_InternedSamplers;
	uint64_t m_nGenerationClock = 0;
	CUtlVector<SamplerEntry> m_SamplerCache;
	int m_nLastSamplerIndex = -1;
	CDescriptorAllocatorDX12 m_Resources, m_PersistentResources, m_Samplers;
	ID3D12Device *m_pDevice = nullptr;
	uint64_t m_nCompletedFence = 0, m_nTouchClock = 0;
};
} // namespace shaderapidx12

#endif // BINDINGS_DX12_H
