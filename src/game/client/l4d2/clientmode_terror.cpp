//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: L4D2 normal client mode, viewport and native presentation lifetime.
//
//=============================================================================//

#include "cbase.h"
#include "l4d2/clientmode_terror.h"
#include "l4d2/ui/terror_hud_resources.h"
#include "l4d2/ui/basemodui/terror_basemodui.h"
#include "cdll_client_int.h"
#include "cdll_bounded_cvars.h"
#include "hud.h"
#include "hud_element_helper.h"
#include "l4d2/hud/hud_terror_statscrawl.h"
#include "l4d2/hud/terror_pz_endgame.h"
#include "hud_vote.h"
#include "usermessages.h"
#include "l4d2/hud/hud_chat.h"
#include "hud_crosshair.h"
#include "ivmodemanager.h"
#include "panelmetaclassmgr.h"
#include "l4d2/ui/cs_textwindow.h"
#include "l4d2/hud/terror_takeover_bar.h"
#include "l4d2/ui/terror_radial_menu.h"
#include "l4d2/hud/terror_spectator_gui.h"
#include "l4d2/hud/terror_client_scoreboard.h"
#include "l4d2/hud/terror_ready_countdown.h"
#include "KeyValues.h"
#include "l4d2/player/c_terrorplayer.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/player/c_cs_player.h"
#include "l4d2/weapons/weapon_csbase.h"
#include "l4d2/player/terror_playeranimstate.h"
#include "l4d2/player/c_terror_player_resource.h"
#include "l4d2/effects/c_terror_postprocess_controller.h"
#include "l4d2/effects/terror_flashlight.h"
#include "l4d2/effects/terror_glow_render.h"
#include "l4d2/ui/terror_client_panels.h"
#include "l4d2/stats/terror_client_stats.h"
#include "l4d2/ui/terror_spawnmenus.h"
#include "l4d2/hud/terror_hud_viewport_panels.h"
#include "l4d2/ui/terror_perf_wizard.h"
#include "l4d2/hud/terror_mode_scoreboards.h"
#include "view_shared.h"
#include "viewpostprocess.h"
#include "engine/IEngineSound.h"
#include "engine/IEngineTrace.h"
#include "engine/ivdebugoverlay.h"
#include "engine/ivmodelinfo.h"
#include "ienginevgui.h"
#include "in_buttons.h"
#include "kbutton.h"
#include "iinput.h"
#include "ivieweffects.h"
#include "c_rumble.h"
#include "physpropclientside.h"
#include "c_soundscape.h"
#include "vguicenterprint.h"
#include "vgui/ILocalize.h"
#include "vgui/ISurface.h"
#include "vgui/IInput.h"
#include "vgui_controls/AnimationController.h"
#include "particle_property.h"
#include "particlemgr.h"
#include "particles_new.h"
#include "tier1/utlbuffer.h"
#include "tier1/callqueue.h"
#include "tier1/interface.h"
#include "tier0/icommandline.h"
#include "GameUI/IGameUI.h"
#include "toolframework_client.h"
#include "cliententitylist.h"
#include "filesystem.h"
#include "steam/steam_api.h"
#include <emmintrin.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar default_fov( "default_fov", "90", 0x00004008 );

// Sole definitions and survivor debug callback belong to the console owner.
extern ConVar cl_viewmodelfovsurvivor;
extern ConVar cl_viewmodelfovsmoker;
extern ConVar cl_viewmodelfovboomer;
extern ConVar cl_viewmodelfovhunter;
extern ConVar cl_viewmodelfovtank;

void HookMusicUserMessage();
void ResetTransitioningPlayers();
C_TerrorPlayer *CinematicViewPlayer();
C_TerrorPlayer *PostProcessViewPlayer();

IClientMode *g_pClientMode = NULL;


class CTerrorViewport : public CBaseViewport
{
	DECLARE_CLASS_SIMPLE( CTerrorViewport, CBaseViewport );

public:
	virtual void ShowBackGround( bool ) OVERRIDE
	{
		// Native TerrorViewport (10228F00) never shows the SDK modal backdrop.
		// Its full-screen stencil would otherwise occlude the radial popup.
		BaseClass::ShowBackGround( false );
	}

	virtual void CreateDefaultPanels() OVERRIDE
	{
		static const char *const panels[] =
		{
			"team", "takeover_survivor_bar", "radialmenu",
			"spawnmode_bar", "spawnmode", "spawn_hunter", "spawn_charger",
			"spawn_smoker", "spawn_boomer", "spawn_spitter", "spawn_jockey",
			"zombie_intro", "message_box", "info_window",
			"ready_countdown", "fullscreen_vs_scoreboard",
			"fullscreen_vs_results", "fullscreen_scavenge_scoreboard",
			"vs_shutting_down", PANEL_INFO, "transition_stats",
			"fullscreen_survival_scoreboard", "survival_shutting_down"
		};
		for ( int i = 0; i < ARRAYSIZE( panels ); ++i )
		{
			if ( !FindPanelByName( panels[i] ) )
				AddNewPanel( CreatePanelByName( panels[i] ), panels[i] );
		}
		BaseClass::CreateDefaultPanels();
	}

	virtual IViewPortPanel *CreatePanelByName( const char *pPanelName ) OVERRIDE
	{
		if ( !Q_strcmp( pPanelName, PANEL_SCOREBOARD ) ) return new CTerrorClientScoreBoardDialog( this );
		if ( !Q_strcmp( pPanelName, PANEL_SPECMENU ) ) return NULL;
		if ( !Q_strcmp( pPanelName, PANEL_SPECGUI ) ) return new CTerrorSpectatorGUI( this );
		if ( !Q_strcmp( pPanelName, "ready_countdown" ) ) return new CReadyCountdown( this );
		IViewPortPanel *panel = CreateTerrorClientPanel( this, pPanelName );
		if ( panel ) return panel;
		panel = CreateTerrorHudViewportPanel( this, pPanelName );
		if ( panel ) return panel;
		panel = CreateTerrorModeScoreboardPanel( this, pPanelName );
		if ( panel ) return panel;
		if ( !Q_strcmp( pPanelName, PANEL_INFO ) )
		{
			return new CTerrorTextWindow( this );
		}
		if ( !Q_strcmp( pPanelName, "takeover_survivor_bar" ) ) return new CTakeoverBar( this );
		if ( !Q_strcmp( pPanelName, "radialmenu" ) ) return new CRadialMenu( this );

		return BaseClass::CreatePanelByName( pPanelName );
	}

protected:
	virtual void ApplySchemeSettings( vgui::IScheme *pScheme ) OVERRIDE
	{
		BaseClass::ApplySchemeSettings( pScheme );
		gHUD.InitColors( pScheme );
		SetPaintBackgroundEnabled( false );
	}
};

void ClientModeTerrorNormal::InitViewport()
{
	if ( !PrepareTerrorHudResources() )
		Warning( "Unable to prepare the mounted original L4D2 HUD resources\n" );
	m_pViewport = new CTerrorViewport;
	m_pViewport->Start( gameuifuncs, gameeventmanager );
}

IClientMode *GetClientModeNormal()
{
	static ClientModeTerrorNormal s_ClientModeNormal;
	return &s_ClientModeNormal;
}

IClientMode *GetClientMode()
{
	return g_pClientMode;
}

class CTerrorModeManager : public IVModeManager
{
public:
	virtual void Init() OVERRIDE
	{
		g_pClientMode = GetClientModeNormal();
		PanelMetaClassMgr()->LoadMetaClassDefinitionFile( "scripts/vgui_screens.txt" );
	}

	virtual void SwitchMode( bool commander, bool force ) OVERRIDE
	{
		IClientMode *pNormalMode = GetClientModeNormal();
		if ( !force && g_pClientMode == pNormalMode )
			return;

		if ( g_pClientMode )
		{
			g_pClientMode->Disable();
		}

		g_pClientMode = pNormalMode;
		g_pClientMode->Enable();
	}

	virtual void LevelInit( const char *pMapName ) OVERRIDE
	{
		g_pClientMode->LevelInit( pMapName );
	}

	virtual void LevelShutdown() OVERRIDE
	{
		g_pClientMode->LevelShutdown();
	}
};

static CTerrorModeManager g_ModeManager;
IVModeManager *modemanager = &g_ModeManager;

static CCallQueue s_TerrorClientMessageQueue;
void ProcessTerrorClientMessageQueue()
{
	s_TerrorClientMessageQueue.CallQueued(); //100B0B70, exact OnRenderStart phase
}

