//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DX12 GTAO client render-target, material and pass scheduling.
//
//===========================================================================//
#include "cbase.h"
#include "gtao_dx12.h"
#include "upscaler_dx12.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imaterialsystemhardwareconfig.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/imaterialvar.h"
#include "materialsystem/itexture.h"
#include "rendertexture.h"
#include "fmtstr.h"
#include "view_shared.h"
#include "mathlib/vmatrix.h"
#include <cmath>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace
{
struct GtaoMaterial_t
{
	IMaterial *m_pMaterial = nullptr;
};

static bool s_bDX12 = false;
static bool s_bRenderTargetsReady = false;
static bool s_bMaterialsReady = false;
static int s_nDispatchedFrame = -1;
static GtaoMaterial_t s_Prefilter;
static GtaoMaterial_t s_ViewDepth;
static GtaoMaterial_t s_Main;
static GtaoMaterial_t s_Denoise[3];
static GtaoMaterial_t s_Apply;
static GtaoMaterial_t s_ApplyDebug;
static CTextureReference s_GtaoDepth;
static CTextureReference s_GtaoViewDepth;
static CTextureReference s_GtaoTerm0;
static CTextureReference s_GtaoTerm1;
static CTextureReference s_GtaoEdges;

static IMaterialVar *FindVar( IMaterial *pMaterial, const char *pName )
{
	if ( !pMaterial )
		return nullptr;
	bool bFound = false;
	return pMaterial->FindVar( pName, &bFound, false );
}

static void SetInt( IMaterial *pMaterial, const char *pName, int value )
{
	if ( IMaterialVar *pVar = FindVar( pMaterial, pName ) )
		pVar->SetIntValue( value );
}


static void SetVector4( IMaterial *pMaterial, const char *pName, const float *value )
{
	if ( IMaterialVar *pVar = FindVar( pMaterial, pName ) )
		pVar->SetVecValue( value, 4 );
}

static void SetTexture( IMaterial *pMaterial, const char *pName, ITexture *pTexture )
{
	if ( IMaterialVar *pVar = FindVar( pMaterial, pName ) )
		pVar->SetTextureValue( pTexture );
}

// Pass selection lives in the VMT key values so it survives material reloads.
static IMaterial *CreateMaterial( IMaterialSystem *pMaterialSystem, const char *pName, const char *pShader, const char *pKey, int nValue )
{
	KeyValues *pKeyValues = new KeyValues( pShader );
	pKeyValues->SetInt( pKey, nValue );
	return pMaterialSystem->CreateMaterial( pName, pKeyValues );
}

static void ReleaseMaterial( GtaoMaterial_t &material )
{
	if ( material.m_pMaterial )
	{
		material.m_pMaterial->DecrementReferenceCount();
		material.m_pMaterial = nullptr;
	}
}

static bool MaterialValid( IMaterial *pMaterial )
{
	return pMaterial && !pMaterial->IsErrorMaterial();
}

static void SetGtaoConstants( IMaterial *pMaterial, int aoWidth, int aoHeight, int fullWidth, int fullHeight,
	const VMatrix &projection, float radius, float power, int noiseIndex, int slices, int steps, int denoiseFinal,
	int originX, int originY )
{
	const float projectionDepth = projection[3][2];
	const float depthUnpack[4] = { projection[2][3] / projectionDepth, projection[2][2] / projectionDepth, 0.f, 0.f };
	const float mulX = -projectionDepth / projection[0][0];
	const float mulY = -projectionDepth / projection[1][1];
	const float addX = projection[0][2] / projection[0][0];
	const float addY = projection[1][2] / projection[1][1];
	const float ndcMul[4] = { 2.f * mulX, -2.f * mulY, 0.f, 0.f };
	const float ndcAdd[4] = { addX - mulX, mulY + addY, 0.f, 0.f };
	const float cameraFov[4] = { 1.f / fabsf( projection[0][0] ), 1.f / fabsf( projection[1][1] ), 0.f, 0.f };
	const float viewport[4] = { static_cast<float>( aoWidth ), static_cast<float>( aoHeight ), 1.f / aoWidth, 1.f / aoHeight };
	const float ndcPixel[4] = { ndcMul[0] / aoWidth, ndcMul[1] / aoHeight, 0.f, 0.f };
	const float gtao0[4] = { radius, 0.615f, 1.457f, power };
	const float gtao1[4] = { denoiseFinal ? 1.2f : 1e4f, 2.f, 0.f, 3.30f };
	const float gtao2[4] = { static_cast<float>( noiseIndex ), static_cast<float>( slices ), static_cast<float>( steps ), static_cast<float>( denoiseFinal ) };

	SetVector4( pMaterial, "$viewport", viewport );
	SetVector4( pMaterial, "$depthunpack", depthUnpack );
	SetVector4( pMaterial, "$cameratanhfov", cameraFov );
	SetVector4( pMaterial, "$ndctoviewmul", ndcMul );
	SetVector4( pMaterial, "$ndctoviewadd", ndcAdd );
	SetVector4( pMaterial, "$ndctoviewmulxpixelsize", ndcPixel );
	SetVector4( pMaterial, "$effect", gtao0 );
	SetVector4( pMaterial, "$denoise", gtao1 );
	SetVector4( pMaterial, "$noise", gtao2 );
	const float sceneOrigin[4] = { static_cast<float>( originX ), static_cast<float>( originY ), 0.f, 0.f };
	const float depthScale[4] = { fullWidth / static_cast<float>( aoWidth ), fullHeight / static_cast<float>( aoHeight ), 0.f, 0.f };
	SetVector4( pMaterial, "$sceneorigin", sceneOrigin );
	SetVector4( pMaterial, "$depthscale", depthScale );
}

static void SetGtaoApplyConstants( IMaterial *pMaterial, int aoWidth, int aoHeight, int fullWidth, int fullHeight, bool bRestoreScale, int originX, int originY,
	const VMatrix &view, const VMatrix &projection )
{
	const float sceneOrigin[4] = { static_cast<float>( originX ), static_cast<float>( originY ), 0.f, 0.f };
	const float fullSize[4] = { static_cast<float>( fullWidth ), static_cast<float>( fullHeight ), 0.f, 0.f };
	const float aoSize[4] = { static_cast<float>( aoWidth ), static_cast<float>( aoHeight ), 0.f, 0.f };
	SetVector4( pMaterial, "$sceneorigin", sceneOrigin );
	SetVector4( pMaterial, "$fullsize", fullSize );
	SetVector4( pMaterial, "$aosize", aoSize );
	SetInt( pMaterial, "$fullres", aoWidth == fullWidth && aoHeight == fullHeight );
	SetInt( pMaterial, "$restorescale", bRestoreScale );

	// Scene fog reconstruction. GTAO view depth d is -z_view, so the materials' projPos.z is P23 - P22 * d, and
	// clip = C * ( ndc.xy * d, d, 1 ) with clip.w = -P32 * d; world = inverse( P * V ) * C (homogeneous).
	const float clipZ[4] = { projection[2][3], -projection[2][2], 0.f, 0.f };
	SetVector4( pMaterial, "$fogclipz", clipZ );
	VMatrix inverseView;
	if ( MatrixInverseGeneral( view, inverseView ) )
	{
		const Vector eye = inverseView.GetTranslation();
		const float fogEye[4] = { eye.x, eye.y, eye.z, 0.f };
		SetVector4( pMaterial, "$fogeye", fogEye );
	}
	VMatrix inverseViewProjection;
	if ( MatrixInverseGeneral( projection * view, inverseViewProjection ) )
	{
		const float w = -projection[3][2];
		const VMatrix depthToClip( w, 0.f, 0.f, 0.f,
			0.f, w, 0.f, 0.f,
			0.f, 0.f, -projection[2][2], projection[2][3],
			0.f, 0.f, w, 0.f );
		const VMatrix toWorld = inverseViewProjection * depthToClip;
		static const char *const kRows[4] = { "$fogworld0", "$fogworld1", "$fogworld2", "$fogworld3" };
		for ( int i = 0; i < 4; ++i )
			SetVector4( pMaterial, kRows[i], toWorld[i] );
	}
}

// Compute materials dispatch from their dynamic state and skip the raster draw; the rectangle only drives the pass.
static void DrawCompute( IMatRenderContext *pRenderContext, IMaterial *pMaterial )
{
	pRenderContext->DrawScreenSpaceRectangle( pMaterial, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1 );
}
}

