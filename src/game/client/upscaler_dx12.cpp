//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DX12 temporal upscaler client trigger. The client decides which RenderView is the temporal main view, its
//          render scale and when its history breaks; the backend owns provider selection, jitter and the fail-closed dispatch.
//
//===========================================================================//
#include "cbase.h"
#include "upscaler_dx12.h"
#include "motionvectors_dx12.h"
#include "framegen_dx12.h"
#include "igamesystem.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imaterialsystemhardwareconfig.h"
#include "shaderapi/ishaderapi.h"
#include "tier1/interface.h"
#include "renderparm.h"
#include "view_shared.h"

#include <math.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static bool s_bDX12 = false;
static IShaderAPI *s_pShaderAPI = nullptr;
// History gap: the next dispatch must carry reset (level load, cvar/mode change, view or frame discontinuity).
static bool s_bResetPending = true;
static bool s_bLastFrameEligible = false;
static bool s_bFrameLatched = false;
static Vector s_vecLastOrigin( 0, 0, 0 );
static bool s_bHaveLastOrigin = false;
static int s_nLastWidth = 0, s_nLastHeight = 0, s_nLastStatus = 0, s_nLastNrStatus = 0;
// A scaled dispatch reported no provider / feature creation failure / replay error: render native until the
// mode or the scale changes (a scaled feature that cannot exist must not be recreated every other frame).
static bool s_bScaleFailed = false;

void UpscalerDX12_ReconcileMSAA()
{
	// Looked up per call: a ConVarRef cached before the engine registers mat_antialias stays invalid.
	ConVarRef mat_antialias( "mat_antialias", true );
	if ( mat_antialias.IsValid() && mat_antialias.GetInt() != 0 )
	{
		mat_antialias.SetValue( 0 );
		Msg( "r_upscaler: temporal AA / frame generation replaces MSAA; mat_antialias set to 0\n" );
	}
}

static void OnUpscalerChanged( IConVar *pVar, const char *pOldValue, float flOldValue )
{
	s_bResetPending = true;
	s_bScaleFailed = false;
	ConVarRef var( pVar );
	if ( s_bDX12 && var.GetInt() != 0 )
		UpscalerDX12_ReconcileMSAA();
}

ConVar r_upscaler( "r_upscaler", "1", FCVAR_ARCHIVE,
	"DX12 temporal AA / upscaler: 0 off, 1 auto (DLSS / FSR / XeSS), 2 DLSS, 3 FSR (FSR 4 on AMD is untested), 4 XeSS, 5 SMAA T2x (native only, ignores r_upscaler_scale); r_upscaler_scale sets provider render scale",
	true, 0, true, 5, OnUpscalerChanged );

static void OnUpscalerScaleChanged( IConVar *pVar, const char *pOldValue, float flOldValue );

static void OnUpscalerLodBiasChanged( IConVar *pVar, const char *pOldValue, float flOldValue )
{
	s_bResetPending = true;
}

ConVar r_upscaler_lod_bias( "r_upscaler_lod_bias", "-1", FCVAR_ARCHIVE,
	"DX12 temporal texture mip LOD bias: -1 auto when r_upscaler_lod_bias_auto is 1; explicit range -2..0",
	true, -2.f, true, 0.f, OnUpscalerLodBiasChanged );

ConVar r_upscaler_lod_bias_auto( "r_upscaler_lod_bias_auto", "1", FCVAR_ARCHIVE,
	"DX12 temporal texture mip LOD bias: 1 uses provider/scale auto (-1 at native, -2 at half scale), 0 honors r_upscaler_lod_bias",
	true, 0, true, 1, OnUpscalerLodBiasChanged );

static void OnUpscalerScaleChanged( IConVar *pVar, const char *pOldValue, float flOldValue )
{
	s_bResetPending = true;
	s_bScaleFailed = false;
}
// Rendering below native resolution is the main lever on older GPUs: every pixel-bound pass (world shading, SSR,
// GTAO, volumetrics, post) shrinks with the render area. The upscaler reconstructs the unscaled frame.
ConVar r_upscaler_scale( "r_upscaler_scale", "1", FCVAR_ARCHIVE,
	"DX12 temporal upscaler render scale per axis: 1 native-resolution AA, 0.77 ultra quality, 0.667 quality, 0.58 balanced, 0.5 performance (needs r_upscaler 1-4; ignored by SMAA T2x)",
	true, 0.5f, true, 1.f, OnUpscalerScaleChanged );