#pragma float_control( precise, on, push )
static void WitchBloodRandomSphere( Vector &out )
{
	//10363E40 delegates its integer generator1018BE40 to real RandomInt,
	//NOT mathlib SDK's rand() implementation. All native f32 spills explicit.
	const double unit = (double)3.0518509447574615e-05f;
	const float u = (float)( (double)RandomInt( 0, 32767 ) * unit );
	const float v = (float)( (double)RandomInt( 0, 32767 ) * unit );
	const float phi = (float)acos( 1.0 - (double)u * 2.0 );
	const float theta = (float)( (double)v * 6.283185307179586 );
	const float radius = (float)pow( (double)RandomInt( 0, 32767 ) * unit, 0.3333333432674408 );
	const float cp = (float)cos( (double)phi ), sp = (float)sin( (double)phi );
	const float ct = (float)cos( (double)theta ), st = (float)sin( (double)theta );
	const float radial = sp * radius;
	out.x = radial * ct; out.y = radial * st; out.z = cp * radius;
}
static void WitchBloodNormalize( Vector &value )
{
	//1035F4C0: sqrt result spills before adding FLT_EPSILON and division.
	const float length = (float)sqrt( (double)( value.x * value.x + value.y * value.y + value.z * value.z ) );
	const float inverse = 1.0f / ( length + 0.00000011920929f );
	value *= inverse;
}
static void WitchBloodSplatter( const Vector &origin )
{
	static int lastFrame = -1; //native106E827C: only first splatter per frame
	if ( lastFrame == gpGlobals->framecount ) return;
	lastFrame = gpGlobals->framecount;
	MDLCACHE_CRITICAL_SECTION();
	// The approved immutable-engine capability boundary uses ordinary traces,
	// not the original engine's unavailable Create/FreeTraceList ABI.
	CTraceFilterNoNPCsOrPlayer filter( NULL, COLLISION_GROUP_NONE );
	int remainingHits = 25;
	for ( int attempt = 0; attempt < 35; ++attempt )
	{
		Vector direction;
		WitchBloodRandomSphere( direction );
		WitchBloodNormalize( direction );
		direction.z = direction.z * 0.75f - 0.25f;
		WitchBloodNormalize( direction );
		Ray_t ray;
		ray.Init( origin, origin + direction * 128.0f );
		trace_t trace;
		enginetrace->TraceRay( ray, 33570827, &filter, &trace );
		if ( trace.fraction < 1.0f || trace.allsolid || trace.startsolid )
		{
			//Native1AD290 guards null entity before virtualDecalTrace. The
			//SDK shared helper does not; the ledger's minimal null skip applies.
			if ( trace.m_pEnt ) UTIL_BloodDecalTrace( &trace, BLOOD_COLOR_RED );
			if ( --remainingHits <= 0 ) break;
		}
	}
}
#pragma float_control( pop )
static void ReceiveWitchBloodSplatter( bf_read &message )
{
	Vector origin;
	message.ReadBitVec3Coord( origin );
	s_TerrorClientMessageQueue.QueueCall( WitchBloodSplatter, origin );
}
static void ReceiveTerrorKillCam( bf_read &message )
{
	C_BasePlayer *local = C_BasePlayer::GetLocalPlayer();
	if ( !local || !local->IsPlayer() ) return;
	const int mode = message.ReadByte();
	if ( mode != g_nKillCamMode )
	{
		if ( g_nKillCamMode == OBS_MODE_NONE ) g_bForceCLPredictOff = true;
		else if ( mode == OBS_MODE_NONE ) g_bForceCLPredictOff = false;
		g_nKillCamMode = mode;
	}
	g_nKillCamTarget1 = message.ReadByte();
	g_nKillCamTarget2 = message.ReadByte();
}

ConVar cl_cinematiclight_r( "cl_cinematiclight_r", "3.0", 0x00004008 );
ConVar cl_cinematiclight_g( "cl_cinematiclight_g", "0.3", 0x00004008 );
ConVar cl_cinematiclight_b( "cl_cinematiclight_b", "0.05", 0x00004008 );
ConVar cl_cinematiclight_tonemap( "cl_cinematiclight_tonemap", "0.4", 0x00004008 );
ConVar cl_cinematiclight_scale( "cl_cinematiclight_scale", "7.0", 0x00004008 );
ConVar cl_transition_blur_fade_time( "cl_transition_blur_fade_time", "2.0", 0x0000000A );
ConVar cl_force_3rd_strike( "cl_force_3rd_strike", "0", 0x00004008 );
ConVar spec_keep_team_cc( "spec_keep_team_cc", "1", 0x00004008, "Use team color correction even when spectating in vs mode" );
ConVar r_bloomtintexponent_ghost( "r_bloomtintexponent_ghost", "0.1", 0x0000000A );
ConVar l4d_delete_temp_files( "l4d_delete_temp_files", "1", 0x80088, "Delete custom player sprays and other temp files during shutdown" );
extern ConVar hud_takesshots;
extern ConVar cl_glow_brightness;
extern ConVar building_cubemaps;
extern ConVar survivor_incap_health;
extern ConVar cl_glow_it_timer_ratio_reduction;
void UpdateTutorClient(); // existing tutor_client.cpp singleton, NOT another owner
void SetTerrorColorCorrectionClientSideWeight( C_BaseEntity *, bool, bool );

static const char *const s_ModeLookupFiles[4] =
{
	"materials/correction/infected.raw", "materials/correction/thirdstrike.raw",
	"materials/correction/ghost.raw", "materials/correction/zombieintro.raw"
};
static bool s_bGhostBloomTintActive; // Native BSS107C2A28, process-wide.
static CUtlVector< vgui::Panel * > s_ClientGameUIPanels; //21E680/106E25D0

void RegisterClientGameUIPanel( vgui::Panel *panel )
{
	// Native21E860: stable add once, pointer identity; registry is non-owning
	// until the real VGui_Shutdown phase. No made-up per-panel constructor call.
	if ( s_ClientGameUIPanels.Find( panel ) == s_ClientGameUIPanels.InvalidIndex() )
		s_ClientGameUIPanels.AddToTail( panel );
}

static void ClearClientGameUIPanels()
{
	// Native21E7B0 does NOT purge its vector. Do not turn this into a purge,
	// destructor-driven deletion or a parallel AutoGameSystem lifetime.
	FOR_EACH_VEC( s_ClientGameUIPanels, i )
	{
		s_ClientGameUIPanels[i]->SetParent( (vgui::VPANEL)0 );
		s_ClientGameUIPanels[i]->DeletePanel();
	}
}

#pragma float_control( precise, on, push )
// Whole252D90: float remaining and quotient spills; unordered timestamp
// returns0, unordered quotient survives. Do not use generic clamp(NaN).
static double Terror_CountdownRemainingRatio( const CountdownTimer &timer )
{
	if ( !( timer.m_timestamp > 0.0f ) ) return 0.0;
	const float remaining = (float)( (double)timer.m_timestamp - (double)gpGlobals->curtime );
	const float ratio = remaining / timer.m_duration;
	if ( 0.0f > ratio ) return 0.0;
	if ( ratio > 1.0f ) return 1.0;
	return ratio;
}
#pragma float_control( pop )

ClientModeTerrorNormal::ClientModeTerrorNormal()
	: m_flObserverTransitionTime( 0 ), m_flObserverYaw( 0 ),
	m_nPanelOpenFlags( 0 ), m_nPreviousPanelOpenFlags( 0 ),
	m_bCinematicLightActive( false ), m_flSavedExposure( 0 ), m_bCinematicExposureActive( false ),
	m_bStatsRequested( false ), m_bTransitionFadeActive( false ), m_bHoldTransitionFade( false ),
	m_flBlurScale( 0 ), m_bDominationPostProcess( false ), m_bTransitionStatsPanelOpen( false ),
	m_pCrosshair( NULL ), m_pActiveColorCorrection( NULL ), m_flShowStatsTime( -1 )
{
	// 273DD0: timers/texture use genuine constructors. Static owner supplies
	// native zero-fill for omitted fields, not gameplay Spawn reinitialization.
	for ( int i = 0; i < 4; ++i )
	{
		m_flColorWeight[i] = 0.0f;
		m_hColorCorrection[i] = INVALID_CLIENT_CCHANDLE;
	}
}
ClientModeTerrorNormal::~ClientModeTerrorNormal()
{
	//274080: release mode override before texture/base viewport destruction.
	ReleaseCinematicLight();
	m_bDominationPostProcess = false;
}

