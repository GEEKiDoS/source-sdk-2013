//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
//===========================================================================

#include "cbase.h"

#include "materialsystem/imaterialsystem.h"
#include "materialsystem/MaterialSystemUtil.h"
#include "materialsystem/itexture.h"
#include "materialsystem/imaterialvar.h"
#include "materialsystem/imaterialsystemhardwareconfig.h"
#include "materialsystem/materialsystem_config.h"
#include "tier1/callqueue.h"
#include "colorcorrectionmgr.h"
#include "view_scene.h"
#include "c_world.h"
#include "bitmap/tgawriter.h"
#include "filesystem.h"
#include "tier0/vprof.h"

#include "proxyentity.h"

#if defined( L4D2 )
#include "viewpostprocess.h"
#include "l4d2/clientmode_terror.h"
#include "l4d2/ui/terror_client_panels.h"
#include "l4d2/weapons/c_vomitjar_projectile.h"
#include "l4d2/effects/c_terror_postprocess_controller.h"
#include "l4d2/effects/terror_tonemap_state.h"
#include "l4d2/effects/terror_depth_of_field.h"
#include "iviewrender.h"
#include "view_shared.h"
#include "materialsystem/MaterialSystemUtil.h"
#include "igamesystem.h"
#endif

#include "motionvectors_dx12.h"
#include "postprocess_dx12.h"
//-----------------------------------------------------------------------------
// Globals
//-----------------------------------------------------------------------------

// mapmaker controlled autoexposure
bool g_bUseCustomAutoExposureMin = false;
bool g_bUseCustomAutoExposureMax = false;
bool g_bUseCustomBloomScale = false;
float g_flCustomAutoExposureMin = 0;
float g_flCustomAutoExposureMax = 0;
float g_flCustomBloomScale = 0.0f;
float g_flCustomBloomScaleMinimum = 0.0f;

bool g_bFlashlightIsOn = false;

// hdr parameters
#if defined( L4D2 )
ConVar mat_bloomscale( "mat_bloomscale", "1", 
#if defined( L4D2 )
	0x0000000A
#else
	FCVAR_DEVELOPMENTONLY
#endif
 );
ConVar mat_hdr_level( "mat_hdr_level", "2", 
#if defined( L4D2 )
	0x0000000A
#else
	FCVAR_DEVELOPMENTONLY
#endif
 );
#else
ConVar mat_bloomscale( "mat_bloomscale", "1" );
ConVar mat_hdr_level( "mat_hdr_level", "2", FCVAR_ARCHIVE );
#endif

ConVar mat_bloomamount_rate( "mat_bloomamount_rate", "0.05f", 
#if defined( L4D2 )
	0x00004008
#else
	FCVAR_CHEAT
#endif
 );
static ConVar debug_postproc( "mat_debug_postprocessing_effects", "0", 
#if defined( L4D2 )
	0x0000000A
#else
	FCVAR_NONE
#endif
, "0 = off, 1 = show post-processing passes in quadrants of the screen, 2 = only apply post-processing to the centre of the screen" );
static ConVar split_postproc( "mat_debug_process_halfscreen", "0", FCVAR_CHEAT );
static ConVar mat_postprocessing_combine( "mat_postprocessing_combine", "1", FCVAR_NONE, "Combine bloom, software anti-aliasing and color correction into one post-processing pass" );
static ConVar mat_dynamic_tonemapping( "mat_dynamic_tonemapping", "1", 
#if defined( L4D2 )
	0x00004008
#else
	FCVAR_CHEAT
#endif
 );
static ConVar mat_show_ab_hdr( "mat_show_ab_hdr", "0" );
#if defined( L4D2 )
static ConVar mat_tonemapping_occlusion_use_stencil( "mat_tonemapping_occlusion_use_stencil", "0", 
#if defined( L4D2 )
	0x0000000A
#else
	FCVAR_DEVELOPMENTONLY
#endif
 );
#else
static ConVar mat_tonemapping_occlusion_use_stencil( "mat_tonemapping_occlusion_use_stencil", "0" );
#endif
ConVar mat_debug_autoexposure("mat_debug_autoexposure","0", FCVAR_CHEAT);
#if defined( L4D2 )
static ConVar mat_autoexposure_max( "mat_autoexposure_max", "8", 
#if defined( L4D2 )
	0x0000000A
#else
	FCVAR_DEVELOPMENTONLY
#endif
 );
static ConVar mat_autoexposure_min( "mat_autoexposure_min", "0.1", 
#if defined( L4D2 )
	0x0000000A
#else
	FCVAR_DEVELOPMENTONLY
#endif
 );
static ConVar mat_show_histogram( "mat_show_histogram", "0", 
#if defined( L4D2 )
	0x0000000A
#else
	FCVAR_DEVELOPMENTONLY
#endif
 );
#else
static ConVar mat_autoexposure_max( "mat_autoexposure_max", "2" );
static ConVar mat_autoexposure_min( "mat_autoexposure_min", "0.5" );
static ConVar mat_show_histogram( "mat_show_histogram", "0" );
#endif
ConVar mat_hdr_tonemapscale( "mat_hdr_tonemapscale", "1.0", FCVAR_CHEAT );
ConVar mat_hdr_uncapexposure( "mat_hdr_uncapexposure", "0", 
#if defined( L4D2 )
	0x00004008
#else
	FCVAR_CHEAT
#endif
 );
ConVar mat_force_bloom("mat_force_bloom","0", 
#if defined( L4D2 )
	0x00004008
#else
	FCVAR_CHEAT
#endif
);
#if defined( L4D2 )
ConVar mat_disable_bloom( "mat_disable_bloom", "0", 
#if defined( L4D2 )
	0x0000000A
#else
	FCVAR_DEVELOPMENTONLY
#endif
 );
#else
ConVar mat_disable_bloom("mat_disable_bloom","0");
#endif
ConVar mat_debug_bloom("mat_debug_bloom","0", 
#if defined( L4D2 )
	0x00004008
#else
	FCVAR_CHEAT
#endif
);

ConVar mat_accelerate_adjust_exposure_down( "mat_accelerate_adjust_exposure_down", "3.0", 
#if defined( L4D2 )
	0x00004008
#else
	FCVAR_CHEAT
#endif
 );
#if defined( L4D2 )
ConVar mat_hdr_manual_tonemap_rate( "mat_hdr_manual_tonemap_rate", "1.0", 
#if defined( L4D2 )
	0x0000000A
#else
	FCVAR_DEVELOPMENTONLY
#endif
 );
#else
ConVar mat_hdr_manual_tonemap_rate( "mat_hdr_manual_tonemap_rate", "1.0" );
#endif

// fudge factor to make non-hdr bloom more closely match hdr bloom. Because of auto-exposure, high
// bloomscales don't blow out as much in hdr. this factor was derived by comparing images in a
// reference scene.
#if defined( L4D2 )
ConVar mat_non_hdr_bloom_scalefactor( "mat_non_hdr_bloom_scalefactor", ".3", 
#if defined( L4D2 )
	0x0000000A
#else
	FCVAR_DEVELOPMENTONLY
#endif
 );
#else
ConVar mat_non_hdr_bloom_scalefactor("mat_non_hdr_bloom_scalefactor",".3");
#endif

// Apply addition scale to the final bloom scale
#if defined( L4D2 )
static ConVar mat_bloom_scalefactor_scalar( "mat_bloom_scalefactor_scalar", "1.0", 
#if defined( L4D2 )
	0x00080008
#else
	0x80000
#endif
 );
#else
static ConVar mat_bloom_scalefactor_scalar( "mat_bloom_scalefactor_scalar", "1.0" );
#endif

//ConVar mat_exposure_center_region_x( "mat_exposure_center_region_x","0.75", FCVAR_CHEAT );
//ConVar mat_exposure_center_region_y( "mat_exposure_center_region_y","0.80", FCVAR_CHEAT );
//ConVar mat_exposure_center_region_x_flashlight( "mat_exposure_center_region_x_flashlight","0.33", FCVAR_CHEAT );
//ConVar mat_exposure_center_region_y_flashlight( "mat_exposure_center_region_y_flashlight","0.33", FCVAR_CHEAT );

ConVar mat_exposure_center_region_x( "mat_exposure_center_region_x","0.9", 
#if defined( L4D2 )
	0x00004008
#else
	FCVAR_CHEAT
#endif
 );
ConVar mat_exposure_center_region_y( "mat_exposure_center_region_y","0.85", 
#if defined( L4D2 )
	0x00004008
#else
	FCVAR_CHEAT
#endif
 );
#if !defined( L4D2 )
ConVar mat_exposure_center_region_x_flashlight( "mat_exposure_center_region_x_flashlight","0.9", FCVAR_CHEAT );
ConVar mat_exposure_center_region_y_flashlight( "mat_exposure_center_region_y_flashlight","0.85", FCVAR_CHEAT );
#endif

ConVar mat_tonemap_algorithm( "mat_tonemap_algorithm", "1", 
#if defined( L4D2 )
	0x00004008
#else
	FCVAR_CHEAT
#endif
, "0 = Original Algorithm 1 = New Algorithm" );
#if !defined( L4D2 )
ConVar mat_tonemap_percent_target( "mat_tonemap_percent_target", "60.0", FCVAR_CHEAT );
ConVar mat_tonemap_percent_bright_pixels( "mat_tonemap_percent_bright_pixels", "2.0", FCVAR_CHEAT );
ConVar mat_tonemap_min_avglum( "mat_tonemap_min_avglum", "3.0", FCVAR_CHEAT );
#endif
ConVar mat_fullbright( "mat_fullbright", "0", 
#if defined( L4D2 )
	0x00004008
#else
	FCVAR_CHEAT
#endif
 );

#if defined( L4D2 )
C_TerrorPostProcessController *C_TerrorPostProcessController::s_pMaster=NULL;
 IMPLEMENT_CLIENTCLASS_DT(C_TerrorPostProcessController,DT_PostProcessController,CPostProcessController)
  RecvPropFloat(RECVINFO_NAME(m_Values[0],m_fLocalContrastStrength)),
  RecvPropFloat(RECVINFO_NAME(m_Values[1],m_fLocalContrastEdgeStrength)),
  RecvPropFloat(RECVINFO_NAME(m_Values[2],m_fVignetteStart)),
  RecvPropFloat(RECVINFO_NAME(m_Values[3],m_fVignetteEnd)),
  RecvPropFloat(RECVINFO_NAME(m_Values[4],m_fVignetteBlurStrength)),
  RecvPropFloat(RECVINFO_NAME(m_Values[5],m_fFadeToBlackStrength)),
  RecvPropFloat(RECVINFO_NAME(m_Values[6],m_fGrainStrength)),
  RecvPropFloat(RECVINFO_NAME(m_Values[7],m_fTopVignetteStrength)),
  RecvPropFloat(RECVINFO(m_fFadeTime)),
  RecvPropBool(RECVINFO(m_bMaster)),
 END_RECV_TABLE()

float g_flTerrorTonemapPercentTarget=45.0f;
 float g_flTerrorTonemapPercentBrightPixels=1.0f;
 float g_flTerrorTonemapMinAvgLum=3.0f;
 static CTerrorTonemapState g_TerrorTonemapState;
 static ConVar mat_force_tonemap_percent_target("mat_force_tonemap_percent_target","-1",
#if defined( L4D2 )
	0x00004008
#else
	FCVAR_CHEAT
#endif
,"Override. Old default was 45.");
 static ConVar mat_force_tonemap_percent_bright_pixels("mat_force_tonemap_percent_bright_pixels","-1",
#if defined( L4D2 )
	0x00004008
#else
	FCVAR_CHEAT
#endif
,"Override. Old value was 1.0");
 static ConVar mat_force_tonemap_min_avglum("mat_force_tonemap_min_avglum","-1",
#if defined( L4D2 )
	0x00004008
#else
	FCVAR_CHEAT
#endif
,"Override. Old default was 3.0");
 static float Terror_TonemapPercentTarget()
 {
  float v=mat_force_tonemap_percent_target.GetFloat();
  return v<0.0f?g_flTerrorTonemapPercentTarget:v;
 }
 static float Terror_TonemapPercentBrightPixels()
 {
  float v=mat_force_tonemap_percent_bright_pixels.GetFloat();
  return v<0.0f?g_flTerrorTonemapPercentBrightPixels:v;
 }
 static float Terror_TonemapMinAvgLum()
 {
  float v=mat_force_tonemap_min_avglum.GetFloat();
  return v<0.0f?g_flTerrorTonemapMinAvgLum:v;
 }

static float g_TerrorPostFade[4] = { 0, 0, 0, 0 };
static int g_TerrorPostFadeType = 0;
static ConVar mat_grain_enable( "mat_grain_enable", "1", 
#if defined( L4D2 )
	0x0000000A
#else
	FCVAR_DEVELOPMENTONLY
#endif
 );
static ConVar mat_vignette_enable( "mat_vignette_enable", "1", 
#if defined( L4D2 )
	0x0000000A
#else
	FCVAR_DEVELOPMENTONLY
#endif
 );
static ConVar mat_local_contrast_enable( "mat_local_contrast_enable", "1", 
#if defined( L4D2 )
	0x0000000A
#else
	FCVAR_DEVELOPMENTONLY
#endif
 );
static ConVar mat_postprocess_enable( "mat_postprocess_enable", "1", 
#if defined( L4D2 )
	0x00004008
#else
	FCVAR_CHEAT
#endif
 );

void Terror_SetEnginePostFade( const byte *rgba, bool modulate )
{
	for ( int i = 0; i < 4; ++i ) g_TerrorPostFade[i] = (float)rgba[i] * 0.0039215689f;
	g_TerrorPostFadeType = rgba[3] > 0 ? ( modulate ? 2 : 1 ) : 0;
}

class CTerrorPostMaterials : public CAutoGameSystem
{
public:
	CTerrorPostMaterials() : CAutoGameSystem( "TerrorPostMaterials" ), m_bEngineBloomAuthored( true ) {}
	IMaterial *GetEnginePost()
	{
		if ( !m_Engine.IsValid() )
		{
			KeyValues *material = new KeyValues( "TerrorEnginePost" );
			if ( !material->LoadFromFile( filesystem, "materials/dev/engine_post.vmt", "GAME" ) )
			{
				material->deleteThis();
				// Native C1C9310 uses FindMaterial, including its error material.
				m_Engine.Init( "dev/engine_post", TEXTURE_GROUP_OTHER );
				return m_Engine;
			}
			material->SetName( "TerrorEnginePost" );
			m_Engine.Init( "__l4d2/engine_post", material );
			m_bEngineBloomAuthored = m_Engine->FindVar( "$bloomenable", NULL, false )->GetIntValue() != 0;
		}
		return m_Engine;
	}
	// The DX12 post stack owns bloom; the engine post keeps its authored bloom only when it is the sole bloom.
	void SetEngineBloomEnabled( bool enable )
	{
		IMaterialVar *pBloomEnable = GetEnginePost()->FindVar( "$bloomenable", NULL, false );
		pBloomEnable->SetIntValue( enable && m_bEngineBloomAuthored );
	}
	IMaterial *GetPrepare( int pass )
	{
		if ( !m_Prepare[pass].IsValid() )
		{
			static const char *names[5] = { "__l4d2/post_downsample", "__l4d2/post_blur_x", "__l4d2/post_blur_y_clear",
				"__l4d2/post_downsample_hdr", "__l4d2/post_blur_y" };
			KeyValues *material = new KeyValues( "TerrorPostPrepare" );
			material->SetString( "$basetexture", ( pass == 0 || pass == 3 ) ? "_rt_FullFrameFB" : pass == 1 ? "_rt_SmallFB0" : "_rt_SmallFB1" );
			material->SetInt( "$pass", pass );
			material->SetInt( "$ignorez", 1 );
			m_Prepare[pass].Init( names[pass], material );
		}
		return m_Prepare[pass];
	}
	IMaterial *GetFadeBlur()
	{
		if ( !m_FadeBlur.IsValid() )
		{
			KeyValues *material = new KeyValues( "TerrorFadeBlur" );
			if ( !material->LoadFromFile( filesystem, "materials/dev/fade_blur.vmt", "GAME" ) )
			{
				material->deleteThis();
				// Preserve the native material-system missing-file diagnostic.
				m_FadeBlur.Init( "dev/fade_blur", TEXTURE_GROUP_OTHER );
				return m_FadeBlur;
			}
			material->SetName( "TerrorFadeBlur" );
			m_FadeBlur.Init( "__l4d2/fade_blur", material );
		}
		return m_FadeBlur;
	}
	IMaterial *GetDOFMaterial( int pass )
	{
		if ( !m_DOF[pass].IsValid() )
		{
			static const char *paths[3] = { "materials/dev/depth_of_field.vmt", "materials/dev/blurgaussian_3x3.vmt", "materials/dev/clearalpha.vmt" };
			static const char *shaders[3] = { "DepthOfField", "TerrorDOFBlurGaussian", "TerrorDOFClearAlpha" };
			static const char *names[3] = { "__l4d2/depth_of_field", "__l4d2/dof_gaussian", "__l4d2/dof_clearalpha" };
			static const char *originalNames[3] = { "dev/depth_of_field", "dev/blurgaussian_3x3", "dev/clearalpha" };
			KeyValues *material = new KeyValues( shaders[pass] );
			if ( !material->LoadFromFile( filesystem, paths[pass], "GAME" ) )
			{
				material->deleteThis();
				m_DOF[pass].Init( originalNames[pass], TEXTURE_GROUP_OTHER );
				return m_DOF[pass];
			}
			material->SetName( shaders[pass] );
			m_DOF[pass].Init( names[pass], material );
		}
		return m_DOF[pass];
	}
	virtual void Shutdown() OVERRIDE
	{
		m_Engine.Shutdown();
		m_FadeBlur.Shutdown();
		for ( int i = 0; i < 5; ++i ) m_Prepare[i].Shutdown();
		for ( int i = 0; i < 3; ++i ) m_DOF[i].Shutdown();
	}
private:
	CMaterialReference m_Engine, m_Prepare[5], m_FadeBlur;
	CMaterialReference m_DOF[3];
	bool m_bEngineBloomAuthored;
};
static CTerrorPostMaterials g_TerrorPostMaterials;

static void SetRenderTargetAndViewPort(ITexture *rt);

static void Terror_PrepareEnginePost(IMatRenderContext *ctx)
{
 ITexture *full=materials->FindTexture("_rt_FullFrameFB",TEXTURE_GROUP_RENDER_TARGET);
 ITexture *small0=materials->FindTexture("_rt_SmallFB0",TEXTURE_GROUP_RENDER_TARGET);
 ITexture *small1=materials->FindTexture("_rt_SmallFB1",TEXTURE_GROUP_RENDER_TARGET);
 const int fw=full->GetActualWidth(), fh=full->GetActualHeight();
 const int qw=fw/4, qh=fh/4;
 ctx->PushRenderTargetAndViewport();
 SetRenderTargetAndViewPort(small0);
 ctx->DrawScreenSpaceRectangle(g_TerrorPostMaterials.GetPrepare(0),0,0,qw,qh,0,0,fw-2,fh-2,fw,fh);
 SetRenderTargetAndViewPort(small1);
 ctx->DrawScreenSpaceRectangle(g_TerrorPostMaterials.GetPrepare(1),0,0,qw,qh,0,0,qw-1,qh-1,qw,qh);
 SetRenderTargetAndViewPort(small0);
 ctx->DrawScreenSpaceRectangle(g_TerrorPostMaterials.GetPrepare(2),0,0,qw,qh,0,0,qw-1,qh-1,qw,qh);
 ctx->PopRenderTargetAndViewport();
}

static void Terror_PrepareTransitionBlur( IMatRenderContext *context )
{
	ITexture *full = materials->FindTexture( "_rt_FullFrameFB", TEXTURE_GROUP_RENDER_TARGET );
	ITexture *small0 = materials->FindTexture( "_rt_SmallFB0", TEXTURE_GROUP_RENDER_TARGET );
	ITexture *small1 = materials->FindTexture( "_rt_SmallFB1", TEXTURE_GROUP_RENDER_TARGET );
	const int width = full->GetActualWidth(), height = full->GetActualHeight();
	const int qw = width / 4, qh = height / 4;
	context->PushRenderTargetAndViewport();
	SetRenderTargetAndViewPort( small0 );
	context->DrawScreenSpaceRectangle( g_TerrorPostMaterials.GetPrepare( 3 ), 0, 0, qw, qh, 0, 0, width - 2, height - 2, width, height );
	SetRenderTargetAndViewPort( small1 );
	context->DrawScreenSpaceRectangle( g_TerrorPostMaterials.GetPrepare( 1 ), 0, 0, qw, qh, 0, 0, qw - 1, qh - 1, qw, qh );
	SetRenderTargetAndViewPort( small0 );
	context->DrawScreenSpaceRectangle( g_TerrorPostMaterials.GetPrepare( 4 ), 0, 0, qw, qh, 0, 0, qw - 1, qh - 1, qw, qh );
	context->PopRenderTargetAndViewport();
}
void Terror_RenderTransitionBlur( float alpha, float desaturation )
{
	//COMISS/JB: unordered alpha skips, exactly like values below 1e-4.
	if ( !( alpha >= 0.000099999997f ) ) return;
	const CViewSetup *setup = view->GetViewSetup();
	UpdateScreenEffectTexture( 0, setup->x, setup->y, setup->width, setup->height, false );
	CMatRenderContextPtr context( materials );
	Terror_PrepareTransitionBlur( context );
	int x, y, width, height;
	context->GetViewport( x, y, width, height );
	int targetWidth, targetHeight;
	context->GetRenderTargetDimensions( targetWidth, targetHeight );
	IMaterial *fade = g_TerrorPostMaterials.GetFadeBlur();
	fade->FindVar( "$c0_x", NULL, false )->SetFloatValue( alpha );
	fade->FindVar( "$c1_x", NULL, false )->SetFloatValue( desaturation );
	context->DrawScreenSpaceRectangle( fade, 0, 0, width, height, (float)x, (float)y,
		(float)( x + width - 1 ), (float)( y + height - 1 ), targetWidth, targetHeight );
}
#endif

extern ConVar localplayer_visionflags;

enum PostProcessingCondition {
	PPP_ALWAYS,
	PPP_IF_COND_VAR,
	PPP_IF_NOT_COND_VAR
};

struct PostProcessingPass {
	PostProcessingCondition ppp_test;
	ConVar const *cvar_to_test;
	char const *material_name;								// terminate list with null
	char const *dest_rendering_target;
	char const *src_rendering_target;						// can be null. needed for source scaling
	int xdest_scale,ydest_scale;							// allows scaling down
	int xsrc_scale,ysrc_scale;								// allows scaling down
	CMaterialReference m_mat_ref;							// so we don't have to keep searching
};

enum PostProcessMaterialId
{
	PPMAT_LUMINANCE, PPMAT_NO_PIXEL_WRITE, PPMAT_ENGINE_POST, PPMAT_BLOOM_ADD,
	PPMAT_DOWNSAMPLE_NO_HDR, PPMAT_BLUR_X_NO_HDR, PPMAT_BLUR_Y_NO_HDR,
	PPMAT_FLOAT_COMBINE, PPMAT_MOTION_BLUR, PPMAT_MOTION_BLUR_MV,
	PPMAT_PYRO_BORDER, PPMAT_PYRO_VIGNETTE, PPMAT_PYRO_POST, PPMAT_COUNT
};

enum PostProcessVarId
{
	PPVAR_BASE, PPVAR_C0_X, PPVAR_C0_Y, PPVAR_C0_Z, PPVAR_BLOOM_AMOUNT,
	PPVAR_VELOCITY_REGION, PPVAR_DISABLED, PPVAR_COUNT
};

struct PostProcessMaterialEntry
{
	const char *name;
	unsigned int varMask;
	CMaterialReference material;
	unsigned int tokens[PPVAR_COUNT];
	bool found[PPVAR_COUNT];
};