// DLSS-NR (NVIDIA neural rendering, nvngx_dlssnr.dll) runs after the temporal AA, before the HUD. Layers chain: the
// temporal-AA frame -> NR -> NR output -> NR -> ... -> final image. Model tuning rebuilds every layer's feature.
static ConVar r_dlss_nr_layers( "r_dlss_nr_layers", "0", FCVAR_ARCHIVE,
	"DLSS-NR layers after the temporal AA (0 off); each layer is fed the previous layer's output and costs a full model pass",
	true, 0, true, DX12_NR_MAX_LAYERS );
static ConVar r_dlss_nr_preset( "r_dlss_nr_preset", "0", FCVAR_ARCHIVE, "DLSS-NR model preset: 0 default, 1-3 model presets", true, 0, true, 3 );
static ConVar r_dlss_nr_style( "r_dlss_nr_style", "0", FCVAR_ARCHIVE, "DLSS-NR style: 0 standard, 1 natural, 2 cinematic", true, 0, true, 2 );
static ConVar r_dlss_nr_intensity( "r_dlss_nr_intensity", "0.5", FCVAR_ARCHIVE, "DLSS-NR model intensity", true, 0, true, 2 );
static ConVar r_dlss_nr_local_structure( "r_dlss_nr_local_structure", "1", FCVAR_ARCHIVE, "DLSS-NR local structure strength", true, 0, true, 2 );
static ConVar r_dlss_nr_local_tone( "r_dlss_nr_local_tone", "0.2", FCVAR_ARCHIVE, "DLSS-NR local tone strength", true, 0, true, 2 );
static ConVar r_dlss_nr_skin_structure( "r_dlss_nr_skin_structure", "0.5", FCVAR_ARCHIVE, "DLSS-NR skin structure strength; -1 follows local structure", true, -1, true, 2 );
static ConVar r_dlss_nr_auto_mask( "r_dlss_nr_auto_mask", "1", FCVAR_ARCHIVE, "DLSS-NR automatic skin mask", true, 0, true, 1 );
static ConVar r_dlss_nr_ui_correction( "r_dlss_nr_ui_correction", "1", FCVAR_ARCHIVE, "DLSS-NR UI correction (the model's default; no UI layer is supplied)", true, 0, true, 1 );
static ConVar r_dlss_nr_white_point( "r_dlss_nr_white_point", "1", FCVAR_ARCHIVE,
	"DLSS-NR paper white: the scene value shown to the model as display white; higher treats highlights as less extreme", true, 0.01f, true, 64 );
static ConVar r_dlss_nr_transfer_strength( "r_dlss_nr_transfer_strength", "1", FCVAR_ARCHIVE,
	"DLSS-NR detail strength: how much of the model's answer reaches the frame; above 1 amplifies its luminance edit", true, 0, true, 2 );
static ConVar r_dlss_nr_colour_strength( "r_dlss_nr_colour_strength", "1", FCVAR_ARCHIVE,
	"DLSS-NR colour strength: 0 keeps the frame's hue, 1 takes the model's colour, above 1 over-saturates", true, 0, true, 4 );
static ConVar r_dlss_nr_max_ratio( "r_dlss_nr_max_ratio", "2", FCVAR_ARCHIVE, "DLSS-NR highlight guard: the most a pixel may be brightened or darkened by", true, 1, true, 8 );
static ConVar r_dlss_nr_reversible_mode( "r_dlss_nr_reversible_mode", "0", FCVAR_ARCHIVE,
	"DLSS-NR proxy curve: 0 soft knee, 1 Neutwo composed, 2 Neutwo replace, 3 hybrid composed, 4 hybrid replace (replace modes flash on highlights)", true, 0, true, 4 );
static ConVar r_dlss_nr_apply_model( "r_dlss_nr_apply_model", "1", FCVAR_ARCHIVE, "DLSS-NR: 0 runs the model but shows each layer's input (A/B)", true, 0, true, 1 );
static ConVar r_dlss_nr_debug_view( "r_dlss_nr_debug_view", "0", 0, "DLSS-NR debug view: 0 off, 1 model input proxy, 2 model answer, 3 amplified edit", true, 0, true, 3 );
static ConVar r_dlss_nr_compare( "r_dlss_nr_compare", "0", 0, "DLSS-NR comparison per layer: 0 off, 1 side by side, 2 wipe", true, 0, true, 2 );
static ConVar r_dlss_nr_compare_split( "r_dlss_nr_compare_split", "0.5", 0, "DLSS-NR wipe position", true, 0, true, 1 );
static ConVar r_dlss_nr_compare_zoom( "r_dlss_nr_compare_zoom", "1", 0, "DLSS-NR side by side: 1 fits the frame, 2 fills each half", true, 1, true, 2 );
static ConVar r_dlss_nr_compare_swap( "r_dlss_nr_compare_swap", "0", 0, "DLSS-NR comparison: put the edited frame on the other side", true, 0, true, 1 );

