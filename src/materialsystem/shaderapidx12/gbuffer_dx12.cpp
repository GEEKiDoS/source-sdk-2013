//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Private PBR G-buffer, scene-sized and scene-multisampled: render target 1 = world-space octahedral normal (16:16 in x)
//          plus the depth the surface was drawn at (asuint( SV_Position.z ) in y), R32G32_UINT; render target 2 = F0.rgb +
//          roughness, R8G8B8A8_UNORM. INT_RENDERPARM_DX12_PBR_GBUFFER_PASS brackets the opaque scene pass of a frame whose PBR
//          override is committed on. DrawBuffers appends the targets to every native PS that declares three SV_Targets.
//          A legacy draw does not write them (an RT bound without a matching PS output is not written, whatever the blend
//          state), so a pixel holds PBR data only while the scene depth still equals the stored depth: consumers compare it
//          with the scene depth (README "PBR G-buffer contract"). Both targets clear to 0 at the start of the pass, and a
//          PBR draw never writes roughness 0 (PBR_MIN_ROUGHNESS), so specular alpha 0 marks pixels nothing has drawn.
//          Lifecycle mirrors the private motion target (motion_vectors_dx12.cpp).
//
//=============================================================================//

#include "shaderapi_dx12.h"
#include "shaderdevice_dx12.h"
#include "renderparm.h"
#include "tier0/dbg.h"

