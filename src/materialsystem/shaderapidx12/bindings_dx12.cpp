//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Fence-retired D3D12 descriptor heaps and the interned sampler table cache.
//
//=============================================================================//

#include "bindings_dx12.h"
#include "tier0/basetypes.h"

namespace shaderapidx12
{

//-----------------------------------------------------------------------------
// Purpose: Creates the first heap; later heaps are created on demand by Allocate
//-----------------------------------------------------------------------------
bool CDescriptorAllocatorDX12::Initialize( ID3D12Device *pDevice, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t nCapacity, bool bShaderVisible )
{
	Shutdown();
	m_pDevice = pDevice;
	m_Type = type;
	m_nCapacity = nCapacity;
	m_bShaderVisible = bShaderVisible;
	m_nStride = pDevice->GetDescriptorHandleIncrementSize( type );
	D3D12_DESCRIPTOR_HEAP_DESC desc{};
	desc.Type = type;
	desc.NumDescriptors = nCapacity;
	desc.Flags = bShaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	return SUCCEEDED( pDevice->CreateDescriptorHeap( &desc, IID_PPV_ARGS( &m_pHeap ) ) );
}

//-----------------------------------------------------------------------------
// Purpose: Releases the current, retired and pooled heaps
//-----------------------------------------------------------------------------
void CDescriptorAllocatorDX12::Shutdown()
{
	m_pHeap.Reset();
	m_Retired.Purge();
	m_Pending = {};
	m_RetiredHeaps.Purge();
	for ( int i = 0; i < m_AvailableHeaps.Count(); ++i )
		m_AvailableHeaps[i]->Release();
	m_AvailableHeaps.Purge();
	m_pDevice.Reset();
	m_Type = {};
	m_nStride = m_nCapacity = m_nHead = m_nUsed = 0;
	m_nHeapFence = 0;
	m_nGeneration = 1;
}

//-----------------------------------------------------------------------------
// Purpose: Ensures a contiguous transient run fits without allocating it
//-----------------------------------------------------------------------------
bool CDescriptorAllocatorDX12::EnsureCapacity( uint32_t nCount, uint64_t nRetireFence )
{
	if ( !nCount || nCount > m_nCapacity || !m_pHeap )
		return false;
	if ( !m_nUsed )
		m_nHead = 0;
	if ( nCount > m_nCapacity - m_nHead )
	{
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> pNext;
		if ( !m_AvailableHeaps.IsEmpty() )
		{
			pNext.Attach( m_AvailableHeaps.Tail() );
			m_AvailableHeaps.RemoveMultipleFromTail( 1 );
		}
		else
		{
			D3D12_DESCRIPTOR_HEAP_DESC desc{};
			desc.Type = m_Type;
			desc.NumDescriptors = m_nCapacity;
			desc.Flags = m_bShaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
			if ( FAILED( m_pDevice->CreateDescriptorHeap( &desc, IID_PPV_ARGS( &pNext ) ) ) )
				return false;
		}
		RetiredHeap &retiredHeap = m_RetiredHeaps[m_RetiredHeaps.AddToTail()];
		retiredHeap.heap.Swap( m_pHeap );
		retiredHeap.fence = m_nHeapFence;
		m_pHeap.Swap( pNext );
		m_Retired.RemoveAll();
		m_Pending = {};
		m_nHead = m_nUsed = 0;
		m_nHeapFence = 0;
		++m_nGeneration;
	}
	m_nHeapFence = MAX( nRetireFence, m_nHeapFence );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Linear transient allocation retired with nRetireFence; switches to a
//          fresh heap when the current one cannot fit the request
//-----------------------------------------------------------------------------
DescriptorRangeDX12 CDescriptorAllocatorDX12::Allocate( uint32_t nCount, uint64_t nRetireFence )
{
	DescriptorRangeDX12 out{};
	if ( !EnsureCapacity( nCount, nRetireFence ) )
		return out;
	out.index = m_nHead;
	out.count = nCount;
	out.generation = m_nGeneration;
	out.cpu = m_pHeap->GetCPUDescriptorHandleForHeapStart();
	out.cpu.ptr += static_cast<SIZE_T>( m_nHead ) * m_nStride;
	if ( m_bShaderVisible )
	{
		out.gpu = m_pHeap->GetGPUDescriptorHandleForHeapStart();
		out.gpu.ptr += static_cast<UINT64>( m_nHead ) * m_nStride;
	}
	m_nHead += nCount;
	m_nUsed += nCount;
	// Consecutive allocations for one fence retire together; the queue receives one entry per run.
	m_nHeapFence = MAX( nRetireFence, m_nHeapFence );
	if ( m_Pending.count && m_Pending.fence == nRetireFence && m_Pending.first + m_Pending.count == out.index )
		m_Pending.count += nCount;
	else
	{
		if ( m_Pending.count )
			m_Retired.Insert( m_Pending );
		m_Pending = { out.index, nCount, nRetireFence };
	}
	return out;
}

//-----------------------------------------------------------------------------
// Purpose: Allocation that is never reclaimed; it only pins the current heap
//          until nLastUseFence
//-----------------------------------------------------------------------------
DescriptorRangeDX12 CDescriptorAllocatorDX12::AllocatePersistent( uint32_t nCount, uint64_t nLastUseFence )
{
	DescriptorRangeDX12 out{};
	if ( !nCount || nCount > m_nCapacity - m_nHead )
		return out;
	out.index = m_nHead;
	out.count = nCount;
	out.generation = m_nGeneration;
	out.cpu = m_pHeap->GetCPUDescriptorHandleForHeapStart();
	out.cpu.ptr += static_cast<SIZE_T>( m_nHead ) * m_nStride;
	if ( m_bShaderVisible )
	{
		out.gpu = m_pHeap->GetGPUDescriptorHandleForHeapStart();
		out.gpu.ptr += static_cast<UINT64>( m_nHead ) * m_nStride;
	}
	m_nHead += nCount;
	m_nUsed += nCount;
	m_nHeapFence = MAX( nLastUseFence, m_nHeapFence );
	return out;
}

//-----------------------------------------------------------------------------
// Purpose: Frees runs and recycles heaps whose fence has completed
//-----------------------------------------------------------------------------
void CDescriptorAllocatorDX12::Reclaim( uint64_t nCompletedFence )
{
	if ( m_Pending.count && m_Pending.fence <= nCompletedFence )
	{
		m_Retired.Insert( m_Pending );
		m_Pending = {};
	}
	while ( !m_Retired.IsEmpty() && m_Retired.Head().fence <= nCompletedFence )
	{
		m_nUsed -= m_Retired.Head().count;
		m_Retired.RemoveAtHead();
	}
	while ( !m_RetiredHeaps.IsEmpty() && m_RetiredHeaps[m_RetiredHeaps.Head()].fence <= nCompletedFence )
	{
		const uint32_t hHead = m_RetiredHeaps.Head();
		m_AvailableHeaps.AddToTail( m_RetiredHeaps[hHead].heap.Detach() );
		m_RetiredHeaps.Remove( hHead );
	}
	if ( !m_nUsed )
	{
		m_nHead = 0;
		m_nHeapFence = 0;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Creates the transient, persistent and sampler heaps
//-----------------------------------------------------------------------------
bool CBindingCacheDX12::Initialize( ID3D12Device *pDevice )
{
	m_SamplerCache.RemoveAll();
	for ( int i = 0; i < ARRAYSIZE( m_SamplerIds ); ++i )
		m_SamplerIds[i] = SamplerIdEntry();
	m_nLastSamplerIndex = -1;
	m_pDevice = pDevice;
	m_nCompletedFence = m_nTouchClock = 0;
	return m_Resources.Initialize( pDevice, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 262144, true ) && m_PersistentResources.Initialize( pDevice, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 65536, false ) && m_Samplers.Initialize( pDevice, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 2048, true );
}

//-----------------------------------------------------------------------------
// Purpose: Releases every heap and forgets cached sampler tables
//-----------------------------------------------------------------------------
void CBindingCacheDX12::Shutdown()
{
	m_SamplerCache.RemoveAll();
	for ( int i = 0; i < ARRAYSIZE( m_SamplerIds ); ++i )
		m_SamplerIds[i] = SamplerIdEntry();
	m_nLastSamplerIndex = -1;
	m_Resources.Shutdown();
	m_PersistentResources.Shutdown();
	m_Samplers.Shutdown();
	m_pDevice = nullptr;
	m_nCompletedFence = m_nTouchClock = 0;
}

//-----------------------------------------------------------------------------
// Purpose: Persistent CPU-only resource and shader-visible sampler descriptors
//-----------------------------------------------------------------------------
DescriptorRangeDX12 CBindingCacheDX12::AllocatePersistentResource( uint32_t nCount, uint64_t nLastUseFence )
{
	return m_PersistentResources.AllocatePersistent( nCount, nLastUseFence );
}

DescriptorRangeDX12 CBindingCacheDX12::AllocatePersistentSampler( uint32_t nCount, uint64_t nLastUseFence )
{
	return m_Samplers.AllocatePersistent( nCount, nLastUseFence );
}

//-----------------------------------------------------------------------------
// Purpose: Returns a process-lifetime id for desc, or kUninternedSampler when full
//-----------------------------------------------------------------------------
uint16_t CBindingCacheDX12::InternSampler( const D3D12_SAMPLER_DESC &desc )
{
	// Id 0 is the canonical default sampler; ids are stable for the process lifetime.
	if ( m_InternedSamplers.IsEmpty() )
		m_InternedSamplers.AddToTail( DefaultSampler() );
	for ( int i = 0; i < m_InternedSamplers.Count(); ++i )
		if ( !memcmp( &m_InternedSamplers[i], &desc, sizeof( desc ) ) )
			return static_cast<uint16_t>( i );
	if ( m_InternedSamplers.Count() >= 0xffff )
		return kUninternedSampler;
	return static_cast<uint16_t>( m_InternedSamplers.AddToTail( desc ) );
}

//-----------------------------------------------------------------------------
// Purpose: Finds or creates a shader-visible 32-sampler table; least recently
//          touched completed entries are recycled when the heap is full
//-----------------------------------------------------------------------------
DescriptorRangeDX12 CBindingCacheDX12::AcquireSamplerTable( const D3D12_SAMPLER_DESC ( &samplers )[32], const uint16_t ( &ids )[32], uint64_t nFence )
{
	const auto touch = [&]( int nIndex )
	{
		SamplerEntry &entry = m_SamplerCache[nIndex];
		entry.lastUseFence = MAX( nFence, entry.lastUseFence );
		entry.lastTouch = ++m_nTouchClock;
		m_nLastSamplerIndex = nIndex;
		return entry.range;
	};
	// Interned ids identify each descriptor exactly; a 64-byte id table replaces hashing 32 descriptions.
	bool bInterned = true;
	for ( int i = 0; i < ARRAYSIZE( ids ); ++i )
		if ( ids[i] == kUninternedSampler )
		{
			bInterned = false;
			break;
		}
	SamplerIdEntry *pIdEntry = nullptr;
	if ( bInterned )
	{
		uint64_t nHash = 1469598103934665603ull;
		{
			const uint64_t *pWords = reinterpret_cast<const uint64_t *>( ids );
			for ( size_t i = 0; i < sizeof( ids ) / sizeof( uint64_t ); ++i )
			{
				nHash ^= pWords[i];
				nHash *= 1099511628211ull;
			}
		}
		pIdEntry = &m_SamplerIds[static_cast<size_t>( nHash ^ ( nHash >> 29 ) ) & ( ARRAYSIZE( m_SamplerIds ) - 1 )];
		if ( !memcmp( pIdEntry->ids, ids, sizeof( ids ) ) && pIdEntry->index >= 0 && pIdEntry->index < m_SamplerCache.Count() && m_SamplerCache[pIdEntry->index].generation == pIdEntry->generation )
			return touch( pIdEntry->index );
	}
	const auto remember = [&]( int nIndex )
	{
		if ( pIdEntry )
		{
			memcpy( pIdEntry->ids, ids, sizeof( ids ) );
			pIdEntry->index = nIndex;
			pIdEntry->generation = m_SamplerCache[nIndex].generation;
		}
		return touch( nIndex );
	};
	// Equal tables compare quickly; the previous table is the common hit, so try it before hashing.
	if ( m_nLastSamplerIndex >= 0 && !memcmp( m_SamplerCache[m_nLastSamplerIndex].samplers, samplers, sizeof( samplers ) ) )
		return remember( m_nLastSamplerIndex );
	// Four independent lanes keep the word hash latency-bound work short; a byte compare still decides equality.
	uint64_t lanes[4] = { 1469598103934665603ull, 0x9E3779B97F4A7C15ull, 0xC2B2AE3D27D4EB4Full, 0x165667B19E3779F9ull };
	{
		const uint64_t *pWords = reinterpret_cast<const uint64_t *>( samplers );
		constexpr size_t nWordCount = sizeof( samplers ) / sizeof( uint64_t );
		static_assert( nWordCount % 4 == 0, "sampler table size" );
		for ( size_t i = 0; i < nWordCount; i += 4 )
			for ( size_t nLane = 0; nLane < 4; ++nLane )
			{
				lanes[nLane] ^= pWords[i + nLane];
				lanes[nLane] *= 1099511628211ull;
			}
	}
	const uint64_t nHash = lanes[0] ^ ( lanes[1] << 1 ) ^ ( lanes[2] << 2 ) ^ ( lanes[3] << 3 );
	for ( int i = 0; i < m_SamplerCache.Count(); ++i )
		if ( i != m_nLastSamplerIndex && m_SamplerCache[i].hash == nHash && !memcmp( m_SamplerCache[i].samplers, samplers, sizeof( samplers ) ) )
			return remember( i );
	DescriptorRangeDX12 range = m_Samplers.AllocatePersistent( 32, nFence );
	int nIndex = -1;
	if ( range.count != 32 )
	{
		for ( int i = 0; i < m_SamplerCache.Count(); ++i )
			if ( m_SamplerCache[i].lastUseFence <= m_nCompletedFence && ( nIndex < 0 || m_SamplerCache[i].lastTouch < m_SamplerCache[nIndex].lastTouch ) )
				nIndex = i;
		if ( nIndex < 0 )
			return {};
		range = m_SamplerCache[nIndex].range;
	}
	else
		nIndex = m_SamplerCache.AddToTail();
	SamplerEntry &entry = m_SamplerCache[nIndex];
	memcpy( entry.samplers, samplers, sizeof( samplers ) );
	entry.range = range;
	entry.lastUseFence = nFence;
	entry.lastTouch = ++m_nTouchClock;
	entry.hash = nHash;
	entry.generation = ++m_nGenerationClock;
	const UINT nStride = m_pDevice->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER );
	D3D12_CPU_DESCRIPTOR_HANDLE cpu = range.cpu;
	for ( unsigned i = 0; i < 32; ++i )
	{
		m_pDevice->CreateSampler( &samplers[i], cpu );
		cpu.ptr += nStride;
	}
	return remember( nIndex );
}

//-----------------------------------------------------------------------------
// Purpose: Reclaims transient descriptors up to nCompletedFence
//-----------------------------------------------------------------------------
void CBindingCacheDX12::Reclaim( uint64_t nCompletedFence )
{
	if ( nCompletedFence <= m_nCompletedFence )
		return;
	m_nCompletedFence = nCompletedFence;
	m_Resources.Reclaim( nCompletedFence );
}
} // namespace shaderapidx12