static void ReceiveModeShowStats( bf_read & )
{
	static_cast< ClientModeTerrorNormal * >( GetClientModeNormal() )->ShowStats();
}
static void ReceiveModeBlurFade( bf_read & )
{
	engine->ClientCmd( "-mouse_menu" );
	static_cast< ClientModeTerrorNormal * >( GetClientModeNormal() )->StartTransitionFade( true );
}
static void ReceiveAllPlayersConnectedGameStarting( bf_read & )
{
	//26EE30: actual resource owns frozen-team state. Last HideLoadingPlaque
	//engine+116 is omitted under the ledger's explicit capability decision.
	if ( g_PR ) static_cast< C_TerrorPlayerResource * >( g_PR )->SetTeamsFrozen( false );
}
static void ReceiveDisconnectToLobby( bf_read & )
{
	// C26EE50: native byte10735EB0 selects disconnect only for -pressdemo.
	// Keep the ordinary gameover payload. Main's standing missing-module rule
	// omits only MatchFramework BroadcastEvent; no new disconnect or fake lobby.
	// Native C26EE97-C26EEAC has no framework/session-null fallback branch.
	if ( CommandLine()->FindParm( "-pressdemo" ) ) { engine->ClientCmd( "disconnect" ); return; }
	KeyValues *event = new KeyValues( "OnEngineEndGame", "reason", "gameover" );
	event->deleteThis(); // Release payload ownership when the absent broadcast is omitted.
}

void ClientModeTerrorNormal::Init()
{
	//274240 first clears GameUI's loading background, NOT main-menu override.
	CSysModule *module = Sys_LoadModule( "gameui" );
	if ( module )
	{
		CreateInterfaceFn factory = Sys_GetFactory( module );
		IGameUI *ui = factory ? static_cast< IGameUI * >( factory( GAMEUI_INTERFACE_VERSION, NULL ) ) : NULL;
		if ( ui ) ui->SetLoadingBackgroundDialog( 0 );
		Sys_UnloadModule( module );
	}
	BaseClass::Init();
	if ( !InitTerrorBaseModUI() )
		Warning( "Unable to initialize the client-owned L4D2 main menu\n" );
	// Native228D60 and274240 register these events (duplicate round events
	//are one listener identity, not double delivery).
	static const char *const events[] =
	{
		"round_end", "round_start", "player_team", "player_death", "round_end_message",
		"finale_start", "finale_escape_start", "round_start_pre_entity", "survival_goal_reached",
		"pills_used", "heal_success"
	};
	for ( int i = 0; i < ARRAYSIZE( events ); ++i ) ListenForGameEvent( events[i] );
	usermessages->HookMessage( "ShowStats", ReceiveModeShowStats );
	usermessages->HookMessage( "BlurFade", ReceiveModeBlurFade );
	usermessages->HookMessage( "WitchBloodSplatter", ReceiveWitchBloodSplatter );
	usermessages->HookMessage( "AllPlayersConnectedGameStarting", ReceiveAllPlayersConnectedGameStarting );
	usermessages->HookMessage( "DisconnectToLobby", ReceiveDisconnectToLobby );
	usermessages->HookMessage( "KillCam", ReceiveTerrorKillCam );
	HookMusicUserMessage(); //24C010 ->24BF90, actual existing MusicCmd owner
	m_PostPresets.Load();
	m_PostProcess.ReloadTarget( m_PostPresets );
	m_pCrosshair = GET_HUDELEMENT( CHudCrosshair );
}

void ClientModeTerrorNormal::VGui_Shutdown()
{
	//228730: real menu manager267BE0 shuts down before client UI registry.
	ShutdownTerrorPerfWizard(); //267BE0 singleton ->267C20, real panel owner
	ShutdownTerrorBaseModUI();
	ClearClientGameUIPanels();
	BaseClass::VGui_Shutdown();
}

static void DeleteTerrorTempFiles( const char *directory )
{
	//2705D0 MOD-only recursion; skip every dot-prefixed entry as native.
	char wildcard[MAX_PATH], filename[MAX_PATH];
	Q_snprintf( wildcard, sizeof( wildcard ), "%s/*", directory );
	FileFindHandle_t find;
	for ( const char *entry = filesystem->FindFirstEx( wildcard, "MOD", &find ); entry;
		entry = filesystem->FindNext( find ) )
	{
		if ( entry[0] == '.' ) continue;
		Q_snprintf( filename, sizeof( filename ), "%s/%s", directory, entry );
		if ( filesystem->IsDirectory( filename, "MOD" ) ) DeleteTerrorTempFiles( filename );
		else filesystem->RemoveFile( filename, "MOD" );
	}
	filesystem->FindClose( find );
}

void ClientModeTerrorNormal::Shutdown()
{
	//2723B0: same borrowed pointer clear, personal HUD destruction, release,
	//domination latch, temp-file order. Native lower Shutdown is empty.
	m_pActiveColorCorrection = NULL;
	ShutdownPersonalStatsSummary();
	ReleaseCinematicLight();
	m_bDominationPostProcess = false;
	if ( l4d_delete_temp_files.GetBool() )
	{
		DeleteTerrorTempFiles( "materials/temp" );
		DeleteTerrorTempFiles( "download/user_custom" );
	}
	BaseClass::Shutdown();
}

void ClientModeTerrorNormal::Disable()
{
	BaseClass::Disable();
	m_flShowStatsTime = -1.0f; //26EEB0, not a release/level reset
}

void ClientModeTerrorNormal::LevelInit( const char *newmap )
{
	ResetTransitioningPlayers(); //native107C6EC0=0, same existing ordered registry
	BaseClass::LevelInit( newmap );
	for ( int i = 0; i < 4; ++i )
	{
		if ( m_hColorCorrection[i] == INVALID_CLIENT_CCHANDLE )
		{
			char name[MAX_PATH];
			Q_snprintf( name, sizeof( name ), "%s_ss%d", s_ModeLookupFiles[i], 0 );
			m_flColorWeight[i] = 0.0f;
			m_hColorCorrection[i] = g_pColorCorrectionMgr->AddColorCorrection( name, s_ModeLookupFiles[i] );
		}
	}
	m_CinematicTexture.Init( "effects/spotlight", TEXTURE_GROUP_OTHER, true );
	m_flObserverTransitionTime = m_flObserverYaw = 0.0f;
	m_bStatsRequested = false;
	m_bTransitionFadeActive = m_bHoldTransitionFade = false;
	m_TransitionFadeTimer.Invalidate();
	m_PostProcess.LevelInitReset( m_PostPresets, gpGlobals->curtime );
	ReleaseCinematicLight();
	Terror_SetExposureOverride( false, 0.0f );
}

void ClientModeTerrorNormal::LevelShutdown()
{
	BaseClass::LevelShutdown();
	//2726F0 deliberately forgets, but DOES NOT remove, zombieintro lookup3.
	for ( int i = 0; i < 3; ++i ) g_pColorCorrectionMgr->RemoveColorCorrection( m_hColorCorrection[i] );
	if ( m_pActiveColorCorrection )
	{
		SetTerrorColorCorrectionClientSideWeight( m_pActiveColorCorrection, false, false );
		m_pActiveColorCorrection = NULL;
	}
	for ( int i = 0; i < 4; ++i )
	{
		m_flColorWeight[i] = 0.0f;
		m_hColorCorrection[i] = INVALID_CLIENT_CCHANDLE;
	}
	m_CinematicTexture.Shutdown();
	ReleaseCinematicLight();
	m_bDominationPostProcess = false;
	// Do NOT call the LevelInit reset here: pills/heal/fade timers are not
	//invalidated by native LevelShutdown; native state0 transition is retained.
	m_PostProcess.LevelShutdownReset( m_PostPresets, gpGlobals->curtime );
}

#pragma float_control( precise, on, push )
void ClientModeTerrorNormal::OverrideView( CViewSetup *setup )
{
	// C26FF40 first invokes the game-owned shared C3EE0 camera policy.
	BaseClass::OverrideView( setup );
	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
	float yaw = 0.0f;
	if ( player && player->m_lifeState != LIFE_ALIVE &&
		!( gpGlobals->realtime > (float)( m_flObserverTransitionTime + 0.1f ) ) )
	{
		if ( player->GetObserverMode() == OBS_MODE_CHASE ) yaw = 18.0f;
		else if ( player->GetObserverMode() == OBS_MODE_IN_EYE ) yaw = 9.0f;
		else if ( player->GetObserverMode() == OBS_MODE_ROAMING ) yaw = 12.0f;
	}
	m_flObserverYaw = yaw;
	//107DD0A8 and107DD0B0 are zero immutable angle components. The global
	//yaw107DD0AC is NOT added: it is replaced by this frame's observer yaw.
	setup->angles.x += 0.0f;
	setup->angles.y += yaw;
	setup->angles.z += 0.0f;
	if ( m_pCrosshair ) m_pCrosshair->SetCrosshairAngle( QAngle( -0.0f, -yaw, -0.0f ) );
}
#pragma float_control( pop )

