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
#include "materialsystem/MaterialSystemUtil.h"
#include "materialsystem/itexture.h"
#include "shaderapi/ishaderapidx12.h"
#include "tier1/interface.h"
#include "tier1/keyvalues.h"
#include "fmtstr.h"
#include "renderparm.h"

#include <cmath>
// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace
{
enum PostPassVar_t
{
	POST_SRC_TEXTURE,
	POST_DST_TEXTURE,
	POST_SRC_MIP,
	POST_DST_MIP,
	POST_SIZES,
	POST_PARAMS0,
	POST_PARAMS1,
	POST_SRC_TEXTURE2,
	POST_SRC_MIP2,
	POST_DST_TEXTURE2,
	POST_DST_MIP2,
	POST_VAR_COUNT
};

static const char *const s_PostVarNames[POST_VAR_COUNT] =
{
	"$srctexture", "$dsttexture", "$srcmip", "$dstmip", "$sizes", "$params0", "$params1",
	"$srctexture2", "$srcmip2", "$dsttexture2", "$dstmip2"
};

enum CompositeVar_t
{
	COMPOSITE_SCENE,
	COMPOSITE_BLOOM,
	COMPOSITE_FLARE,
	COMPOSITE_GLARE,
	COMPOSITE_DIRT,
	COMPOSITE_BLOOM_ENABLED,
	COMPOSITE_FLARE_ENABLED,
	COMPOSITE_GLARE_ENABLED,
	COMPOSITE_DIRT_ENABLED,
	COMPOSITE_OUT_SCALE,
	COMPOSITE_PEAK,
	COMPOSITE_DEBUG_VIEW,
	COMPOSITE_PARAMS0,
	COMPOSITE_PARAMS1,
	COMPOSITE_OUTPUT,
	COMPOSITE_VAR_COUNT
};

static const char *const s_CompositeVarNames[COMPOSITE_VAR_COUNT] =
{
	"$scene", "$bloom", "$flare", "$glare", "$dirt", "$bloomenabled", "$flareenabled", "$glareenabled",
	"$dirtenabled", "$outscale", "$peak", "$debugview", "$params0", "$params1", "$output"
};

enum RcasVar_t
{
	RCAS_BASE_TEXTURE,
	RCAS_SHARPNESS,
	RCAS_NORM_SCALE,
	RCAS_TEXEL_SIZE,
	RCAS_VAR_COUNT
};

static const char *const s_RcasVarNames[RCAS_VAR_COUNT] =
{
	"$basetexture", "$sharpness", "$normscale", "$texelsize"
};

struct PostMaterial_t
{
	CMaterialReference m_Material;
	const char *const *m_pVarNames = nullptr;
	unsigned int m_nVarTokens[COMPOSITE_VAR_COUNT] = {};
};

// MagicBloom's layered bloom on Froyok's pyramid: eight levels, the first at half resolution. Level n lives in mip n + 1
// of the bloom targets.
const int kBloomLevels = 8;
// Max reduction writes 16x16 component bounds, then their global maximum to mip 4.
const int kGlareBoundsSize = 16;
const int kGlareBoundsMip = 4;

static bool s_bDX12 = false;
static bool s_bRenderTargetsReady = false;
static bool s_bMaterialsReady = false;
static CTextureReference s_PostScene;
static CTextureReference s_BloomDown;
static CTextureReference s_BloomUp;
static CTextureReference s_FlareInput;
static CTextureReference s_Flare;
static CTextureReference s_Glare;
static CTextureReference s_GlareBounds;
static CTextureReference s_Composite;
static CTextureReference s_LensDirt;
static PostMaterial_t s_BloomFirst;
static PostMaterial_t s_BloomDownsample[kBloomLevels - 1];
static PostMaterial_t s_BloomUpsample[kBloomLevels - 1];
static PostMaterial_t s_FlareThreshold;
static PostMaterial_t s_FlareInputBlurDown;
static PostMaterial_t s_FlareInputBlurUp;
static PostMaterial_t s_FlareMaterial;
static PostMaterial_t s_FlareBlurDown;
static PostMaterial_t s_FlareBlurUp;
static PostMaterial_t s_GlareMaterial;
static PostMaterial_t s_GlareReduce;
static PostMaterial_t s_CompositeMaterial;
static PostMaterial_t s_RcasMaterial;
static IShaderAPIDX12 *s_pShaderAPIDX12 = nullptr;

static IMaterialVar *GetPostVar( PostMaterial_t &material, int nVar )
{
	Assert( material.m_Material.IsValid() );
	Assert( nVar >= 0 && nVar < ARRAYSIZE( material.m_nVarTokens ) );
	Assert( material.m_nVarTokens[nVar] != 0 );
	// Keep the token, not the IMaterialVar*: FindVarFast revalidates its index after a material reload.
	IMaterialVar *pVar = material.m_Material->FindVarFast( material.m_pVarNames[nVar], &material.m_nVarTokens[nVar] );
	AssertMsg1( pVar, "Missing required DX12 post-processing variable %s", material.m_pVarNames[nVar] );
	return pVar;
}

static void SetInt( PostMaterial_t &material, int nVar, int value )
{
	GetPostVar( material, nVar )->SetIntValue( value );
}

static void SetFloat( PostMaterial_t &material, int nVar, float value )
{
	GetPostVar( material, nVar )->SetFloatValue( value );
}

static void SetVector4( PostMaterial_t &material, int nVar, const float *value )
{
	GetPostVar( material, nVar )->SetVecValue( value, 4 );
}

static void SetTexture( PostMaterial_t &material, int nVar, ITexture *pTexture )
{
	GetPostVar( material, nVar )->SetTextureValue( pTexture );
}

static bool MaterialValid( PostMaterial_t &material )
{
	return material.m_Material.IsValid() && !material.m_Material->IsErrorMaterial();
}

static void CreateMaterial( PostMaterial_t &material, IMaterialSystem *pMaterialSystem, const char *pName,
	KeyValues *pKeyValues, const char *const *pVarNames, int nVarCount )
{
	IMaterial *pMaterial = pMaterialSystem->CreateMaterial( pName, pKeyValues );
	material.m_Material.Init( pMaterial );
	// CreateMaterial returns one reference; CMaterialReference now owns the retained reference.
	if ( pMaterial )
		pMaterial->DecrementReferenceCount();
	material.m_pVarNames = pVarNames;
	if ( !MaterialValid( material ) )
	{
		Warning( "DX12 post: material %s failed to load\n", pName );
		return;
	}

	Assert( nVarCount <= ARRAYSIZE( material.m_nVarTokens ) );
	// Render-target initialization is main-side. Prime every shader parameter before queuing any pass,
	// including parameters used only by disabled bloom/lens/sharpen branches.
	for ( int i = 0; i < nVarCount; ++i )
	{
		IMaterialVar *pVar = material.m_Material->FindVarFast( pVarNames[i], &material.m_nVarTokens[i] );
		AssertMsg1( pVar, "Missing required DX12 post-processing variable %s", pVarNames[i] );
		(void)pVar;
	}
}

// Pass selection lives in the VMT key values so it survives material reloads.
static void CreatePassMaterial( PostMaterial_t &material, IMaterialSystem *pMaterialSystem, const char *pName, int nPass )
{
	KeyValues *pKeyValues = new KeyValues( "DX12_PostFX" );
	pKeyValues->SetInt( "$pass", nPass );
	CreateMaterial( material, pMaterialSystem, pName, pKeyValues, s_PostVarNames, ARRAYSIZE( s_PostVarNames ) );
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
static void DrawCompute( IMatRenderContext *pRenderContext, PostMaterial_t &material )
{
	pRenderContext->DrawScreenSpaceRectangle( material.m_Material, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1 );
}

// Binds one compute pass: source/destination mips plus the cbuffer sizes and per-pass parameters p0/p1/p2.
static void SetPostPass( PostMaterial_t &material, ITexture *pSource, int nSourceMip, ITexture *pDest, int nDestMip, float p0, float p1, int nTexelScale = 1, float p2 = 0.f )
{
	const int nSrcWidth = MAX( 1, pSource->GetActualWidth() >> nSourceMip );
	const int nSrcHeight = MAX( 1, pSource->GetActualHeight() >> nSourceMip );
	const int nDstWidth = MAX( 1, pDest->GetActualWidth() >> nDestMip );
	const int nDstHeight = MAX( 1, pDest->GetActualHeight() >> nDestMip );
	const float sizes[4] = { static_cast<float>( nSrcWidth ), static_cast<float>( nSrcHeight ), static_cast<float>( nDstWidth ), static_cast<float>( nDstHeight ) };
	const float flScale = static_cast<float>( MAX( nTexelScale, 1 ) );
	const float params0[4] = { flScale / nSrcWidth, flScale / nSrcHeight, p0, p1 };
	const float params1[4] = { p2, 0.f, 0.f, 0.f };
	SetTexture( material, POST_SRC_TEXTURE, pSource );
	SetTexture( material, POST_DST_TEXTURE, pDest );
	SetInt( material, POST_SRC_MIP, nSourceMip );
	SetInt( material, POST_DST_MIP, nDestMip );
	SetVector4( material, POST_SIZES, sizes );
	SetVector4( material, POST_PARAMS0, params0 );
	SetVector4( material, POST_PARAMS1, params1 );
}
}

static ConVar mat_postfx_dx12( "mat_postfx_dx12", "1", FCVAR_ARCHIVE, "DX12 post stack (HDR bloom, AgX, lens effects, LUT); 0 = legacy engine_post" );
// A config.cfg saved before this flag existed may carry mat_postfx_dx12 0 that no option of the current build wrote
// (a GTX 1070 perftest config did). PostProcessDX12_ValidateConfig turns the stack back on once for such a config;
// afterwards the archived mat_postfx_dx12 is the user's choice.
static ConVar mat_postfx_dx12_set( "mat_postfx_dx12_set", "0", FCVAR_ARCHIVE | FCVAR_HIDDEN, "Set once the DX12 post stack was validated for this config" );
static ConVar mat_postfx_exposure( "mat_postfx_exposure", "0", FCVAR_ARCHIVE, "EV bias applied before bloom/tonemap" );
static ConVar mat_postfx_bloom_intensity( "mat_postfx_bloom_intensity", "100", FCVAR_ARCHIVE, "DX12 post bloom exposure: the bloom layer average is multiplied by this before its filmic tonemap (MagicBloom's 100; 0 off, 25..400 useful)" );
static ConVar mat_postfx_bloom_threshold( "mat_postfx_bloom_threshold", "1.25", FCVAR_ARCHIVE, "DX12 post bloom threshold: scene luma above which pixels feed the bloom, weighted by luma - threshold (MagicBloom uses 1)", true, 0.f, true, 10.f );
static ConVar mat_postfx_bloom_boost( "mat_postfx_bloom_boost", "0.015", FCVAR_ARCHIVE, "DX12 post bloom extraction boost: maximum weight (luma - threshold) multiplying an extracted bright pixel (MagicBloom uses 10)", true, 0.f, true, 100.f );
static ConVar mat_postfx_bloom_quality( "mat_postfx_bloom_quality", "2", FCVAR_ARCHIVE, "DX12 post bloom quality: 0 low (drops the two finest levels), 1 medium (drops the half-resolution level), 2 high (8 levels)", true, 0.f, true, 2.f );
static ConVar mat_postfx_lensdirt( "mat_postfx_lensdirt", "0.5", FCVAR_ARCHIVE, "DX12 post lens dirt intensity (screens the dirt into the bloom)" );
static ConVar mat_postfx_lensdirt_enable( "mat_postfx_lensdirt_enable", "1", FCVAR_ARCHIVE, "DX12 post lens dirt: 0 off, 1 on. Intensity stays mat_postfx_lensdirt.", true, 0.f, true, 1.f );
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

// The composite's AgX peak relative to the scene's paper white.
static float HdrPeak( bool bHdr )
{
	const float flWorldNits = MAX( mat_hdr_output_world_nits.GetFloat(), 1.f );
	return bHdr ? MAX( mat_hdr_output_max_nits.GetFloat(), flWorldNits ) / flWorldNits : 1.f;
}

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
	s_GlareBounds.Init( pMaterialSystem->CreateNamedRenderTargetTextureEx2( "_rt_PostFXGlareBounds", kGlareBoundsSize, kGlareBoundsSize, RT_SIZE_LITERAL,
		IMAGE_FORMAT_RGBA16161616F, MATERIAL_RT_DEPTH_NONE, TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT | TEXTUREFLAGS_POINTSAMPLE, 0 ) );
	s_Composite.Init( pMaterialSystem->CreateNamedRenderTargetTextureEx2( "_rt_PostFXComposite", 1, 1, RT_SIZE_FULL_FRAME_BUFFER,
		IMAGE_FORMAT_RGBA16161616F, MATERIAL_RT_DEPTH_NONE, TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT, 0 ) );
	s_LensDirt.Init( pMaterialSystem->FindTexture( "effects/lensdirt", TEXTURE_GROUP_OTHER, true ) );
	if ( s_LensDirt.IsValid() && s_LensDirt->IsError() )
		s_LensDirt.Shutdown();
	s_bRenderTargetsReady = s_PostScene.IsValid() && s_BloomDown.IsValid() && s_BloomUp.IsValid() && s_FlareInput.IsValid() && s_Flare.IsValid() && s_Glare.IsValid() &&
		s_GlareBounds.IsValid() && s_Composite.IsValid();
	if ( !s_bRenderTargetsReady )
	{
		Warning( "DX12 post: render targets unavailable; the legacy engine post runs instead\n" );
		return;
	}

	CreatePassMaterial( s_BloomFirst, pMaterialSystem, "dx12/postfx_bloom_first", 7 );
	for ( int i = 0; i < ARRAYSIZE( s_BloomDownsample ); ++i )
		CreatePassMaterial( s_BloomDownsample[i], pMaterialSystem, CFmtStr( "dx12/postfx_downsample_%d", i ), 0 );
	for ( int i = 0; i < ARRAYSIZE( s_BloomUpsample ); ++i )
		CreatePassMaterial( s_BloomUpsample[i], pMaterialSystem, CFmtStr( "dx12/postfx_upsample_%d", i ), 1 );
	CreatePassMaterial( s_FlareThreshold, pMaterialSystem, "dx12/postfx_flare_threshold", 2 );
	CreatePassMaterial( s_FlareInputBlurDown, pMaterialSystem, "dx12/postfx_flare_input_blur_down", 4 );
	CreatePassMaterial( s_FlareInputBlurUp, pMaterialSystem, "dx12/postfx_flare_input_blur_up", 5 );
	CreatePassMaterial( s_FlareMaterial, pMaterialSystem, "dx12/postfx_flare", 3 );
	CreatePassMaterial( s_FlareBlurDown, pMaterialSystem, "dx12/postfx_flare_blur_down", 4 );
	CreatePassMaterial( s_FlareBlurUp, pMaterialSystem, "dx12/postfx_flare_blur_up", 5 );
	CreatePassMaterial( s_GlareMaterial, pMaterialSystem, "dx12/postfx_glare", 6 );
	CreatePassMaterial( s_GlareReduce, pMaterialSystem, "dx12/postfx_glare_reduce", 8 );
	CreateMaterial( s_CompositeMaterial, pMaterialSystem, "dx12/postfx_composite", new KeyValues( "DX12_PostFXComposite" ),
		s_CompositeVarNames, ARRAYSIZE( s_CompositeVarNames ) );
	CreateMaterial( s_RcasMaterial, pMaterialSystem, "dx12/postfx_rcas", new KeyValues( "DX12_PostFXRcas" ),
		s_RcasVarNames, ARRAYSIZE( s_RcasVarNames ) );
	s_bMaterialsReady = MaterialValid( s_FlareThreshold ) && MaterialValid( s_FlareInputBlurDown ) && MaterialValid( s_FlareInputBlurUp ) &&
		MaterialValid( s_FlareMaterial ) && MaterialValid( s_FlareBlurDown ) && MaterialValid( s_FlareBlurUp ) &&
		MaterialValid( s_GlareMaterial ) && MaterialValid( s_CompositeMaterial ) && MaterialValid( s_RcasMaterial ) &&
		MaterialValid( s_BloomFirst ) && MaterialValid( s_GlareReduce );
	for ( int i = 0; i < ARRAYSIZE( s_BloomDownsample ); ++i )
		s_bMaterialsReady = s_bMaterialsReady && MaterialValid( s_BloomDownsample[i] );
	for ( int i = 0; i < ARRAYSIZE( s_BloomUpsample ); ++i )
		s_bMaterialsReady = s_bMaterialsReady && MaterialValid( s_BloomUpsample[i] );
	if ( !s_bMaterialsReady )
		Warning( "DX12 post: materials unavailable; the legacy engine post runs instead\n" );
}

