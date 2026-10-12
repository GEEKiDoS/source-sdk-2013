//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Stochastic screen-space reflections of one opaque main-view bracket. Runs on the recording owner from
//          the ordered DispatchSSR marker, after the G-buffer pass and motion vectors, on single-sample scenes:
//            1. BlitTexture copies the scene FP16 viewport rectangle into DX12_SSRScene mip 0 (no encode), then
//               ssr_depth_cs51 writes positive view depth, clear -> 1e30 (DX12_SSRDepth[current]).
//            2. ssr_reduce_color_cs51 builds the mean-colour mip chain of DX12_SSRScene one level at a time.
//            3. ssr_trace_cs51 traces one GGX visible-normal sample per trace texel at the profile size: hit
//               radiance (occluded by this frame's GTAO term when published) / confidence (DX12_SSRTrace) and the ray
//               with its pdf (DX12_SSRRay).
//            4. ssr_resolve_cs51 reuses each full-resolution receiver's neighbouring rays, re-weighted to its own lobe
//               (DX12_SSRResolve, DX12_SSRResolveAux).
//            5. ssr_composite_cs51 accumulates the resolve over the reprojected history (DX12_SSRHistory[current ^ 1],
//               validated by DX12_SSRDepth[current ^ 1]; motion vectors when resolved this frame) and writes the
//               next history and the signed correction C.rgb * K - C.a * E - O * S (DX12_SSRDelta).
//            6. BlitTexture adds the delta RGB onto the live scene (alpha preserved).
//          Any failure stops before step 6, so the forward scene keeps its cubemap reflections, and drops the
//          history. Scratch targets are ordinary backend texture records promoted for UAV/mips at (re)creation and
//          fence-retained by DeleteTexture.
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
	float worldToClip[16];     // gSSRWorldToClip      row_major float4x4
	float clipToWorld[16];     // gSSRClipToWorld      row_major float4x4
	float prevWorldToClip[16]; // gSSRPrevWorldToClip  previous SSR frame's worldToClip
	float eye[4];              // gSSREye              xyz eye, w 0
	float forward[4];          // gSSRForward          xyz unit forward, w 0
	float viewport[4];         // gSSRViewport         scene offset x, y, local width, height
	float sizes[4];            // gSSRSizes            source width, height, trace width, height
	float traceParams[4];      // gSSRTrace            maxDistance, thickness, resolveRadius, startDistance (0.5)
	uint32 control[4];         // gSSRControl          steps, refinements, colorMipCount, frame index
	uint32 mip[4];             // gSSRMip              sourceMip, destinationMip, sourceWidth, sourceHeight (reduce pass)
	float limits[4];           // gSSRLimits           depthClear, normalBias (0.25), edgeFade (8), 0
	float temporal[4];         // gSSRTemporal         history valid, max history frames (rough lobes), scene width, scene height
	uint32 resolve[4];         // gSSRResolve          resolve neighbour taps, motion vectors valid, 0, 0
	float ambient[4];          // gSSRAmbient          AO term uv per local pixel x, y, value scale (1 / 1.5 packed), valid
};
static_assert( sizeof( DX12SSRConstants ) == 368, "DX12SSRConstants must match HLSL DX12SSR" );
static_assert( sizeof( DX12SSRConstants ) <= SHADERAPIDX12_COMPUTE_MAX_CONSTANTS, "DX12SSRConstants exceeds the compute constant limit" );

// Quality table. Trace size: 0/1 ceil( viewport / 2 ), 2 full viewport. Every roughness is traced.
struct SSRProfileDX12
{
	bool halfResolution;
	uint32 samples, refinements;
	uint32 resolveTaps; // neighbour rays the resolve reuses besides the receiver's own trace texel
};
constexpr SSRProfileDX12 kSSRProfiles[3] = {
	{ true, 24, 3, 4 },
	{ true, 48, 4, 6 },
	{ false, 96, 5, 8 },
};

constexpr int kSSRGroupSize = 8;
constexpr float kSSRStartDistance = 0.5f, kSSRNormalBias = 0.25f, kSSREdgeFade = 8.f;
// Resolve disk radius in trace texels at roughness 1 (1 + radius * sqrt( roughness )).
constexpr float kSSRResolveRadius = 3.f;
// Frames a stable rough-lobe pixel averages (glossy lobes cap at 8 in the shader); the temporal upscaler accumulates
// further.
constexpr float kSSRMaxHistory = 32.f;

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