#define PPVAR_BIT( var ) ( 1u << ( var ) )
static PostProcessMaterialEntry s_PostProcessMaterials[PPMAT_COUNT] =
{
	{ "dev/lumcompare", PPVAR_BIT( PPVAR_BASE ) | PPVAR_BIT( PPVAR_C0_X ) | PPVAR_BIT( PPVAR_C0_Y ) | PPVAR_BIT( PPVAR_C0_Z ) },
	{ "dev/no_pixel_write", 0 },
	{ "dev/engine_post", 0 },
	{ "dev/bloomadd", 0 },
#ifdef CSTRIKE_DLL
	{ "dev/downsample_non_hdr_cstrike", 0 },
#else
	{ "dev/downsample_non_hdr", 0 },
#endif
	{ "dev/blurfilterx_nohdr", 0 },
	{ "dev/blurfiltery_nohdr", PPVAR_BIT( PPVAR_BLOOM_AMOUNT ) },
	{ "dev/floattoscreen_combine", PPVAR_BIT( PPVAR_BLOOM_AMOUNT ) },
	{ "dev/motion_blur", PPVAR_BIT( PPVAR_BASE ) },
	{ "dev/motion_blur_mv", PPVAR_BIT( PPVAR_BASE ) | PPVAR_BIT( PPVAR_VELOCITY_REGION ) },
#ifdef TF_CLIENT_DLL
	{ "dev/pyro_vignette_border", 0 },
	{ "dev/pyro_vignette", 0 },
	{ "dev/pyro_post", PPVAR_BIT( PPVAR_DISABLED ) },
#else
	{ NULL, 0 }, { NULL, 0 }, { NULL, 0 },
#endif
};
#undef PPVAR_BIT

static const char *s_PostProcessVarNames[PPVAR_COUNT] =
{
	"$basetexture", "$C0_X", "$C0_Y", "$C0_Z", "$bloomamount", "$velocityregion", "$disabled"
};

static IMaterial *PostProcessMaterial( PostProcessMaterialId id )
{
	return s_PostProcessMaterials[id].material;
}

static IMaterialVar *PostProcessVar( PostProcessMaterialId material, PostProcessVarId var )
{
	PostProcessMaterialEntry &entry = s_PostProcessMaterials[material];
	return entry.found[var] ? entry.material->FindVarFast( s_PostProcessVarNames[var], &entry.tokens[var] ) : NULL;
}

#define PPP_PROCESS_PARTIAL_SRC(srcmatname,dest_rt_name,src_tname,scale) \
{PPP_ALWAYS,0,srcmatname,dest_rt_name,src_tname,1,1,scale,scale}
#define PPP_PROCESS_PARTIAL_DEST(srcmatname,dest_rt_name,src_tname,scale) \
{PPP_ALWAYS,0,srcmatname,dest_rt_name,src_tname,scale,scale,1,1}
#define PPP_PROCESS_PARTIAL_SRC_PARTIAL_DEST(srcmatname,dest_rt_name,src_tname,srcscale,destscale) \
{PPP_ALWAYS,0,srcmatname,dest_rt_name,src_tname,destscale,destscale,srcscale,srcscale}
#define PPP_END 	{PPP_ALWAYS,0,NULL,NULL,0,0,0,0,0}
#define PPP_PROCESS(srcmatname,dest_rt_name) {PPP_ALWAYS,0,srcmatname,dest_rt_name,0,1,1,1,1}
#define PPP_PROCESS_IF_CVAR(cvarptr,srcmatname,dest_rt_name) \
{PPP_IF_COND_VAR,cvarptr,srcmatname,dest_rt_name,0,1,1,1,1}
#define PPP_PROCESS_IF_NOT_CVAR(cvarptr,srcmatname,dest_rt_name) \
{PPP_IF_NOT_COND_VAR,cvarptr,srcmatname,dest_rt_name,0,1,1,1,1}
#define PPP_PROCESS_IF_NOT_CVAR_SRCTEXTURE(cvarptr,srcmatname,src_tname,dest_rt_name) \
{PPP_IF_NOT_COND_VAR,cvarptr,srcmatname,dest_rt_name,src_tname,1,1,1,1}
#define PPP_PROCESS_IF_CVAR_SRCTEXTURE(cvarptr,srcmatname,src_txtrname,dest_rt_name) \
{PPP_IF_COND_VAR,cvarptr,srcmatname,dest_rt_name,src_txtrname,1,1,1,1}
#define PPP_PROCESS_SRCTEXTURE(srcmatname,src_tname,dest_rt_name) \
{PPP_ALWAYS,0,srcmatname,dest_rt_name,src_tname,1,1,1,1}

struct ClipBox
{
	int m_minx, m_miny;
	int m_maxx, m_maxy;
};

static void DrawClippedScreenSpaceRectangle( 
	IMaterial *pMaterial,
	int destx, int desty,
	int width, int height,
	float src_texture_x0, float src_texture_y0,			// which texel you want to appear at
	// destx/y
	float src_texture_x1, float src_texture_y1,			// which texel you want to appear at
	// destx+width-1, desty+height-1
	int src_texture_width, int src_texture_height,		// needed for fixup
	ClipBox const *clipbox,
	void *pClientRenderable = NULL )
{
	if (clipbox)
	{
		if ( (destx > clipbox->m_maxx ) || ( desty > clipbox->m_maxy ))
			return;
		if ( (destx + width - 1 < clipbox->m_minx ) || ( desty + height - 1 < clipbox->m_miny ))
			return;
		// left clip
		if ( destx < clipbox->m_minx )
		{
			src_texture_x0 = FLerp( src_texture_x0, src_texture_x1, destx, destx + width - 1, clipbox->m_minx );
			width -= ( clipbox->m_minx - destx );
			destx = clipbox->m_minx;
		}
		// top clip
		if ( desty < clipbox->m_miny )
		{
			src_texture_y0 = FLerp( src_texture_y0, src_texture_y1, desty, desty + height - 1, clipbox->m_miny );
			height -= ( clipbox->m_miny - desty );
			desty = clipbox->m_miny;
		}
		// right clip
		if ( destx + width - 1 > clipbox->m_maxx )
		{
			src_texture_x1 = FLerp( src_texture_x0, src_texture_x1, destx, destx + width - 1, clipbox->m_maxx );
			width = clipbox->m_maxx - destx;
		}
		// bottom clip
		if ( desty + height - 1 > clipbox->m_maxy )
		{
			src_texture_y1 = FLerp( src_texture_y0, src_texture_y1, desty, desty + height - 1, clipbox->m_maxy );
			height = clipbox->m_maxy - desty;
		}
	}
	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->DrawScreenSpaceRectangle( pMaterial, destx, desty, width, height, src_texture_x0,
											  src_texture_y0, src_texture_x1, src_texture_y1,
											  src_texture_width, src_texture_height, pClientRenderable );
}


void ApplyPostProcessingPasses(PostProcessingPass *pass_list, // table of effects to apply
							   ClipBox const *clipbox=0,	// clipping box for these effects
							   ClipBox *dest_coords_out=0)	// receives dest coords of last blit
{
	CMatRenderContextPtr pRenderContext( materials );
	ITexture *pSaveRenderTarget = pRenderContext->GetRenderTarget();
	int pcount=0;
	if ( debug_postproc.GetInt() == 1 ) 
	{
		pRenderContext->SetRenderTarget(NULL);
		int dest_width,dest_height;
		pRenderContext->GetRenderTargetDimensions( dest_width, dest_height );
		pRenderContext->Viewport( 0, 0, dest_width, dest_height );
		pRenderContext->ClearColor3ub(255,0,0);
		//		pRenderContext->ClearBuffers(true,true);
	}

	while(pass_list->material_name)
	{
		bool do_it=true;

		switch(pass_list->ppp_test)
		{
		case PPP_IF_COND_VAR:
			do_it=(pass_list->cvar_to_test)->GetBool();
			break;
		case PPP_IF_NOT_COND_VAR:
			do_it=! ((pass_list->cvar_to_test)->GetBool());
			break;
		}
		if ((pass_list->dest_rendering_target==0) && (debug_postproc.GetInt() == 1))
			do_it=0;
		if (do_it)
		{
			ClipBox const *cb=0;
			if (pass_list->dest_rendering_target==0)
			{
				cb=clipbox;
			}

			IMaterial *src_mat=pass_list->m_mat_ref;
			if (pass_list->dest_rendering_target)
			{
				ITexture *dest_rt=materials->FindTexture(pass_list->dest_rendering_target,
					TEXTURE_GROUP_RENDER_TARGET );
				pRenderContext->SetRenderTarget( dest_rt);
			}
			else
			{
				pRenderContext->SetRenderTarget( NULL );
			}
			int dest_width,dest_height;
			pRenderContext->GetRenderTargetDimensions( dest_width, dest_height );
			pRenderContext->Viewport( 0, 0, dest_width, dest_height );
			dest_width/=pass_list->xdest_scale;
			dest_height/=pass_list->ydest_scale;

			if (pass_list->src_rendering_target)
			{
				ITexture *src_rt=materials->FindTexture(pass_list->src_rendering_target,
					TEXTURE_GROUP_RENDER_TARGET );
				int src_width=src_rt->GetActualWidth();
				int src_height=src_rt->GetActualHeight();
				int ssrc_width=(src_width-1)/pass_list->xsrc_scale;
				int ssrc_height=(src_height-1)/pass_list->ysrc_scale;
				DrawClippedScreenSpaceRectangle(
					src_mat,0,0,dest_width,dest_height,
					0,0,ssrc_width,ssrc_height,src_width,src_height,cb);
				if ((pass_list->dest_rendering_target) && (debug_postproc.GetInt() == 1))
				{
					pRenderContext->SetRenderTarget(NULL);
					int row=pcount/2;
					int col=pcount %2;
					int vdest_width,vdest_height;
					pRenderContext->GetRenderTargetDimensions( vdest_width, vdest_height );
					pRenderContext->Viewport( 0, 0, vdest_width, vdest_height );
					pRenderContext->DrawScreenSpaceRectangle(
						src_mat,col*400,200+row*300,dest_width,dest_height,
						0,0,ssrc_width,ssrc_height,src_width,src_height);
				}
			}
			else
			{
				// just draw the whole source
				if ((pass_list->dest_rendering_target==0) && split_postproc.GetInt())
				{
					DrawClippedScreenSpaceRectangle(src_mat,0,0,dest_width/2,dest_height,
						0,0,.5,1,1,1,cb);
				}
				else
				{
					DrawClippedScreenSpaceRectangle(src_mat,0,0,dest_width,dest_height,
						0,0,1,1,1,1,cb);
				}
				if ((pass_list->dest_rendering_target) && (debug_postproc.GetInt() == 1))
				{
					pRenderContext->SetRenderTarget(NULL);
					int row=pcount/4;
					int col=pcount %4;
					//int dest_width,dest_height;
					pRenderContext->GetRenderTargetDimensions( dest_width, dest_height );
					pRenderContext->Viewport( 0, 0, dest_width, dest_height );
					DrawClippedScreenSpaceRectangle(src_mat,10+col*220,10+row*220,
						200,200,
						0,0,1,1,1,1,cb);
				}	
			}
			if (dest_coords_out)
			{
				dest_coords_out->m_minx=0;
				dest_coords_out->m_maxx=dest_width-1;
				dest_coords_out->m_miny=0;
				dest_coords_out->m_maxy=dest_height-1;
			}
		}
		pass_list++;
		pcount++;
	}
	pRenderContext->SetRenderTarget(pSaveRenderTarget);
}

PostProcessingPass HDRFinal_Float[] =
{
	PPP_PROCESS_SRCTEXTURE( "dev/downsample", "_rt_FullFrameFB", "_rt_SmallFB0" ),
	PPP_PROCESS_SRCTEXTURE( "dev/blurfilterx", "_rt_SmallFB0", "_rt_SmallFB1" ),
 	PPP_PROCESS_SRCTEXTURE( "dev/blurfiltery", "_rt_SmallFB1", "_rt_SmallFB0" ),
 	PPP_PROCESS_SRCTEXTURE("dev/floattoscreen_combine","_rt_FullFrameFB",NULL),
	PPP_END
};

PostProcessingPass HDRFinal_Float_NoBloom[] =
{
	PPP_PROCESS_SRCTEXTURE("dev/copyfullframefb", "_rt_FullFrameFB",NULL),
	PPP_END
};

PostProcessingPass HDRSimulate_NonHDR[] =
{
	PPP_PROCESS("dev/copyfullframefb_vanilla",NULL),
	PPP_END
};

void PrepareViewPostProcessMaterials()
{
	// FindVar's engine symbol table uses a shared search-string scratch slot
	// under a reader lock. Prime tokens on the client thread; replay must not
	// enter that lookup concurrently with next-frame material preparation.
	const char *shaderDLL = g_pMaterialSystemHardwareConfig->GetShaderDLLName();
	const bool bDX12 = shaderDLL && !V_stricmp( shaderDLL, "stdshader_dx12" );
	for ( int i = 0; i < PPMAT_COUNT; ++i )
	{
		PostProcessMaterialEntry &entry = s_PostProcessMaterials[i];
		if ( !entry.name || ( i == PPMAT_MOTION_BLUR_MV && !bDX12 ) )
			continue;
		if ( !entry.material.IsValid() )
			entry.material.Init( entry.name, TEXTURE_GROUP_OTHER, true );
		entry.material->GetMappingWidth(); // Build snapshots/proxies here, not during replay.
		for ( int var = 0; var < PPVAR_COUNT; ++var )
		{
			if ( !( entry.varMask & ( 1u << var ) ) )
				continue;
			bool found = false;
			entry.material->FindVar( s_PostProcessVarNames[var], &found, var != PPVAR_DISABLED );
			entry.found[var] = found;
			if ( found )
				entry.material->FindVarFast( s_PostProcessVarNames[var], &entry.tokens[var] );
		}
	}

	PostProcessingPass *lists[] = { HDRFinal_Float, HDRFinal_Float_NoBloom, HDRSimulate_NonHDR };
	for ( int i = 0; i < ARRAYSIZE( lists ); ++i )
	{
		for ( PostProcessingPass *pass = lists[i]; pass->material_name; ++pass )
		{
			if ( !pass->m_mat_ref.IsValid() )
				pass->m_mat_ref.Init( pass->material_name, TEXTURE_GROUP_OTHER, true );
			pass->m_mat_ref->GetMappingWidth();
		}
	}
}

static void SetRenderTargetAndViewPort(ITexture *rt)
{
	tmZone( TELEMETRY_LEVEL0, TMZF_NONE, "%s", __FUNCTION__ );

	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->SetRenderTarget(rt);
	pRenderContext->Viewport(0,0,rt->GetActualWidth(),rt->GetActualHeight());
}

#if defined( L4D2 )
#include "l4d2/effects/terror_depth_of_field.inc"
#endif

#define FILTER_KERNEL_SLOP 20

// Note carefully about the downsampling: the first downsampling samples from the full rendertarget
// down to a temp. When doing this sampling, the texture source clamping will take care of the out
// of bounds sampling done because of the filter kernels's width. However, on any of the subsequent
// sampling operations, we will be sampling from a partially filled render target. So, texture
// coordinate clamping cannot help us here. So, we need to always render a few more pixels to the
// destination than we actually intend to, so as to replicate the border pixels so that garbage
// pixels do not get sucked into the sampling. To deal with this, we always add FILTER_KERNEL_SLOP
// to our widths/heights if there is room for them in the destination.
static void DrawScreenSpaceRectangleWithSlop( 
	ITexture *dest_rt,
	IMaterial *pMaterial,
	int destx, int desty,
	int width, int height,
	float src_texture_x0, float src_texture_y0,			// which texel you want to appear at
	// destx/y
	float src_texture_x1, float src_texture_y1,			// which texel you want to appear at
	// destx+width-1, desty+height-1
	int src_texture_width, int src_texture_height		// needed for fixup
	)
{
	// add slop
	int slopwidth = width + FILTER_KERNEL_SLOP; //min(dest_rt->GetActualWidth()-destx,width+FILTER_KERNEL_SLOP);
	int slopheight = height + FILTER_KERNEL_SLOP; //min(dest_rt->GetActualHeight()-desty,height+FILTER_KERNEL_SLOP);

	// adjust coordinates for slop
	src_texture_x1 = FLerp( src_texture_x0, src_texture_x1, destx, destx + width - 1, destx + slopwidth - 1 );
	src_texture_y1 = FLerp( src_texture_y0, src_texture_y1, desty, desty + height - 1, desty + slopheight - 1 );
	width = slopwidth;
	height = slopheight;

	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->DrawScreenSpaceRectangle( pMaterial, destx, desty, width, height,
											  src_texture_x0, src_texture_y0,
											  src_texture_x1, src_texture_y1,
											  src_texture_width, src_texture_height );
}

enum Histogram_entry_state_t
{
	HESTATE_INITIAL=0,
	HESTATE_FIRST_QUERY_IN_FLIGHT,
	HESTATE_QUERY_IN_FLIGHT,
	HESTATE_QUERY_DONE,
};

#define N_LUMINANCE_RANGES 31
#define N_LUMINANCE_RANGES_NEW 17
#define MAX_QUERIES_PER_FRAME 1

class CHistogram_entry_t
{
public:
	Histogram_entry_state_t m_state;
	OcclusionQueryObjectHandle_t m_occ_handle;				// the occlusion query handle
	int m_frame_queued;										// when this query was last queued
	int m_npixels;										   // # of pixels this histogram represents
	int m_npixels_in_range;
	float m_min_lum, m_max_lum;					 // the luminance range this entry was queried with
	float m_minx, m_miny, m_maxx, m_maxy;				// range is 0..1 in fractions of the screen

	bool ContainsValidData( void )
	{
		return ( m_state == HESTATE_QUERY_DONE ) || ( m_state == HESTATE_QUERY_IN_FLIGHT );
	}

	void IssueQuery( int frm_num );
};

// One sampleable copy of the bound HDR frame per frame. Motion blur and the histogram both need it;
// whichever runs first copies, the other reuses that texture.
static int s_nBoundFrameCopyFrame = -1;
static void CopyBoundFrameForEpoch( int x, int y, int w, int h )
{
	CMatRenderContextPtr pRenderContext( materials );
	const int nBackendFrame = MotionVectorsDX12_FrameEpoch();
	const int nFrame = nBackendFrame >= 0 ? nBackendFrame : gpGlobals->framecount;
	if ( s_nBoundFrameCopyFrame != nFrame )
	{
		UpdateScreenEffectTexture( 1, x, y, w, h, true );
		pRenderContext->SetFrameBufferCopyTexture( GetFullFrameFrameBufferTexture( 1 ), 0 );
		s_nBoundFrameCopyFrame = nFrame;
	}
}

static ITexture *AcquireBoundFrameCopy( IMatRenderContext *pRenderContext, int x, int y, int w, int h )
{
	if ( g_pMaterialSystemHardwareConfig->GetHDRType() != HDR_TYPE_FLOAT )
		return NULL;
	ITexture *pScene = GetFullFrameFrameBufferTexture( 0 );
	ITexture *pCopy = GetFullFrameFrameBufferTexture( 1 );
	ITexture *pCurrent = pRenderContext->GetRenderTarget();
	if ( !pScene || !pCopy || !pCurrent || pCopy->IsError() || pCopy == pCurrent )
		return NULL;
	if ( pCopy->GetImageFormat() != pScene->GetImageFormat()
		|| pCopy->GetActualWidth() != pScene->GetActualWidth()
		|| pCopy->GetActualHeight() != pScene->GetActualHeight() )
		return NULL;
	// Cache decisions belong to ordered replay too: gpGlobals can already be preparing
	// the next frame while the render owner executes this frame's motion blur/histogram.
	if ( ICallQueue *pQueue = pRenderContext->GetCallQueue() )
		pQueue->QueueCall( CopyBoundFrameForEpoch, x, y, w, h );
	else
		CopyBoundFrameForEpoch( x, y, w, h );
	return pCopy;
}

static void RetargetBaseTexture( PostProcessMaterialId material, ITexture *pTexture, IMaterialVar *&pBase, ITexture *&pSaved )
{
	pBase = NULL;
	pSaved = NULL;
	if ( !pTexture )
		return;
	pBase = PostProcessVar( material, PPVAR_BASE );
	if ( !pBase )
		return;
	pSaved = pBase->GetTextureValue();
	pBase->SetTextureValue( pTexture );
}


void CHistogram_entry_t::IssueQuery( int frm_num )
{
	CMatRenderContextPtr pRenderContext( materials );
	if ( !m_occ_handle )
	{
		m_occ_handle = pRenderContext->CreateOcclusionQueryObject();
	}

	int xl, yl, dest_width, dest_height;
	pRenderContext->GetViewport( xl, yl, dest_width, dest_height );

#if defined( L4D2 )
	// Native C1C5FE0 includes the entire dark and bright tails.
	float flTestRangeMin = ( m_min_lum == 0.0f ) ? -1.0e20f : m_min_lum;
	float flTestRangeMax = ( m_max_lum == 1.0f ) ? 1.0e20f : m_max_lum;
#else
	// Find min and max gamma-space test range
	float flTestRangeMin = m_min_lum;
	float flTestRangeMax = ( m_max_lum == 1.0f ) ? 10000.0f : m_max_lum; // Count all pixels >1.0 as 1.0
#endif

	// First, set stencil bits where the colors match
	IMaterial *test_mat = PostProcessMaterial( PPMAT_LUMINANCE );
	IMaterialVar *pMinVar = PostProcessVar( PPMAT_LUMINANCE, PPVAR_C0_X );
	IMaterialVar *pMaxVar = PostProcessVar( PPMAT_LUMINANCE, PPVAR_C0_Y );
	if ( !test_mat || !pMinVar || !pMaxVar )
		return;
	pMinVar->SetFloatValue( flTestRangeMin );
	pMaxVar->SetFloatValue( flTestRangeMax );

	int scrx_min = FLerp( xl, ( xl + dest_width - 1 ), 0, 1, m_minx );
	int scrx_max = FLerp( xl, ( xl + dest_width - 1 ), 0, 1, m_maxx );
	int scry_min = FLerp( yl, ( yl + dest_height - 1 ), 0, 1, m_miny );
	int scry_max = FLerp( yl, ( yl + dest_height - 1 ), 0, 1, m_maxy );

	float exposure_width_scale, exposure_height_scale;

#if !defined( L4D2 )
	// now, shrink region of interest if the flashlight is on
	if ( g_bFlashlightIsOn )
	{
		exposure_width_scale = ( 0.5f * ( 1.0f - mat_exposure_center_region_x_flashlight.GetFloat() ) );
		exposure_height_scale = ( 0.5f * ( 1.0f - mat_exposure_center_region_y_flashlight.GetFloat() ) );
	}
	else
#endif
	{
		exposure_width_scale = ( 0.5f * ( 1.0f - mat_exposure_center_region_x.GetFloat() ) );
		exposure_height_scale = ( 0.5f * ( 1.0f - mat_exposure_center_region_y.GetFloat() ) );
	}
	int skip_edgex = ( 1 + scrx_max - scrx_min ) * exposure_width_scale;
	int skip_edgey = ( 1 + scry_max - scry_min ) * exposure_height_scale;

	// The SDK comparison shader needs a scale; native L4D2 compares the
	// already-exposed scene directly, so retain unity there.
	float tscale = 1.0;
#if !defined( L4D2 )
	if ( g_pMaterialSystemHardwareConfig->GetHDRType() == HDR_TYPE_FLOAT )
	{
		tscale = pRenderContext->GetToneMappingScaleLinear().x;
	}
#endif
	IMaterialVar *use_t_scale = PostProcessVar( PPMAT_LUMINANCE, PPVAR_C0_Z );
	if ( !use_t_scale )
		return;
	use_t_scale->SetFloatValue( tscale );

	m_npixels = ( 1 + scrx_max - scrx_min ) * ( 1 + scry_max - scry_min );

	if ( mat_tonemapping_occlusion_use_stencil.GetInt() )
	{
		pRenderContext->SetStencilWriteMask( 1 );

		// AV - We don't need to clear stencil here because it's already been cleared at the beginning of the frame
		//pRenderContext->ClearStencilBufferRectangle( scrx_min, scry_min, scrx_max, scry_max, 0 );

		pRenderContext->SetStencilEnable( true );
		pRenderContext->SetStencilPassOperation( STENCILOPERATION_REPLACE );
		pRenderContext->SetStencilCompareFunction( STENCILCOMPARISONFUNCTION_ALWAYS );
		pRenderContext->SetStencilFailOperation( STENCILOPERATION_KEEP );
		pRenderContext->SetStencilZFailOperation( STENCILOPERATION_KEEP );
		pRenderContext->SetStencilReferenceValue( 1 );
	}
	else
	{
		pRenderContext->BeginOcclusionQueryDrawing( m_occ_handle );
	}

	scrx_min += skip_edgex;
	scry_min += skip_edgey;
	scrx_max -= skip_edgex;
	scry_max -= skip_edgey;
	IMaterialVar *pBase = NULL;
	ITexture *pSavedBase = NULL;
	RetargetBaseTexture( PPMAT_LUMINANCE, AcquireBoundFrameCopy( pRenderContext, xl, yl, dest_width, dest_height ), pBase, pSavedBase );
	int source_width = dest_width;
	int source_height = dest_height;
#if defined( L4D2 )
	// UVs address the full target; destination coordinates are viewport-local.
	pRenderContext->GetRenderTargetDimensions( source_width, source_height );
#else
	xl = yl = 0;
#endif
	pRenderContext->DrawScreenSpaceRectangle( test_mat,
											  scrx_min - xl, scry_min - yl,
											  1 + scrx_max - scrx_min,
											  1 + scry_max - scry_min,
											  scrx_min, scry_min,
											  scrx_max, scry_max,
											  source_width, source_height );

	if ( mat_tonemapping_occlusion_use_stencil.GetInt() )
	{
		// now, start counting how many pixels had their stencil bit set via an occlusion query
		pRenderContext->BeginOcclusionQueryDrawing( m_occ_handle );

		// now, issue an occlusion query using stencil as the mask
		pRenderContext->SetStencilEnable( true );
		pRenderContext->SetStencilTestMask( 1 );
		pRenderContext->SetStencilPassOperation( STENCILOPERATION_KEEP );
		pRenderContext->SetStencilCompareFunction( STENCILCOMPARISONFUNCTION_EQUAL );
		pRenderContext->SetStencilFailOperation( STENCILOPERATION_KEEP );
		pRenderContext->SetStencilZFailOperation( STENCILOPERATION_KEEP );
		pRenderContext->SetStencilReferenceValue( 1 );
		IMaterial *stest_mat = PostProcessMaterial( PPMAT_NO_PIXEL_WRITE );
		pRenderContext->DrawScreenSpaceRectangle( stest_mat,
												  scrx_min, scry_min,
												  1 + scrx_max - scrx_min,
												  1 + scry_max - scry_min,
												  scrx_min, scry_min,
												  scrx_max, scry_max,
												  source_width, source_height );
		pRenderContext->SetStencilEnable( false );
#if defined( L4D2 )
		pRenderContext->SetStencilCompareFunction( STENCILCOMPARISONFUNCTION_ALWAYS );
		pRenderContext->SetStencilReferenceValue( 0 );
		pRenderContext->SetStencilTestMask( 0xffffffff );
		pRenderContext->SetStencilWriteMask( 0xffffffff );
#endif
	}
	pRenderContext->EndOcclusionQueryDrawing( m_occ_handle );
	if ( pBase && pSavedBase )
		pBase->SetTextureValue( pSavedBase );
	if ( m_state == HESTATE_INITIAL )
		m_state = HESTATE_FIRST_QUERY_IN_FLIGHT;
	else
		m_state = HESTATE_QUERY_IN_FLIGHT;
	m_frame_queued = frm_num;
}