void ClientModeTerrorNormal::UpdatePanelChatPosition()
{
	if ( m_nPanelOpenFlags == m_nPreviousPanelOpenFlags ) return;
	m_nPreviousPanelOpenFlags = m_nPanelOpenFlags;
	const char *animation = ( m_nPanelOpenFlags & 1 ) ? "DeathPanelOpen" :
		( m_nPanelOpenFlags & 2 ) ? "ZombieIntroPanelOpen" : "ResetChatPosition";
	m_pViewport->GetAnimationController()->StartAnimationSequence( animation );
}

void ClientModeTerrorNormal::Update()
{
	//Main-approved target GameUI publication, before native HUD/spawn readers.
	static ConVarRef menuState( "gameui_ingame_menu_active" );
	const int menuVisible = enginevgui->IsGameUIVisible() ? 1 : 0;
	if ( menuState.GetInt() != menuVisible ) menuState.SetValue( menuVisible );
	BaseClass::Update();
	// CClientGameUI::Update21E6A0 is native empty; no fake callback/framework.
	UpdateTutorClient();
	UpdatePanelChatPosition();
	UpdateCinematicLight();
	UpdatePostProcessing();
	if ( m_flShowStatsTime > 0.0f && gpGlobals->curtime > m_flShowStatsTime )
	{
		gViewPortInterface->ShowPanel( "transition_stats", true );
		m_flShowStatsTime = -1.0f;
	}
}

void ClientModeTerrorNormal::ShowStats()
{
	CTransitionStatsPanel *panel = GetTransitionStatsPanel();
	if ( panel )
	{
		panel->PrepareForShow();
		m_flShowStatsTime = gpGlobals->curtime + cl_transition_blur_fade_time.GetFloat();
	}
	engine->ClientCmd( "-mouse_menu" );
	if ( hud_takesshots.GetBool() ) gHUD.SetScreenShotTime( gpGlobals->curtime + 1.0f );
	m_bStatsRequested = true;
	StartTransitionFade( false );
}
void ClientModeTerrorNormal::StartTransitionFade( bool hold )
{
	m_bTransitionFadeActive = true;
	const float duration = cl_transition_blur_fade_time.GetFloat();
	m_TransitionFadeTimer.m_timestamp = (float)( (double)gpGlobals->curtime + (double)duration );
	m_TransitionFadeTimer.m_duration = duration;
	m_bHoldTransitionFade = hold;
}
void ClientModeTerrorNormal::StopTransitionFade()
{
	m_TransitionFadeTimer.Invalidate();
	m_bTransitionFadeActive = m_bHoldTransitionFade = false;
}
#pragma float_control( precise, on, push )
float ClientModeTerrorNormal::GetTransitionFadeScale() const
{
	if ( !m_bTransitionFadeActive ) return 1.0f;
	const float ratio = (float)Terror_CountdownRemainingRatio( m_TransitionFadeTimer );
	const float scale = ratio + ratio - 1.0f;
	if ( scale > 1.0f ) return 1.0f;
	if ( scale >= 0.0f ) return scale;
	return 0.0f;
}
bool ClientModeTerrorNormal::DoPostScreenSpaceEffects( const CViewSetup *setup )
{
	//274460 outer gates enclose BOTH glow and blur. No SDK replay post hook.
	if ( building_cubemaps.GetBool() || !( cl_glow_brightness.GetFloat() > 0.0f ) ) return true;
	if ( !m_bTransitionStatsPanelOpen ) TerrorGlow_Render( *setup, 0 );
	if ( m_bTransitionFadeActive )
	{
		const float strength = (float)pow( 1.0 - Terror_CountdownRemainingRatio( m_TransitionFadeTimer ), (double)0.699999988079071f );
		Terror_RenderTransitionBlur( strength, 0.0f );
		if ( gpGlobals->curtime >= m_TransitionFadeTimer.m_timestamp && !m_bHoldTransitionFade )
			m_bTransitionFadeActive = false;
	}
	else if ( m_flBlurScale != 0.0f ) Terror_RenderTransitionBlur( m_flBlurScale, 1.0f );
	return true;
}
#pragma float_control( pop )
void ClientModeTerrorNormal::SetBlurScale( float scale )
{
	//26F4F0 uses native31C460 = ToolFrameworkClient.m_pTools->InToolMode.
	//This is unrelated to split screen. Preserve the genuine SDK query.
	if ( !ToolsEnabled() ) m_flBlurScale = scale;
}

void ClientModeTerrorNormal::ReloadPostProcessParams( const char *filename )
{
	m_PostPresets.Load( filename ? filename : "scripts/postprocess.txt" );
	m_PostProcess.ReloadTarget( m_PostPresets );
}
CON_COMMAND_F( cl_reloadpostprocessparams, "", 0x00004008 )
{
	static_cast< ClientModeTerrorNormal * >( GetClientModeNormal() )->ReloadPostProcessParams( args.ArgC() == 2 ? args[1] : NULL );
}

// 251BB0, used by this producer (IN_EYE OR CHASE). The postprocess producer
// deliberately uses the different 251B50 (IN_EYE only) rule below.
C_TerrorPlayer *CinematicViewPlayer()
{
	C_TerrorPlayer *player = ToTerrorPlayer( C_BasePlayer::GetLocalPlayer() );
	if ( player && ( player->GetObserverMode() == OBS_MODE_IN_EYE || player->GetObserverMode() == OBS_MODE_CHASE ) )
	{
		C_BaseEntity *target = player->GetObserverTarget();
		if ( target && target->IsPlayer() ) return static_cast< C_TerrorPlayer * >( target );
	}
	return player;
}

#pragma float_control( precise, on, push )
// SSE instruction ordering at 270DD0..270EF0 and 271049..27112A. No extra
// orthogonalization of right against up (the native does not perform one).
static void CinematicProjectBasis( const Vector &forward, Vector &right, Vector &up )
{
	float dot = ( right.y * forward.y + right.x * forward.x ) + right.z * forward.z;
	right.x = right.x - dot * forward.x;
	right.y = right.y - forward.y * dot;
	right.z = right.z - forward.z * dot;
	dot = ( up.y * forward.y + up.x * forward.x ) + up.z * forward.z;
	up.x = up.x - dot * forward.x;
	up.y = up.y - forward.y * dot;
	up.z = up.z - forward.z * dot;
	VectorNormalize( right );
	VectorNormalize( up );
}