ShaderAPIDX12ComputeDispatch_t SSRDispatch( const char *pszShader, const DX12SSRConstants &constants, int nWidth, int nHeight )
{
	ShaderAPIDX12ComputeDispatch_t dispatch{};
	dispatch.m_pShaderName = pszShader;
	dispatch.m_nStaticIndex = 0;
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
// Purpose: Steps 1-6 for the bracket's scene rectangle; false leaves the scene colour untouched (step 6 not run)
//-----------------------------------------------------------------------------
bool CShaderAPIDX12::RunSSRPasses( const Rect_t &sceneRect )
{
	const DX12ScreenSpaceViewConstants &view = m_ScreenSpace.constants;
	const SSRProfileDX12 &profile = kSSRProfiles[MIN( m_ScreenSpace.ssr.quality, 2u )];
	const int nWidth = sceneRect.width, nHeight = sceneRect.height;
	const int nTraceWidth = profile.halfResolution ? ( nWidth + 1 ) / 2 : nWidth;
	const int nTraceHeight = profile.halfResolution ? ( nHeight + 1 ) / 2 : nHeight;
	const int nCurrent = m_SSR.current, nPrevious = m_SSR.current ^ 1;
	// A recreated previous depth/history target carries no history.
	const ShaderAPITextureHandle_t hPrevDepth = m_SSR.depth[nPrevious].handle, hPrevHistory = m_SSR.history[nPrevious].handle,
		hPrevAux = m_SSR.historyAux[nPrevious].handle;
	if ( !EnsureSSRTexture( m_SSR.scene, nWidth, nHeight, IMAGE_FORMAT_RGBA16161616F, true, "DX12_SSRScene" ) ||
	     !EnsureSSRTexture( m_SSR.depth[0], nWidth, nHeight, IMAGE_FORMAT_R32F, false, "DX12_SSRDepth0" ) ||
	     !EnsureSSRTexture( m_SSR.depth[1], nWidth, nHeight, IMAGE_FORMAT_R32F, false, "DX12_SSRDepth1" ) ||
	     !EnsureSSRTexture( m_SSR.trace, nTraceWidth, nTraceHeight, IMAGE_FORMAT_RGBA16161616F, false, "DX12_SSRTrace" ) ||
	     !EnsureSSRTexture( m_SSR.ray, nTraceWidth, nTraceHeight, IMAGE_FORMAT_RGBA32323232F, false, "DX12_SSRRay" ) ||
	     !EnsureSSRTexture( m_SSR.resolve, nWidth, nHeight, IMAGE_FORMAT_RGBA16161616F, false, "DX12_SSRResolve" ) ||
	     !EnsureSSRTexture( m_SSR.resolveAux, nWidth, nHeight, IMAGE_FORMAT_RGBA16161616F, false, "DX12_SSRResolveAux" ) ||
	     !EnsureSSRTexture( m_SSR.history[0], nWidth, nHeight, IMAGE_FORMAT_RGBA16161616F, false, "DX12_SSRHistory0" ) ||
	     !EnsureSSRTexture( m_SSR.history[1], nWidth, nHeight, IMAGE_FORMAT_RGBA16161616F, false, "DX12_SSRHistory1" ) ||
	     !EnsureSSRTexture( m_SSR.historyAux[0], nWidth, nHeight, IMAGE_FORMAT_RGBA16161616F, false, "DX12_SSRHistoryAux0" ) ||
	     !EnsureSSRTexture( m_SSR.historyAux[1], nWidth, nHeight, IMAGE_FORMAT_RGBA16161616F, false, "DX12_SSRHistoryAux1" ) ||
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
	const bool bHistory = m_SSR.prevFrame != ~0ull && m_SSR.prevFrame + 1 == m_nFrameCounter &&
		m_SSR.prevRect.x == sceneRect.x && m_SSR.prevRect.y == sceneRect.y && m_SSR.prevRect.width == nWidth && m_SSR.prevRect.height == nHeight &&
		hPrevDepth == m_SSR.depth[nPrevious].handle && hPrevHistory == m_SSR.history[nPrevious].handle &&
		hPrevAux == m_SSR.historyAux[nPrevious].handle;
	// Motion vectors resolved this frame into a scene-sized texture.
	const int nSceneWidth = m_pDevice->SceneWidth(), nSceneHeight = m_pDevice->SceneHeight();
	TextureRecord *pMotion = m_hMotionResolvedHandle && m_nMotionResolvedFrame == m_nFrameCounter ? FindTexture( m_hMotionResolvedHandle ) : nullptr;
	const bool bMotion = pMotion && pMotion->width == nSceneWidth && pMotion->height == nSceneHeight;
	// This frame's AO term (GTAO computes before the SSR marker and applies after it).
	TextureRecord *pAmbient = m_SSR.aoTerm && m_SSR.aoFrame == m_nFrameCounter && m_SSR.aoWidth > 0 && m_SSR.aoHeight > 0 ? FindTexture( m_SSR.aoTerm ) : nullptr;
	const bool bAmbient = pAmbient && pAmbient->width >= m_SSR.aoWidth && pAmbient->height >= m_SSR.aoHeight;

	DX12SSRConstants constants;
	memset( &constants, 0, sizeof( constants ) );
	memcpy( constants.worldToClip, view.worldToClip, sizeof( constants.worldToClip ) );
	memcpy( constants.clipToWorld, view.clipToWorld, sizeof( constants.clipToWorld ) );
	memcpy( constants.prevWorldToClip, bHistory ? m_SSR.prevWorldToClip : view.worldToClip, sizeof( constants.prevWorldToClip ) );
	constants.eye[0] = view.eyeLength[0];
	constants.eye[1] = view.eyeLength[1];
	constants.eye[2] = view.eyeLength[2];
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
	constants.traceParams[2] = kSSRResolveRadius;
	constants.traceParams[3] = kSSRStartDistance;
	constants.control[0] = profile.samples;
	constants.control[1] = profile.refinements;
	constants.control[2] = static_cast<uint32>( m_SSR.scene.mipLevels );
	constants.control[3] = static_cast<uint32>( m_nFrameCounter );
	constants.limits[0] = view.depth[0];
	constants.limits[1] = kSSRNormalBias;
	constants.limits[2] = kSSREdgeFade;
	constants.temporal[0] = bHistory ? 1.f : 0.f;
	constants.temporal[1] = kSSRMaxHistory;
	constants.temporal[2] = static_cast<float>( nSceneWidth );
	constants.temporal[3] = static_cast<float>( nSceneHeight );
	constants.resolve[0] = profile.resolveTaps;
	constants.resolve[1] = bMotion ? 1u : 0u;
	if ( bAmbient )
	{
		constants.ambient[0] = static_cast<float>( m_SSR.aoWidth ) / ( static_cast<float>( nWidth ) * static_cast<float>( pAmbient->width ) );
		constants.ambient[1] = static_cast<float>( m_SSR.aoHeight ) / ( static_cast<float>( nHeight ) * static_cast<float>( pAmbient->height ) );
		constants.ambient[2] = m_SSR.aoPacked ? 1.5f : 1.f;
		constants.ambient[3] = 1.f;
	}

	// 1. Immutable copy of the scene at the opaque boundary, then view depth.
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

		ShaderAPIDX12ComputeDispatch_t depth = SSRDispatch( "ssr_depth_cs51", constants, nWidth, nHeight );
		depth.m_Srv[0] = SSRSpecial( SHADERAPIDX12_COMPUTE_RESOURCE_SCENE_DEPTH );
		depth.m_Uav[0] = SSRTexture( m_SSR.depth[nCurrent].handle, 0, 1 );
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
			ShaderAPIDX12ComputeDispatch_t reduce = SSRDispatch( "ssr_reduce_color_cs51", constants, nDestWidth, nDestHeight );
			reduce.m_Srv[0] = SSRTexture( m_SSR.scene.handle, nLevel - 1, 1 );
			reduce.m_Uav[0] = SSRTexture( m_SSR.scene.handle, nLevel, 1 );
			if ( !Dispatch( reduce ) )
				return false;
		}
		memset( constants.mip, 0, sizeof( constants.mip ) );
	}

	// 3. Stochastic trace at the profile size.
	{
		SSRGpuEventDX12 event( m_pDevice, "SSR trace" );
		ShaderAPIDX12ComputeDispatch_t trace = SSRDispatch( "ssr_trace_cs51", constants, nTraceWidth, nTraceHeight );
		trace.m_Srv[0] = SSRTexture( m_SSR.depth[nCurrent].handle, 0, 1 );
		trace.m_Srv[1] = SSRTexture( m_SSR.scene.handle, 0, m_SSR.scene.mipLevels );
		trace.m_Srv[2] = SSRSpecial( SHADERAPIDX12_COMPUTE_RESOURCE_PBR_NORMALS );
		trace.m_Srv[3] = SSRSpecial( SHADERAPIDX12_COMPUTE_RESOURCE_PBR_SPECULAR );
		trace.m_Srv[4] = SSRSpecial( SHADERAPIDX12_COMPUTE_RESOURCE_SCENE_STENCIL );
		trace.m_Srv[5] = SSRSpecial( SHADERAPIDX12_COMPUTE_RESOURCE_PBR_SSR_RESPONSE );
		trace.m_Srv[6] = SSRSpecial( SHADERAPIDX12_COMPUTE_RESOURCE_SCENE_DEPTH );
		// Without an AO term the shader never reads t7; any SRV keeps the table complete.
		trace.m_Srv[7] = SSRTexture( bAmbient ? m_SSR.aoTerm : m_SSR.depth[nCurrent].handle, 0, 1 );
		trace.m_Uav[0] = SSRTexture( m_SSR.trace.handle, 0, 1 );
		trace.m_Uav[1] = SSRTexture( m_SSR.ray.handle, 0, 1 );
		if ( !Dispatch( trace ) )
			return false;
	}

	// 4. Full-resolution ray reuse.
	{
		SSRGpuEventDX12 event( m_pDevice, "SSR resolve" );
		ShaderAPIDX12ComputeDispatch_t resolve = SSRDispatch( "ssr_resolve_cs51", constants, nWidth, nHeight );
		resolve.m_Srv[0] = SSRTexture( m_SSR.trace.handle, 0, 1 );
		resolve.m_Srv[1] = SSRTexture( m_SSR.ray.handle, 0, 1 );
		resolve.m_Srv[2] = SSRSpecial( SHADERAPIDX12_COMPUTE_RESOURCE_PBR_NORMALS );
		resolve.m_Srv[3] = SSRSpecial( SHADERAPIDX12_COMPUTE_RESOURCE_PBR_SPECULAR );
		resolve.m_Srv[4] = SSRSpecial( SHADERAPIDX12_COMPUTE_RESOURCE_SCENE_STENCIL );
		resolve.m_Srv[5] = SSRSpecial( SHADERAPIDX12_COMPUTE_RESOURCE_PBR_SSR_RESPONSE );
		resolve.m_Srv[6] = SSRSpecial( SHADERAPIDX12_COMPUTE_RESOURCE_SCENE_DEPTH );
		resolve.m_Srv[7] = SSRSpecial( SHADERAPIDX12_COMPUTE_RESOURCE_PBR_ENVIRONMENT );
		resolve.m_Srv[8] = SSRSpecial( SHADERAPIDX12_COMPUTE_RESOURCE_PBR_SSR_SPECULAR );
		resolve.m_Uav[0] = SSRTexture( m_SSR.resolve.handle, 0, 1 );
		resolve.m_Uav[1] = SSRTexture( m_SSR.resolveAux.handle, 0, 1 );
		if ( !Dispatch( resolve ) )
			return false;
	}

	// 5. Temporal accumulation into the signed delta and the next history, 6. additive blit onto the live scene.
	{
		SSRGpuEventDX12 event( m_pDevice, "SSR composite" );
		ShaderAPIDX12ComputeDispatch_t composite = SSRDispatch( "ssr_composite_cs51", constants, nWidth, nHeight );
		composite.m_Srv[0] = SSRTexture( m_SSR.resolve.handle, 0, 1 );
		composite.m_Srv[1] = SSRTexture( m_SSR.resolveAux.handle, 0, 1 );
		composite.m_Srv[2] = SSRSpecial( SHADERAPIDX12_COMPUTE_RESOURCE_PBR_NORMALS );
		composite.m_Srv[3] = SSRSpecial( SHADERAPIDX12_COMPUTE_RESOURCE_PBR_SPECULAR );
		composite.m_Srv[4] = SSRSpecial( SHADERAPIDX12_COMPUTE_RESOURCE_SCENE_STENCIL );
		composite.m_Srv[5] = SSRSpecial( SHADERAPIDX12_COMPUTE_RESOURCE_PBR_ENVIRONMENT );
		composite.m_Srv[6] = SSRSpecial( SHADERAPIDX12_COMPUTE_RESOURCE_PBR_SSR_RESPONSE );
		composite.m_Srv[7] = SSRSpecial( SHADERAPIDX12_COMPUTE_RESOURCE_SCENE_DEPTH );
		composite.m_Srv[8] = SSRSpecial( SHADERAPIDX12_COMPUTE_RESOURCE_PBR_SSR_SPECULAR );
		composite.m_Srv[9] = SSRTexture( m_SSR.history[nPrevious].handle, 0, 1 );
		composite.m_Srv[10] = SSRTexture( m_SSR.historyAux[nPrevious].handle, 0, 1 );
		composite.m_Srv[11] = SSRTexture( m_SSR.depth[nPrevious].handle, 0, 1 );
		// Without motion vectors the shader never reads t12; any SRV keeps the table complete.
		composite.m_Srv[12] = SSRTexture( bMotion ? m_hMotionResolvedHandle : m_SSR.resolve.handle, 0, 1 );
		composite.m_Uav[0] = SSRTexture( m_SSR.delta.handle, 0, 1 );
		composite.m_Uav[1] = SSRTexture( m_SSR.history[nCurrent].handle, 0, 1 );
		composite.m_Uav[2] = SSRTexture( m_SSR.historyAux[nCurrent].handle, 0, 1 );
		if ( !Dispatch( composite ) )
			return false;

		D3D12_RESOURCE_STATES sceneState = m_pDevice->SceneColorState();
		const Rect_t localRect{ 0, 0, nWidth, nHeight };
		if ( !BlitTexture( pDelta->resource.Get(), pDelta->subresourceStates[0], false, pDelta->format, m_pDevice->SceneColor(), sceneState, true, CShaderDeviceDX12::kSceneImageFormat, localRect, sceneRect, false, false, nullptr, BlitEncodeDX12::None, true ) )
			return false;
		pDelta->sampledStateValid = false;
		++m_nTextureStateEpoch;
	}

	memcpy( m_SSR.prevWorldToClip, view.worldToClip, sizeof( m_SSR.prevWorldToClip ) );
	m_SSR.prevRect = sceneRect;
	m_SSR.prevFrame = m_nFrameCounter;
	m_SSR.current = nPrevious;
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Records this frame's AO term (called with the GTAO compute passes, before the SSR marker replays)
//-----------------------------------------------------------------------------
void CShaderAPIDX12::SetAmbientOcclusionTerm( ShaderAPITextureHandle_t hTerm, int nWidth, int nHeight, bool bPacked )
{
	m_SSR.aoTerm = hTerm;
	m_SSR.aoWidth = nWidth;
	m_SSR.aoHeight = nHeight;
	m_SSR.aoPacked = bPacked;
	m_SSR.aoFrame = hTerm ? m_nFrameCounter : ~0ull;
}

//-----------------------------------------------------------------------------
// Purpose: Ordered DispatchSSR replay: only an active, SSR-capturing, single-sample bracket whose payload targets
//			exist runs
//-----------------------------------------------------------------------------
void CShaderAPIDX12::ReplayDispatchSSR()
{
	if ( !m_pDevice || !m_pDevice->IsRecordingOwner() || !m_pDevice->CommandList() || !m_ScreenSpace.active || !m_ScreenSpace.ssrCapture ||
	     !m_pGBufferEnvironment || !m_pDevice->SceneColor() || !m_pDevice->SceneDepth() )
		return;
	if ( m_pDevice->SceneSampleCount() > 1 )
	{
		if ( !m_SSR.msaaWarned )
		{
			m_SSR.msaaWarned = true;
			Warning( "ShaderAPIDX12: SSR requires a single-sample scene; skipped at %dx MSAA\n", m_pDevice->SceneSampleCount() );
		}
		m_SSR.prevFrame = ~0ull;
		return;
	}
	const float *pViewport = m_ScreenSpace.constants.viewport;
	const Rect_t sceneRect{ static_cast<int>( pViewport[0] ), static_cast<int>( pViewport[1] ), static_cast<int>( pViewport[2] ), static_cast<int>( pViewport[3] ) };
	if ( sceneRect.width <= 0 || sceneRect.height <= 0 )
		return;
	FlushBufferedPrimitives();
	if ( !RunSSRPasses( sceneRect ) )
	{
		m_SSR.prevFrame = ~0ull;
		// The capture may have left the scene colour readable; forward drawing resumes on a render target.
		m_pDevice->TransitionSceneColor( D3D12_RESOURCE_STATE_RENDER_TARGET );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Drops the scratch targets (DeleteTexture fence-retains their resources) and the history; device reset /
//			texture teardown
//-----------------------------------------------------------------------------
void CShaderAPIDX12::ReleaseSSRResources()
{
	SSRTextureDX12 *const textures[] = { &m_SSR.scene, &m_SSR.trace, &m_SSR.ray, &m_SSR.resolve, &m_SSR.resolveAux, &m_SSR.delta,
		&m_SSR.depth[0], &m_SSR.depth[1], &m_SSR.history[0], &m_SSR.history[1], &m_SSR.historyAux[0], &m_SSR.historyAux[1] };
	for ( SSRTextureDX12 *pTexture : textures )
	{
		if ( pTexture->handle )
			DeleteTexture( pTexture->handle );
		*pTexture = {};
	}
	m_SSR.current = 0;
	m_SSR.prevFrame = ~0ull;
	m_SSR.aoTerm = 0;
	m_SSR.aoFrame = ~0ull;
	m_SSR.warned = m_SSR.msaaWarned = false;
}
} // namespace shaderapidx12
