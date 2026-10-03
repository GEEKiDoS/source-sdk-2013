//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Generic native DX12 compute dispatch for shader materials.
//
//=============================================================================//
#include "shaderapi_dx12.h"
#include "shaderdevice_dx12.h"
#include "command_recorder_dx12.h"
#include "tier0/dbg.h"
#include <d3dcompiler.h>
#include <cstring>
#include <type_traits>

namespace shaderapidx12
{
namespace
{
constexpr D3D12_RESOURCE_STATES kSceneDepthRead = D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
constexpr UINT kComputeDescriptorCount = SHADERAPIDX12_COMPUTE_MAX_SRVS + SHADERAPIDX12_COMPUTE_MAX_UAVS;
constexpr UINT kMaxComputeBarriers = 32;

struct ComputeReplayPayloadDX12
{
	ID3D12RootSignature *root = nullptr;
	ID3D12PipelineState *pso = nullptr;
	ID3D12DescriptorHeap *heap = nullptr;
	D3D12_GPU_DESCRIPTOR_HANDLE srvTable{};
	D3D12_GPU_DESCRIPTOR_HANDLE uavTable{};
	D3D12_GPU_VIRTUAL_ADDRESS constants = 0;
	UINT groupsX = 0, groupsY = 0, groupsZ = 0;
	D3D12_RESOURCE_BARRIER barriers[kMaxComputeBarriers]{};
	UINT barrierCount = 0;
	ID3D12Resource *sceneDepth = nullptr;
	D3D12_RESOURCE_STATES sceneDepthBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
	bool restoreSceneDepth = false;
};
static_assert( std::is_trivially_copyable<ComputeReplayPayloadDX12>::value, "external command payloads are copied bytewise" );

void ComputeReplayThunk( ID3D12GraphicsCommandList *pList, ID3D12Device *, const void *pData ) noexcept
{
	ComputeReplayPayloadDX12 payload;
	memcpy( &payload, pData, sizeof( payload ) );
	if ( payload.barrierCount )
		pList->ResourceBarrier( payload.barrierCount, payload.barriers );
	ID3D12DescriptorHeap *heaps[] = { payload.heap };
	pList->SetDescriptorHeaps( 1, heaps );
	pList->SetComputeRootSignature( payload.root );
	pList->SetPipelineState( payload.pso );
	pList->SetComputeRootConstantBufferView( 0, payload.constants );
	pList->SetComputeRootDescriptorTable( 1, payload.srvTable );
	pList->SetComputeRootDescriptorTable( 2, payload.uavTable );
	pList->Dispatch( payload.groupsX, payload.groupsY, payload.groupsZ );
	if ( payload.restoreSceneDepth && payload.sceneDepth )
	{
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = payload.sceneDepth;
		barrier.Transition.StateBefore = kSceneDepthRead;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		pList->ResourceBarrier( 1, &barrier );
	}
}

void WarnComputeOnce( const char *pszName, const char *pszReason )
{
	static CUtlVector<CUtlString> warned;
	for ( int i = 0; i < warned.Count(); ++i )
	{
		if ( !V_stricmp( warned[i].Get(), pszName ? pszName : "" ) )
			return;
	}
	warned.AddToTail( pszName ? pszName : "" );
	Warning( "ShaderAPIDX12 compute %s: %s\n", pszName ? pszName : "<null>", pszReason ? pszReason : "invalid dispatch" );
}

D3D12_CPU_DESCRIPTOR_HANDLE Offset( D3D12_CPU_DESCRIPTOR_HANDLE handle, UINT index, UINT stride )
{
	handle.ptr += static_cast<SIZE_T>( index ) * stride;
	return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE Offset( D3D12_GPU_DESCRIPTOR_HANDLE handle, UINT index, UINT stride )
{
	handle.ptr += static_cast<UINT64>( index ) * stride;
	return handle;
}

bool AddTransition( ComputeReplayPayloadDX12 &payload, ID3D12Resource *resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after, UINT subresource )
{
	if ( !resource || before == after )
		return true;
	if ( payload.barrierCount >= kMaxComputeBarriers )
		return false;
	D3D12_RESOURCE_BARRIER &barrier = payload.barriers[payload.barrierCount++];
	barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = resource;
	barrier.Transition.StateBefore = before;
	barrier.Transition.StateAfter = after;
	barrier.Transition.Subresource = subresource;
	return true;
}

uint32_t ReflectedConstantBytes( const D3D12_SHADER_BYTECODE &bytecode )
{
	Microsoft::WRL::ComPtr<ID3D12ShaderReflection> reflection;
	if ( !bytecode.pShaderBytecode || FAILED( D3DReflect( bytecode.pShaderBytecode, bytecode.BytecodeLength, IID_PPV_ARGS( &reflection ) ) ) )
		return UINT_MAX;
	D3D12_SHADER_DESC shader{};
	if ( FAILED( reflection->GetDesc( &shader ) ) )
		return UINT_MAX;
	uint32_t nBytes = 0;
	for ( UINT i = 0; i < shader.BoundResources; ++i )
	{
		D3D12_SHADER_INPUT_BIND_DESC binding{};
		if ( FAILED( reflection->GetResourceBindingDesc( i, &binding ) ) )
			return UINT_MAX;
		if ( binding.Type != D3D_SIT_CBUFFER || binding.BindPoint != 0 || binding.Space != 1 )
			continue;
		if ( nBytes )
			return UINT_MAX;
		ID3D12ShaderReflectionConstantBuffer *buffer = reflection->GetConstantBufferByName( binding.Name );
		D3D12_SHADER_BUFFER_DESC desc{};
		if ( !buffer || FAILED( buffer->GetDesc( &desc ) ) )
			return UINT_MAX;
		nBytes = desc.Size;
	}
	return nBytes;
}
} // namespace

//-----------------------------------------------------------------------------
// Purpose: Loads one compute combo from shaders/csh; records live until device shutdown
//-----------------------------------------------------------------------------
ShaderRecordDX12 *CShaderAPIDX12::ResolveComputeShader( const char *pszName, int nStaticIndex, int nDynamicIndex )
{
	if ( !g_pShaderDeviceMgrDX12 || !g_pShaderDeviceMgrDX12->HostFileSystem() || !pszName || !*pszName || nStaticIndex < 0 || nDynamicIndex < 0 )
		return nullptr;
	CUtlString fileKey( "c:" );
	fileKey.Append( pszName );
	UtlHashHandle_t fileHandle = m_NamedShaderFiles.Find( fileKey.String() );
	if ( fileHandle == m_NamedShaderFiles.InvalidHandle() )
	{
		ShaderVcsFile *file = new ShaderVcsFile;
		CUtlString error;
		if ( !file->Open( *g_pShaderDeviceMgrDX12->HostFileSystem(), pszName, VcsStage::Compute, error ) )
		{
			delete file;
			m_NamedShaderFiles.Insert( fileKey.String(), nullptr );
			WarnComputeOnce( pszName, error.Get() );
			return nullptr;
		}
		fileHandle = m_NamedShaderFiles.Insert( fileKey.String(), file );
	}
	ShaderVcsFile *file = m_NamedShaderFiles[fileHandle];
	if ( !file )
		return nullptr;
	if ( static_cast<uint32_t>( nDynamicIndex ) >= file->DynamicComboCount() )
		return nullptr;
	CUtlString error;
	if ( !file->LoadStaticCombo( static_cast<uint32_t>( nStaticIndex ), error ) )
		return nullptr;
	const VcsPayload *payload = file->DynamicPayload( static_cast<uint32_t>( nStaticIndex ), static_cast<uint32_t>( nDynamicIndex ) );
	if ( !payload || payload->tokens.Count() < 4 || memcmp( payload->tokens.Base(), "DXBC", 4 ) != 0 )
		return nullptr;
	ShaderRecordDX12 *record = new ShaderRecordDX12;
	record->bytecode.CopyArray( payload->tokens.Base(), payload->tokens.Count() );
	m_ComputeShaderRecords.AddToTail( record );
	return record;
}

//-----------------------------------------------------------------------------
// Purpose: The one compute root signature: CBV b0 space1, SRV t0..t7, UAV u0..u7, s0 point and s1 linear clamp
//-----------------------------------------------------------------------------
bool CShaderAPIDX12::EnsureComputeRootSignature()
{
	if ( m_pComputeRoot )
		return true;
	if ( !m_pDevice || !m_pDevice->NativeDevice() )
		return false;
	D3D12_DESCRIPTOR_RANGE ranges[2]{};
	ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	ranges[0].NumDescriptors = SHADERAPIDX12_COMPUTE_MAX_SRVS;
	ranges[0].BaseShaderRegister = 0;
	ranges[0].RegisterSpace = 0;
	ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
	ranges[1].NumDescriptors = SHADERAPIDX12_COMPUTE_MAX_UAVS;
	ranges[1].BaseShaderRegister = 0;
	ranges[1].RegisterSpace = 0;
	D3D12_ROOT_PARAMETER parameters[3]{};
	parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	parameters[0].Descriptor.ShaderRegister = 0;
	parameters[0].Descriptor.RegisterSpace = 1;
	parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	parameters[1].DescriptorTable.NumDescriptorRanges = 1;
	parameters[1].DescriptorTable.pDescriptorRanges = &ranges[0];
	parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	parameters[2].DescriptorTable.NumDescriptorRanges = 1;
	parameters[2].DescriptorTable.pDescriptorRanges = &ranges[1];
	parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	D3D12_STATIC_SAMPLER_DESC samplers[2]{};
	for ( int i = 0; i < 2; ++i )
	{
		samplers[i].ShaderRegister = static_cast<UINT>( i );
		samplers[i].Filter = i == 0 ? D3D12_FILTER_MIN_MAG_MIP_POINT : D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		samplers[i].AddressU = samplers[i].AddressV = samplers[i].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		samplers[i].MaxLOD = D3D12_FLOAT32_MAX;
		samplers[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	}
	D3D12_ROOT_SIGNATURE_DESC desc{};
	desc.NumParameters = ARRAYSIZE( parameters );
	desc.pParameters = parameters;
	desc.NumStaticSamplers = ARRAYSIZE( samplers );
	desc.pStaticSamplers = samplers;
	Microsoft::WRL::ComPtr<ID3DBlob> serialized, errors;
	if ( FAILED( D3D12SerializeRootSignature( &desc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors ) ) )
		return false;
	return SUCCEEDED( m_pDevice->NativeDevice()->CreateRootSignature( 0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS( &m_pComputeRoot ) ) );
}

//-----------------------------------------------------------------------------
// Purpose: Re-creates a colour render target with UAV access and/or a full mip chain. materialsystem.dll
//          creates every render target with one mip and no UAV flag, so compute use is what asks for them.
//          Mip 0 keeps its contents and tracked state; added mips start as render targets.
//-----------------------------------------------------------------------------
bool CShaderAPIDX12::PromoteRenderTarget( TextureRecord &texture, bool bUav, int nMipLevels )
{
	if ( ( texture.uavCapable || !bUav ) && texture.mipLevels >= nMipLevels )
		return true;
	if ( !( texture.flags & TEXTURE_CREATE_RENDERTARGET ) || ( texture.flags & ( TEXTURE_CREATE_DEPTHBUFFER | TEXTURE_CREATE_CUBEMAP ) ) ||
		texture.copies != 1 || texture.depth != 1 || !EnsureTextureResident( texture ) )
		return false;

	m_pDevice->DrainRecording();
	const Microsoft::WRL::ComPtr<ID3D12Resource> pOld = texture.resource;
	m_pDevice->RetainResource( pOld.Get() );
	const int nOldMips = texture.mipLevels;
	CUtlVector<D3D12_RESOURCE_STATES> oldStates;
	oldStates.CopyArray( texture.subresourceStates.Base(), nOldMips );
	const unsigned char bGpuAuthoritative = texture.gpuAuthoritativeSubresources[0];

	for ( int i = 0; i < ARRAYSIZE( texture.m_SrvSources ); ++i )
	{
		m_Pipeline.ReleaseResourceDescriptor( texture.m_SrvSources[i], 0 );
		texture.m_SrvSources[i] = {};
		texture.m_pSrvResources[i] = nullptr;
		texture.m_SrvDescriptors[i] = {};
	}
	for ( PreparedTextureSlot &slot : m_PreparedTextureSlots )
		slot.valid = false;
	m_bTextureSetValid = false;
	m_PreparedSamplerTable = {};
	m_nPreparedSamplerFence = 0;

	texture.uavCapable = texture.uavCapable || bUav;
	if ( nMipLevels > nOldMips )
	{
		int nFullChain = 1;
		for ( int nExtent = MAX( texture.width, texture.height ); nExtent > 1; nExtent >>= 1 )
			++nFullChain;
		ResizeTextureStaging( texture, nFullChain );
		texture.gpuAuthoritativeSubresources[0] = bGpuAuthoritative;
	}
	if ( !AllocateNativeTexture( texture ) )
		return false;

	// The old resource keeps its shape; only the mips both resources share are copied.
	CCommandRecorderDX12 *pList = m_pDevice->CommandList();
	CUtlVector<D3D12_RESOURCE_BARRIER> barriers;
	auto addTransition = [&barriers]( ID3D12Resource *pResource, UINT nSubresource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after )
	{
		if ( before == after )
			return;
		D3D12_RESOURCE_BARRIER &barrier = barriers[barriers.AddToTail()];
		barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = pResource;
		barrier.Transition.StateBefore = before;
		barrier.Transition.StateAfter = after;
		barrier.Transition.Subresource = nSubresource;
	};
	const int nCopyMips = MIN( nOldMips, texture.mipLevels );
	for ( int nMip = 0; nMip < nCopyMips; ++nMip )
	{
		addTransition( pOld.Get(), nMip, oldStates[nMip], D3D12_RESOURCE_STATE_COPY_SOURCE );
		addTransition( texture.resource.Get(), nMip, texture.subresourceStates[nMip], D3D12_RESOURCE_STATE_COPY_DEST );
	}
	if ( barriers.Count() )
		pList->ResourceBarrier( barriers.Count(), barriers.Base() );
	for ( int nMip = 0; nMip < nCopyMips; ++nMip )
	{
		D3D12_TEXTURE_COPY_LOCATION source{}, destination{};
		source.pResource = pOld.Get();
		source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		source.SubresourceIndex = nMip;
		destination.pResource = texture.resource.Get();
		destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		destination.SubresourceIndex = nMip;
		pList->CopyTextureRegion( &destination, 0, 0, 0, &source, nullptr );
	}
	barriers.RemoveAll();
	for ( int nMip = 0; nMip < nCopyMips; ++nMip )
	{
		addTransition( texture.resource.Get(), nMip, D3D12_RESOURCE_STATE_COPY_DEST, oldStates[nMip] );
		texture.subresourceStates[nMip] = oldStates[nMip];
	}
	if ( barriers.Count() )
		pList->ResourceBarrier( barriers.Count(), barriers.Base() );
	texture.sampledStateValid = false;
	m_Pipeline.InvalidateGraphicsBindings();
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: IShaderAPIDX12Compute: records one dispatch into the frame's command stream
//-----------------------------------------------------------------------------
bool CShaderAPIDX12::Dispatch( const ShaderAPIDX12ComputeDispatch_t &dispatch )
{
	const char *name = dispatch.m_pShaderName;
	if ( !name || !*name || dispatch.m_nConstantBytes < 0 || dispatch.m_nConstantBytes > SHADERAPIDX12_COMPUTE_MAX_CONSTANTS || ( dispatch.m_nConstantBytes && !dispatch.m_pConstants ) || dispatch.m_nGroupsX <= 0 || dispatch.m_nGroupsY <= 0 || dispatch.m_nGroupsZ <= 0 )
	{
		WarnComputeOnce( name, "invalid dispatch arguments" );
		return false;
	}
	if ( !m_pDevice || !m_pDevice->IsRecordingOwner() || !m_pDevice->CommandList() || !EnsureComputeRootSignature() )
	{
		WarnComputeOnce( name, "recording or root signature unavailable" );
		return false;
	}
	ComputePipelineDX12 *pipeline = nullptr;
	for ( int i = 0; i < m_ComputePipelines.Count(); ++i )
	{
		ComputePipelineDX12 &candidate = m_ComputePipelines[i];
		if ( !V_stricmp( candidate.name.Get(), name ) && candidate.staticIndex == dispatch.m_nStaticIndex && candidate.dynamicIndex == dispatch.m_nDynamicIndex )
		{
			pipeline = &candidate;
			break;
		}
	}
	if ( !pipeline )
	{
		ShaderRecordDX12 *record = ResolveComputeShader( name, dispatch.m_nStaticIndex, dispatch.m_nDynamicIndex );
		if ( !record )
		{
			WarnComputeOnce( name, "shader combo unavailable" );
			return false;
		}
		const D3D12_SHADER_BYTECODE bytecode = record->Bytecode();
		// Reflection reports the cbuffer padded to a whole register; the caller's struct may stop short of it.
		const uint32_t constantBytes = ReflectedConstantBytes( bytecode );
		if ( constantBytes == UINT_MAX || ( ( dispatch.m_nConstantBytes + 15 ) & ~15 ) != static_cast<int>( constantBytes ) )
		{
			WarnComputeOnce( name, "constant buffer size does not match b0 space1" );
			return false;
		}
		ComputePipelineDX12 &entry = m_ComputePipelines[m_ComputePipelines.AddToTail()];
		entry.name = name;
		entry.staticIndex = dispatch.m_nStaticIndex;
		entry.dynamicIndex = dispatch.m_nDynamicIndex;
		entry.constantBytes = dispatch.m_nConstantBytes;
		D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
		desc.pRootSignature = m_pComputeRoot.Get();
		desc.CS = bytecode;
		if ( FAILED( m_pDevice->NativeDevice()->CreateComputePipelineState( &desc, IID_PPV_ARGS( &entry.pso ) ) ) )
		{
			m_ComputePipelines.Remove( m_ComputePipelines.Count() - 1 );
			WarnComputeOnce( name, "pipeline creation failed" );
			return false;
		}
		pipeline = &entry;
	}
	if ( pipeline->constantBytes != static_cast<uint32_t>( dispatch.m_nConstantBytes ) )
	{
		WarnComputeOnce( name, "constant buffer size changed" );
		return false;
	}
	auto resolveHandle = [this]( const ShaderAPIDX12ComputeResource_t &resource ) -> ShaderAPITextureHandle_t
	{
		if ( resource.m_nKind == SHADERAPIDX12_COMPUTE_RESOURCE_STANDARD_TEXTURE )
			return resource.m_nStandardTexture >= 0 && resource.m_nStandardTexture < TEXTURE_MAX_STD_TEXTURES ? m_StandardTextures[resource.m_nStandardTexture] : 0;
		return resource.m_hTexture;
	};
	// Render targets gain UAV access and the mips this dispatch addresses before any descriptor refers to them.
	for ( int i = 0; i < SHADERAPIDX12_COMPUTE_MAX_SRVS + SHADERAPIDX12_COMPUTE_MAX_UAVS; ++i )
	{
		const bool bUav = i >= SHADERAPIDX12_COMPUTE_MAX_SRVS;
		const ShaderAPIDX12ComputeResource_t &resource = bUav ? dispatch.m_Uav[i - SHADERAPIDX12_COMPUTE_MAX_SRVS] : dispatch.m_Srv[i];
		if ( resource.m_nKind != SHADERAPIDX12_COMPUTE_RESOURCE_TEXTURE && resource.m_nKind != SHADERAPIDX12_COMPUTE_RESOURCE_STANDARD_TEXTURE )
			continue;
		TextureRecord *record = FindTexture( resolveHandle( resource ) );
		if ( record && ( record->flags & TEXTURE_CREATE_RENDERTARGET ) && !PromoteRenderTarget( *record, bUav, MAX( resource.m_nMip, 0 ) + MAX( resource.m_nMipCount, 1 ) ) )
		{
			WarnComputeOnce( name, "render target could not be promoted for compute" );
			return false;
		}
	}
	FlushBufferedPrimitives();
	CCommandRecorderDX12 *list = m_pDevice->CommandList();
	const UINT stride = m_pDevice->NativeDevice()->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
	const DescriptorRangeDX12 range = m_Pipeline.AllocateTransientResources( kComputeDescriptorCount, m_pDevice->NextFenceValue() );
	if ( range.count != kComputeDescriptorCount )
	{
		WarnComputeOnce( name, "descriptor allocation failed" );
		return false;
	}
	D3D12_SHADER_RESOURCE_VIEW_DESC nullSrv{};
	nullSrv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	nullSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	nullSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	nullSrv.Texture2D.MipLevels = 1;
	D3D12_UNORDERED_ACCESS_VIEW_DESC nullUav{};
	nullUav.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	nullUav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	for ( UINT i = 0; i < SHADERAPIDX12_COMPUTE_MAX_SRVS; ++i )
		m_pDevice->NativeDevice()->CreateShaderResourceView( nullptr, &nullSrv, Offset( range.cpu, i, stride ) );
	for ( UINT i = 0; i < SHADERAPIDX12_COMPUTE_MAX_UAVS; ++i )
		m_pDevice->NativeDevice()->CreateUnorderedAccessView( nullptr, nullptr, &nullUav, Offset( range.cpu, SHADERAPIDX12_COMPUTE_MAX_SRVS + i, stride ) );
	ComputeReplayPayloadDX12 payload;
	payload.root = m_pComputeRoot.Get();
	payload.pso = pipeline->pso.Get();
	payload.heap = m_Pipeline.ResourceDescriptorHeap();
	payload.srvTable = range.gpu;
	payload.uavTable = Offset( range.gpu, SHADERAPIDX12_COMPUTE_MAX_SRVS, stride );
	payload.groupsX = static_cast<UINT>( dispatch.m_nGroupsX );
	payload.groupsY = static_cast<UINT>( dispatch.m_nGroupsY );
	payload.groupsZ = static_cast<UINT>( dispatch.m_nGroupsZ );
	const uint32_t zero = 0;
	const void *pConstants = dispatch.m_nConstantBytes ? dispatch.m_pConstants : &zero;
	const size_t constantBytes = dispatch.m_nConstantBytes ? static_cast<size_t>( dispatch.m_nConstantBytes ) : sizeof( zero );
	if ( !m_Pipeline.UploadTransient( pConstants, constantBytes, constantBytes, 256, m_pDevice->NextFenceValue(), payload.constants ) )
		return false;
	ID3D12Resource *retained[SHADERAPIDX12_COMPUTE_MAX_SRVS + SHADERAPIDX12_COMPUTE_MAX_UAVS + 1]{};
	int retainedCount = 0;
	auto retain = [&]( ID3D12Resource *resource )
	{
		if ( resource )
			retained[retainedCount++] = resource;
	};
	auto bindSrv = [&]( const ShaderAPIDX12ComputeResource_t &resource, UINT slot ) -> bool
	{
		if ( resource.m_nKind == SHADERAPIDX12_COMPUTE_RESOURCE_NONE )
			return true;
		if ( resource.m_nKind == SHADERAPIDX12_COMPUTE_RESOURCE_SCENE_DEPTH )
		{
			if ( !m_pDevice->SceneDepth() )
				return false;
			D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
			srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srv.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
			if ( m_pDevice->SceneSampleCount() > 1 )
			{
				srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
			}
			else
			{
				srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
				srv.Texture2D.MipLevels = 1;
			}
			m_pDevice->NativeDevice()->CreateShaderResourceView( m_pDevice->SceneDepth(), &srv, Offset( range.cpu, slot, stride ) );
			payload.sceneDepth = m_pDevice->SceneDepth();
			payload.sceneDepthBefore = m_pDevice->SceneDepthState();
			payload.restoreSceneDepth = true;
			if ( payload.sceneDepthBefore != kSceneDepthRead && !AddTransition( payload, payload.sceneDepth, payload.sceneDepthBefore, kSceneDepthRead, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES ) )
				return false;
			retain( payload.sceneDepth );
			return true;
		}
		const ShaderAPITextureHandle_t handle = resolveHandle( resource );
		TextureRecord *record = FindTexture( handle );
		const int mip = MAX( resource.m_nMip, 0 );
		const int mipCount = resource.m_nMipCount > 0 ? resource.m_nMipCount : 0;
		if ( !record || mip >= record->mipLevels || ( mipCount > 0 && mip + mipCount > record->mipLevels ) )
			return false;
		ID3D12Resource *native = nullptr;
		D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
		D3D12_SAMPLER_DESC sampler{};
		if ( !PrepareSampledTexture( handle, false, &native, srv, sampler, nullptr, false, mip, mipCount ) || !native )
			return false;
		if ( srv.ViewDimension == D3D12_SRV_DIMENSION_TEXTURE2D )
		{
			srv.Texture2D.MostDetailedMip = mip;
			srv.Texture2D.MipLevels = mipCount > 0 ? mipCount : record->mipLevels - mip;
		}
		m_pDevice->NativeDevice()->CreateShaderResourceView( native, &srv, Offset( range.cpu, slot, stride ) );
		retain( native );
		return true;
	};
	auto bindUav = [&]( const ShaderAPIDX12ComputeResource_t &resource, UINT slot ) -> bool
	{
		if ( resource.m_nKind == SHADERAPIDX12_COMPUTE_RESOURCE_NONE )
			return true;
		const ShaderAPITextureHandle_t handle = resolveHandle( resource );
		TextureRecord *record = FindTexture( handle );
		const int mip = MAX( resource.m_nMip, 0 );
		// A UAV must not alias the bound render or depth targets.
		bool bBoundTarget = handle == m_hDepthTarget;
		for ( ShaderAPITextureHandle_t hTarget : m_RenderTargets )
			bBoundTarget = bBoundTarget || hTarget == handle;
		if ( !record || bBoundTarget || !record->uavCapable || mip >= record->mipLevels )
			return false;
		const int nBase = record->currentCopy * ( ( record->flags & TEXTURE_CREATE_CUBEMAP ) ? 6 : 1 ) * record->mipLevels;
		const int nState = nBase + mip;
		const D3D12_RESOURCE_STATES before = record->subresourceStates[nState];
		if ( !AddTransition( payload, record->resource.Get(), before, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, static_cast<UINT>( mip ) ) )
			return false;
		record->subresourceStates[nState] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		record->sampledStateValid = false;
		++m_nTextureStateEpoch;
		D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
		uav.Format = record->resource->GetDesc().Format;
		uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		uav.Texture2D.MipSlice = mip;
		m_pDevice->NativeDevice()->CreateUnorderedAccessView( record->resource.Get(), nullptr, &uav, Offset( range.cpu, SHADERAPIDX12_COMPUTE_MAX_SRVS + slot, stride ) );
		retain( record->resource.Get() );
		return true;
	};
	for ( UINT i = 0; i < SHADERAPIDX12_COMPUTE_MAX_SRVS; ++i )
	{
		if ( !bindSrv( dispatch.m_Srv[i], i ) )
		{
			WarnComputeOnce( name, "SRV resource could not be bound" );
			return false;
		}
	}
	for ( UINT i = 0; i < SHADERAPIDX12_COMPUTE_MAX_UAVS; ++i )
	{
		if ( !bindUav( dispatch.m_Uav[i], i ) )
		{
			WarnComputeOnce( name, "UAV resource could not be bound" );
			return false;
		}
	}
	list->ExternalCommand( &ComputeReplayThunk, payload );
	m_Pipeline.InvalidateGraphicsBindings();
	for ( int i = 0; i < retainedCount; ++i )
		m_pDevice->RetainResource( retained[i] );
	if ( payload.restoreSceneDepth )
		m_pDevice->SetSceneStatesAfterExternal( m_pDevice->SceneColorState(), D3D12_RESOURCE_STATE_DEPTH_WRITE );
	return true;
}
} // namespace shaderapidx12
