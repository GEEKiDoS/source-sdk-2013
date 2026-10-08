//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DX12 pipeline state cache, upload pages and per-recording draw binding filters.
//
//=============================================================================//

#ifndef PIPELINE_DX12_H
#define PIPELINE_DX12_H
#pragma once

#include "resources_dx12.h"
#include "bindings_dx12.h"
#include "command_recorder_dx12.h"
#include "tier1/utlhashtable.h"
#include "tier1/utlvector.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>

namespace shaderapidx12
{
struct PipelineKeyDX12
{
	uint64_t vs = 0, ps = 0, gs = 0, input = 0, vsVariant = 0, psVariant = 0, gsVariant = 0;
	DXGI_FORMAT color = DXGI_FORMAT_B8G8R8A8_UNORM, depth = DXGI_FORMAT_D24_UNORM_S8_UINT;
	uint32_t samples = 1, topology = 3, blend = 0, depthState = 0, raster = 0;
	DXGI_FORMAT colorFormats[4] = {};
	uint32_t colorCount = 1, sampleQuality = 0;
	bool frontCounterClockwise = false, wireframe = false, scissor = false, depthBias = false;
	int depthBiasValue = 0;
	float slopeScaledDepthBias = 0.f;
	uint32_t blendSource = 1, blendDestination = 0, blendAlphaSource = 1, blendAlphaDestination = 0, blendOperation = 0, blendAlphaOperation = 0, depthFunction = 3;
	uint32_t stencilFunction = 8, stencilFail = 1, stencilDepthFail = 1, stencilPass = 1;
	uint8_t stencilReadMask = 0xff, stencilWriteMask = 0xff;
	bool separateAlpha = false, depthTest = true, depthWrite = true, culling = true, colorWrites = true, alphaWrites = true, alphaToCoverage = false, stencil = false;
	bool lightingAbi = false, highresAbi = false;

	bool operator==( const PipelineKeyDX12 &o ) const { return vs == o.vs && ps == o.ps && gs == o.gs && vsVariant == o.vsVariant && psVariant == o.psVariant && gsVariant == o.gsVariant && input == o.input && color == o.color && !memcmp( colorFormats, o.colorFormats, sizeof( colorFormats ) ) && colorCount == o.colorCount && sampleQuality == o.sampleQuality && depth == o.depth && samples == o.samples && topology == o.topology && blend == o.blend && depthState == o.depthState && raster == o.raster && blendSource == o.blendSource && blendDestination == o.blendDestination && blendAlphaSource == o.blendAlphaSource && blendAlphaDestination == o.blendAlphaDestination && blendOperation == o.blendOperation && blendAlphaOperation == o.blendAlphaOperation && separateAlpha == o.separateAlpha && depthFunction == o.depthFunction && stencilFunction == o.stencilFunction && stencilFail == o.stencilFail && stencilDepthFail == o.stencilDepthFail && stencilPass == o.stencilPass && stencilReadMask == o.stencilReadMask && stencilWriteMask == o.stencilWriteMask && depthTest == o.depthTest && depthWrite == o.depthWrite && culling == o.culling && colorWrites == o.colorWrites && alphaWrites == o.alphaWrites && alphaToCoverage == o.alphaToCoverage && stencil == o.stencil && frontCounterClockwise == o.frontCounterClockwise && wireframe == o.wireframe && scissor == o.scissor && depthBias == o.depthBias && depthBiasValue == o.depthBiasValue && slopeScaledDepthBias == o.slopeScaledDepthBias && lightingAbi == o.lightingAbi && highresAbi == o.highresAbi; }
};

// Owned by the recording thread, like the command list it fills: every entry point runs on the current
// recording owner (draws, uploads, reclamation, shader retirement), so the caches take no locks.
class CPipelineCacheDX12
{
public:
	bool Initialize( ID3D12Device *pDevice );
	void Shutdown();
	void BindDrawState( CCommandRecorderDX12 *pList, const D3D12_VIEWPORT &viewport, const D3D12_RECT &scissor, uint64_t nRetireFence );

	D3D12_CPU_DESCRIPTOR_HANDLE NullShaderResourceView() const { return m_NullSrv; }
	bool LightingRootAvailable() const { return m_pLightingRoot.Get() != nullptr; }
	bool HighresRootAvailable() const { return m_pHighresRoot.Get() != nullptr; }

