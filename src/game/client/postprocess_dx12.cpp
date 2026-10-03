//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DX12 client-owned post-processing targets, materials and pass chain.
//
//===========================================================================//
#include "cbase.h"
#include "postprocess_dx12.h"
#include "upscaler_dx12.h"
#include "materialsystem/imaterialsystemhardwareconfig.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/imaterialvar.h"
#include "materialsystem/itexture.h"
#include "shaderapi/ishaderapidx12.h"
#include "tier1/interface.h"
#include "tier1/keyvalues.h"
#include "fmtstr.h"

#include <cmath>
// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace
{
struct PostMaterial_t
{
	IMaterial *m_pMaterial = nullptr;
};

// Froyok's bloom: eight levels, the first at half resolution. Level n lives in mip n + 1 of the bloom targets.
const int kBloomLevels = 8;

static bool s_bDX12 = false;
static bool s_bRenderTargetsReady = false;
static bool s_bMaterialsReady = false;
static CTextureReference s_PostScene;
static CTextureReference s_BloomDown;
static CTextureReference s_BloomUp;
static CTextureReference s_FlareInput;
static CTextureReference s_Flare;
static CTextureReference s_Glare;
static CTextureReference s_Composite;
static CTextureReference s_LensDirt;
static PostMaterial_t s_BloomDownsample[kBloomLevels];
static PostMaterial_t s_BloomUpsample[kBloomLevels - 1];
static PostMaterial_t s_FlareThreshold;
static PostMaterial_t s_FlareInputBlurDown;
static PostMaterial_t s_FlareInputBlurUp;
static PostMaterial_t s_FlareMaterial;
static PostMaterial_t s_FlareBlurDown;
static PostMaterial_t s_FlareBlurUp;
static PostMaterial_t s_GlareMaterial;
static PostMaterial_t s_CompositeMaterial;
static PostMaterial_t s_RcasMaterial;
static IShaderAPIDX12 *s_pShaderAPIDX12 = nullptr;

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

static void SetFloat( IMaterial *pMaterial, const char *pName, float value )
{
	if ( IMaterialVar *pVar = FindVar( pMaterial, pName ) )
		pVar->SetFloatValue( value );
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

static IMaterial *CreateMaterial( IMaterialSystem *pMaterialSystem, const char *pName, const char *pShader )
{
	return pMaterialSystem->CreateMaterial( pName, new KeyValues( pShader ) );
}

// Pass selection lives in the VMT key values so it survives material reloads.
static IMaterial *CreatePassMaterial( IMaterialSystem *pMaterialSystem, const char *pName, int nPass )
{
	KeyValues *pKeyValues = new KeyValues( "DX12_PostFX" );
	pKeyValues->SetInt( "$pass", nPass );
	return pMaterialSystem->CreateMaterial( pName, pKeyValues );
}

static void ReleaseMaterial( PostMaterial_t &material )
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

static int DisplayStatus()
{
	return s_pShaderAPIDX12 ? s_pShaderAPIDX12->HdrDisplayStatus() : 0;
}

static int DisplayNits( int status )
{
	return MAX( 80, SHADERAPIDX12_HDR_DISPLAY_MAX_NITS( status ) );
}

// Compute materials dispatch from their dynamic state and skip the raster draw; the rectangle only drives the pass.
static void DrawCompute( IMatRenderContext *pRenderContext, IMaterial *pMaterial )
{
	pRenderContext->DrawScreenSpaceRectangle( pMaterial, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1 );
}

// Binds one compute pass: source/destination mips plus the cbuffer sizes and per-pass parameters p0/p1.
static void SetPostPass( IMaterial *pMaterial, ITexture *pSource, int nSourceMip, ITexture *pDest, int nDestMip, float p0, float p1 )
{
	const int nSrcWidth = MAX( 1, pSource->GetActualWidth() >> nSourceMip );
	const int nSrcHeight = MAX( 1, pSource->GetActualHeight() >> nSourceMip );
	const int nDstWidth = MAX( 1, pDest->GetActualWidth() >> nDestMip );
	const int nDstHeight = MAX( 1, pDest->GetActualHeight() >> nDestMip );
	const float sizes[4] = { static_cast<float>( nSrcWidth ), static_cast<float>( nSrcHeight ), static_cast<float>( nDstWidth ), static_cast<float>( nDstHeight ) };
	const float params0[4] = { 1.f / nSrcWidth, 1.f / nSrcHeight, p0, p1 };
	SetTexture( pMaterial, "$srctexture", pSource );
	SetTexture( pMaterial, "$dsttexture", pDest );
	SetInt( pMaterial, "$srcmip", nSourceMip );
	SetInt( pMaterial, "$dstmip", nDestMip );
	SetVector4( pMaterial, "$sizes", sizes );
	SetVector4( pMaterial, "$params0", params0 );
}
}

static ConVar mat_postfx_dx12( "mat_postfx_dx12", "1", FCVAR_ARCHIVE, "DX12 post stack (HDR bloom, AgX, lens effects, LUT); 0 = legacy engine_post" );
static ConVar mat_postfx_exposure( "mat_postfx_exposure", "0", FCVAR_ARCHIVE, "EV bias applied before bloom/tonemap" );
static ConVar mat_postfx_bloom_intensity( "mat_postfx_bloom_intensity", "0.1", FCVAR_ARCHIVE, "DX12 post bloom: how far the scene is blended towards the bloom (0..1, keep under 0.3)" );
static ConVar mat_postfx_bloom_radius( "mat_postfx_bloom_radius", "0.85", FCVAR_ARCHIVE, "DX12 post bloom radius: blend of each smaller level over the next larger one (0..1)" );
static ConVar mat_postfx_lensdirt( "mat_postfx_lensdirt", "0.5", FCVAR_ARCHIVE, "DX12 post lens dirt intensity (raises the bloom blend where the dirt is)" );
static ConVar mat_postfx_lensflare( "mat_postfx_lensflare", "1.0", FCVAR_ARCHIVE, "DX12 post lens flare (ghosts, halo, glare) intensity" );
static ConVar mat_postfx_lensflare_threshold( "mat_postfx_lensflare_threshold", "1.0", FCVAR_ARCHIVE, "DX12 post lens flare: summed RGB where bright content starts to flare" );
static ConVar mat_postfx_lensflare_threshold_range( "mat_postfx_lensflare_threshold_range", "1.0", FCVAR_ARCHIVE, "DX12 post lens flare: summed RGB range over which flaring fades in" );
static ConVar mat_postfx_glare( "mat_postfx_glare", "0.02", FCVAR_ARCHIVE, "DX12 post lens flare glare (star) intensity" );
static ConVar mat_postfx_glare_divider( "mat_postfx_glare_divider", "60", FCVAR_ARCHIVE, "DX12 post lens flare glare: summed RGB at which a star reaches full length" );
static ConVar mat_postfx_sharpen( "mat_postfx_sharpen", "0.5", FCVAR_ARCHIVE, "DX12 post RCAS sharpness 0..1" );
static ConVar mat_postfx_debug( "mat_postfx_debug", "0", FCVAR_CHEAT, "DX12 post debug view: 0 off, 1 bloom, 2 flare, 3 glare, 4 dirt mask" );
static ConVar mat_hdr_output( "mat_hdr_output", "0", FCVAR_ARCHIVE, "1 = HDR display output when the display reports HDR" );
static ConVar mat_hdr_output_max_nits( "mat_hdr_output_max_nits", "0", FCVAR_ARCHIVE, "0 = detect display max nits on first HDR activation" );
static ConVar mat_hdr_output_world_nits( "mat_hdr_output_world_nits", "200", FCVAR_ARCHIVE, "HDR scene paper-white nits" );
static ConVar mat_hdr_output_ui_nits( "mat_hdr_output_ui_nits", "200", FCVAR_ARCHIVE, "HDR UI paper-white nits" );

static void DetectHdrDisplay( const CCommand & )
{
	const int status = DisplayStatus();
	mat_hdr_output_max_nits.SetValue( DisplayNits( status ) );
	Msg( "HDR display: %s, max %d nits\n", SHADERAPIDX12_HDR_DISPLAY_TYPE( status ) == SHADERAPIDX12_HDR_DISPLAY_HDR ? "HDR" : "SDR", DisplayNits( status ) );
}
static ConCommand mat_hdr_output_detect( "mat_hdr_output_detect", DetectHdrDisplay, "Re-read DX12 display HDR status and update max nits", FCVAR_NONE );

void PostProcessDX12_Init( IMaterialSystemHardwareConfig *pHardwareConfig )
{
	const char *shaderDLL = pHardwareConfig ? pHardwareConfig->GetShaderDLLName() : nullptr;
	s_bDX12 = shaderDLL && !V_stricmp( shaderDLL, "stdshader_dx12" );
	s_bRenderTargetsReady = false;
	s_bMaterialsReady = false;
	s_pShaderAPIDX12 = nullptr;
	if ( !s_bDX12 )
		return;
	CreateInterfaceFn factory = Sys_GetFactory( "shaderapidx12" );
	s_pShaderAPIDX12 = factory ? static_cast<IShaderAPIDX12 *>( factory( SHADERAPIDX12_INTERFACE_VERSION, nullptr ) ) : nullptr;
}

void PostProcessDX12_CreateRenderTargets( IMaterialSystem *pMaterialSystem, IMaterialSystemHardwareConfig *pHardwareConfig )
{
	s_bRenderTargetsReady = false;
	s_bMaterialsReady = false;
	const char *shaderDLL = pHardwareConfig ? pHardwareConfig->GetShaderDLLName() : nullptr;
	if ( !pMaterialSystem || !shaderDLL || V_stricmp( shaderDLL, "stdshader_dx12" ) )
		return;
	s_PostScene.Init( pMaterialSystem->CreateNamedRenderTargetTextureEx2( "_rt_PostFXScene", 1, 1, RT_SIZE_FULL_FRAME_BUFFER,
		IMAGE_FORMAT_RGBA16161616F, MATERIAL_RT_DEPTH_NONE, TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT | TEXTUREFLAGS_POINTSAMPLE, 0 ) );
	s_BloomDown.Init( pMaterialSystem->CreateNamedRenderTargetTextureEx2( "_rt_PostFXBloomDown", 1, 1, RT_SIZE_FULL_FRAME_BUFFER,
		IMAGE_FORMAT_RGBA16161616F, MATERIAL_RT_DEPTH_NONE, TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT, 0 ) );
	s_BloomUp.Init( pMaterialSystem->CreateNamedRenderTargetTextureEx2( "_rt_PostFXBloomUp", 1, 1, RT_SIZE_FULL_FRAME_BUFFER,
		IMAGE_FORMAT_RGBA16161616F, MATERIAL_RT_DEPTH_NONE, TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT, 0 ) );
	s_FlareInput.Init( pMaterialSystem->CreateNamedRenderTargetTextureEx2( "_rt_PostFXFlareInput", 1, 1, RT_SIZE_HDR,
		IMAGE_FORMAT_RGBA16161616F, MATERIAL_RT_DEPTH_NONE, TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT, 0 ) );
	s_Flare.Init( pMaterialSystem->CreateNamedRenderTargetTextureEx2( "_rt_PostFXFlare", 1, 1, RT_SIZE_HDR,
		IMAGE_FORMAT_RGBA16161616F, MATERIAL_RT_DEPTH_NONE, TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT, 0 ) );
	s_Glare.Init( pMaterialSystem->CreateNamedRenderTargetTextureEx2( "_rt_PostFXGlare", 1, 1, RT_SIZE_HDR,
		IMAGE_FORMAT_RGBA16161616F, MATERIAL_RT_DEPTH_NONE, TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT, 0 ) );
	s_Composite.Init( pMaterialSystem->CreateNamedRenderTargetTextureEx2( "_rt_PostFXComposite", 1, 1, RT_SIZE_FULL_FRAME_BUFFER,
		IMAGE_FORMAT_RGBA16161616F, MATERIAL_RT_DEPTH_NONE, TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT, 0 ) );
	s_LensDirt.Init( pMaterialSystem->FindTexture( "effects/lensdirt", TEXTURE_GROUP_OTHER, true ) );
	if ( s_LensDirt.IsValid() && s_LensDirt->IsError() )
		s_LensDirt.Shutdown();
	s_bRenderTargetsReady = s_PostScene.IsValid() && s_BloomDown.IsValid() && s_BloomUp.IsValid() && s_FlareInput.IsValid() && s_Flare.IsValid() && s_Glare.IsValid() &&
		s_Composite.IsValid();
	if ( !s_bRenderTargetsReady )
		return;

	for ( int i = 0; i < ARRAYSIZE( s_BloomDownsample ); ++i )
		s_BloomDownsample[i].m_pMaterial = CreatePassMaterial( pMaterialSystem, CFmtStr( "dx12/postfx_downsample_%d", i ), 0 );
	for ( int i = 0; i < ARRAYSIZE( s_BloomUpsample ); ++i )
		s_BloomUpsample[i].m_pMaterial = CreatePassMaterial( pMaterialSystem, CFmtStr( "dx12/postfx_upsample_%d", i ), 1 );
	s_FlareThreshold.m_pMaterial = CreatePassMaterial( pMaterialSystem, "dx12/postfx_flare_threshold", 2 );
	s_FlareInputBlurDown.m_pMaterial = CreatePassMaterial( pMaterialSystem, "dx12/postfx_flare_input_blur_down", 4 );
	s_FlareInputBlurUp.m_pMaterial = CreatePassMaterial( pMaterialSystem, "dx12/postfx_flare_input_blur_up", 5 );
	s_FlareMaterial.m_pMaterial = CreatePassMaterial( pMaterialSystem, "dx12/postfx_flare", 3 );
	s_FlareBlurDown.m_pMaterial = CreatePassMaterial( pMaterialSystem, "dx12/postfx_flare_blur_down", 4 );
	s_FlareBlurUp.m_pMaterial = CreatePassMaterial( pMaterialSystem, "dx12/postfx_flare_blur_up", 5 );
	s_GlareMaterial.m_pMaterial = CreatePassMaterial( pMaterialSystem, "dx12/postfx_glare", 6 );
	s_CompositeMaterial.m_pMaterial = CreateMaterial( pMaterialSystem, "dx12/postfx_composite", "DX12_PostFXComposite" );
	s_RcasMaterial.m_pMaterial = CreateMaterial( pMaterialSystem, "dx12/postfx_rcas", "DX12_PostFXRcas" );
	s_bMaterialsReady = MaterialValid( s_FlareThreshold.m_pMaterial ) && MaterialValid( s_FlareInputBlurDown.m_pMaterial ) && MaterialValid( s_FlareInputBlurUp.m_pMaterial ) &&
		MaterialValid( s_FlareMaterial.m_pMaterial ) && MaterialValid( s_FlareBlurDown.m_pMaterial ) && MaterialValid( s_FlareBlurUp.m_pMaterial ) &&
		MaterialValid( s_GlareMaterial.m_pMaterial ) && MaterialValid( s_CompositeMaterial.m_pMaterial ) && MaterialValid( s_RcasMaterial.m_pMaterial );
	for ( int i = 0; i < ARRAYSIZE( s_BloomDownsample ); ++i )
		s_bMaterialsReady = s_bMaterialsReady && MaterialValid( s_BloomDownsample[i].m_pMaterial );
	for ( int i = 0; i < ARRAYSIZE( s_BloomUpsample ); ++i )
		s_bMaterialsReady = s_bMaterialsReady && MaterialValid( s_BloomUpsample[i].m_pMaterial );
}

void PostProcessDX12_Shutdown()
{
	for ( int i = 0; i < ARRAYSIZE( s_BloomDownsample ); ++i )
		ReleaseMaterial( s_BloomDownsample[i] );
	for ( int i = 0; i < ARRAYSIZE( s_BloomUpsample ); ++i )
		ReleaseMaterial( s_BloomUpsample[i] );
	ReleaseMaterial( s_FlareThreshold );
	ReleaseMaterial( s_FlareInputBlurDown );
	ReleaseMaterial( s_FlareInputBlurUp );
	ReleaseMaterial( s_FlareMaterial );
	ReleaseMaterial( s_FlareBlurDown );
	ReleaseMaterial( s_FlareBlurUp );
	ReleaseMaterial( s_GlareMaterial );
	ReleaseMaterial( s_CompositeMaterial );
	ReleaseMaterial( s_RcasMaterial );
	s_PostScene.Shutdown();
	s_BloomDown.Shutdown();
	s_BloomUp.Shutdown();
	s_FlareInput.Shutdown();
	s_Flare.Shutdown();
	s_Glare.Shutdown();
	s_Composite.Shutdown();
	s_LensDirt.Shutdown();
	s_pShaderAPIDX12 = nullptr;
	s_bMaterialsReady = false;
	s_bRenderTargetsReady = false;
	s_bDX12 = false;
}

bool PostProcessDX12_Active()
{
	return s_bDX12 && mat_postfx_dx12.GetBool() && s_bRenderTargetsReady && s_bMaterialsReady && s_pShaderAPIDX12;
}

// Per-frame HDR output settings; a plain store in the renderer, so no render context is involved.
void PostProcessDX12_SubmitFrameConfig()
{
	if ( !s_pShaderAPIDX12 )
		return;
	const int status = DisplayStatus();
	const bool bHdr = PostProcessDX12_Active() && mat_hdr_output.GetBool() && SHADERAPIDX12_HDR_DISPLAY_TYPE( status ) == SHADERAPIDX12_HDR_DISPLAY_HDR && s_pShaderAPIDX12->HdrOutputCapable();
	if ( bHdr && mat_hdr_output_max_nits.GetInt() <= 0 )
	{
		const int nits = DisplayNits( status );
		mat_hdr_output_max_nits.SetValue( nits );
		Msg( "mat_hdr_output: detected display max %d nits\n", nits );
	}
	s_pShaderAPIDX12->SetHdrOutput( bHdr, MAX( mat_hdr_output_ui_nits.GetFloat(), 1.f ) );
}

void PostProcessDX12_Render( IMatRenderContext *pRenderContext, int x, int y, int w, int h, float flBloomScale, bool bColorCorrection )
{
	if ( !PostProcessDX12_Active() || w <= 0 || h <= 0 )
		return;

	// Every pass works on whole render targets; the view rectangle only selects what is copied in and drawn out.
	Rect_t area = { x, y, w, h };
	pRenderContext->CopyRenderTargetToTextureEx( s_PostScene, 0, &area, &area );
	const float flExposure = exp2f( mat_postfx_exposure.GetFloat() );

	// Bloom (Froyok): no threshold, the whole frame is downsampled into mips 1..8, then each level is upsampled and
	// lerped over the next larger one by the radius, from mip 8 back to mip 1.
	for ( int i = 0; i < ARRAYSIZE( s_BloomDownsample ); ++i )
	{
		IMaterial *pMaterial = s_BloomDownsample[i].m_pMaterial;
		if ( i == 0 )
			SetPostPass( pMaterial, s_PostScene, 0, s_BloomDown, 1, flExposure, 0.f );
		else
			SetPostPass( pMaterial, s_BloomDown, i, s_BloomDown, i + 1, 1.f, 0.f );
		DrawCompute( pRenderContext, pMaterial );
	}
	for ( int i = 0; i < ARRAYSIZE( s_BloomUpsample ); ++i )
	{
		const int nMip = ARRAYSIZE( s_BloomUpsample ) - i;
		IMaterial *pMaterial = s_BloomUpsample[i].m_pMaterial;
		SetPostPass( pMaterial, s_BloomDown, nMip, s_BloomUp, nMip, Clamp( mat_postfx_bloom_radius.GetFloat(), 0.f, 1.f ), 0.f );
		SetTexture( pMaterial, "$srctexture2", i == 0 ? s_BloomDown : s_BloomUp );
		SetInt( pMaterial, "$srcmip2", nMip + 1 );
		DrawCompute( pRenderContext, pMaterial );
	}

	// Lens flare (Froyok): the half-resolution bloom level is thresholded into a quarter-resolution input and
	// stabilised by one dual Kawase step through its mip 1. Ghosts and halo are built from it and softened by another
	// dual Kawase step; the glare star gathers from the input's eighth-resolution mip 1.
	SetPostPass( s_FlareThreshold.m_pMaterial, s_BloomDown, 1, s_FlareInput, 0, mat_postfx_lensflare_threshold.GetFloat(),
		MAX( mat_postfx_lensflare_threshold_range.GetFloat(), 0.01f ) );
	DrawCompute( pRenderContext, s_FlareThreshold.m_pMaterial );
	SetPostPass( s_FlareInputBlurDown.m_pMaterial, s_FlareInput, 0, s_FlareInput, 1, 0.f, 0.f );
	DrawCompute( pRenderContext, s_FlareInputBlurDown.m_pMaterial );
	SetPostPass( s_FlareInputBlurUp.m_pMaterial, s_FlareInput, 1, s_FlareInput, 0, 0.f, 0.f );
	DrawCompute( pRenderContext, s_FlareInputBlurUp.m_pMaterial );
	SetPostPass( s_FlareMaterial.m_pMaterial, s_FlareInput, 0, s_Flare, 0, 0.f, 0.f );
	DrawCompute( pRenderContext, s_FlareMaterial.m_pMaterial );
	SetPostPass( s_FlareBlurDown.m_pMaterial, s_Flare, 0, s_Flare, 1, 0.f, 0.f );
	DrawCompute( pRenderContext, s_FlareBlurDown.m_pMaterial );
	SetPostPass( s_FlareBlurUp.m_pMaterial, s_Flare, 1, s_Flare, 0, 0.f, 0.f );
	DrawCompute( pRenderContext, s_FlareBlurUp.m_pMaterial );
	SetPostPass( s_GlareMaterial.m_pMaterial, s_FlareInput, 1, s_Glare, 0, mat_postfx_glare.GetFloat(), MAX( mat_postfx_glare_divider.GetFloat(), 0.01f ) );
	DrawCompute( pRenderContext, s_GlareMaterial.m_pMaterial );

	const int nStatus = DisplayStatus();
	const bool bHdr = mat_hdr_output.GetBool() && SHADERAPIDX12_HDR_DISPLAY_TYPE( nStatus ) == SHADERAPIDX12_HDR_DISPLAY_HDR && s_pShaderAPIDX12->HdrOutputCapable();
	const float flWorldNits = MAX( mat_hdr_output_world_nits.GetFloat(), 1.f );
	const float flUiNits = MAX( mat_hdr_output_ui_nits.GetFloat(), 1.f );
	const float flPeak = bHdr ? MAX( mat_hdr_output_max_nits.GetFloat(), flWorldNits ) / flWorldNits : 1.f;
	const float flOutScale = bHdr ? flWorldNits / flUiNits : 1.f;
	const bool bDirt = s_LensDirt.IsValid() && mat_postfx_lensdirt.GetFloat() > 0.f;
	const float composite0[4] = { flExposure, mat_postfx_bloom_intensity.GetFloat() * flBloomScale, mat_postfx_lensflare.GetFloat(), 0.f };
	const float composite1[4] = { mat_postfx_lensdirt.GetFloat(), 0.f, 0.f, 0.f };
	const float output[4] = { static_cast<float>( bHdr ? 1 : 0 ), 0.f, 0.f, 0.f };
	IMaterial *pComposite = s_CompositeMaterial.m_pMaterial;
	SetTexture( pComposite, "$scene", s_PostScene );
	SetTexture( pComposite, "$bloom", s_BloomUp );
	SetTexture( pComposite, "$flare", s_Flare );
	SetTexture( pComposite, "$glare", s_Glare );
	SetTexture( pComposite, "$dirt", s_LensDirt );
	SetInt( pComposite, "$dirtenabled", bDirt );
	SetFloat( pComposite, "$outscale", flOutScale );
	SetFloat( pComposite, "$peak", flPeak );
	SetInt( pComposite, "$debugview", mat_postfx_debug.GetInt() & 0xf );
	SetVector4( pComposite, "$params0", composite0 );
	SetVector4( pComposite, "$params1", composite1 );
	SetVector4( pComposite, "$output", output );

	// RCAS sharpens the temporal upscaler's output, so it only runs on frames the upscaler dispatched.
	const bool bSharpen = UpscalerDX12_Enabled() && UpscalerDX12_Status() > 0 && mat_postfx_sharpen.GetFloat() > 0.f;
	const int nTextureWidth = s_PostScene->GetActualWidth();
	const int nTextureHeight = s_PostScene->GetActualHeight();
	pRenderContext->EnableColorCorrection( bColorCorrection );
	if ( bSharpen )
	{
		pRenderContext->PushRenderTargetAndViewport( s_Composite );
		pRenderContext->DrawScreenSpaceRectangle( pComposite, x, y, w, h, x, y, x + w - 1, y + h - 1, nTextureWidth, nTextureHeight );
		pRenderContext->PopRenderTargetAndViewport();
		const float texelSize[4] = { 1.f / nTextureWidth, 1.f / nTextureHeight, 0.f, 0.f };
		IMaterial *pRcas = s_RcasMaterial.m_pMaterial;
		SetTexture( pRcas, "$basetexture", s_Composite );
		SetFloat( pRcas, "$sharpness", Clamp( mat_postfx_sharpen.GetFloat(), 0.f, 1.f ) );
		SetFloat( pRcas, "$normscale", flPeak * flOutScale );
		SetVector4( pRcas, "$texelsize", texelSize );
		pRenderContext->SetRenderTarget( nullptr );
		pRenderContext->DrawScreenSpaceRectangle( pRcas, x, y, w, h, x, y, x + w - 1, y + h - 1, nTextureWidth, nTextureHeight );
	}
	else
	{
		pRenderContext->SetRenderTarget( nullptr );
		pRenderContext->DrawScreenSpaceRectangle( pComposite, x, y, w, h, x, y, x + w - 1, y + h - 1, nTextureWidth, nTextureHeight );
	}
}

int PostProcessDX12_Status()
{
	return PostProcessDX12_Active() ? 1 : 0;
}