#define HISTOGRAM_BAR_SIZE 200

class CLuminanceHistogramSystem
{
	CHistogram_entry_t CurHistogram[N_LUMINANCE_RANGES];
	int cur_query_frame;
public:
	float FindLocationOfPercentBrightPixels( float flPercentBrightPixels, float flPercentTarget );

	float GetTargetTonemapScalar( bool bGetIdealTargetForDebugMode );

	void Update( void );

	void DisplayHistogram( void );

	void UpdateLuminanceRanges( void );

	CLuminanceHistogramSystem(void)
	{
		UpdateLuminanceRanges();
	}
};

void CLuminanceHistogramSystem::Update( void )
{
	UpdateLuminanceRanges();

	// find which histogram entries should have something done this frame
	int n_queries_issued_this_frame=0;
	cur_query_frame++;

	int nNumRanges = N_LUMINANCE_RANGES;
	if ( mat_tonemap_algorithm.GetInt() == 1 )
		nNumRanges = N_LUMINANCE_RANGES_NEW;

	for ( int i=0; i<nNumRanges; i++ )
	{
		switch ( CurHistogram[i].m_state )
		{
			case HESTATE_INITIAL:
				if ( n_queries_issued_this_frame<MAX_QUERIES_PER_FRAME )
				{
					CurHistogram[i].IssueQuery(cur_query_frame);
					n_queries_issued_this_frame++;
				}
				break;

			case HESTATE_FIRST_QUERY_IN_FLIGHT:
			case HESTATE_QUERY_IN_FLIGHT:
				if ( cur_query_frame > CurHistogram[i].m_frame_queued + 2 )
				{
					CMatRenderContextPtr pRenderContext( materials );
					int np = pRenderContext->OcclusionQuery_GetNumPixelsRendered(
						CurHistogram[i].m_occ_handle );
					if ( np !=- 1 ) 						// -1=query not finished. wait until
						// next time
					{
						CurHistogram[i].m_npixels_in_range = np;
						// 						    if (mat_debug_autoexposure.GetInt())
						// 								Warning("min=%f max=%f np = %d\n",CurHistogram[i].m_min_lum,CurHistogram[i].m_max_lum,np);
						CurHistogram[i].m_state = HESTATE_QUERY_DONE;
					}
				}
				break;
		}
	}
	// now, issue queries for the oldest finished queries we have
	while( n_queries_issued_this_frame < MAX_QUERIES_PER_FRAME )
	{
		nNumRanges = N_LUMINANCE_RANGES;
		if ( mat_tonemap_algorithm.GetInt() == 1 )
			nNumRanges = N_LUMINANCE_RANGES_NEW;

		int oldest_so_far =- 1;
		for( int i = 0;i < nNumRanges;i ++ )
			if ( ( CurHistogram[i].m_state == HESTATE_QUERY_DONE ) &&
				 ( ( oldest_so_far ==- 1 ) ||
				   ( CurHistogram[i].m_frame_queued <
					 CurHistogram[oldest_so_far].m_frame_queued ) ) )
				oldest_so_far = i;
		if ( oldest_so_far ==- 1 )								// nothing to do
			break;
		CurHistogram[oldest_so_far].IssueQuery( cur_query_frame );
		n_queries_issued_this_frame ++;
	}
}

float CLuminanceHistogramSystem::FindLocationOfPercentBrightPixels( float flPercentBrightPixels, float flPercentTargetToSnapToIfInSameBin = -1.0f )
{
	if ( mat_tonemap_algorithm.GetInt() == 1 ) // New algorithm
	{
		int nTotalValidPixels = 0;
		for ( int i=0; i<N_LUMINANCE_RANGES_NEW-1; i++ )
		{
			if ( CurHistogram[i].ContainsValidData() )
			{
				nTotalValidPixels += CurHistogram[i].m_npixels_in_range;
			}
		}

		if ( nTotalValidPixels == 0 )
		{
			return -1.0f;
		}

		// Find where percent range border is
		float flTotalPercentRangeTested = 0.0f;
		float flTotalPercentPixelsTested = 0.0f;
		for ( int i=N_LUMINANCE_RANGES_NEW-2; i>=0; i-- ) // Start at the bright end
		{
			if ( !CurHistogram[i].ContainsValidData() )
				return -1.0f;

			float flPixelPercentNeeded = ( flPercentBrightPixels / 100.0f ) - flTotalPercentPixelsTested;
			float flThisBinPercentOfTotalPixels = float( CurHistogram[i].m_npixels_in_range ) / float( nTotalValidPixels );
			float flThisBinLuminanceRange = CurHistogram[i].m_max_lum - CurHistogram[i].m_min_lum;
			if ( flThisBinPercentOfTotalPixels >= flPixelPercentNeeded ) // We found the bin needed
			{
				if ( flPercentTargetToSnapToIfInSameBin >= 0.0f )
				{
					if ( ( CurHistogram[i].m_min_lum <= ( flPercentTargetToSnapToIfInSameBin / 100.0f ) ) && ( CurHistogram[i].m_max_lum >= ( flPercentTargetToSnapToIfInSameBin / 100.0f ) ) )
					{
						// Sticky bin...We're in the same bin as the target so keep the tonemap scale where it is
						return ( flPercentTargetToSnapToIfInSameBin / 100.0f );
					}
				}

				float flPercentOfThesePixelsNeeded = flPixelPercentNeeded / flThisBinPercentOfTotalPixels;
				float flPercentLocationOfBorder = 1.0f - ( flTotalPercentRangeTested + ( flThisBinLuminanceRange * flPercentOfThesePixelsNeeded ) );
				flPercentLocationOfBorder = MAX( CurHistogram[i].m_min_lum, MIN( CurHistogram[i].m_max_lum, flPercentLocationOfBorder ) ); // Clamp to this bin just in case
				return flPercentLocationOfBorder;
			}

			flTotalPercentPixelsTested += flThisBinPercentOfTotalPixels;
			flTotalPercentRangeTested += flThisBinLuminanceRange;
		}

		return -1.0f;
	}
	else
	{
		// Don't know what to do for other algorithms yet
		return -1.0f;
	}
}

float CLuminanceHistogramSystem::GetTargetTonemapScalar( bool bGetIdealTargetForDebugMode = false )
{
	if ( mat_tonemap_algorithm.GetInt() == 1 ) // New algorithm
	{
		float flPercentLocationOfTarget;
		if ( bGetIdealTargetForDebugMode == true)
#if defined( L4D2 )
			flPercentLocationOfTarget = FindLocationOfPercentBrightPixels( Terror_TonemapPercentBrightPixels() );
#else
			flPercentLocationOfTarget = FindLocationOfPercentBrightPixels( mat_tonemap_percent_bright_pixels.GetFloat() ); // Don't pass in the second arg so the scalar doesn't snap to a bin
#endif
		else
#if defined( L4D2 )
			flPercentLocationOfTarget = FindLocationOfPercentBrightPixels( Terror_TonemapPercentBrightPixels(), Terror_TonemapPercentTarget() );
#else
			flPercentLocationOfTarget = FindLocationOfPercentBrightPixels( mat_tonemap_percent_bright_pixels.GetFloat(), mat_tonemap_percent_target.GetFloat() );
#endif
		if ( flPercentLocationOfTarget < 0.0f ) // This is the return error code
		{
#if defined( L4D2 )
			flPercentLocationOfTarget = Terror_TonemapPercentTarget() / 100.0f;
#else
			flPercentLocationOfTarget = mat_tonemap_percent_target.GetFloat() / 100.0f; // Pretend we're at the target
#endif
		}

		// Make sure this is > 0.0f
		flPercentLocationOfTarget = MAX( 0.0001f, flPercentLocationOfTarget );

		// Compute target scalar
#if defined( L4D2 )
		float flTargetScalar = ( Terror_TonemapPercentTarget() / 100.0f ) / flPercentLocationOfTarget;
#else
		float flTargetScalar = ( mat_tonemap_percent_target.GetFloat() / 100.0f ) / flPercentLocationOfTarget;
#endif

		// Compute secondary target scalar
		float flAverageLuminanceLocation = FindLocationOfPercentBrightPixels( 50.0f );
		if ( flAverageLuminanceLocation > 0.0f )
		{
#if defined( L4D2 )
			float flTargetScalar2 = ( Terror_TonemapMinAvgLum() / 100.0f ) / flAverageLuminanceLocation;
#else
			float flTargetScalar2 = ( mat_tonemap_min_avglum.GetFloat() / 100.0f ) / flAverageLuminanceLocation;
#endif

			// Only override it if it's trying to brighten the image more than the primary algorithm
			if ( flTargetScalar2 > flTargetScalar )
			{
				flTargetScalar = flTargetScalar2;
			}
		}

		// Apply this against last frames scalar
#if defined( L4D2 )
		float flLastScale = g_TerrorTonemapState.GetCurrent();
#else
		CMatRenderContextPtr pRenderContext( materials );
		float flLastScale = pRenderContext->GetToneMappingScaleLinear().x;
#endif
		flTargetScalar *= flLastScale;

		flTargetScalar = MAX( 0.001f, flTargetScalar );
		return flTargetScalar;
	}
	else // Original tonemapping
	{
		float average_luminance = 0.5f;

		float total = 0;
		int total_pixels = 0;
		float scale_value = 1.0;
		if ( CurHistogram[N_LUMINANCE_RANGES-1].ContainsValidData() )
		{
			scale_value = CurHistogram[N_LUMINANCE_RANGES-1].m_npixels * ( 1.0f / CurHistogram[N_LUMINANCE_RANGES-1].m_npixels_in_range );

			if ( mat_debug_autoexposure.GetInt() )
			{
				engine->Con_NPrintf( 20, "Scale value = %f", scale_value );
				//Warning( "scale value=%f\n", scale_value );
			}
		}
		else
			average_luminance = 0.5;

		if ( !IsFinite( scale_value ) )
			scale_value = 1.0f;

		for ( int i=0; i<N_LUMINANCE_RANGES-1; i++ )
		{
			if ( CurHistogram[i].ContainsValidData() )
			{
				total += scale_value * CurHistogram[i].m_npixels_in_range * AVG( CurHistogram[i].m_min_lum, CurHistogram[i].m_max_lum );
				total_pixels += CurHistogram[i].m_npixels;
			}
			else
				average_luminance = 0.5; // always return 0.5 until we've queried a whole frame
		}
		if ( total_pixels > 0 )
			average_luminance = total * ( 1.0 / total_pixels );
		else
			average_luminance = 0.5;

		// Make sure this is > 0.0f
		average_luminance = MAX( 0.0001f, average_luminance );

		// Compute target scalar
		float flTargetScalar = 0.005 / average_luminance;

		return flTargetScalar;
	}
}

static float GetCurrentBloomScale( void )
{
	// Use the appropriate bloom scale settings.  Mapmakers's overrides the convar settings.
	float flCurrentBloomScale = 1.0f;
	if ( g_bUseCustomBloomScale )
	{
		flCurrentBloomScale = g_flCustomBloomScale;
	}
	else
	{
		flCurrentBloomScale = mat_bloomscale.GetFloat();
	}
	return flCurrentBloomScale;
}

static void GetExposureRange( float *flAutoExposureMin, float *flAutoExposureMax )
{
	// Get min
	if ( ( g_bUseCustomAutoExposureMin ) && ( g_flCustomAutoExposureMin > 0.0f ) )
	{
		*flAutoExposureMin = g_flCustomAutoExposureMin;
	}
	else
	{
		*flAutoExposureMin = mat_autoexposure_min.GetFloat();
	}

	// Get max
	if ( ( g_bUseCustomAutoExposureMax ) && ( g_flCustomAutoExposureMax > 0.0f ) )
	{
		*flAutoExposureMax = g_flCustomAutoExposureMax;
	}
	else
	{
		*flAutoExposureMax = mat_autoexposure_max.GetFloat();
	}

	// Override
	if ( mat_hdr_uncapexposure.GetInt() )
	{
#if defined( L4D2 )
		*flAutoExposureMax = 100.0f;
#else
		*flAutoExposureMax = 20.0f;
#endif
		*flAutoExposureMin = 0.0f;
	}

	// Make sure min <= max
	if ( *flAutoExposureMin > *flAutoExposureMax )
	{
		*flAutoExposureMax = *flAutoExposureMin;
	}
}

void CLuminanceHistogramSystem::UpdateLuminanceRanges( void )
{
	// Only update if our mode changed
	static int s_nCurrentBucketAlgorithm = -1;
	if ( s_nCurrentBucketAlgorithm == mat_tonemap_algorithm.GetInt() )
		return;
	s_nCurrentBucketAlgorithm = mat_tonemap_algorithm.GetInt();

	//==================================================================//
	// Force fallback to original tone mapping algorithm for these mods //
	//==================================================================//
	static bool s_bFirstTime = true;
	if ( engine == NULL )
	{
		// Force this code to get hit again so we can change algorithm based on the client
		s_nCurrentBucketAlgorithm = -1;
	}
	else if ( s_bFirstTime == true )
	{
		s_bFirstTime = false;

		// This seems like a bad idea but it's fine for now
		const char *sModsForOriginalAlgorithm[] = { "dod", "cstrike", "lostcoast", "hl1" };
		for ( int i=0; i<3; i++ )
		{
			if ( strlen( engine->GetGameDirectory() ) >= strlen( sModsForOriginalAlgorithm[i] ) )
			{
				if ( stricmp( &( engine->GetGameDirectory()[strlen( engine->GetGameDirectory() ) - strlen( sModsForOriginalAlgorithm[i] )] ), sModsForOriginalAlgorithm[i] ) == 0 )
				{
					mat_tonemap_algorithm.SetValue( 0 ); // Original algorithm
					s_nCurrentBucketAlgorithm = mat_tonemap_algorithm.GetInt();
					break;
				}
			}
		}
	}

	int nNumRanges = N_LUMINANCE_RANGES;

	if ( mat_tonemap_algorithm.GetInt() == 1 )
		nNumRanges = N_LUMINANCE_RANGES_NEW;

	cur_query_frame=0;
	for ( int bucket = 0; bucket < nNumRanges; bucket ++ )
	{
		int idx = bucket;
		CHistogram_entry_t & e = CurHistogram[idx];
		e.m_state = HESTATE_INITIAL;
		e.m_minx = 0;
		e.m_maxx = 1;
		e.m_miny = 0;
		e.m_maxy = 1;
		if ( bucket != nNumRanges-1 ) // Last bucket is special
		{
			if ( mat_tonemap_algorithm.GetInt() == 0 ) // Original algorithm
			{
				// Use a logarithmic ramp for high range in the low range
				e.m_min_lum = - 0.01 + exp( FLerp( log( .01 ), log( .01 + 1 ), 0, nNumRanges - 1, bucket ) );
				e.m_max_lum = - 0.01 + exp( FLerp( log( .01 ), log( .01 + 1 ), 0, nNumRanges - 1, bucket + 1 ) );
			}
			else
			{
				// Use even distribution
				e.m_min_lum = float( bucket ) / float( nNumRanges - 1 );
				e.m_max_lum = float( bucket + 1 ) / float( nNumRanges - 1 );

				// Use a distribution with slightly more bins in the low range
#if defined( L4D2 )
				e.m_min_lum = e.m_min_lum > 0.0f ? powf( e.m_min_lum, 2.5f ) : e.m_min_lum;
				e.m_max_lum = e.m_max_lum > 0.0f ? powf( e.m_max_lum, 2.5f ) : e.m_max_lum;
#else
				e.m_min_lum = e.m_min_lum > 0.0f ? powf( e.m_min_lum, 1.5f ) : e.m_min_lum;
				e.m_max_lum = e.m_max_lum > 0.0f ? powf( e.m_max_lum, 1.5f ) : e.m_max_lum;
#endif
			}
		}
		else
		{
			// The last bucket is used as a test to determine the return range for occlusion
			// queries to use as a scale factor. some boards (nvidia) have their occlusion
			// query return values larger when using AA.
			e.m_min_lum = 0;
			e.m_max_lum = 100000.0;
		}

		//Warning( "Bucket %d: min/max %f / %f ", bucket, e.m_min_lum, e.m_max_lum );
	}
}


