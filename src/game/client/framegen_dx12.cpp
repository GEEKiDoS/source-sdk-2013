//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DX12 frame generation and NVIDIA Reflex client side. Settings, status and latency markers go through
//          IShaderAPIDX12 (the renderer's extension interface); only the per-frame work that must stay ordered with
//          the draw stream (eligible view, frame id, dispatch) goes through the render context. A renderer without
//          the interface leaves both features off.
//
//===========================================================================//
#include "cbase.h"
#include "framegen_dx12.h"
#include "upscaler_dx12.h"
#include "motionvectors_dx12.h"
#include "igamesystem.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imaterialsystemhardwareconfig.h"
#include "shaderapi/ishaderapidx12.h"
#include "tier1/interface.h"
#include "renderparm.h"
#include "view_shared.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static IShaderAPIDX12 *s_pShaderAPIDX12 = nullptr;
// History gap: the next dispatch must carry reset (level load, cvar change, view or frame discontinuity).
static bool s_bResetPending = true;
static bool s_bFrameLatched = false;
static Vector s_vecLastOrigin( 0, 0, 0 );
static bool s_bHaveLastOrigin = false;
static int s_nLastStatus = 0, s_nLastReflexStatus = 0;
static float s_flLastFpsLimit = -1.f;
// Frame id shared by the latency markers and the dispatch; opened after the previous FRAME_RENDER_END
// (gpGlobals->framecount advances between the simulation and render stages, so it cannot tag one frame consistently).
static unsigned int s_nFrameId = 0;
// Smoothed frames shown per rendered frame (cl_showfps reads it next to the host frame rate).
static float s_flShownPerFrame = 1.f;

static void OnFrameGenChanged( IConVar *pVar, const char *pOldValue, float flOldValue );
static void OnReflexChanged( IConVar *pVar, const char *pOldValue, float flOldValue );

ConVar r_framegen( "r_framegen", "0", FCVAR_ARCHIVE,
	"DX12 frame generation: 0 off, 1 auto (DLSS-G on RTX, else FSR frame generation, else XeSS-FG), 2 DLSS-G, 3 FSR FG, 4 XeSS-FG",
	true, 0, true, 4, OnFrameGenChanged );
ConVar r_framegen_multiplier( "r_framegen_multiplier", "2", FCVAR_ARCHIVE,
	"Frames shown per rendered frame (2-4); FSR is always 2, DLSS-G/XeSS-FG clamp to the GPU's maximum",
	true, 2, true, 4, OnFrameGenChanged );
ConVar r_framegen_hudless( "r_framegen_hudless", "1", FCVAR_ARCHIVE,
	"Give the frame generator a HUD-less copy of the scene so the HUD is not warped",
	true, 0, true, 1, OnFrameGenChanged );
ConVar r_reflex( "r_reflex", "0", FCVAR_ARCHIVE,
	"NVIDIA Reflex on DX12: 0 off, 1 low latency, 2 low latency + boost (DLSS-G always runs low latency; XeSS-FG uses XeLL instead)",
	true, 0, true, 2, OnReflexChanged );

static void PushFrameGenSettings()
{
	if ( s_pShaderAPIDX12 )
		s_pShaderAPIDX12->SetFrameGeneration( r_framegen.GetInt(), r_framegen_multiplier.GetInt(), r_framegen_hudless.GetBool() );
}

static void OnFrameGenChanged( IConVar *pVar, const char *pOldValue, float flOldValue )
{
	s_bResetPending = true;
	PushFrameGenSettings();
	if ( s_pShaderAPIDX12 && r_framegen.GetInt() != 0 )
		UpscalerDX12_ReconcileMSAA();
}

static void OnReflexChanged( IConVar *pVar, const char *pOldValue, float flOldValue )
{
	if ( s_pShaderAPIDX12 )
		s_pShaderAPIDX12->SetReflexMode( r_reflex.GetInt() );
}

class CFrameGenDX12GameSystem : public CAutoGameSystem
{
public:
	CFrameGenDX12GameSystem() : CAutoGameSystem( "CFrameGenDX12GameSystem" ) {}
	virtual void LevelInitPostEntity() { s_bResetPending = true; s_bHaveLastOrigin = false; }
	virtual void LevelShutdownPostEntity() { s_bResetPending = true; s_bHaveLastOrigin = false; }
};
static CFrameGenDX12GameSystem s_FrameGenDX12GameSystem;