namespace shaderapidx12
{
//-----------------------------------------------------------------------------
// Purpose: Creates (or recreates on a scene shape change) both targets and their RTVs
//-----------------------------------------------------------------------------
bool CShaderAPIDX12::EnsureGBufferResources()
{
	if ( !m_pDevice->NativeDevice() || !m_pDevice->SceneColor() )
		return false;
	const D3D12_RESOURCE_DESC scene = m_pDevice->SceneColor()->GetDesc();
	if ( m_pGBufferNormal && scene.Width == m_nGBufferWidth && scene.Height == m_nGBufferHeight && scene.SampleDesc.Count == m_nGBufferSamples && scene.SampleDesc.Quality == m_nGBufferQuality )
		return true;
	if ( !m_pDevice->SupportsMSAAFormat( kGBufferFormats[0], scene.SampleDesc.Count, scene.SampleDesc.Quality ) || !m_pDevice->SupportsMSAAFormat( kGBufferFormats[1], scene.SampleDesc.Count, scene.SampleDesc.Quality ) )
	{
		if ( !m_bGBufferWarned )
		{
			m_bGBufferWarned = true;
			Warning( "ShaderAPIDX12: PBR G-buffer unavailable at %ux MSAA quality %u\n", scene.SampleDesc.Count, scene.SampleDesc.Quality );
		}
		return false;
	}
	m_pDevice->DrainRecording();
	if ( m_pGBufferNormal )
	{
		m_pDevice->RetainResource( m_pGBufferNormal.Get() );
		m_pDevice->RetainResource( m_pGBufferSpec.Get() );
	}
	ID3D12Device *pNative = m_pDevice->NativeDevice();
	if ( !m_pGBufferRtvHeap )
	{
		D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
		heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		heapDesc.NumDescriptors = ARRAYSIZE( m_GBufferRtv );
		if ( FAILED( pNative->CreateDescriptorHeap( &heapDesc, IID_PPV_ARGS( &m_pGBufferRtvHeap ) ) ) )
			return false;
	}
	const UINT rtvStride = pNative->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_RTV );
	// The pass clears both targets to 0 ("no PBR data"); the optimized clear value matches.
	Microsoft::WRL::ComPtr<ID3D12Resource> resources[2];
	D3D12_HEAP_PROPERTIES heap{};
	heap.Type = D3D12_HEAP_TYPE_DEFAULT;
	for ( int i = 0; i < 2; ++i )
	{
		D3D12_RESOURCE_DESC desc{};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		desc.Width = scene.Width;
		desc.Height = scene.Height;
		desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.Format = kGBufferFormats[i];
		desc.SampleDesc = scene.SampleDesc;
		desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		D3D12_CLEAR_VALUE clear{};
		clear.Format = desc.Format;
		if ( FAILED( pNative->CreateCommittedResource( &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RENDER_TARGET, &clear, IID_PPV_ARGS( &resources[i] ) ) ) )
			return false;
		m_GBufferRtv[i] = m_pGBufferRtvHeap->GetCPUDescriptorHandleForHeapStart();
		m_GBufferRtv[i].ptr += SIZE_T( i ) * rtvStride;
		D3D12_RENDER_TARGET_VIEW_DESC rtv{};
		rtv.Format = desc.Format;
		rtv.ViewDimension = desc.SampleDesc.Count > 1 ? D3D12_RTV_DIMENSION_TEXTURE2DMS : D3D12_RTV_DIMENSION_TEXTURE2D;
		pNative->CreateRenderTargetView( resources[i].Get(), &rtv, m_GBufferRtv[i] );
	}
	m_pGBufferNormal = std::move( resources[0] );
	m_pGBufferSpec = std::move( resources[1] );
	m_GBufferState[0] = m_GBufferState[1] = D3D12_RESOURCE_STATE_RENDER_TARGET;
	m_nGBufferWidth = static_cast<UINT>( scene.Width );
	m_nGBufferHeight = scene.Height;
	m_nGBufferSamples = scene.SampleDesc.Count;
	m_nGBufferQuality = scene.SampleDesc.Quality;
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Records a barrier moving both targets to the desired state
//-----------------------------------------------------------------------------
void CShaderAPIDX12::TransitionGBuffer( D3D12_RESOURCE_STATES desiredState )
{
	ID3D12Resource *const resources[2] = { m_pGBufferNormal.Get(), m_pGBufferSpec.Get() };
	D3D12_RESOURCE_BARRIER barriers[2]{};
	UINT count = 0;
	for ( int i = 0; i < 2; ++i )
		if ( m_GBufferState[i] != desiredState )
		{
			D3D12_RESOURCE_BARRIER &barrier = barriers[count++];
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Transition.pResource = resources[i];
			barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			barrier.Transition.StateBefore = m_GBufferState[i];
			barrier.Transition.StateAfter = desiredState;
			m_GBufferState[i] = desiredState;
		}
	if ( count )
		m_pDevice->CommandList()->ResourceBarrier( count, barriers );
}

//-----------------------------------------------------------------------------
// Purpose: INT_RENDERPARM_DX12_PBR_GBUFFER_PASS handler. BEGIN clears the targets and routes them to PBR draws;
//			END leaves them shader-readable. While the committed PBR override is off there are no producers: BEGIN
//			releases the targets instead (consumers read the null "no data" view) and no draw gets extra targets.
//-----------------------------------------------------------------------------
void CShaderAPIDX12::SetGBufferPass( int nMode )
{
	if ( !m_pDevice || !m_pDevice->CommandList() )
		return;
	if ( nMode == DX12_GBUFFER_PASS_END )
	{
		if ( !m_bGBufferPass )
			return;
		FlushBufferedPrimitives(); // still drawn with the G-buffer targets bound
		m_bGBufferPass = false;
		TransitionGBuffer( kGBufferReadState );
		return;
	}
	FlushBufferedPrimitives();
	m_bGBufferPass = false;
	if ( !m_pDevice->Lighting().PbrOverride() )
	{
		if ( m_pGBufferNormal )
		{
			m_pDevice->RetainResource( m_pGBufferNormal.Get() );
			m_pDevice->RetainResource( m_pGBufferSpec.Get() );
			ReleaseGBufferResources();
		}
		return;
	}
	if ( !EnsureGBufferResources() )
		return;
	TransitionGBuffer( D3D12_RESOURCE_STATE_RENDER_TARGET );
	const float noData[4] = {};
	m_pDevice->CommandList()->ClearRenderTargetView( m_GBufferRtv[0], noData, 0, nullptr );
	m_pDevice->CommandList()->ClearRenderTargetView( m_GBufferRtv[1], noData, 0, nullptr );
	m_bGBufferPass = true;
}

//-----------------------------------------------------------------------------
// Purpose: Releases both targets and ends any pass (device reset / texture teardown)
//-----------------------------------------------------------------------------
void CShaderAPIDX12::ReleaseGBufferResources()
{
	m_bGBufferPass = false;
	m_bGBufferWarned = false;
	m_pGBufferRtvHeap.Reset();
	m_pGBufferNormal.Reset();
	m_pGBufferSpec.Reset();
}
} // namespace shaderapidx12