void CLuminanceHistogramSystem::DisplayHistogram( void )
{
	bool bDrawTextThisFrame = true;
	if ( IsX360() )
	{
		static float s_flLastTimeUpdate = 0.0f;
		if ( int( gpGlobals->curtime ) - int( s_flLastTimeUpdate ) >= 2 )
		{
			s_flLastTimeUpdate = gpGlobals->curtime;
			bDrawTextThisFrame = true;
		}
		else
		{
			bDrawTextThisFrame = false;
		}
	}

	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->PushRenderTargetAndViewport();

	int nNumRanges = N_LUMINANCE_RANGES-1;
	if ( mat_tonemap_algorithm.GetInt() == 1 )
		nNumRanges = N_LUMINANCE_RANGES_NEW-1;

	int nMaxValidPixels = 0;
	int nTotalValidPixels = 0;
	int nTotalGraphPixelsWide = 0;
	for ( int l=0; l<nNumRanges; l++ )
	{
		CHistogram_entry_t &e = CurHistogram[l];
		if ( e.ContainsValidData() )
		{
			nTotalValidPixels += e.m_npixels_in_range;
			if ( e.m_npixels_in_range > nMaxValidPixels )
			{
				nMaxValidPixels = e.m_npixels_in_range;
			}
		}

		int width = MAX( 1, 500 * ( e.m_max_lum - e.m_min_lum ) );
		nTotalGraphPixelsWide += width + 2;
	}

	int xl, yl, dest_width, dest_height;
	pRenderContext->GetViewport( xl, yl, dest_width, dest_height );

	if ( bDrawTextThisFrame == true )
	{
		engine->Con_NPrintf( 17, "(All values in linear space)" );

#if defined( L4D2 )
		engine->Con_NPrintf( 21, "AvgLum @ %4.2f%%  Tonemap min average luminance = %4.2f%%  Using %d pixels of %d pixels on screen (%3d%%)",
			MAX( 0.0f, FindLocationOfPercentBrightPixels( 50.0f ) ) * 100.0f, Terror_TonemapMinAvgLum(),
			nTotalValidPixels, ( dest_width * dest_height ), int( float( nTotalValidPixels ) * 100.0f / float( dest_width * dest_height ) ) );
#else
		engine->Con_NPrintf( 21, "AvgLum @ %4.2f%%  mat_tonemap_min_avglum = %4.2f%%  Using %d pixels of %d pixels on screen (%3d%%)", 
			MAX( 0.0f, FindLocationOfPercentBrightPixels( 50.0f ) ) * 100.0f, mat_tonemap_min_avglum.GetFloat(),
			nTotalValidPixels, ( dest_width * dest_height ), int( float( nTotalValidPixels ) * 100.0f / float( dest_width * dest_height ) ) );
#endif
		engine->Con_NPrintf( 23, "BloomScale = %4.2f  mat_hdr_manual_tonemap_rate = %4.2f  mat_accelerate_adjust_exposure_down = %4.2f", 
			GetCurrentBloomScale(), mat_hdr_manual_tonemap_rate.GetFloat(), mat_accelerate_adjust_exposure_down.GetFloat() );
	}

	if ( mat_tonemap_algorithm.GetInt() == 1 ) // New algorithm only
	{
		float vTotalPixelsAndHigher[N_LUMINANCE_RANGES];
		for ( int i=0; i<nNumRanges; i++ )
		{
			vTotalPixelsAndHigher[i] = CurHistogram[nNumRanges-1-i].m_npixels_in_range;
			if ( i > 0 )
			{
				vTotalPixelsAndHigher[i] += vTotalPixelsAndHigher[i-1];
			}
		}

		/* // This code works when N_LUMINANCE_RANGES_NEW = 11
		if ( bDrawTextThisFrame == true )
		{
			engine->Con_NPrintf( 17, "%04.2f         %04.2f         %04.2f         %04.2f         %04.2f         %04.2f         %04.2f         %04.2f         %04.2f         %04.2f   ",
			   100.0f * float( vTotalPixelsAndHigher[9] ) / float( nTotalValidPixels ),
			   100.0f * float( vTotalPixelsAndHigher[8] ) / float( nTotalValidPixels ),
			   100.0f * float( vTotalPixelsAndHigher[7] ) / float( nTotalValidPixels ),
			   100.0f * float( vTotalPixelsAndHigher[6] ) / float( nTotalValidPixels ),
			   100.0f * float( vTotalPixelsAndHigher[5] ) / float( nTotalValidPixels ),
			   100.0f * float( vTotalPixelsAndHigher[4] ) / float( nTotalValidPixels ),
			   100.0f * float( vTotalPixelsAndHigher[3] ) / float( nTotalValidPixels ),
			   100.0f * float( vTotalPixelsAndHigher[2] ) / float( nTotalValidPixels ),
			   100.0f * float( vTotalPixelsAndHigher[1] ) / float( nTotalValidPixels ),
			   100.0f * float( vTotalPixelsAndHigher[0] ) / float( nTotalValidPixels ) );

			engine->Con_NPrintf( 15, "%04.2f         %04.2f         %04.2f         %04.2f         %04.2f         %04.2f         %04.2f         %04.2f         %04.2f         %04.2f   ",
			   100.0f * float( CurHistogram[nNumRanges-1-9].m_npixels_in_range ) / float( nTotalValidPixels ),
			   100.0f * float( CurHistogram[nNumRanges-1-8].m_npixels_in_range ) / float( nTotalValidPixels ),
			   100.0f * float( CurHistogram[nNumRanges-1-7].m_npixels_in_range ) / float( nTotalValidPixels ),
			   100.0f * float( CurHistogram[nNumRanges-1-6].m_npixels_in_range ) / float( nTotalValidPixels ),
			   100.0f * float( CurHistogram[nNumRanges-1-5].m_npixels_in_range ) / float( nTotalValidPixels ),
			   100.0f * float( CurHistogram[nNumRanges-1-4].m_npixels_in_range ) / float( nTotalValidPixels ),
			   100.0f * float( CurHistogram[nNumRanges-1-3].m_npixels_in_range ) / float( nTotalValidPixels ),
			   100.0f * float( CurHistogram[nNumRanges-1-2].m_npixels_in_range ) / float( nTotalValidPixels ),
			   100.0f * float( CurHistogram[nNumRanges-1-1].m_npixels_in_range ) / float( nTotalValidPixels ),
			   100.0f * float( CurHistogram[nNumRanges-1-0].m_npixels_in_range ) / float( nTotalValidPixels ) );
			}
		//*/
	}
	else
	{
		if ( bDrawTextThisFrame == true )
		{
			engine->Con_NPrintf( 17, "" );
			engine->Con_NPrintf( 15, "" );
		}
	}

	int xpStart = dest_width - nTotalGraphPixelsWide - 10;
	if ( IsX360() )
	{
		xpStart -= 50;
	}

	int xp = xpStart;
	for ( int l=0; l<nNumRanges; l++ )
	{
		int np = 0;
		CHistogram_entry_t &e = CurHistogram[l];
		if ( e.ContainsValidData() )
			np += e.m_npixels_in_range;
		int width = MAX( 1, 500 * ( e.m_max_lum - e.m_min_lum ) );

		//Warning( "Bucket %d: min/max %f / %f.  m_npixels_in_range=%d   m_npixels=%d\n", l, e.m_min_lum, e.m_max_lum, e.m_npixels_in_range, e.m_npixels );

		if ( np )
		{
			int height = MAX( 1, MIN( HISTOGRAM_BAR_SIZE, ( (float)np / (float)nMaxValidPixels ) * HISTOGRAM_BAR_SIZE ) );

			pRenderContext->ClearColor3ub( 255, 0, 0 );
			pRenderContext->Viewport( xp, 4 + HISTOGRAM_BAR_SIZE - height, width, height );
			pRenderContext->ClearBuffers( true, true );
		}
		else
		{
			int height = 1;
			pRenderContext->ClearColor3ub( 0, 0, 255 );
			pRenderContext->Viewport( xp, 4 + HISTOGRAM_BAR_SIZE - height, width, height );
			pRenderContext->ClearBuffers( true, true );
		}
		xp += width + 2;
	}

	if ( mat_tonemap_algorithm.GetInt() == 1 ) // New algorithm only
	{
#if defined( L4D2 )
		float flYellowTargetPixelStart = ( xpStart + ( float( nTotalGraphPixelsWide ) * Terror_TonemapPercentTarget() / 100.0f ) );
		float flYellowAveragePixelStart = ( xpStart + ( float( nTotalGraphPixelsWide ) * Terror_TonemapMinAvgLum() / 100.0f ) );
#else
		float flYellowTargetPixelStart = ( xpStart + ( float( nTotalGraphPixelsWide ) * mat_tonemap_percent_target.GetFloat() / 100.0f ) );
		float flYellowAveragePixelStart = ( xpStart + ( float( nTotalGraphPixelsWide ) * mat_tonemap_min_avglum.GetFloat() / 100.0f ) );
#endif

#if defined( L4D2 )
		float flTargetPixelStart = ( xpStart + ( float( nTotalGraphPixelsWide ) * FindLocationOfPercentBrightPixels( Terror_TonemapPercentBrightPixels(), Terror_TonemapPercentTarget() ) ) );
#else
		float flTargetPixelStart = ( xpStart + ( float( nTotalGraphPixelsWide ) * FindLocationOfPercentBrightPixels( mat_tonemap_percent_bright_pixels.GetFloat(), mat_tonemap_percent_target.GetFloat() ) ) );
#endif
		float flAveragePixelStart = ( xpStart + ( float( nTotalGraphPixelsWide ) * FindLocationOfPercentBrightPixels( 50.0f ) ) );

		// Draw target yellow border bar
		int height = HISTOGRAM_BAR_SIZE;

		// Green is current percent target location
		pRenderContext->Viewport( flYellowTargetPixelStart, 4 + HISTOGRAM_BAR_SIZE - height, 4, height );
		pRenderContext->ClearColor3ub( 200, 200, 0 );
		pRenderContext->ClearBuffers( true, true );

		pRenderContext->Viewport( flTargetPixelStart, 4 + HISTOGRAM_BAR_SIZE - height, 4, height );
		pRenderContext->ClearColor3ub( 0, 255, 0 );
		pRenderContext->ClearBuffers( true, true );

		// Blue is average luminance location
		pRenderContext->Viewport( flYellowAveragePixelStart, 4 + HISTOGRAM_BAR_SIZE - height, 4, height );
		pRenderContext->ClearColor3ub( 200, 200, 0 );
		pRenderContext->ClearBuffers( true, true );

		pRenderContext->Viewport( flAveragePixelStart, 4 + HISTOGRAM_BAR_SIZE - height, 4, height );
		pRenderContext->ClearColor3ub( 0, 200, 200 );
		pRenderContext->ClearBuffers( true, true );
	}

	// Show actual tonemap value
	if ( 1 )
	{
		float flAutoExposureMin;
		float flAutoExposureMax;
		GetExposureRange( &flAutoExposureMin, &flAutoExposureMax );

		float flBarWidth = 600.0f;
		float flBarStart = dest_width - flBarWidth - 10.0f;
		if ( IsX360() )
		{
			flBarStart -= 50;
		}

		pRenderContext->Viewport( flBarStart, 4 + HISTOGRAM_BAR_SIZE - 4 + 75, flBarWidth, 4 );
		pRenderContext->ClearColor3ub( 200, 200, 200 );
		pRenderContext->ClearBuffers( true, true );

		pRenderContext->Viewport( flBarStart, 4 + HISTOGRAM_BAR_SIZE - 4 + 75 + 1, flBarWidth, 2 );
		pRenderContext->ClearColor3ub( 0, 0, 0 );
		pRenderContext->ClearBuffers( true, true );

		pRenderContext->Viewport( flBarStart + ( flBarWidth * ( ( pRenderContext->GetToneMappingScaleLinear().x - flAutoExposureMin ) / ( flAutoExposureMax - flAutoExposureMin ) ) ),
								  4 + HISTOGRAM_BAR_SIZE - 4 + 75 - 6, 4, 16 );
		pRenderContext->ClearColor3ub( 255, 0, 0 );
		pRenderContext->ClearBuffers( true, true );

		if ( bDrawTextThisFrame == true )
		{
			if ( IsX360() )
				engine->Con_NPrintf( 26, "Min: %.2f  Max: %.2f", flAutoExposureMin, flAutoExposureMax );
			else
				engine->Con_NPrintf( 26, "%.2f                                                                                       %.2f                                                                                           %.2f", flAutoExposureMin, ( flAutoExposureMax + flAutoExposureMin ) / 2.0f, flAutoExposureMax );
		}
	}

	// Last bar doesn't clear properly so draw an extra pixel
	pRenderContext->Viewport( 0, 0, 1, 1 );
	pRenderContext->ClearColor3ub( 0, 0, 0 );
	pRenderContext->ClearBuffers( true, true );

	pRenderContext->PopRenderTargetAndViewport();
}


static CLuminanceHistogramSystem g_HDR_HistogramSystem;

#if !defined( L4D2 )
static float s_MovingAverageToneMapScale[10] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
static int s_nInAverage = 0;
#endif

void ResetToneMapping(float value)
{
	CMatRenderContextPtr pRenderContext( materials );
#if defined( L4D2 )
	float minimum, maximum;
	GetExposureRange( &minimum, &maximum );
	g_TerrorTonemapState.Reset( value, minimum, maximum );
#else
	s_nInAverage = 0;
#endif
	pRenderContext->ResetToneMappingScale(value);
}

static ConVar mat_force_tonemap_scale( "mat_force_tonemap_scale", "0.0", 
#if defined( L4D2 )
	0x00004008
#else
	FCVAR_CHEAT
#endif
 );

#if defined( L4D2 )
static bool s_bTerrorExposureOverride;
static float s_flTerrorExposureOverride;
float Terror_GetCurrentExposure() { return g_TerrorTonemapState.GetCurrent(); } //1C5CC0
void Terror_SetExposureOverride( bool enabled, float value ) //1C5CD0
{
	s_bTerrorExposureOverride = enabled;
	s_flTerrorExposureOverride = value;
}
void Terror_ApplyToneMapping( IMatRenderContext *context )
{
	if ( g_pMaterialSystemHardwareConfig->GetHDRType() == HDR_TYPE_NONE )
	{
		context->SetToneMappingScaleLinear( Vector( 1, 1, 1 ) );
		return;
	}
	const float forced = mat_fullbright.GetInt() == 1 ? 1.0f : mat_force_tonemap_scale.GetFloat();
	if ( forced > 0.0f )
	{
		float minimum, maximum; GetExposureRange( &minimum, &maximum );
		g_TerrorTonemapState.Reset( forced, minimum, maximum );
	}
	else if ( s_bTerrorExposureOverride )
	{
		float minimum, maximum; GetExposureRange( &minimum, &maximum );
		//1C738C: compare polarity retains maximum for unordered override.
		float selected = maximum;
		if ( s_flTerrorExposureOverride <= maximum ) selected = minimum <= s_flTerrorExposureOverride ? s_flTerrorExposureOverride : minimum;
		g_TerrorTonemapState.Reset( selected, minimum, maximum );
	}
	const float scale = g_TerrorTonemapState.GetCurrent();
	// Unlike L4D2, the SDK's vector setter only updates the shader API.
	// Keep the render-context scale in sync for water reflection save/restore.
	context->ResetToneMappingScale( scale );
}
#endif

static void SetToneMapScale(IMatRenderContext *pRenderContext, float newvalue, float minvalue, float maxvalue)
{
	Assert( IsFinite( newvalue ) );
	if( !IsFinite( newvalue ) )
		return;
#if defined( L4D2 )
	g_TerrorTonemapState.Update( newvalue, minvalue, maxvalue, gpGlobals->frametime,
		mat_tonemap_algorithm.GetInt(), mat_hdr_manual_tonemap_rate.GetFloat(),
		mat_accelerate_adjust_exposure_down.GetFloat(), mat_force_tonemap_scale.GetFloat() );
	mat_hdr_tonemapscale.SetValue( g_TerrorTonemapState.GetCurrent() );
#else

	float flForcedTonemapScale = mat_force_tonemap_scale.GetFloat();

	if( mat_fullbright.GetInt() == 1 )
	{
		flForcedTonemapScale = 1.0f;
	}

	if( flForcedTonemapScale > 0.0f )
	{
		mat_hdr_tonemapscale.SetValue( flForcedTonemapScale );
		pRenderContext->ResetToneMappingScale( flForcedTonemapScale );
		return;
	}

	mat_hdr_tonemapscale.SetValue( newvalue );
	pRenderContext->SetGoalToneMappingScale( newvalue );

	if ( s_nInAverage < ARRAYSIZE( s_MovingAverageToneMapScale ))
	{
		s_MovingAverageToneMapScale[s_nInAverage ++]= newvalue;
	}
	else
	{
		// scroll, losing oldest
		for( int i = 0;i < ARRAYSIZE( s_MovingAverageToneMapScale ) - 1;i ++ )
			s_MovingAverageToneMapScale[i]= s_MovingAverageToneMapScale[i + 1];
		s_MovingAverageToneMapScale[ARRAYSIZE( s_MovingAverageToneMapScale ) - 1]= newvalue;
	}

	// now, use the average of the last tonemap calculations as our goal scale
	if ( s_nInAverage == ARRAYSIZE( s_MovingAverageToneMapScale ))	// got full buffer yet?
	{
		float avg = 0.;
		float sumweights = 0;
		int sample_pt = ARRAYSIZE( s_MovingAverageToneMapScale ) / 2;
		for( int i = 0;i < ARRAYSIZE( s_MovingAverageToneMapScale );i ++ )
		{
			float weight = abs( i - sample_pt ) * ( 1.0 / ( ARRAYSIZE( s_MovingAverageToneMapScale ) / 2 ));
			sumweights += weight;
			avg += weight * s_MovingAverageToneMapScale[i];
		}
		avg *= ( 1.0 / sumweights );
		avg = MIN( maxvalue, MAX( minvalue, avg ));
		pRenderContext->SetGoalToneMappingScale( avg );
		mat_hdr_tonemapscale.SetValue( avg );
	}
#endif
}


//=====================================================================================================================
// Engine_Post material proxy ============================================================================================
//=====================================================================================================================

#if defined( L4D2 )
static ConVar mat_software_aa_strength( "mat_software_aa_strength", "-1.0", 0x0000000A, "Software AA - perform a software anti-aliasing post-process (an alternative/supplement to MSAA). This value sets the strength of the effect: (0.0 - off), (1.0 - full)" );
static ConVar mat_software_aa_quality( "mat_software_aa_quality", "0", 0x0000000A, "Software AA quality mode: (0 - 5-tap filter), (1 - 9-tap filter)" );
static ConVar mat_software_aa_edge_threshold( "mat_software_aa_edge_threshold", "1.0", 0x0000000A, "Software AA - adjusts the sensitivity of the software AA shader's edge detection (default 1.0 - a lower value will soften more edges, a higher value will soften fewer)" );
static ConVar mat_software_aa_blur_one_pixel_lines( "mat_software_aa_blur_one_pixel_lines", "0.5", 0x0000000A, "How much software AA should blur one-pixel thick lines: (0.0 - none), (1.0 - lots)" );
static ConVar mat_software_aa_tap_offset( "mat_software_aa_tap_offset", "1.0", 0x0000000A, "Software AA - adjusts the displacement of the taps used by the software AA shader (default 1.0 - a lower value will make the image sharper, higher will make it blurrier)" );
static ConVar mat_software_aa_debug( "mat_software_aa_debug", "0", 0x0000000A, "Software AA debug mode: (0 - off), (1 - show number of 'unlike' samples: 0->black, 1->red, 2->green, 3->blue), (2 - show anti-alias blend strength), (3 - show averaged 'unlike' colour)" );
static ConVar mat_software_aa_strength_vgui( "mat_software_aa_strength_vgui", "-1.0", 0x0000000A, "Same as mat_software_aa_strength, but forced to this value when called by the post vgui AA pass." );
#else
static ConVar mat_software_aa_strength( "mat_software_aa_strength", "0", FCVAR_ARCHIVE, "Software AA - perform a software anti-aliasing post-process (an alternative/supplement to MSAA). This value sets the strength of the effect: (0.0 - off), (1.0 - full)" );
static ConVar mat_software_aa_quality( "mat_software_aa_quality", "0", FCVAR_ARCHIVE, "Software AA quality mode: (0 - 5-tap filter), (1 - 9-tap filter)" );
static ConVar mat_software_aa_edge_threshold( "mat_software_aa_edge_threshold", "1.0", FCVAR_ARCHIVE, "Software AA - adjusts the sensitivity of the software AA shader's edge detection (default 1.0 - a lower value will soften more edges, a higher value will soften fewer)" );
static ConVar mat_software_aa_blur_one_pixel_lines( "mat_software_aa_blur_one_pixel_lines", "0.5", FCVAR_ARCHIVE, "How much software AA should blur one-pixel thick lines: (0.0 - none), (1.0 - lots)" );
static ConVar mat_software_aa_tap_offset( "mat_software_aa_tap_offset", "1.0", FCVAR_ARCHIVE, "Software AA - adjusts the displacement of the taps used by the software AA shader (default 1.0 - a lower value will make the image sharper, higher will make it blurrier)" );
static ConVar mat_software_aa_debug( "mat_software_aa_debug", "0", FCVAR_NONE, "Software AA debug mode: (0 - off), (1 - show number of 'unlike' samples: 0->black, 1->red, 2->green, 3->blue), (2 - show anti-alias blend strength), (3 - show averaged 'unlike' colour)" );
static ConVar mat_software_aa_strength_vgui( "mat_software_aa_strength_vgui", "1", FCVAR_ARCHIVE, "Same as mat_software_aa_strength, but forced to this value when called by the post vgui AA pass." );
#endif

class CEnginePostMaterialProxy : public CEntityMaterialProxy
{
public:
	CEnginePostMaterialProxy();
	virtual ~CEnginePostMaterialProxy();
	virtual bool Init( IMaterial *pMaterial, KeyValues *pKeyValues );
	virtual void OnBind( C_BaseEntity *pEntity );
	virtual IMaterial *GetMaterial();

private:
	IMaterialVar *m_pMaterialParam_AAValues;
	IMaterialVar *m_pMaterialParam_AAValues2;
	IMaterialVar *m_pMaterialParam_BloomEnable;
	IMaterialVar *m_pMaterialParam_BloomUVTransform;
	IMaterialVar *m_pMaterialParam_ColCorrectEnable;
	IMaterialVar *m_pMaterialParam_ColCorrectNumLookups;
	IMaterialVar *m_pMaterialParam_ColCorrectDefaultWeight;
	IMaterialVar *m_pMaterialParam_ColCorrectLookupWeights;
#if defined( L4D2 )
	IMaterialVar *m_pTerrorPost[8], *m_pTerrorBlurEnable, *m_pTerrorFadeColor,
		*m_pTerrorFadeType, *m_pTerrorBloomAmount;
#endif

public:
	static IMaterial * SetupEnginePostMaterial( const Vector4D & fullViewportBloomUVs, const Vector4D & fullViewportFBUVs, const Vector2D & destTexSize,
												bool bPerformSoftwareAA, bool bPerformBloom, bool bPerformColCorrect, float flAAStrength );
	static void SetupEnginePostMaterialAA( bool bPerformSoftwareAA, float flAAStrength );
	static void SetupEnginePostMaterialTextureTransform( const Vector4D & fullViewportBloomUVs, const Vector4D & fullViewportFBUVs, Vector2D destTexSize );

private:
	static float s_vBloomAAValues[4];
	static float s_vBloomAAValues2[4];
	static float s_vBloomUVTransform[4];
	static int   s_PostBloomEnable;
};

float CEnginePostMaterialProxy::s_vBloomAAValues[4]					= { 0.0f, 0.0f, 0.0f, 0.0f };
float CEnginePostMaterialProxy::s_vBloomAAValues2[4]				= { 0.0f, 0.0f, 0.0f, 0.0f };
float CEnginePostMaterialProxy::s_vBloomUVTransform[4]				= { 0.0f, 0.0f, 0.0f, 0.0f };
int   CEnginePostMaterialProxy::s_PostBloomEnable					= 1;

CEnginePostMaterialProxy::CEnginePostMaterialProxy()
{
	m_pMaterialParam_AAValues					= NULL;
	m_pMaterialParam_AAValues2					= NULL;
	m_pMaterialParam_BloomUVTransform			= NULL;
	m_pMaterialParam_BloomEnable				= NULL;
	m_pMaterialParam_ColCorrectEnable			= NULL;
	m_pMaterialParam_ColCorrectNumLookups		= NULL;
	m_pMaterialParam_ColCorrectDefaultWeight	= NULL;
	m_pMaterialParam_ColCorrectLookupWeights	= NULL;
#if defined( L4D2 )
	for ( int i = 0; i < 8; ++i ) m_pTerrorPost[i] = NULL;
	m_pTerrorBlurEnable = m_pTerrorFadeColor = m_pTerrorFadeType = m_pTerrorBloomAmount = NULL;
#endif
}

CEnginePostMaterialProxy::~CEnginePostMaterialProxy()
{
	// Do nothing
}

bool CEnginePostMaterialProxy::Init( IMaterial *pMaterial, KeyValues *pKeyValues )
{
	bool bFoundVar = false;

	m_pMaterialParam_AAValues = pMaterial->FindVar( "$AAInternal1", &bFoundVar, false );
	m_pMaterialParam_AAValues2 = pMaterial->FindVar( "$AAInternal3", &bFoundVar, false );
	m_pMaterialParam_BloomUVTransform = pMaterial->FindVar( "$AAInternal2", &bFoundVar, false );
	m_pMaterialParam_BloomEnable = pMaterial->FindVar( "$bloomEnable", &bFoundVar, false );
	m_pMaterialParam_ColCorrectEnable = pMaterial->FindVar( "$colCorrectEnable", &bFoundVar, false );
	m_pMaterialParam_ColCorrectNumLookups = pMaterial->FindVar( "$colCorrect_NumLookups", &bFoundVar, false );
	m_pMaterialParam_ColCorrectDefaultWeight = pMaterial->FindVar( "$colCorrect_DefaultWeight", &bFoundVar, false );
	m_pMaterialParam_ColCorrectLookupWeights = pMaterial->FindVar( "$colCorrect_LookupWeights", &bFoundVar, false );
#if defined( L4D2 )
	static const char *names[8] = { "$localContrastScale", "$localContrastEdgeScale", "$localContrastVignetteStart",
		"$localContrastVignetteEnd", "$blurredVignetteScale", "$fadeToBlackScale", "$noiseScale", "$topVignetteScale" };
	for ( int i = 0; i < 8; ++i ) m_pTerrorPost[i] = pMaterial->FindVar( names[i], &bFoundVar, false );
	m_pTerrorBlurEnable = pMaterial->FindVar( "$blurredVignetteEnable", &bFoundVar, false );
	m_pTerrorFadeColor = pMaterial->FindVar( "$fadeColor", &bFoundVar, false );
	m_pTerrorFadeType = pMaterial->FindVar( "$fade", &bFoundVar, false );
	m_pTerrorBloomAmount = pMaterial->FindVar( "$bloomAmount", &bFoundVar, false );
#endif

	return true;
}

void CEnginePostMaterialProxy::OnBind( C_BaseEntity *pEnt )
{
	if ( m_pMaterialParam_AAValues )
		m_pMaterialParam_AAValues->SetVecValue( s_vBloomAAValues, 4 );

	if ( m_pMaterialParam_AAValues2 )
		m_pMaterialParam_AAValues2->SetVecValue( s_vBloomAAValues2, 4 );

	if ( m_pMaterialParam_BloomUVTransform )
		m_pMaterialParam_BloomUVTransform->SetVecValue( s_vBloomUVTransform, 4 );

	if ( m_pMaterialParam_BloomEnable )
		m_pMaterialParam_BloomEnable->SetIntValue( s_PostBloomEnable );
#if defined( L4D2 )
	const float *post = static_cast< ClientModeTerrorNormal * >( GetClientModeNormal() )->GetPostProcessValues();
	for ( int i = 0; i < 8; ++i ) m_pTerrorPost[i]->SetFloatValue( post[i] );
	m_pTerrorBlurEnable->SetIntValue( post[4] > 0.0f );
	m_pTerrorFadeColor->SetVecValue( g_TerrorPostFade, 4 );
	m_pTerrorFadeType->SetIntValue( g_TerrorPostFadeType );
	m_pTerrorBloomAmount->SetFloatValue( 0.0f );
	Terror_BindVomitScreenSpaceMaterial( GetMaterial() );
#endif
}

IMaterial *CEnginePostMaterialProxy::GetMaterial()
{
	if ( m_pMaterialParam_AAValues == NULL)
		return NULL;

	return m_pMaterialParam_AAValues->GetOwningMaterial();
}

void CEnginePostMaterialProxy::SetupEnginePostMaterialAA( bool bPerformSoftwareAA, float flAAStrength )
{
	if ( bPerformSoftwareAA )
	{
		// Pass ConVars to the material by proxy
		//  - the strength of the AA effect (from 0 to 1)
		//  - how much to allow 1-pixel lines to be blurred (from 0 to 1)
		//  - pick one of the two quality modes (5-tap or 9-tap filter)
		//  - optionally enable one of several debug modes (via dynamic combos)
		// NOTE: this order matches pixel shader constants in Engine_Post_ps2x.fxc
		s_vBloomAAValues[0]  = flAAStrength;
		s_vBloomAAValues[1]  = 1.0f - mat_software_aa_blur_one_pixel_lines.GetFloat();
		s_vBloomAAValues[2]  = mat_software_aa_quality.GetInt();
		s_vBloomAAValues[3]  = mat_software_aa_debug.GetInt();
		s_vBloomAAValues2[0] = mat_software_aa_edge_threshold.GetFloat();
		s_vBloomAAValues2[1] = mat_software_aa_tap_offset.GetFloat();
		//s_vBloomAAValues2[2] = unused;
		//s_vBloomAAValues2[3] = unused;
	}
	else
	{
		// Zero-strength AA is interpreted as "AA disabled"
		s_vBloomAAValues[0] = 0.0f;
	}
}

