//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Raymarched volumetric lighting of one main camera view (plan §10 + half-resolution amendment). Runs on the
//          recording owner from the ordered BeginVolumetrics / DrawVolumetrics markers:
//            BeginVolumetrics  stores the settings snapshot, opens the scope and starts the lighting handoff.
//            Each opaque bracket's BeginView discards the previous candidate; its EndView captures the completed
//            native camera (contact/SSR control zeroed) and the current lighting view state (CLightingDX12 keeps it
//            past its own EndView). The latest eligible main bracket wins; an ineligible one invalidates drawing.
//            DrawVolumetrics   two fullscreen pixel draws:
//                              1. volumetric_ps51 on the lighting root marches every half-resolution texel
//                                 (ceil( viewport / 2 )) over the completed scene depth into the backend-owned
//                                 RGBA16F in-scattering + R32F view-depth pair (MRT, single-sample, no blending).
//                              2. volumetric_upsample_ps51 on the ordinary root gathers that pair depth-aware per
//                                 scene sample and adds RGB (ONE/ONE, RGB only) onto the FP16 scene.
//                              Then the capture is released and the scope ends, whether or not the pass drew.
//          Failure to resolve shaders, targets, bindings or a PSO skips the addition with a warning; the scene is only
//          written by the second draw, so nothing is left partially applied. Nothing is retried.
//
//=============================================================================//

#include "shaderapi_dx12.h"
#include "shaderdevice_dx12.h"
#include "tier0/dbg.h"
#include <cstring>