void PostProcessDX12_Shutdown()
{
	s_BloomFirst.m_Material.Shutdown();
	for ( int i = 0; i < ARRAYSIZE( s_BloomDownsample ); ++i )
		s_BloomDownsample[i].m_Material.Shutdown();
	for ( int i = 0; i < ARRAYSIZE( s_BloomUpsample ); ++i )
		s_BloomUpsample[i].m_Material.Shutdown();
	s_FlareThreshold.m_Material.Shutdown();
	s_FlareInputBlurDown.m_Material.Shutdown();
	s_FlareInputBlurUp.m_Material.Shutdown();
	s_FlareMaterial.m_Material.Shutdown();
	s_FlareBlurDown.m_Material.Shutdown();
	s_FlareBlurUp.m_Material.Shutdown();
	s_GlareMaterial.m_Material.Shutdown();
	s_GlareReduce.m_Material.Shutdown();
	s_CompositeMaterial.m_Material.Shutdown();
	s_RcasMaterial.m_Material.Shutdown();
	s_PostScene.Shutdown();
	s_BloomDown.Shutdown();
	s_BloomUp.Shutdown();
	s_FlareInput.Shutdown();
	s_Flare.Shutdown();
	s_Glare.Shutdown();
	s_GlareBounds.Shutdown();
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

void PostProcessDX12_ValidateConfig()
{
	if ( !s_bDX12 || mat_postfx_dx12_set.GetBool() )
		return;
	mat_postfx_dx12_set.SetValue( 1 );
	if ( mat_postfx_dx12.GetBool() )
		return;
	mat_postfx_dx12.SetValue( 1 );
	Msg( "DX12 post: config.cfg predates mat_postfx_dx12_set and had the post stack off; re-enabled mat_postfx_dx12\n" );
}

// Per-frame settings read outside this file: the renderer's HDR output state (a plain store, no render context)
// and the tone curve of this frame's composite for display-referred materials.
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
	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->SetVectorRenderingParameter( VECTOR_RENDERPARM_DX12_DISPLAY_TRANSFORM,
		Vector( PostProcessDX12_Active() ? HdrPeak( bHdr ) : 0.f, mat_postfx_exposure.GetFloat(), 0.f ) );
}