void FrameGenDX12_Init( IMaterialSystemHardwareConfig *pHardwareConfig )
{
	const char *shaderDLL = pHardwareConfig ? pHardwareConfig->GetShaderDLLName() : nullptr;
	s_pShaderAPIDX12 = nullptr;
	s_bResetPending = true;
	s_bHaveLastOrigin = false;
	s_flLastFpsLimit = -1.f;
	s_nLastStatus = s_nLastReflexStatus = 0;
	if ( !shaderDLL || V_stricmp( shaderDLL, "stdshader_dx12" ) )
		return;
	CreateInterfaceFn f = Sys_GetFactory( "shaderapidx12" );
	s_pShaderAPIDX12 = f ? static_cast< IShaderAPIDX12 * >( f( SHADERAPIDX12_INTERFACE_VERSION, nullptr ) ) : nullptr;
	if ( !s_pShaderAPIDX12 )
	{
		// Older renderer build: everything here stays off, the rest of the client is unaffected.
		Msg( "r_framegen: shaderapidx12 has no %s interface; frame generation and Reflex are unavailable\n", SHADERAPIDX12_INTERFACE_VERSION );
		return;
	}
	PushFrameGenSettings();
	s_pShaderAPIDX12->SetReflexMode( r_reflex.GetInt() );
	if ( r_framegen.GetInt() != 0 )
		UpscalerDX12_ReconcileMSAA();
}

void FrameGenDX12_Shutdown()
{
	s_pShaderAPIDX12 = nullptr;
	s_bResetPending = true;
	s_bFrameLatched = false;
	s_bHaveLastOrigin = false;
}

bool FrameGenDX12_Enabled()
{
	return s_pShaderAPIDX12 && r_framegen.GetInt() != 0 && MotionVectorsDX12_RenderTarget() != nullptr;
}

bool FrameGenDX12_RequiresMotionVectors()
{
	return s_bFrameLatched;
}

void FrameGenDX12_BeginFrame( IMatRenderContext *pRenderContext, bool bMainViewEligible )
{
	s_bFrameLatched = false;
	if ( !s_pShaderAPIDX12 || !pRenderContext )
		return;
	// The provider stays selected across frames; a frame without an eligible view presents in pass-through (pause,
	// menus, monitors, loading). Without a motion-vector target no frame can be eligible.
	if ( !FrameGenDX12_Enabled() )
	{
		s_bResetPending = true;
		bMainViewEligible = false;
	}
	if ( !bMainViewEligible )
	{
		// An ineligible view must not preserve provider history across its pass-through frame.
		s_bResetPending = true;
	}
	pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_DX12_FRAMEGEN_VIEW, bMainViewEligible ? 1 : 0 );
	// fps_max is an engine cvar without a client callback: forward it when it changes.
	static ConVarRef fps_max( "fps_max" );
	const float flFpsLimit = fps_max.IsValid() ? fps_max.GetFloat() : 0.f;
	if ( flFpsLimit != s_flLastFpsLimit )
	{
		s_flLastFpsLimit = flFpsLimit;
		s_pShaderAPIDX12->SetFrameRateLimit( flFpsLimit );
	}
	s_bFrameLatched = bMainViewEligible;
}