ConVar r_gtao( "r_gtao", "1", FCVAR_ARCHIVE, "DX12: ground-truth ambient occlusion over the opaque scene (0 off, 1 on)", true, 0, true, 1 );
ConVar r_gtao_slices( "r_gtao_slices", "3", FCVAR_ARCHIVE, "GTAO slices per pixel (1 fastest, 9 highest quality)", true, 1, true, 9 );
ConVar r_gtao_steps( "r_gtao_steps", "3", FCVAR_ARCHIVE, "GTAO horizon steps per slice side", true, 1, true, 8 );
ConVar r_gtao_resolution( "r_gtao_resolution", "1", FCVAR_ARCHIVE, "GTAO internal resolution scale per axis; below 1 the term is upsampled depth-aware", true, 0.25f, true, 1.f );
ConVar r_gtao_radius( "r_gtao_radius", "24", FCVAR_ARCHIVE, "GTAO effect radius in world units", true, 1.f, true, 1024.f );
ConVar r_gtao_power( "r_gtao_power", "2.2", FCVAR_ARCHIVE, "GTAO final value power (occlusion contrast)", true, 0.1f, true, 8.f );
ConVar r_gtao_denoise( "r_gtao_denoise", "2", FCVAR_ARCHIVE, "GTAO spatial denoise passes (with temporal AA the noise pattern also animates so the upscaler accumulates it)", true, 0, true, 3 );
ConVar r_gtao_debug( "r_gtao_debug", "0", 0, "GTAO: 1 shows the AO term instead of applying it", true, 0, true, 1 );