	void Reclaim( uint64_t nCompletedFence );
	bool UploadTransient( const void *pData, size_t nBytes, size_t nAllocationBytes, size_t nAlignment, uint64_t nRetireFence, D3D12_GPU_VIRTUAL_ADDRESS &nGpuAddress, const uint32_t *pSwapOffsets = nullptr, size_t nSwapCount = 0, size_t nVertexStride = 0 );
	// Borrowed upload resource, retained through nRetireFence; offset is a multiple of nStride.
	bool UploadStructured( const void *pData, size_t nBytes, uint32_t nStride, uint64_t nRetireFence, ID3D12Resource **ppResource, uint64_t *pOffset );
	void RetainExternalResource( ID3D12Resource *pResource, uint64_t nRetireFence );
	bool EnsureGeometryBuffer( CCommandRecorderDX12 *pList, CVertexBufferDX12 &buffer, size_t nUsedBytes, uint64_t nRetireFence, D3D12_GPU_VIRTUAL_ADDRESS &nGpuAddress, const uint32_t *pSwapOffsets = nullptr, size_t nSwapCount = 0, size_t nVertexStride = 0 );
	bool EnsureIndexBuffer( CCommandRecorderDX12 *pList, CIndexBufferDX12 &buffer, size_t nUsedBytes, uint64_t nRetireFence, D3D12_GPU_VIRTUAL_ADDRESS &nGpuAddress );

	struct BindingInputDX12
	{
		explicit BindingInputDX12( D3D12_CPU_DESCRIPTOR_HANDLE nullView )
		{
			for ( int i = 0; i < ARRAYSIZE( srvSources ); ++i )
				srvSources[i] = nullView;
		}

		const void *constantData[10] = {};
		size_t constantSizes[10] = {};
		uint64_t constantVersions[10] = {};
		uint64_t constantShaderIds[6] = {};
		uint32_t consumedRegisters[6] = {};
		// Native space-1 CBVs: slots 0-7 VS b0-b7, 8-15 PS b0-b7; only stages flagged in nativeStage are bound.
		const void *nativeData[16] = {};
		uint32_t nativeSizes[16] = {};
		uint64_t nativeVersions[16] = {};
		bool nativeStage[2] = {};
		ID3D12Resource *textures[32] = {};
		D3D12_CPU_DESCRIPTOR_HANDLE srvSources[32];
		// Descriptions are only read for slots whose source handle is zero; those slots must supply a description.
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDescs[32];
		D3D12_SAMPLER_DESC samplerDescs[32] = {};
		uint16_t samplerIds[32] = {};
		DescriptorRangeDX12 samplerTable{};
		bool lightingAbi = false;
		bool lightingVisibilityRequired = true;
		DescriptorRangeDX12 lightingViewTable{};
		DescriptorRangeDX12 lightingVisibilityTable{};
		D3D12_GPU_VIRTUAL_ADDRESS lightingViewConstants = 0;
		D3D12_GPU_VIRTUAL_ADDRESS propDrawConstants = 0, propTriangles = 0;
		bool highresAbi = false;
		DescriptorRangeDX12 highresTable{};
		D3D12_GPU_VIRTUAL_ADDRESS highresConstants = 0, highresFailure = 0;
		uint64_t retireFence = 0;
		// Whether the draw samples vertex textures / runs a geometry stage; unused root parameters may stay stale.
		bool vertexTextures = true, geometryStage = true;
		// Caller guarantees textures/srvSources equal the previous PrepareBindings call's input in this recording.
		bool texturesUnchanged = false;
	};

	bool PrepareBindings( CCommandRecorderDX12 *pList, const BindingInputDX12 &input );

	struct StatsDX12
	{
		uint64_t srvTableHits = 0, srvTableCopies = 0, constantHits = 0, constantUploads = 0, transientConstants = 0, rootCbvSets = 0, rootTableSets = 0, rootSrvSets = 0;
	};

	const StatsDX12 &Stats() const { return m_Stats; }

	// Changes whenever a cached PSO may have been released.
	uint64_t PipelineEpoch() const { return m_nPipelineEpoch; }