void CEnginePostMaterialProxy::SetupEnginePostMaterialTextureTransform( const Vector4D & fullViewportBloomUVs, const Vector4D & fullViewportFBUVs, Vector2D fbSize )
{
	// Engine_Post uses a UV transform (from (quarter-res) bloom texture coords ('1')
	// to (full-res) framebuffer texture coords ('2')).
	//
	// We compute the UV transform as an offset and a scale, using the texture coordinates
	// of the top-left corner of the screen to compute the offset and the coordinate
	// change from the top-left to the bottom-right of the quad to compute the scale.

	// Take texel coordinates (start = top-left, end = bottom-right):
	Vector2D texelStart1	= Vector2D( fullViewportBloomUVs.x,   fullViewportBloomUVs.y );
	Vector2D texelStart2	= Vector2D( fullViewportFBUVs.x,      fullViewportFBUVs.y );
	Vector2D texelEnd1		= Vector2D( fullViewportBloomUVs.z,   fullViewportBloomUVs.w );
	Vector2D texelEnd2		= Vector2D( fullViewportFBUVs.z,      fullViewportFBUVs.w );
	// ...and transform to UV coordinates:
	Vector2D texRes1		= fbSize / 4;
	Vector2D texRes2		= fbSize;
	Vector2D uvStart1		= ( texelStart1 + Vector2D(0.5,0.5) ) / texRes1;
	Vector2D uvStart2		= ( texelStart2 + Vector2D(0.5,0.5) ) / texRes2;
	Vector2D dUV1			= ( texelEnd1   - texelStart1 )       / texRes1;
	Vector2D dUV2			= ( texelEnd2   - texelStart2 )       / texRes2;

	// We scale about the rect's top-left pixel centre (not the origin) in UV-space:
	//    uv' = ((uv - uvStart1)*uvScale + uvStart1) + uvOffset
	//        = uvScale*uv + uvOffset + uvStart1*(1 - uvScale)
	Vector2D uvOffset		= uvStart2 - uvStart1;
	Vector2D uvScale		= dUV2 / dUV1;
	uvOffset				= uvOffset + uvStart1*(Vector2D(1,1) - uvScale);

	s_vBloomUVTransform[0]	= uvOffset.x;
	s_vBloomUVTransform[1]	= uvOffset.y;
	s_vBloomUVTransform[2]	= uvScale.x;
	s_vBloomUVTransform[3]	= uvScale.y;
}

IMaterial * CEnginePostMaterialProxy::SetupEnginePostMaterial(	const Vector4D & fullViewportBloomUVs, const Vector4D & fullViewportFBUVs, const Vector2D & destTexSize,
																bool bPerformSoftwareAA, bool bPerformBloom, bool bPerformColCorrect, float flAAStrength )
{
#if defined( L4D2 )
	s_PostBloomEnable = 0; // Native1C57C0 ignores requested bloom enable/amount.
	SetupEnginePostMaterialAA( bPerformSoftwareAA, flAAStrength );
	SetupEnginePostMaterialTextureTransform( fullViewportBloomUVs, fullViewportFBUVs, destTexSize );
	return g_TerrorPostMaterials.GetEnginePost();
#else
	// Shouldn't get here if none of the effects are enabled
	Assert( bPerformSoftwareAA || bPerformBloom || bPerformColCorrect );

	s_PostBloomEnable		= bPerformBloom ? 1 : 0;

	SetupEnginePostMaterialAA( bPerformSoftwareAA, flAAStrength );

	if ( bPerformSoftwareAA || bPerformColCorrect )
	{
		SetupEnginePostMaterialTextureTransform( fullViewportBloomUVs, fullViewportFBUVs, destTexSize );
		return PostProcessMaterial( PPMAT_ENGINE_POST );
	}
	else
	{
		// Just use the old bloomadd material (which uses additive blending, unlike engine_post)
		// NOTE: this path is what gets used for DX8 (which cannot enable AA or col-correction)
		return PostProcessMaterial( PPMAT_BLOOM_ADD );
	}
#endif
}

EXPOSE_INTERFACE( CEnginePostMaterialProxy, IMaterialProxy, "engine_post" IMATERIAL_PROXY_INTERFACE_VERSION );


static void DrawBloomDebugBoxes( IMatRenderContext *pRenderContext )
{
	// draw inset rects which should have a centered bloom 
	pRenderContext->SetRenderTarget(NULL);
	int dest_width, dest_height;
	pRenderContext->GetRenderTargetDimensions( dest_width, dest_height );

	// full screen clear
	pRenderContext->Viewport( 0, 0, dest_width, dest_height );
	pRenderContext->ClearColor3ub( 0, 0, 0 );
	pRenderContext->ClearBuffers( true, true );

	// inset for screensafe
	int inset = 64;
	int size = 32;

	// centerish, translating
	static int wx = 0;
	wx = ( wx + 1 ) & 63;

	pRenderContext->Viewport( dest_width / 2 + wx, dest_height / 2, size, size );
	pRenderContext->ClearColor3ub( 255, 255, 255 );
	pRenderContext->ClearBuffers( true, true );

	// upper left
	pRenderContext->Viewport( inset, inset, size, size );
	pRenderContext->ClearBuffers( true, true );

	// upper right
	pRenderContext->Viewport( dest_width - inset - size, inset, size, size );
	pRenderContext->ClearBuffers( true, true );
	
	// lower right
	pRenderContext->Viewport( dest_width - inset - size, dest_height - inset - size, size, size );
	pRenderContext->ClearBuffers( true, true );
	
	// lower left
	pRenderContext->Viewport( inset, dest_height - inset - size, size, size );
	pRenderContext->ClearBuffers( true, true );
	
	// restore
	pRenderContext->Viewport( 0, 0, dest_width, dest_height );
}

static float GetBloomAmount( void )
{
	// return bloom amount ( 0.0 if disabled or otherwise turned off )
	if ( engine->GetDXSupportLevel() < 80 )
		return 0.0;

	HDRType_t hdrType = g_pMaterialSystemHardwareConfig->GetHDRType();

	bool bBloomEnabled = (mat_hdr_level.GetInt() >= 1);
	
	if ( !engine->MapHasHDRLighting() )
		bBloomEnabled = false;
	if ( mat_force_bloom.GetInt() )
		bBloomEnabled = true;
	if ( mat_disable_bloom.GetInt() )
		bBloomEnabled = false;
	if ( building_cubemaps.GetBool() )
		bBloomEnabled = false;
	if ( mat_fullbright.GetInt() == 1 )
	{
		bBloomEnabled = false;
	}
	if( !g_pMaterialSystemHardwareConfig->CanDoSRGBReadFromRTs() && g_pMaterialSystemHardwareConfig->FakeSRGBWrite() )
	{
		bBloomEnabled = false;		
	}

	float flBloomAmount=0.0;

	if (bBloomEnabled)
	{
		static float currentBloomAmount = 1.0f;
		float rate = mat_bloomamount_rate.GetFloat();

		// Use the appropriate bloom scale settings.  Mapmakers's overrides the convar settings.
		currentBloomAmount = GetCurrentBloomScale() * rate + ( 1.0f - rate ) * currentBloomAmount;
		flBloomAmount = currentBloomAmount;
	}

	if ( hdrType == HDR_TYPE_NONE )
	{
		flBloomAmount *= mat_non_hdr_bloom_scalefactor.GetFloat();
	}

	flBloomAmount *= mat_bloom_scalefactor_scalar.GetFloat();

	return flBloomAmount;
}

// Control for dumping render targets to files for debugging
static ConVar mat_dump_rts( "mat_dump_rts", "0"
#if defined( L4D2 )
	, 0x0000000A
#endif
 );
static int s_nRTIndex = 0;
bool g_bDumpRenderTargets = false;


// Dump a rendertarget to a TGA.  Useful for looking at intermediate render target results.
void DumpTGAofRenderTarget( const int width, const int height, const char *pFilename )
{
	// Ensure that mat_queue_mode is zero
	static ConVarRef mat_queue_mode( "mat_queue_mode" );
	if ( mat_queue_mode.GetInt() != 0 )
	{
		DevMsg( "Error: mat_queue_mode must be 0 to dump debug rendertargets\n" );
		mat_dump_rts.SetValue( 0 );		// Just report this error once and stop trying to dump images
		return;
	}

	CMatRenderContextPtr pRenderContext( materials );

	// Get the data from the render target and save to disk bitmap bits
	unsigned char *pImage = ( unsigned char * )malloc( width * 4 * height );

	// Get Bits from the material system
	pRenderContext->ReadPixels( 0, 0, width, height, pImage, IMAGE_FORMAT_RGBA8888 );

	// allocate a buffer to write the tga into
	int iMaxTGASize = 1024 + (width * height * 4);
	void *pTGA = malloc( iMaxTGASize );
	CUtlBuffer buffer( pTGA, iMaxTGASize );

	if( !TGAWriter::WriteToBuffer( pImage, buffer, width, height, IMAGE_FORMAT_RGBA8888, IMAGE_FORMAT_RGBA8888 ) )
	{
		Error( "Couldn't write bitmap data snapshot.\n" );
	}

	free( pImage );

	// async write to disk (this will take ownership of the memory)
	char szPathedFileName[_MAX_PATH];
	Q_snprintf( szPathedFileName, sizeof(szPathedFileName), "//MOD/%d_%s_%s.tga", s_nRTIndex++, pFilename, IsOSX() ? "OSX" : "PC" );

	FileHandle_t fileTGA = filesystem->Open( szPathedFileName, "wb" );
	filesystem->Write( buffer.Base(), buffer.TellPut(), fileTGA );
	filesystem->Close( fileTGA );

	free( pTGA );
}


static bool s_bScreenEffectTextureIsUpdated = false;

#if !defined( L4D2 )
static void Generate8BitBloomTexture( IMatRenderContext *pRenderContext, float flBloomScale,
										int x, int y, int w, int h )
{
	tmZone( TELEMETRY_LEVEL0, TMZF_NONE, "%s", __FUNCTION__ );

	pRenderContext->PushRenderTargetAndViewport();
	ITexture *pSrc = materials->FindTexture( "_rt_FullFrameFB", TEXTURE_GROUP_RENDER_TARGET );
	int nSrcWidth = pSrc->GetActualWidth();
	int nSrcHeight = pSrc->GetActualHeight(); //,dest_height;

	// Counter-Strike: Source uses a different downsample algorithm than other games
	IMaterial *downsample_mat = PostProcessMaterial( PPMAT_DOWNSAMPLE_NO_HDR );
	IMaterial *xblur_mat = PostProcessMaterial( PPMAT_BLUR_X_NO_HDR );
	IMaterial *yblur_mat = PostProcessMaterial( PPMAT_BLUR_Y_NO_HDR );
	ITexture *dest_rt0 = materials->FindTexture( "_rt_SmallFB0", TEXTURE_GROUP_RENDER_TARGET );
	ITexture *dest_rt1 = materials->FindTexture( "_rt_SmallFB1", TEXTURE_GROUP_RENDER_TARGET );

	// *Everything* in here relies on the small RTs being exactly 1/4 the full FB res
	Assert( dest_rt0->GetActualWidth()  == pSrc->GetActualWidth()  / 4 );
	Assert( dest_rt0->GetActualHeight() == pSrc->GetActualHeight() / 4 );
	Assert( dest_rt1->GetActualWidth()  == pSrc->GetActualWidth()  / 4 );
	Assert( dest_rt1->GetActualHeight() == pSrc->GetActualHeight() / 4 );

	// Downsample fb to rt0
	SetRenderTargetAndViewPort( dest_rt0 );
	// note the -2's below. Thats because we are downsampling on each axis and the shader
	// accesses pixels on both sides of the source coord
	pRenderContext->DrawScreenSpaceRectangle(	downsample_mat, 0, 0, nSrcWidth/4, nSrcHeight/4,
												0, 0, nSrcWidth-2, nSrcHeight-2,
												nSrcWidth, nSrcHeight );

	if ( IsX360() )
	{
		pRenderContext->CopyRenderTargetToTextureEx( dest_rt0, 0, NULL, NULL );
	}
	else if ( g_bDumpRenderTargets )
	{
		DumpTGAofRenderTarget( nSrcWidth/4, nSrcHeight/4, "QuarterSizeFB" );
	}

	// Gaussian blur x rt0 to rt1
	SetRenderTargetAndViewPort( dest_rt1 );
	pRenderContext->DrawScreenSpaceRectangle(	xblur_mat, 0, 0, nSrcWidth/4, nSrcHeight/4,
												0, 0, nSrcWidth/4-1, nSrcHeight/4-1,
												nSrcWidth/4, nSrcHeight/4 );
	if ( IsX360() )
	{
		pRenderContext->CopyRenderTargetToTextureEx( dest_rt1, 0, NULL, NULL );
	}
	else if ( g_bDumpRenderTargets )
	{
		DumpTGAofRenderTarget( nSrcWidth/4, nSrcHeight/4, "BlurX" );
	}

	// Gaussian blur y rt1 to rt0
	SetRenderTargetAndViewPort( dest_rt0 );
	IMaterialVar *pBloomAmountVar = PostProcessVar( PPMAT_BLUR_Y_NO_HDR, PPVAR_BLOOM_AMOUNT );
	pBloomAmountVar->SetFloatValue( flBloomScale );
	pRenderContext->DrawScreenSpaceRectangle(	yblur_mat, 0, 0, nSrcWidth / 4, nSrcHeight / 4,
												0, 0, nSrcWidth / 4 - 1, nSrcHeight / 4 - 1,
												nSrcWidth / 4, nSrcHeight / 4 );
	if ( IsX360() )
	{
		pRenderContext->CopyRenderTargetToTextureEx( dest_rt0, 0, NULL, NULL );
	}
	else if ( g_bDumpRenderTargets )
	{
		DumpTGAofRenderTarget( nSrcWidth/4, nSrcHeight/4, "BlurYAndBloom" );
	}

	pRenderContext->PopRenderTargetAndViewport();
}
#endif

static void DoPreBloomTonemapping( IMatRenderContext *pRenderContext, int nX, int nY, int nWidth, int nHeight, float flAutoExposureMin, float flAutoExposureMax )
{
	// Update HDR histogram before bloom
#if defined( L4D2 )
	if ( mat_dynamic_tonemapping.GetInt() )
#else
	if ( mat_dynamic_tonemapping.GetInt() || mat_show_histogram.GetInt() )
#endif
	{
		tmZone( TELEMETRY_LEVEL0, TMZF_NONE, "%s", __FUNCTION__ );

		if ( s_bScreenEffectTextureIsUpdated == false )
		{
			// FIXME: nX/nY/nWidth/nHeight are used here, but the equivalent parameters are ignored in Generate8BitBloomTexture
			if ( !AcquireBoundFrameCopy( pRenderContext, nX, nY, nWidth, nHeight ) )
#if defined( L4D2 )
				UpdateScreenEffectTexture( 0, nX, nY, nWidth, nHeight, false );
#else
				UpdateScreenEffectTexture( 0, nX, nY, nWidth, nHeight, true );
#endif
			s_bScreenEffectTextureIsUpdated = true;
		}

		g_HDR_HistogramSystem.Update();
		if ( mat_dynamic_tonemapping.GetInt() || mat_show_histogram.GetInt() )
		{
			float flTargetScalar = g_HDR_HistogramSystem.GetTargetTonemapScalar();
			float flTargetScalarClamped = MAX( flAutoExposureMin, MIN( flAutoExposureMax, flTargetScalar ) );
			flTargetScalarClamped = MAX( 0.001f, flTargetScalarClamped ); // Don't let this go to 0!
			if ( mat_dynamic_tonemapping.GetInt() )
			{
				SetToneMapScale( pRenderContext, flTargetScalarClamped, flAutoExposureMin, flAutoExposureMax );
			}
			
			if ( mat_debug_autoexposure.GetInt() || mat_show_histogram.GetInt() )
			{
				bool bDrawTextThisFrame = true;

				if ( IsX360() )
				{
					static float s_flLastTimeUpdate = 0.0f;
					if ( int( gpGlobals->curtime ) - int( s_flLastTimeUpdate ) >= 2 )
					{
						s_flLastTimeUpdate = gpGlobals->curtime;
						bDrawTextThisFrame = true;
					}
					else
					{
						bDrawTextThisFrame = false;
					}
				}

				if ( bDrawTextThisFrame == true )
				{
					if ( mat_tonemap_algorithm.GetInt() == 0 )
					{
						engine->Con_NPrintf( 19, "(Original algorithm) Target Scalar = %4.2f  Min/Max( %4.2f, %4.2f )  Final Scalar: %4.2f  Actual: %4.2f",
											 flTargetScalar, flAutoExposureMin, flAutoExposureMax, mat_hdr_tonemapscale.GetFloat(), pRenderContext->GetToneMappingScaleLinear().x );
					}
					else
					{
#if defined( L4D2 )
						engine->Con_NPrintf( 19, "%.2f%% of pixels above %d%% target @ %4.2f%%  Target Scalar = %4.2f  Min/Max( %4.2f, %4.2f )  Final Scalar: %4.2f  Actual: %4.2f",
							Terror_TonemapPercentBrightPixels(), (int)Terror_TonemapPercentTarget(),
							( g_HDR_HistogramSystem.FindLocationOfPercentBrightPixels( Terror_TonemapPercentBrightPixels(), Terror_TonemapPercentTarget() ) * 100.0f ),
							g_HDR_HistogramSystem.GetTargetTonemapScalar( true ), flAutoExposureMin, flAutoExposureMax, mat_hdr_tonemapscale.GetFloat(), pRenderContext->GetToneMappingScaleLinear().x );
#else
						engine->Con_NPrintf( 19, "%.2f%% of pixels above %d%% target @ %4.2f%%  Target Scalar = %4.2f  Min/Max( %4.2f, %4.2f )  Final Scalar: %4.2f  Actual: %4.2f",
											 mat_tonemap_percent_bright_pixels.GetFloat(), mat_tonemap_percent_target.GetInt(),
											 ( g_HDR_HistogramSystem.FindLocationOfPercentBrightPixels( mat_tonemap_percent_bright_pixels.GetFloat(), mat_tonemap_percent_target.GetFloat() ) * 100.0f ),
											 g_HDR_HistogramSystem.GetTargetTonemapScalar( true ), flAutoExposureMin, flAutoExposureMax, mat_hdr_tonemapscale.GetFloat(), pRenderContext->GetToneMappingScaleLinear().x );
#endif
					}
				}
			}
		}
	}
}

static void DoPostBloomTonemapping( IMatRenderContext *pRenderContext, int nX, int nY, int nWidth, int nHeight, float flAutoExposureMin, float flAutoExposureMax )
{
#if defined( L4D2 )
	if ( mat_show_histogram.GetInt() && mat_dynamic_tonemapping.GetInt() && ( engine->GetDXSupportLevel() >= 90 ) )
#else
	if ( mat_show_histogram.GetInt() && ( engine->GetDXSupportLevel() >= 90 ) )
#endif
	{
		g_HDR_HistogramSystem.DisplayHistogram();
	}
}

static void CenterScaleQuadUVs( Vector4D & quadUVs, const Vector2D & uvScale )
{
	Vector2D uvMid	= 0.5f*Vector2D( ( quadUVs.z + quadUVs.x ), ( quadUVs.w + quadUVs.y ) );
	Vector2D uvRange= 0.5f*Vector2D( ( quadUVs.z - quadUVs.x ), ( quadUVs.w - quadUVs.y ) );
	quadUVs.x		= uvMid.x - uvScale.x*uvRange.x;
	quadUVs.y		= uvMid.y - uvScale.y*uvRange.y;
	quadUVs.z		= uvMid.x + uvScale.x*uvRange.x;
	quadUVs.w		= uvMid.y + uvScale.y*uvRange.y;
}


typedef struct SPyroSide
{
	float		m_vCornerPos[ 2 ][ 2 ];
	float		m_flIntensity;
	float		m_flIntensityLimit;
	float		m_flRate;
	bool		m_bHorizontal;
	bool		m_bIncreasing;
	bool		m_bAlive;
} TPyroSide;

#define MAX_PYRO_SIDES			50
#define NUM_PYRO_SEGMENTS		8
#define MIN_PYRO_SIDE_LENGTH	0.5f
#define MAX_PYRO_SIDE_LENGTH	0.90f
#define MIN_PYRO_SIDE_WIDTH		0.25f
#define MAX_PYRO_SIDE_WIDTH		0.95f

static TPyroSide	PyroSides[ MAX_PYRO_SIDES ];

ConVar pyro_vignette( "pyro_vignette", "2", FCVAR_ARCHIVE );
ConVar pyro_vignette_distortion( "pyro_vignette_distortion", "1", FCVAR_ARCHIVE );
ConVar pyro_min_intensity( "pyro_min_intensity", "0.1", FCVAR_ARCHIVE );
ConVar pyro_max_intensity( "pyro_max_intensity", "0.35", FCVAR_ARCHIVE );
ConVar pyro_min_rate( "pyro_min_rate", "0.05", FCVAR_ARCHIVE );
ConVar pyro_max_rate( "pyro_max_rate", "0.2", FCVAR_ARCHIVE );
ConVar pyro_min_side_length( "pyro_min_side_length", "0.3", FCVAR_ARCHIVE );
ConVar pyro_max_side_length( "pyro_max_side_length", "0.55", FCVAR_ARCHIVE );
ConVar pyro_min_side_width( "pyro_min_side_width", "0.65", FCVAR_ARCHIVE );
ConVar pyro_max_side_width( "pyro_max_side_width", "0.95", FCVAR_ARCHIVE );

