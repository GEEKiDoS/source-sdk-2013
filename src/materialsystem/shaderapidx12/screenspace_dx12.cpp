//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Screen-space effect brackets of shaderapidx12.dll (plan sections 2-3): the IShaderAPIDX12ScreenSpace markers,
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
#include "../stdshaders_dx12/native_src/bend_sss_cpu.h"

namespace shaderapidx12
{
namespace
{
enum ScreenSpaceOpDX12 { ScreenSpaceBounce, ScreenSpaceViewModelBounce, ScreenSpaceBeginView, ScreenSpaceBeginContactDepth, ScreenSpaceEndContactDepth, ScreenSpaceDispatchSSR, ScreenSpaceEndView, ScreenSpaceBeginVolumetrics, ScreenSpaceDrawVolumetrics, ScreenSpaceBeginDepthPrepass, ScreenSpaceEndDepthPrepass, ScreenSpaceBeginSkybox, ScreenSpaceEndSkybox, ScreenSpaceBeginViewModelContacts, ScreenSpaceEndViewModelContacts };

// depth.yzw of DX12ScreenSpaceViewConstants (section 2); depth.x is the native clear value: ClearBuffers always clears native
// depth to 1 (textures_dx12.cpp ClearDepthStencilView), so the clip-space far plane is the only "no geometry" depth.
constexpr float kContactNormalBias = 0.25f, kContactFirstDistance = 0.5f, kContactEdgeFade = 8.f, kNativeDepthClear = 1.f;
constexpr float kViewModelContactDepthMax = 0.1f;

// With perspective clip W = projection[3][2] * view Z, device depth is a + b / clip W.
bool ContactDepthProjection( const VMatrix &projection, float range, float minimum, float maximum, float *out )
{
	if ( projection[3][3] != 0.f || projection[3][2] == 0.f || projection[2][0] != 0.f || projection[2][1] != 0.f ||
	     projection[2][3] == 0.f )
		return false;
	out[0] = range * projection[2][2] / projection[3][2];
	out[1] = range * projection[2][3];
	out[2] = minimum;
	out[3] = maximum;
	return true;
}
struct alignas( 16 ) BendShadowConstantsDX12
{
	float lightCoordinate[4];
	int waveOffset[2];
	int viewportOrigin[2];
	float visual[4];
	float depth[4];
	int dimensions[4];
};
static_assert( sizeof( BendShadowConstantsDX12 ) == 80, "Bend screen-shadow constants layout" );
} // namespace

//-----------------------------------------------------------------------------
// Purpose: Pooled marker packet; the queue functor AddRefs it for the queue's lifetime
//-----------------------------------------------------------------------------
struct CShaderAPIDX12::ScreenSpacePacketDX12 final : IRefCounted
{
	CShaderAPIDX12 *owner = nullptr;
	std::atomic<int> references{ 1 };
	int operation = 0;
	bool flag = false;
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
	packet->flag = false;
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
	case ScreenSpaceBeginContactDepth: ReplayBeginContactDepth( pPacket->flag ); break;
	case ScreenSpaceEndContactDepth: ReplayEndContactDepth(); break;
	case ScreenSpaceDispatchSSR: ReplayDispatchSSR(); break;
	case ScreenSpaceBounce:
		if ( m_ScreenSpace.active && m_flBounceScale > 0.f && m_nBounceFrame != m_nFrameCounter )
		{
			FlushBufferedPrimitives();
			m_nBounceFrame = m_nFrameCounter;
			MeasureBounce();
			ApplyBounce( false );
		}
		break;
	case ScreenSpaceViewModelBounce: ApplyBounce( true ); break;
	case ScreenSpaceEndView: ReplayScreenSpaceEndView(); break;
	case ScreenSpaceBeginVolumetrics: ReplayBeginVolumetrics( pPacket->settings ); break;
	case ScreenSpaceDrawVolumetrics: ReplayDrawVolumetrics(); break;
	case ScreenSpaceBeginDepthPrepass: ReplayDepthPrepass( true ); break;
	case ScreenSpaceEndDepthPrepass: ReplayDepthPrepass( false ); break;
	case ScreenSpaceBeginSkybox: ReplaySkybox( true ); break;
	case ScreenSpaceEndSkybox: ReplaySkybox( false ); break;
	case ScreenSpaceBeginViewModelContacts: ReplayBeginViewModelContacts( pPacket->flag ); break;
	case ScreenSpaceEndViewModelContacts: ReplayEndViewModelContacts(); break;
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

void CShaderAPIDX12::BeginContactDepth( bool hasViewModelDepth )
{
	ScreenSpacePacketDX12 *packet = ScreenSpacePacket( ScreenSpaceBeginContactDepth );
	packet->flag = hasViewModelDepth;
	EnqueueScreenSpacePacket( packet );
}

void CShaderAPIDX12::DrawBounce()
{
	EnqueueScreenSpacePacket( ScreenSpacePacket( ScreenSpaceBounce ) );
}

void CShaderAPIDX12::DrawViewModelBounce()
{
	EnqueueScreenSpacePacket( ScreenSpacePacket( ScreenSpaceViewModelBounce ) );
}

void CShaderAPIDX12::BeginViewModelContacts( bool reuseMainDepth )
{
	ScreenSpacePacketDX12 *packet = ScreenSpacePacket( ScreenSpaceBeginViewModelContacts );
	packet->flag = reuseMainDepth;
	EnqueueScreenSpacePacket( packet );
}

void CShaderAPIDX12::EndViewModelContacts()
{
	EnqueueScreenSpacePacket( ScreenSpacePacket( ScreenSpaceEndViewModelContacts ) );
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

void CShaderAPIDX12::BeginDepthPrepass()
{
	EnqueueScreenSpacePacket( ScreenSpacePacket( ScreenSpaceBeginDepthPrepass ) );
}

void CShaderAPIDX12::EndDepthPrepass()
{
	EnqueueScreenSpacePacket( ScreenSpacePacket( ScreenSpaceEndDepthPrepass ) );
}

void CShaderAPIDX12::ReplayDepthPrepass( bool bBegin )
{
	// The preceding buffered draws belong to the old pass, including at END.
	FlushBufferedPrimitives();
	m_bDepthPrepass = bBegin;
	m_bDepthPrepassResolved = !bBegin;
	if ( m_pDevice ) m_pDevice->RequestSceneRecordingBoundary();
	// Coverage twins use a different native material block binding; restore ordinary PS metadata on the
	// first draw of the next pass even when the last material/dynamic index happens to be unchanged.
	m_bNamedPixelShaderDirty = !m_ActiveSnapshot.pixelShaderName.empty();
}

void CShaderAPIDX12::BeginSkybox()
{
	EnqueueScreenSpacePacket( ScreenSpacePacket( ScreenSpaceBeginSkybox ) );
}

void CShaderAPIDX12::EndSkybox()
{
	EnqueueScreenSpacePacket( ScreenSpacePacket( ScreenSpaceEndSkybox ) );
}

void CShaderAPIDX12::ReplaySkybox( bool bBegin )
{
	// Buffered geometry belongs to the preceding pass, including the final sky draws at END.
	FlushBufferedPrimitives();
	const bool stencilEnabled = m_bStencilEnabled;
	const int stencilReference = m_nStencilRef;
	const uint8_t stencilReadMask = m_nStencilReadMask, stencilWriteMask = m_nStencilWriteMask;
	const StencilComparisonFunction_t stencilCompare = m_StencilCompare;
	const StencilOperation_t stencilFail = m_StencilFailOp, stencilDepthFail = m_StencilDepthFailOp, stencilPass = m_StencilPassOp;
	m_bStencilEnabled = true;
	m_nStencilRef = kSkyboxStencilBit;
	m_nStencilReadMask = m_nStencilWriteMask = kSkyboxStencilBit;
	m_StencilCompare = bBegin ? STENCILCOMPARISONFUNCTION_ALWAYS : STENCILCOMPARISONFUNCTION_EQUAL;
	m_StencilFailOp = STENCILOPERATION_KEEP;
	m_StencilDepthFailOp = bBegin ? STENCILOPERATION_ZERO : STENCILOPERATION_KEEP;
	m_StencilPassOp = bBegin ? STENCILOPERATION_REPLACE : STENCILOPERATION_ZERO;
	if ( bBegin )
	{
		// z=1 passes only clear depth; depth failure explicitly removes any stale sky bit on covered pixels.
		DrawMaskedClear( false, false, false, nullptr, true, true );
	}
	else
	{
		// Restore far depth only where sky was admitted, and remove just its reserved stencil bit.
		DrawMaskedClear( false, false, true, nullptr, true );
	}
	m_bStencilEnabled = stencilEnabled;
	m_nStencilRef = stencilReference;
	m_nStencilReadMask = stencilReadMask;
	m_nStencilWriteMask = stencilWriteMask;
	m_StencilCompare = stencilCompare;
	m_StencilFailOp = stencilFail;
	m_StencilDepthFailOp = stencilDepthFail;
	m_StencilPassOp = stencilPass;
	m_bSkyboxPass = bBegin;
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
	m_flBounceScale = view.bounceScale;
	m_bBounceDebug = view.bounceDebug != 0;
	state.active = state.contactReplay = state.contactsEnabled = state.ssrCapture = state.forwardSSR = false;
	m_ContactDepth.replayDraws = false;
	m_ContactDepth.prepared = false;
	state.viewFrame = state.contactFrame = ~0ull;
	state.viewModelContacts = state.contactHasViewModelDepth = false;
	state.forwardConstantsFence[1] = 0;
	++m_ContactDepth.constantsVersion;
	DX12ScreenSpaceViewConstants &constants = state.constants;
	constants.control[0] = 0;
	constants.contactControl[0] = 0;
	// Contact mask state is backend-owned: no mask until this bracket's EndContactDepth publishes one.
	constants.contactParams[3] = 0.f;
	memset( constants.contactLights, 0, sizeof( constants.contactLights ) );
	state.contactLocalMax = view.contactLocalLights;
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
			state.viewFrame = m_nFrameCounter;
			constants.contactControl[1] = constants.control[1];
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
	if ( FAILED( CreateCommittedResourceDX12( pNative, &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, IID_PPV_ARGS( &pResource ), "shadow", "DX12 contact shadow depth" ) ) )
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

bool CShaderAPIDX12::EnsureContactTexture( ShaderAPITextureHandle_t &handle, int nWidth, int nHeight, const char *pName )
{
	TextureRecord *record = handle ? FindTexture( handle ) : nullptr;
	if ( !record )
	{
		handle = CreateTexture( nWidth, nHeight, 1, IMAGE_FORMAT_R32F, 1, 1, TEXTURE_CREATE_RENDERTARGET, pName, "RenderTargets" );
		record = handle ? FindTexture( handle ) : nullptr;
	}
	return record && DiscardResizeComputeTarget( *record, nWidth, nHeight );
}

// One Bend wavefront pass into `target` toward lightPosition, homogeneous world space: w 0 the direction toward a
// directional light, w 1 a local light's position.
bool CShaderAPIDX12::DispatchContactShadow( ShaderAPITextureHandle_t target, const VMatrix &worldToClip, const float lightPosition[4], int width, int height )
{
	Vector4D lightProjection;
	Vector4DMultiply( worldToClip, Vector4D( lightPosition[0], lightPosition[1], lightPosition[2], lightPosition[3] ), lightProjection );
	// Use the same limiting homogeneous coordinate for XY and Z at the horizon (or a light in the camera plane).
	const float minimumW = 0.000002f * 64.f;
	if ( fabsf( lightProjection.w ) < minimumW )
		lightProjection.w = lightProjection.w < 0.f ? -minimumW : minimumW;
	const int viewport[2] = { width, height };
	const int minBounds[2] = { 0, 0 };
	const int maxBounds[2] = { width, height };
	const Bend::DispatchList list = Bend::BuildDispatchList( lightProjection.Base(), viewport, minBounds, maxBounds, false, 64 );
	if ( list.DispatchCount <= 0 )
		return false;
	BendShadowConstantsDX12 constants{};
	memcpy( constants.lightCoordinate, list.LightCoordinate_Shader, sizeof( constants.lightCoordinate ) );
	constants.viewportOrigin[0] = static_cast<int>( m_ScreenSpace.constants.viewport[0] );
	constants.viewportOrigin[1] = static_cast<int>( m_ScreenSpace.constants.viewport[1] );
	memcpy( constants.visual, m_ScreenSpace.constants.contactParams, 3 * sizeof( float ) );
	constants.visual[3] = static_cast<float>( m_ScreenSpace.constants.contactControl[2] );
	const float *depth = m_ScreenSpace.viewModelContacts ? m_ScreenSpace.viewModelContactDepth : m_ScreenSpace.constants.contactDepth;
	constants.depth[0] = depth[2];
	constants.depth[1] = depth[3];
	constants.depth[2] = 1.0f;
	constants.depth[3] = 0.0f;
	constants.dimensions[0] = width;
	constants.dimensions[1] = height;
	constants.dimensions[2] = static_cast<int>( m_ContactDepth.samples );

	ShaderAPIDX12ComputeDispatch_t dispatch{};
	dispatch.m_pShaderName = "bend_screen_space_shadow_cs51";
	dispatch.m_nDynamicIndex = m_ContactDepth.samples > 1 ? 1 : 0;
	dispatch.m_pConstants = &constants;
	dispatch.m_nConstantBytes = sizeof( constants );
	dispatch.m_Srv[0].m_nKind = SHADERAPIDX12_COMPUTE_RESOURCE_SCREEN_CONTACT_DEPTH;
	dispatch.m_Uav[0].m_nKind = SHADERAPIDX12_COMPUTE_RESOURCE_TEXTURE;
	dispatch.m_Uav[0].m_hTexture = target;
	dispatch.m_Uav[0].m_nMipCount = 1;
	for ( int i = 0; i < list.DispatchCount; ++i )
	{
		memcpy( constants.waveOffset, list.Dispatch[i].WaveOffset_Shader, sizeof( constants.waveOffset ) );
		dispatch.m_nGroupsX = list.Dispatch[i].WaveCount[0];
		dispatch.m_nGroupsY = list.Dispatch[i].WaveCount[1];
		dispatch.m_nGroupsZ = list.Dispatch[i].WaveCount[2];
		if ( !Dispatch( dispatch ) )
			return false;
	}
	return true;
}

// The current receiver scope's masks: the sun's when it is shadowmapped, then up to the bracket's contactLocalMax
// realtime-shadowed local lights (CLightingDX12::GetContactLights). Each is independent: a light that fails keeps no mask.
bool CShaderAPIDX12::RunContactShadowPasses()
{
	ContactShadowsDX12 &masks = m_Contacts;
	masks.sunReady = false;
	memset( masks.contactLights, 0, sizeof( masks.contactLights ) );
	if ( !m_pDevice || !m_pDevice->CommandList() || !m_ContactDepth.resource ||
		!( m_ScreenSpace.active || m_ScreenSpace.viewModelContacts ) )
		return false;
	const int width = static_cast<int>( m_ScreenSpace.constants.viewport[2] );
	const int height = static_cast<int>( m_ScreenSpace.constants.viewport[3] );
	if ( width <= 0 || height <= 0 )
		return false;
	VMatrix worldToClip;
	MatrixMultiply( m_Matrices[MATERIAL_PROJECTION], m_Matrices[MATERIAL_VIEW], worldToClip );
	const bool jitter = UpscalerJitterActive( false );
	const float jitterX = ( 1.f + 2.f * ( jitter ? m_UpscalerJitter[0] : 0.f ) ) / width;
	const float jitterY = ( 1.f + 2.f * ( jitter ? m_UpscalerJitter[1] : 0.f ) ) / height;
	for ( int column = 0; column < 4; ++column )
	{
		worldToClip[0][column] += jitterX * worldToClip[3][column];
		worldToClip[1][column] -= jitterY * worldToClip[3][column];
	}
	bool any = false;
	float sunTravel[4];
	if ( m_pDevice->Lighting().GetSunContactDirection( sunTravel ) && EnsureContactTexture( masks.sun, width, height, "DX12_SunContactShadow" ) )
	{
		// cSunTravel points from the sun; Bend marches toward the light.
		const float towardSun[4] = { -sunTravel[0], -sunTravel[1], -sunTravel[2], 0.f };
		any = masks.sunReady = DispatchContactShadow( masks.sun, worldToClip, towardSun, width, height );
	}
	float origins[DX12_SCREENSPACE_LOCAL_CONTACTS][4];
	uint32 lightIds[DX12_SCREENSPACE_LOCAL_CONTACTS];
	const int count = m_pDevice->Lighting().GetContactLights( m_ScreenSpace.constants.eyeLength,
		MIN( static_cast<int>( m_ScreenSpace.contactLocalMax ), static_cast<int>( DX12_SCREENSPACE_LOCAL_CONTACTS ) ), origins, lightIds );
	static const char *const kNames[DX12_SCREENSPACE_LOCAL_CONTACTS] = { "DX12_LocalContactShadow0", "DX12_LocalContactShadow1", "DX12_LocalContactShadow2", "DX12_LocalContactShadow3" };
	for ( int k = 0; k < count; ++k )
	{
		if ( EnsureContactTexture( masks.local[k], width, height, kNames[k] ) && DispatchContactShadow( masks.local[k], worldToClip, origins[k], width, height ) )
		{
			masks.contactLights[k] = lightIds[k] + 1u;
			any = true;
		}
	}
	return any;
}

// Which masks a receiver scope's constants may read: the sun's (contactParams.w) and the local slots' light ids.
void CShaderAPIDX12::PublishContactMasks( DX12ScreenSpaceViewConstants &constants ) const
{
	constants.contactParams[3] = m_Contacts.sunReady ? 1.f : 0.f;
	memcpy( constants.contactLights, m_Contacts.contactLights, sizeof( constants.contactLights ) );
}

void CShaderAPIDX12::ReleaseContactShadowResources()
{
	if ( m_Contacts.sun )
		DeleteTexture( m_Contacts.sun );
	for ( ShaderAPITextureHandle_t handle : m_Contacts.local )
		if ( handle )
			DeleteTexture( handle );
	m_Contacts = {};
}

//-----------------------------------------------------------------------------
// Purpose: Enters the private replay. The bracket is entered even when ineligible so that every replayed draw is
//			dropped rather than duplicated into the scene. Eligible: the current scene depth/stencil (earlier water
//			subviews included) is copied into the private target, which then receives the replayed geometry.
//-----------------------------------------------------------------------------
void CShaderAPIDX12::ReplayBeginContactDepth( bool hasViewModelDepth )
{
	// Buffered draws belong to the pass before the marker.
	FlushBufferedPrimitives();
	ScreenSpaceViewStateDX12 &state = m_ScreenSpace;
	ContactDepthDX12 &contact = m_ContactDepth;
	state.contactReplay = true;
	state.contactsEnabled = false;
	contact.replayDraws = false;
	contact.prepared = false;
	state.contactHasViewModelDepth = hasViewModelDepth && m_bDepthPrepassResolved;
	state.contactFrame = ~0ull;
	if ( !state.active || !m_pDevice || !m_pDevice->CommandList() ||
	     !ContactDepthProjection( m_Matrices[MATERIAL_PROJECTION], 1.f, state.contactHasViewModelDepth ? kViewModelContactDepthMax : 0.f,
	         kNativeDepthClear, state.constants.contactDepth ) )
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
	// The prepass already used the original material coverage and selected geometry. Do not shade it a third time.
	contact.replayDraws = !m_bDepthPrepassResolved;
	contact.prepared = true;
	if ( contact.replayDraws )
		TransitionContactDepth( D3D12_RESOURCE_STATE_DEPTH_WRITE );
}

void CShaderAPIDX12::ReplayBeginViewModelContacts( bool reuseMainDepth )
{
	FlushBufferedPrimitives();
	ScreenSpaceViewStateDX12 &state = m_ScreenSpace;
	ContactDepthDX12 &contact = m_ContactDepth;
	state.contactReplay = true;
	state.viewModelContacts = state.contactsEnabled = false;
	contact.replayDraws = contact.prepared = false;
	if ( state.active || !m_pDevice || !m_pDevice->CommandList() || state.viewFrame != m_nFrameCounter ||
	     m_RenderTargets[0] != SHADER_RENDERTARGET_BACKBUFFER || m_hDepthTarget != SHADER_RENDERTARGET_DEPTHBUFFER ||
	     m_nViewportCount == 0 || m_Viewports[0].m_flMinZ != 0.f || m_Viewports[0].m_flMaxZ != kViewModelContactDepthMax ||
	     m_Viewports[0].m_nWidth <= 0 || m_Viewports[0].m_nHeight <= 0 )
		return;
	const VMatrix &projection = m_Matrices[MATERIAL_PROJECTION];
	if ( !ContactDepthProjection( projection, kViewModelContactDepthMax, 0.f, kViewModelContactDepthMax, state.viewModelContactDepth ) )
		return;
	if ( reuseMainDepth )
	{
		if ( state.contactFrame != m_nFrameCounter || !state.contactHasViewModelDepth || !contact.resource )
			return;
	}
	else
	{
		if ( !EnsureContactDepthResources() )
			return;
		m_pDevice->BeginGpuPIXEvent( "Viewmodel contact depth" );
		contact.eventOpen = true;
		TransitionContactDepth( D3D12_RESOURCE_STATE_DEPTH_WRITE );
		m_pDevice->CommandList()->ClearDepthStencilView( contact.dsv,
		    D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, kNativeDepthClear, 0, 0, nullptr );
		contact.replayDraws = true;
	}
	state.viewModelContacts = contact.prepared = true;
}

void CShaderAPIDX12::ReplayEndViewModelContacts()
{
	FlushBufferedPrimitives();
	m_ScreenSpace.viewModelContacts = m_ScreenSpace.contactsEnabled = false;
	m_ScreenSpace.forwardConstantsFence[1] = 0;
}

//-----------------------------------------------------------------------------
// Purpose: Publish private depth for the world or viewmodel receivers, even after a copy-only pass.
//-----------------------------------------------------------------------------
void CShaderAPIDX12::ReplayEndContactDepth()
{
	// Still inside the replay: buffered replay draws must reach the private depth.
	FlushBufferedPrimitives();
	ScreenSpaceViewStateDX12 &state = m_ScreenSpace;
	ContactDepthDX12 &contact = m_ContactDepth;
	const bool bPrepared = state.contactReplay && contact.prepared && m_pDevice && m_pDevice->CommandList();
	state.contactReplay = false;
	contact.replayDraws = contact.prepared = false;
	if ( contact.eventOpen )
	{
		m_pDevice->EndGpuPIXEvent();
		contact.eventOpen = false;
	}
	if ( !bPrepared )
		return;
	TransitionContactDepth( D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE );
	const bool ready = RunContactShadowPasses();
	contact.tableFence = 0;
	state.forwardTableFence = 0;
	state.contactsEnabled = ready;
	state.contactFrame = m_nFrameCounter;
	if ( state.viewModelContacts )
		state.forwardConstantsFence[1] = 0;
	else
	{
		state.constants.contactControl[0] = ready ? 1u : 0u;
		PublishContactMasks( state.constants );
		++contact.constantsVersion;
	}
}

// A space-6 contact slot's view: the sun mask at t0, local masks at t8..t11, each once its texture exists (its
// constants say whether this scope wrote it). Other slots and absent masks keep the typed null.
bool CShaderAPIDX12::PrepareContactMaskSrv( UINT slot, ID3D12Resource *&pResource, D3D12_SHADER_RESOURCE_VIEW_DESC &srv )
{
	ShaderAPITextureHandle_t handle = 0;
	if ( slot == 0 )
		handle = m_Contacts.sun;
	else if ( slot >= DX12_SCREENSPACE_T_LOCAL_CONTACT && slot < DX12_SCREENSPACE_T_LOCAL_CONTACT + DX12_SCREENSPACE_LOCAL_CONTACTS )
		handle = m_Contacts.local[slot - DX12_SCREENSPACE_T_LOCAL_CONTACT];
	D3D12_SAMPLER_DESC unusedSampler{};
	return !handle || PrepareSampledTexture( handle, false, &pResource, srv, unusedSampler, nullptr, false, 0, 1 );
}

//-----------------------------------------------------------------------------
// Purpose: Ends the bracket: hands the camera to the volume scope (when this bracket was eligible), then disables
//			contacts and SSR capture. The private depth stays allocated for the next bracket.
//-----------------------------------------------------------------------------
void CShaderAPIDX12::ReplayScreenSpaceEndView()
{
	FlushBufferedPrimitives();
	m_bDepthPrepassResolved = false;
	ScreenSpaceViewStateDX12 &state = m_ScreenSpace;
	ContactDepthDX12 &contact = m_ContactDepth;
	if ( state.contactReplay )
	{
		// Unbalanced BeginContactDepth: close the replay without enabling contacts.
		state.contactReplay = false;
		contact.replayDraws = false;
		contact.prepared = false;
		if ( contact.eventOpen )
		{
			m_pDevice->EndGpuPIXEvent();
			contact.eventOpen = false;
		}
	}
	VolumetricBracketEnd( state.active ? &state.constants : nullptr );
	state.active = state.contactsEnabled = state.ssrCapture = false;
	state.constants.contactControl[0] = 0;
	++contact.constantsVersion;
}

//-----------------------------------------------------------------------------
// Purpose: Space-6 inputs of an ordinary scene draw inside an active bracket that reads contacts or captures SSR:
//			b0 is the bracket's completed view block (control.x contacts, control.w SSR payload). While contacts are
//			enabled t0 is the viewport-local sun visibility mask; other slots remain typed nulls.
//			Table and constants are built once per recording fence and reused by every draw.
//-----------------------------------------------------------------------------
bool CShaderAPIDX12::PrepareScreenSpaceBinding( CPipelineCacheDX12::BindingInputDX12 &input )
{
	ContactDepthDX12 &contact = m_ContactDepth;
	CPipelineCacheDX12 &pipeline = m_Pipeline;
	const uint64_t fence = m_pDevice->NextFenceValue();
	const bool bContacts = m_ScreenSpace.contactsEnabled;
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
		for ( UINT slot = 0; slot < DX12_SCREENSPACE_TABLE_COUNT; ++slot )
		{
			D3D12_SHADER_RESOURCE_VIEW_DESC srv = CPipelineCacheDX12::ScreenSpaceNullSrvDesc( slot );
			ID3D12Resource *pResource = nullptr;
			if ( !PrepareContactMaskSrv( slot, pResource, srv ) )
				return false;
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
		if ( !pipeline.UploadTransient( &m_ScreenSpace.constants, sizeof( m_ScreenSpace.constants ), 512, 256, fence, address ) )
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
// Purpose: Space-6 inputs of a translucent or viewmodel scene draw after the opaque bracket
//			(native_src/pbr_forward_screenspace.hlsli). Once the bracket's SSR succeeded this frame: b0 its camera with the
//			trace in control.z / eyeLength.w / forwardThickness.w, t5 the SSR scene copy's mip 1 (mip 0 now holds the
//			composite's delta) and t6 its positive view depth (opaque only: translucents, the sky and the viewmodel read
//			as far). Viewmodel draws also get t7, this frame's GTAO term, when it was computed with their depth. The table
//			and both constant blocks are built once per recording fence; the textures stay readable until the next
//			frame's compute passes write them.
//-----------------------------------------------------------------------------
void CShaderAPIDX12::PrepareForwardScreenSpaceBinding( CPipelineCacheDX12::BindingInputDX12 &input, bool viewModel )
{
	ScreenSpaceViewStateDX12 &state = m_ScreenSpace;
	CPipelineCacheDX12 &pipeline = m_Pipeline;
	const uint64_t fence = m_pDevice->NextFenceValue();
	const bool ssr = state.forwardSSR && state.forwardSSRFrame == m_nFrameCounter && m_SSR.scene.handle && m_SSR.scene.mipLevels >= 2 &&
		m_SSR.depth[m_SSR.current ^ 1].handle;
	const bool aoAvailable = m_SSR.aoTerm && m_SSR.aoFrame == m_nFrameCounter && m_SSR.aoViewModel && m_SSR.aoWidth > 0 && m_SSR.aoHeight > 0;
	const bool contacts = viewModel && state.viewModelContacts && state.contactsEnabled;
	const bool debugContacts = viewModel && state.viewFrame == m_nFrameCounter && state.constants.contactControl[3] != 0;
	if ( !( ssr || ( viewModel && aoAvailable ) || contacts || debugContacts ) )
		return;
	auto fail = [&]()
	{
		state.forwardSSR = false;
		m_SSR.aoViewModel = false;
		input.screenSpaceTable = {};
		input.screenSpaceView = 0;
	};
	// Same reservation as the bracket's own table: the tables allocated after this one must not roll the heap.
	if ( !pipeline.ReserveResourceDescriptors( 2 * DX12_SCREENSPACE_TABLE_COUNT + DX12_LIGHTING_RESOURCE_TABLE_COUNT + 1 + 8 + DX12_PBR_TABLE_COUNT + DX12_PROBE_TABLE_COUNT + 32 + 16, fence ) )
		return fail();
	if ( state.forwardTableFence != fence || state.forwardTable.generation != pipeline.ResourceHeapGeneration() ||
	     state.forwardTableSSR != ssr || state.forwardTableAO != aoAvailable || state.forwardTableContacts != contacts )
	{
		ID3D12Resource *pScene = nullptr, *pDepth = nullptr, *pAO = nullptr;
		D3D12_SHADER_RESOURCE_VIEW_DESC sceneSrv{}, depthSrv{}, aoSrv{};
		D3D12_SAMPLER_DESC unusedSampler{};
		// Leave mip 0 (the signed delta) out of the forward radiance view.
		if ( ssr && ( !PrepareSampledTexture( m_SSR.scene.handle, false, &pScene, sceneSrv, unusedSampler, nullptr, false, 1, 0 ) || !pScene ||
		     !PrepareSampledTexture( m_SSR.depth[m_SSR.current ^ 1].handle, false, &pDepth, depthSrv, unusedSampler, nullptr, false, -1, 0 ) || !pDepth ) )
			return fail();
		if ( aoAvailable && ( !PrepareSampledTexture( m_SSR.aoTerm, false, &pAO, aoSrv, unusedSampler, nullptr, false, -1, 0 ) || !pAO ) )
			return fail();
		const DescriptorRangeDX12 table = pipeline.AllocateTransientResources( DX12_SCREENSPACE_TABLE_COUNT, fence );
		if ( table.count != DX12_SCREENSPACE_TABLE_COUNT )
			return fail();
		ID3D12Device *pNative = m_pDevice->NativeDevice();
		const UINT stride = pNative->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
		for ( UINT slot = 0; slot < DX12_SCREENSPACE_TABLE_COUNT; ++slot )
		{
			D3D12_SHADER_RESOURCE_VIEW_DESC srv = CPipelineCacheDX12::ScreenSpaceNullSrvDesc( slot );
			ID3D12Resource *pResource = nullptr;
			if ( contacts && ( slot == 0 || slot >= DX12_SCREENSPACE_T_LOCAL_CONTACT ) )
			{
				if ( !PrepareContactMaskSrv( slot, pResource, srv ) )
					return fail();
			}
			else if ( slot == DX12_SCREENSPACE_T_SSR_SCENE && pScene )
			{
				srv.Format = sceneSrv.Format;
				srv.Texture2D.MostDetailedMip = 1;
				srv.Texture2D.MipLevels = m_SSR.scene.mipLevels - 1;
				pResource = pScene;
			}
			else if ( slot == DX12_SCREENSPACE_T_SSR_DEPTH && pDepth )
			{
				srv.Format = depthSrv.Format;
				pResource = pDepth;
			}
			else if ( slot == DX12_SCREENSPACE_T_AO && pAO )
			{
				srv.Format = aoSrv.Format;
				pResource = pAO;
			}
			D3D12_CPU_DESCRIPTOR_HANDLE cpu = table.cpu;
			cpu.ptr += SIZE_T( slot ) * stride;
			pNative->CreateShaderResourceView( pResource, &srv, cpu );
		}
		state.forwardTable = table;
		state.forwardTableFence = fence;
		state.forwardTableSSR = ssr;
		state.forwardTableAO = aoAvailable;
		state.forwardTableContacts = contacts;
		state.forwardConstantsFence[0] = state.forwardConstantsFence[1] = 0;
	}
	const int kind = viewModel ? 1 : 0;
	if ( state.forwardConstantsFence[kind] != fence )
	{
		DX12ScreenSpaceViewConstants constants{};
		if ( ssr )
			constants = state.forwardConstants;
		else if ( m_nViewportCount > 0 )
		{
			// AO alone needs only the viewport the term covers: the viewmodel view shares the scene's.
			constants.viewport[0] = static_cast<float>( m_Viewports[0].m_nTopLeftX );
			constants.viewport[1] = static_cast<float>( m_Viewports[0].m_nTopLeftY );
			constants.viewport[2] = static_cast<float>( MAX( 1, m_Viewports[0].m_nWidth ) );
			constants.viewport[3] = static_cast<float>( MAX( 1, m_Viewports[0].m_nHeight ) );
		}
		if ( viewModel && aoAvailable )
		{
			constants.control[1] = static_cast<uint32>( m_SSR.aoWidth ) | ( static_cast<uint32>( m_SSR.aoHeight ) << 16 );
			constants.control[2] |= DX12_SCREENSPACE_FORWARD_AO | ( m_SSR.aoPacked ? DX12_SCREENSPACE_FORWARD_AO_PACKED : 0u );
		}
		// The SSR camera snapshot comes from the opaque bracket. Publish a contact scope and its masks only when *this*
		// draw's table binds them; a null SRV would read as fully shadowed.
		constants.contactControl[0] = contacts ? 2u : 0u;
		if ( contacts )
			PublishContactMasks( constants );
		else
		{
			constants.contactParams[3] = 0.f;
			memset( constants.contactLights, 0, sizeof( constants.contactLights ) );
		}
		constants.contactControl[3] = debugContacts ? 1u : 0u;
		if ( !pipeline.UploadTransient( &constants, sizeof( constants ), 512, 256, fence, state.forwardConstantsAddress[kind] ) )
			return fail();
		state.forwardConstantsFence[kind] = fence;
	}
	input.screenSpaceTable = state.forwardTable;
	input.screenSpaceView = state.forwardConstantsAddress[kind];
}

//-----------------------------------------------------------------------------
// Purpose: Releases the private depth and ends any bracket (device reset / texture teardown)
//-----------------------------------------------------------------------------
void CShaderAPIDX12::ReleaseScreenSpaceResources()
{
	if ( m_ContactDepth.eventOpen && m_pDevice )
		m_pDevice->EndGpuPIXEvent();
	ReleaseContactShadowResources();
	m_ContactDepth = ContactDepthDX12{};
	m_ScreenSpace = ScreenSpaceViewStateDX12{};
	m_bDepthPrepass = false;
	m_bDepthPrepassResolved = false;
	m_bSkyboxPass = false;
}
} // namespace shaderapidx12