	// Uploads dynamic buffer contents once per (content version, recording fence).
	template <class Buffer>
	bool UploadDynamic( Buffer &buffer, size_t nBytes, size_t nAlignment, uint64_t nRetireFence, D3D12_GPU_VIRTUAL_ADDRESS &nGpuAddress, const uint32_t *pSwapOffsets = nullptr, size_t nSwapCount = 0, size_t nVertexStride = 0 )
	{
		typename Buffer::TransientUploadDX12 &last = buffer.TransientUpload();
		if ( last.fence == nRetireFence && last.version == buffer.ContentVersion() && last.epoch == m_nRetainEpoch && last.bytes == nBytes )
		{
			nGpuAddress = last.address;
			return true;
		}
		if ( !UploadTransient( buffer.Data().data(), nBytes, nBytes, nAlignment, nRetireFence, nGpuAddress, pSwapOffsets, nSwapCount, nVertexStride ) )
			return false;
		last = { buffer.ContentVersion(), nRetireFence, m_nRetainEpoch, nBytes, nGpuAddress };
		return true;
	}

	void ResetStats() { m_Stats = {}; }

	// Clear/blit passes that bind their own native graphics state must invalidate this draw state.
	void InvalidateGraphicsBindings()
	{
		m_bGraphicsBindingsValid = false;
		m_pBoundRoot = nullptr;
		m_bDrawStateValid = false;
		m_pBoundPipeline = nullptr;
		m_bBoundTargetsValid = false;
		m_bIaValid = false;
	}

	// Engine slots 0-15 and backend-reserved slots 16-17 are cached independently per recording fence.
	void BindInputAssembler( CCommandRecorderDX12 *pList, const D3D12_VERTEX_BUFFER_VIEW *pViews, UINT nCount, const D3D12_VERTEX_BUFFER_VIEW *pZero, const D3D12_VERTEX_BUFFER_VIEW *pSunCoordinates, D3D12_PRIMITIVE_TOPOLOGY topology, uint64_t nRetireFence )
	{
		const bool reset = !m_bIaValid || m_nIaFence != nRetireFence;
		if ( reset || m_nBoundVertexCount != nCount || memcmp( m_BoundVertexViews, pViews, sizeof( D3D12_VERTEX_BUFFER_VIEW ) * nCount ) )
		{
			if ( nCount )
				pList->IASetVertexBuffers( 0, nCount, pViews );
			memcpy( m_BoundVertexViews, pViews, sizeof( D3D12_VERTEX_BUFFER_VIEW ) * nCount );
			m_nBoundVertexCount = nCount;
		}
		const D3D12_VERTEX_BUFFER_VIEW reserved[2] = { pZero ? *pZero : D3D12_VERTEX_BUFFER_VIEW{}, pSunCoordinates ? *pSunCoordinates : D3D12_VERTEX_BUFFER_VIEW{} };
		for ( UINT i = 0; i < ARRAYSIZE( reserved ); ++i )
			if ( reset || memcmp( &m_BoundVertexViews[16 + i], &reserved[i], sizeof( reserved[i] ) ) )
			{
				// Explicitly clear an unused reserved slot; engine-prefix gaps never overwrite either cache entry.
				pList->IASetVertexBuffers( 16 + i, 1, &reserved[i] );
				m_BoundVertexViews[16 + i] = reserved[i];
			}
		if ( reset || m_BoundTopology != topology )
		{
			pList->IASetPrimitiveTopology( topology );
			m_BoundTopology = topology;
		}
		if ( reset )
		{
			m_bBoundIndexValid = false;
		}
		m_nIaFence = nRetireFence;
		m_bIaValid = true;
	}

	void BindIndexBuffer( CCommandRecorderDX12 *pList, const D3D12_INDEX_BUFFER_VIEW *pView, uint64_t nRetireFence )
	{
		// Field compare: the view was just built from separate stores, and a whole-struct reload would stall on them.
		const D3D12_INDEX_BUFFER_VIEW value = pView ? *pView : D3D12_INDEX_BUFFER_VIEW{};
		if ( !m_bIaValid || m_nIaFence != nRetireFence || !m_bBoundIndexValid || m_BoundIndexView.BufferLocation != value.BufferLocation || m_BoundIndexView.SizeInBytes != value.SizeInBytes || m_BoundIndexView.Format != value.Format )
		{
			pList->IASetIndexBuffer( pView );
			m_BoundIndexView = value;
			m_bBoundIndexValid = true;
		}
	}