static void CreatePyroSide( int nSide, Vector2D &vMaxSize )
{
	int nFound = 0;
	for( ; nFound < MAX_PYRO_SIDES; nFound++ )
	{
		if ( !PyroSides[ nFound ].m_bAlive )
		{
			break;
		}
	}

	if ( nFound >= MAX_PYRO_SIDES )
	{
		return;
	}

	TPyroSide *pSide = &PyroSides[ nFound ];

	pSide->m_flIntensity = 0.0f;
	pSide->m_flIntensityLimit = RandomFloat( pyro_min_intensity.GetFloat(), pyro_max_intensity.GetFloat() );
	pSide->m_flRate = RandomFloat( pyro_min_rate.GetFloat(), pyro_max_rate.GetFloat() );
	pSide->m_bIncreasing = true;
	pSide->m_bHorizontal = ( ( nSide >> 1 ) & 1 ) == 0;
	pSide->m_bAlive = true;

//	float flWidth = RandomFloat( MIN_PYRO_SIDE_WIDTH, MAX_PYRO_SIDE_WIDTH ) * 2.0f;
//	float flLength = RandomFloat( MIN_PYRO_SIDE_LENGTH, MAX_PYRO_SIDE_LENGTH );
	float flWidth = RandomFloat( pyro_min_side_width.GetFloat(), pyro_max_side_width.GetFloat() ) * 2.0f;
	float flLength = RandomFloat( pyro_min_side_length.GetFloat(), pyro_max_side_length.GetFloat() );

	switch( nSide )
	{
		case 0:
			{
				pSide->m_vCornerPos[ 0 ][ 0 ] = -1.0f;
				pSide->m_vCornerPos[ 0 ][ 1 ] = 1.0f;

				pSide->m_vCornerPos[ 1 ][ 0 ] = -1.0f + flWidth;
				pSide->m_vCornerPos[ 1 ][ 1 ] = 1.0f - (  flLength * vMaxSize.y );
			}
			break;

		case 1:
			{
				pSide->m_vCornerPos[ 0 ][ 0 ] = 1.0f;
				pSide->m_vCornerPos[ 0 ][ 1 ] = 1.0f;

				pSide->m_vCornerPos[ 1 ][ 0 ] = 1.0f - flWidth;
				pSide->m_vCornerPos[ 1 ][ 1 ] = 1.0f - (  flLength * vMaxSize.y );
			}
			break;

		case 2:
			{
				pSide->m_vCornerPos[ 0 ][ 0 ] = 1.0f;
				pSide->m_vCornerPos[ 0 ][ 1 ] = 1.0f;

				pSide->m_vCornerPos[ 1 ][ 0 ] = 1.0f - (  flLength * vMaxSize.x );
				pSide->m_vCornerPos[ 1 ][ 1 ] = 1.0f - flWidth;
			}
			break;

		case 3:
			{
				pSide->m_vCornerPos[ 0 ][ 0 ] = 1.0f;
				pSide->m_vCornerPos[ 0 ][ 1 ] = -1.0f;

				pSide->m_vCornerPos[ 1 ][ 0 ] = 1.0f - (  flLength * vMaxSize.x );
				pSide->m_vCornerPos[ 1 ][ 1 ] = -1.0f + flWidth;
			}
			break;

		case 4:
			{
				pSide->m_vCornerPos[ 0 ][ 0 ] = 1.0f;
				pSide->m_vCornerPos[ 0 ][ 1 ] = -1.0f;

				pSide->m_vCornerPos[ 1 ][ 0 ] = 1.0f - flWidth;
				pSide->m_vCornerPos[ 1 ][ 1 ] = -1.0f + (  flLength * vMaxSize.y );
			}
			break;

		case 5:
			{
				pSide->m_vCornerPos[ 0 ][ 0 ] = -1.0f;
				pSide->m_vCornerPos[ 0 ][ 1 ] = -1.0f;

				pSide->m_vCornerPos[ 1 ][ 0 ] = -1.0f + flWidth;
				pSide->m_vCornerPos[ 1 ][ 1 ] = -1.0f + (  flLength * vMaxSize.y );
			}
			break;

		case 6:
			{
				pSide->m_vCornerPos[ 0 ][ 0 ] = -1.0f;
				pSide->m_vCornerPos[ 0 ][ 1 ] = -1.0f;

				pSide->m_vCornerPos[ 1 ][ 0 ] = -1.0f + (  flLength * vMaxSize.x );
				pSide->m_vCornerPos[ 1 ][ 1 ] = -1.0f + flWidth;
			}
			break;

		case 7:
			{
				pSide->m_vCornerPos[ 0 ][ 0 ] = -1.0f;
				pSide->m_vCornerPos[ 0 ][ 1 ] = 1.0f;

				pSide->m_vCornerPos[ 1 ][ 0 ] = -1.0f + (  flLength * vMaxSize.x );
				pSide->m_vCornerPos[ 1 ][ 1 ] = 1.0f - flWidth;
			}
			break;
	}
}


static float PryoVignetteSTHorizontal[ 6 ][ 2 ] =
{
	{	0.0f, 0.0f },		 
	{	0.0f, 1.0f },		 
	{	1.0f, 1.0f },		 

	{	1.0f, 1.0f },		 
	{	0.0f, 0.0f },		 
	{	1.0f, 0.0f }
};

static float PryoVignetteSTVertical[ 6 ][ 2 ] =
{
	{	0.0f, 0.0f },		 
	{	1.0f, 0.0f },		 
	{	1.0f, 1.0f },		 

	{	1.0f, 1.0f },		 
	{	0.0f, 0.0f },		 
	{	0.0f, 1.0f }
};


static int PryoSideIndexes[ 6 ][ 2 ] =
{
	{ 0, 0 },
	{ 0, 1 },
	{ 1, 1 },

	{ 1, 1 },
	{ 0, 0 },
	{ 1, 0 }
};


static void DrawPyroVignette( int nDestX, int nDestY, int nWidth, int nHeight,	// Rect to draw into in screen space
	float flSrcTextureX0, float flSrcTextureY0,		// which texel you want to appear at destx/y
	float flSrcTextureX1, float flSrcTextureY1,		// which texel you want to appear at destx+width-1, desty+height-1
	void *pClientRenderable )
{
	static bool			bInit = false;
	static int			nNextSide = 0;

	CMatRenderContextPtr pRenderContext( g_pMaterialSystem );

	IMaterial *pVignetteBorder = PostProcessMaterial( PPMAT_PYRO_BORDER );
	IMaterial *pMaterial = PostProcessMaterial( PPMAT_PYRO_VIGNETTE );
	ITexture *pRenderTarget = materials->FindTexture( "_rt_ResolvedFullFrameDepth", TEXTURE_GROUP_RENDER_TARGET );

	pRenderContext->PushRenderTargetAndViewport( pRenderTarget );
	pRenderContext->ClearColor4ub( 0, 0, 0, 0 );
	pRenderContext->ClearBuffers( true, false );

	int nScreenWidth, nScreenHeight;
	pRenderContext->GetRenderTargetDimensions( nScreenWidth, nScreenHeight );
	pRenderContext->DrawScreenSpaceRectangle( pVignetteBorder, 0, 0, nScreenWidth, nScreenHeight, 0, 0, nScreenWidth - 1, nScreenHeight - 1, nScreenWidth, nScreenHeight, pClientRenderable );

	if ( pyro_vignette.GetInt() > 1 )
	{
		float flPyroSegments = 2.0f / NUM_PYRO_SEGMENTS;
		Vector2D vMaxSize( flPyroSegments, flPyroSegments );

		if ( !bInit )
		{
			for( int i = 0; i < MAX_PYRO_SIDES; i++ )
			{
				PyroSides[ i ].m_bAlive = false;
			}

			CreatePyroSide( nNextSide, vMaxSize );
			nNextSide = ( nNextSide + 1 ) & 7;

			bInit = true;
		}

		int nNumAlive = 0;
		TPyroSide *pSide = &PyroSides[ 0 ];
		for( int nIndex = 0; nIndex < MAX_PYRO_SIDES; nIndex++, pSide++ )
		{
			if ( pSide->m_bAlive )
			{
				if ( pSide->m_bIncreasing )
				{
					pSide->m_flIntensity += pSide->m_flRate * gpGlobals->frametime;
					if ( pSide->m_flIntensity >= pSide->m_flIntensityLimit )
					{
						pSide->m_bIncreasing = false;
					}
				}
				else
				{
					pSide->m_flIntensity -= pSide->m_flRate * gpGlobals->frametime;
					if ( pSide->m_flIntensity <= 0.0f )
					{
						pSide->m_bAlive = false;
					}
				}
			}

			if ( pSide->m_bAlive )
			{
				nNumAlive++;
			}
		}

		if ( nNumAlive > 0 )
		{
			pRenderContext->MatrixMode( MATERIAL_VIEW );
			pRenderContext->PushMatrix();
			pRenderContext->LoadIdentity();

			pRenderContext->MatrixMode( MATERIAL_PROJECTION );
			pRenderContext->PushMatrix();
			pRenderContext->LoadIdentity();

			pRenderContext->Bind( pMaterial, pClientRenderable );

			CMeshBuilder meshBuilder;

			IMesh* pMesh = pRenderContext->GetDynamicMesh( true );
			meshBuilder.Begin( pMesh, MATERIAL_TRIANGLES, nNumAlive * 2 );

			pSide = &PyroSides[ 0 ];
			for( int nIndex = 0; nIndex < MAX_PYRO_SIDES; nIndex++, pSide++ )
			{
				if ( pSide->m_bAlive )
				{
					for( int i = 0; i < 6; i++ )
					{
						meshBuilder.Position3f( pSide->m_vCornerPos[ PryoSideIndexes[ i ][ 0 ] ][ 0 ], pSide->m_vCornerPos[ PryoSideIndexes[ i ][ 1 ] ][ 1 ], 0.0f );
						meshBuilder.Color4f( pSide->m_flIntensity, pSide->m_flIntensity, pSide->m_flIntensity, 1.0f );
						meshBuilder.TexCoord2fv( 0, pSide->m_bHorizontal ? PryoVignetteSTHorizontal[ i ] : PryoVignetteSTVertical[ i ] );
						meshBuilder.TangentS3f( 0.0f, 1.0f, 0.0f );
						meshBuilder.TangentT3f( 1.0f, 0.0f, 0.0f );
						meshBuilder.Normal3f( 0.0f, 0.0f, 1.0f );
						meshBuilder.AdvanceVertex();
					}
				}
			}

			meshBuilder.End();
			pMesh->Draw();

			pRenderContext->MatrixMode( MATERIAL_VIEW );
			pRenderContext->PopMatrix();

			pRenderContext->MatrixMode( MATERIAL_PROJECTION );
			pRenderContext->PopMatrix();
		}

		if ( nNumAlive < 25 )
		{
			CreatePyroSide( nNextSide, vMaxSize );
			nNextSide = ( nNextSide + 1 ) & 7;
		}
	}

	pRenderContext->PopRenderTargetAndViewport();
}


static void DrawPyroPost( IMaterial *pMaterial, 
	int nDestX, int nDestY, int nWidth, int nHeight,	// Rect to draw into in screen space
	float flSrcTextureX0, float flSrcTextureY0,		// which texel you want to appear at destx/y
	float flSrcTextureX1, float flSrcTextureY1,		// which texel you want to appear at destx+width-1, desty+height-1
	int nSrcTextureWidth, int nSrcTextureHeight,		// needed for fixup
	void *pClientRenderable )							// Used to pass to the bind proxies
{
	IMaterialVar *pVar = PostProcessVar( PPMAT_PYRO_POST, PPVAR_DISABLED );
	if ( pVar && pVar->GetIntValue() )
	{
		return;
	}


	CMatRenderContextPtr pRenderContext( g_pMaterialSystem );

	pRenderContext->MatrixMode( MATERIAL_VIEW );
	pRenderContext->PushMatrix();
	pRenderContext->LoadIdentity();

	pRenderContext->MatrixMode( MATERIAL_PROJECTION );
	pRenderContext->PushMatrix();
	pRenderContext->LoadIdentity();

	pRenderContext->Bind( pMaterial, pClientRenderable );

	int xSegments = NUM_PYRO_SEGMENTS;
	int ySegments = NUM_PYRO_SEGMENTS;

	CMeshBuilder meshBuilder;

	IMesh* pMesh = pRenderContext->GetDynamicMesh( true );
	meshBuilder.Begin( pMesh, MATERIAL_QUADS, 4 );

	int nScreenWidth, nScreenHeight;
	pRenderContext->GetRenderTargetDimensions( nScreenWidth, nScreenHeight );

	float flOffset = IsPosix() ? 0.0f : 0.5f;

	float flLeftX = nDestX - flOffset;
	float flRightX = nDestX + nWidth - flOffset;

	float flTopY = nDestY - flOffset;
	float flBottomY = nDestY + nHeight - flOffset;

	float flSubrectWidth = flSrcTextureX1 - flSrcTextureX0;
	float flSubrectHeight = flSrcTextureY1 - flSrcTextureY0;

	float flTexelsPerPixelX = ( nWidth > 1 ) ? flSubrectWidth / ( nWidth - 1 ) : 0.0f;
	float flTexelsPerPixelY = ( nHeight > 1 ) ? flSubrectHeight / ( nHeight - 1 ) : 0.0f;

	float flLeftU = flSrcTextureX0 + 0.5f - ( 0.5f * flTexelsPerPixelX );
	float flRightU = flSrcTextureX1 + 0.5f + ( 0.5f * flTexelsPerPixelX );
	float flTopV = flSrcTextureY0 + 0.5f - ( 0.5f * flTexelsPerPixelY );
	float flBottomV = flSrcTextureY1 + 0.5f + ( 0.5f * flTexelsPerPixelY );

	float flOOTexWidth = 1.0f / nSrcTextureWidth;
	float flOOTexHeight = 1.0f / nSrcTextureHeight;
	flLeftU *= flOOTexWidth;
	flRightU *= flOOTexWidth;
	flTopV *= flOOTexHeight;
	flBottomV *= flOOTexHeight;

	// Get the current viewport size
	int vx, vy, vw, vh;
	pRenderContext->GetViewport( vx, vy, vw, vh );

	// map from screen pixel coords to -1..1
	flRightX = FLerp( -1, 1, 0, vw, flRightX );
	flLeftX = FLerp( -1, 1, 0, vw, flLeftX );
	flTopY = FLerp( 1, -1, 0, vh ,flTopY );
	flBottomY = FLerp( 1, -1, 0, vh, flBottomY );

	// Screen height and width of a subrect
	float flWidth  = (flRightX - flLeftX) / (float) xSegments;
	float flHeight = (flTopY - flBottomY) / (float) ySegments;

	// UV height and width of a subrect
	float flUWidth  = (flRightU - flLeftU) / (float) xSegments;
	float flVHeight = (flBottomV - flTopV) / (float) ySegments;


	// Top Bar

	// Top left
	meshBuilder.Position3f( flLeftX   + (float) 0 * flWidth, flTopY - (float) 0 * flHeight, 0.0f );
	meshBuilder.TexCoord2f( 0, flLeftU   + (float) 0 * flUWidth, flTopV + (float) 0 * flVHeight);
	meshBuilder.AdvanceVertex();

	// Top right (x+1)
	meshBuilder.Position3f( flLeftX   + (float) (xSegments+1) * flWidth, flTopY - (float) 0 * flHeight, 0.0f );
	meshBuilder.TexCoord2f( 0, flLeftU   + (float) (xSegments+1) * flUWidth, flTopV + (float) 0 * flVHeight);
	meshBuilder.AdvanceVertex();

	// Bottom right (x+1), (y+1)
	meshBuilder.Position3f( flLeftX   + (float) (xSegments+1) * flWidth, flTopY - (float) (0+1) * flHeight, 0.0f );
	meshBuilder.TexCoord2f( 0, flLeftU   + (float) (xSegments+1) * flUWidth, flTopV + (float)(0+1) * flVHeight);
	meshBuilder.AdvanceVertex();

	// Bottom left (y+1)
	meshBuilder.Position3f( flLeftX   + (float) 0 * flWidth, flTopY - (float) (0+1) * flHeight, 0.0f );
	meshBuilder.TexCoord2f( 0, flLeftU   + (float) 0 * flUWidth, flTopV + (float)(0+1) * flVHeight);
	meshBuilder.AdvanceVertex();
	
	// Bottom Bar

	// Top left
	meshBuilder.Position3f( flLeftX   + (float) 0 * flWidth, flTopY - (float) ( ySegments - 1) * flHeight, 0.0f );
	meshBuilder.TexCoord2f( 0, flLeftU   + (float) 0 * flUWidth, flTopV + (float) ( ySegments - 1) * flVHeight);
	meshBuilder.AdvanceVertex();

	// Top right (x+1)
	meshBuilder.Position3f( flLeftX   + (float) (xSegments) * flWidth, flTopY - (float) ( ySegments - 1) * flHeight, 0.0f );
	meshBuilder.TexCoord2f( 0, flLeftU   + (float) (xSegments) * flUWidth, flTopV + (float) ( ySegments - 1 ) * flVHeight);
	meshBuilder.AdvanceVertex();

	// Bottom right (x+1), (y+1)
	meshBuilder.Position3f( flLeftX   + (float) (xSegments) * flWidth, flTopY - (float) ( ySegments ) * flHeight, 0.0f );
	meshBuilder.TexCoord2f( 0, flLeftU   + (float) (xSegments) * flUWidth, flTopV + (float)( ySegments ) * flVHeight);
	meshBuilder.AdvanceVertex();

	// Bottom left (y+1)
	meshBuilder.Position3f( flLeftX   + (float) 0 * flWidth, flTopY - (float) ( ySegments ) * flHeight, 0.0f );
	meshBuilder.TexCoord2f( 0, flLeftU   + (float) 0 * flUWidth, flTopV + (float)( ySegments ) * flVHeight);
	meshBuilder.AdvanceVertex();

	// Left Bar

	// Top left
	meshBuilder.Position3f( flLeftX   + (float) 0 * flWidth, flTopY - (float) 1 * flHeight, 0.0f );
	meshBuilder.TexCoord2f( 0, flLeftU   + (float) 0 * flUWidth, flTopV + (float) 1 * flVHeight);
	meshBuilder.AdvanceVertex();

	// Top right (x+1)
	meshBuilder.Position3f( flLeftX   + (float) (0+1) * flWidth, flTopY - (float) 1 * flHeight, 0.0f );
	meshBuilder.TexCoord2f( 0, flLeftU   + (float) (0+1) * flUWidth, flTopV + (float) 1 * flVHeight);
	meshBuilder.AdvanceVertex();

	// Bottom right (x+1), (y+1)
	meshBuilder.Position3f( flLeftX   + (float) (0+1) * flWidth, flTopY - (float) (ySegments - 1) * flHeight, 0.0f );
	meshBuilder.TexCoord2f( 0, flLeftU   + (float) (0+1) * flUWidth, flTopV + (float)(ySegments - 1) * flVHeight);
	meshBuilder.AdvanceVertex();

	// Bottom left (y+1)
	meshBuilder.Position3f( flLeftX   + (float) 0 * flWidth, flTopY - (float) (ySegments - 1) * flHeight, 0.0f );
	meshBuilder.TexCoord2f( 0, flLeftU   + (float) 0 * flUWidth, flTopV + (float)(ySegments - 1) * flVHeight);
	meshBuilder.AdvanceVertex();

	// Right Bar

	// Top left
	meshBuilder.Position3f( flLeftX   + (float) (xSegments - 1) * flWidth, flTopY - (float) 1 * flHeight, 0.0f );
	meshBuilder.TexCoord2f( 0, flLeftU   + (float) (xSegments - 1) * flUWidth, flTopV + (float) 1 * flVHeight);
	meshBuilder.AdvanceVertex();

	// Top right (x+1)
	meshBuilder.Position3f( flLeftX   + (float) (xSegments) * flWidth, flTopY - (float) 1 * flHeight, 0.0f );
	meshBuilder.TexCoord2f( 0, flLeftU   + (float) (xSegments) * flUWidth, flTopV + (float) 1 * flVHeight);
	meshBuilder.AdvanceVertex();

	// Bottom right (x+1), (y+1)
	meshBuilder.Position3f( flLeftX   + (float) (xSegments) * flWidth, flTopY - (float) (ySegments - 1) * flHeight, 0.0f );
	meshBuilder.TexCoord2f( 0, flLeftU   + (float) (xSegments) * flUWidth, flTopV + (float)(ySegments - 1) * flVHeight);
	meshBuilder.AdvanceVertex();

	// Bottom left (y+1)
	meshBuilder.Position3f( flLeftX   + (float) (xSegments - 1) * flWidth, flTopY - (float) (ySegments - 1) * flHeight, 0.0f );
	meshBuilder.TexCoord2f( 0, flLeftU   + (float) (xSegments - 1) * flUWidth, flTopV + (float)(ySegments - 1) * flVHeight);
	meshBuilder.AdvanceVertex();



#if 0

	for ( int x=0; x < xSegments; x++ )
	{
		for ( int y=0; y < ySegments; y++ )
		{
			if ( ( x == 1 || x == 2 ) && ( y == 1 || y == 2 ) )
			{	// skip the center 4 segments
				continue;
			}

			// Top left
			meshBuilder.Position3f( flLeftX   + (float) x * flWidth, flTopY - (float) y * flHeight, 0.0f );
			meshBuilder.TexCoord2f( 0, flLeftU   + (float) x * flUWidth, flTopV + (float) y * flVHeight);
			meshBuilder.AdvanceVertex();

			// Top right (x+1)
			meshBuilder.Position3f( flLeftX   + (float) (x+1) * flWidth, flTopY - (float) y * flHeight, 0.0f );
			meshBuilder.TexCoord2f( 0, flLeftU   + (float) (x+1) * flUWidth, flTopV + (float) y * flVHeight);
			meshBuilder.AdvanceVertex();

			// Bottom right (x+1), (y+1)
			meshBuilder.Position3f( flLeftX   + (float) (x+1) * flWidth, flTopY - (float) (y+1) * flHeight, 0.0f );
			meshBuilder.TexCoord2f( 0, flLeftU   + (float) (x+1) * flUWidth, flTopV + (float)(y+1) * flVHeight);
			meshBuilder.AdvanceVertex();

			// Bottom left (y+1)
			meshBuilder.Position3f( flLeftX   + (float) x * flWidth, flTopY - (float) (y+1) * flHeight, 0.0f );
			meshBuilder.TexCoord2f( 0, flLeftU   + (float) x * flUWidth, flTopV + (float)(y+1) * flVHeight);
			meshBuilder.AdvanceVertex();
		}
	}
#endif

	meshBuilder.End();
	pMesh->Draw();

	pRenderContext->MatrixMode( MATERIAL_VIEW );
	pRenderContext->PopMatrix();

	pRenderContext->MatrixMode( MATERIAL_PROJECTION );
	pRenderContext->PopMatrix();
}


static ConVar r_queued_post_processing( "r_queued_post_processing", "0"
#if defined( L4D2 )
	, 0x0000000A
#endif
 );

// How much to dice up the screen during post-processing on 360
// This has really marginal effects, but 4x1 does seem vaguely better for post-processing
#if defined( L4D2 )
static ConVar mat_postprocess_x( "mat_postprocess_x", "4", 
#if defined( L4D2 )
	0x0000000A
#else
	FCVAR_DEVELOPMENTONLY
#endif
 );
static ConVar mat_postprocess_y( "mat_postprocess_y", "1", 
#if defined( L4D2 )
	0x0000000A
#else
	FCVAR_DEVELOPMENTONLY
#endif
 );
#else
static ConVar mat_postprocess_x( "mat_postprocess_x", "4" );
static ConVar mat_postprocess_y( "mat_postprocess_y", "1" );
#endif

#if defined( L4D2 )
static void Terror_RenderEnginePost(IMatRenderContext *ctx,int x,int y,int w,int h,bool postVGui,float bloomScale,bool engineBloom)
{
 static ConVarRef mat_colorcorrection("mat_colorcorrection");
 const bool cc=!postVGui && g_pColorCorrectionMgr->HasNonZeroColorCorrectionWeights() && mat_colorcorrection.GetBool();
 ctx->EnableColorCorrection(cc);
 g_TerrorPostMaterials.SetEngineBloomEnabled(engineBloom);
 IMaterial *post=g_TerrorPostMaterials.GetEnginePost();
 const bool local=post->FindVar("$localcontrastenable",NULL,false)->GetIntValue()!=0 && mat_local_contrast_enable.GetBool();
 const bool vomit=Terror_HasVomitScreenSpaceParticles();
 if(!s_bScreenEffectTextureIsUpdated)
 {
  UpdateScreenEffectTexture(0,x,y,w,h,false);
  s_bScreenEffectTextureIsUpdated=true;
 }
 if((!postVGui && bloomScale>0.0f) || local || vomit)
 {
  Terror_PrepareEnginePost(ctx);
  if(vomit) Terror_RenderVomitScreenSpaceParticles(ctx);
 }
 ITexture *src=materials->FindTexture("_rt_FullFrameFB",TEXTURE_GROUP_RENDER_TARGET);
 ITexture *small=materials->FindTexture("_rt_SmallFB1",TEXTURE_GROUP_RENDER_TARGET);
 const int fw=src->GetActualWidth(),fh=src->GetActualHeight();
 Vector4D bloomUV((float)x*.25f,(float)y*.25f-.5f,(float)(x+w)*.25f-1.0f,(float)(y+h)*.25f-1.0f);
 Vector4D fbUV((float)x,(float)y,(float)(x+w-1),(float)(y+h-1));
 Vector2D texSize((float)fw,(float)fh);
 static bool firstFrame=true;
 if(!firstFrame || !cc)
 {
  CEnginePostMaterialProxy::SetupEnginePostMaterial(bloomUV,fbUV,texSize,false,false,cc,0.0f);
  ctx->DrawScreenSpaceRectangle(post,0,0,w,h,bloomUV.x,bloomUV.y,bloomUV.z,bloomUV.w,
   small->GetActualWidth(),small->GetActualHeight(),GetClientWorldEntity()->GetClientRenderable(),
   mat_postprocess_x.GetInt(),mat_postprocess_y.GetInt());
 }
 firstFrame=false;
}
#endif