// Complete C:2707E0. All state predicates are evaluated on the ORIGINAL viewer
// before the attacker substitution. Preserve the native carry/pummel null case.
void ClientModeTerrorNormal::UpdateCinematicLight()
{
	C_TerrorPlayer *player = CinematicViewPlayer();
	if ( !player ) { ReleaseCinematicLight(); return; }
	const bool pounce = player->m_pounceAttacker.Get() != NULL || player->m_pounceVictim.Get() != NULL;
	const bool jockey = player->m_jockeyAttacker.Get() != NULL;
	const bool ledge = player->m_isHangingFromLedge;
	const bool hangingTongue = player->m_isHangingFromTongue;
	const bool tongueDrag = player->m_tongueOwner.Get() != NULL && !player->m_isHangingFromTongue &&
		(double)gpGlobals->curtime - (double)player->m_tongueVictimTimer.m_timestamp > 1.0; //254370
	const bool selfUse = player->IsPerformingSelfUseAction(); // exact 269A70(0)
	const bool pummel = player->m_pummelVictim.Get() != NULL || player->m_pummelAttacker.Get() != NULL;
	C_TerrorPlayer *tongueVictim = static_cast< C_TerrorPlayer * >( player->m_tongueVictim.Get() );
	const bool victimTongueHang = tongueVictim && tongueVictim->m_reachedTongueOwner;
	if ( !pounce && !jockey && !ledge && !hangingTongue && !tongueDrag && !selfUse && !pummel && !victimTongueHang )
	{
		ReleaseCinematicLight();
		return;
	}

	Vector color( cl_cinematiclight_r.GetFloat(), cl_cinematiclight_g.GetFloat(), cl_cinematiclight_b.GetFloat() );
	bool useExposure = true;
	if ( player->m_pounceAttacker.Get() )
	{
		player = static_cast< C_TerrorPlayer * >( player->m_pounceAttacker.Get() );
	}
	else if ( player->m_pounceVictim.Get() )
	{
	}
	else if ( player->m_jockeyAttacker.Get() )
	{
		// C2709DF: yellow light belongs to the ridden survivor, not the Jockey.
		color.Init( 0.30000001f, 0.30000001f, 0.0f );
	}
	else if ( player->m_jockeyVictim.Get() )
	{
	}
	else if ( player->m_pummelAttacker.Get() || player->m_carryAttacker.Get() )
	{
		// Native 2709A0 calls 2698F0, which reads PUMMEL attacker even when
		// carry-attacker made the gate true. Do not substitute carry attacker.
		player = static_cast< C_TerrorPlayer * >( player->m_pummelAttacker.Get() );
	}
	else if ( player->m_pummelVictim.Get() || player->m_carryVictim.Get() || ledge || hangingTongue || pummel )
	{
	}
	else if ( !tongueDrag && !selfUse )
	{
		if ( !victimTongueHang ) { ReleaseCinematicLight(); return; }
		player = static_cast< C_TerrorPlayer * >( player->m_tongueVictim.Get() );
	}
	else
	{
		color.Init( 0.30000001f, 0.30000001f, 0.30000001f );
		useExposure = false;
		if ( m_bCinematicExposureActive ) Terror_SetExposureOverride( false, 0.0f );
	}
	m_bCinematicExposureActive = useExposure;
	if ( !player ) { ReleaseCinematicLight(); return; }
	if ( !m_bCinematicLightActive )
	{
		m_flSavedExposure = Terror_GetCurrentExposure();
		m_bCinematicLightActive = true;
		m_CinematicTimer.Start( 3.0f );
	}
	float inverseExposure = 1.0f;
	if ( m_flSavedExposure != 0.0f ) inverseExposure = 1.0f / m_flSavedExposure;
	const float scale = cl_cinematiclight_scale.GetFloat();
	color.x = ( color.x * inverseExposure ) * scale;
	color.y = ( color.y * inverseExposure ) * scale;
	color.z = ( inverseExposure * color.z ) * scale;
	float exposure = cl_cinematiclight_tonemap.GetFloat() * m_flSavedExposure;
	Vector origin, forward( 0, 0, -1 ), right( 1, 0, 0 ), up( 0, 1, 0 );
	float fov = 0.0f;
	if ( pounce )
	{
		origin = player->GetAbsOrigin(); origin.z += 90.0f; fov = 68.0f;
	}
	if ( jockey )
	{
		origin = player->GetAbsOrigin(); origin.z += 120.0f; fov = 68.0f;
	}
	else if ( pummel || ledge )
	{
		origin = player->GetAbsOrigin(); origin.z += 120.0f; fov = 70.0f;
	}
	else if ( tongueDrag || victimTongueHang )
	{
		origin = player->GetAbsOrigin(); origin.z += 90.0f; fov = 60.0f;
	}
	else if ( hangingTongue )
	{
		Vector hangDirection = player->m_tongueOwner.Get() ? player->m_initialTonguePullDir : vec3_origin; //26F660
		hangDirection.z = 0.0f;
		VectorNormalize( hangDirection );
		origin = player->GetAbsOrigin(); origin.z += 80.0f;
		origin.x = hangDirection.x * 45.0f + origin.x;
		origin.y = hangDirection.y * 45.0f + origin.y;
		origin.z = hangDirection.z * 45.0f + origin.z;
		Vector mins, maxs, renderOrigin;
		{
			MDLCACHE_CRITICAL_SECTION();
			player->GetRenderBounds( mins, maxs );
			renderOrigin = player->GetRenderOrigin();
		}
		forward.x = ( ( maxs.x + mins.x ) * 0.5f + renderOrigin.x ) - origin.x;
		forward.y = ( ( maxs.y + mins.y ) * 0.5f + renderOrigin.y ) - origin.y;
		forward.z = ( ( maxs.z + mins.z ) * 0.5f + renderOrigin.z ) - origin.z;
		VectorNormalize( forward );
		CinematicProjectBasis( forward, right, up );
		const double fade = 1.0 - Terror_CountdownRemainingRatio( m_CinematicTimer );
		const float exposureDifference = exposure - m_flSavedExposure;
		const float fadeSpill = (float)fade;
		exposure = exposureDifference * fadeSpill + m_flSavedExposure;
		color.x = (float)( (double)color.x * fade );
		color.y = (float)( (double)color.y * fade );
		color.z = (float)( fade * (double)color.z );
		fov = 70.0f;
	}
	if ( selfUse )
	{
		// Native state virtual+128 is 27DAE0: EYE yaw228, not feet240.
		Vector direction;
		AngleVectors( QAngle( 0, (float)player->GetTerrorAnimState()->GetEyeYaw(), 0 ), &direction );
		VectorNormalize( direction );
		origin = player->GetAbsOrigin(); origin.z += 140.0f;
		origin.x = direction.x * 90.0f + origin.x;
		origin.y = direction.y * 90.0f + origin.y;
		origin.z = direction.z * 90.0f + origin.z;
		forward = player->GetAbsOrigin() - origin;
		VectorNormalize( forward );
		CinematicProjectBasis( forward, right, up );
		fov = 40.0f;
	}
	{
		MDLCACHE_CRITICAL_SECTION();
		SetTerrorFlashlightOverride( true ); //271152, manager+52, BEFORE allocation/update
		UpdateTerrorFlashlightOverride( origin, forward, right, up, fov, m_CinematicTexture, color );
		if ( m_bCinematicExposureActive ) Terror_SetExposureOverride( true, exposure );
	}
}

// Complete C:26EF60. Backend false restores saved regular-beam state44/8/4/
//12/16/20 OR destroys an override-only object; this is not a plain bool setter.
void ClientModeTerrorNormal::ReleaseCinematicLight()
{
	if ( m_bCinematicLightActive )
	{
		SetTerrorFlashlightOverride( false );
		m_bCinematicLightActive = false;
	}
	if ( m_bCinematicExposureActive )
	{
		Terror_SetExposureOverride( false, 0.0f );
		m_bCinematicExposureActive = false;
	}
}
#pragma float_control( pop )

C_TerrorPlayer *PostProcessViewPlayer()
{
	C_TerrorPlayer *player = ToTerrorPlayer( C_BasePlayer::GetLocalPlayer() );
	if ( player && player->GetObserverMode() == OBS_MODE_IN_EYE )
	{
		C_BaseEntity *target = player->GetObserverTarget();
		if ( target && target->IsPlayer() ) return static_cast< C_TerrorPlayer * >( target );
	}
	return player;
}

// C26F380, ClientModeTerror primary virtual byte 160.
float ClientModeTerrorNormal::GetViewModelFOV()
{
	C_TerrorPlayer *player = PostProcessViewPlayer(); // C251B50: IN_EYE only.
	if ( player && !player->IsThirdPersonCameraActive() && player->GetTeamNumber() == TEAM_INFECTED )
	{
		switch ( player->GetZombieClass() )
		{
		case 1:
			return cl_viewmodelfovsmoker.GetFloat();
		case 2:
			return cl_viewmodelfovboomer.GetFloat();
		case 3:
			return cl_viewmodelfovhunter.GetFloat();
		case 8:
			return cl_viewmodelfovtank.GetFloat();
		}
	}
	return cl_viewmodelfovsurvivor.GetFloat();
}