namespace shaderapidx12
{
namespace
{
constexpr unsigned kVolumetricWarnings = 8;
}

void CShaderAPIDX12::ReplayBeginVolumetrics( const DX12VolumetricSettings &settings )
{
	if ( m_Volumetric.scope && m_pDevice )
		m_pDevice->Lighting().EndVolumetrics(); // an unterminated scope never draws; release its capture
	m_Volumetric.settings = settings;
	m_Volumetric.scope = true;
	m_Volumetric.cameraValid = false;
	if ( m_pDevice )
		m_pDevice->Lighting().BeginVolumetrics();
}

void CShaderAPIDX12::VolumetricBracketBegin()
{
	if ( !m_Volumetric.scope )
		return;
	m_Volumetric.cameraValid = false;
}

void CShaderAPIDX12::VolumetricBracketEnd( const DX12ScreenSpaceViewConstants *pCamera )
{
	if ( !m_Volumetric.scope )
		return;
	if ( !pCamera )
	{
		m_Volumetric.cameraValid = false;
		return;
	}
	m_Volumetric.camera = *pCamera;
	// No contact reads / SSR capture in the volume passes; control.y stays the scene sample count the march gathers.
	m_Volumetric.camera.control[0] = m_Volumetric.camera.control[2] = m_Volumetric.camera.control[3] = 0;
	m_Volumetric.cameraValid = true;
	if ( m_pDevice )
		m_pDevice->Lighting().CaptureVolumetricView();
}

void CShaderAPIDX12::ReplayDrawVolumetrics()
{
	if ( !m_Volumetric.scope )
		return;
	FlushBufferedPrimitives();
	if ( m_Volumetric.cameraValid )
		DrawVolumetricPass();
	// The snapshot is consumed by this one draw, drawn or skipped; a later view captures afresh.
	if ( m_pDevice )
		m_pDevice->Lighting().EndVolumetrics();
	m_Volumetric.scope = false;
	m_Volumetric.cameraValid = false;
}

void CShaderAPIDX12::ReleaseVolumetricResources()
{
	if ( m_Volumetric.scope && m_pDevice )
		m_pDevice->Lighting().EndVolumetrics();
	m_Volumetric.scope = false;
	m_Volumetric.cameraValid = false;
	VolumetricTargetDX12 *const targets[] = { &m_Volumetric.scatter, &m_Volumetric.depth };
	for ( VolumetricTargetDX12 *pTarget : targets )
	{
		if ( pTarget->handle )
			DeleteTexture( pTarget->handle ); // fence-retains the resource and its views
		*pTarget = {};
	}
}

//-----------------------------------------------------------------------------
// Purpose: (Re)creates one half-resolution march target at the requested shape as an ordinary single-mip render-target
//			record; a replaced record is fence-retained by DeleteTexture.
//-----------------------------------------------------------------------------
bool CShaderAPIDX12::EnsureVolumetricTarget( VolumetricTargetDX12 &target, int nWidth, int nHeight, ImageFormat format, const char *pszName )
{
	TextureRecord *pExisting = target.handle ? FindTexture( target.handle ) : nullptr;
	if ( pExisting && target.width == nWidth && target.height == nHeight && EnsureTextureResident( *pExisting ) )
		return true;
	if ( target.handle )
		DeleteTexture( target.handle );
	target = {};
	const ShaderAPITextureHandle_t hTexture = CreateTexture( nWidth, nHeight, 1, format, 1, 1, TEXTURE_CREATE_RENDERTARGET, pszName, "RenderTargets" );
	TextureRecord *pRecord = hTexture ? FindTexture( hTexture ) : nullptr;
	if ( !pRecord || !EnsureTextureResident( *pRecord ) )
	{
		if ( hTexture )
			DeleteTexture( hTexture );
		return false;
	}
	target.handle = hTexture;
	target.width = nWidth;
	target.height = nHeight;
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: The two final draws. March: lighting root, half-resolution viewport, two single-sample RTVs, no DSV, no
//          blending. Upsample: ordinary root, the native FP16 scene RTV with its actual sample layout, no DSV,
//          RGB-only ONE/ONE, the march pair at space-0 t0/t1. Both read the scene depth through the matching
//          single/MSAA SRV in space 6 (t2/t3) while it is a pixel-shader resource and share the captured camera (b0)
//          and medium (b1) blocks. Tracked states are restored and the draw cache invalidated afterwards, as
//          BlitTexture does.
//-----------------------------------------------------------------------------
bool CShaderAPIDX12::DrawVolumetricPass()
{
	static unsigned s_nWarnings = 0;
	const auto warn = [&]( const char *pszReason )
	{
		if ( s_nWarnings++ < kVolumetricWarnings )
			Warning( "ShaderAPIDX12: volumetric lighting skipped: %s\n", pszReason );
		return false;
	};
	if ( !m_pDevice || !m_pDevice->CommandList() || !m_pDevice->NativeDevice() || !m_pDevice->SceneColor() || !m_pDevice->SceneDepth() )
		return false;
	RenderTargetBindingDX12 target{};
	if ( !PrepareRenderTargets( target, false ) || target.colorCount < 1 || target.color != m_pDevice->SceneColor() || target.depth != m_pDevice->SceneDepth() )
		return warn( "the native scene colour/depth pair is not bound" );
	const DX12ScreenSpaceViewConstants &camera = m_Volumetric.camera;
	const LONG x = static_cast<LONG>( camera.viewport[0] ), y = static_cast<LONG>( camera.viewport[1] );
	const LONG width = static_cast<LONG>( camera.viewport[2] ), height = static_cast<LONG>( camera.viewport[3] );
	if ( x < 0 || y < 0 || width <= 0 || height <= 0 || x + width > target.width || y + height > target.height )
		return warn( "the captured viewport does not fit the scene" );
	const bool bMSAA = target.sampleCount > 1;
	ShaderRecordDX12 *vs = ResolveNamedShader( "volumetric_vs51", false, 0, 0 );
	ShaderRecordDX12 *ps = ResolveNamedShader( "volumetric_ps51", true, bMSAA ? 1 : 0, 0 );
	ShaderRecordDX12 *upsample = ResolveNamedShader( "volumetric_upsample_ps51", true, bMSAA ? 1 : 0, 0 );
	if ( !vs || !ps || !upsample || !vs->legacyBytecode.IsEmpty() || !ps->legacyBytecode.IsEmpty() || !upsample->legacyBytecode.IsEmpty() || !ps->lightingAbi )
		return warn( "native volumetric_vs51 / volumetric_ps51 / volumetric_upsample_ps51 unavailable" );
	const LONG halfWidth = ( width + 1 ) / 2, halfHeight = ( height + 1 ) / 2;
	if ( !EnsureVolumetricTarget( m_Volumetric.scatter, halfWidth, halfHeight, IMAGE_FORMAT_RGBA16161616F, "DX12_VolumetricScatter" ) ||
	     !EnsureVolumetricTarget( m_Volumetric.depth, halfWidth, halfHeight, IMAGE_FORMAT_R32F, "DX12_VolumetricDepth" ) )
		return warn( "half-resolution march targets unavailable" );
	TextureRecord *pScatter = FindTexture( m_Volumetric.scatter.handle ), *pDepth = FindTexture( m_Volumetric.depth.handle );
	const DXGI_FORMAT scatterFormat = ImageFormatToDXGI12( pScatter->format ), depthFormat = ImageFormatToDXGI12( pDepth->format );

	ID3D12Device *pNative = m_pDevice->NativeDevice();
	CCommandRecorderDX12 *pList = m_pDevice->CommandList();
	const uint64_t fence = m_pDevice->NextFenceValue();
	const UINT stride = pNative->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
	// Space 6 of both draws: typed-null contact planes, the scene depth in the slot matching its sample layout, a typed
	// null in the other.
	const auto sceneDepthTable = [&]( DescriptorRangeDX12 &table )
	{
		table = m_Pipeline.AllocateTransientResources( DX12_SCREENSPACE_TABLE_COUNT, fence );
		if ( table.count != DX12_SCREENSPACE_TABLE_COUNT )
			return false;
		for ( UINT slot = 0; slot < DX12_SCREENSPACE_TABLE_COUNT; ++slot )
		{
			const bool bScene = slot >= 2 && ( ( slot & 1 ) != 0 ) == bMSAA;
			D3D12_SHADER_RESOURCE_VIEW_DESC srv = CPipelineCacheDX12::ScreenSpaceNullSrvDesc( slot );
			if ( bScene )
				srv.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
			D3D12_CPU_DESCRIPTOR_HANDLE handle = table.cpu;
			handle.ptr += static_cast<SIZE_T>( slot ) * stride;
			pNative->CreateShaderResourceView( bScene ? m_pDevice->SceneDepth() : nullptr, &srv, handle );
		}
		return true;
	};
	const auto prepareInput = [&]( CPipelineCacheDX12::BindingInputDX12 &input )
	{
		input.retireFence = fence;
		input.vertexTextures = false;
		input.geometryStage = false;
		for ( int i = 0; i < ARRAYSIZE( input.samplerDescs ); ++i )
			input.samplerDescs[i] = CPipelineCacheDX12::DefaultSamplerDesc();
		input.samplerTable = m_Pipeline.PrepareSamplerTable( input.samplerDescs, input.samplerIds, fence );
		return input.samplerTable.count == 32;
	};
	const auto transitionRecord = [&]( TextureRecord &record, D3D12_RESOURCE_STATES desired )
	{
		D3D12_RESOURCE_STATES &state = record.subresourceStates[0];
		if ( state == desired )
			return;
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = record.resource.Get();
		barrier.Transition.Subresource = 0;
		barrier.Transition.StateBefore = state;
		barrier.Transition.StateAfter = desired;
		pList->ResourceBarrier( 1, &barrier );
		state = desired;
	};

	// Constants shared by both draws.
	DX12VolumetricConstants volume{};
	volume.settings = m_Volumetric.settings;
	volume.outputScale[0] = m_ToneScale.x;
	D3D12_GPU_VIRTUAL_ADDRESS cameraAddress = 0, volumeAddress = 0;
	if ( !m_Pipeline.UploadTransient( &camera, sizeof( camera ), 256, 256, fence, cameraAddress ) ||
	     !m_Pipeline.UploadTransient( &volume, sizeof( volume ), 256, 256, fence, volumeAddress ) )
		return warn( "constant upload failed" );

	// March bindings: space 2 (captured lighting view, no receiver/prop visibility) and space 5 (current projected-light
	// table, legal even when empty). Lighting reserves every table allocated below within one heap generation.
	CPipelineCacheDX12::BindingInputDX12 march( m_Pipeline.NullShaderResourceView() );
	if ( !prepareInput( march ) )
		return warn( "sampler descriptors unavailable" );
	if ( !m_pDevice->Lighting().PrepareVolumetricDraw( march ) )
		return warn( "no valid lighting capture for this view" );
	if ( !m_pDevice->Lighting().PreparePbrSpots( march ) )
		return warn( "projected-light descriptors unavailable" );
	if ( !sceneDepthTable( march.screenSpaceTable ) )
		return warn( "screen-space descriptors unavailable" );
	march.screenSpaceView = cameraAddress;
	march.volumetricConstants = volumeAddress;

	PipelineKeyDX12 key{};
	key.lightingAbi = true;
	key.vs = vs->identity;
	key.ps = ps->identity;
	key.vsVariant = vs->activeVariantKey;
	key.psVariant = ps->activeVariantKey;
	key.topology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	key.color = scatterFormat;
	key.colorFormats[0] = scatterFormat;
	key.colorFormats[1] = depthFormat;
	key.colorCount = 2;
	key.depth = DXGI_FORMAT_UNKNOWN;
	key.samples = 1;
	key.sampleQuality = 0;
	key.blend = 0;
	key.depthState = 0;
	key.depthTest = key.depthWrite = false;
	key.depthFunction = SHADER_DEPTHFUNC_ALWAYS;
	key.culling = false;
	key.colorWrites = key.alphaWrites = true;
	key.stencil = false;
	const D3D12_INPUT_LAYOUT_DESC layout{};
	ID3D12PipelineState *marchPso = m_Pipeline.GetOrCreate( key, vs->Bytecode(), ps->Bytecode(), D3D12_SHADER_BYTECODE{}, layout, fence );
	if ( !marchPso )
		return warn( "march pipeline state unavailable" );

	// Upsample PSO: created before anything is recorded so a failure leaves the scene untouched.
	key.lightingAbi = false;
	key.ps = upsample->identity;
	key.psVariant = upsample->activeVariantKey;
	key.color = target.colorFormat;
	key.colorFormats[0] = target.colorFormat;
	key.colorFormats[1] = DXGI_FORMAT_UNKNOWN;
	key.colorCount = 1;
	key.samples = target.sampleCount;
	key.sampleQuality = target.sampleQuality;
	key.blend = 1;
	key.blendSource = SHADER_BLEND_ONE;
	key.blendDestination = SHADER_BLEND_ONE;
	key.blendOperation = SHADER_BLEND_OP_ADD;
	key.alphaWrites = false;
	ID3D12PipelineState *upsamplePso = m_Pipeline.GetOrCreate( key, vs->Bytecode(), upsample->Bytecode(), D3D12_SHADER_BYTECODE{}, layout, fence );
	if ( !upsamplePso )
		return warn( "upsample pipeline state unavailable" );

	// The scene depth was left in DEPTH_WRITE by PrepareRenderTargets; sample it with no DSV bound, then put it back.
	const D3D12_RESOURCE_STATES depthBefore = m_pDevice->SceneDepthState();
	m_pDevice->TransitionSceneDepth( D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
	m_pDevice->BeginGpuPIXEvent( "Volumetric lighting" );
	m_pDevice->BeginGpuPIXEvent( "Volumetric march" );
	transitionRecord( *pScatter, D3D12_RESOURCE_STATE_RENDER_TARGET );
	transitionRecord( *pDepth, D3D12_RESOURCE_STATE_RENDER_TARGET );
	bool bBound = m_Pipeline.PrepareBindings( pList, march );
	if ( bBound )
	{
		const D3D12_VIEWPORT viewport{ 0.f, 0.f, static_cast<float>( halfWidth ), static_cast<float>( halfHeight ), 0.f, 1.f };
		const D3D12_RECT scissor{ 0, 0, halfWidth, halfHeight };
		const D3D12_CPU_DESCRIPTOR_HANDLE rtvs[2] = { pScatter->rtv, pDepth->rtv };
		ID3D12Resource *const colors[2] = { pScatter->resource.Get(), pDepth->resource.Get() };
		m_Pipeline.BindDrawState( pList, viewport, scissor, fence );
		m_Pipeline.BindRenderTargets( pList, 2, rtvs, colors, nullptr, nullptr, fence );
		m_Pipeline.BindPipelineState( pList, marchPso, 0, fence );
		pList->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
		m_pDevice->GpuReceiverDraw( false );
		pList->DrawInstanced( 3, 1, 0, 0 );
	}
	m_pDevice->EndGpuPIXEvent();
	if ( bBound )
	{
		m_pDevice->BeginGpuPIXEvent( "Volumetric upsample" );
		transitionRecord( *pScatter, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
		transitionRecord( *pDepth, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
		// Ordinary root: the march pair at t0/t1 through the normal SRV table, the scene depth again in space 6. The
		// reservation covers this table plus the 32-slot table PrepareBindings allocates after it.
		CPipelineCacheDX12::BindingInputDX12 composite( m_Pipeline.NullShaderResourceView() );
		bBound = prepareInput( composite ) && m_Pipeline.ReserveResourceDescriptors( DX12_SCREENSPACE_TABLE_COUNT + 32, fence ) && sceneDepthTable( composite.screenSpaceTable );
		if ( bBound )
		{
			composite.screenSpaceView = cameraAddress;
			composite.volumetricConstants = volumeAddress;
			ID3D12Resource *const sources[2] = { pScatter->resource.Get(), pDepth->resource.Get() };
			const DXGI_FORMAT formats[2] = { scatterFormat, depthFormat };
			for ( int i = 0; i < 2; ++i )
			{
				composite.textures[i] = sources[i];
				composite.srvSources[i] = {};
				D3D12_SHADER_RESOURCE_VIEW_DESC &srv = composite.srvDescs[i];
				srv = {};
				srv.Format = formats[i];
				srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
				srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
				srv.Texture2D.MipLevels = 1;
			}
			bBound = m_Pipeline.PrepareBindings( pList, composite );
		}
		if ( bBound )
		{
			const D3D12_VIEWPORT viewport{ static_cast<float>( x ), static_cast<float>( y ), static_cast<float>( width ), static_cast<float>( height ), 0.f, 1.f };
			const D3D12_RECT scissor{ x, y, x + width, y + height };
			m_Pipeline.BindDrawState( pList, viewport, scissor, fence );
			m_Pipeline.BindRenderTargets( pList, 1, &target.rtv, &target.color, nullptr, nullptr, fence );
			m_Pipeline.BindPipelineState( pList, upsamplePso, 0, fence );
			pList->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
			m_pDevice->GpuReceiverDraw( false );
			pList->DrawInstanced( 3, 1, 0, 0 );
		}
		m_pDevice->EndGpuPIXEvent();
	}
	m_pDevice->EndGpuPIXEvent();
	m_Pipeline.InvalidateGraphicsBindings();
	m_pDevice->TransitionSceneDepth( depthBefore );
	pScatter->sampledStateValid = pDepth->sampledStateValid = false;
	++m_nTextureStateEpoch;
	return bBound ? true : warn( "binding descriptors unavailable" );
}
} // namespace shaderapidx12