static void SubmitNrConfig( IMatRenderContext *pRenderContext )
{
	const int nConfig = ( r_dlss_nr_layers.GetInt() << DX12_NR_CONFIG_LAYERS_SHIFT ) | ( r_dlss_nr_preset.GetInt() << DX12_NR_CONFIG_PRESET_SHIFT ) |
		( r_dlss_nr_style.GetInt() << DX12_NR_CONFIG_STYLE_SHIFT ) | ( r_dlss_nr_reversible_mode.GetInt() << DX12_NR_CONFIG_REVERSIBLE_SHIFT ) |
		( r_dlss_nr_debug_view.GetInt() << DX12_NR_CONFIG_DEBUG_VIEW_SHIFT ) | ( r_dlss_nr_compare.GetInt() << DX12_NR_CONFIG_COMPARE_SHIFT ) |
		( r_dlss_nr_auto_mask.GetBool() ? DX12_NR_CONFIG_AUTO_MASK : 0 ) | ( r_dlss_nr_ui_correction.GetBool() ? DX12_NR_CONFIG_UI_CORRECTION : 0 ) |
		( r_dlss_nr_apply_model.GetBool() ? DX12_NR_CONFIG_APPLY_MODEL : 0 ) | ( r_dlss_nr_compare_swap.GetBool() ? DX12_NR_CONFIG_COMPARE_SWAP : 0 );
	pRenderContext->SetFloatRenderingParameter( FLOAT_RENDERPARM_DX12_NR_INTENSITY, r_dlss_nr_intensity.GetFloat() );
	pRenderContext->SetFloatRenderingParameter( FLOAT_RENDERPARM_DX12_NR_LOCAL_STRUCTURE, r_dlss_nr_local_structure.GetFloat() );
	pRenderContext->SetFloatRenderingParameter( FLOAT_RENDERPARM_DX12_NR_LOCAL_TONE, r_dlss_nr_local_tone.GetFloat() );
	pRenderContext->SetFloatRenderingParameter( FLOAT_RENDERPARM_DX12_NR_SKIN_STRUCTURE, r_dlss_nr_skin_structure.GetFloat() );
	pRenderContext->SetFloatRenderingParameter( FLOAT_RENDERPARM_DX12_NR_WHITE_POINT, r_dlss_nr_white_point.GetFloat() );
	pRenderContext->SetFloatRenderingParameter( FLOAT_RENDERPARM_DX12_NR_TRANSFER_STRENGTH, r_dlss_nr_transfer_strength.GetFloat() );
	pRenderContext->SetFloatRenderingParameter( FLOAT_RENDERPARM_DX12_NR_COLOUR_STRENGTH, r_dlss_nr_colour_strength.GetFloat() );
	pRenderContext->SetFloatRenderingParameter( FLOAT_RENDERPARM_DX12_NR_MAX_RATIO, r_dlss_nr_max_ratio.GetFloat() );
	pRenderContext->SetFloatRenderingParameter( FLOAT_RENDERPARM_DX12_NR_COMPARE_SPLIT, r_dlss_nr_compare_split.GetFloat() );
	pRenderContext->SetFloatRenderingParameter( FLOAT_RENDERPARM_DX12_NR_COMPARE_ZOOM, r_dlss_nr_compare_zoom.GetFloat() );
	pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_DX12_NR_CONFIG, nConfig );
}

class CUpscalerDX12GameSystem : public CAutoGameSystem
{
public:
	CUpscalerDX12GameSystem() : CAutoGameSystem( "CUpscalerDX12GameSystem" ) {}
	virtual void LevelInitPostEntity() { s_bResetPending = true; s_bHaveLastOrigin = false; }
	virtual void LevelShutdownPostEntity() { s_bResetPending = true; s_bHaveLastOrigin = false; }
};
static CUpscalerDX12GameSystem s_UpscalerDX12GameSystem;