	// Skips OMSetRenderTargets when the same views and resources are already bound in this recording.
	void BindRenderTargets( CCommandRecorderDX12 *pList, UINT nCount, const D3D12_CPU_DESCRIPTOR_HANDLE *pRtvs, ID3D12Resource *const *ppColors, const D3D12_CPU_DESCRIPTOR_HANDLE *pDsv, ID3D12Resource *pDepth, uint64_t nRetireFence )
	{
		bool same = m_bBoundTargetsValid && m_nBoundTargetsFence == nRetireFence && m_nBoundTargetCount == nCount && m_pBoundDepth == pDepth && m_BoundDsv.ptr == ( pDsv ? pDsv->ptr : 0 );
		for ( UINT i = 0; same && i < nCount; ++i )
			same = m_BoundRtvs[i].ptr == pRtvs[i].ptr && m_BoundColors[i] == ppColors[i];
		if ( same )
			return;
		pList->OMSetRenderTargets( nCount, nCount ? pRtvs : nullptr, FALSE, pDsv );
		m_nBoundTargetCount = nCount;
		for ( UINT i = 0; i < nCount; ++i )
		{
			m_BoundRtvs[i] = pRtvs[i];
			m_BoundColors[i] = ppColors[i];
		}
		m_BoundDsv.ptr = pDsv ? pDsv->ptr : 0;
		m_pBoundDepth = pDepth;
		m_nBoundTargetsFence = nRetireFence;
		m_bBoundTargetsValid = true;
	}

	// Shader-visible transient descriptors retired with the recording fence, plus a persistent linear-clamp sampler.
	DescriptorRangeDX12 AllocateTransientResources( uint32_t nCount, uint64_t nRetireFence ) { return m_Bindings.AllocateDescriptors( nCount, nRetireFence ); }
	// Call before building related tables; subsequent allocations totaling nCount cannot roll over.
	bool ReserveResourceDescriptors( uint32_t nCount, uint64_t nRetireFence ) { return m_Bindings.ResourceHeap().EnsureCapacity( nCount, nRetireFence ); }
	uint64_t ResourceHeapGeneration() const { return m_Bindings.ResourceHeap().Generation(); }

	D3D12_GPU_DESCRIPTOR_HANDLE LinearClampSampler() const { return m_LinearClampSampler; }

	ID3D12DescriptorHeap *ResourceDescriptorHeap() { return m_Bindings.ResourceHeap().Heap(); }

	ID3D12DescriptorHeap *SamplerDescriptorHeap() { return m_Bindings.SamplerHeap().Heap(); }

	void BindPipelineState( CCommandRecorderDX12 *pList, ID3D12PipelineState *pPipeline, UINT nStencilReference, uint64_t nRetireFence )
	{
		const bool reset = !m_pBoundPipeline || m_nBoundPipelineFence != nRetireFence;
		if ( reset || m_pBoundPipeline != pPipeline )
			pList->SetPipelineState( pPipeline );
		if ( reset || m_nBoundStencilReference != nStencilReference )
			pList->OMSetStencilRef( nStencilReference );
		m_pBoundPipeline = pPipeline;
		m_nBoundStencilReference = nStencilReference;
		m_nBoundPipelineFence = nRetireFence;
	}

	// Bind the returned table in the same recording batch; a submission requires a new reservation.
	static D3D12_SAMPLER_DESC DefaultSamplerDesc() { return CBindingCacheDX12::DefaultSampler(); }

	// Unset descriptions (AddressU==0) canonicalize to the default sampler, id 0.
	uint16_t InternSampler( const D3D12_SAMPLER_DESC &desc ) { return desc.AddressU == 0 ? 0 : m_Bindings.InternSampler( desc ); }