#pragma float_control( precise, on, push )
void ClientModeTerrorNormal::UpdateModeColorWeights()
{
	C_TerrorPlayer *raw = ToTerrorPlayer( C_BasePlayer::GetLocalPlayer() );
	C_TerrorPlayer *player = PostProcessViewPlayer(); //251B50
	if ( TerrorGameRules()->IsPlayerControlledZombies() && raw )
	{
		if ( spec_keep_team_cc.GetBool() )
		{
			if ( raw->GetTeamNumber() != TEAM_SPECTATOR && raw->GetObserverMode() != OBS_MODE_DEATHCAM ) player = raw;
		}
		else player = CinematicViewPlayer(); //251BB0, NOT251B50
	}
	if ( !player )
	{
		for ( int i = 0; i < 4; ++i ) m_flColorWeight[i] = 0.0f;
		return; //native21200 does NOT reset process-global ghost bloom here
	}
	bool active[4] = { false, false, false, false };
	if ( player->m_lifeState != LIFE_ALIVE &&
		( !spec_keep_team_cc.GetBool() || raw->GetObserverMode() == OBS_MODE_DEATHCAM ) )
	{
		active[3] = IsZombieIntroPanelVisible();
	}
	else if ( player->GetTeamNumber() == TEAM_SURVIVOR )
	{
		active[1] = ( player->m_lifeState == LIFE_ALIVE && player->m_bIsOnThirdStrike && !player->GetViewEntity() ) || cl_force_3rd_strike.GetBool();
	}
	else if ( player->GetTeamNumber() == TEAM_INFECTED )
	{
		if ( player->IsLocalPlayer() && player->IsGhost() ) active[2] = true;
		else active[0] = true;
	}
	for ( int i = 0; i < 4; ++i )
	{
		const float next = m_flColorWeight[i] + ( active[i] ? 0.1f : -0.1f );
		// Native COMISS/JBE then JAE: NaN goes to zero (not a generic clamp).
		m_flColorWeight[i] = next > 1.0f ? 1.0f : next >= 0.0f ? next : 0.0f;
	}
	static ConVarRef bloom( "r_bloomtintexponent" );
	if ( m_flColorWeight[2] > 0.0f )
	{
		if ( !s_bGhostBloomTintActive ) bloom.SetValue( r_bloomtintexponent_ghost.GetFloat() );
		s_bGhostBloomTintActive = true;
	}
	else
	{
		if ( s_bGhostBloomTintActive ) bloom.SetValue( bloom.GetDefault() );
		s_bGhostBloomTintActive = false;
	}
}

void ClientModeTerrorNormal::OnColorCorrectionWeightsReset()
{
	//271530 order: mode ramp+four publications THEN controller enable change.
	UpdateModeColorWeights();
	for ( int i = 0; i < 4; ++i )
		g_pColorCorrectionMgr->SetColorCorrectionWeight( m_hColorCorrection[i], m_flColorWeight[i], false );
	C_TerrorPlayer *player = CinematicViewPlayer();
	C_TerrorPlayer *raw = ToTerrorPlayer( C_BasePlayer::GetLocalPlayer() );
	if ( TerrorGameRules()->IsPlayerControlledZombies() && raw && raw->GetTeamNumber() != TEAM_SPECTATOR &&
		spec_keep_team_cc.GetBool() && raw->GetObserverMode() != OBS_MODE_DEATHCAM ) player = raw;
	if ( player )
	{
		C_BaseEntity *controller = player->GetTerrorColorCorrectionController();
		if ( controller != m_pActiveColorCorrection )
		{
			if ( m_pActiveColorCorrection ) SetTerrorColorCorrectionClientSideWeight( m_pActiveColorCorrection, false, false );
			if ( controller ) SetTerrorColorCorrectionClientSideWeight( controller, true, m_pActiveColorCorrection == NULL );
			m_pActiveColorCorrection = controller;
		}
	}
}
void ClientModeTerrorNormal::OnColorCorrectionEntityDeleted( C_BaseEntity *entity )
{
	if ( entity == m_pActiveColorCorrection )
	{
		m_pActiveColorCorrection = NULL;
		DevWarning( "Active FX Volume color correction entity being deleted on client\n" );
	}
}

double ClientModeTerrorNormal::GetModeSaturation() const
{
	//26F030 uses unordered-as-replace JA polarity, not max(NaN,...).
	float maximum = m_flColorWeight[1];
	if ( !( maximum > m_flColorWeight[2] ) ) maximum = m_flColorWeight[2];
	if ( !( maximum > m_flColorWeight[0] ) ) maximum = m_flColorWeight[0];
	if ( !( maximum > m_flColorWeight[3] ) ) maximum = m_flColorWeight[3];
	return 1.0 - (double)maximum;
}
#pragma float_control( pop )

void ClientModeTerrorNormal::UpdatePostProcessing()
{
	C_TerrorPlayer *player = PostProcessViewPlayer();
	TerrorPostProcessInputs_t input = {};
	input.m_bHavePlayer = player != NULL;
	if ( player )
	{
		input.m_bAlive = player->m_lifeState == LIFE_ALIVE;
		input.m_bIncapacitated = player->m_isIncapacitated;
		input.m_bHaveViewEntity = player->GetViewEntity() != NULL;
		input.m_bPounced = player->m_pounceAttacker.Get() != NULL;
		input.m_bPummeled = player->m_pummelAttacker.Get() != NULL;
		input.m_bHangingFromTongue = player->m_isHangingFromTongue;
		input.m_bHangingFromLedge = player->m_isHangingFromLedge;
		input.m_bDefibrillatorUse = player->IsPerformingUseAction( 5 ); // C269A00, no self-target prerequisite.
		input.m_bAdrenaline = player->IsAdrenalineActive();
		input.m_bThirdStrike = player->m_bIsOnThirdStrike || cl_force_3rd_strike.GetBool();
		input.m_bOnFire = player->GetEffectEntity() != NULL;
		input.m_flHealth = (float)player->GetHealth();
		input.m_flIncapHealth = survivor_incap_health.GetFloat();
		input.m_flIntensity = (float)player->m_clientIntensity;
		input.m_flITDuration = player->m_itTimer.m_duration;
		input.m_flITTimestamp = player->m_itTimer.m_timestamp;
		input.m_flITRatioReduction = cl_glow_it_timer_ratio_reduction.GetFloat();
		C_TerrorPlayer *reviver = static_cast< C_TerrorPlayer * >( player->m_reviveOwner.Get() );
		input.m_bReviving = reviver != NULL;
		if ( reviver ) input.m_flReviveProgress =
			( reviver->GetSimulationTime() - reviver->m_flProgressBarStartTime ) / reviver->m_flProgressBarDuration;
		C_TerrorPostProcessController *controller = static_cast< C_TerrorPostProcessController * >( player->GetTerrorPostProcessController() );
		if ( !controller && engine->IsLevelMainMenuBackground() ) controller = C_TerrorPostProcessController::GetMaster();
		input.m_pController = controller;
		if ( controller )
		{
			input.m_pControllerValues = controller->GetValues();
			input.m_flControllerFadeTime = controller->GetFadeTime();
		}
	}
	if ( !input.m_bHaveViewEntity && ( input.m_bPounced || input.m_bPummeled ) ) m_bDominationPostProcess = true;
	m_PostProcess.Update( input, m_PostPresets, gpGlobals->curtime );
	// Native1C4430 is a scalar publication, not an additional computation.
	// EnginePost's real proxy consumes GetPostProcessValues() directly.
	if ( m_PostProcess.GetState() != 5 && m_PostProcess.GetState() != 6 ) m_bDominationPostProcess = false;
}

extern kbutton_t in_ducktoggle;

static void ResetTerrorToggleDuck()
{
	//131270 is the REAL toggle_duck kbutton owner, not joystick/sensitivity.
	if ( ::input->KeyState( &in_ducktoggle ) != 0.0f )
	{
		in_ducktoggle.down[0] = in_ducktoggle.down[1] = 0;
		in_ducktoggle.state = 4;
	}
}
static void ClearTerrorShakesAndDuck()
{
	ScreenShake_t shake = {};
	shake.command = SHAKE_STOP;
	vieweffects->Shake( shake ); //native vieweffects+44=ClearAllShakes, no fade reset
	ResetTerrorToggleDuck();
}
static C_TerrorPlayer *EventTerrorPlayer( int userID )
{
	return ToTerrorPlayer( cl_entitylist->GetEnt( engine->GetPlayerForUserID( userID ) ) );
}

