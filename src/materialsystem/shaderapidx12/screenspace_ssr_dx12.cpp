//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Screen-space reflections of one opaque main-view bracket (plan §6-7). Runs on the recording owner from
//          the ordered DispatchSSR marker, after the G-buffer pass and motion vectors:
//            1. BlitTexture copies/resolves the scene FP16 viewport rectangle into DX12_SSRScene mip 0 (no encode).
//            2. ssr_depth_cs51 converts native (MSAA) depth to positive view depth, clear -> 1e30 (DX12_SSRDepth).
//            3. ssr_reduce_color_cs51 builds the mean-colour mip chain of DX12_SSRScene one level at a time.
//            4. ssr_trace_cs51 writes hit radiance/confidence (DX12_SSRTrace) and hit distance (DX12_SSRTraceDistance)
//               at the profile size, then ssr_composite_cs51 reconstructs the signed correction
//               confidence * ( hit * K - E ) - confidence * o * S (DX12_SSRDelta), o being the soft [hitDistance < D]
//               that removes the direct specular S of a receiver whose reflected light path is blocked.
//            5. BlitTexture adds the delta RGB onto the live scene (alpha and MSAA samples preserved).
//          Any failure stops before step 5, so the forward scene keeps its cubemap reflections. Scratch targets are
//          ordinary backend texture records promoted for UAV/mips at (re)creation and fence-retained by DeleteTexture.
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
// Byte-identical to HLSL cbuffer DX12SSR ( b0, space1 ) in native_src/ssr_common.hlsli.
struct alignas( 16 ) DX12SSRConstants
{
	float worldToClip[16];  // gSSRWorldToClip  row_major float4x4
	float clipToWorld[16];  // gSSRClipToWorld  row_major float4x4
	float eye[4];           // gSSREye          xyz eye, w sample count
	float forward[4];       // gSSRForward      xyz unit forward, w 0
	float viewport[4];      // gSSRViewport     scene offset x, y, local width, height
	float sizes[4];         // gSSRSizes        source width, height, trace width, height
	float traceParams[4];   // gSSRTrace        maxDistance, thickness, roughnessLimit, startDistance (0.5)
	uint32 control[4];      // gSSRControl      steps, refinements, colorMipCount, 0
	uint32 mip[4];          // gSSRMip          sourceMip, destinationMip, sourceWidth, sourceHeight (reduce pass)
	float limits[4];        // gSSRLimits       depthClear, normalBias (0.25), edgeFade (8), 0
};
static_assert( sizeof( DX12SSRConstants ) == 256, "DX12SSRConstants must match HLSL DX12SSR" );

// Plan §1 quality table. Trace size: 0/1 ceil( viewport / 2 ), 2 full viewport.
struct SSRProfileDX12
{
	bool halfResolution;
	uint32 samples, refinements;
	float roughnessLimit;
};
constexpr SSRProfileDX12 kSSRProfiles[3] = {
	{ true, 24, 3, 0.60f },
	{ true, 48, 4, 0.75f },
	{ false, 96, 5, 0.85f },
};

constexpr int kSSRGroupSize = 8;
constexpr float kSSRStartDistance = 0.5f, kSSRNormalBias = 0.25f, kSSREdgeFade = 8.f;

int SSRGroups( int nExtent )
{
	return ( nExtent + kSSRGroupSize - 1 ) / kSSRGroupSize;
}

int SSRFullMipChain( int nWidth, int nHeight )
{
	int nMips = 1;
	for ( int nExtent = MAX( nWidth, nHeight ); nExtent > 1; nExtent >>= 1 )
		++nMips;
	return nMips;
}

ShaderAPIDX12ComputeResource_t SSRSpecial( int nKind )
{
	ShaderAPIDX12ComputeResource_t resource{};
	resource.m_nKind = nKind;
	resource.m_nMip = 0;
	resource.m_nMipCount = 0;
	return resource;
}