void PostProcessDX12_Render( IMatRenderContext *pRenderContext, int x, int y, int w, int h, float flBloomScale, bool bColorCorrection )
{
	if ( !PostProcessDX12_Active() || w <= 0 || h <= 0 )
		return;

	// Every pass works on whole render targets; the view rectangle only selects what is copied in and drawn out.
	Rect_t area = { x, y, w, h };
	{
		PIXEVENT( pRenderContext, "Post copy" );
		pRenderContext->CopyRenderTargetToTextureEx( s_PostScene, 0, &area, &area );
	}
	const float flExposure = exp2f( mat_postfx_exposure.GetFloat() );
	const int nDebugView = mat_postfx_debug.GetInt() & 0xf;
	const float flBloomExposure = mat_postfx_bloom_intensity.GetFloat() * flBloomScale;
	const float flFlareIntensity = mat_postfx_lensflare.GetFloat();
	const float flGlareIntensity = mat_postfx_glare.GetFloat();
	// Native HDR inputs and intermediates must be finite; NaN/Inf is already a rendering bug, not a supported
	// zero-weight effect. The composite binds neutral black for skipped chains, never stale previous-frame output.
	// Debug views replace the composite with their selected term. The lens dirt only ever screens into the bloom.
	const bool bComposite = nDebugView < 1 || nDebugView > 4;
	const bool bBloom = ( bComposite && flBloomExposure != 0.f ) || nDebugView == 1;
	const bool bFlare = ( bComposite || nDebugView == 2 ) && flFlareIntensity != 0.f;
	const bool bGlare = ( bComposite || nDebugView == 3 ) && flFlareIntensity != 0.f && flGlareIntensity != 0.f;
	const bool bFlareInput = bFlare || bGlare;
	const bool bDirt = ( bBloom || nDebugView == 4 ) && s_LensDirt.IsValid() && mat_postfx_lensdirt_enable.GetBool() && mat_postfx_lensdirt.GetFloat() > 0.f;

	// High is the eight-level chain. Medium and low drop the finest downsample (half, then quarter too) and
	// widen the first 13-tap kernel by the same factor, then carry the coarser layer sum up through the missing
	// fine mips so the composite still reads bloom mip 1 and averages over the levels actually summed.
	const int nDropFinest = 2 - Clamp( mat_postfx_bloom_quality.GetInt(), 0, 2 );
	const int nFirstMip = 1 + nDropFinest;
	const int nLayers = kBloomLevels - nDropFinest;
	// The first level writes the soft-thresholded exposed scene for the bloom and the plain exposed scene for the
	// lens flare, the latter into the up chain's mip, which the layer sum overwrites only after the flare has read it.
	if ( bBloom || bFlareInput )
	{
		PIXEVENT( pRenderContext, "Bloom downsample" );
		PostMaterial_t &first = s_BloomFirst;
		SetPostPass( first, s_PostScene, 0, s_BloomDown, nFirstMip, flExposure, mat_postfx_bloom_threshold.GetFloat(), 1 << nDropFinest,
			mat_postfx_bloom_boost.GetFloat() );
		SetTexture( first, POST_DST_TEXTURE2, s_BloomUp );
		SetInt( first, POST_DST_MIP2, nFirstMip );
		DrawCompute( pRenderContext, first );
		for ( int nMip = nFirstMip; bBloom && nMip < kBloomLevels; ++nMip )
		{
			PostMaterial_t &material = s_BloomDownsample[nMip - 1];
			SetPostPass( material, s_BloomDown, nMip, s_BloomDown, nMip + 1, 1.f, 0.f );
			DrawCompute( pRenderContext, material );
		}
	}

	// Lens flare (Froyok): the plain first bloom level is thresholded into the flare input and stabilised by one
	// dual Kawase step through its mip 1. Ghosts and halo are built from it and softened by another
	// dual Kawase step; the glare star gathers from the input's eighth-resolution mip 1.
	if ( bFlareInput )
	{
		PIXEVENT( pRenderContext, "Flare" );
		SetPostPass( s_FlareThreshold, s_BloomUp, nFirstMip, s_FlareInput, 0, mat_postfx_lensflare_threshold.GetFloat(),
			MAX( mat_postfx_lensflare_threshold_range.GetFloat(), 0.01f ) );
		DrawCompute( pRenderContext, s_FlareThreshold );
		SetPostPass( s_FlareInputBlurDown, s_FlareInput, 0, s_FlareInput, 1, 0.f, 0.f );
		DrawCompute( pRenderContext, s_FlareInputBlurDown );
	if ( bFlare )
	{
		// Glare reads mip 1 directly; this upsample is only needed by ghosts and halo.
		SetPostPass( s_FlareInputBlurUp, s_FlareInput, 1, s_FlareInput, 0, 0.f, 0.f );
		DrawCompute( pRenderContext, s_FlareInputBlurUp );
		SetPostPass( s_FlareMaterial, s_FlareInput, 0, s_Flare, 0, 0.f, 0.f );
		DrawCompute( pRenderContext, s_FlareMaterial );
		SetPostPass( s_FlareBlurDown, s_Flare, 0, s_Flare, 1, 0.f, 0.f );
		DrawCompute( pRenderContext, s_FlareBlurDown );
		SetPostPass( s_FlareBlurUp, s_Flare, 1, s_Flare, 0, 0.f, 0.f );
		DrawCompute( pRenderContext, s_FlareBlurUp );
	}
	if ( bGlare )
	{
		// Exact FP16 component maxima bound every filtered input tap. Reducing to
		// 16x16 then 1x1 is independent of render size and leaves the glare sum intact.
		SetPostPass( s_GlareReduce, s_FlareInput, 1, s_GlareBounds, 0, 0.f, 0.f );
		DrawCompute( pRenderContext, s_GlareReduce );
		SetPostPass( s_GlareReduce, s_GlareBounds, 0, s_GlareBounds, kGlareBoundsMip, 0.f, 0.f );
		DrawCompute( pRenderContext, s_GlareReduce );
		SetPostPass( s_GlareMaterial, s_FlareInput, 1, s_Glare, 0, flGlareIntensity, MAX( mat_postfx_glare_divider.GetFloat(), 0.01f ) );
		SetTexture( s_GlareMaterial, POST_SRC_TEXTURE2, s_GlareBounds );
		SetInt( s_GlareMaterial, POST_SRC_MIP2, kGlareBoundsMip );
		DrawCompute( pRenderContext, s_GlareMaterial );
	}

	}

	// MagicBloom's layer sum: up[last] = down[last], up[n] = down[n] + tent( up[n + 1] ), so mip 1 holds all layers.
	// A dropped fine mip only carries the sum up (p0 = 0); its unread gSrc is the same coarser mip as gSrc2.
	if ( bBloom )
	{
		PIXEVENT( pRenderContext, "Bloom upsample" );
		for ( int i = 0; i < ARRAYSIZE( s_BloomUpsample ); ++i )
		{
			const int nMip = ARRAYSIZE( s_BloomUpsample ) - i;
			PostMaterial_t &material = s_BloomUpsample[i];
			if ( nMip < nFirstMip )
				SetPostPass( material, s_BloomUp, nMip + 1, s_BloomUp, nMip, 0.f, 0.f );
			else
				SetPostPass( material, s_BloomDown, nMip, s_BloomUp, nMip, 1.f, 0.f );
			SetTexture( material, POST_SRC_TEXTURE2, i == 0 ? s_BloomDown : s_BloomUp );
			SetInt( material, POST_SRC_MIP2, nMip + 1 );
			DrawCompute( pRenderContext, material );
		}
	}
	const int nStatus = DisplayStatus();
	const bool bHdr = mat_hdr_output.GetBool() && SHADERAPIDX12_HDR_DISPLAY_TYPE( nStatus ) == SHADERAPIDX12_HDR_DISPLAY_HDR && s_pShaderAPIDX12->HdrOutputCapable();
	const float flWorldNits = MAX( mat_hdr_output_world_nits.GetFloat(), 1.f );
	const float flUiNits = MAX( mat_hdr_output_ui_nits.GetFloat(), 1.f );
	const float flPeak = HdrPeak( bHdr );
	const float flOutScale = bHdr ? flWorldNits / flUiNits : 1.f;
	// Lens flare and glare are added to the HDR scene at a tenth of their intensity, the bloom weight that used to
	// scale them. The bloom exposure already carries the division by the summed layer count.
	const float composite0[4] = { flExposure, flBloomExposure / nLayers, flFlareIntensity * 0.1f, 0.f };
	const float composite1[4] = { mat_postfx_lensdirt.GetFloat(), 0.f, 0.f, 0.f };
	const float output[4] = { static_cast<float>( bHdr ? 1 : 0 ), 0.f, 0.f, 0.f };
	PostMaterial_t &composite = s_CompositeMaterial;
	SetTexture( composite, COMPOSITE_SCENE, s_PostScene );
	SetTexture( composite, COMPOSITE_BLOOM, s_BloomUp );
	SetTexture( composite, COMPOSITE_FLARE, s_Flare );
	SetTexture( composite, COMPOSITE_GLARE, s_Glare );
	SetTexture( composite, COMPOSITE_DIRT, s_LensDirt );
	SetInt( composite, COMPOSITE_BLOOM_ENABLED, bBloom );
	SetInt( composite, COMPOSITE_FLARE_ENABLED, bFlare );
	SetInt( composite, COMPOSITE_GLARE_ENABLED, bGlare );
	SetInt( composite, COMPOSITE_DIRT_ENABLED, bDirt );
	SetFloat( composite, COMPOSITE_OUT_SCALE, flOutScale );
	SetFloat( composite, COMPOSITE_PEAK, flPeak );
	SetInt( composite, COMPOSITE_DEBUG_VIEW, nDebugView );
	SetVector4( composite, COMPOSITE_PARAMS0, composite0 );
	SetVector4( composite, COMPOSITE_PARAMS1, composite1 );
	SetVector4( composite, COMPOSITE_OUTPUT, output );

	// RCAS sharpens the temporal upscaler's output, so it only runs on frames the upscaler dispatched.
	const bool bSharpen = UpscalerDX12_Enabled() && UpscalerDX12_Status() > 0 && mat_postfx_sharpen.GetFloat() > 0.f;
	const int nTextureWidth = s_PostScene->GetActualWidth();
	const int nTextureHeight = s_PostScene->GetActualHeight();
	pRenderContext->EnableColorCorrection( bColorCorrection );
	if ( bSharpen )
	{
		{
			PIXEVENT( pRenderContext, "Composite" );
			pRenderContext->PushRenderTargetAndViewport( s_Composite );
			pRenderContext->DrawScreenSpaceRectangle( composite.m_Material, x, y, w, h, x, y, x + w - 1, y + h - 1, nTextureWidth, nTextureHeight );
			pRenderContext->PopRenderTargetAndViewport();
		}
		const float texelSize[4] = { 1.f / nTextureWidth, 1.f / nTextureHeight, 0.f, 0.f };
		PostMaterial_t &rcas = s_RcasMaterial;
		SetTexture( rcas, RCAS_BASE_TEXTURE, s_Composite );
		SetFloat( rcas, RCAS_SHARPNESS, Clamp( mat_postfx_sharpen.GetFloat(), 0.f, 1.f ) );
		SetFloat( rcas, RCAS_NORM_SCALE, flPeak * flOutScale );
		SetVector4( rcas, RCAS_TEXEL_SIZE, texelSize );
		{
			PIXEVENT( pRenderContext, "Sharpen" );
			pRenderContext->SetRenderTarget( nullptr );
			pRenderContext->DrawScreenSpaceRectangle( rcas.m_Material, x, y, w, h, x, y, x + w - 1, y + h - 1, nTextureWidth, nTextureHeight );
		}
	}
	else
	{
		PIXEVENT( pRenderContext, "Composite" );
		pRenderContext->SetRenderTarget( nullptr );
		pRenderContext->DrawScreenSpaceRectangle( composite.m_Material, x, y, w, h, x, y, x + w - 1, y + h - 1, nTextureWidth, nTextureHeight );
	}
}

int PostProcessDX12_Status()
{
	return PostProcessDX12_Active() ? 1 : 0;
}