void ClientModeTerrorNormal::ResetRoundPresentation()
{
	// Listener this is mode+4 in274B40; the original clears mode300/152,
	//NOT the crosshair296. Do not disable a controller before this blind clear.
	m_pActiveColorCorrection = NULL;
	m_PostProcess.ClearController();
	for ( int index = 1; index <= gpGlobals->maxClients; ++index )
	{
		C_TerrorPlayer *player = ToTerrorPlayer( cl_entitylist->GetEnt( index ) );
		if ( !player || !g_PR->IsConnected( index ) ) continue;
		player->ParticleProp()->StopParticlesNamed( "blood_bleedout", true );
		player->ParticleProp()->StopParticlesNamed( "blood_incapacitated", true );
		player->ClearRagdoll(); //real root2E830, not the network m_hRagdoll owner
	}
	StopTransitionFade(); //mode virtual200=271BD0, NOT a viewport layout/reset
	for ( C_BaseEntity *entity = cl_entitylist->FirstBaseEntity(); entity; )
	{
		C_BaseEntity *next = cl_entitylist->NextBaseEntity( entity );
		if ( entity->IsClientCreated() && !Q_stricmp( entity->GetClassname(), "class C_ClientRagdoll" ) ) entity->SUB_Remove();
		entity = next;
	}
	C_TerrorPlayer *local = ToTerrorPlayer( C_BasePlayer::GetLocalPlayer() );
	if ( local )
	{
		DevMsg( "[STATS] ResetPerformanceStats for player '%s'\n", local->GetPlayerName() );
		local->m_flPerformanceStatsStartTime = 0.0f;
	}
}

//252DF0. Native MatchFramework persistent record reads/writes are omitted
//under Main's backend-boundary decision; native clock/gates/score comparison stay.
static void RecordTerrorRoundPerformance( int type, int reason )
{
	if ( engine->IsHLTV() ) return;
	C_TerrorPlayer *local = ToTerrorPlayer( C_BasePlayer::GetLocalPlayer() );
	if ( !local ) return;
	local->m_flPerformanceStatsStartTime = (float)Plat_FloatTime();
	const bool updateRecord = TerrorGameRules()->IsVersusMode() &&
		TerrorGameRules()->IsInSecondHalfOfRound() && type == 1 && reason == 5;
	if ( updateRecord )
	{
		const int team = local->m_iVersusTeam;
		int score = TerrorGameRules()->GetVersusChapterScore( team );
		if ( score == 0 ) score = TerrorGameRules()->GetVersusScore( team, false );
		const int other = 2 - ( team != 1 );
		int otherScore = TerrorGameRules()->GetVersusChapterScore( other );
		if ( otherScore == 0 ) otherScore = TerrorGameRules()->GetVersusScore( other, false );
		const bool nativeWin = score >= otherScore;
		NOTE_UNUSED( nativeWin );
	}
	//C252E..C252F: unavailable MatchFramework -> player manager -> local
	//persistent record (+0 played,+4 won,+8 rolling bits). No fake local record.
}

#pragma float_control( precise, on, push )
static uint64 SurvivalCentiseconds( double time )
{
	//274DD9/274ED7: x87 widens the FLOAT32 .005 constant, no .005 double.
	//The native __ftol2 is signed FISTP; retain integer-indefinite on NaN/
	//out-of-range through explicit x64 intrinsic rather than C++ undefined cast.
	return (uint64)_mm_cvttsd_si64( _mm_set_sd( floor( ( time + (double)0.0049999999f ) * 100.0 ) ) );
}
#pragma float_control( pop )

void ClientModeTerrorNormal::RecordSurvivalTime( uint64 centiseconds )
{
	//2743F0. Backend-existence check is the ONLY adapted outer gate: the
	//target has no MatchFramework pointer, but Main requires real payload
	//construction without claiming an impossible target-app leaderboard upload.
	if ( centiseconds == 0 || engine->IsHLTV() ) return;
	ConVarRef cheats( "sv_cheats", true );
	if ( cheats.IsValid() && cheats.GetBool() ) return;
	if ( !TerrorGameRules() || TerrorGameRules()->IsPlayerControlledZombies() ) return;
	KeyValues *chapter = TerrorGameRules()->GetChapterInfo();
	if ( !chapter ) return; //native27C130 mission/map dictionary prerequisite
	char leaderboard[128];
	Q_snprintf( leaderboard, sizeof( leaderboard ), "survival_%s", chapter->GetString( "map", "" ) );
	KeyValues *root = new KeyValues( "Leaderboard" );
	KeyValues *map = new KeyValues( leaderboard );
	root->AddSubKey( map );
	map->SetUint64( "besttime", centiseconds );
	unsigned char *payload = new unsigned char[256];
	Q_memset( payload, 0, 256 );
	CUtlBuffer buffer( payload, 256, 0 );
	buffer.SetBigEndian( true ); //103DAF30(1), not platform default byte order
	buffer.PutUnsignedChar( 1 );
	int count = 0;
	for ( int index = 1; count < 4 && index <= 32; ++index )
	{
		C_TerrorPlayer *player = ToTerrorPlayer( cl_entitylist->GetEnt( index ) );
		if ( !player || !g_PR->IsConnected( index ) || player->IsBot() || player->GetTeamNumber() != TEAM_SURVIVOR ) continue;
		player_info_t info;
		if ( !engine->GetPlayerInfo( index, &info ) ) continue;
		// Original player_info starts with uint64 XUID; target has friendsID.
		// Use the real connected Steam universe, not userID/fabricated XUID.
		ISteamUtils *utils = steamapicontext ? steamapicontext->SteamUtils() : NULL;
		if ( !info.friendsID || !utils ) continue; //Main: native zero-XUID path, no assumed universe
		const CSteamID steamID( info.friendsID, 1, utils->GetConnectedUniverse(), k_EAccountTypeIndividual );
		const uint64 id = steamID.ConvertToUint64();
		if ( !id ) continue;
		buffer.PutInt64( (int64)id );
		++count;
	}
	while ( count++ < 4 ) buffer.PutInt64( 0 );
	CTerrorPlayerStats stats;
	stats.CapturePlayers( 0, TEAM_SURVIVOR ); //native26DF40, same shared query as panels/crawl
	int total = stats.SumStatistic( 28 );
	buffer.PutUnsignedShort( (unsigned short)clamp( total, 0, 32767 ) );
	static const int byteStats[] = { 22, 20, 21, 23, 25, 24, 27, 13, 14, 18, 19 };
	for ( int i = 0; i < ARRAYSIZE( byteStats ); ++i )
	{
		total = stats.SumStatistic( byteStats[i] );
		buffer.PutUnsignedChar( (unsigned char)clamp( total, 0, 255 ) );
	}
	map->SetPtr( ":payloadptr", payload );
	map->SetInt( ":payloadsize", buffer.TellPut() ); //native length46, allocation256
	//27499..274AD: original scans valid local primary-context player10552,
	//then MatchFramework/GetMatchSystem/GetPlayerManager/GetLocalPlayer(0)/
	//UpdateLeaderboard(root). The immutable target has no MatchFramework or
	//L4D2 leaderboard for its Steam app. ONLY that backend chain is omitted;
	//the complete KV/binary payload above is built and released, no success.
	delete[] payload;
	root->deleteThis();
}

void ClientModeTerrorNormal::FireGameEvent( IGameEvent *event )
{
	const char *name = event->GetName();
	if ( !name || !name[0] ) return;
	//Native clears shake/toggle BEFORE splitscreenplayer filtering.
	if ( !Q_strcmp( name, "player_death" ) || !Q_strcmp( name, "player_team" ) )
	{
		C_TerrorPlayer *player = EventTerrorPlayer( event->GetInt( "userid" ) );
		if ( player && player->IsLocalPlayer() && player == C_BasePlayer::GetLocalPlayer() ) ResetTerrorToggleDuck();
	}
	else if ( !Q_strcmp( name, "round_start" ) || !Q_strcmp( name, "round_end" ) ) ClearTerrorShakesAndDuck();
	if ( event->GetInt( "splitscreenplayer", 0 ) != 0 ) return;
	if ( !Q_strcmp( name, "round_start_pre_entity" ) ) ParticleMgr()->StopEffectsLongerThan( 8.0f );
	else if ( !Q_strcmp( name, "round_start" ) ) ResetRoundPresentation();
	else if ( !Q_strcmp( name, "round_end" ) )
	{
		const int reason = event->GetInt( "reason" );
		if ( reason == 14 ) RecordSurvivalTime( SurvivalCentiseconds( (double)event->GetFloat( "time" ) ) );
		RecordTerrorRoundPerformance( 1, reason );
	}
	else if ( !Q_stricmp( name, "survival_goal_reached" ) )
	{
		C_TerrorPlayer *player = EventTerrorPlayer( event->GetInt( "userid" ) );
		if ( player && player->IsLocalPlayer() )
			RecordSurvivalTime( SurvivalCentiseconds( TerrorGameRules()->GetTeamRoundDuration( TEAM_SURVIVOR ) ) );
	}
	else if ( !Q_strcmp( name, "player_connect" ) || !Q_strcmp( name, "player_disconnect" ) )
	{
		if ( event->GetBool( "bot" ) && Q_strstr( event->GetString( "name", "" ), "Zombie" ) ) return;
	}
	else if ( !Q_stricmp( name, "pills_used" ) || !Q_stricmp( name, "heal_success" ) )
	{
		if ( EventTerrorPlayer( event->GetInt( "subject" ) ) == PostProcessViewPlayer() )
		{
			if ( !Q_stricmp( name, "pills_used" ) ) m_PostProcess.PillsUsed( gpGlobals->curtime );
			else m_PostProcess.HealSucceeded( gpGlobals->curtime );
		}
	}
	ProcessCSEvent( event );
}

