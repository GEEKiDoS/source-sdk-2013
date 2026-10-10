//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DX12 pipeline state cache, upload pages and per-recording draw binding filters.
//
//=============================================================================//

#include "pipeline_dx12.h"
#include "shaderapi/ishadershadow.h"
#include "shaderapi/ishaderapidx12lighting.h"
#include "tier0/dbg.h"
#include "tracy_dx12.h"
#include <utility>

namespace shaderapidx12
{

//-----------------------------------------------------------------------------
// Purpose: Creates descriptor heaps, the null SRV, the linear-clamp sampler, the
//          zero constant buffer and ordinary/lighting root signatures
//-----------------------------------------------------------------------------
bool CPipelineCacheDX12::Initialize( ID3D12Device *pDevice )
{
	Shutdown();
	m_pDevice = pDevice;
	m_nResourceStride = pDevice->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
	m_nSamplerStride = pDevice->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER );
	if ( !m_Bindings.Initialize( pDevice ) )
		return false;
	++m_nSrvDescriptorEpoch;
	InvalidateGraphicsBindings();
	// Fence values restart with a new device; drop every fence-keyed reuse record.
	m_LastSrvTable = CachedSrvTable();
	memset( m_LastConstantSlots, 0, sizeof( m_LastConstantSlots ) );
	memset( m_LastNativeSlots, 0, sizeof( m_LastNativeSlots ) );
	memset( m_NativeTableCache, 0, sizeof( m_NativeTableCache ) );
	memset( m_SrvTables, 0, sizeof( m_SrvTables ) );
	memset( m_RecentConstants, 0, sizeof( m_RecentConstants ) );
	m_nUploadPageHint = 0;
	m_NullSrvDesc = {};
	m_NullSrvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	m_NullSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	m_NullSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	m_NullSrvDesc.Texture2D.MipLevels = 1;
	const DescriptorRangeDX12 nullViews = m_Bindings.AllocatePersistentResource( 1, 0 );
	if ( nullViews.count != 1 )
		return false;
	m_NullSrv = nullViews.cpu;
	m_pDevice->CreateShaderResourceView( nullptr, &m_NullSrvDesc, m_NullSrv );
	{
		const DescriptorRangeDX12 sampler = m_Bindings.AllocatePersistentSampler( 1, 0 );
		if ( sampler.count != 1 )
			return false;
		D3D12_SAMPLER_DESC linear{};
		linear.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		linear.AddressU = linear.AddressV = linear.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		linear.MaxAnisotropy = 1;
		linear.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		linear.MaxLOD = D3D12_FLOAT32_MAX;
		m_pDevice->CreateSampler( &linear, sampler.cpu );
		m_LinearClampSampler = sampler.gpu;
	}
	{
		// Empty constant banks bind this zero buffer; it spans the largest possible cbuffer.
		D3D12_HEAP_PROPERTIES heap{};
		heap.Type = D3D12_HEAP_TYPE_UPLOAD;
		D3D12_RESOURCE_DESC zeroDesc{};
		zeroDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		zeroDesc.Width = kConstantBufferMaxBytes;
		zeroDesc.Height = 1;
		zeroDesc.DepthOrArraySize = 1;
		zeroDesc.MipLevels = 1;
		zeroDesc.SampleDesc.Count = 1;
		zeroDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		if ( FAILED( m_pDevice->CreateCommittedResource( &heap, D3D12_HEAP_FLAG_NONE, &zeroDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS( &m_pZeroConstants ) ) ) )
			return false;
		void *pMapped = nullptr;
		D3D12_RANGE read{};
		if ( FAILED( m_pZeroConstants->Map( 0, &read, &pMapped ) ) )
			return false;
		memset( pMapped, 0, kConstantBufferMaxBytes );
		m_pZeroConstants->Unmap( 0, nullptr );
		m_nZeroConstantAddress = m_pZeroConstants->GetGPUVirtualAddress();
	}
	// Ranges: SRV t0-15, sampler s0-15, native CBV b0-b7 in register space 1; [4] lighting visibility, [5] highres, [6] PBR
	// projected lights t0-16 space 5, [7] ambient probes t0-8 space 4.
	D3D12_DESCRIPTOR_RANGE ranges[8]{};
	ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	ranges[0].NumDescriptors = 16;
	ranges[0].BaseShaderRegister = 0;
	ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
	ranges[1].NumDescriptors = 16;
	ranges[1].BaseShaderRegister = 0;
	ranges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
	ranges[2].NumDescriptors = 8;
	ranges[2].BaseShaderRegister = 0;
	ranges[2].RegisterSpace = 1;
	// 0-3: PS SRV/sampler and VS SRV/sampler tables; 4-7 VS b0-b3, 8-13 PS b0-b5 and 14-17 GS b0-b3 root CBVs; 18/19 VS/PS space-1 CBV tables; 20 PS space-5 PBR table.
	D3D12_ROOT_PARAMETER params[kHighresRootParameterCount]{};
	for ( int i = 0; i < 4; ++i )
	{
		params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		params[i].DescriptorTable.NumDescriptorRanges = 1;
		params[i].DescriptorTable.pDescriptorRanges = &ranges[i & 1];
		params[i].ShaderVisibility = i < 2 ? D3D12_SHADER_VISIBILITY_PIXEL : D3D12_SHADER_VISIBILITY_VERTEX;
	}
	for ( UINT i = kRootVertexConstants; i < kRootNativeVertex; ++i )
	{
		params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		if ( i < kRootPixelConstants )
		{
			params[i].Descriptor.ShaderRegister = i - kRootVertexConstants;
			params[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
		}
		else if ( i < kRootGeometryConstants )
		{
			params[i].Descriptor.ShaderRegister = i - kRootPixelConstants;
			params[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		}
		else
		{
			params[i].Descriptor.ShaderRegister = i - kRootGeometryConstants;
			params[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_GEOMETRY;
		}
	}
	for ( UINT i = kRootNativeVertex; i < kRootPbrSpots; ++i )
	{
		params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		params[i].DescriptorTable.NumDescriptorRanges = 1;
		params[i].DescriptorTable.pDescriptorRanges = &ranges[2];
		params[i].ShaderVisibility = i == kRootNativeVertex ? D3D12_SHADER_VISIBILITY_VERTEX : D3D12_SHADER_VISIBILITY_PIXEL;
	}
	ranges[6].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	ranges[6].NumDescriptors = DX12_PBR_TABLE_COUNT;
	ranges[6].RegisterSpace = DX12_PBR_REGISTER_SPACE;
	params[kRootPbrSpots].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	params[kRootPbrSpots].DescriptorTable = { 1, &ranges[6] };
	params[kRootPbrSpots].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	// Static samplers accumulate: each signature binds the prefix its parameters need, so its count is the number added so far.
	D3D12_STATIC_SAMPLER_DESC samplers[5]{};
	UINT samplerCount = 0;
	const auto addSampler = [&]( D3D12_FILTER filter, D3D12_COMPARISON_FUNC comparison, UINT shaderRegister, UINT space )
	{
		D3D12_STATIC_SAMPLER_DESC &sampler = samplers[samplerCount++];
		sampler.Filter = filter;
		sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		sampler.MaxAnisotropy = 1;
		sampler.ComparisonFunc = comparison;
		sampler.MaxLOD = D3D12_FLOAT32_MAX;
		sampler.ShaderRegister = shaderRegister;
		sampler.RegisterSpace = space;
		sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	};
	addSampler( D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_COMPARISON_FUNC_ALWAYS, DX12_PBR_S_LINEAR, DX12_PBR_REGISTER_SPACE );
	addSampler( D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR, D3D12_COMPARISON_FUNC_LESS_EQUAL, DX12_PBR_S_COMPARISON, DX12_PBR_REGISTER_SPACE );
	D3D12_ROOT_SIGNATURE_DESC desc{};
	desc.NumParameters = kRootParameterCount;
	desc.pParameters = params;
	desc.NumStaticSamplers = samplerCount;
	desc.pStaticSamplers = samplers;
	desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	Microsoft::WRL::ComPtr<ID3DBlob> blob, error;
	HRESULT hr = D3D12SerializeRootSignature( &desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error );
	if ( FAILED( hr ) )
	{
		Warning( "ShaderAPIDX12: root signature serialization failed 0x%08x: %s\n", static_cast<unsigned>( hr ), error ? static_cast<const char *>( error->GetBufferPointer() ) : "unknown" );
		return false;
	}
	hr = m_pDevice->CreateRootSignature( 0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS( &m_pRoot ) );
	if ( FAILED( hr ) )
	{
		Warning( "ShaderAPIDX12: root signature creation failed 0x%08x\n", static_cast<unsigned>( hr ) );
		return false;
	}

	D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
	if ( FAILED( m_pDevice->CheckFeatureSupport( D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof( options ) ) ) || options.ResourceBindingTier < D3D12_RESOURCE_BINDING_TIER_2 )
		return true; // Ordinary rendering does not require the lighting signature.
	// ABI 6 adds per-draw static-prop constants/triangles without coupling model draws to lightmap pages.
	D3D12_DESCRIPTOR_RANGE lightingRanges[2]{};
	lightingRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	lightingRanges[0].NumDescriptors = DX12_LIGHTING_VIEW_TABLE_COUNT;
	lightingRanges[0].BaseShaderRegister = DX12_LIGHTING_T_LOCAL_ATLAS_FIRST;
	lightingRanges[0].RegisterSpace = DX12_LIGHTING_REGISTER_SPACE;
	lightingRanges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	lightingRanges[1].NumDescriptors = 4;
	lightingRanges[1].BaseShaderRegister = DX12_LIGHTING_T_VISIBILITY_FACES;
	lightingRanges[1].RegisterSpace = DX12_LIGHTING_REGISTER_SPACE;
	lightingRanges[1].OffsetInDescriptorsFromTableStart = DX12_LIGHTING_T_VISIBILITY_FACES;
	params[kRootLightingView].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	params[kRootLightingView].DescriptorTable.NumDescriptorRanges = 2;
	params[kRootLightingView].DescriptorTable.pDescriptorRanges = lightingRanges;
	params[kRootLightingView].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	params[kRootLightingViewConstants].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	params[kRootLightingViewConstants].Descriptor.ShaderRegister = DX12_LIGHTING_B_VIEW;
	params[kRootLightingViewConstants].Descriptor.RegisterSpace = DX12_LIGHTING_REGISTER_SPACE;
	params[kRootLightingViewConstants].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	ranges[4].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	ranges[4].NumDescriptors = 1;
	ranges[4].BaseShaderRegister = DX12_LIGHTING_T_SUN_VISIBILITY;
	ranges[4].RegisterSpace = DX12_LIGHTING_REGISTER_SPACE;
	params[kRootLightingVisibility].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	params[kRootLightingVisibility].DescriptorTable = { 1, &ranges[4] };
	params[kRootLightingVisibility].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	params[kRootPropDraw].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	params[kRootPropDraw].Descriptor.ShaderRegister = DX12_LIGHTING_B_PROP_DRAW;
	params[kRootPropDraw].Descriptor.RegisterSpace = DX12_LIGHTING_REGISTER_SPACE;
	params[kRootPropDraw].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	params[kRootPropTriangles].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
	params[kRootPropTriangles].Descriptor.ShaderRegister = DX12_LIGHTING_T_PROP_TRIANGLES;
	params[kRootPropTriangles].Descriptor.RegisterSpace = DX12_LIGHTING_REGISTER_SPACE;
	params[kRootPropTriangles].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	ranges[7].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	ranges[7].NumDescriptors = DX12_PROBE_TABLE_COUNT;
	ranges[7].RegisterSpace = DX12_PROBE_REGISTER_SPACE;
	params[kRootProbeTable].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	params[kRootProbeTable].DescriptorTable = { 1, &ranges[7] };
	params[kRootProbeTable].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	params[kRootProbeConstants].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	params[kRootProbeConstants].Descriptor.RegisterSpace = DX12_PROBE_REGISTER_SPACE;
	params[kRootProbeConstants].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	addSampler( D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR, D3D12_COMPARISON_FUNC_LESS_EQUAL, DX12_LIGHTING_S_COMPARISON, DX12_LIGHTING_REGISTER_SPACE );
	addSampler( D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_COMPARISON_FUNC_ALWAYS, 0, DX12_PROBE_REGISTER_SPACE );
	desc.NumParameters = kLightingRootParameterCount;
	desc.NumStaticSamplers = samplerCount;
	blob.Reset();
	error.Reset();
	hr = D3D12SerializeRootSignature( &desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error );
	if ( FAILED( hr ) )
	{
		Warning( "ShaderAPIDX12: lighting root signature serialization failed 0x%08x: %s\n", static_cast<unsigned>( hr ), error ? static_cast<const char *>( error->GetBufferPointer() ) : "unknown" );
		return true; // ValidateMap rejects lighting while the ordinary signature remains usable.
	}
	hr = m_pDevice->CreateRootSignature( 0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS( &m_pLightingRoot ) );
	if ( FAILED( hr ) )
		Warning( "ShaderAPIDX12: lighting root signature creation failed 0x%08x\n", static_cast<unsigned>( hr ) );
	if ( !m_pLightingRoot )
		return true;
	ranges[5].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	ranges[5].NumDescriptors = 8;
	ranges[5].RegisterSpace = 3;
	params[kRootHighresTable].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	params[kRootHighresTable].DescriptorTable = { 1, &ranges[5] };
	params[kRootHighresTable].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	params[kRootHighresConstants].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	params[kRootHighresConstants].Descriptor.RegisterSpace = 3;
	params[kRootHighresConstants].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	params[kRootHighresFailure].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
	params[kRootHighresFailure].Descriptor.RegisterSpace = 3;
	params[kRootHighresFailure].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	addSampler( D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_COMPARISON_FUNC_ALWAYS, 0, 3 );
	desc.NumParameters = kHighresRootParameterCount;
	desc.NumStaticSamplers = samplerCount;
	blob.Reset();
	error.Reset();
	hr = D3D12SerializeRootSignature( &desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error );
	if ( SUCCEEDED( hr ) )
		hr = m_pDevice->CreateRootSignature( 0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS( &m_pHighresRoot ) );
	if ( FAILED( hr ) )
		Warning( "ShaderAPIDX12: highres root signature creation failed 0x%08x: %s\n", static_cast<unsigned>( hr ), error ? static_cast<const char *>( error->GetBufferPointer() ) : "unknown" );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Releases every device object; the cache may be re-initialized
//-----------------------------------------------------------------------------
void CPipelineCacheDX12::Shutdown()
{
	m_Entries.Purge();
	++m_nPipelineEpoch;
	m_nLastPipelineIndex = -1;
	memset( m_PipelineHints, 0, sizeof( m_PipelineHints ) );
	m_FreeResourceDescriptors.Purge();
	m_GeometryInFlight.Purge();
	++m_nRetainEpoch;
	for ( int i = 0; i < m_UploadPages.Count(); ++i )
		if ( m_UploadPages[i].mapped )
			m_UploadPages[i].resource->Unmap( 0, nullptr );
	m_UploadPages.Purge();
	m_pZeroConstants.Reset();
	m_nZeroConstantAddress = 0;
	m_Bindings.Shutdown();
	m_pRoot.Reset();
	m_pLightingRoot.Reset();
	m_pHighresRoot.Reset();
	m_pBoundRoot = nullptr;
	m_pDevice = nullptr;
	m_NullSrv = {};
	m_LinearClampSampler = {};
	m_nResourceStride = m_nSamplerStride = 0;
	m_nLastReclaimedFence = 0;
	m_bReclaimDirty = true;
}

//-----------------------------------------------------------------------------
// Purpose: Persistent CPU-only SRV slot; released slots are recycled by fence
//-----------------------------------------------------------------------------
D3D12_CPU_DESCRIPTOR_HANDLE CPipelineCacheDX12::AcquireResourceDescriptor( uint64_t nLastUseFence )
{
	// Released slots are reused only after the fence that could still replay a copy from them completes.
	if ( m_FreeResourceDescriptors.Count() && m_FreeResourceDescriptors.Head().fence <= m_nLastReclaimedFence )
	{
		const D3D12_CPU_DESCRIPTOR_HANDLE descriptor = m_FreeResourceDescriptors.Head().descriptor;
		m_FreeResourceDescriptors.Remove( 0 );
		return descriptor;
	}
	return m_Bindings.AllocatePersistentResource( 1, nLastUseFence ).cpu;
}

void CPipelineCacheDX12::ReleaseResourceDescriptor( D3D12_CPU_DESCRIPTOR_HANDLE descriptor, uint64_t nRetireFence )
{
	// Recorded table copies read these CPU-only source slots at replay; retire them by fence.
	if ( descriptor.ptr )
	{
		m_FreeResourceDescriptors.AddToTail( { descriptor, nRetireFence } );
		++m_nSrvDescriptorEpoch;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Sub-allocates from fence-retired write-back upload pages, optionally
//          swapping red/blue of vertex colors while copying
//-----------------------------------------------------------------------------
bool CPipelineCacheDX12::AllocateUploadLocked( const void *pData, size_t nBytes, size_t nAllocationBytes, size_t nAlignment, uint64_t nRetireFence, D3D12_GPU_VIRTUAL_ADDRESS &nGpuAddress, ID3D12Resource **ppSource, size_t *pSourceOffset, const uint32_t *pSwapOffsets, size_t nSwapCount, size_t nVertexStride )
{
	if ( !pData || !nBytes || nAllocationBytes < nBytes || !nAlignment )
		return false;
	if ( nSwapCount && ( !pSwapOffsets || !nVertexStride || nBytes % nVertexStride ) )
		return false;
	for ( size_t i = 0; i < nSwapCount; ++i )
		if ( pSwapOffsets[i] > nVertexStride || nVertexStride - pSwapOffsets[i] < 4 )
			return false;
	// Pages are cached (write-back) system memory, like readback heaps: the per-draw constant/geometry copies and
	// compare-free rewrites are cheaper than write-combined stores, and the GPU reads them coherently over the bus.
	UploadPage *chosen = nullptr;
	size_t offset = 0;
	// Try the page that served the previous allocation before scanning; pages fill front to back.
	const auto fits = [&]( UploadPage &page )
	{
		// Structured records may have a non-power-of-two stride; ordinary uploads keep bitmask alignment.
		size_t padding;
		if ( nAlignment & ( nAlignment - 1 ) )
		{
			const size_t remainder = page.used % nAlignment;
			padding = remainder ? nAlignment - remainder : 0;
		}
		else
			padding = -page.used & ( nAlignment - 1 );
		if ( page.used <= page.capacity && padding <= page.capacity - page.used && page.capacity - page.used - padding >= nAllocationBytes )
		{
			const size_t start = page.used + padding;
			chosen = &page;
			offset = start;
			return true;
		}
		return false;
	};
	if ( m_nUploadPageHint < m_UploadPages.Count() && fits( m_UploadPages[m_nUploadPageHint] ) )
	{
	}
	else
		for ( int i = 0; i < m_UploadPages.Count(); ++i )
			if ( fits( m_UploadPages[i] ) )
			{
				m_nUploadPageHint = i;
				break;
			}
	if ( !chosen )
	{
		D3D12_HEAP_PROPERTIES heap{};
		heap.Type = D3D12_HEAP_TYPE_CUSTOM;
		heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
		heap.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
		D3D12_RESOURCE_DESC desc{};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		desc.Width = MAX( size_t( 4u * 1024u * 1024u ), ( nAllocationBytes + 65535u ) & ~size_t( 65535u ) ) + kConstantBufferMaxBytes;
		desc.Height = 1;
		desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.SampleDesc.Count = 1;
		desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		UploadPage page{};
		if ( FAILED( m_pDevice->CreateCommittedResource( &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS( &page.resource ) ) ) )
			return false;
		void *pMapped = nullptr;
		D3D12_RANGE read{};
		if ( FAILED( page.resource->Map( 0, &read, &pMapped ) ) )
			return false;
		page.mapped = static_cast<unsigned char *>( pMapped );
		page.gpu = page.resource->GetGPUVirtualAddress();
		page.capacity = static_cast<size_t>( desc.Width ) - kConstantBufferMaxBytes;
		*m_UploadPages.AddToTailGetPtr() = page;
		chosen = &m_UploadPages.Tail();
		m_nUploadPageHint = m_UploadPages.Count() - 1;
	}
	memcpy( chosen->mapped + offset, pData, nBytes );
	if ( nSwapCount )
	{
		ZoneNamedN( ___tracy_scoped_zone, "DX12 UploadColorSwap", DX12_DRAW_ZONES_ACTIVE );
		// Convert from the CPU source; the destination is only written.
		const unsigned char *pSourceBytes = static_cast<const unsigned char *>( pData );
		for ( size_t vertex = 0; vertex < nBytes / nVertexStride; ++vertex )
			for ( size_t i = 0; i < nSwapCount; ++i )
			{
				const size_t colorOffset = vertex * nVertexStride + pSwapOffsets[i];
				uint32_t color;
				memcpy( &color, pSourceBytes + colorOffset, sizeof( color ) );
				color = ( color & 0xff00ff00u ) | ( ( color & 0x00ff0000u ) >> 16 ) | ( ( color & 0x000000ffu ) << 16 );
				memcpy( chosen->mapped + offset + colorOffset, &color, sizeof( color ) );
			}
	}
	if ( nAllocationBytes > nBytes )
		memset( chosen->mapped + offset + nBytes, 0, nAllocationBytes - nBytes );
	chosen->used = offset + nAllocationBytes;
	chosen->fence = MAX( nRetireFence, chosen->fence );
	nGpuAddress = chosen->gpu + offset;
	if ( ppSource )
		*ppSource = chosen->resource.Get();
	if ( pSourceOffset )
		*pSourceOffset = offset;
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Transient upload retired with retireFence
//-----------------------------------------------------------------------------
bool CPipelineCacheDX12::UploadTransient( const void *pData, size_t nBytes, size_t nAllocationBytes, size_t nAlignment, uint64_t nRetireFence, D3D12_GPU_VIRTUAL_ADDRESS &nGpuAddress, const uint32_t *pSwapOffsets, size_t nSwapCount, size_t nVertexStride )
{
	if ( nAlignment & ( nAlignment - 1 ) )
		return false;
	return AllocateUploadLocked( pData, nBytes, nAllocationBytes, nAlignment, nRetireFence, nGpuAddress, nullptr, nullptr, pSwapOffsets, nSwapCount, nVertexStride );
}

//-----------------------------------------------------------------------------
// Purpose: Uploads structured data with an element-aligned resource offset
//-----------------------------------------------------------------------------
bool CPipelineCacheDX12::UploadStructured( const void *pData, size_t nBytes, uint32_t nStride, uint64_t nRetireFence, ID3D12Resource **ppResource, uint64_t *pOffset )
{
	if ( !ppResource || !pOffset )
		return false;
	*ppResource = nullptr;
	*pOffset = 0;
	if ( !nStride || nBytes % nStride )
		return false;
	D3D12_GPU_VIRTUAL_ADDRESS gpu = 0;
	size_t offset = 0;
	if ( !AllocateUploadLocked( pData, nBytes, nBytes, nStride, nRetireFence, gpu, ppResource, &offset, nullptr, 0, 0 ) )
		return false;
	*pOffset = static_cast<uint64_t>( offset );
	m_bReclaimDirty = true;
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Keeps an external draw resource alive until its recording completes
//-----------------------------------------------------------------------------
void CPipelineCacheDX12::RetainExternalResource( ID3D12Resource *pResource, uint64_t nRetireFence )
{
	if ( pResource )
	{
		RetainGeometryLocked( pResource, nRetireFence );
		m_bReclaimDirty = true;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Keeps a static geometry resource alive until retireFence completes
//-----------------------------------------------------------------------------
void CPipelineCacheDX12::RetainGeometryLocked( ID3D12Resource *pResource, uint64_t nRetireFence )
{
	ZoneNamedN( ___tracy_scoped_zone, "DX12 RetainGeometry", DX12_DRAW_ZONES_ACTIVE );
	UtlHashHandle_t hFlight = m_GeometryInFlight.Find( pResource );
	if ( hFlight == m_GeometryInFlight.InvalidHandle() )
	{
		hFlight = m_GeometryInFlight.Insert( pResource, RetiredResource{} );
		m_GeometryInFlight[hFlight].resource = pResource;
	}
	RetiredResource &flight = m_GeometryInFlight[hFlight];
	flight.fence = MAX( nRetireFence, flight.fence );
}

//-----------------------------------------------------------------------------
// Purpose: Dynamic buffers upload transiently; static buffers get a default-heap
//          copy that is reused while the content version matches
//-----------------------------------------------------------------------------
bool CPipelineCacheDX12::EnsureGeometryBuffer( CCommandRecorderDX12 *pList, CVertexBufferDX12 &buffer, size_t nUsedBytes, uint64_t nRetireFence, D3D12_GPU_VIRTUAL_ADDRESS &nGpuAddress, const uint32_t *pSwapOffsets, size_t nSwapCount, size_t nVertexStride )
{
	ZoneNamedN( ___tracy_scoped_zone, "DX12 EnsureGeometryBuffer", DX12_DRAW_ZONES_ACTIVE );
	if ( !pList || !nUsedBytes || !buffer.Stride() || nUsedBytes > buffer.Data().size() )
		return false;
	if ( buffer.IsDynamic() )
		return UploadTransient( buffer.Data().data(), nUsedBytes, nUsedBytes, 16, nRetireFence, nGpuAddress, pSwapOffsets, nSwapCount, nVertexStride ? nVertexStride : buffer.Stride() );
	const bool current = buffer.NativeResource() && buffer.NativeDevice() == m_pDevice && buffer.NativeResourceVersion() == buffer.ContentVersion() && buffer.NativeResourceBytes() >= nUsedBytes;
	// Retention for this fence already exists; skip the table update.
	if ( current && buffer.IsRetainedFor( nRetireFence, m_nRetainEpoch ) )
	{
		nGpuAddress = buffer.RetainedAddress();
		return true;
	}
	if ( current )
	{
		nGpuAddress = buffer.NativeResource()->GetGPUVirtualAddress();
		RetainGeometryLocked( buffer.NativeResource(), nRetireFence );
		buffer.MarkRetained( nRetireFence, m_nRetainEpoch );
		return true;
	}
	D3D12_HEAP_PROPERTIES heap{};
	heap.Type = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_RESOURCE_DESC desc{};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Width = ( nUsedBytes + 3u ) & ~size_t( 3u );
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	Microsoft::WRL::ComPtr<ID3D12Resource> resource;
	if ( FAILED( m_pDevice->CreateCommittedResource( &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS( &resource ) ) ) )
		return false;
	D3D12_GPU_VIRTUAL_ADDRESS uploadGpu = 0;
	ID3D12Resource *uploadSource = nullptr;
	size_t uploadOffset = 0;
	if ( !AllocateUploadLocked( buffer.Data().data(), nUsedBytes, desc.Width, 16, nRetireFence, uploadGpu, &uploadSource, &uploadOffset, pSwapOffsets, nSwapCount, nVertexStride ? nVertexStride : buffer.Stride() ) )
		return false;
	pList->CopyBufferRegion( resource.Get(), 0, uploadSource, uploadOffset, nUsedBytes );
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = resource.Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	pList->ResourceBarrier( 1, &barrier );
	buffer.NativeResourceRef() = std::move( resource );
	buffer.SetNativeResourceVersion( buffer.ContentVersion(), nUsedBytes, m_pDevice );
	nGpuAddress = buffer.NativeResource()->GetGPUVirtualAddress();
	RetainGeometryLocked( buffer.NativeResource(), nRetireFence );
	buffer.MarkRetained( nRetireFence, m_nRetainEpoch );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Index-buffer counterpart of EnsureGeometryBuffer
//-----------------------------------------------------------------------------
bool CPipelineCacheDX12::EnsureIndexBuffer( CCommandRecorderDX12 *pList, CIndexBufferDX12 &buffer, size_t nUsedBytes, uint64_t nRetireFence, D3D12_GPU_VIRTUAL_ADDRESS &nGpuAddress )
{
	ZoneNamedN( ___tracy_scoped_zone, "DX12 EnsureIndexBuffer", DX12_DRAW_ZONES_ACTIVE );
	if ( !pList || !nUsedBytes || nUsedBytes > buffer.Data().size() )
		return false;
	if ( buffer.IsDynamic() )
		return UploadTransient( buffer.Data().data(), nUsedBytes, nUsedBytes, 4, nRetireFence, nGpuAddress );
	const bool current = buffer.NativeResource() && buffer.NativeDevice() == m_pDevice && buffer.NativeResourceVersion() == buffer.ContentVersion() && buffer.NativeResourceBytes() >= nUsedBytes;
	if ( current && buffer.IsRetainedFor( nRetireFence, m_nRetainEpoch ) )
	{
		nGpuAddress = buffer.RetainedAddress();
		return true;
	}
	if ( current )
	{
		nGpuAddress = buffer.NativeResource()->GetGPUVirtualAddress();
		RetainGeometryLocked( buffer.NativeResource(), nRetireFence );
		buffer.MarkRetained( nRetireFence, m_nRetainEpoch );
		return true;
	}
	D3D12_HEAP_PROPERTIES heap{};
	heap.Type = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_RESOURCE_DESC desc{};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Width = ( nUsedBytes + 3u ) & ~size_t( 3u );
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	Microsoft::WRL::ComPtr<ID3D12Resource> resource;
	if ( FAILED( m_pDevice->CreateCommittedResource( &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS( &resource ) ) ) )
		return false;
	D3D12_GPU_VIRTUAL_ADDRESS uploadGpu = 0;
	ID3D12Resource *uploadSource = nullptr;
	size_t uploadOffset = 0;
	if ( !AllocateUploadLocked( buffer.Data().data(), nUsedBytes, desc.Width, 4, nRetireFence, uploadGpu, &uploadSource, &uploadOffset, nullptr, 0, 0 ) )
		return false;
	pList->CopyBufferRegion( resource.Get(), 0, uploadSource, uploadOffset, nUsedBytes );
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = resource.Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_INDEX_BUFFER;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	pList->ResourceBarrier( 1, &barrier );
	buffer.NativeResourceRef() = std::move( resource );
	buffer.SetNativeResourceVersion( buffer.ContentVersion(), nUsedBytes, m_pDevice );
	nGpuAddress = buffer.NativeResource()->GetGPUVirtualAddress();
	RetainGeometryLocked( buffer.NativeResource(), nRetireFence );
	buffer.MarkRetained( nRetireFence, m_nRetainEpoch );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Shader-visible 32-sampler table for this draw
//-----------------------------------------------------------------------------
DescriptorRangeDX12 CPipelineCacheDX12::PrepareSamplerTable( const D3D12_SAMPLER_DESC ( &samplers )[32], const uint16_t ( &ids )[32], uint64_t nRetireFence )
{
	ZoneNamedN( ___tracy_scoped_zone, "DX12 SamplerDescriptorAllocation", DX12_DRAW_ZONES_ACTIVE );
	// Callers supply canonical descriptions: unused slots hold DefaultSamplerDesc with id 0.
	return m_Bindings.AcquireSamplerTable( samplers, ids, nRetireFence );
}

//-----------------------------------------------------------------------------
// Purpose: Viewport and scissor, filtered per recording fence
//-----------------------------------------------------------------------------
void CPipelineCacheDX12::BindDrawState( CCommandRecorderDX12 *pList, const D3D12_VIEWPORT &viewport, const D3D12_RECT &scissor, uint64_t nRetireFence )
{
	const bool reset = !m_bDrawStateValid || m_nDrawStateFence != nRetireFence;
	if ( reset || memcmp( &m_BoundViewport, &viewport, sizeof( viewport ) ) )
	{
		pList->RSSetViewports( 1, &viewport );
		m_BoundViewport = viewport;
	}
	if ( reset || memcmp( &m_BoundScissor, &scissor, sizeof( scissor ) ) )
	{
		pList->RSSetScissorRects( 1, &scissor );
		m_BoundScissor = scissor;
	}
	m_nDrawStateFence = nRetireFence;
	m_bDrawStateValid = true;
}

//-----------------------------------------------------------------------------
// Purpose: Uploads constants and binds descriptor heaps, SRV/sampler tables and
//          root CBVs for one draw, skipping state already bound in this recording
//-----------------------------------------------------------------------------
bool CPipelineCacheDX12::PrepareBindings( CCommandRecorderDX12 *pList, const BindingInputDX12 &input )
{
	if ( !pList )
		return false;
	const DescriptorRangeDX12 &samplerTable = input.samplerTable;
	if ( samplerTable.count != 32 || !m_nZeroConstantAddress )
		return false;
	// Reserve every ordinary draw table before consulting generation-keyed caches. A later SRV
	// allocation must not roll the heap after either stage has captured its native CBV table.
	const uint32_t descriptorCount = 32u + ( input.nativeStage[0] ? 8u : 0u ) + ( input.nativeStage[1] ? 8u : 0u );
	if ( !ReserveResourceDescriptors( descriptorCount, input.retireFence ) )
		return false;
	if ( input.lightingAbi && ( !m_pLightingRoot || input.lightingViewTable.count != DX12_LIGHTING_RESOURCE_TABLE_COUNT ||
	     !input.lightingViewTable.gpu.ptr || !input.lightingViewConstants || !input.propDrawConstants || !input.propTriangles ||
	     input.lightingViewTable.generation != ResourceHeapGeneration() ||
	     ( ( input.lightingVisibilityRequired || input.lightingVisibilityTable.count ) &&
	       ( input.lightingVisibilityTable.count != 1 || !input.lightingVisibilityTable.gpu.ptr ||
	         input.lightingVisibilityTable.generation != ResourceHeapGeneration() ) ) ) )
		return false;
	if ( input.highresAbi && ( !m_pHighresRoot || input.highresTable.count != 8 ||
	     !input.highresTable.gpu.ptr || input.highresTable.generation != ResourceHeapGeneration() ||
	     !input.highresConstants || !input.highresFailure ) )
		return false;
	if ( input.pbrSpotsTable.count && ( input.pbrSpotsTable.count != DX12_PBR_TABLE_COUNT || !input.pbrSpotsTable.gpu.ptr || input.pbrSpotsTable.generation != ResourceHeapGeneration() ) )
		return false;
	if ( input.probeAbi && ( !input.lightingAbi || input.probeTable.count != DX12_PROBE_TABLE_COUNT || !input.probeTable.gpu.ptr ||
	     input.probeTable.generation != ResourceHeapGeneration() || !input.probeConstants ) )
		return false;
	const SIZE_T resourceStride = m_nResourceStride;
	const SIZE_T samplerStride = m_nSamplerStride;
	// Constant uploads are fence-scoped: an unchanged (version, shader, extent) slot keeps its address, and a
	// small per-bank table catches versions revisited within the same recording fence (A/B/A material
	// alternation). Uploads come from fence-retired transient pages, so no cross-fence pinning is needed.
	D3D12_GPU_VIRTUAL_ADDRESS constantAddresses[10]; // every slot is assigned or the call fails
	{
		ZoneNamedN( ___tracy_scoped_zone, "DX12 ConstantBindings", DX12_DRAW_ZONES_ACTIVE );
		for ( unsigned i = 0; i < 10; ++i )
		{
			// Cache banks 0-5 are shader register files; 6-9 are the four extension buffers.
			const unsigned bank = i < 3 ? i : ( i == 3 ? 6 : ( i <= 6 ? i - 1 : i ) );
			size_t nBytes = input.constantSizes[i];
			if ( bank < 6 )
			{
				const size_t registerSize = ( i == 2 || i == 6 ) ? sizeof( uint32_t ) : 16;
				const size_t count = input.consumedRegisters[bank];
				if ( count * registerSize > nBytes )
					return false;
				nBytes = count * registerSize;
			}
			// Root CBVs have no extent; an empty bank reads the persistent zero buffer.
			if ( !nBytes )
			{
				constantAddresses[i] = m_nZeroConstantAddress;
				continue;
			}
			const uint64_t version = input.constantVersions[bank], shaderId = bank < 6 ? input.constantShaderIds[bank] : 0;
			LastConstantSlot &last = m_LastConstantSlots[i];
			if ( version && last.fence == input.retireFence && last.version == version && last.shaderId == shaderId && last.bytes == nBytes )
			{
				constantAddresses[i] = last.address;
				++m_Stats.constantHits;
				continue;
			}
			const size_t uploadBytes = MAX( nBytes, size_t( 4 ) );
			const size_t size = ( uploadBytes + 255 ) & ~size_t( 255 );
			if ( size > kConstantBufferMaxBytes )
				return false;
			D3D12_GPU_VIRTUAL_ADDRESS gpu = 0;
			if ( version )
			{
				LastConstantSlot &recent = m_RecentConstants[bank][Mix32HashFunctor()( static_cast<uint32_t>( version ) ^ static_cast<uint32_t>( version >> 32 ) ^ static_cast<uint32_t>( shaderId ) * 0x9E3779B1u ) & ( kRecentConstants - 1 )];
				if ( recent.fence == input.retireFence && recent.version == version && recent.shaderId == shaderId && recent.bytes == nBytes )
				{
					gpu = recent.address;
					++m_Stats.constantHits;
				}
				else
				{
					if ( !input.constantData[i] )
						return false;
					++m_Stats.constantUploads;
					if ( !AllocateUploadLocked( input.constantData[i], nBytes, size, 256, input.retireFence, gpu, nullptr, nullptr, nullptr, 0, 0 ) )
						return false;
					recent = { input.retireFence, version, shaderId, nBytes, gpu };
				}
				last = { input.retireFence, version, shaderId, nBytes, gpu };
			}
			else
			{
				++m_Stats.transientConstants;
				last.fence = 0;
				if ( !AllocateUploadLocked( input.constantData[i], uploadBytes, size, 256, input.retireFence, gpu, nullptr, nullptr, nullptr, 0, 0 ) )
					return false;
			}
			constantAddresses[i] = gpu;
		}
	}
	// Native space-1 CBV tables exist only for stages bound to native records; legacy-only draws skip them.
	D3D12_GPU_DESCRIPTOR_HANDLE nativeTables[2]{};
	for ( unsigned stage = 0; stage < 2; ++stage )
	{
		if ( !input.nativeStage[stage] )
			continue;
		D3D12_GPU_VIRTUAL_ADDRESS addresses[8];
		UINT sizes[8];
		for ( unsigned slot = 0; slot < 8; ++slot )
		{
			const unsigned i = stage * 8 + slot;
			const size_t nBytes = input.nativeSizes[i];
			sizes[slot] = nBytes ? static_cast<UINT>( ( nBytes + 255 ) & ~size_t( 255 ) ) : 256u;
			if ( !nBytes )
			{
				addresses[slot] = m_nZeroConstantAddress;
				continue;
			}
			if ( !input.nativeData[i] || nBytes > kConstantBufferMaxBytes || ( nBytes & 15 ) )
				return false;
			const uint64_t version = input.nativeVersions[i];
			LastConstantSlot &last = m_LastNativeSlots[i];
			if ( version && last.fence == input.retireFence && last.version == version && last.bytes == nBytes )
			{
				addresses[slot] = last.address;
				++m_Stats.constantHits;
				continue;
			}
			if ( !AllocateUploadLocked( input.nativeData[i], nBytes, sizes[slot], 256, input.retireFence, addresses[slot], nullptr, nullptr, nullptr, 0, 0 ) )
				return false;
			++m_Stats.constantUploads;
			last = { input.retireFence, version, 0, nBytes, addresses[slot] };
		}
		// An unchanged address set within this fence reuses the previous table.
		NativeTableCache &cached = m_NativeTableCache[stage];
		if ( cached.fence == input.retireFence && cached.heapGeneration == m_Bindings.ResourceHeap().Generation() && !memcmp( cached.addresses, addresses, sizeof( addresses ) ) && !memcmp( cached.sizes, sizes, sizeof( sizes ) ) )
		{
			nativeTables[stage] = cached.gpu;
			continue;
		}
		const DescriptorRangeDX12 table = m_Bindings.AllocateDescriptors( 8, input.retireFence );
		if ( table.count != 8 )
			return false;
		for ( unsigned slot = 0; slot < 8; ++slot )
		{
			D3D12_CPU_DESCRIPTOR_HANDLE cpu = table.cpu;
			cpu.ptr += SIZE_T( slot ) * resourceStride;
			const D3D12_CONSTANT_BUFFER_VIEW_DESC view{ addresses[slot], sizes[slot] };
			m_pDevice->CreateConstantBufferView( &view, cpu );
		}
		memcpy( cached.addresses, addresses, sizeof( addresses ) );
		memcpy( cached.sizes, sizes, sizeof( sizes ) );
		cached.gpu = table.gpu;
		cached.fence = input.retireFence;
		cached.heapGeneration = table.generation;
		nativeTables[stage] = table.gpu;
	}
	D3D12_GPU_DESCRIPTOR_HANDLE srvs{};
	{
		ZoneNamedN( ___tracy_scoped_zone, "DX12 SRVBindings", DX12_DRAW_ZONES_ACTIVE );
		// Complete-source tables are reusable within their recording fence and heap generation; a CPU source slot
		// can be rewritten for another texture copy, so resources are compared as well as handles.
		const uint64_t generation = m_Bindings.ResourceHeap().Generation();
		const auto sameTable = [&]( const CachedSrvTable &cached )
		{
			return cached.fence == input.retireFence && cached.heapGeneration == generation && cached.descriptorEpoch == m_nSrvDescriptorEpoch &&
			    !memcmp( cached.sources, input.srvSources, sizeof( input.srvSources ) ) &&
			    !memcmp( cached.resources, input.textures, sizeof( input.textures ) );
		};
		CachedSrvTable *slot = nullptr;
		const bool previousValid = m_bLastSrvTableIsPrevious;
		// An unchanged texture set reuses the previous draw's table (whose sources were complete) without scanning
		// or comparing the 32 handles.
		const bool reusePrevious = input.texturesUnchanged && previousValid && m_LastSrvTable.fence == input.retireFence && m_LastSrvTable.heapGeneration == generation && m_LastSrvTable.descriptorEpoch == m_nSrvDescriptorEpoch;
		const bool sourcesComplete = reusePrevious || [&]
		{
			SIZE_T nZeroMask = 0;
			for ( int i = 0; i < ARRAYSIZE( input.srvSources ); ++i )
				nZeroMask |= input.srvSources[i].ptr == 0;
			return nZeroMask == 0;
		}();
		m_bLastSrvTableIsPrevious = false;
		if ( sourcesComplete )
		{
			if ( reusePrevious )
			{
				srvs = m_LastSrvTable.gpu;
				++m_Stats.srvTableHits;
			}
			else if ( sameTable( m_LastSrvTable ) )
			{
				srvs = m_LastSrvTable.gpu;
				++m_Stats.srvTableHits;
			}
			else
			{
				const uint64_t identity = input.srvSources[0].ptr ^ ( input.srvSources[1].ptr * 1099511628211ull ) ^ input.srvSources[2].ptr;
				slot = &m_SrvTables[Mix32HashFunctor()( static_cast<uint32_t>( identity ) ^ static_cast<uint32_t>( identity >> 32 ) ) & ( ARRAYSIZE( m_SrvTables ) - 1 )];
				if ( sameTable( *slot ) )
				{
					srvs = slot->gpu;
					m_LastSrvTable = *slot;
					++m_Stats.srvTableHits;
					slot = nullptr;
				}
			}
			m_bLastSrvTableIsPrevious = srvs.ptr != 0;
		}
		if ( !srvs.ptr )
		{
			DescriptorRangeDX12 table;
			{
				ZoneNamedN( ___tracy_scoped_zone, "DX12 ResourceDescriptorAllocation", DX12_DRAW_ZONES_ACTIVE );
				table = m_Bindings.AllocateDescriptors( 32, input.retireFence );
			}
			if ( table.count != 32 )
				return false;
			srvs = table.gpu;
			if ( sourcesComplete )
			{
				// The submission worker performs the copy at replay; source slots are fence-retired (ReleaseResourceDescriptor).
				pList->CopyDescriptorTable( table.cpu, 32, input.srvSources );
				++m_Stats.srvTableCopies;
				CachedSrvTable &fresh = m_LastSrvTable;
				memcpy( fresh.sources, input.srvSources, sizeof( fresh.sources ) );
				memcpy( fresh.resources, input.textures, sizeof( fresh.resources ) );
				fresh.gpu = srvs;
				fresh.fence = input.retireFence;
				fresh.heapGeneration = table.generation;
				fresh.descriptorEpoch = m_nSrvDescriptorEpoch;
				if ( slot )
					*slot = fresh;
				m_bLastSrvTableIsPrevious = true;
			}
			else
			{
				D3D12_CPU_DESCRIPTOR_HANDLE destinations[32], sources[32];
				UINT copyCount = 0;
				for ( unsigned i = 0; i < 32; ++i )
				{
					D3D12_CPU_DESCRIPTOR_HANDLE cpu = table.cpu;
					cpu.ptr += i * resourceStride;
					D3D12_CPU_DESCRIPTOR_HANDLE source = input.srvSources[i];
					if ( !source.ptr )
					{
						D3D12_SHADER_RESOURCE_VIEW_DESC srv = input.srvDescs[i];
						if ( !srv.Shader4ComponentMapping )
							srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
						if ( !srv.ViewDimension )
							srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
						if ( srv.Format == DXGI_FORMAT_UNKNOWN )
							srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
						if ( srv.ViewDimension == D3D12_SRV_DIMENSION_TEXTURE2D && !srv.Texture2D.MipLevels )
							srv.Texture2D.MipLevels = 1;
						if ( !input.textures[i] && !memcmp( &srv, &m_NullSrvDesc, sizeof( srv ) ) )
							source = m_NullSrv;
						else
						{
							m_pDevice->CreateShaderResourceView( input.textures[i], &srv, cpu );
							continue;
						}
					}
					destinations[copyCount] = cpu;
					sources[copyCount] = source;
					++copyCount;
				}
				if ( copyCount )
					m_pDevice->CopyDescriptors( copyCount, destinations, nullptr, copyCount, sources, nullptr, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
			}
		}
	}
	// The lighting view table precedes ordinary draw tables; a missing reservation must not bind a stale handle.
	if ( input.lightingAbi && ( input.lightingViewTable.generation != ResourceHeapGeneration() ||
	     ( input.lightingVisibilityTable.count && input.lightingVisibilityTable.generation != ResourceHeapGeneration() ) ) )
		return false;
	if ( ( input.highresAbi && input.highresTable.generation != ResourceHeapGeneration() ) || ( input.pbrSpotsTable.count && input.pbrSpotsTable.generation != ResourceHeapGeneration() ) ||
	     ( input.probeAbi && input.probeTable.generation != ResourceHeapGeneration() ) )
		return false;
	{
		ZoneNamedN( ___tracy_scoped_zone, "DX12 RootTables", DX12_DRAW_ZONES_ACTIVE );
		ID3D12RootSignature *root = input.highresAbi ? m_pHighresRoot.Get() : input.lightingAbi ? m_pLightingRoot.Get() : m_pRoot.Get();
		if ( !m_bGraphicsBindingsValid || m_nGraphicsBindingsFence != input.retireFence || m_pBoundRoot != root )
		{
			pList->SetGraphicsRootSignature( root );
			m_pBoundRoot = root;
			// SetGraphicsRootSignature invalidates every root argument, including ordinary ones.
			m_bGraphicsBindingsValid = false;
		}
		ID3D12DescriptorHeap *resourceHeap = m_Bindings.ResourceHeap().Heap(), *samplerHeap = m_Bindings.SamplerHeap().Heap();
		if ( !m_bGraphicsBindingsValid || m_nGraphicsBindingsFence != input.retireFence || m_pBoundResourceHeap != resourceHeap || m_pBoundSamplerHeap != samplerHeap )
		{
			ID3D12DescriptorHeap *heaps[] = { resourceHeap, samplerHeap };
			pList->SetDescriptorHeaps( 2, heaps );
			m_bGraphicsBindingsValid = false;
			// Draws without space 4/5 skip those binds (also after a root change), so their cached handles must not survive it.
			m_BoundPbrSpots = {};
			m_BoundProbeTable = {};
			m_nBoundProbeConstants = 0;
			m_pBoundResourceHeap = resourceHeap;
			m_pBoundSamplerHeap = samplerHeap;
		}
		D3D12_GPU_DESCRIPTOR_HANDLE vertexSrvs = srvs;
		vertexSrvs.ptr += 16 * resourceStride;
		D3D12_GPU_DESCRIPTOR_HANDLE vertexSamplers = samplerTable.gpu;
		vertexSamplers.ptr += 16 * samplerStride;
		const D3D12_GPU_DESCRIPTOR_HANDLE rootTables[] = { srvs, samplerTable.gpu, vertexSrvs, vertexSamplers };
		// Parameters a draw's shaders never read may stay stale; *Current_ records whether they match this draw.
		if ( !m_bGraphicsBindingsValid )
		{
			m_bVertexTablesCurrent = false;
			m_bGeometryConstantsCurrent = false;
		}
		const UINT tableCount = input.vertexTextures ? 4u : 2u;
		for ( UINT i = 0; i < tableCount; ++i )
			if ( !m_bGraphicsBindingsValid || ( i >= 2 && !m_bVertexTablesCurrent ) || m_BoundRootTables[i].ptr != rootTables[i].ptr )
			{
				++m_Stats.rootTableSets;
				pList->SetGraphicsRootDescriptorTable( i, rootTables[i] );
				m_BoundRootTables[i] = rootTables[i];
			}
		if ( input.vertexTextures )
			m_bVertexTablesCurrent = true;
		else if ( m_BoundRootTables[2].ptr != vertexSrvs.ptr || m_BoundRootTables[3].ptr != vertexSamplers.ptr )
			m_bVertexTablesCurrent = false;
		for ( unsigned stage = 0; stage < 2; ++stage )
			if ( input.nativeStage[stage] )
			{
				const UINT root = stage ? kRootNativePixel : kRootNativeVertex;
				D3D12_GPU_DESCRIPTOR_HANDLE &bound = m_BoundNativeTables[stage];
				if ( !m_bGraphicsBindingsValid || bound.ptr != nativeTables[stage].ptr )
				{
					++m_Stats.rootTableSets;
					pList->SetGraphicsRootDescriptorTable( root, nativeTables[stage] );
					bound = nativeTables[stage];
				}
			}
		// Space-5 projected lights exist on every root, so the cache is only meaningful for the root it was bound to.
		if ( input.pbrSpotsTable.count && ( !m_bGraphicsBindingsValid || m_BoundPbrSpots.ptr != input.pbrSpotsTable.gpu.ptr ) )
		{
			++m_Stats.rootTableSets;
			pList->SetGraphicsRootDescriptorTable( kRootPbrSpots, input.pbrSpotsTable.gpu );
			m_BoundPbrSpots = input.pbrSpotsTable.gpu;
		}
		// Space-4 ambient probes (lighting and highres roots): the nine-descriptor table and its constants travel together.
		if ( input.probeAbi )
		{
			if ( !m_bGraphicsBindingsValid || m_BoundProbeTable.ptr != input.probeTable.gpu.ptr )
			{
				++m_Stats.rootTableSets;
				pList->SetGraphicsRootDescriptorTable( kRootProbeTable, input.probeTable.gpu );
				m_BoundProbeTable = input.probeTable.gpu;
			}
			if ( !m_bGraphicsBindingsValid || m_nBoundProbeConstants != input.probeConstants )
			{
				++m_Stats.rootCbvSets;
				pList->SetGraphicsRootConstantBufferView( kRootProbeConstants, input.probeConstants );
				m_nBoundProbeConstants = input.probeConstants;
			}
		}
		// Slots 0-3 feed VS b0-b3 (mirrored to GS b0-b3 when a geometry stage runs); slots 4-9 feed PS b0-b5.
		for ( UINT slot = 0; slot < 10; ++slot )
		{
			const UINT root = slot < 4 ? kRootVertexConstants + slot : kRootPixelConstants + ( slot - 4 );
			if ( !m_bGraphicsBindingsValid || m_BoundRootConstants[root - kRootVertexConstants] != constantAddresses[slot] )
			{
				++m_Stats.rootCbvSets;
				pList->SetGraphicsRootConstantBufferView( root, constantAddresses[slot] );
				m_BoundRootConstants[root - kRootVertexConstants] = constantAddresses[slot];
			}
			if ( slot < 4 && input.geometryStage )
			{
				const UINT geometry = kRootGeometryConstants + slot;
				if ( !m_bGraphicsBindingsValid || !m_bGeometryConstantsCurrent || m_BoundRootConstants[geometry - kRootVertexConstants] != constantAddresses[slot] )
				{
					++m_Stats.rootCbvSets;
					pList->SetGraphicsRootConstantBufferView( geometry, constantAddresses[slot] );
					m_BoundRootConstants[geometry - kRootVertexConstants] = constantAddresses[slot];
				}
			}
		}
		if ( input.geometryStage )
			m_bGeometryConstantsCurrent = true;
		else
			for ( UINT slot = 0; slot < 4; ++slot )
				if ( m_BoundRootConstants[kRootGeometryConstants + slot - kRootVertexConstants] != constantAddresses[slot] )
					m_bGeometryConstantsCurrent = false;
		if ( input.lightingAbi )
		{
			if ( !m_bGraphicsBindingsValid || m_BoundLightingViewTable.ptr != input.lightingViewTable.gpu.ptr )
			{
				++m_Stats.rootTableSets;
				pList->SetGraphicsRootDescriptorTable( kRootLightingView, input.lightingViewTable.gpu );
				m_BoundLightingViewTable = input.lightingViewTable.gpu;
			}
			if ( input.lightingVisibilityTable.count &&
			     ( !m_bGraphicsBindingsValid || m_BoundLightingVisibilityTable.ptr != input.lightingVisibilityTable.gpu.ptr ) )
			{
				++m_Stats.rootTableSets;
				pList->SetGraphicsRootDescriptorTable( kRootLightingVisibility, input.lightingVisibilityTable.gpu );
				m_BoundLightingVisibilityTable = input.lightingVisibilityTable.gpu;
			}
			if ( !m_bGraphicsBindingsValid || m_nBoundLightingViewConstants != input.lightingViewConstants )
			{
				++m_Stats.rootCbvSets;
				pList->SetGraphicsRootConstantBufferView( kRootLightingViewConstants, input.lightingViewConstants );
				m_nBoundLightingViewConstants = input.lightingViewConstants;
			}
			if ( !m_bGraphicsBindingsValid || m_BoundPropDraw != input.propDrawConstants )
			{
				++m_Stats.rootCbvSets;
				pList->SetGraphicsRootConstantBufferView( kRootPropDraw, input.propDrawConstants );
				m_BoundPropDraw = input.propDrawConstants;
			}
			if ( !m_bGraphicsBindingsValid || m_BoundPropTriangles != input.propTriangles )
			{
				++m_Stats.rootSrvSets;
				pList->SetGraphicsRootShaderResourceView( kRootPropTriangles, input.propTriangles );
				m_BoundPropTriangles = input.propTriangles;
			}
		}
		if ( input.highresAbi )
		{
			if ( !m_bGraphicsBindingsValid || m_BoundHighresTable.ptr != input.highresTable.gpu.ptr )
			{
				++m_Stats.rootTableSets;
				pList->SetGraphicsRootDescriptorTable( kRootHighresTable, input.highresTable.gpu );
				m_BoundHighresTable = input.highresTable.gpu;
			}
			if ( !m_bGraphicsBindingsValid || m_nBoundHighresConstants != input.highresConstants )
			{
				++m_Stats.rootCbvSets;
				pList->SetGraphicsRootConstantBufferView( kRootHighresConstants, input.highresConstants );
				m_nBoundHighresConstants = input.highresConstants;
			}
			if ( !m_bGraphicsBindingsValid || m_nBoundHighresFailure != input.highresFailure )
			{
				pList->SetGraphicsRootUnorderedAccessView( kRootHighresFailure, input.highresFailure );
				m_nBoundHighresFailure = input.highresFailure;
			}
		}
		m_bGraphicsBindingsValid = true;
		m_nGraphicsBindingsFence = input.retireFence;
		return true;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Recycles upload pages, geometry retentions, descriptors and PSOs
//          whose fences have completed
//-----------------------------------------------------------------------------
void CPipelineCacheDX12::Reclaim( uint64_t nCompletedFence )
{
	ZoneNamedN( ___tracy_scoped_zone, "DX12 PipelineReclaim", DX12_ZONES_ACTIVE );
	if ( !m_bReclaimDirty && nCompletedFence <= m_nLastReclaimedFence )
		return;
	m_Bindings.Reclaim( nCompletedFence );
	for ( int i = 0; i < m_UploadPages.Count(); ++i )
		if ( m_UploadPages[i].used && m_UploadPages[i].fence <= nCompletedFence )
			m_UploadPages[i].used = 0;
	m_nUploadPageHint = 0;
	for ( UtlHashHandle_t index = m_GeometryInFlight.FirstHandle(); index != m_GeometryInFlight.InvalidHandle(); )
	{
		if ( m_GeometryInFlight[index].fence <= nCompletedFence )
			index = m_GeometryInFlight.RemoveAndAdvance( index );
		else
			index = m_GeometryInFlight.NextHandle( index );
	}
	// Retain completed pages for reuse, as with m_UploadPages; Shutdown releases the high-water pool.
	bool removedPipeline = false;
	for ( int i = 0; i < m_Entries.Count(); )
		if ( ( m_Entries[i].destroyed || m_Entries.Count() > 512 ) && m_Entries[i].lastUseFence <= nCompletedFence )
		{
			m_Entries.Remove( i );
			removedPipeline = true;
		}
		else
			++i;
	if ( removedPipeline )
	{
		m_nLastPipelineIndex = -1;
		memset( m_PipelineHints, 0, sizeof( m_PipelineHints ) );
		++m_nPipelineEpoch;
	}
	m_nLastReclaimedFence = MAX( nCompletedFence, m_nLastReclaimedFence );
	m_bReclaimDirty = false;
}

//-----------------------------------------------------------------------------
// Purpose: Marks PSOs using a destroyed shader and drops constant reuse records
//          that name it
//-----------------------------------------------------------------------------
void CPipelineCacheDX12::NotifyShaderDestroyed( uint64_t nShaderIdentity )
{
	m_bReclaimDirty = true;
	for ( int i = 0; i < m_Entries.Count(); ++i )
	{
		Entry &entry = m_Entries[i];
		if ( entry.key.vs == nShaderIdentity || entry.key.ps == nShaderIdentity || entry.key.gs == nShaderIdentity )
			entry.destroyed = true;
	}
	// Fence-scoped constant entries may name this shader until the fence changes; drop them now.
	for ( int nBank = 0; nBank < ARRAYSIZE( m_RecentConstants ); ++nBank )
		for ( int i = 0; i < ARRAYSIZE( m_RecentConstants[nBank] ); ++i )
			if ( m_RecentConstants[nBank][i].shaderId == nShaderIdentity )
				m_RecentConstants[nBank][i].fence = 0;
	for ( int i = 0; i < ARRAYSIZE( m_LastConstantSlots ); ++i )
		if ( m_LastConstantSlots[i].shaderId == nShaderIdentity )
			m_LastConstantSlots[i].fence = 0;
}

//-----------------------------------------------------------------------------
// Purpose: Maps ShaderBlendFactor_t to the D3D12 color blend factor
//-----------------------------------------------------------------------------
static D3D12_BLEND BlendFactorDX12( uint32_t nFactor )
{
	switch ( nFactor )
	{
	case SHADER_BLEND_ZERO:
		return D3D12_BLEND_ZERO;
	case SHADER_BLEND_ONE:
		return D3D12_BLEND_ONE;
	case SHADER_BLEND_DST_COLOR:
		return D3D12_BLEND_DEST_COLOR;
	case SHADER_BLEND_ONE_MINUS_DST_COLOR:
		return D3D12_BLEND_INV_DEST_COLOR;
	case SHADER_BLEND_SRC_ALPHA:
		return D3D12_BLEND_SRC_ALPHA;
	case SHADER_BLEND_ONE_MINUS_SRC_ALPHA:
		return D3D12_BLEND_INV_SRC_ALPHA;
	case SHADER_BLEND_DST_ALPHA:
		return D3D12_BLEND_DEST_ALPHA;
	case SHADER_BLEND_ONE_MINUS_DST_ALPHA:
		return D3D12_BLEND_INV_DEST_ALPHA;
	case SHADER_BLEND_SRC_ALPHA_SATURATE:
		return D3D12_BLEND_SRC_ALPHA_SAT;
	case SHADER_BLEND_SRC_COLOR:
		return D3D12_BLEND_SRC_COLOR;
	case SHADER_BLEND_ONE_MINUS_SRC_COLOR:
		return D3D12_BLEND_INV_SRC_COLOR;
	default:
		return D3D12_BLEND_ONE;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Alpha blend factors may not name color channels; map them to alpha
//-----------------------------------------------------------------------------
static D3D12_BLEND AlphaBlendFactorDX12( uint32_t nFactor )
{
	switch ( nFactor )
	{
	case SHADER_BLEND_DST_COLOR:
		return D3D12_BLEND_DEST_ALPHA;
	case SHADER_BLEND_ONE_MINUS_DST_COLOR:
		return D3D12_BLEND_INV_DEST_ALPHA;
	case SHADER_BLEND_SRC_COLOR:
		return D3D12_BLEND_SRC_ALPHA;
	case SHADER_BLEND_ONE_MINUS_SRC_COLOR:
		return D3D12_BLEND_INV_SRC_ALPHA;
	case SHADER_BLEND_SRC_ALPHA_SATURATE:
		return D3D12_BLEND_ONE;
	default:
		return BlendFactorDX12( nFactor );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Maps ShaderStencilOp_t (1-8) to D3D12; anything else keeps
//-----------------------------------------------------------------------------
static D3D12_STENCIL_OP StencilOpDX12( uint32_t nOp )
{
	static constexpr D3D12_STENCIL_OP s_StencilOps[] = { D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_ZERO, D3D12_STENCIL_OP_REPLACE, D3D12_STENCIL_OP_INCR_SAT, D3D12_STENCIL_OP_DECR_SAT, D3D12_STENCIL_OP_INVERT, D3D12_STENCIL_OP_INCR, D3D12_STENCIL_OP_DECR };
	return nOp >= 1 && nOp <= 8 ? s_StencilOps[nOp] : D3D12_STENCIL_OP_KEEP;
}

//-----------------------------------------------------------------------------
// Purpose: ShaderStencilFunc_t values 1-8 equal D3D12_COMPARISON_FUNC; else always
//-----------------------------------------------------------------------------
static D3D12_COMPARISON_FUNC StencilFunctionDX12( uint32_t nFunc )
{
	return nFunc >= 1 && nFunc <= 8 ? static_cast<D3D12_COMPARISON_FUNC>( nFunc ) : D3D12_COMPARISON_FUNC_ALWAYS;
}

//-----------------------------------------------------------------------------
// Purpose: Returns the cached PSO for key, creating it on a miss
//-----------------------------------------------------------------------------
ID3D12PipelineState *CPipelineCacheDX12::GetOrCreate( const PipelineKeyDX12 &key, const D3D12_SHADER_BYTECODE &vs, const D3D12_SHADER_BYTECODE &ps, const D3D12_SHADER_BYTECODE &gs, const D3D12_INPUT_LAYOUT_DESC &layout, uint64_t nRetireFence )
{
	if ( m_nLastPipelineIndex >= 0 )
	{
		Entry &entry = m_Entries[m_nLastPipelineIndex];
		if ( !entry.destroyed && entry.key == key )
		{
			entry.lastUseFence = nRetireFence;
			return entry.pso.Get();
		}
	}
	const uint64_t identity = key.vs ^ ( key.ps * 1099511628211ull ) ^ key.vsVariant ^ key.psVariant ^ key.gs ^ key.gsVariant ^ key.input ^ ( uint64_t( key.color ) << 8 ) ^ ( uint64_t( key.depth ) << 16 ) ^ key.samples ^ key.raster ^ ( key.lightingAbi ? 0x9E3779B97F4A7C15ull : 0ull );
	const unsigned hint = Mix32HashFunctor()( static_cast<uint32_t>( identity ) ^ static_cast<uint32_t>( identity >> 32 ) ) & static_cast<unsigned>( ARRAYSIZE( m_PipelineHints ) - 1 );
	if ( m_PipelineHints[hint] )
	{
		const int index = static_cast<int>( m_PipelineHints[hint] - 1 );
		Entry &entry = m_Entries[index];
		if ( !entry.destroyed && entry.key == key )
		{
			m_nLastPipelineIndex = index;
			entry.lastUseFence = nRetireFence;
			return entry.pso.Get();
		}
	}
	{
		ZoneNamedN( pipelineSearch, "DX12 PSOHintFallback", DX12_DRAW_ZONES_ACTIVE );
		for ( int i = 0; i < m_Entries.Count(); ++i )
		{
			Entry &entry = m_Entries[i];
			if ( !entry.destroyed && entry.key == key )
			{
				m_nLastPipelineIndex = i;
				m_PipelineHints[hint] = static_cast<uint32_t>( i ) + 1;
				entry.lastUseFence = nRetireFence;
				return entry.pso.Get();
			}
		}
	}
	ZoneNamedN( ___tracy_scoped_zone, "DX12 PSOCreateMiss", DX12_ZONES_ACTIVE );
	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
	desc.pRootSignature = key.highresAbi ? m_pHighresRoot.Get() : key.lightingAbi ? m_pLightingRoot.Get() : m_pRoot.Get();
	desc.VS = vs;
	desc.PS = ps;
	desc.GS = gs;
	desc.InputLayout = layout;
	desc.PrimitiveTopologyType = static_cast<D3D12_PRIMITIVE_TOPOLOGY_TYPE>( key.topology );
	desc.NumRenderTargets = key.colorCount;
	for ( UINT i = 0; i < key.colorCount && i < 4; ++i )
		desc.RTVFormats[i] = key.colorFormats[i];
	desc.DSVFormat = key.depth;
	desc.SampleDesc.Count = key.samples;
	desc.SampleDesc.Quality = key.sampleQuality;
	desc.SampleMask = UINT_MAX;
	desc.RasterizerState.FillMode = key.wireframe ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
	desc.RasterizerState.CullMode = key.culling ? D3D12_CULL_MODE_BACK : D3D12_CULL_MODE_NONE;
	desc.RasterizerState.FrontCounterClockwise = key.frontCounterClockwise;
	desc.RasterizerState.DepthBias = key.depthBiasValue;
	desc.RasterizerState.SlopeScaledDepthBias = key.slopeScaledDepthBias;
	desc.RasterizerState.DepthClipEnable = TRUE;
	desc.BlendState.AlphaToCoverageEnable = key.alphaToCoverage && key.samples > 1;
	static constexpr D3D12_BLEND_OP blendOps[] = { D3D12_BLEND_OP_ADD, D3D12_BLEND_OP_SUBTRACT, D3D12_BLEND_OP_REV_SUBTRACT, D3D12_BLEND_OP_MIN, D3D12_BLEND_OP_MAX };
	for ( UINT i = 0; i < key.colorCount && i < 4; ++i )
	{
		D3D12_RENDER_TARGET_BLEND_DESC &target = desc.BlendState.RenderTarget[i];
		const bool gbufferTarget = key.gbuffer && i > 0;
		target.RenderTargetWriteMask = gbufferTarget ? D3D12_COLOR_WRITE_ENABLE_ALL : ( key.colorWrites ? D3D12_COLOR_WRITE_ENABLE_RED | D3D12_COLOR_WRITE_ENABLE_GREEN | D3D12_COLOR_WRITE_ENABLE_BLUE : 0 ) | ( key.alphaWrites ? D3D12_COLOR_WRITE_ENABLE_ALPHA : 0 );
		target.BlendEnable = key.blend != 0 && !gbufferTarget;
		target.SrcBlend = BlendFactorDX12( key.blendSource );
		target.DestBlend = BlendFactorDX12( key.blendDestination );
		target.BlendOp = key.blendOperation < 5 ? blendOps[key.blendOperation] : D3D12_BLEND_OP_ADD;
		target.SrcBlendAlpha = AlphaBlendFactorDX12( key.separateAlpha ? key.blendAlphaSource : key.blendSource );
		target.DestBlendAlpha = AlphaBlendFactorDX12( key.separateAlpha ? key.blendAlphaDestination : key.blendDestination );
		target.BlendOpAlpha = ( key.separateAlpha ? key.blendAlphaOperation : key.blendOperation ) < 5 ? blendOps[key.separateAlpha ? key.blendAlphaOperation : key.blendOperation] : D3D12_BLEND_OP_ADD;
	}
	desc.BlendState.IndependentBlendEnable = key.colorCount > 1;
	desc.DepthStencilState.DepthEnable = key.depthTest && key.depth != DXGI_FORMAT_UNKNOWN;
	desc.DepthStencilState.DepthWriteMask = key.depthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
	static constexpr D3D12_COMPARISON_FUNC depthFuncs[] = { D3D12_COMPARISON_FUNC_NEVER, D3D12_COMPARISON_FUNC_LESS, D3D12_COMPARISON_FUNC_EQUAL, D3D12_COMPARISON_FUNC_LESS_EQUAL, D3D12_COMPARISON_FUNC_GREATER, D3D12_COMPARISON_FUNC_NOT_EQUAL, D3D12_COMPARISON_FUNC_GREATER_EQUAL, D3D12_COMPARISON_FUNC_ALWAYS };
	desc.DepthStencilState.DepthFunc = key.depthFunction < 8 ? depthFuncs[key.depthFunction] : D3D12_COMPARISON_FUNC_LESS_EQUAL;
	desc.DepthStencilState.StencilEnable = key.stencil && key.depth != DXGI_FORMAT_UNKNOWN;
	desc.DepthStencilState.StencilReadMask = key.stencilReadMask;
	desc.DepthStencilState.StencilWriteMask = key.stencilWriteMask;
	desc.DepthStencilState.FrontFace.StencilFailOp = StencilOpDX12( key.stencilFail );
	desc.DepthStencilState.FrontFace.StencilDepthFailOp = StencilOpDX12( key.stencilDepthFail );
	desc.DepthStencilState.FrontFace.StencilPassOp = StencilOpDX12( key.stencilPass );
	desc.DepthStencilState.FrontFace.StencilFunc = StencilFunctionDX12( key.stencilFunction );
	desc.DepthStencilState.BackFace = desc.DepthStencilState.FrontFace;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
	HRESULT hr = m_pDevice->CreateGraphicsPipelineState( &desc, IID_PPV_ARGS( &pso ) );
	if ( FAILED( hr ) )
	{
		Warning( "ShaderAPIDX12: graphics pipeline creation failed 0x%08x\n", static_cast<unsigned>( hr ) );
		return nullptr;
	}
	Entry &entry = *m_Entries.AddToTailGetPtr();
	entry.key = key;
	entry.pso = std::move( pso );
	entry.lastUseFence = nRetireFence;
	m_nLastPipelineIndex = m_Entries.Count() - 1;
	m_PipelineHints[hint] = static_cast<uint32_t>( m_Entries.Count() );
	return entry.pso.Get();
}
} // namespace shaderapidx12
