//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Private PBR G-buffer, scene-sized and scene-multisampled, main view only.
//          Render target 1 = world-space octahedral shading normal, R16G16_UNORM.
//          Render target 2 = F0.rgb + roughness, R8G8B8A8_UNORM.
//          Render target 3 = E.rgb, the material's environment (cubemap) contribution in final scene units; alpha D,
//                            the specular-luminance-weighted light distance of target 5 (0 when it is zero).
//          Render target 4 = K.rgb, the sampled-scene reflection response, zero unless a fresh eligible writer;
//                            alpha 1 for such a writer.
//          Render target 5 = S.rgb, the direct GGX specular in final scene units, alpha 0.
//          Targets 3/4/5 are R16G16B16A16_FLOAT, or R11G11B10_FLOAT at r_color_precision 1 (no alphas; render
//          target 6 = D, R16_FLOAT, then carries the distance). The format is chosen when the payload is created.
//          Targets 3/4/5(/6) exist only once an SSR bracket asked for them and are attached only while
//          m_ScreenSpace.ssrCapture is set; BEGIN clears exactly those three so only this bracket's fresh
//          PBR writers carry a response (water issues several main brackets per frame).
//          Validity is stencil bit 0x80 of the scene depth: every depth-writing draw in the pass sets it
//          for a PBR writer and clears it otherwise. Consumers read that plane, sample 0.
//          INT_RENDERPARM_DX12_PBR_GBUFFER_PASS brackets the opaque pass. BEGIN does not clear targets 1/2.
//          DrawBuffers appends the targets to every native PS that declares eight SV_Targets.
//          Lifecycle mirrors the private motion target (motion_vectors_dx12.cpp).
//
//=============================================================================//

#include "shaderapi_dx12.h"
#include "shaderdevice_dx12.h"
#include "renderparm.h"
#include "tier0/dbg.h"
#include "tier1/convar.h"