	DescriptorRangeDX12 PrepareSamplerTable( const D3D12_SAMPLER_DESC ( &samplers )[32], const uint16_t ( &ids )[32], uint64_t nRetireFence );
	D3D12_CPU_DESCRIPTOR_HANDLE AcquireResourceDescriptor( uint64_t nLastUseFence );
	void ReleaseResourceDescriptor( D3D12_CPU_DESCRIPTOR_HANDLE descriptor, uint64_t nRetireFence );
	void NotifyShaderDestroyed( uint64_t nShaderIdentity );
	ID3D12PipelineState *GetOrCreate( const PipelineKeyDX12 &key, const D3D12_SHADER_BYTECODE &vs, const D3D12_SHADER_BYTECODE &ps, const D3D12_SHADER_BYTECODE &gs, const D3D12_INPUT_LAYOUT_DESC &layout, uint64_t nRetireFence );

private:
	struct Entry
	{
		PipelineKeyDX12 key;
		Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
		uint64_t lastUseFence = 0;
		bool destroyed = false;
	};

	struct UploadPage
	{
		Microsoft::WRL::ComPtr<ID3D12Resource> resource;
		unsigned char *mapped = nullptr;
		D3D12_GPU_VIRTUAL_ADDRESS gpu = 0;
		size_t capacity = 0, used = 0;
		uint64_t fence = 0;
	};

	struct RetiredResource
	{
		Microsoft::WRL::ComPtr<ID3D12Resource> resource;
		uint64_t fence = 0;
	};

	// Largest legal cbuffer; also the read slack kept past every upload page used for root CBVs.
	static constexpr size_t kConstantBufferMaxBytes = 65536;
	// 0-3 SRV/sampler tables, 4-7 VS b0-b3, 8-13 PS b0-b5, 14-17 GS b0-b3 root CBVs (space 0); 18/19 VS/PS space-1 CBV tables.
	static constexpr UINT kRootVertexConstants = 4, kRootPixelConstants = 8, kRootGeometryConstants = 14, kRootNativeVertex = 18, kRootNativePixel = 19, kRootParameterCount = 20;
	static constexpr UINT kRootLightingView = 20, kRootLightingViewConstants = 21, kRootLightingVisibility = 22, kRootPropDraw = 23, kRootPropTriangles = 24, kLightingRootParameterCount = 25;
	static constexpr UINT kRootHighresTable = 25, kRootHighresConstants = 26, kRootHighresFailure = 27, kHighresRootParameterCount = 28;
	bool AllocateUploadLocked( const void *pData, size_t nBytes, size_t nAllocationBytes, size_t nAlignment, uint64_t nRetireFence, D3D12_GPU_VIRTUAL_ADDRESS &nGpuAddress, ID3D12Resource **ppSource, size_t *pSourceOffset, const uint32_t *pSwapOffsets, size_t nSwapCount, size_t nVertexStride );
	void RetainGeometryLocked( ID3D12Resource *pResource, uint64_t nRetireFence );
	CBindingCacheDX12 m_Bindings;
	CUtlVector<UploadPage> m_UploadPages;
	int m_nUploadPageHint = 0;
	CUtlHashtable<ID3D12Resource *, RetiredResource> m_GeometryInFlight;

	struct RetiredDescriptor
	{
		D3D12_CPU_DESCRIPTOR_HANDLE descriptor;
		uint64_t fence;
	};

	CUtlVector<RetiredDescriptor> m_FreeResourceDescriptors; // FIFO, fence order

	struct CachedSrvTable
	{
		D3D12_CPU_DESCRIPTOR_HANDLE sources[32] = {};
		ID3D12Resource *resources[32] = {};
		D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
		uint64_t fence = 0, heapGeneration = 0, descriptorEpoch = 0;
	};

	CachedSrvTable m_SrvTables[64];
	uint64_t m_nSrvDescriptorEpoch = 1;
	CachedSrvTable m_LastSrvTable{};

	struct LastConstantSlot
	{
		uint64_t fence = 0, version = 0, shaderId = 0;
		size_t bytes = 0;
		D3D12_GPU_VIRTUAL_ADDRESS address = 0;
	};