void UpscalerDX12_Init( IMaterialSystemHardwareConfig *pHardwareConfig )
{
	const char *shaderDLL = pHardwareConfig ? pHardwareConfig->GetShaderDLLName() : nullptr;
	s_bDX12 = shaderDLL && !V_stricmp( shaderDLL, "stdshader_dx12" );
	s_pShaderAPI = nullptr;
	s_bResetPending = true;
	s_bHaveLastOrigin = false;
	if ( !s_bDX12 )
		return;
	CreateInterfaceFn f = Sys_GetFactory( "shaderapidx12" );
	s_pShaderAPI = f ? static_cast< IShaderAPI * >( f( SHADERAPI_INTERFACE_VERSION, nullptr ) ) : nullptr;
	if ( r_upscaler.GetInt() != 0 )
		UpscalerDX12_ReconcileMSAA();
}

void UpscalerDX12_Shutdown()
{
	s_bDX12 = false;
	s_pShaderAPI = nullptr;
	s_bResetPending = true;
	s_bFrameLatched = false;
	s_bLastFrameEligible = false;
	s_bHaveLastOrigin = false;
}

bool UpscalerDX12_Enabled()
{
	return s_bDX12 && r_upscaler.GetInt() != 0 && MotionVectorsDX12_RenderTarget() != nullptr;
}

bool UpscalerDX12_RequiresMotionVectors()
{
	// NR is dispatched only as part of this latched temporal-AA dispatch.
	return s_bFrameLatched;
}

static float UpscalerDX12_ViewLodBias( const CViewSetup &view )
{
	if ( !r_upscaler_lod_bias_auto.GetBool() )
		return r_upscaler_lod_bias.GetFloat();
	if ( r_upscaler.GetInt() == 5 )
		return -0.5f;
	const float flScale = view.x == 0 && view.width > 0 && view.m_nUnscaledWidth > 0
		? MIN( 1.f, float( view.width ) / float( view.m_nUnscaledWidth ) ) : 1.f;
	return Clamp( log2f( flScale ) - 1.f, -2.f, 0.f );
}

void UpscalerDX12_BeginFrame( IMatRenderContext *pRenderContext, bool bMainTemporalViewEligible, const CViewSetup &view )
{
	s_bFrameLatched = false;
	if ( !s_bDX12 || !pRenderContext )
		return;
	if ( !UpscalerDX12_Enabled() )
	{
		// Mode 0 disarms the backend for every draw of this view; the target-less case never dispatches.
		pRenderContext->SetFloatRenderingParameter( FLOAT_RENDERPARM_DX12_UPSCALE_LOD_BIAS, 0.f );
		pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_DX12_UPSCALE_MODE, 0 );
		s_bLastFrameEligible = false;
		s_bResetPending = true;
		return;
	}
	if ( bMainTemporalViewEligible != s_bLastFrameEligible || view.width != s_nLastWidth || view.height != s_nLastHeight )
		s_bResetPending = true;
	s_bLastFrameEligible = bMainTemporalViewEligible;
	s_nLastWidth = view.width; s_nLastHeight = view.height;
	s_bFrameLatched = bMainTemporalViewEligible;
	// Submit before the mode arms the provider and before any material draw; the backend changes
	// sampler cache keys only when this value actually changes.
	pRenderContext->SetFloatRenderingParameter( FLOAT_RENDERPARM_DX12_UPSCALE_LOD_BIAS,
		bMainTemporalViewEligible ? UpscalerDX12_ViewLodBias( view ) : 0.f );
	pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_DX12_UPSCALE_MODE, bMainTemporalViewEligible ? r_upscaler.GetInt() : 0 );
}