namespace shaderapidx12
{
static ConVar r_color_precision( "r_color_precision", "0", FCVAR_ARCHIVE,
	"SSR payload G-buffer precision: 0 RGBA16F environment/response/specular targets, 1 R11G11B10F (plus an R16F light-distance target)",
	true, 0, true, 1 );
namespace
{
//-----------------------------------------------------------------------------
// Purpose: Creates one scene-shaped G-buffer target (optimized clear value 0) and its RTV
//-----------------------------------------------------------------------------
bool CreateGBufferTarget( ID3D12Device *pNative, const D3D12_RESOURCE_DESC &scene, DXGI_FORMAT format, D3D12_CPU_DESCRIPTOR_HANDLE rtv, Microsoft::WRL::ComPtr<ID3D12Resource> &resource )
{
	D3D12_HEAP_PROPERTIES heap{};
	heap.Type = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_RESOURCE_DESC desc{};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = scene.Width;
	desc.Height = scene.Height;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = format;
	desc.SampleDesc = scene.SampleDesc;
	desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	D3D12_CLEAR_VALUE clear{};
	clear.Format = format;
	if ( FAILED( CreateCommittedResourceDX12( pNative, &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RENDER_TARGET, &clear, IID_PPV_ARGS( &resource ), "G-buffer", "DX12 G-buffer" ) ) )
		return false;
	D3D12_RENDER_TARGET_VIEW_DESC view{};
	view.Format = format;
	view.ViewDimension = scene.SampleDesc.Count > 1 ? D3D12_RTV_DIMENSION_TEXTURE2DMS : D3D12_RTV_DIMENSION_TEXTURE2D;
	pNative->CreateRenderTargetView( resource.Get(), &view, rtv );
	return true;
}
} // namespace

//-----------------------------------------------------------------------------
// Purpose: Resource of G-buffer target nTarget (0 normal, 1 F0/roughness, 2 environment, 3 SSR response, 4 direct
//          specular, 5 light distance); null when absent
//-----------------------------------------------------------------------------
ID3D12Resource *CShaderAPIDX12::GBufferTarget( UINT nTarget ) const
{
	switch ( nTarget )
	{
	case 0: return m_pGBufferNormal.Get();
	case 1: return m_pGBufferSpec.Get();
	case 2: return m_pGBufferEnvironment.Get();
	case 3: return m_pGBufferResponse.Get();
	case 4: return m_pGBufferSpecular.Get();
	case kGBufferDistanceTarget: return m_pGBufferDistance.Get();
	case kGBufferAlbedoTarget: return m_pGBufferAlbedo.Get();
	default: return nullptr;
	}
}

DXGI_FORMAT CShaderAPIDX12::GBufferFormat( UINT nTarget ) const
{
	switch ( nTarget )
	{
	case 0: return DXGI_FORMAT_R16G16_UNORM;
	case 1: return DXGI_FORMAT_R8G8B8A8_UNORM;
	case kGBufferDistanceTarget: return DXGI_FORMAT_R16_FLOAT;
	case kGBufferAlbedoTarget: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	default: return m_bGBufferReducedPayload ? DXGI_FORMAT_R11G11B10_FLOAT : DXGI_FORMAT_R16G16B16A16_FLOAT;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Creates (or recreates on a scene shape change) the normal and F0/roughness targets and their RTVs.
//			A shape change drops the SSR payload targets too; the next capturing BEGIN recreates them.
//-----------------------------------------------------------------------------
bool CShaderAPIDX12::EnsureGBufferResources()
{
	if ( !m_pDevice->NativeDevice() || !m_pDevice->SceneColor() )
		return false;
	const D3D12_RESOURCE_DESC scene = m_pDevice->SceneColor()->GetDesc();
	if ( m_pGBufferNormal && scene.Width == m_nGBufferWidth && scene.Height == m_nGBufferHeight && scene.SampleDesc.Count == m_nGBufferSamples && scene.SampleDesc.Quality == m_nGBufferQuality )
		return true;
	if ( !m_pDevice->SupportsMSAAFormat( GBufferFormat( 0 ), scene.SampleDesc.Count, scene.SampleDesc.Quality ) || !m_pDevice->SupportsMSAAFormat( GBufferFormat( 1 ), scene.SampleDesc.Count, scene.SampleDesc.Quality ) )
	{
		if ( !m_bGBufferWarned )
		{
			m_bGBufferWarned = true;
			Warning( "ShaderAPIDX12: PBR G-buffer unavailable at %ux MSAA quality %u\n", scene.SampleDesc.Count, scene.SampleDesc.Quality );
		}
		return false;
	}
	m_pDevice->DrainRecording();
	for ( UINT i = 0; i < kGBufferTargets; ++i )
		if ( ID3D12Resource *pOld = GBufferTarget( i ) )
			m_pDevice->RetainResource( pOld );
	m_pGBufferEnvironment.Reset();
	m_pGBufferResponse.Reset();
	m_pGBufferSpecular.Reset();
	m_pGBufferDistance.Reset();
	m_pGBufferAlbedo.Reset();
	ID3D12Device *pNative = m_pDevice->NativeDevice();
	if ( !m_pGBufferRtvHeap )
	{
		D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
		heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		heapDesc.NumDescriptors = kGBufferTargets + 1;
		if ( FAILED( pNative->CreateDescriptorHeap( &heapDesc, IID_PPV_ARGS( &m_pGBufferRtvHeap ) ) ) )
			return false;
		const UINT rtvStride = pNative->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_RTV );
		for ( UINT i = 0; i < kGBufferTargets; ++i )
		{
			m_GBufferRtv[i] = m_pGBufferRtvHeap->GetCPUDescriptorHandleForHeapStart();
			m_GBufferRtv[i].ptr += SIZE_T( i ) * rtvStride;
		}
	}
	// A real null RTV lets the optional SV_Target7 coexist with absent SSR outputs 3..6.
	m_GBufferNullRtv.ptr = m_GBufferRtv[kGBufferAlbedoTarget].ptr + pNative->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_RTV );
	D3D12_RENDER_TARGET_VIEW_DESC nullRtv{};
	nullRtv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	nullRtv.ViewDimension = scene.SampleDesc.Count > 1 ? D3D12_RTV_DIMENSION_TEXTURE2DMS : D3D12_RTV_DIMENSION_TEXTURE2D;
	pNative->CreateRenderTargetView( nullptr, &nullRtv, m_GBufferNullRtv );
	// Optimized clear value is 0. The pass does not clear these two; stencil bit 0x80 is the validity mark.
	Microsoft::WRL::ComPtr<ID3D12Resource> resources[kGBufferBaseTargets];
	for ( UINT i = 0; i < kGBufferBaseTargets; ++i )
		if ( !CreateGBufferTarget( pNative, scene, GBufferFormat( i ), m_GBufferRtv[i], resources[i] ) )
			return false;
	m_pGBufferNormal = std::move( resources[0] );
	m_pGBufferSpec = std::move( resources[1] );
	for ( D3D12_RESOURCE_STATES &state : m_GBufferState )
		state = D3D12_RESOURCE_STATE_RENDER_TARGET;
	m_nGBufferWidth = static_cast<UINT>( scene.Width );
	m_nGBufferHeight = scene.Height;
	m_nGBufferSamples = scene.SampleDesc.Count;
	m_nGBufferQuality = scene.SampleDesc.Quality;
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: The optional albedo target lives only while the main camera requests it.
//-----------------------------------------------------------------------------
void CShaderAPIDX12::SetGBufferAlbedoEnabled( bool enabled )
{
	if ( enabled && !m_pGBufferAlbedo )
	{
		const D3D12_RESOURCE_DESC scene = m_pDevice->SceneColor()->GetDesc();
		const DXGI_FORMAT format = GBufferFormat( kGBufferAlbedoTarget );
		if ( m_pDevice->SupportsMSAAFormat( format, scene.SampleDesc.Count, scene.SampleDesc.Quality ) &&
		     CreateGBufferTarget( m_pDevice->NativeDevice(), scene, format, m_GBufferRtv[kGBufferAlbedoTarget], m_pGBufferAlbedo ) )
			m_GBufferState[kGBufferAlbedoTarget] = D3D12_RESOURCE_STATE_RENDER_TARGET;
	}
	else if ( !enabled && m_pGBufferAlbedo )
	{
		m_pDevice->RetainResource( m_pGBufferAlbedo.Get() );
		m_pGBufferAlbedo.Reset();
	}
}

// Recreate the optional SSR attachments independently of bounce albedo.
bool CShaderAPIDX12::EnsureGBufferSsrResources()
{
	const bool bReduced = r_color_precision.GetBool();
	if ( m_pGBufferEnvironment && m_bGBufferReducedPayload == bReduced )
		return true;
	if ( !m_pGBufferNormal )
		return false;
	const D3D12_RESOURCE_DESC scene = m_pDevice->SceneColor()->GetDesc();
	const DXGI_FORMAT payloadFormat = bReduced ? DXGI_FORMAT_R11G11B10_FLOAT : DXGI_FORMAT_R16G16B16A16_FLOAT;
	if ( !m_pDevice->SupportsMSAAFormat( payloadFormat, scene.SampleDesc.Count, scene.SampleDesc.Quality ) ||
	     ( bReduced && !m_pDevice->SupportsMSAAFormat( DXGI_FORMAT_R16_FLOAT, scene.SampleDesc.Count, scene.SampleDesc.Quality ) ) )
	{
		if ( !m_bGBufferSsrWarned )
		{
			m_bGBufferSsrWarned = true;
			Warning( "ShaderAPIDX12: SSR G-buffer payload unavailable at %ux MSAA quality %u\n", scene.SampleDesc.Count, scene.SampleDesc.Quality );
		}
		return false;
	}
	m_pDevice->DrainRecording();
	for ( UINT i = kGBufferBaseTargets; i < kGBufferAlbedoTarget; ++i )
		if ( ID3D12Resource *pOld = GBufferTarget( i ) )
			m_pDevice->RetainResource( pOld );
	m_pGBufferEnvironment.Reset();
	m_pGBufferResponse.Reset();
	m_pGBufferSpecular.Reset();
	m_pGBufferDistance.Reset();
	m_bGBufferReducedPayload = bReduced;
	ID3D12Device *pNative = m_pDevice->NativeDevice();
	Microsoft::WRL::ComPtr<ID3D12Resource> resources[kGBufferAlbedoTarget - kGBufferBaseTargets];
	const UINT nPayload = GBufferPayloadTargets() - kGBufferBaseTargets;
	for ( UINT i = 0; i < nPayload; ++i )
		if ( !CreateGBufferTarget( pNative, scene, GBufferFormat( kGBufferBaseTargets + i ), m_GBufferRtv[kGBufferBaseTargets + i], resources[i] ) )
			return false;
	m_pGBufferEnvironment = std::move( resources[0] );
	m_pGBufferResponse = std::move( resources[1] );
	m_pGBufferSpecular = std::move( resources[2] );
	m_pGBufferDistance = std::move( resources[3] );
	for ( UINT i = kGBufferBaseTargets; i < kGBufferAlbedoTarget; ++i )
		m_GBufferState[i] = D3D12_RESOURCE_STATE_RENDER_TARGET;
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Records a barrier moving every existing target to the desired state
//-----------------------------------------------------------------------------
void CShaderAPIDX12::TransitionGBuffer( D3D12_RESOURCE_STATES desiredState )
{
	D3D12_RESOURCE_BARRIER barriers[kGBufferTargets]{};
	UINT count = 0;
	for ( UINT i = 0; i < kGBufferTargets; ++i )
	{
		ID3D12Resource *pResource = GBufferTarget( i );
		if ( !pResource || m_GBufferState[i] == desiredState )
			continue;
		D3D12_RESOURCE_BARRIER &barrier = barriers[count++];
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = pResource;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = m_GBufferState[i];
		barrier.Transition.StateAfter = desiredState;
		m_GBufferState[i] = desiredState;
	}
	if ( count )
		m_pDevice->CommandList()->ResourceBarrier( count, barriers );
}

//-----------------------------------------------------------------------------
// Purpose: INT_RENDERPARM_DX12_PBR_GBUFFER_PASS handler. BEGIN makes the targets renderable and, in an
//			SSR-capturing bracket, clears only the E/K/S payload; END leaves them shader-readable.
//			Validity is the stencil bit, so BEGIN clears nothing else.
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
	const bool viewModel = nMode == DX12_GBUFFER_PASS_VIEWMODEL;
	const bool rr = RayReconstructionView();
	if ( viewModel && ( !m_pGBufferAlbedo || ( !rr && m_nBounceFrame != m_nFrameCounter ) ) )
		return;
	if ( !EnsureGBufferResources() )
		return;
	// A failed payload allocation keeps the pass at three targets; DrawBuffers checks the resource, and the SSR
	// dispatch skips a bracket whose payload never existed.
	SetGBufferAlbedoEnabled( rr || nMode != DX12_GBUFFER_PASS_BEGIN );
	const bool bSsrPayload = m_ScreenSpace.ssrCapture && EnsureGBufferSsrResources();
	TransitionGBuffer( D3D12_RESOURCE_STATE_RENDER_TARGET );
	// Preserve the opaque albedo behind the viewmodel for the final full-frame RR guide.
	if ( m_pGBufferAlbedo && !( rr && viewModel ) )
	{
		static constexpr float clear[4] = {};
		m_pDevice->CommandList()->ClearRenderTargetView( m_GBufferRtv[kGBufferAlbedoTarget], clear, 0, nullptr );
	}
	if ( bSsrPayload )
	{
		static constexpr float kZero[4] = { 0.f, 0.f, 0.f, 0.f };
		CCommandRecorderDX12 *pList = m_pDevice->CommandList();
		for ( UINT i = kGBufferBaseTargets; i < GBufferPayloadTargets(); ++i )
			pList->ClearRenderTargetView( m_GBufferRtv[i], kZero, 0, nullptr );
	}
	m_bGBufferPass = true;
}

//-----------------------------------------------------------------------------
// Purpose: Releases every target and ends any pass (device reset / texture teardown)
//-----------------------------------------------------------------------------
void CShaderAPIDX12::ReleaseGBufferResources()
{
	m_bGBufferPass = false;
	m_bGBufferWarned = false;
	m_bGBufferSsrWarned = false;
	m_pGBufferRtvHeap.Reset();
	m_pGBufferAlbedo.Reset();
	m_pGBufferNormal.Reset();
	m_pGBufferSpec.Reset();
	m_pGBufferEnvironment.Reset();
	m_pGBufferResponse.Reset();
	m_pGBufferSpecular.Reset();
	m_pGBufferDistance.Reset();
}
} // namespace shaderapidx12