	LastConstantSlot m_LastConstantSlots[10];
	static constexpr size_t kRecentConstants = 64;
	LastConstantSlot m_RecentConstants[10][kRecentConstants];
	D3D12_GPU_DESCRIPTOR_HANDLE m_BoundRootTables[4] = {};
	D3D12_GPU_DESCRIPTOR_HANDLE m_BoundNativeTables[2] = {};
	D3D12_GPU_DESCRIPTOR_HANDLE m_BoundLightingViewTable{};
	D3D12_GPU_DESCRIPTOR_HANDLE m_BoundLightingVisibilityTable{};
	D3D12_GPU_VIRTUAL_ADDRESS m_nBoundLightingViewConstants = 0;
	D3D12_GPU_DESCRIPTOR_HANDLE m_BoundHighresTable{};
	D3D12_GPU_VIRTUAL_ADDRESS m_nBoundHighresConstants = 0, m_nBoundHighresFailure = 0;
	LastConstantSlot m_LastNativeSlots[16];

	struct NativeTableCache
	{
		D3D12_GPU_VIRTUAL_ADDRESS addresses[8] = {};
		UINT sizes[8] = {};
		D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
		uint64_t fence = 0, heapGeneration = 0;
	};

	NativeTableCache m_NativeTableCache[2];
	D3D12_GPU_VIRTUAL_ADDRESS m_BoundRootConstants[kRootNativeVertex - kRootVertexConstants] = {};
	D3D12_GPU_VIRTUAL_ADDRESS m_BoundPropDraw = 0, m_BoundPropTriangles = 0;
	ID3D12DescriptorHeap *m_pBoundResourceHeap = nullptr, *m_pBoundSamplerHeap = nullptr;
	ID3D12RootSignature *m_pBoundRoot = nullptr;
	uint64_t m_nGraphicsBindingsFence = 0;
	bool m_bGraphicsBindingsValid = false;
	D3D12_VIEWPORT m_BoundViewport{};
	D3D12_RECT m_BoundScissor{};
	uint64_t m_nDrawStateFence = 0;
	bool m_bDrawStateValid = false;
	ID3D12PipelineState *m_pBoundPipeline = nullptr;
	uint64_t m_nBoundPipelineFence = 0;
	UINT m_nBoundStencilReference = 0;
	D3D12_CPU_DESCRIPTOR_HANDLE m_BoundRtvs[8] = {};
	ID3D12Resource *m_BoundColors[8] = {};
	D3D12_CPU_DESCRIPTOR_HANDLE m_BoundDsv{};
	ID3D12Resource *m_pBoundDepth = nullptr;
	UINT m_nBoundTargetCount = 0;
	uint64_t m_nBoundTargetsFence = 0;
	bool m_bBoundTargetsValid = false;
	bool m_bVertexTablesCurrent = false, m_bGeometryConstantsCurrent = false;
	D3D12_GPU_DESCRIPTOR_HANDLE m_LinearClampSampler{};
	StatsDX12 m_Stats{};
	uint64_t m_nPipelineEpoch = 1;
	D3D12_VERTEX_BUFFER_VIEW m_BoundVertexViews[18] = {};
	D3D12_INDEX_BUFFER_VIEW m_BoundIndexView{};
	D3D12_PRIMITIVE_TOPOLOGY m_BoundTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
	UINT m_nBoundVertexCount = 0;
	uint64_t m_nIaFence = 0;
	bool m_bIaValid = false, m_bBoundIndexValid = false;
	uint64_t m_nLastReclaimedFence = 0;
	// Invalidates per-buffer retention marks when m_GeometryInFlight is purged.
	uint64_t m_nRetainEpoch = 1;
	bool m_bReclaimDirty = true;
	ID3D12Device *m_pDevice = nullptr;
	D3D12_CPU_DESCRIPTOR_HANDLE m_NullSrv{};
	Microsoft::WRL::ComPtr<ID3D12Resource> m_pZeroConstants;
	D3D12_GPU_VIRTUAL_ADDRESS m_nZeroConstantAddress = 0;
	D3D12_SHADER_RESOURCE_VIEW_DESC m_NullSrvDesc{};
	SIZE_T m_nResourceStride = 0, m_nSamplerStride = 0;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_pRoot, m_pLightingRoot, m_pHighresRoot;
	CUtlVector<Entry> m_Entries;
	int m_nLastPipelineIndex = -1;
	// Index plus one; a collision only falls back to the complete-key search.
	uint32_t m_PipelineHints[256] = {};
	// m_LastSrvTable was produced or matched by the most recent PrepareBindings call.
	bool m_bLastSrvTableIsPrevious = false;
};
} // namespace shaderapidx12

#endif // PIPELINE_DX12_H
