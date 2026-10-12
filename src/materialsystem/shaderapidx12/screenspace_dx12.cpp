//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Screen-space effect brackets of shaderapidx12.dll (plan §2-3): the IShaderAPIDX12ScreenSpace markers,
//          their ordered replay on the recording owner, the per-bracket view constants / space-6 bindings of
//          ordinary forward draws and the private contact-depth replay target.
//          SSR lives in screenspace_ssr_dx12.cpp, volumetric lighting in screenspace_volumetric_dx12.cpp.
//
//          Markers queue an immutable pooled refcounted packet exactly like CLightingDX12::Enqueue: Flush( false )
//          on the current render context, then QueueFunctor through the explicit CMemberFunctor0 /
//          CFuncMemPolicyRefCount pair (tier1's CreateRefCountingFunctor never instantiates upstream), or a direct
//          replay when there is no call queue.
//
//=============================================================================//

#include "shaderapi_dx12.h"
#include "shaderdevice_dx12.h"
#include "shaderapi/ishaderapidx12lighting.h"
#include "materialsystem/imaterialsystem.h"
#include "tier2/tier2.h"
#include "tier1/callqueue.h"
#include "tier1/refcount.h"
#include "tier0/dbg.h"
#include <atomic>
#include <cstring>

namespace shaderapidx12
{
namespace
{
enum ScreenSpaceOpDX12 { ScreenSpaceBeginView, ScreenSpaceBeginContactDepth, ScreenSpaceEndContactDepth, ScreenSpaceDispatchSSR, ScreenSpaceEndView, ScreenSpaceBeginVolumetrics, ScreenSpaceDrawVolumetrics };

// depth.yzw of DX12ScreenSpaceViewConstants (§2); depth.x is the native clear value: ClearBuffers always clears native
// depth to 1 (textures_dx12.cpp ClearDepthStencilView), so the clip-space far plane is the only "no geometry" depth.
constexpr float kContactNormalBias = 0.25f, kContactFirstDistance = 0.5f, kContactEdgeFade = 8.f, kNativeDepthClear = 1.f;
} // namespace

//-----------------------------------------------------------------------------
// Purpose: Pooled marker packet; the queue functor AddRefs it for the queue's lifetime
//-----------------------------------------------------------------------------
struct CShaderAPIDX12::ScreenSpacePacketDX12 final : IRefCounted
{
	CShaderAPIDX12 *owner = nullptr;
	std::atomic<int> references{ 1 };
	int operation = 0;
	DX12ScreenSpaceViewDesc view{};
	DX12VolumetricSettings settings{};
	int AddRef() override { return ++references; }
	int Release() override
	{
		const int count = --references;
		if ( !count )
			owner->RecycleScreenSpacePacket( this );
		return count;
	}
	void Replay() { owner->ExecuteScreenSpacePacket( this ); }
};

CShaderAPIDX12::ScreenSpacePacketDX12 *CShaderAPIDX12::ScreenSpacePacket( int nOperation )
{
	ScreenSpacePacketDX12 *packet;
	{
		AUTO_LOCK( m_ScreenSpacePackets.mutex );
		if ( m_ScreenSpacePackets.free.Count() )
		{
			packet = m_ScreenSpacePackets.free.Tail();
			m_ScreenSpacePackets.free.RemoveMultipleFromTail( 1 );
			packet->references.store( 1 );
		}
		else
			packet = new ScreenSpacePacketDX12;
	}
	packet->owner = this;
	packet->operation = nOperation;
	packet->view = {};
	packet->settings = {};
	return packet;
}

void CShaderAPIDX12::RecycleScreenSpacePacket( ScreenSpacePacketDX12 *pPacket )
{
	AUTO_LOCK( m_ScreenSpacePackets.mutex );
	m_ScreenSpacePackets.free.AddToTail( pPacket );
}

void CShaderAPIDX12::PurgeScreenSpacePackets()
{
	AUTO_LOCK( m_ScreenSpacePackets.mutex );
	for ( ScreenSpacePacketDX12 *packet : m_ScreenSpacePackets.free )
		delete packet;
	m_ScreenSpacePackets.free.RemoveAll();
}

//-----------------------------------------------------------------------------
// Purpose: Orders the marker behind the material command stream (CLightingDX12::Enqueue pattern)
//-----------------------------------------------------------------------------
void CShaderAPIDX12::EnqueueScreenSpacePacket( ScreenSpacePacketDX12 *pPacket )
{
	if ( g_pMaterialSystem )
	{
		CMatRenderContextPtr context( g_pMaterialSystem );
		// The marker must follow the material command stream even when this thread temporarily owns the device;
		// Flush drains buffered draws, it is not a GPU wait.
		context->Flush( false );
		ICallQueue *queue = context->GetCallQueue();
		if ( queue )
		{
			typedef CMemberFunctor0<ScreenSpacePacketDX12 *, void ( ScreenSpacePacketDX12::* )(), CFunctorBase, CFuncMemPolicyRefCount<ScreenSpacePacketDX12 *> > ReplayFunctor_t;
			CFunctor *functor = new ReplayFunctor_t( pPacket, &ScreenSpacePacketDX12::Replay );
			queue->QueueFunctor( functor );
			functor->Release();
		}
		else
			pPacket->Replay();
	}
	else
		pPacket->Replay();
	pPacket->Release();
}

void CShaderAPIDX12::ExecuteScreenSpacePacket( ScreenSpacePacketDX12 *pPacket )
{
	switch ( pPacket->operation )
	{
	case ScreenSpaceBeginView: ReplayScreenSpaceBeginView( pPacket->view ); break;
	case ScreenSpaceBeginContactDepth: ReplayBeginContactDepth(); break;
	case ScreenSpaceEndContactDepth: ReplayEndContactDepth(); break;
	case ScreenSpaceDispatchSSR: ReplayDispatchSSR(); break;
	case ScreenSpaceEndView: ReplayScreenSpaceEndView(); break;
	case ScreenSpaceBeginVolumetrics: ReplayBeginVolumetrics( pPacket->settings ); break;
	case ScreenSpaceDrawVolumetrics: ReplayDrawVolumetrics(); break;
	default: break;
	}
}

//-----------------------------------------------------------------------------
// IShaderAPIDX12ScreenSpace: every method copies its arguments into a packet and queues it
//-----------------------------------------------------------------------------
void CShaderAPIDX12::BeginView( const DX12ScreenSpaceViewDesc &view )
{
	ScreenSpacePacketDX12 *packet = ScreenSpacePacket( ScreenSpaceBeginView );
	packet->view = view;
	EnqueueScreenSpacePacket( packet );
}

void CShaderAPIDX12::BeginContactDepth()
{
	EnqueueScreenSpacePacket( ScreenSpacePacket( ScreenSpaceBeginContactDepth ) );
}

void CShaderAPIDX12::EndContactDepth()
{
	EnqueueScreenSpacePacket( ScreenSpacePacket( ScreenSpaceEndContactDepth ) );
}

void CShaderAPIDX12::DispatchSSR()
{
	EnqueueScreenSpacePacket( ScreenSpacePacket( ScreenSpaceDispatchSSR ) );
}

void CShaderAPIDX12::EndView()
{
	EnqueueScreenSpacePacket( ScreenSpacePacket( ScreenSpaceEndView ) );
}

void CShaderAPIDX12::BeginVolumetrics( const DX12VolumetricSettings &settings )
{
	ScreenSpacePacketDX12 *packet = ScreenSpacePacket( ScreenSpaceBeginVolumetrics );
	packet->settings = settings;
	EnqueueScreenSpacePacket( packet );
}

void CShaderAPIDX12::DrawVolumetrics()
{
	EnqueueScreenSpacePacket( ScreenSpacePacket( ScreenSpaceDrawVolumetrics ) );
}

//-----------------------------------------------------------------------------
// Purpose: Starts an opaque bracket. Eligible when the native scene colour/depth are bound and the viewport is
//			non-empty; the camera is PV = projection * view from the current matrices with the native raster
//			epilogue (half pixel plus upscaler jitter, DrawBuffers' viewportScale), inverted. Sample count and
//			depth clear are backend-authoritative; contacts stay off until a private replay succeeded.
//-----------------------------------------------------------------------------
void CShaderAPIDX12::ReplayScreenSpaceBeginView( const DX12ScreenSpaceViewDesc &view )
{
	// Buffered draws precede the marker and keep the previous bracket's bindings.
	FlushBufferedPrimitives();
	ScreenSpaceViewStateDX12 &state = m_ScreenSpace;
	state.constants = view.constants;
	state.ssr = view.ssr;
	state.active = state.contactReplay = state.contactsEnabled = state.ssrCapture = false;
	m_ContactDepth.replayDraws = false;
	++m_ContactDepth.constantsVersion;
	DX12ScreenSpaceViewConstants &constants = state.constants;
	constants.control[0] = 0;
	const float flViewportWidth = constants.viewport[2], flViewportHeight = constants.viewport[3];
	if ( m_pDevice && m_pDevice->CommandList() && m_pDevice->SceneColor() && m_pDevice->SceneDepth() &&
	     m_RenderTargets[0] == SHADER_RENDERTARGET_BACKBUFFER && m_hDepthTarget == SHADER_RENDERTARGET_DEPTHBUFFER &&
	     flViewportWidth > 0.f && flViewportHeight > 0.f )
	{
		VMatrix worldToClip, clipToWorld;
		MatrixMultiply( m_Matrices[MATERIAL_PROJECTION], m_Matrices[MATERIAL_VIEW], worldToClip );
		// The epilogue is applied over the draw viewport (m_Viewports[0] when set), exactly like DrawBuffers.
		float flWidth = flViewportWidth, flHeight = flViewportHeight;
		if ( m_nViewportCount > 0 && m_Viewports[0].m_nWidth > 0 && m_Viewports[0].m_nHeight > 0 )
		{
			flWidth = static_cast<float>( m_Viewports[0].m_nWidth );
			flHeight = static_cast<float>( m_Viewports[0].m_nHeight );
		}
		const bool bJitter = UpscalerJitterActive( false );
		const float flScaleX = ( 1.f + 2.f * ( bJitter ? m_UpscalerJitter[0] : 0.f ) ) / flWidth;
		const float flScaleY = ( 1.f + 2.f * ( bJitter ? m_UpscalerJitter[1] : 0.f ) ) / flHeight;
		for ( int column = 0; column < 4; ++column )
		{
			worldToClip[0][column] += flScaleX * worldToClip[3][column];
			worldToClip[1][column] -= flScaleY * worldToClip[3][column];
		}
		if ( MatrixInverseGeneral( worldToClip, clipToWorld ) )
		{
			memcpy( constants.worldToClip, worldToClip.Base(), sizeof( constants.worldToClip ) );
			memcpy( constants.clipToWorld, clipToWorld.Base(), sizeof( constants.clipToWorld ) );
			constants.control[1] = static_cast<uint32>( m_pDevice->SceneSampleCount() );
			constants.depth[0] = kNativeDepthClear;
			constants.depth[1] = kContactNormalBias;
			constants.depth[2] = kContactFirstDistance;
			constants.depth[3] = kContactEdgeFade;
			state.active = true;
			state.ssrCapture = constants.control[3] != 0;
		}
	}
	VolumetricBracketBegin();
}

//-----------------------------------------------------------------------------
// Purpose: (Re)creates the private depth at the scene depth's shape: typeless R24G8 with a D24S8 DSV, readable as
//			R24_UNORM_X8_TYPELESS. The replaced resource stays fence-retained.
//-----------------------------------------------------------------------------
bool CShaderAPIDX12::EnsureContactDepthResources()
{
	ID3D12Device *pNative = m_pDevice->NativeDevice();
	ID3D12Resource *pSceneDepth = m_pDevice->SceneDepth();
	if ( !pNative || !pSceneDepth )
		return false;
	const D3D12_RESOURCE_DESC scene = pSceneDepth->GetDesc();
	ContactDepthDX12 &contact = m_ContactDepth;
	if ( contact.resource && scene.Width == contact.width && scene.Height == contact.height && scene.SampleDesc.Count == contact.samples && scene.SampleDesc.Quality == contact.quality )
		return true;
	// Recorded OMSetRenderTargets calls name the DSV; replay them before the descriptor is rewritten.
	m_pDevice->DrainRecording();
	if ( contact.resource )
		m_pDevice->RetainResource( contact.resource.Get() );
	contact.resource.Reset();
	contact.tableFence = 0;
	D3D12_HEAP_PROPERTIES heap{};
	heap.Type = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_RESOURCE_DESC desc{};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = scene.Width;
	desc.Height = scene.Height;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
	desc.SampleDesc = scene.SampleDesc;
	desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
	D3D12_CLEAR_VALUE clear{};
	clear.Format = m_pDevice->SceneDepthFormat();
	clear.DepthStencil.Depth = kNativeDepthClear;
	Microsoft::WRL::ComPtr<ID3D12Resource> pResource;
	if ( FAILED( pNative->CreateCommittedResource( &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, IID_PPV_ARGS( &pResource ) ) ) )
	{
		if ( !contact.warned )
		{
			contact.warned = true;
			Warning( "ShaderAPIDX12: contact depth target unavailable at %llux%u %ux MSAA quality %u\n", static_cast<unsigned long long>( scene.Width ), scene.Height, scene.SampleDesc.Count, scene.SampleDesc.Quality );
		}
		return false;
	}
	pResource->SetName( L"DX12_ContactDepth" );
	if ( !contact.dsvHeap )
	{
		D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
		heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		heapDesc.NumDescriptors = 1;
		if ( FAILED( pNative->CreateDescriptorHeap( &heapDesc, IID_PPV_ARGS( &contact.dsvHeap ) ) ) )
			return false;
		contact.dsv = contact.dsvHeap->GetCPUDescriptorHandleForHeapStart();
	}
	D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
	dsv.Format = m_pDevice->SceneDepthFormat();
	dsv.ViewDimension = scene.SampleDesc.Count > 1 ? D3D12_DSV_DIMENSION_TEXTURE2DMS : D3D12_DSV_DIMENSION_TEXTURE2D;
	pNative->CreateDepthStencilView( pResource.Get(), &dsv, contact.dsv );
	contact.resource = std::move( pResource );
	contact.state = D3D12_RESOURCE_STATE_DEPTH_WRITE;
	contact.width = static_cast<UINT>( scene.Width );
	contact.height = scene.Height;
	contact.samples = scene.SampleDesc.Count;
	contact.quality = scene.SampleDesc.Quality;
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Records a barrier moving the private depth to the desired state
//-----------------------------------------------------------------------------
void CShaderAPIDX12::TransitionContactDepth( D3D12_RESOURCE_STATES desiredState )
{
	ContactDepthDX12 &contact = m_ContactDepth;
	if ( !contact.resource || contact.state == desiredState )
		return;
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = contact.resource.Get();
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = contact.state;
	barrier.Transition.StateAfter = desiredState;
	m_pDevice->CommandList()->ResourceBarrier( 1, &barrier );
	contact.state = desiredState;
}

//-----------------------------------------------------------------------------
// Purpose: Enters the private replay. The bracket is entered even when ineligible so that every replayed draw is
//			dropped rather than duplicated into the scene. Eligible: the current scene depth/stencil (earlier water
//			subviews included) is copied into the private target, which then receives the replayed geometry.
//-----------------------------------------------------------------------------
void CShaderAPIDX12::ReplayBeginContactDepth()
{
	// Buffered draws belong to the pass before the marker.
	FlushBufferedPrimitives();
	ScreenSpaceViewStateDX12 &state = m_ScreenSpace;
	ContactDepthDX12 &contact = m_ContactDepth;
	state.contactReplay = true;
	state.contactsEnabled = false;
	contact.replayDraws = false;
	if ( !state.active || !m_pDevice || !m_pDevice->CommandList() )
		return;
	m_pDevice->BeginGpuPIXEvent( "Contact depth" );
	contact.eventOpen = true;
	if ( !EnsureContactDepthResources() )
		return;
	CCommandRecorderDX12 *pList = m_pDevice->CommandList();
	const D3D12_RESOURCE_STATES sceneDepthState = m_pDevice->SceneDepthState();
	m_pDevice->TransitionSceneDepth( D3D12_RESOURCE_STATE_COPY_SOURCE );
	TransitionContactDepth( D3D12_RESOURCE_STATE_COPY_DEST );
	pList->CopyResource( contact.resource.Get(), m_pDevice->SceneDepth() );
	m_pDevice->TransitionSceneDepth( sceneDepthState );
	TransitionContactDepth( D3D12_RESOURCE_STATE_DEPTH_WRITE );
	contact.replayDraws = true;
}

//-----------------------------------------------------------------------------
// Purpose: Leaves the private replay: flushes the replayed geometry, makes the private depth shader-readable and
//			enables contacts (control.x = 1) for the forward draws of this bracket. A failed replay leaves them off.
//-----------------------------------------------------------------------------
void CShaderAPIDX12::ReplayEndContactDepth()
{
	// Still inside the replay: buffered replay draws must reach the private depth.
	FlushBufferedPrimitives();
	ScreenSpaceViewStateDX12 &state = m_ScreenSpace;
	ContactDepthDX12 &contact = m_ContactDepth;
	const bool bReplayed = state.contactReplay && contact.replayDraws && m_pDevice && m_pDevice->CommandList();
	state.contactReplay = false;
	contact.replayDraws = false;
	if ( contact.eventOpen )
	{
		m_pDevice->EndGpuPIXEvent();
		contact.eventOpen = false;
	}
	if ( !bReplayed )
		return;
	TransitionContactDepth( D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
	state.constants.control[0] = 1;
	++contact.constantsVersion;
	state.contactsEnabled = true;
}

//-----------------------------------------------------------------------------
// Purpose: Ends the bracket: hands the camera to the volume scope (when this bracket was eligible), then disables
//			contacts and SSR capture. The private depth stays allocated for the next bracket.
//-----------------------------------------------------------------------------
void CShaderAPIDX12::ReplayScreenSpaceEndView()
{
	FlushBufferedPrimitives();
	ScreenSpaceViewStateDX12 &state = m_ScreenSpace;
	ContactDepthDX12 &contact = m_ContactDepth;
	if ( state.contactReplay )
	{
		// Unbalanced BeginContactDepth: close the replay without enabling contacts.
		state.contactReplay = false;
		contact.replayDraws = false;
		if ( contact.eventOpen )
		{
			m_pDevice->EndGpuPIXEvent();
			contact.eventOpen = false;
		}
	}
	VolumetricBracketEnd( state.active ? &state.constants : nullptr );
	state.active = state.contactsEnabled = state.ssrCapture = false;
	state.constants.control[0] = 0;
	++contact.constantsVersion;
}

//-----------------------------------------------------------------------------
// Purpose: Space-6 inputs of an ordinary scene draw inside an active bracket that reads contacts or captures SSR:
//			b0 is the bracket's completed view block (control.x contacts, control.w SSR payload). While contacts are
//			enabled t0 (single-sample) or t1 (MSAA) is the private depth and the other slots are typed nulls; otherwise
//			the table stays neutral. Table and constants are built once per recording fence and reused by every draw.
//-----------------------------------------------------------------------------
bool CShaderAPIDX12::PrepareScreenSpaceBinding( CPipelineCacheDX12::BindingInputDX12 &input )
{
	ContactDepthDX12 &contact = m_ContactDepth;
	CPipelineCacheDX12 &pipeline = m_Pipeline;
	const uint64_t fence = m_pDevice->NextFenceValue();
	const bool bContacts = m_ScreenSpace.contactsEnabled;
	if ( bContacts && ( !contact.resource || contact.state != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE ) )
		return false;
	// Every draw reserves before reusing the cached table: the lighting, highres, projected-light, probe and ordinary
	// tables allocated after it reserve again, and a rollover there would leave this table in a stale heap generation.
	// The count covers this table plus the largest reservation that follows it (lighting's, which includes its own
	// space-6 share), so none of the later reservations can roll the heap.
	if ( bContacts && !pipeline.ReserveResourceDescriptors( 2 * DX12_SCREENSPACE_TABLE_COUNT + DX12_LIGHTING_RESOURCE_TABLE_COUNT + 1 + 8 + DX12_PBR_TABLE_COUNT + DX12_PROBE_TABLE_COUNT + 32 + 16, fence ) )
		return false;
	if ( bContacts && ( contact.tableFence != fence || contact.table.generation != pipeline.ResourceHeapGeneration() ) )
	{
		const DescriptorRangeDX12 table = pipeline.AllocateTransientResources( DX12_SCREENSPACE_TABLE_COUNT, fence );
		if ( table.count != DX12_SCREENSPACE_TABLE_COUNT )
			return false;
		ID3D12Device *pNative = m_pDevice->NativeDevice();
		const UINT stride = pNative->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
		const UINT depthSlot = contact.samples > 1 ? 1u : 0u;
		for ( UINT slot = 0; slot < DX12_SCREENSPACE_TABLE_COUNT; ++slot )
		{
			D3D12_SHADER_RESOURCE_VIEW_DESC srv = CPipelineCacheDX12::ScreenSpaceNullSrvDesc( slot );
			ID3D12Resource *pResource = nullptr;
			if ( slot == depthSlot )
			{
				srv.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
				pResource = contact.resource.Get();
			}
			D3D12_CPU_DESCRIPTOR_HANDLE cpu = table.cpu;
			cpu.ptr += SIZE_T( slot ) * stride;
			pNative->CreateShaderResourceView( pResource, &srv, cpu );
		}
		contact.table = table;
		contact.tableFence = fence;
	}
	if ( contact.constantsFence != fence || contact.uploadedVersion != contact.constantsVersion )
	{
		D3D12_GPU_VIRTUAL_ADDRESS address = 0;
		if ( !pipeline.UploadTransient( &m_ScreenSpace.constants, sizeof( m_ScreenSpace.constants ), 256, 256, fence, address ) )
			return false;
		contact.constants = address;
		contact.constantsFence = fence;
		contact.uploadedVersion = contact.constantsVersion;
	}
	input.screenSpaceTable = bContacts ? contact.table : DescriptorRangeDX12{};
	input.screenSpaceView = contact.constants;
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Releases the private depth and ends any bracket (device reset / texture teardown)
//-----------------------------------------------------------------------------
void CShaderAPIDX12::ReleaseScreenSpaceResources()
{
	if ( m_ContactDepth.eventOpen && m_pDevice )
		m_pDevice->EndGpuPIXEvent();
	m_ContactDepth = ContactDepthDX12{};
	m_ScreenSpace = ScreenSpaceViewStateDX12{};
}
} // namespace shaderapidx12