void GTAODX12_Init( IMaterialSystemHardwareConfig *pHardwareConfig )
{
	const char *shaderDLL = pHardwareConfig ? pHardwareConfig->GetShaderDLLName() : nullptr;
	s_bDX12 = shaderDLL && !V_stricmp( shaderDLL, "stdshader_dx12" );
	s_bMaterialsReady = false;
	s_nDispatchedFrame = -1;
}

void GTAODX12_CreateRenderTargets( IMaterialSystem *pMaterialSystem, IMaterialSystemHardwareConfig *pHardwareConfig )
{
	s_bRenderTargetsReady = false;
	s_bMaterialsReady = false;
	const char *shaderDLL = pHardwareConfig ? pHardwareConfig->GetShaderDLLName() : nullptr;
	if ( !pMaterialSystem || !shaderDLL || V_stricmp( shaderDLL, "stdshader_dx12" ) )
		return;
	s_GtaoDepth.Init( pMaterialSystem->CreateNamedRenderTargetTextureEx2( "_rt_GTAODepth", 1, 1, RT_SIZE_FULL_FRAME_BUFFER,
		IMAGE_FORMAT_R32F, MATERIAL_RT_DEPTH_NONE, TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT | TEXTUREFLAGS_POINTSAMPLE, 0 ) );
	s_GtaoViewDepth.Init( pMaterialSystem->CreateNamedRenderTargetTextureEx2( "_rt_GTAOViewDepth", 1, 1, RT_SIZE_FULL_FRAME_BUFFER,
		IMAGE_FORMAT_R32F, MATERIAL_RT_DEPTH_NONE, TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT | TEXTUREFLAGS_POINTSAMPLE, 0 ) );
	s_GtaoTerm0.Init( pMaterialSystem->CreateNamedRenderTargetTextureEx2( "_rt_GTAOTerm0", 1, 1, RT_SIZE_FULL_FRAME_BUFFER,
		IMAGE_FORMAT_I8, MATERIAL_RT_DEPTH_NONE, TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT | TEXTUREFLAGS_POINTSAMPLE, 0 ) );
	s_GtaoTerm1.Init( pMaterialSystem->CreateNamedRenderTargetTextureEx2( "_rt_GTAOTerm1", 1, 1, RT_SIZE_FULL_FRAME_BUFFER,
		IMAGE_FORMAT_I8, MATERIAL_RT_DEPTH_NONE, TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT | TEXTUREFLAGS_POINTSAMPLE, 0 ) );
	s_GtaoEdges.Init( pMaterialSystem->CreateNamedRenderTargetTextureEx2( "_rt_GTAOEdges", 1, 1, RT_SIZE_FULL_FRAME_BUFFER,
		IMAGE_FORMAT_I8, MATERIAL_RT_DEPTH_NONE, TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT | TEXTUREFLAGS_POINTSAMPLE, 0 ) );
	s_bRenderTargetsReady = s_GtaoDepth.IsValid() && s_GtaoViewDepth.IsValid() && s_GtaoTerm0.IsValid() && s_GtaoTerm1.IsValid() && s_GtaoEdges.IsValid();
	if ( !s_bRenderTargetsReady )
		return;

	s_Prefilter.m_pMaterial = CreateMaterial( pMaterialSystem, "dx12/gtao_prefilter", "DX12_GTAO", "$pass", 0 );
	s_ViewDepth.m_pMaterial = CreateMaterial( pMaterialSystem, "dx12/gtao_viewdepth", "DX12_GTAO", "$pass", 1 );
	s_Main.m_pMaterial = CreateMaterial( pMaterialSystem, "dx12/gtao_main", "DX12_GTAO", "$pass", 2 );
	for ( int i = 0; i < ARRAYSIZE( s_Denoise ); ++i )
		s_Denoise[i].m_pMaterial = CreateMaterial( pMaterialSystem, CFmtStr( "dx12/gtao_denoise_%d", i ), "DX12_GTAO", "$pass", 3 );
	s_Apply.m_pMaterial = CreateMaterial( pMaterialSystem, "dx12/gtao_apply", "DX12_GTAOApply", "$debugview", 0 );
	s_ApplyDebug.m_pMaterial = CreateMaterial( pMaterialSystem, "dx12/gtao_apply_debug", "DX12_GTAOApply", "$debugview", 1 );
	s_bMaterialsReady = MaterialValid( s_Prefilter.m_pMaterial ) && MaterialValid( s_ViewDepth.m_pMaterial ) && MaterialValid( s_Main.m_pMaterial ) &&
		MaterialValid( s_Denoise[0].m_pMaterial ) && MaterialValid( s_Denoise[1].m_pMaterial ) && MaterialValid( s_Denoise[2].m_pMaterial ) &&
		MaterialValid( s_Apply.m_pMaterial ) && MaterialValid( s_ApplyDebug.m_pMaterial );
}