void ClientModeTerrorNormal::ProcessCSEvent( IGameEvent *event )
{
	//228D60/229030 is the real native ancestor, NOT HL2MP policy.
	C_BasePlayer *local = C_BasePlayer::GetLocalPlayer();
	CHudChat *chat = GET_HUDELEMENT( CHudChat );
	if ( !local || !chat ) return;
	const char *name = event->GetName();
	if ( !name || !name[0] ) return;
	if ( !Q_strcmp( name, "round_start" ) )
	{
		C_PhysPropClientside::RecreateAll();
		engine->ClientCmd( "r_cleardecals\n" );
		enginesound->StopAllSounds( true );
		Soundscape_OnStopAllSounds();
		return;
	}
	if ( !Q_strcmp( name, "round_end" ) ) return;
	if ( !Q_strcmp( name, "round_end_message" ) )
	{
		const int winner = event->GetInt( "winner" );
		if ( winner != TEAM_INFECTED )
		{
			CLocalPlayerFilter filter;
			C_BaseEntity::EmitSound( filter, -1, winner == TEAM_SURVIVOR ? "Event.TERWin" : "Event.RoundDraw" );
		}
		internalCenterPrint->Print( g_pVGuiLocalize->Find( event->GetString( "message", "" ) ) );
		return;
	}
	ConVarRef developer( "developer" );
	if ( !Q_strcmp( name, "player_team" ) )
	{
		C_TerrorPlayer *player = EventTerrorPlayer( event->GetInt( "userid" ) );
		if ( player && !event->GetBool( "disconnect" ) )
		{
			// Native player virtual1080 is empty on every Terror table; no
			//fabricated OnTeamChanged callback or duplicate team mutation.
			const int team = event->GetInt( "team" );
			if ( developer.GetInt() > 0 && ( !event->GetBool( "isbot" ) || team == 2 || team == 4 ) )
			{
				const char *playerName = event->GetString( "name", "" );
				if ( !playerName[0] ) playerName = player->GetPlayerName();
				const char *token = team == 2 ? "#Cstrike_TitlesTXT_Game_join_terrorist" :
					team == 3 ? "#Cstrike_TitlesTXT_Game_join_ct" : "#Cstrike_game_join_spectators";
				wchar_t unicodeName[32], output[100]; char ansi[100];
				g_pVGuiLocalize->ConvertANSIToUnicode( playerName, unicodeName, sizeof( unicodeName ) );
				g_pVGuiLocalize->ConstructString( output, sizeof( output ), g_pVGuiLocalize->Find( token ), 1, unicodeName );
				g_pVGuiLocalize->ConvertUnicodeToANSI( output, ansi, sizeof( ansi ) );
				chat->Printf( 0, "%s", ansi );
			}
		}
		return;
	}
	if ( !Q_strcmp( name, "finale_start" ) || !Q_strcmp( name, "finale_escape_start" ) || !Q_strcmp( name, "player_death" ) ) return;
	if ( !Q_strcmp( name, "player_connect" ) )
	{
		if ( developer.GetInt() < 2 && !Q_stricmp( event->GetString( "address", "" ), "none" ) ) return;
	}
	else if ( !Q_strcmp( name, "player_disconnect" ) )
	{
		if ( developer.GetInt() < 2 && !Q_stricmp( event->GetString( "networkid", "" ), "BOT" ) ) return;
	}
	BaseClass::FireGameEvent( event );
}

void ClientModeTerrorNormal::InitHUD()
{
	//2740E0: latch92 cleared, vector count0 (capacity retained).
	m_nPreviousPanelOpenFlags = 0;
	m_SpawnMenus.RemoveAll();
	static const char *const names[] =
	{
		"spawnmode", "spawn_hunter", "spawn_smoker", "spawn_boomer",
		"spawn_charger", "spawn_jockey", "spawn_spitter"
	};
	for ( int i = 0; i < ARRAYSIZE( names ); ++i )
	{
		IViewPortPanel *viewPanel = m_pViewport->FindPanelByName( names[i] );
		vgui::Panel *panel = dynamic_cast< vgui::Panel * >( viewPanel );
		if ( panel ) m_SpawnMenus[m_SpawnMenus.AddToTail()] = panel;
	}
}
bool ClientModeTerrorNormal::ShouldDrawViewModel()
{
	//2286B0, with native client230D70 zoom-hide virtual on the real weapon.
	C_CSPlayer *player = ToCSPlayer( C_BasePlayer::GetLocalPlayer() );
	if ( !player ) return false;
	C_WeaponCSBase *weapon = static_cast< C_WeaponCSBase * >( player->GetActiveWeapon() );
	if ( !weapon ) return false;
	const float fov = player->GetFOV();
	if ( (double)fov != (double)player->GetDefaultFOV() && weapon->HideViewModelWhenZoomed() ) return false;
	return BaseClass::ShouldDrawViewModel();
}
int ClientModeTerrorNormal::HudElementKeyInput( int down, ButtonCode_t key, const char *binding )
{
	//271C20 re-resolves each weak VGUI handle before each use, in list order.
	FOR_EACH_VEC( m_SpawnMenus, i )
	{
		vgui::Panel *panel = m_SpawnMenus[i].Get();
		if ( panel && panel->IsVisible() )
		{
			CSpawnBaseMenu *menu = dynamic_cast< CSpawnBaseMenu * >( m_SpawnMenus[i].Get() );
			if ( menu && !menu->HudElementKeyInput( down, key, binding ) ) return 0;
		}
	}
	// C26F560/C0C3190: fold the real fullscreen route into the sole user0 mode.
	if ( g_pStatsCrawl && g_pStatsCrawl->vgui::Panel::IsVisible() && down > 0 &&
		g_pStatsCrawl->KeyInput( down, key, binding ) == 0 )
		return 0;
	if ( down != 0 && ( key == KEY_XBUTTON_BACK || key == KEY_XBUTTON_START ||
		key == STEAMCONTROLLER_SELECT || key == STEAMCONTROLLER_START ) )
	{
		CHudVote *vote = GET_HUDELEMENT( CHudVote );
		if ( vote && vote->KeyInput( down, key, binding ) == 0 )
			return 0;
	}
	CPZEndGamePanel *endGame = GET_HUDELEMENT( CPZEndGamePanel );
	if ( endGame && endGame->vgui::Panel::IsVisible() && down != 0 &&
		endGame->KeyInput( down, key, binding ) == 0 )
		return 0;
	return BaseClass::HudElementKeyInput( down, key, binding );
}
// CreateMove native slot27/C2D70 is INHERITED: localNULL->true, otherwise
// player->CreateMove(sampleTime,cmd). No Terror override exists to add.
// Shared OverrideMouseInput nativeC2DD0 also calls the real player callback
// BEFORE its active weapon callback. Apply supporting_bindings.txt to that
// existing owner; retaining the SDK weapon-only method is NOT native closure.

// Whole C2294D0. Real C_BaseEntityIteratorB9340/B9960 skips dormant entities;
// existing Root GetClassname has actual class-map then RTTI fallback42FA0.
// Do NOT replace this class-entity scan with started-smoke/per-player membership.
bool ClientModeTerrorNormal::CanRecordDemo( char *errorMsg, int length ) const
{
	C_CSPlayer *player = C_CSPlayer::GetLocalCSPlayer();
	if ( !player || player->m_lifeState != LIFE_ALIVE )
		return true;
	if ( player->m_flFlashEndTime > gpGlobals->curtime )
	{
		Q_strncpy( errorMsg, "Cannot record demos while blind.", length );
		return false;
	}
	C_BaseEntityIterator iterator;
	for ( C_BaseEntity *entity = iterator.Next(); entity; entity = iterator.Next() )
	{
		if ( !Q_strcmp( entity->GetClassname(), "class C_ParticleSmokeGrenade" ) )
		{
			Q_strncpy( errorMsg, "Cannot record demos while a smoke grenade is active.", length );
			return false;
		}
	}
	return true;
}