void FrameGenDX12_Dispatch( IMatRenderContext *pRenderContext, const CViewSetup &view, bool bReset )
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

	// Camera values: vertical FOV from the horizontal view.fov and the main view aspect (same as the upscaler).
	const float flAspect = view.m_flAspectRatio > 0.f ? view.m_flAspectRatio : ( view.height > 0 ? float( view.width ) / float( view.height ) : 1.f );
	const float flFovY = 2.f * atanf( tanf( DEG2RAD( view.fov ) * 0.5f ) / flAspect );
	pRenderContext->SetFloatRenderingParameter( FLOAT_RENDERPARM_DX12_UPSCALE_FOV_Y, flFovY );
	pRenderContext->SetFloatRenderingParameter( FLOAT_RENDERPARM_DX12_UPSCALE_NEAR, view.zNear );
	pRenderContext->SetFloatRenderingParameter( FLOAT_RENDERPARM_DX12_UPSCALE_FAR, view.zFar );
	pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_DX12_FRAMEGEN_FRAME, static_cast< int >( s_nFrameId ) );

	const bool bResetDispatch = bReset || s_bResetPending;
	pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_DX12_FRAMEGEN_DISPATCH,
		DX12_FRAMEGEN_DISPATCH_RUN | ( bResetDispatch ? DX12_FRAMEGEN_DISPATCH_RESET : 0 ) );
	s_bResetPending = false;
	// Under mat_queue_mode 2 the status lags the queued dispatch by the queue depth; it is a diagnostic, not a gate.
	const int nStatus = FrameGenDX12_Status();
	if ( nStatus != s_nLastStatus )
	{
		if ( nStatus > 0 )
			Msg( "r_framegen: %s active (x%d)\n", FrameGenDX12_ActiveName(), 1 + SHADERAPIDX12_FRAMEGEN_STATUS_GENERATED( nStatus ) );
		else
			DevMsg( "r_framegen: status %d -> %d\n", s_nLastStatus, nStatus );
	}
	s_nLastStatus = nStatus;
}

void FrameGenDX12_FrameStage( ClientFrameStage_t stage )
{
	if ( !s_pShaderAPIDX12 )
		return;
	if ( stage == FRAME_RENDER_END )
	{
		// Frames the back end showed for the last rendered frame: 1 without frame generation, 2..4 with it.
		s_flShownPerFrame += ( float( MAX( s_pShaderAPIDX12->FramesShown(), 1 ) ) - s_flShownPerFrame ) * 0.1f;
		const int nReflex = s_pShaderAPIDX12->ReflexStatus();
		if ( nReflex != s_nLastReflexStatus )
		{
			if ( nReflex > 0 )
				Msg( "r_reflex: NVIDIA Reflex %s\n", r_reflex.GetInt() == 2 ? "low latency + boost" : "low latency" );
			else if ( nReflex < 0 )
				Msg( "r_reflex: unavailable (%s)\n", s_pShaderAPIDX12->LastError() );
			s_nLastReflexStatus = nReflex;
		}
	}
	if ( r_framegen.GetInt() == 0 && r_reflex.GetInt() == 0 )
		return;
	// FRAME_RENDER_START/END are the only stages the engine sends exactly once per rendered frame (FRAME_START and
	// FRAME_NET_UPDATE_* follow the tick loop: zero or several per frame). The latency frame therefore opens right
	// after the previous frame's render submission, which is the last point before the engine samples input again.
	switch ( stage )
	{
	case FRAME_RENDER_START:
		s_pShaderAPIDX12->LatencyMarker( SHADERAPIDX12_MARKER_SIMULATION_END, s_nFrameId );
		s_pShaderAPIDX12->LatencyMarker( SHADERAPIDX12_MARKER_RENDER_SUBMIT_START, s_nFrameId );
		break;
	case FRAME_RENDER_END:
		s_pShaderAPIDX12->LatencyMarker( SHADERAPIDX12_MARKER_RENDER_SUBMIT_END, s_nFrameId );
		s_nFrameId = ( s_nFrameId + 1 ) & 0x0FFFFFFFu;
		s_pShaderAPIDX12->LatencyMarker( SHADERAPIDX12_MARKER_SIMULATION_START, s_nFrameId ); // Reflex/XeLL sleep happens inside this one
		break;
	default:
		break;
	}
}

int FrameGenDX12_Status()
{
	return s_pShaderAPIDX12 ? s_pShaderAPIDX12->FrameGenerationStatus() : 0;
}

float FrameGenDX12_ShownPerFrame()
{
	return s_flShownPerFrame;
}

const char *FrameGenDX12_ActiveName()
{
	const int nStatus = FrameGenDX12_Status();
	if ( nStatus <= 0 )
		return NULL;
	switch ( SHADERAPIDX12_FRAMEGEN_STATUS_KIND( nStatus ) )
	{
	case 1: return "DLSS-G";
	case 2: return "FSR FG";
	default: return "XeSS-FG";
	}
}