void GTAODX12_Shutdown()
{
	ReleaseMaterial( s_Prefilter );
	ReleaseMaterial( s_ViewDepth );
	ReleaseMaterial( s_Main );
	for ( int i = 0; i < ARRAYSIZE( s_Denoise ); ++i )
		ReleaseMaterial( s_Denoise[i] );
	ReleaseMaterial( s_Apply );
	ReleaseMaterial( s_ApplyDebug );
	s_GtaoDepth.Shutdown();
	s_GtaoViewDepth.Shutdown();
	s_GtaoTerm0.Shutdown();
	s_GtaoTerm1.Shutdown();
	s_GtaoEdges.Shutdown();
	s_bMaterialsReady = false;
	s_bRenderTargetsReady = false;
	s_bDX12 = false;
	s_nDispatchedFrame = -1;
}

bool GTAODX12_Enabled()
{
	return s_bDX12 && r_gtao.GetBool() && s_bRenderTargetsReady && s_bMaterialsReady;
}

void GTAODX12_Dispatch( IMatRenderContext *pRenderContext )
{
	if ( !GTAODX12_Enabled() || s_nDispatchedFrame == gpGlobals->framecount )
		return;

	int viewportX, viewportY, fullWidth, fullHeight;
	pRenderContext->GetViewport( viewportX, viewportY, fullWidth, fullHeight );
	if ( fullWidth <= 0 || fullHeight <= 0 )
		return;
	VMatrix projection, view;
	pRenderContext->GetMatrix( MATERIAL_PROJECTION, &projection );
	pRenderContext->GetMatrix( MATERIAL_VIEW, &view );
	const float projectionDepth = projection[3][2];
	if ( projection[3][3] != 0.f || projectionDepth == 0.f || projection[0][0] == 0.f || projection[1][1] == 0.f )
		return;
	const float scale = Clamp( r_gtao_resolution.GetFloat(), 0.25f, 1.f );
	const int aoWidth = MAX( 1, static_cast<int>( ceilf( fullWidth * scale ) ) );
	const int aoHeight = MAX( 1, static_cast<int>( ceilf( fullHeight * scale ) ) );
	const int slices = Clamp( r_gtao_slices.GetInt(), 1, 9 );
	const int steps = Clamp( r_gtao_steps.GetInt(), 1, 8 );
	// A temporal upscaler accumulates an animated noise pattern on top of the spatial filter.
	const bool bTemporal = UpscalerDX12_Enabled() && UpscalerDX12_Status() > 0;
	const int denoisePasses = Clamp( r_gtao_denoise.GetInt(), 0, 3 );
	const int noiseIndex = bTemporal ? gpGlobals->framecount & 63 : 0;
	const float radius = Clamp( r_gtao_radius.GetFloat(), 1.f, 1024.f );
	const float power = Clamp( r_gtao_power.GetFloat(), 0.1f, 8.f );

	SetTexture( s_Prefilter.m_pMaterial, "$depthmips", s_GtaoDepth );
	SetTexture( s_ViewDepth.m_pMaterial, "$viewdepth", s_GtaoViewDepth );
	SetTexture( s_Main.m_pMaterial, "$depthmips", s_GtaoDepth );
	SetTexture( s_Main.m_pMaterial, "$aoout", s_GtaoTerm0 );
	SetTexture( s_Main.m_pMaterial, "$edges", s_GtaoEdges );
	for ( int i = 0; i < ARRAYSIZE( s_Denoise ); ++i )
	{
		SetTexture( s_Denoise[i].m_pMaterial, "$aoin", ( i & 1 ) ? s_GtaoTerm1 : s_GtaoTerm0 );
		SetTexture( s_Denoise[i].m_pMaterial, "$edges", s_GtaoEdges );
		SetTexture( s_Denoise[i].m_pMaterial, "$aoout", ( i & 1 ) ? s_GtaoTerm0 : s_GtaoTerm1 );
	}
	IMaterial *pApply = r_gtao_debug.GetBool() ? s_ApplyDebug.m_pMaterial : s_Apply.m_pMaterial;
	SetTexture( pApply, "$aoin", denoisePasses & 1 ? s_GtaoTerm1 : s_GtaoTerm0 );
	SetTexture( pApply, "$viewdepth", s_GtaoViewDepth );
	SetTexture( pApply, "$depthmips", s_GtaoDepth );

	SetGtaoConstants( s_Prefilter.m_pMaterial, aoWidth, aoHeight, fullWidth, fullHeight, projection, radius, power, noiseIndex, slices, steps, 0, viewportX, viewportY );
	SetGtaoConstants( s_ViewDepth.m_pMaterial, fullWidth, fullHeight, fullWidth, fullHeight, projection, radius, power, noiseIndex, slices, steps, 0, viewportX, viewportY );
	SetGtaoConstants( s_Main.m_pMaterial, aoWidth, aoHeight, fullWidth, fullHeight, projection, radius, power, noiseIndex, slices, steps, 0, viewportX, viewportY );
	for ( int i = 0; i < denoisePasses; ++i )
		SetGtaoConstants( s_Denoise[i].m_pMaterial, aoWidth, aoHeight, fullWidth, fullHeight, projection, radius, power, noiseIndex, slices, steps, i + 1 == denoisePasses, viewportX, viewportY );
	SetGtaoApplyConstants( pApply, aoWidth, aoHeight, fullWidth, fullHeight, denoisePasses == 0, viewportX, viewportY, view, projection );

	DrawCompute( pRenderContext, s_Prefilter.m_pMaterial );
	if ( scale < 1.f )
		DrawCompute( pRenderContext, s_ViewDepth.m_pMaterial );
	DrawCompute( pRenderContext, s_Main.m_pMaterial );
	for ( int i = 0; i < denoisePasses; ++i )
		DrawCompute( pRenderContext, s_Denoise[i].m_pMaterial );
	pRenderContext->DrawScreenSpaceRectangle( pApply, 0, 0, fullWidth, fullHeight, 0, 0, fullWidth - 1, fullHeight - 1, fullWidth, fullHeight );
	s_nDispatchedFrame = gpGlobals->framecount;
}

int GTAODX12_Status()
{
	return GTAODX12_Enabled() ? 1 : 0;
}