ShaderAPIDX12ComputeResource_t SSRTexture( ShaderAPITextureHandle_t hTexture, int nMip, int nMipCount )
{
	ShaderAPIDX12ComputeResource_t resource{};
	resource.m_nKind = SHADERAPIDX12_COMPUTE_RESOURCE_TEXTURE;
	resource.m_hTexture = hTexture;
	resource.m_nMip = nMip;
	resource.m_nMipCount = nMipCount;
	return resource;
}

ShaderAPIDX12ComputeDispatch_t SSRDispatch( const char *pszShader, int nStaticIndex, const DX12SSRConstants &constants, int nWidth, int nHeight )
{
	ShaderAPIDX12ComputeDispatch_t dispatch{};
	dispatch.m_pShaderName = pszShader;
	dispatch.m_nStaticIndex = nStaticIndex;
	dispatch.m_nDynamicIndex = 0;
	dispatch.m_pConstants = &constants;
	dispatch.m_nConstantBytes = sizeof( constants );
	dispatch.m_nGroupsX = SSRGroups( nWidth );
	dispatch.m_nGroupsY = SSRGroups( nHeight );
	dispatch.m_nGroupsZ = 1;
	return dispatch;
}

// Balanced BeginGpuPIXEvent/EndGpuPIXEvent around one pass, including its failure returns.
struct SSRGpuEventDX12
{
	CShaderDeviceDX12 *m_pDevice;
	SSRGpuEventDX12( CShaderDeviceDX12 *pDevice, const char *pszName ) : m_pDevice( pDevice ) { m_pDevice->BeginGpuPIXEvent( pszName ); }
	~SSRGpuEventDX12() { m_pDevice->EndGpuPIXEvent(); }
};
} // namespace