void DoEnginePostProcessing( int x, int y, int w, int h, bool bFlashlightIsOn, bool bPostVGui )
{
	tmZone( TELEMETRY_LEVEL0, TMZF_NONE, "%s", __FUNCTION__ );
#if defined( L4D2 )
	if ( IsTransitionStatsPostProcessingSuppressed() ) return;
#endif

	CMatRenderContextPtr pRenderContext( materials );

	if ( g_bDumpRenderTargets )
	{
		g_bDumpRenderTargets = false;   // Turn off from previous frame
	}

	if ( mat_dump_rts.GetBool() )
	{
		g_bDumpRenderTargets = true;    // Dump intermediate render targets this frame
		s_nRTIndex = 0;                 // Used for numbering the TGA files for easy browsing
		mat_dump_rts.SetValue( 0 );     // We only want to capture one frame, on rising edge of this convar

		DumpTGAofRenderTarget( w, h, "BackBuffer" );
	}

#if defined( _X360 )
	pRenderContext->PushVertexShaderGPRAllocation( 16 ); //max out pixel shader threads
#endif

	if ( r_queued_post_processing.GetInt() )
	{
		ICallQueue *pCallQueue = pRenderContext->GetCallQueue();
		if ( pCallQueue )
		{
			pCallQueue->QueueCall( DoEnginePostProcessing, x, y, w, h, bFlashlightIsOn, bPostVGui );
			return;
		}
	}

#if defined( L4D2 )
	Terror_UpdateTonemapController();
#endif
	float flBloomScale = GetBloomAmount();

	HDRType_t hdrType = g_pMaterialSystemHardwareConfig->GetHDRType();

	g_bFlashlightIsOn = bFlashlightIsOn;

	// Use the appropriate autoexposure min / max settings.
	// Mapmaker's overrides the convar settings.
	float flAutoExposureMin;
	float flAutoExposureMax;
	GetExposureRange( &flAutoExposureMin, &flAutoExposureMax );

#if defined( L4D2 )
	s_bScreenEffectTextureIsUpdated = false;
	if ( hdrType != HDR_TYPE_NONE )
		DoPreBloomTonemapping( pRenderContext, x, y, w, h, flAutoExposureMin, flAutoExposureMax );
	// The DX12 post stack owns bloom, lens effects and the final tone curve. The L4D2 engine post then
	// applies colour correction, vignette, grain, local contrast, vomit and fade to its composite.
	const bool bPostProcessDX12 = !bPostVGui && PostProcessDX12_Active();
	if ( bPostProcessDX12 )
	{
		pRenderContext->SetRenderTarget( NULL );
		PostProcessDX12_Render( pRenderContext, x, y, w, h, flBloomScale, false );
		s_bScreenEffectTextureIsUpdated = false;
	}
	if ( mat_postprocess_enable.GetBool() )
		Terror_RenderEnginePost( pRenderContext, x, y, w, h, bPostVGui, bPostProcessDX12 ? 0.0f : flBloomScale, !bPostProcessDX12 );
	if ( hdrType != HDR_TYPE_NONE )
		DoPostBloomTonemapping( pRenderContext, x, y, w, h, flAutoExposureMin, flAutoExposureMax );
#else

	if ( mat_debug_bloom.GetInt() == 1 )
	{
		DrawBloomDebugBoxes( pRenderContext );
	}

	switch( hdrType )
	{   
		case HDR_TYPE_NONE:
		case HDR_TYPE_INTEGER:
		{
			s_bScreenEffectTextureIsUpdated = false;

			if ( hdrType != HDR_TYPE_NONE )
			{
				// Native post consumes the already exposed scene; these queries still drive the next frame's
				// world/fog tonemap scale and the exposure debug views, not just the legacy bloom composite.
				DoPreBloomTonemapping( pRenderContext, x, y, w, h, flAutoExposureMin, flAutoExposureMax );
			}

			// Set software-AA on by default for 360
			if ( mat_software_aa_strength.GetFloat() == -1.0f )
			{
				if ( IsX360() )
				{
					mat_software_aa_strength.SetValue( 1.0f );
					if ( g_pMaterialSystem->GetCurrentConfigForVideoCard().m_VideoMode.m_Height > 480 )
					{
						mat_software_aa_quality.SetValue( 0 );
					}
					else
					{
						// For standard-def, we have fewer pixels so we can afford 'high quality' mode (5->9 taps/pixel)
						mat_software_aa_quality.SetValue( 1 );
					}
				}
				else
				{
					mat_software_aa_strength.SetValue( 0.0f );
				}
			}

			// Same trick for setting up the vgui aa strength
			if ( mat_software_aa_strength_vgui.GetFloat() == -1.0f )
			{
				if ( IsX360() && (g_pMaterialSystem->GetCurrentConfigForVideoCard().m_VideoMode.m_Height == 720) )
				{
					mat_software_aa_strength_vgui.SetValue( 2.0f );
				}
				else
				{
					mat_software_aa_strength_vgui.SetValue( 1.0f );
				}
			}

			float flAAStrength;

			// We do a second AA blur pass over the TF intro menus. use mat_software_aa_strength_vgui there instead
			if ( IsX360() && bPostVGui )
			{
				flAAStrength = mat_software_aa_strength_vgui.GetFloat();
			}
			else
			{
				flAAStrength = mat_software_aa_strength.GetFloat();
			}

			static ConVarRef mat_colorcorrection( "mat_colorcorrection" );

			// bloom, software-AA and colour-correction (applied in 1 pass, after generation of the bloom texture)
			bool  bPerformSoftwareAA	= IsX360() && ( engine->GetDXSupportLevel() >= 90 ) && ( flAAStrength != 0.0f );
			bool  bPerformBloom			= !bPostVGui && ( flBloomScale > 0.0f ) && ( engine->GetDXSupportLevel() >= 90 );
			bool  bPerformColCorrect	= !bPostVGui && 
										  ( g_pMaterialSystemHardwareConfig->GetDXSupportLevel() >= 90) &&
										  ( g_pMaterialSystemHardwareConfig->GetHDRType() != HDR_TYPE_FLOAT ) &&
										  g_pColorCorrectionMgr->HasNonZeroColorCorrectionWeights() &&
										  mat_colorcorrection.GetInt();
			bool  bSplitScreenHDR		= mat_show_ab_hdr.GetInt();
			const bool bPostProcessDX12 = PostProcessDX12_Active();
			if ( bPostProcessDX12 )
			{
				pRenderContext->SetRenderTarget( NULL );
				PostProcessDX12_Render( pRenderContext, x, y, w, h, flBloomScale, bPerformColCorrect );
			}
			else
			{
				pRenderContext->EnableColorCorrection( bPerformColCorrect );
			}
			if ( !bPostProcessDX12 && ( bPerformBloom || bPerformSoftwareAA || bPerformColCorrect ) )
			{
				tmZone( TELEMETRY_LEVEL0, TMZF_NONE, "ColorCorrection" );

				ITexture *pSrc = materials->FindTexture( "_rt_FullFrameFB", TEXTURE_GROUP_RENDER_TARGET );
				int nSrcWidth = pSrc->GetActualWidth();
				int nSrcHeight = pSrc->GetActualHeight();

				ITexture *dest_rt1 = materials->FindTexture( "_rt_SmallFB1", TEXTURE_GROUP_RENDER_TARGET );

				if ( !s_bScreenEffectTextureIsUpdated )
				{
					// NOTE: UpdateScreenEffectTexture() uses StretchRect, so _rt_FullFrameFB is always 100%
					//		 filled, even when the viewport is not fullscreen (e.g. with 'mat_viewportscale 0.5')
					UpdateScreenEffectTexture( 0, x, y, w, h, true );
					s_bScreenEffectTextureIsUpdated = true;
				}

				if ( bPerformBloom )
				{
					Generate8BitBloomTexture( pRenderContext, flBloomScale, x, y, w, h );
				}

				// Now add bloom (dest_rt0) to the framebuffer and perform software anti-aliasing and
				// colour correction, all in one pass (improves performance, reduces quantization errors)
				//
				// First, set up texel coords (in the bloom and fb textures) at the centres of the outer pixel of the viewport:
				Vector4D fullViewportPostSrcCorners(	0.0f,	-0.5f,	nSrcWidth/4-1,	nSrcHeight/4-1 );
				Vector4D fullViewportPostDestCorners(	0.0f,	 0.0f,	nSrcWidth - 1,	nSrcHeight - 1 );
				Rect_t   fullViewportPostDestRect = {	x,		 y,		w,				h };
				Vector2D destTexSize(									nSrcWidth,		nSrcHeight );

				// When the viewport is not fullscreen, the UV-space size of a pixel changes
				// (due to a stretchrect blit being used in UpdateScreenEffectTexture()), so
				// we need to adjust the corner-pixel UVs sent to our drawrect call:
				Vector2D uvScale(	( nSrcWidth  - ( nSrcWidth  / (float)w ) ) / ( nSrcWidth  - 1 ),
									( nSrcHeight - ( nSrcHeight / (float)h ) ) / ( nSrcHeight - 1 ) );
				CenterScaleQuadUVs( fullViewportPostSrcCorners,  uvScale );
				CenterScaleQuadUVs( fullViewportPostDestCorners, uvScale );

				Rect_t   partialViewportPostDestRect   = fullViewportPostDestRect;
				Vector4D partialViewportPostSrcCorners = fullViewportPostSrcCorners;
				if ( debug_postproc.GetInt() == 2 )
				{
					// Restrict the post effects to the centre quarter of the screen
					// (we only use a portion of the bloom texture, so this *does* affect bloom texture UVs)
					partialViewportPostDestRect.x		+= 0.25f*fullViewportPostDestRect.width;
					partialViewportPostDestRect.y		+= 0.25f*fullViewportPostDestRect.height;
					partialViewportPostDestRect.width	-= 0.50f*fullViewportPostDestRect.width;
					partialViewportPostDestRect.height	-= 0.50f*fullViewportPostDestRect.height;

					// This math interprets texel coords as being at corner pixel centers (*not* at corner vertices):
					Vector2D uvScalePost(	1.0f - ( (w / 2) / (float)(w - 1) ),
										1.0f - ( (h / 2) / (float)(h - 1) ) );
					CenterScaleQuadUVs( partialViewportPostSrcCorners, uvScalePost );
				}

				// Temporary hack... Color correction was crashing on the first frame 
				// when run outside the debugger for some mods (DoD). This forces it to skip
				// a frame, ensuring we don't get the weird texture crash we otherwise would.
				// FIXME: This will be removed when the true cause is found [added: Main CL 144694]
				static bool bFirstFrame = !IsX360();
				if( !bFirstFrame || !bPerformColCorrect )
				{
					bool bFBUpdated = false;

					if ( mat_postprocessing_combine.GetInt() )
					{
						// Perform post-processing in one combined pass

						IMaterial *post_mat = CEnginePostMaterialProxy::SetupEnginePostMaterial( fullViewportPostSrcCorners, fullViewportPostDestCorners, destTexSize, bPerformSoftwareAA, bPerformBloom, bPerformColCorrect, flAAStrength );

						if (bSplitScreenHDR)
						{
							pRenderContext->SetScissorRect( partialViewportPostDestRect.width / 2, 0, partialViewportPostDestRect.width, partialViewportPostDestRect.height, true );
						}

						pRenderContext->DrawScreenSpaceRectangle(post_mat,
                                                                 // TomF - offset already done by the viewport.
																 0,0, //partialViewportPostDestRect.x,				partialViewportPostDestRect.y, 
																 partialViewportPostDestRect.width,			partialViewportPostDestRect.height, 
																 partialViewportPostSrcCorners.x,			partialViewportPostSrcCorners.y, 
																 partialViewportPostSrcCorners.z,			partialViewportPostSrcCorners.w, 
																 dest_rt1->GetActualWidth(),dest_rt1->GetActualHeight(),
																 GetClientWorldEntity()->GetClientRenderable(),
																 mat_postprocess_x.GetInt(), mat_postprocess_y.GetInt() );

						if (bSplitScreenHDR)
						{
							pRenderContext->SetScissorRect( -1, -1, -1, -1, false );
						}
						bFBUpdated = true;
					}
					else
					{
						// Perform post-processing in three separate passes
						if ( bPerformSoftwareAA )
						{
							IMaterial *aa_mat = CEnginePostMaterialProxy::SetupEnginePostMaterial( fullViewportPostSrcCorners, fullViewportPostDestCorners, destTexSize, bPerformSoftwareAA, false, false, flAAStrength );

							if (bSplitScreenHDR)
							{
								pRenderContext->SetScissorRect( partialViewportPostDestRect.width / 2, 0, partialViewportPostDestRect.width, partialViewportPostDestRect.height, true );
							}

							pRenderContext->DrawScreenSpaceRectangle(aa_mat,
                                                                     // TODO: check if offsets should be 0,0 here, as with the combined-pass case
																	 partialViewportPostDestRect.x,				partialViewportPostDestRect.y, 
																	 partialViewportPostDestRect.width,			partialViewportPostDestRect.height, 
																	 partialViewportPostSrcCorners.x,			partialViewportPostSrcCorners.y, 
																	 partialViewportPostSrcCorners.z,			partialViewportPostSrcCorners.w, 
																	 dest_rt1->GetActualWidth(),dest_rt1->GetActualHeight(),
																	 GetClientWorldEntity()->GetClientRenderable());

							if (bSplitScreenHDR)
							{
								pRenderContext->SetScissorRect( -1, -1, -1, -1, false );
							}
							bFBUpdated = true;
						}

						if ( bPerformBloom )
						{
							IMaterial *bloom_mat = CEnginePostMaterialProxy::SetupEnginePostMaterial( fullViewportPostSrcCorners, fullViewportPostDestCorners, destTexSize, false, bPerformBloom, false, flAAStrength );

							if (bSplitScreenHDR)
							{
								pRenderContext->SetScissorRect( partialViewportPostDestRect.width / 2, 0, partialViewportPostDestRect.width, partialViewportPostDestRect.height, true );
							}

							pRenderContext->DrawScreenSpaceRectangle(bloom_mat,
                                                                     // TODO: check if offsets should be 0,0 here, as with the combined-pass case
																	 partialViewportPostDestRect.x,				partialViewportPostDestRect.y, 
																	 partialViewportPostDestRect.width,			partialViewportPostDestRect.height, 
																	 partialViewportPostSrcCorners.x,			partialViewportPostSrcCorners.y, 
																	 partialViewportPostSrcCorners.z,			partialViewportPostSrcCorners.w, 
																	 dest_rt1->GetActualWidth(),dest_rt1->GetActualHeight(),
																	 GetClientWorldEntity()->GetClientRenderable());

							if (bSplitScreenHDR)
							{
								pRenderContext->SetScissorRect( -1, -1, -1, -1, false );
							}
							bFBUpdated = true;
						}

						if ( bPerformColCorrect )
						{
							if ( bFBUpdated )
							{
								Rect_t actualRect;
								UpdateScreenEffectTexture( 0, x, y, w, h, false, &actualRect );
							}

							IMaterial *colcorrect_mat = CEnginePostMaterialProxy::SetupEnginePostMaterial( fullViewportPostSrcCorners, fullViewportPostDestCorners, destTexSize, false, false, bPerformColCorrect, flAAStrength );

							if (bSplitScreenHDR)
							{
								pRenderContext->SetScissorRect( partialViewportPostDestRect.width / 2, 0, partialViewportPostDestRect.width, partialViewportPostDestRect.height, true );
							}

							pRenderContext->DrawScreenSpaceRectangle(colcorrect_mat,
                                                                     // TODO: check if offsets should be 0,0 here, as with the combined-pass case
																	 partialViewportPostDestRect.x,				partialViewportPostDestRect.y, 
																	 partialViewportPostDestRect.width,			partialViewportPostDestRect.height, 
																	 partialViewportPostSrcCorners.x,			partialViewportPostSrcCorners.y, 
																	 partialViewportPostSrcCorners.z,			partialViewportPostSrcCorners.w, 
																	 dest_rt1->GetActualWidth(),dest_rt1->GetActualHeight(),
																	 GetClientWorldEntity()->GetClientRenderable());

							if (bSplitScreenHDR)
							{
								pRenderContext->SetScissorRect( -1, -1, -1, -1, false );
							}
							bFBUpdated  = true;
						}
					}

					bool bVisionOverride = ( localplayer_visionflags.GetInt() & ( 0x01 ) ); // Pyro-vision Goggles

					if ( bVisionOverride && g_pMaterialSystemHardwareConfig->SupportsPixelShaders_2_0() && pyro_vignette.GetInt() > 0 )
					{
						if ( bFBUpdated )
						{
							Rect_t actualRect;
							UpdateScreenEffectTexture( 0, x, y, w, h, false, &actualRect );
						}

						DrawPyroVignette(
                            // TODO: check if offsets should be 0,0 here, as with the combined-pass case
                            partialViewportPostDestRect.x,				partialViewportPostDestRect.y, 
							partialViewportPostDestRect.width,			partialViewportPostDestRect.height, 
							partialViewportPostSrcCorners.x,			partialViewportPostSrcCorners.y, 
							partialViewportPostSrcCorners.z,			partialViewportPostSrcCorners.w, 
							GetClientWorldEntity()->GetClientRenderable() );

						IMaterial *pPyroVisionPostMaterial = PostProcessMaterial( PPMAT_PYRO_POST );
						DrawPyroPost( pPyroVisionPostMaterial,
                            // TODO: check if offsets should be 0,0 here, as with the combined-pass case
							partialViewportPostDestRect.x,				partialViewportPostDestRect.y, 
							partialViewportPostDestRect.width,			partialViewportPostDestRect.height, 
							partialViewportPostSrcCorners.x,			partialViewportPostSrcCorners.y, 
							partialViewportPostSrcCorners.z,			partialViewportPostSrcCorners.w, 
							dest_rt1->GetActualWidth(),dest_rt1->GetActualHeight(),
							GetClientWorldEntity()->GetClientRenderable() );
					}

					if ( g_bDumpRenderTargets )
					{
						DumpTGAofRenderTarget( partialViewportPostDestRect.width, partialViewportPostDestRect.height, "EnginePost" );
					}
				}
				bFirstFrame = false;
			}

			if ( hdrType != HDR_TYPE_NONE )
			{
				DoPostBloomTonemapping( pRenderContext, x, y, w, h, flAutoExposureMin, flAutoExposureMax );
			}
		}
		break;

		case HDR_TYPE_FLOAT:
		{
			int dest_width,dest_height;
			pRenderContext->GetRenderTargetDimensions( dest_width, dest_height );
			if (mat_dynamic_tonemapping.GetInt() || mat_show_histogram.GetInt())
			{
				g_HDR_HistogramSystem.Update();
				//				Warning("avg_lum=%f\n",g_HDR_HistogramSystem.GetTargetTonemapScalar());
				if ( mat_dynamic_tonemapping.GetInt() )
				{
					// Algorithm 1 returns the exposure scale itself (histogram adjustment times the current scale),
					// which is what the integer path applies. 0.18/x and 0.023/x are for algorithm 0's
					// 0.005/luminance return; on algorithm 1 they clamp to mat_autoexposure_min every frame.
					if ( mat_tonemap_algorithm.GetInt() == 1 )
					{
						float flTargetScalar = g_HDR_HistogramSystem.GetTargetTonemapScalar();
						float scalevalue = MAX( flAutoExposureMin, MIN( flAutoExposureMax, flTargetScalar ) );
						scalevalue = MAX( 0.001f, scalevalue );
						SetToneMapScale( pRenderContext, scalevalue, flAutoExposureMin, flAutoExposureMax );
					}
					else
					{
						float avg_lum = MAX( 0.0001f, g_HDR_HistogramSystem.GetTargetTonemapScalar() );
						float scalevalue = MAX( flAutoExposureMin, MIN( flAutoExposureMax, 0.18f / avg_lum ) );
						pRenderContext->SetGoalToneMappingScale( scalevalue );
						mat_hdr_tonemapscale.SetValue( scalevalue );
					}
				}
			}
			
			IMaterialVar *pBloomAmountVar = PostProcessVar( PPMAT_FLOAT_COMBINE, PPVAR_BLOOM_AMOUNT );
			pBloomAmountVar->SetFloatValue( flBloomScale );
			
			PostProcessingPass* selectedHDR;
			
			if ( PostProcessDX12_Active() )
			{
				selectedHDR = HDRFinal_Float_NoBloom;
			}
			else if ( flBloomScale > 0.0 )
			{
				selectedHDR = HDRFinal_Float;
			}
			else
			{
				selectedHDR = HDRFinal_Float_NoBloom;
			}
			
			if (mat_show_ab_hdr.GetInt())
			{
				ClipBox splitScreenClip;
				
				splitScreenClip.m_minx = splitScreenClip.m_miny = 0;

				// Left half
				splitScreenClip.m_maxx = dest_width / 2;
				splitScreenClip.m_maxy = dest_height - 1;
				
				ApplyPostProcessingPasses(HDRSimulate_NonHDR, &splitScreenClip);
				
				// Right half
				splitScreenClip.m_minx = splitScreenClip.m_maxx;
				splitScreenClip.m_maxx = dest_width - 1;
				
				ApplyPostProcessingPasses(selectedHDR, &splitScreenClip);
				
			}
			else
			{
				ApplyPostProcessingPasses(selectedHDR);
			}

			pRenderContext->SetRenderTarget(NULL);
			if ( PostProcessDX12_Active() )
			{
				static ConVarRef mat_colorcorrection( "mat_colorcorrection" );
				PostProcessDX12_Render( pRenderContext, 0, 0, dest_width, dest_height, flBloomScale,
					g_pColorCorrectionMgr->HasNonZeroColorCorrectionWeights() && mat_colorcorrection.GetInt() );
			}
			if ( mat_show_histogram.GetInt() && (engine->GetDXSupportLevel()>=90))
				g_HDR_HistogramSystem.DisplayHistogram();
			if ( mat_dynamic_tonemapping.GetInt() && mat_tonemap_algorithm.GetInt() != 1 )
			{
				float avg_lum = MAX( 0.0001f, g_HDR_HistogramSystem.GetTargetTonemapScalar() );
				float scalevalue = MAX( flAutoExposureMin, MIN( flAutoExposureMax, 0.023f / avg_lum ) );
				SetToneMapScale( pRenderContext, scalevalue, flAutoExposureMin, flAutoExposureMax );
			}
			pRenderContext->SetRenderTarget( NULL );
			break;
		}
	}
#endif

#if defined( _X360 )
	pRenderContext->PopVertexShaderGPRAllocation();
#endif
}

