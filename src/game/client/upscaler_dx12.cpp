//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DX12 native-AA upscaler client trigger. The client decides which RenderView is the temporal main view and
//          when its history breaks; the backend owns provider selection, jitter and the fail-closed dispatch.
//
//===========================================================================//
#include "cbase.h"
#include "upscaler_dx12.h"
#include "motionvectors_dx12.h"
#include "igamesystem.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imaterialsystemhardwareconfig.h"
#include "shaderapi/ishaderapi.h"
#include "tier1/interface.h"
#include "renderparm.h"
#include "view_shared.h"

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

static void ReconcileMSAA()
{
	// Looked up per call: a ConVarRef cached before the engine registers mat_antialias stays invalid.
	ConVarRef mat_antialias( "mat_antialias", true );
	if ( mat_antialias.IsValid() && mat_antialias.GetInt() != 0 )
	{
		mat_antialias.SetValue( 0 );
		Msg( "r_upscaler: temporal AA replaces MSAA; mat_antialias set to 0\n" );
	}
}

static void OnUpscalerChanged( IConVar *pVar, const char *pOldValue, float flOldValue )
{
	s_bResetPending = true;
	ConVarRef var( pVar );
	if ( s_bDX12 && var.GetInt() != 0 )
		ReconcileMSAA();
}

ConVar r_upscaler( "r_upscaler", "1", FCVAR_ARCHIVE,
	"DX12 native-resolution temporal AA: 0 off, 1 auto, 2 DLAA, 3 FSR native AA (FSR 4 on AMD is untested), 4 XeSS AA",
	true, 0, true, 4, OnUpscalerChanged );

// DLSS-NR (NVIDIA neural rendering, nvngx_dlssnr.dll) runs after the temporal AA, before the HUD. Layers chain: the
// temporal-AA frame -> NR -> NR output -> NR -> ... -> final image. Model tuning rebuilds every layer's feature.
static ConVar r_dlss_nr_layers( "r_dlss_nr_layers", "0", FCVAR_ARCHIVE,
	"DLSS-NR layers after the temporal AA (0 off); each layer is fed the previous layer's output and costs a full model pass",
	true, 0, true, DX12_NR_MAX_LAYERS );
static ConVar r_dlss_nr_preset( "r_dlss_nr_preset", "0", FCVAR_ARCHIVE, "DLSS-NR model preset: 0 default, 1-3 model presets", true, 0, true, 3 );
static ConVar r_dlss_nr_style( "r_dlss_nr_style", "0", FCVAR_ARCHIVE, "DLSS-NR style: 0 standard, 1 natural, 2 cinematic", true, 0, true, 2 );
static ConVar r_dlss_nr_intensity( "r_dlss_nr_intensity", "1", FCVAR_ARCHIVE, "DLSS-NR model intensity", true, 0, true, 2 );
static ConVar r_dlss_nr_local_structure( "r_dlss_nr_local_structure", "1", FCVAR_ARCHIVE, "DLSS-NR local structure strength", true, 0, true, 2 );
static ConVar r_dlss_nr_local_tone( "r_dlss_nr_local_tone", "1", FCVAR_ARCHIVE, "DLSS-NR local tone strength", true, 0, true, 2 );
static ConVar r_dlss_nr_skin_structure( "r_dlss_nr_skin_structure", "-1", FCVAR_ARCHIVE, "DLSS-NR skin structure strength; -1 follows local structure", true, -1, true, 2 );
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
		ReconcileMSAA();
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

void UpscalerDX12_BeginFrame( IMatRenderContext *pRenderContext, bool bMainTemporalViewEligible, const CViewSetup &view )
{
	if ( !s_bDX12 || !pRenderContext )
		return;
	if ( !UpscalerDX12_Enabled() )
	{
		// Mode 0 disarms the backend for every draw of this view; the target-less case never dispatches.
		pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_DX12_UPSCALE_MODE, 0 );
		s_bFrameLatched = false;
		s_bLastFrameEligible = false;
		s_bResetPending = true;
		return;
	}
	if ( bMainTemporalViewEligible != s_bLastFrameEligible || view.width != s_nLastWidth || view.height != s_nLastHeight )
		s_bResetPending = true;
	s_bLastFrameEligible = bMainTemporalViewEligible;
	s_nLastWidth = view.width; s_nLastHeight = view.height;
	s_bFrameLatched = bMainTemporalViewEligible;
	pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_DX12_UPSCALE_MODE, bMainTemporalViewEligible ? r_upscaler.GetInt() : 0 );
}

void UpscalerDX12_Dispatch( IMatRenderContext *pRenderContext, const CViewSetup &view, bool bReset )
{
	if ( !s_bFrameLatched || !pRenderContext )
		return;
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
	const int nNrStatus = s_pShaderAPI ? s_pShaderAPI->GetIntRenderingParameter( INT_RENDERPARM_DX12_NR_STATUS ) : 0;
	if ( nNrStatus != s_nLastNrStatus )
		DevMsg( "r_dlss_nr: status %d -> %d\n", s_nLastNrStatus, nNrStatus );
	s_nLastNrStatus = nNrStatus;
}

int UpscalerDX12_Status()
{
	return s_pShaderAPI ? s_pShaderAPI->GetIntRenderingParameter( INT_RENDERPARM_DX12_UPSCALE_STATUS ) : 0;
}