//-----------------------------------------------------------------------------
// Purpose: (Re)creates one scratch target at the requested shape, UAV-promoted (and mip-promoted) right away so
//			the first effect frame never drains recording mid-sequence. The replaced texture is fence-retained.
//-----------------------------------------------------------------------------
bool CShaderAPIDX12::EnsureSSRTexture( SSRTextureDX12 &texture, int nWidth, int nHeight, ImageFormat format, bool bFullMips, const char *pszName )
{
	TextureRecord *pExisting = texture.handle ? FindTexture( texture.handle ) : nullptr;
	if ( pExisting && texture.width == nWidth && texture.height == nHeight && EnsureTextureResident( *pExisting ) )
		return true;
	if ( texture.handle )
		DeleteTexture( texture.handle );
	texture = {};
	const ShaderAPITextureHandle_t hTexture = CreateTexture( nWidth, nHeight, 1, format, 1, 1, TEXTURE_CREATE_RENDERTARGET, pszName, "RenderTargets" );
	TextureRecord *pRecord = hTexture ? FindTexture( hTexture ) : nullptr;
	const int nMips = bFullMips ? SSRFullMipChain( nWidth, nHeight ) : 1;
	if ( !pRecord || !PromoteRenderTarget( *pRecord, true, nMips ) )
	{
		if ( hTexture )
			DeleteTexture( hTexture );
		return false;
	}
	texture.handle = hTexture;
	texture.width = nWidth;
	texture.height = nHeight;
	texture.mipLevels = pRecord->mipLevels;
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Steps 1-5 for the bracket's scene rectangle; false leaves the scene colour untouched (step 5 not run)
//-----------------------------------------------------------------------------
bool CShaderAPIDX12::RunSSRPasses( const Rect_t &sceneRect )
{
	const DX12ScreenSpaceViewConstants &view = m_ScreenSpace.constants;
	const SSRProfileDX12 &profile = kSSRProfiles[MIN( m_ScreenSpace.ssr.quality, 2u )];
	const int nWidth = sceneRect.width, nHeight = sceneRect.height;
	const int nTraceWidth = profile.halfResolution ? ( nWidth + 1 ) / 2 : nWidth;
	const int nTraceHeight = profile.halfResolution ? ( nHeight + 1 ) / 2 : nHeight;
	if ( !EnsureSSRTexture( m_SSR.scene, nWidth, nHeight, IMAGE_FORMAT_RGBA16161616F, true, "DX12_SSRScene" ) ||
	     !EnsureSSRTexture( m_SSR.depth, nWidth, nHeight, IMAGE_FORMAT_R32F, false, "DX12_SSRDepth" ) ||
	     !EnsureSSRTexture( m_SSR.trace, nTraceWidth, nTraceHeight, IMAGE_FORMAT_RGBA16161616F, false, "DX12_SSRTrace" ) ||
	     !EnsureSSRTexture( m_SSR.traceDistance, nTraceWidth, nTraceHeight, IMAGE_FORMAT_R32F, false, "DX12_SSRTraceDistance" ) ||
	     !EnsureSSRTexture( m_SSR.delta, nWidth, nHeight, IMAGE_FORMAT_RGBA16161616F, false, "DX12_SSRDelta" ) )
	{
		if ( !m_SSR.warned )
		{
			m_SSR.warned = true;
			Warning( "ShaderAPIDX12: SSR scratch targets unavailable at %dx%d (trace %dx%d)\n", nWidth, nHeight, nTraceWidth, nTraceHeight );
		}
		return false;
	}
	TextureRecord *pScene = FindTexture( m_SSR.scene.handle ), *pDelta = FindTexture( m_SSR.delta.handle );
	if ( !pScene || !pDelta || !EnsureTextureResident( *pScene ) || !EnsureTextureResident( *pDelta ) )
		return false;
	const int nMsaa = view.control[1] > 1 ? 1 : 0;

	DX12SSRConstants constants;
	memset( &constants, 0, sizeof( constants ) );
	memcpy( constants.worldToClip, view.worldToClip, sizeof( constants.worldToClip ) );
	memcpy( constants.clipToWorld, view.clipToWorld, sizeof( constants.clipToWorld ) );
	constants.eye[0] = view.eyeLength[0];
	constants.eye[1] = view.eyeLength[1];
	constants.eye[2] = view.eyeLength[2];
	constants.eye[3] = static_cast<float>( view.control[1] );
	constants.forward[0] = view.forwardThickness[0];
	constants.forward[1] = view.forwardThickness[1];
	constants.forward[2] = view.forwardThickness[2];
	constants.viewport[0] = static_cast<float>( sceneRect.x );
	constants.viewport[1] = static_cast<float>( sceneRect.y );
	constants.viewport[2] = static_cast<float>( nWidth );
	constants.viewport[3] = static_cast<float>( nHeight );
	constants.sizes[0] = static_cast<float>( nWidth );
	constants.sizes[1] = static_cast<float>( nHeight );
	constants.sizes[2] = static_cast<float>( nTraceWidth );
	constants.sizes[3] = static_cast<float>( nTraceHeight );
	constants.traceParams[0] = m_ScreenSpace.ssr.maxDistance;
	constants.traceParams[1] = m_ScreenSpace.ssr.thickness;
	constants.traceParams[2] = profile.roughnessLimit;
	constants.traceParams[3] = kSSRStartDistance;
	constants.control[0] = profile.samples;
	constants.control[1] = profile.refinements;
	constants.control[2] = static_cast<uint32>( m_SSR.scene.mipLevels );
	constants.limits[0] = view.depth[0];
	constants.limits[1] = kSSRNormalBias;
	constants.limits[2] = kSSREdgeFade;

	// 1. Immutable copy of the scene at the opaque boundary (resolved under MSAA), then view depth.
	{
		SSRGpuEventDX12 event( m_pDevice, "SSR capture" );
		D3D12_RESOURCE_STATES sceneState = m_pDevice->SceneColorState();
		const Rect_t localRect{ 0, 0, nWidth, nHeight };
		if ( !BlitTexture( m_pDevice->SceneColor(), sceneState, true, CShaderDeviceDX12::kSceneImageFormat, pScene->resource.Get(), pScene->subresourceStates[0], false, pScene->format, sceneRect, localRect, false, false ) )
			return false;
		m_pDevice->TransitionSceneColor( D3D12_RESOURCE_STATE_RENDER_TARGET );
		pScene->dirtySubresources[0] = 0;
		pScene->initializedSubresources[0] = 0;
		pScene->gpuAuthoritativeSubresources[0] = 1;
		pScene->gpuDirty = false; // scratch targets never carry CPU staging data
		pScene->sampledStateValid = false;
		++m_nTextureStateEpoch;

		ShaderAPIDX12ComputeDispatch_t depth = SSRDispatch( "ssr_depth_cs51", nMsaa, constants, nWidth, nHeight );
		depth.m_Srv[0] = SSRSpecial( SHADERAPIDX12_COMPUTE_RESOURCE_SCENE_DEPTH );
		depth.m_Uav[0] = SSRTexture( m_SSR.depth.handle, 0, 1 );
		if ( !Dispatch( depth ) )
			return false;
	}

	// 2. Mean-colour chain: each level reads exactly one mip and writes the next (max( 1, base >> level )).
	{
		SSRGpuEventDX12 event( m_pDevice, "SSR color mips" );
		for ( int nLevel = 1; nLevel < m_SSR.scene.mipLevels; ++nLevel )
		{
			const int nSourceWidth = MAX( 1, nWidth >> ( nLevel - 1 ) ), nSourceHeight = MAX( 1, nHeight >> ( nLevel - 1 ) );
			const int nDestWidth = MAX( 1, nWidth >> nLevel ), nDestHeight = MAX( 1, nHeight >> nLevel );
			constants.mip[0] = static_cast<uint32>( nLevel - 1 );
			constants.mip[1] = static_cast<uint32>( nLevel );
			constants.mip[2] = static_cast<uint32>( nSourceWidth );
			constants.mip[3] = static_cast<uint32>( nSourceHeight );
			ShaderAPIDX12ComputeDispatch_t reduce = SSRDispatch( "ssr_reduce_color_cs51", 0, constants, nDestWidth, nDestHeight );
			reduce.m_Srv[0] = SSRTexture( m_SSR.scene.handle, nLevel - 1, 1 );
			reduce.m_Uav[0] = SSRTexture( m_SSR.scene.handle, nLevel, 1 );
			if ( !Dispatch( reduce ) )
				return false;
		}
		memset( constants.mip, 0, sizeof( constants.mip ) );
	}

	// 3. Trace at the profile size.
	{
		SSRGpuEventDX12 event( m_pDevice, "SSR trace" );
		ShaderAPIDX12ComputeDispatch_t trace = SSRDispatch( "ssr_trace_cs51", nMsaa, constants, nTraceWidth, nTraceHeight );
		trace.m_Srv[0] = SSRTexture( m_SSR.depth.handle, 0, 1 );
		trace.m_Srv[1] = SSRTexture( m_SSR.scene.handle, 0, m_SSR.scene.mipLevels );
		trace.m_Srv[2] = SSRSpecial( SHADERAPIDX12_COMPUTE_RESOURCE_PBR_NORMALS );
		trace.m_Srv[3] = SSRSpecial( SHADERAPIDX12_COMPUTE_RESOURCE_PBR_SPECULAR );
		trace.m_Srv[4] = SSRSpecial( SHADERAPIDX12_COMPUTE_RESOURCE_SCENE_STENCIL );
		trace.m_Srv[5] = SSRSpecial( SHADERAPIDX12_COMPUTE_RESOURCE_PBR_SSR_RESPONSE );
		trace.m_Srv[6] = SSRSpecial( SHADERAPIDX12_COMPUTE_RESOURCE_SCENE_DEPTH );
		trace.m_Uav[0] = SSRTexture( m_SSR.trace.handle, 0, 1 );
		trace.m_Uav[1] = SSRTexture( m_SSR.traceDistance.handle, 0, 1 );
		if ( !Dispatch( trace ) )
			return false;
	}

	// 4. Full-resolution reconstruction into the signed delta, 5. additive blit onto the live scene.
	{
		SSRGpuEventDX12 event( m_pDevice, "SSR composite" );
		ShaderAPIDX12ComputeDispatch_t composite = SSRDispatch( "ssr_composite_cs51", nMsaa, constants, nWidth, nHeight );
		composite.m_Srv[0] = SSRTexture( m_SSR.trace.handle, 0, 1 );
		composite.m_Srv[1] = SSRTexture( m_SSR.depth.handle, 0, 1 );
		composite.m_Srv[2] = SSRSpecial( SHADERAPIDX12_COMPUTE_RESOURCE_PBR_NORMALS );
		composite.m_Srv[3] = SSRSpecial( SHADERAPIDX12_COMPUTE_RESOURCE_PBR_SPECULAR );
		composite.m_Srv[4] = SSRSpecial( SHADERAPIDX12_COMPUTE_RESOURCE_SCENE_STENCIL );
		composite.m_Srv[5] = SSRSpecial( SHADERAPIDX12_COMPUTE_RESOURCE_PBR_ENVIRONMENT );
		composite.m_Srv[6] = SSRSpecial( SHADERAPIDX12_COMPUTE_RESOURCE_PBR_SSR_RESPONSE );
		composite.m_Srv[7] = SSRSpecial( SHADERAPIDX12_COMPUTE_RESOURCE_SCENE_DEPTH );
		composite.m_Srv[8] = SSRSpecial( SHADERAPIDX12_COMPUTE_RESOURCE_PBR_SSR_SPECULAR );
		composite.m_Srv[9] = SSRTexture( m_SSR.traceDistance.handle, 0, 1 );
		composite.m_Uav[0] = SSRTexture( m_SSR.delta.handle, 0, 1 );
		if ( !Dispatch( composite ) )
			return false;

		D3D12_RESOURCE_STATES sceneState = m_pDevice->SceneColorState();
		const Rect_t localRect{ 0, 0, nWidth, nHeight };
		if ( !BlitTexture( pDelta->resource.Get(), pDelta->subresourceStates[0], false, pDelta->format, m_pDevice->SceneColor(), sceneState, true, CShaderDeviceDX12::kSceneImageFormat, localRect, sceneRect, false, false, nullptr, BlitEncodeDX12::None, true ) )
			return false;
		pDelta->sampledStateValid = false;
		++m_nTextureStateEpoch;
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Ordered DispatchSSR replay: only an active, SSR-capturing bracket whose payload targets exist runs
//-----------------------------------------------------------------------------
void CShaderAPIDX12::ReplayDispatchSSR()
{
	if ( !m_pDevice || !m_pDevice->IsRecordingOwner() || !m_pDevice->CommandList() || !m_ScreenSpace.active || !m_ScreenSpace.ssrCapture ||
	     !m_pGBufferEnvironment || !m_pDevice->SceneColor() || !m_pDevice->SceneDepth() )
		return;
	const float *pViewport = m_ScreenSpace.constants.viewport;
	const Rect_t sceneRect{ static_cast<int>( pViewport[0] ), static_cast<int>( pViewport[1] ), static_cast<int>( pViewport[2] ), static_cast<int>( pViewport[3] ) };
	if ( sceneRect.width <= 0 || sceneRect.height <= 0 )
		return;
	FlushBufferedPrimitives();
	if ( !RunSSRPasses( sceneRect ) )
	{
		// The capture may have left the scene colour readable; forward drawing resumes on a render target.
		m_pDevice->TransitionSceneColor( D3D12_RESOURCE_STATE_RENDER_TARGET );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Drops the scratch targets (DeleteTexture fence-retains their resources); device reset / texture teardown
//-----------------------------------------------------------------------------
void CShaderAPIDX12::ReleaseSSRResources()
{
	SSRTextureDX12 *const textures[] = { &m_SSR.scene, &m_SSR.depth, &m_SSR.trace, &m_SSR.traceDistance, &m_SSR.delta };
	for ( SSRTextureDX12 *pTexture : textures )
	{
		if ( pTexture->handle )
			DeleteTexture( pTexture->handle );
		*pTexture = {};
	}
	m_SSR.warned = false;
}
} // namespace shaderapidx12