// Motion Blur Material Proxy =========================================================================================
static float g_vMotionBlurValues[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
// Velocity blur can replay after the client starts preparing the next frame. Its proxy
// reads the captured replay values, never the next frame's client-side legacy values.
static thread_local const float *s_pMotionBlurReplayValues = NULL;
class CMotionBlurMaterialProxy : public CEntityMaterialProxy
{
public:
	CMotionBlurMaterialProxy();
	virtual ~CMotionBlurMaterialProxy();
	virtual bool Init( IMaterial *pMaterial, KeyValues *pKeyValues );
	virtual void OnBind( C_BaseEntity *pEntity );
	virtual IMaterial *GetMaterial();

private:
	IMaterialVar *m_pMaterialParam;
};

CMotionBlurMaterialProxy::CMotionBlurMaterialProxy()
{
	m_pMaterialParam = NULL;
}

CMotionBlurMaterialProxy::~CMotionBlurMaterialProxy()
{
	// Do nothing
}

bool CMotionBlurMaterialProxy::Init( IMaterial *pMaterial, KeyValues *pKeyValues )
{
	bool bFoundVar = false;

	m_pMaterialParam = pMaterial->FindVar( "$MotionBlurInternal", &bFoundVar, false );
	if ( bFoundVar == false)
		return false;

	return true;
}

void CMotionBlurMaterialProxy::OnBind( C_BaseEntity *pEnt )
{
	if ( m_pMaterialParam != NULL )
	{
		m_pMaterialParam->SetVecValue( s_pMotionBlurReplayValues ? s_pMotionBlurReplayValues : g_vMotionBlurValues, 4 );
	}
}

IMaterial *CMotionBlurMaterialProxy::GetMaterial()
{
	if ( m_pMaterialParam == NULL)
		return NULL;

	return m_pMaterialParam->GetOwningMaterial();
}

EXPOSE_INTERFACE( CMotionBlurMaterialProxy, IMaterialProxy, "MotionBlur" IMATERIAL_PROXY_INTERFACE_VERSION );

//=====================================================================================================================
// Image-space Motion Blur ============================================================================================
//=====================================================================================================================
ConVar mat_motion_blur_forward_enabled( "mat_motion_blur_forward_enabled", "0"
#if defined( L4D2 )
	, 0x0000000A
#endif
 );
ConVar mat_motion_blur_falling_min( "mat_motion_blur_falling_min", "10.0"
#if defined( L4D2 )
	, 0x0000000A
#endif
 );
ConVar mat_motion_blur_falling_max( "mat_motion_blur_falling_max", "20.0"
#if defined( L4D2 )
	, 0x0000000A
#endif
 );
ConVar mat_motion_blur_falling_intensity( "mat_motion_blur_falling_intensity", "1.0"
#if defined( L4D2 )
	, 0x0000000A
#endif
 );
//ConVar mat_motion_blur_roll_intensity( "mat_motion_blur_roll_intensity", "1.0" );
ConVar mat_motion_blur_rotation_intensity( "mat_motion_blur_rotation_intensity", "1.0"
#if defined( L4D2 )
	, 0x0000000A
#endif
 );
ConVar mat_motion_blur_strength( "mat_motion_blur_strength", "1.0"
#if defined( L4D2 )
	, 0x0000000A
#endif
 );
ConVar mat_motion_blur_shutter_time( "mat_motion_blur_shutter_time", "0.016667", FCVAR_ARCHIVE, "DX12 velocity motion blur exposure time in seconds.", true, 0.0f, false, 0.0f );
ConVar mat_motion_blur_static_skip( "mat_motion_blur_static_skip", "1", FCVAR_CHEAT, "Skip CPU-certified stationary DX12 velocity blur; 0 retains the reference pass for comparisons." );

// pVelocityRegion (DX12 velocity blur): the valid motion-vector region in texels, {width, height, 0, 0}; it differs from
// the target under a temporal render scale, where the blur runs on the upscaled frame.
static void DrawMotionBlurQuad( PostProcessMaterialId material, int x, int y, int w, int h, const float *pVelocityRegion = NULL )
{
	CMatRenderContextPtr pRenderContext( materials );
	ITexture *pSrc = materials->FindTexture( "_rt_FullFrameFB", TEXTURE_GROUP_RENDER_TARGET );
	int nSrcWidth = pSrc->GetActualWidth();
	int nSrcHeight = pSrc->GetActualHeight();
	int dest_width, dest_height, nDummy;
	pRenderContext->GetViewport( nDummy, nDummy, dest_width, dest_height );

	IMaterial *pMatMotionBlur = PostProcessMaterial( material );
	IMaterialVar *pBase = NULL;
	ITexture *pSavedBase = NULL;
	// Non-float HDR copies the backbuffer into _rt_FullFrameFB. Float HDR renders into that
	// texture, so sample the other full-frame buffer (shared with the histogram this frame).
	pRenderContext->BeginPIXEvent( PIX_VALVE_ORANGE, "Motion blur copy" );
	if ( g_pMaterialSystemHardwareConfig->GetHDRType() != HDR_TYPE_FLOAT )
	{
		UpdateScreenEffectTexture( 0, x, y, w, h, true );
	}
	else if ( ITexture *pCopy = AcquireBoundFrameCopy( pRenderContext, x, y, w, h ) )
	{
		pSrc = pCopy;
		nSrcWidth = pCopy->GetActualWidth();
		nSrcHeight = pCopy->GetActualHeight();
		RetargetBaseTexture( material, pCopy, pBase, pSavedBase );
	}
	pRenderContext->EndPIXEvent();

	if ( pMatMotionBlur != NULL && pVelocityRegion != NULL )
	{
		IMaterialVar *pRegion = PostProcessVar( material, PPVAR_VELOCITY_REGION );
		if ( pRegion )
			pRegion->SetVecValue( pVelocityRegion, 4 );
	}
	if ( pMatMotionBlur != NULL )
	{
		PIXEVENT( pRenderContext, "Motion blur gather" );
		pRenderContext->DrawScreenSpaceRectangle(
			pMatMotionBlur,
			0, 0, dest_width, dest_height,
			0, 0, nSrcWidth-1, nSrcHeight-1,
			nSrcWidth, nSrcHeight, GetClientWorldEntity()->GetClientRenderable() );

		if ( g_bDumpRenderTargets )
		{
			DumpTGAofRenderTarget( dest_width, dest_height, "MotionBlur" );
		}
	}
	if ( pBase && pSavedBase )
		pBase->SetTextureValue( pSavedBase );
}

struct MotionBlurReplayDX12
{
	int x, y, w, h;
	int nVelocityWidth, nVelocityHeight;
	float flScale, flMaxBlurLength;
	bool bWholeScreenHot, bStaticSkip;
};

static void DrawVelocityMotionBlurDX12( MotionBlurReplayDX12 blur )
{
	CMatRenderContextPtr pRenderContext( materials );
	if ( ICallQueue *pQueue = pRenderContext->GetCallQueue() )
	{
		pQueue->QueueCall( DrawVelocityMotionBlurDX12, blur );
		return;
	}
	// Resolve, copy, tiles and gather must share the current ordered replay frame. A
	// client-thread query in queued mode would only see the previous frame's producer.
	if ( blur.bStaticSkip && !blur.bWholeScreenHot &&
		MotionVectorsDX12_BlurIsStatic( blur.flScale, blur.flMaxBlurLength, blur.nVelocityWidth, blur.nVelocityHeight ) )
		return;
	float flValues[4] = { blur.flScale, blur.flMaxBlurLength, 0.f, 0.f };
	if ( !blur.bWholeScreenHot )
	{
		int nTileX = 0, nTileY = 0;
		if ( MotionVectorsDX12_BuildBlurTiles( pRenderContext, blur.flMaxBlurLength, blur.nVelocityWidth, blur.nVelocityHeight, nTileX, nTileY ) )
		{
			flValues[2] = static_cast<float>( nTileX );
			flValues[3] = static_cast<float>( nTileY );
		}
	}
	const float flVelocityRegion[4] = { static_cast<float>( blur.nVelocityWidth ), static_cast<float>( blur.nVelocityHeight ), 0.f, 0.f };
	const float *pSavedValues = s_pMotionBlurReplayValues;
	s_pMotionBlurReplayValues = flValues;
	DrawMotionBlurQuad( PPMAT_MOTION_BLUR_MV, blur.x, blur.y, blur.w, blur.h, flVelocityRegion );
	s_pMotionBlurReplayValues = pSavedValues;
}

void DoImageSpaceMotionBlur( const CViewSetup &viewBlur, int x, int y, int w, int h )
{
#ifdef CSS_PERF_TEST
	return;
#endif
	static ConVarRef mat_motion_blur_enabled( "mat_motion_blur_enabled" );

	if ( ( !mat_motion_blur_enabled.GetInt() ) || ( g_pMaterialSystemHardwareConfig->GetDXSupportLevel() < 90 ) )
	{
		return;
	}
	static float s_flLastTimeUpdate = 0.0f;

	// DX12 velocity blur: blur vectors come from _rt_MotionVectors, scaled to an exposure of mat_motion_blur_shutter_time.
	float flMotionVectorDelta = 0.0f;
	if ( MotionVectorsDX12_Enabled() && MotionVectorsDX12_FrameValid( &flMotionVectorDelta ) )
	{
		static ConVarRef mat_motion_blur_percent_of_screen_max( "mat_motion_blur_percent_of_screen_max", true );
		const float flMaxBlurLength = mat_motion_blur_percent_of_screen_max.IsValid() ?
			( mat_motion_blur_percent_of_screen_max.GetFloat() / 100.0f ) : 0.04f;
		// No blur across hitches (same policy as the legacy path: slower than 15 fps).
		const float flMotionBlurScale = ( flMotionVectorDelta > 0.0f && flMotionVectorDelta <= ( 1.0f / 15.0f ) ) ?
			( mat_motion_blur_strength.GetFloat() * mat_motion_blur_shutter_time.GetFloat() / flMotionVectorDelta ) : 0.0f;
		// Yaw and pitch shift every pixel, least at the screen centre. Once that centre shift
		// is several times the gather-skip threshold the tile pass would only report "all hot".
		static QAngle s_angLast( 0, 0, 0 );
		static bool s_bHaveLast = false;
		bool bWholeScreenHot = false;
		if ( s_bHaveLast && flMotionBlurScale != 0.0f )
		{
			const float flFov = Max( viewBlur.fov, 1.0f );
			const float flYaw = fabsf( AngleDiff( viewBlur.angles[YAW], s_angLast[YAW] ) );
			const float flPitch = fabsf( AngleDiff( viewBlur.angles[PITCH], s_angLast[PITCH] ) );
			const float flCenterDeg = FastSqrt( flYaw * flYaw + flPitch * flPitch );
			const float flCenterUv = ( flCenterDeg / flFov ) * flMotionBlurScale;
			const float flSkipUv = 0.5f / Max( Max( w, h ), 1 );
			bWholeScreenHot = flCenterUv > flSkipUv * 4.0f;
		}
		s_angLast = viewBlur.angles;
		s_bHaveLast = true;
		// Keep the legacy path's timing coherent if the blur mode switches at runtime.
		s_flLastTimeUpdate = gpGlobals->realtime;
		if ( flMotionBlurScale == 0.0f )
			return;
		const MotionBlurReplayDX12 blur = { x, y, w, h, viewBlur.width, viewBlur.height,
			flMotionBlurScale, flMaxBlurLength, bWholeScreenHot, mat_motion_blur_static_skip.GetBool() };
		DrawVelocityMotionBlurDX12( blur );
		return;
	}

	//======================================================================================================//
	// Get these convars here to make it easier to remove them later and to default each client differently //
	//======================================================================================================//
	float flMotionBlurRotationIntensity = mat_motion_blur_rotation_intensity.GetFloat() * 0.15f; // The default is to not blur past 15% of the range
	float flMotionBlurRollIntensity = 0.3f; // * mat_motion_blur_roll_intensity.GetFloat(); // The default is to not blur past 30% of the range
	float flMotionBlurFallingIntensity = mat_motion_blur_falling_intensity.GetFloat();
	float flMotionBlurFallingMin = mat_motion_blur_falling_min.GetFloat();
	float flMotionBlurFallingMax = mat_motion_blur_falling_max.GetFloat();
	float flMotionBlurGlobalStrength = mat_motion_blur_strength.GetFloat();

	//===============================================================================//
	// Set global g_vMotionBlurValues[4] values so material proxy can get the values //
	//===============================================================================//
	if ( true )
	{
		//=====================//
		// Previous frame data //
		//=====================//
		static float s_flPreviousPitch = 0.0f;
		static float s_flPreviousYaw = 0.0f;
		static float s_vPreviousPositon[3] = { 0.0f, 0.0f, 0.0f };
		static matrix3x4_t s_mPreviousFrameBasisVectors;
		static float s_flNoRotationalMotionBlurUntil = 0.0f;
		//float vPreviousSideVec[3] = { s_mPreviousFrameBasisVectors[0][1], s_mPreviousFrameBasisVectors[1][1], s_mPreviousFrameBasisVectors[2][1] };
		//float vPreviousForwardVec[3] = { s_mPreviousFrameBasisVectors[0][0], s_mPreviousFrameBasisVectors[1][0], s_mPreviousFrameBasisVectors[2][0] };
		//float vPreviousUpVec[3] = { s_mPreviousFrameBasisVectors[0][2], s_mPreviousFrameBasisVectors[1][2], s_mPreviousFrameBasisVectors[2][2] };

		float flTimeElapsed = gpGlobals->realtime - s_flLastTimeUpdate;

		//===================================//
		// Get current pitch & wrap to +-180 //
		//===================================//
		float flCurrentPitch = viewBlur.angles[PITCH];
		while ( flCurrentPitch > 180.0f )
			flCurrentPitch -= 360.0f;
		while ( flCurrentPitch < -180.0f )
			flCurrentPitch += 360.0f;

		//=================================//
		// Get current yaw & wrap to +-180 //
		//=================================//
		float flCurrentYaw = viewBlur.angles[YAW];
		while ( flCurrentYaw > 180.0f )
			flCurrentYaw -= 360.0f;
		while ( flCurrentYaw < -180.0f )
			flCurrentYaw += 360.0f;

		//engine->Con_NPrintf( 0, "Blur Pitch: %6.2f   Yaw: %6.2f", flCurrentPitch, flCurrentYaw );
		//engine->Con_NPrintf( 1, "Blur FOV: %6.2f   Aspect: %6.2f   Ortho: %s", view.fov, view.m_flAspectRatio, view.m_bOrtho ? "Yes" : "No" );

		//===========================//
		// Get current basis vectors //
		//===========================//
		matrix3x4_t mCurrentBasisVectors;
		AngleMatrix( viewBlur.angles, mCurrentBasisVectors );

		float vCurrentSideVec[3] = { mCurrentBasisVectors[0][1], mCurrentBasisVectors[1][1], mCurrentBasisVectors[2][1] };
		float vCurrentForwardVec[3] = { mCurrentBasisVectors[0][0], mCurrentBasisVectors[1][0], mCurrentBasisVectors[2][0] };
		//float vCurrentUpVec[3] = { mCurrentBasisVectors[0][2], mCurrentBasisVectors[1][2], mCurrentBasisVectors[2][2] };

		//======================//
		// Get current position //
		//======================//
		float vCurrentPosition[3] = { viewBlur.origin.x, viewBlur.origin.y, viewBlur.origin.z };

		//===============================================================//
		// Evaluate change in position to determine if we need to update //
		//===============================================================//
		float vPositionChange[3] = { 0.0f, 0.0f, 0.0f };
		VectorSubtract( s_vPreviousPositon, vCurrentPosition, vPositionChange );
		if ( ( VectorLength( vPositionChange ) > 30.0f ) && ( flTimeElapsed >= 0.5f ) )
		{
			//=======================================================//
			// If we moved a far distance in one frame and more than //
			// half a second elapsed, disable motion blur this frame //
			//=======================================================//
			//engine->Con_NPrintf( 8, " Pos change && time > 0.5 seconds %f ", gpGlobals->realtime );

			g_vMotionBlurValues[0] = 0.0f;
			g_vMotionBlurValues[1] = 0.0f;
			g_vMotionBlurValues[2] = 0.0f;
			g_vMotionBlurValues[3] = 0.0f;
		}
		else if ( flTimeElapsed > ( 1.0f / 15.0f ) )
		{
			//==========================================//
			// If slower than 15 fps, don't motion blur //
			//==========================================//
			g_vMotionBlurValues[0] = 0.0f;
			g_vMotionBlurValues[1] = 0.0f;
			g_vMotionBlurValues[2] = 0.0f;
			g_vMotionBlurValues[3] = 0.0f;
		}
		else if ( VectorLength( vPositionChange ) > 50.0f )
		{
			//================================================================================//
			// We moved a far distance in a frame, use the same motion blur as last frame	  //
			// because I think we just went through a portal (should we ifdef this behavior?) //
			//================================================================================//
			//engine->Con_NPrintf( 8, " Position changed %f units @ %.2f time ", VectorLength( vPositionChange ), gpGlobals->realtime );

			s_flNoRotationalMotionBlurUntil = gpGlobals->realtime + 1.0f; // Wait a second until the portal craziness calms down
		}
		else
		{
			//====================//
			// Normal update path //
			//====================//
			// Compute horizontal and vertical fov
			float flHorizontalFov = viewBlur.fov;
			float flVerticalFov = (viewBlur.m_flAspectRatio <= 0.0f ) ? (viewBlur.fov ) : (viewBlur.fov  / viewBlur.m_flAspectRatio );
			//engine->Con_NPrintf( 2, "Horizontal Fov: %6.2f   Vertical Fov: %6.2f", flHorizontalFov, flVerticalFov );

			//=====================//
			// Forward motion blur //
			//=====================//
			float flViewDotMotion = DotProduct( vCurrentForwardVec, vPositionChange );
			if ( mat_motion_blur_forward_enabled.GetBool() ) // Want forward and falling
				g_vMotionBlurValues[2] = flViewDotMotion;
			else // Falling only
				g_vMotionBlurValues[2] = flViewDotMotion * fabs( vCurrentForwardVec[2] ); // Only want this if we're looking up or down;

			//====================================//
			// Yaw (Compensate for circle strafe) //
			//====================================//
			float flSideDotMotion = DotProduct( vCurrentSideVec, vPositionChange );
			float flYawDiffOriginal = s_flPreviousYaw - flCurrentYaw;
			if ( ( ( s_flPreviousYaw - flCurrentYaw > 180.0f ) || ( s_flPreviousYaw - flCurrentYaw < -180.0f ) ) &&
				 ( ( s_flPreviousYaw + flCurrentYaw > -180.0f ) && ( s_flPreviousYaw + flCurrentYaw < 180.0f ) ) )
				flYawDiffOriginal = s_flPreviousYaw + flCurrentYaw;

			float flYawDiffAdjusted = flYawDiffOriginal + ( flSideDotMotion / 3.0f ); // Yes, 3.0 is a magic number, sue me

			// Make sure the adjustment only lessens the effect, not magnify it or reverse it
			if ( flYawDiffOriginal < 0.0f )
				flYawDiffAdjusted = clamp ( flYawDiffAdjusted, flYawDiffOriginal, 0.0f );
			else
				flYawDiffAdjusted = clamp ( flYawDiffAdjusted, 0.0f, flYawDiffOriginal );

			// Use pitch to dampen yaw
			float flUndampenedYaw = flYawDiffAdjusted / flHorizontalFov;
			g_vMotionBlurValues[0] = flUndampenedYaw * ( 1.0f - ( fabs( flCurrentPitch ) / 90.0f ) ); // Dampen horizontal yaw blur based on pitch

			//engine->Con_NPrintf( 4, "flSideDotMotion: %6.2f   yaw diff: %6.2f  ( %6.2f, %6.2f )", flSideDotMotion, ( s_flPreviousYaw - flCurrentYaw ), flYawDiffOriginal, flYawDiffAdjusted );

			//=======================================//
			// Pitch (Compensate for forward motion) //
			//=======================================//
			float flPitchCompensateMask = 1.0f - ( ( 1.0f - fabs( vCurrentForwardVec[2] ) ) * ( 1.0f - fabs( vCurrentForwardVec[2] ) ) );
			float flPitchDiffOriginal = s_flPreviousPitch - flCurrentPitch;
			float flPitchDiffAdjusted = flPitchDiffOriginal;

			if ( flCurrentPitch > 0.0f )
				flPitchDiffAdjusted = flPitchDiffOriginal - ( ( flViewDotMotion / 2.0f ) * flPitchCompensateMask ); // Yes, 2.0 is a magic number, sue me
			else
				flPitchDiffAdjusted = flPitchDiffOriginal + ( ( flViewDotMotion / 2.0f ) * flPitchCompensateMask ); // Yes, 2.0 is a magic number, sue me

			// Make sure the adjustment only lessens the effect, not magnify it or reverse it
			if ( flPitchDiffOriginal < 0.0f )
				flPitchDiffAdjusted = clamp ( flPitchDiffAdjusted, flPitchDiffOriginal, 0.0f );
			else
				flPitchDiffAdjusted = clamp ( flPitchDiffAdjusted, 0.0f, flPitchDiffOriginal );

			g_vMotionBlurValues[1] = flPitchDiffAdjusted / flVerticalFov;

			//engine->Con_NPrintf( 5, "flViewDotMotion %6.2f, flPitchCompensateMask %6.2f, flPitchDiffOriginal %6.2f, flPitchDiffAdjusted %6.2f, g_vMotionBlurValues[1] %6.2f", flViewDotMotion, flPitchCompensateMask, flPitchDiffOriginal, flPitchDiffAdjusted, g_vMotionBlurValues[1]);

			//========================================================//
			// Roll (Enabled when we're looking down and yaw changes) //
			//========================================================//
			g_vMotionBlurValues[3] = flUndampenedYaw; // Roll starts out as undampened yaw intensity and is then scaled by pitch
			g_vMotionBlurValues[3] *= ( fabs( flCurrentPitch ) / 90.0f ) * ( fabs( flCurrentPitch ) / 90.0f ) * ( fabs( flCurrentPitch ) / 90.0f ); // Dampen roll based on pitch^3

			//engine->Con_NPrintf( 4, "[2] before scale and bias: %6.2f", g_vMotionBlurValues[2] );
			//engine->Con_NPrintf( 5, "[3] before scale and bias: %6.2f", g_vMotionBlurValues[3] );

			//==============================================================//
			// Time-adjust falling effect until we can do something smarter //
			//==============================================================//
			if ( flTimeElapsed > 0.0f )
				g_vMotionBlurValues[2] /= flTimeElapsed * 30.0f; // 1/30th of a second?
			else
				g_vMotionBlurValues[2] = 0.0f;

			// Scale and bias values after time adjustment
			g_vMotionBlurValues[2] = clamp( ( fabs( g_vMotionBlurValues[2] ) - flMotionBlurFallingMin ) / ( flMotionBlurFallingMax - flMotionBlurFallingMin ), 0.0f, 1.0f ) * ( g_vMotionBlurValues[2] >= 0.0f ? 1.0f : -1.0f );
			g_vMotionBlurValues[2] /= 30.0f; // To counter-adjust for time adjustment above

			//=================//
			// Apply intensity //
			//=================//
			g_vMotionBlurValues[0] *= flMotionBlurRotationIntensity * flMotionBlurGlobalStrength;
			g_vMotionBlurValues[1] *= flMotionBlurRotationIntensity * flMotionBlurGlobalStrength;
			g_vMotionBlurValues[2] *= flMotionBlurFallingIntensity * flMotionBlurGlobalStrength;
			g_vMotionBlurValues[3] *= flMotionBlurRollIntensity * flMotionBlurGlobalStrength;

			//===============================================================//
			// Dampen motion blur from 100%-0% as fps drops from 50fps-30fps //
			//===============================================================//
			if ( !IsX360() ) // I'm not doing this on the 360 yet since I can't test it
			{
				float flSlowFps = 30.0f;
				float flFastFps = 50.0f;
				float flCurrentFps = ( flTimeElapsed > 0.0f ) ? ( 1.0f / flTimeElapsed ) : 0.0f;
				float flDampenFactor = clamp( ( ( flCurrentFps - flSlowFps ) / ( flFastFps - flSlowFps ) ), 0.0f, 1.0f );

				//engine->Con_NPrintf( 4, "gpGlobals->realtime %.2f  gpGlobals->curtime %.2f", gpGlobals->realtime, gpGlobals->curtime );
				//engine->Con_NPrintf( 5, "flCurrentFps %.2f", flCurrentFps );
				//engine->Con_NPrintf( 7, "flTimeElapsed %.2f", flTimeElapsed );

				g_vMotionBlurValues[0] *= flDampenFactor;
				g_vMotionBlurValues[1] *= flDampenFactor;
				g_vMotionBlurValues[2] *= flDampenFactor;
				g_vMotionBlurValues[3] *= flDampenFactor;

				//engine->Con_NPrintf( 6, "Dampen: %.2f", flDampenFactor );
			}

			//engine->Con_NPrintf( 6, "Final values: { %6.2f%%, %6.2f%%, %6.2f%%, %6.2f%% }", g_vMotionBlurValues[0]*100.0f, g_vMotionBlurValues[1]*100.0f, g_vMotionBlurValues[2]*100.0f, g_vMotionBlurValues[3]*100.0f );
		}

		//============================================//
		// Zero out blur if still in that time window //
		//============================================//
		if ( gpGlobals->realtime < s_flNoRotationalMotionBlurUntil )
		{
			//engine->Con_NPrintf( 9, " No Rotation @ %f ", gpGlobals->realtime );

			// Zero out rotational blur but leave forward/falling blur alone
			g_vMotionBlurValues[0] = 0.0f; // X
			g_vMotionBlurValues[1] = 0.0f; // Y
			g_vMotionBlurValues[3] = 0.0f; // Roll
		}
		else
		{
			s_flNoRotationalMotionBlurUntil = 0.0f;
		}

		//====================================//
		// Store current frame for next frame //
		//====================================//
		VectorCopy( vCurrentPosition, s_vPreviousPositon );
		s_mPreviousFrameBasisVectors = mCurrentBasisVectors;
		s_flPreviousPitch = flCurrentPitch;
		s_flPreviousYaw = flCurrentYaw;
		s_flLastTimeUpdate = gpGlobals->realtime;
	}

	// Render quad and let the material proxy pick up the g_vMotionBlurValues[4] values just set above.
	DrawMotionBlurQuad( PPMAT_MOTION_BLUR, x, y, w, h );
}