bool UpscalerDX12_Dispatch( IMatRenderContext *pRenderContext, const CViewSetup &view, bool bReset )
{
	if ( !s_bFrameLatched || !pRenderContext )
		return false;
	s_bFrameLatched = false;

	// Camera cut / teleport: a large origin step between dispatches breaks the history.
	if ( s_bHaveLastOrigin && view.origin.DistToSqr( s_vecLastOrigin ) > 512.f * 512.f )
		s_bResetPending = true;
	s_vecLastOrigin = view.origin;
	s_bHaveLastOrigin = true;

	// A failed or rejected dispatch on the previous frame left the history unusable.
	if ( s_nLastStatus < 0 )
		s_bResetPending = true;

	// FSR camera values: vertical FOV from the horizontal view.fov and the main view aspect.
	const float flAspect = view.m_flAspectRatio > 0.f ? view.m_flAspectRatio : ( view.height > 0 ? float( view.width ) / float( view.height ) : 1.f );
	const float flFovY = 2.f * atanf( tanf( DEG2RAD( view.fov ) * 0.5f ) / flAspect );
	pRenderContext->SetFloatRenderingParameter( FLOAT_RENDERPARM_DX12_UPSCALE_FOV_Y, flFovY );
	pRenderContext->SetFloatRenderingParameter( FLOAT_RENDERPARM_DX12_UPSCALE_NEAR, view.zNear );
	pRenderContext->SetFloatRenderingParameter( FLOAT_RENDERPARM_DX12_UPSCALE_FAR, view.zFar );
	// The backend reads the render size at the top-left of the scene targets; anything else is native AA.
	const bool bScaled = view.x == 0 && view.y == 0 && ( view.width < view.m_nUnscaledWidth || view.height < view.m_nUnscaledHeight );
	pRenderContext->SetFloatRenderingParameter( FLOAT_RENDERPARM_DX12_UPSCALE_RENDER_WIDTH, bScaled ? float( view.width ) : 0.f );
	pRenderContext->SetFloatRenderingParameter( FLOAT_RENDERPARM_DX12_UPSCALE_RENDER_HEIGHT, bScaled ? float( view.height ) : 0.f );

	const bool bResetDispatch = bReset || s_bResetPending;
	SubmitNrConfig( pRenderContext );
	pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_DX12_UPSCALE_DISPATCH,
		DX12_UPSCALE_DISPATCH_RUN | ( bResetDispatch ? DX12_UPSCALE_DISPATCH_RESET : 0 ) );
	s_bResetPending = false;
	// Under mat_queue_mode 2 the status lags the queued dispatch by the queue depth; it is a diagnostic, not a gate.
	const int nStatus = UpscalerDX12_Status();
	if ( nStatus != s_nLastStatus )
		DevMsg( "r_upscaler: status %d -> %d\n", s_nLastStatus, nStatus );
	s_nLastStatus = nStatus;
	// -1 no provider, -2 feature creation failed, -6 provider replay error: this adapter cannot upscale at this scale.
	if ( bScaled && !s_bScaleFailed && ( nStatus == -1 || nStatus == -2 || nStatus == -6 ) )
	{
		s_bScaleFailed = true;
		Warning( "r_upscaler_scale: the upscaler failed at %dx%d -> %dx%d (status %d); rendering at native resolution\n",
			view.width, view.height, view.m_nUnscaledWidth, view.m_nUnscaledHeight, nStatus );
	}
	const int nNrStatus = s_pShaderAPI ? s_pShaderAPI->GetIntRenderingParameter( INT_RENDERPARM_DX12_NR_STATUS ) : 0;
	if ( nNrStatus != s_nLastNrStatus )
		DevMsg( "r_dlss_nr: status %d -> %d\n", s_nLastNrStatus, nNrStatus );
	s_nLastNrStatus = nNrStatus;
	return bScaled;
}

float UpscalerDX12_RenderScale()
{
	// SMAA T2x is native-only; frame generation also needs scene-sized motion vectors and depth.
	if ( !UpscalerDX12_Enabled() || r_upscaler.GetInt() == 5 || s_bScaleFailed || FrameGenDX12_Enabled() )
		return 1.f;
	const float flScale = r_upscaler_scale.GetFloat();
	return flScale < 0.995f ? Clamp( flScale, 0.5f, 1.f ) : 1.f;
}

int UpscalerDX12_Status()
{
	return s_pShaderAPI ? s_pShaderAPI->GetIntRenderingParameter( INT_RENDERPARM_DX12_UPSCALE_STATUS ) : 0;
}

static void UpscalerDX12_PrintStatus( const CCommand & )
{
	const int status = UpscalerDX12_Status();
	const int kind = status > 0 ? ( status >> 8 ) & 0xff : 0;
	const char *const names[] = { "none", "DLSS/DLAA", "FSR", "XeSS", "SMAA T2x" };
	Msg( "r_upscaler: mode %d, render scale %.3f, status %d (%s%s)\n", r_upscaler.GetInt(), UpscalerDX12_RenderScale(), status,
	    kind >= 0 && kind < ARRAYSIZE( names ) ? names[kind] : "unknown", status > 0 ? " active" : "" );
}
static ConCommand r_upscaler_status( "r_upscaler_status", UpscalerDX12_PrintStatus,
    "Print the requested temporal AA mode, native/render scale and backend runtime status", FCVAR_CHEAT );
