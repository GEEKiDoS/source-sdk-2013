//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/cs_player.h"
#include "hintmessage.h"
#include "l4d2/gamerules/cs_gamerules.h"
#include "usercmd.h"
#include "in_buttons.h"
#include "KeyValues.h"
#include "networkstringtable_gamedll.h"
#include "gameinterface.h"
#include "team.h"
#include "recipientfilter.h"
#include "vphysics_interface.h"
#include "obstacle_pushaway.h"
#include "l4d2/player/terrorplayer.h"
#include "shake.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/player/terror_playeranimstate.h"
#include "l4d2/entities/survivor_rescue.h"
#include "l4d2/content/terror_model_precache.h"
#include "l4d2/nav/terror_nav_area.h"
#include "l4d2/weapons/weapon_csbase.h"
#include "nav_mesh.h"
#include "ammodef.h"
#include "tier0/icommandline.h"
#include "precache_register.h"
#include "particle_parse.h"
#include "npcevent.h"
#include "model_types.h"
#include <xmmintrin.h>
#include "l4d2/player/cs_player_console_server_cvars.inc"
#if defined( L4D2 ) && !defined( CLIENT_DLL )
#include "l4d2/terror_shareddefs.h"
#endif
#include "tier0/memdbgon.h"

ConVar sv_showimpacts( "sv_showimpacts", "0", 0x00002006, "Shows client (red) and server (blue) bullet impact point (1=both, 2=client-only, 3=server-only)" );
ConVar sv_showplayerhitboxes( "sv_showplayerhitboxes", "0", 0x00002006, "Show lag compensated hitboxes for the specified player index whenever a player fires." );
ConVar sv_showdamage( "sv_showdamage", "0", 0x00002006, "Shows base damage below what sv_showimpacts would show, and if a player was hit he'll show the damage he took above it (as healthdamage (armorabsorbed)).  1 = show hits.  2 = show hits and misses" );

static ConVar sv_runcmds( "sv_runcmds", "1", 0x00000006 );
static ConVar sv_max_usercmd_future_ticks( "sv_max_usercmd_future_ticks", "8", 0x00000006,
	"Prevents clients from running usercmds too far in the future. Prevents speed hacks." );
static ConVar bot_crouch( "bot_crouch", "0", 0x00004004 );

void CCSPlayer::Precache()
{
	// S241060: a live player skips only the unnamed-player resource list.
	if ( GetMaxHealth() == 0 )
	{
		PrecacheScriptSound( "Flesh.BulletImpact" );
		PrecacheScriptSound( "Player.PickupWeapon" );
		PrecacheScriptSound( "Player.FlashlightOn" );
		PrecacheScriptSound( "Player.FlashlightOff" );
		PrecacheScriptSound( "Bot.StuckSound" );
		PrecacheScriptSound( "Bot.StuckStart" );
		PrecacheModel( "sprites/glow01.vmt" );
		PrecacheModel( "particle/beam_smoke_01.vmt" );
		PrecacheEffect( "csblood" );
		PrecacheEffect( "gunshotsplash" );
	}
	PrecacheParticleSystem( "achieved" );
	// Native bypasses CBaseMultiplayerPlayer's duplicate achieved precache.
	CBasePlayer::Precache();
}

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif

BEGIN_SEND_TABLE_NOBASE( CCSPlayer, DT_CSLocalPlayerExclusive )
	SendPropFloat( SENDINFO( m_flStamina ), 14, 0, 0.0f, 3000.0f ),
	SendPropInt( SENDINFO( m_iDirection ), 1, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_iShotsFired ), 8, SPROP_UNSIGNED ),
	SendPropFloat( SENDINFO( m_flVelocityModifier ), 8, 0, 0.0f, 1.0f ),
	SendPropBool( SENDINFO( m_duckUntilOnGround ) ),
	SendPropVectorXY( SENDINFO( m_vecOrigin ), -1, SPROP_NOSCALE | SPROP_CHANGES_OFTEN, 0.0f, HIGH_DEFAULT, SendProxy_OriginXY ),
	SendPropFloat( SENDINFO_VECTORELEM( m_vecOrigin, 2 ), -1, SPROP_NOSCALE | SPROP_CHANGES_OFTEN, 0.0f, HIGH_DEFAULT, SendProxy_OriginZ ),
	SendPropFloat( SENDINFO_VECTORELEM( m_vecViewOffset, 0 ), 10, SPROP_ROUNDDOWN, -64.0f, 64.0f ),
	SendPropFloat( SENDINFO_VECTORELEM( m_vecViewOffset, 1 ), 10, SPROP_ROUNDDOWN, -64.0f, 64.0f ),
	SendPropFloat( SENDINFO_VECTORELEM( m_vecViewOffset, 2 ), 0, SPROP_NOSCALE ),
END_SEND_TABLE()

BEGIN_SEND_TABLE_NOBASE( CCSPlayer, DT_CSNonLocalPlayerExclusive )
	SendPropVectorXY( SENDINFO( m_vecOrigin ), -1, SPROP_COORD_MP | SPROP_CHANGES_OFTEN, 0.0f, HIGH_DEFAULT, SendProxy_OriginXY ),
	SendPropFloat( SENDINFO_VECTORELEM( m_vecOrigin, 2 ), -1, SPROP_COORD_MP | SPROP_CHANGES_OFTEN, 0.0f, HIGH_DEFAULT, SendProxy_OriginZ ),
	SendPropFloat( SENDINFO_VECTORELEM( m_vecViewOffset, 0 ), 10, SPROP_ROUNDDOWN, -64.0f, 64.0f ),
	SendPropFloat( SENDINFO_VECTORELEM( m_vecViewOffset, 1 ), 10, SPROP_ROUNDDOWN, -64.0f, 64.0f ),
	SendPropFloat( SENDINFO_VECTORELEM( m_vecViewOffset, 2 ), 10, SPROP_CHANGES_OFTEN, 0.0f, 128.0f ),
END_SEND_TABLE()

IMPLEMENT_SERVERCLASS_ST( CCSPlayer, DT_CSPlayer )
	SendPropExclude( "DT_BaseAnimating", "m_flPoseParameter" ),
	SendPropExclude( "DT_BaseAnimating", "m_flPlaybackRate" ),
	SendPropExclude( "DT_BaseAnimating", "m_nSequence" ),
	SendPropExclude( "DT_BaseAnimating", "m_nNewSequenceParity" ),
	SendPropExclude( "DT_BaseAnimating", "m_nResetEventsParity" ),
	SendPropExclude( "DT_BaseAnimating", "m_nMuzzleFlashParity" ),
	SendPropExclude( "DT_BaseEntity", "m_angRotation" ),
	SendPropExclude( "DT_BaseAnimatingOverlay", "overlay_vars" ),
	SendPropExclude( "DT_BaseFlex", "m_flexWeight" ),
	SendPropExclude( "DT_BaseFlex", "m_blinktoggle" ),
	SendPropExclude( "DT_CollisionProperty", "m_vecMins" ),
	SendPropExclude( "DT_CollisionProperty", "m_vecMaxs" ),
	SendPropExclude( "DT_ServerAnimationData", "m_flCycle" ),
	SendPropExclude( "DT_AnimTimeMustBeFirst", "m_flAnimTime" ),
	SendPropExclude( "DT_BaseEntity", "m_vecOrigin" ),
	SendPropExclude( "DT_BasePlayer", "m_vecViewOffset[0]" ),
	SendPropExclude( "DT_BasePlayer", "m_vecViewOffset[1]" ),
	SendPropExclude( "DT_BasePlayer", "m_vecViewOffset[2]" ),
	SendPropExclude( "DT_BaseFlex", "m_vecViewOffset[0]" ),
	SendPropExclude( "DT_BaseFlex", "m_vecViewOffset[1]" ),
	SendPropExclude( "DT_BaseFlex", "m_vecViewOffset[2]" ),
	SendPropDataTable( "cslocaldata", 0, &REFERENCE_SEND_TABLE( DT_CSLocalPlayerExclusive ), SendProxy_SendLocalDataTable ),
	SendPropDataTable( "csnonlocaldata", 0, &REFERENCE_SEND_TABLE( DT_CSNonLocalPlayerExclusive ), SendProxy_SendNonLocalDataTable ),
	SendPropInt( SENDINFO( m_iThrowGrenadeCounter ), 3, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_iAddonBits ), 8, SPROP_UNSIGNED ),
	SendPropArray3( SENDINFO_ARRAY3( m_AddonModelWeaponIDs ),
		SendPropInt( SENDINFO_ARRAY( m_AddonModelWeaponIDs ), 8, SPROP_UNSIGNED ) ),
	SendPropInt( SENDINFO( m_iPlayerState ), 4, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_iAccount ), 16, SPROP_UNSIGNED ),
	SendPropBool( SENDINFO( m_bInBombZone ) ),
	SendPropBool( SENDINFO( m_bInBuyZone ) ),
	SendPropInt( SENDINFO( m_iClass ), 4, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_ArmorValue ), 8, 0 ),
	SendPropFloat( SENDINFO_VECTORELEM( m_angEyeAngles, 0 ), 11, SPROP_ROUNDDOWN | SPROP_CHANGES_OFTEN, 0.0f, 360.0f, SendProxy_AngleToFloat ),
	SendPropFloat( SENDINFO_VECTORELEM( m_angEyeAngles, 1 ), 11, SPROP_ROUNDDOWN | SPROP_CHANGES_OFTEN, 0.0f, 360.0f, SendProxy_AngleToFloat ),
	SendPropBool( SENDINFO( m_bHasDefuser ) ),
	SendPropBool( SENDINFO( m_bNightVisionOn ) ),
	SendPropBool( SENDINFO( m_bHasNightVision ) ),
	SendPropInt( SENDINFO( m_iNightVisionScale ), 8, SPROP_UNSIGNED ),
	SendPropBool( SENDINFO( m_bInHostageRescueZone ) ),
	SendPropBool( SENDINFO( m_bIsDefusing ) ),
	SendPropBool( SENDINFO( m_bHasHelmet ) ),
	SendPropFloat( SENDINFO( m_flFlashDuration ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_flFlashMaxAlpha ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_flProgressBarDuration ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_flProgressBarStartTime ), 0, SPROP_NOSCALE ),
	SendPropEHandle( SENDINFO( m_hRagdoll ) ),
	SendPropInt( SENDINFO( m_cycleLatch ), 4, SPROP_UNSIGNED ),
	SendPropVector( SENDINFO( m_lastLadderNormal ), -1, SPROP_COORD_MP ),
END_SEND_TABLE()

BEGIN_DATADESC( CCSPlayer )
	DEFINE_INPUTFUNC( FIELD_VOID, "OnRescueZoneTouch", RescueZoneTouch ),
	DEFINE_THINKFUNC( PushawayThink ),
END_DATADESC()

CCSPlayer::CCSPlayer()
{
	UseClientSideAnimation();
	m_iLastWeaponFireUsercmd = 0;
	m_iAddonBits = 0;
	for ( int index = 0; index < 5; ++index )
		m_AddonModelWeaponIDs.Set( index, 0 );
	m_hasEscaped = false;
	m_iAccount = 0;
	m_nativeCSSpeedEquipmentFlag = false;
	m_iClass = 0;
	m_angEyeAngles = vec3_angle;
	SetViewOffset( g_pGameRules->GetViewVectors()->m_vView );
	m_pStateInfo = NULL;
	m_iThrowGrenadeCounter = 0;
	m_lifeState = LIFE_DEAD;
	m_bInBombZone = false;
	m_bInBuyZone = false;
	m_bInHostageRescueZone = false;
	if ( m_flDeathTime.Get() != 0.0f )
		m_flDeathTime.SetDirect( 0.0f );
	// Native10340/9152 stores are closed unused-field elisions,39C2:76-77.
	m_bTeamChanged = false;
	m_iShotsFired = 0;
	m_iDirection = 0;
	m_receivesMoneyNextRound = true;
	m_bGivingNamedItem = false;
	m_isVIP = false;
	// Native9220 is write-only; the adjacent live9221 bool remains distinct.
	m_bDeathCleanup = false;
	m_teamKillCount = 0;
	m_flLastActivityTime = gpGlobals->curtime;
	m_lastActivityAngles = vec3_angle;
	m_nextMapQueryTime = 0;
	m_szNewName[0] = '\0';
	for ( int index = 0; index < 5; ++index )
		m_nameChangeHistory[index] = -600.0f;
	m_iIgnoreGlobalChat = 0;
	m_ignoreRadioMessages = false;
	m_pHintMessageQueue = new CHintMessageQueue( this );
	m_iDisplayHistoryBits = 0;
	m_bShowHints = true;
	m_iDamageHealth = 0;
	// Native9128's unread curtime destination is elided,39C2:93.
	m_iDamageArmor = 0;
	m_lastDamageTime = 0.0f;
	m_explosionDSPTime = 0.0f;
	m_cycleLatch = 0;
	m_cycleLatchTimer.Invalidate();
	m_iNightVisionScale = 220;
	// Native9216 is unused; no synthetic member/default,39C2:96.
	m_flVelocityModifier = 1.0f;
}

CCSPlayer::~CCSPlayer()
{
	// S249ED0: member/base destruction follows this explicit native order.
	if ( m_hRagdoll.Get() )
		UTIL_Remove( m_hRagdoll.Get() );
	// The queue's default destructor frees its vector, not its messages:
	// preserve the original leak rather than adding a reset/delete pass.
	delete m_pHintMessageQueue;
	m_pHintMessageQueue = NULL;
	m_damageGiven.PurgeAndDeleteElements();
	m_damageTaken.PurgeAndDeleteElements();
	// S249F67/S249F72 is the ONE unconditional animation-state release.
	m_PlayerAnimState->Release();
}

void CCSPlayer::InitialSpawn()
{
	BaseClass::InitialSpawn();
	State_Transition( STATE_WELCOME );
}


bool CCSPlayer::ShouldAutoSelectTeam()
{
	// 0x249D10 is the original CS-layer constant-false policy.
	return false;
}

bool CCSPlayer::HandleCommand_JoinTeam( int iTeam, int iCharacter, bool bForce )
{
	// The original CS layer (0x23FC60) has no team-admission policy of its own.
	return false;
}

void CCSPlayer::ShowMotD()
{
	if ( IsBot() || !g_pStringTableInfoPanel || g_pStringTableInfoPanel->FindStringIndex( "motd" ) == INVALID_STRING_INDEX )
		return;

	KeyValues *pData = new KeyValues( "data" );
	pData->SetString( "title", "#L4D_MOTD" );
	pData->SetString( "type", "1" );
	pData->SetString( "msg", "motd" );
	pData->SetString( "cmd", "joingame" );
	ShowViewPortPanel( "info", true, pData );
	pData->deleteThis();
}

bool CCSPlayer::ShouldRunRateLimitedCommand( const CCommand &args )
{
	int iCommand = m_CommandRateLimit.Find( args[0] );
	if ( iCommand == m_CommandRateLimit.InvalidIndex() )
	{
		m_CommandRateLimit.Insert( args[0], gpGlobals->curtime );
		return true;
	}
	if ( gpGlobals->curtime - m_CommandRateLimit[iCommand] < 0.3f )
		return false;
	m_CommandRateLimit[iCommand] = gpGlobals->curtime;
	return true;
}

bool CCSPlayer::ClientCommand( const CCommand &args )
{
	if ( FStrEq( args[0], "joingame" ) )
	{
		if ( State_Get() == STATE_WELCOME )
			State_Transition( STATE_PICKINGTEAM );
		return true;
	}
	if ( FStrEq( args[0], "jointeam" ) || FStrEq( args[0], "spectate" ) )
	{
		if ( ShouldRunRateLimitedCommand( args ) )
			HandleCommand_JoinTeam( FStrEq( args[0], "spectate" ) ? TEAM_SPECTATOR : atoi( args[1] ), 8 );
		return true;
	}
	return BaseClass::ClientCommand( args );
}

void CCSPlayer::ChangeTeam( int iTeam )
{
	if ( !GetGlobalTeam( iTeam ) )
	{
		Warning( "CCSPlayer::ChangeTeam( %d ) - invalid team index.\n", iTeam );
		return;
	}
	int iOldTeam = GetTeamNumber();
	if ( iOldTeam == iTeam )
		return;
	// S247340: retire inventory and light while the old team is still live.
	DropWeapons( false );
	if ( iTeam == TEAM_SPECTATOR )
		m_iOldTeam = iOldTeam;
	else
		m_bTeamChanged = true;
	FlashlightTurnOff( false );
	BaseClass::ChangeTeam( iTeam );
	m_iClass = 0;
	OnTeamChanged( iOldTeam );
}



void CCSPlayer::NoteTerrorWeaponFired()
{
	CUserCmd *command = GetCurrentCommand();
	if ( command )
		m_iLastWeaponFireUsercmd = command->command_number;
}

void CCSPlayer::PlayerRunCommand( CUserCmd *pCommand, IMoveHelper *pMoveHelper )
{
	static ConVarRef sv_cheats( "sv_cheats" );
	static ConVarRef bot_mimic( "bot_mimic", true );
	if ( !sv_runcmds.GetBool() || ( !sv_cheats.GetBool() && pCommand->tick_count > gpGlobals->tickcount + sv_max_usercmd_future_ticks.GetInt() ) )
		return;

	if ( bot_mimic.IsValid() && -bot_mimic.GetInt() == entindex() )
	{
		CUserCmd mimicCommand = *pCommand;
		mimicCommand.forwardmove = 0.0f;
		mimicCommand.sidemove = 0.0f;
		mimicCommand.upmove = 0.0f;
		mimicCommand.buttons = 0;
		mimicCommand.impulse = 0;
		if ( IsBot() && bot_crouch.GetBool() )
			mimicCommand.buttons |= IN_DUCK;
		BaseClass::PlayerRunCommand( &mimicCommand, pMoveHelper );
		return;
	}
	if ( IsBot() && bot_crouch.GetBool() )
		pCommand->buttons |= IN_DUCK;
	BaseClass::PlayerRunCommand( pCommand, pMoveHelper );
}

// S23FA80, native CCS virtual545/+2180: a genuine one-byte ret.
void CCSPlayer::UpdateAddonBits()
{
}

// S246030. Called once by each selected path through CTerrorPlayer::PostThink.
void CCSPlayer::PostThink()
{
	BaseClass::PostThink();
	UpdateAddonBits();

	QAngle angles = GetLocalAngles();
	angles.x = 0.0f;
	SetLocalAngles( angles );
	m_angEyeAngles = EyeAngles();
	m_PlayerAnimState->Update( m_angEyeAngles[YAW], m_angEyeAngles[PITCH] );

	if ( m_explosionDSPTime != 0.0f && gpGlobals->curtime >= m_explosionDSPTime )
		UpdateExplosionDSP();

	if ( IsPlayerUnderwater() && GetWaterLevel() < WL_Eyes )
	{
		StopSound( "Player.AmbientUnderWater" );
		SetPlayerUnderwater( false );
	}
}

void CCSPlayer::PlayerDeathThink()
{
	// The original CS/Terror death callback at 0x23FCC0 is a single ret.
}

void CCSPlayer::SetAnimation( PLAYER_ANIM playerAnim )
{
	// 0x24B820 is a genuine ret 4. The animstate, not the legacy
	// ref_aim_/crouch_aim_ setter, owns the survivor's main sequence.
}

void CCSPlayer::RescueZoneTouch( inputdata_t &inputdata )
{
	// S2470C0: mark the actual zone byte before the independent team/hint gate.
	m_bInHostageRescueZone = true;
	if ( GetTeamNumber() == 3 && !( m_iDisplayHistoryBits & 0x20000 ) )
		m_iDisplayHistoryBits |= 0x20000;
}

void CCSPlayer::PushawayThink()
{
	// S23FA90. Full CCS Spawn installs this actual context-think callback.
	PerformObstaclePushaway( this );
	SetNextThink( gpGlobals->curtime + 0.05f, "CSPushawayThink" );
}

// S:591410. Native game-DLL-owned definition, with the original default/flags.
ConVar mp_autokick( "mp_autokick", "0", 0x00002006, "Kick idle/team-killing players" );
extern ConVar mp_fadetoblack;
extern ConVar z_survivor_respawn_health;
extern bool g_fGameOver;

static ConVar cs_ShowStateTransitions( "cs_ShowStateTransitions", "-2", 0x00004004,
	"cs_ShowStateTransitions <ent index or -1 for all>. Show player state transitions." );

// S:59ABD0. Native release flag; the stock SDK has no FCVAR_RELEASE name.
static ConVar motd_enabled( "motd_enabled", "1", 0x00080004,
	"Toggles whether or not to show the MOTD to clients when they connect." );

// Initialized, externally backed native CS class catalogs at7A3870/7A3884.
// These are distinct from the Terror model catalogs and have no runtime writer.
static const char *const s_csInfectedClassModels[] =
{
	"models/infected/common_01.mdl",
	"models/infected/boomer.mdl",
	"models/infected/smoker.mdl",
	"models/infected/hulk.mdl",
	"models/infected/witch.mdl"
};
static const char *const s_csSurvivorClassModels[] =
{
	"models/survivors/survivor_namvet.mdl"
};

// S:248F10. A state pointer is absent until the first real State_Enter; the
// constructor's numeric ACTIVE value must not cause an initial active leave.
const CCSPlayerStateInfo *CCSPlayer::State_LookupInfo( CSPlayerState state )
{
	static const CCSPlayerStateInfo states[] =
	{
		{ STATE_ACTIVE, "STATE_ACTIVE", &CCSPlayer::State_Enter_ACTIVE, &CCSPlayer::State_Leave_ACTIVE, &CCSPlayer::State_PreThink_ACTIVE },
		{ STATE_WELCOME, "STATE_WELCOME", &CCSPlayer::State_Enter_WELCOME, NULL, &CCSPlayer::State_PreThink_WELCOME },
		{ STATE_PICKINGTEAM, "STATE_PICKINGTEAM", &CCSPlayer::State_Enter_PICKINGTEAM, NULL, &CCSPlayer::State_PreThink_OBSERVER_MODE },
		{ STATE_PICKINGCLASS, "STATE_PICKINGCLASS", &CCSPlayer::State_Enter_PICKINGCLASS, NULL, &CCSPlayer::State_PreThink_OBSERVER_MODE },
		{ STATE_DEATH_ANIM, "STATE_DEATH_ANIM", &CCSPlayer::State_Enter_DEATH_ANIM, &CCSPlayer::State_Leave_DEATH_ANIM, &CCSPlayer::State_PreThink_DEATH_ANIM },
		{ STATE_DEATH_WAIT_FOR_KEY, "STATE_DEATH_WAIT_FOR_KEY", &CCSPlayer::State_Enter_DEATH_WAIT_FOR_KEY, &CCSPlayer::State_Leave_DEATH_WAIT_FOR_KEY, &CCSPlayer::State_PreThink_DEATH_WAIT_FOR_KEY },
		{ STATE_OBSERVER_MODE, "STATE_OBSERVER_MODE", &CCSPlayer::State_Enter_OBSERVER_MODE, NULL, &CCSPlayer::State_PreThink_OBSERVER_MODE },
		{ STATE_WAITING_FOR_RESCUE, "STATE_WAITING_FOR_RESCUE", &CCSPlayer::State_Enter_WAITING_FOR_RESCUE, NULL, &CCSPlayer::State_PreThink_WAITING_FOR_RESCUE },
		{ STATE_GHOST, "STATE_GHOST", &CCSPlayer::State_Enter_GHOST, &CCSPlayer::State_Leave_GHOST, &CCSPlayer::State_PreThink_GHOST },
		{ STATE_INTRO_CAMERA, "STATE_INTRO_CAMERA", &CCSPlayer::State_Enter_INTRO_CAMERA, &CCSPlayer::State_Leave_INTRO_CAMERA, &CCSPlayer::State_PreThink_INTRO_CAMERA }
	};
	for ( int i = 0; i < ARRAYSIZE( states ); ++i )
	{
		if ( states[i].state == state )
			return &states[i];
	}
	return NULL;
}

// S:24A9B0. Re-entering the same state still runs both callbacks. Individual
// leave callbacks, not this public transition, own any same-state exception.
void CCSPlayer::State_Transition( CSPlayerState state )
{
	State_Leave( state );
	State_Enter( state );
}

void CCSPlayer::State_Leave( CSPlayerState nextState )
{
	if ( m_pStateInfo && m_pStateInfo->leave )
		( this->*m_pStateInfo->leave )( nextState );
}

// S:24A6D0. Invalid states still publish the value and start the interval.
void CCSPlayer::State_Enter( CSPlayerState state )
{
	m_iPlayerState = state;
	m_pStateInfo = State_LookupInfo( state );
	m_stateEnterTimer.Start();
	const int show = cs_ShowStateTransitions.GetInt();
	if ( show == -1 || show == entindex() )
	{
		if ( m_pStateInfo )
			Msg( "ShowStateTransitions: entering '%s'\n", m_pStateInfo->name );
		else
			Msg( "ShowStateTransitions: entering #%d\n", state );
	}
	if ( m_pStateInfo && m_pStateInfo->enter )
		( this->*m_pStateInfo->enter )();
}

// S:23FE00. This callback observes the old numeric state.
void CCSPlayer::State_Leave_ACTIVE( CSPlayerState nextState )
{
	if ( nextState != STATE_ACTIVE )
		OnLeaveActive();
}

// Native table's ACTIVE think is nullsub_18, not the lower player death think.
void CCSPlayer::State_PreThink_ACTIVE()
{
}

// S:2485F0. Observer setup happens while the fatal caller is still ALIVE;
// lower CBasePlayer::Event_Killed changes lifeState afterwards.
void CCSPlayer::State_Enter_DEATH_ANIM()
{
	if ( HasWeapons() )
		PackDeadPlayerItems();
	m_flDeathTime = gpGlobals->curtime;
	const Vector origin = GetAbsOrigin();
	const Vector viewOffset = GetViewOffset();
	const QAngle eyeAngles = EyeAngles();
	StartObserverMode( OBS_MODE_DEATHCAM );
	SetAbsOrigin( origin );
	SetViewOffset( viewOffset );
	if ( IsPlayer() &&
		( ( GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 8 ) ||
		  GetTeamNumber() == TEAM_SURVIVOR || GetTeamNumber() == TEAM_L4D1_SURVIVOR ) )
	{
		SnapEyeAngles( eyeAngles );
	}
	else if ( m_hObserverTarget.Get() )
	{
		Vector direction = m_hObserverTarget->EyePosition() - origin + viewOffset;
		VectorNormalize( direction );
		QAngle angles;
		VectorAngles( direction, angles );
		SnapEyeAngles( angles );
	}
	RemoveEffects( EF_NOINTERP );
	if ( edict() )
		edict()->m_fStateFlags |= FL_EDICT_DIRTY_PVS_INFORMATION;
	DispatchUpdateTransmitState();
	if ( mp_fadetoblack.GetBool() )
	{
		color32 black = { 0, 0, 0, 255 };
		UTIL_ScreenFade( this, black, 3.0f, 3.0f, FFADE_OUT | FFADE_STAYOUT );
	}
}

// S:243460. Airborne corpses are zeroed by this native branch. The deceleration
// is twenty units PER CALL, not twenty units per second.
void CCSPlayer::State_PreThink_DEATH_ANIM()
{
	if ( GetFlags() & FL_ONGROUND )
	{
		const float speed = GetAbsVelocity().Length() - 20.0f;
		// COMISS 0,speed / JB also takes the unordered branch.
		if ( speed > 0.0f || ( FloatBits( speed ) & 0x7FFFFFFFu ) > 0x7F800000u )
		{
			Vector velocity = GetAbsVelocity();
			VectorNormalize( velocity );
			SetAbsVelocity( velocity * speed );
		}
		else
		{
			SetAbsVelocity( vec3_origin );
		}
	}
	else
	{
		SetAbsVelocity( vec3_origin );
	}
	if ( gpGlobals->curtime >= m_flDeathTime + 6.0f )
		State_Transition( STATE_DEATH_WAIT_FOR_KEY );
}

// S:23FDD0,246BF0,23FDE0,2435B0.
void CCSPlayer::State_Leave_DEATH_ANIM( CSPlayerState nextState )
{
	OnLeaveDeathAnim();
}

void CCSPlayer::State_Enter_DEATH_WAIT_FOR_KEY()
{
	m_lifeState = LIFE_DEAD;
	SetPlaybackRate( 0.0f );
	AddEffects( EF_NODRAW );
}

void CCSPlayer::State_Leave_DEATH_WAIT_FOR_KEY( CSPlayerState nextState )
{
	OnLeaveDeathWaitForKey();
}

void CCSPlayer::State_PreThink_DEATH_WAIT_FOR_KEY()
{
	if ( GetMoveType() != MOVETYPE_NONE && ( GetFlags() & FL_ONGROUND ) )
		SetMoveType( MOVETYPE_NONE );
	if ( GetObserverTarget() )
		StartReplayMode( 8.0f, 8.0f, GetObserverTarget()->entindex() );
	State_Transition( STATE_OBSERVER_MODE );
}

// S241DA0 / S241EE0. The replay engine state is owned by CBasePlayer;
// CCSPlayer adds the native reliable client prediction/camera notifications.
bool CCSPlayer::StartReplayMode( float delay, float duration, int entity )
{
	if ( !BaseClass::StartReplayMode( delay, duration, entity ) )
		return false;

	CSingleUserRecipientFilter filter( this );
	filter.MakeReliable();
	CBaseEntity *target = GetObserverTarget();
	UserMessageBegin( filter, "KillCam" );
		WRITE_BYTE( OBS_MODE_IN_EYE );
		WRITE_BYTE( target ? target->entindex() : entindex() );
		WRITE_BYTE( target ? entindex() : 0 );
	MessageEnd();
	ClientPrint( this, HUD_PRINTCENTER, "Kill Cam Replay" );
	return true;
}

void CCSPlayer::StopReplayMode()
{
	BaseClass::StopReplayMode();
	CSingleUserRecipientFilter filter( this );
	filter.MakeReliable();
	UserMessageBegin( filter, "KillCam" );
		WRITE_BYTE( OBS_MODE_NONE );
		WRITE_BYTE( 0 );
		WRITE_BYTE( 0 );
	MessageEnd();
}

// S:242B90. State callbacks and force clearing belong to PreThink, after the
// lower player callback and activity writer, and are suppressed by game-over.
void CCSPlayer::PreThink()
{
	CBasePlayer::PreThink();
	UpdateLastActivity(); // inlined within this same native root 242B90 (not a separate native function);
	                      // factored helper owned by terrorplayer_observer.cpp (m_lastActivityAngles)
	if ( g_fGameOver )
		return;
	RecalculateMaxSpeed();
	if ( m_pStateInfo && m_pStateInfo->preThink )
		( this->*m_pStateInfo->preThink )();
	if ( m_pHintMessageQueue )
		m_pHintMessageQueue->Update();
	m_vecTotalBulletForce = vec3_origin;
	if ( mp_autokick.GetBool() && !IsBot() && !IsHLTV() &&
		!IsAutoKickDisabled() && !nav_edit.GetBool() && gpGlobals->curtime > m_flLastActivityTime + 300.0f )
	{
		UTIL_ClientPrintAll( HUD_PRINTCONSOLE, "#Game_idle_kick", GetPlayerName() );
		engine->ServerCommand( UTIL_VarArgs( "kickid %d\n", GetUserID() ) );
		m_flLastActivityTime = gpGlobals->curtime;
	}
}

// S:30CD10. The base CS virtual has its own weapon-speed policy; Terror uses
// the genuine existing GetPlayerMaxSpeed provider, including ghost/dead states.
void CTerrorPlayer::RecalculateMaxSpeed()
{
	SetMaxSpeed( GetPlayerMaxSpeed() );
}

// S:245430. Wake/Sleep are physics virtuals 96/100, not EnableCollisions.
void CCSPlayer::State_Enter_ACTIVE()
{
	SetMoveType( MOVETYPE_WALK );
	RemoveSolidFlags( FSOLID_NOT_SOLID );
	m_Local.m_iHideHUD = 0;
	if ( VPhysicsGetObject() )
		VPhysicsGetObject()->Wake();
}

// S:243360.
void CCSPlayer::State_Enter_WELCOME()
{
	StartObserverMode( gpGlobals->eLoadType == MapLoad_Background ? OBS_MODE_FIXED : OBS_MODE_ROAMING );
	SetMoveType( MOVETYPE_NONE );
	AddSolidFlags( FSOLID_NOT_SOLID );
	if ( VPhysicsGetObject() )
		VPhysicsGetObject()->Sleep();
	if ( !ShouldShowMotD() )
		engine->ClientCommand( edict(), "joingame\n" );
	else if ( CommandLine()->FindParm( "-makereslists" ) )
		engine->ClientCommand( edict(), "jointeam 3\n" );
	else
		ShowMotD();
}

// S:243410.
void CCSPlayer::State_PreThink_WELCOME()
{
	if ( m_hIntroCamera.Get() && gpGlobals->curtime >= m_flNextIntroCameraTime )
		MoveToNextIntroCamera();
}

// S:23FDA0.
void CCSPlayer::State_Enter_PICKINGTEAM()
{
	if ( !ShouldAutoSelectTeam() )
		ShowViewPortPanel( "team", true, NULL );
}

// S:2417F0. This entry really calls OnLeaveActive even if the preceding
// active-state leave already called it; cleanup and RNG must not be deduplicated.
void CCSPlayer::State_Enter_OBSERVER_MODE()
{
	RemoveAllItems( true );
	if ( mp_fadetoblack.GetBool() )
	{
		StartObserverMode( OBS_MODE_FIXED );
	}
	else
	{
		int mode = m_iObserverLastMode;
		if ( !IsBot() )
		{
			const char *value = engine->GetClientConVarValue( entindex(), "cl_ideal_spec_mode" );
			// S:2417F0 clamps native IN_EYE..ROAMING = 4..6. Under L4D2 the shared
			// enum excludes PASSTIME POI, so ROAMING is the native value 6.
			if ( value )
				mode = clamp( atoi( value ), (int)OBS_MODE_IN_EYE, (int)OBS_MODE_ROAMING );
		}
		StartObserverMode( mode );
	}
	RecalculateMaxSpeed();
	if ( VPhysicsGetObject() )
		VPhysicsGetObject()->Sleep();
	OnLeaveActive();
}

// S:23FDF0/23F830/23F840. The CS layer dispatches genuine Terror virtuals.
void CCSPlayer::State_PreThink_OBSERVER_MODE()
{
	OnPreThinkObserverMode();
}

void CCSPlayer::State_Enter_WAITING_FOR_RESCUE()
{
	OnEnterWaitingForRescue();
}

void CCSPlayer::State_PreThink_WAITING_FOR_RESCUE()
{
	OnPreThinkWaitingForRescue();
}

// S:23F850/23F860/23F870. The leave wrapper discards the next-state argument.
void CCSPlayer::State_Enter_GHOST()
{
	OnEnterGhost();
}

void CCSPlayer::State_PreThink_GHOST()
{
	OnPreThinkGhost();
}

void CCSPlayer::State_Leave_GHOST( CSPlayerState nextState )
{
	OnLeaveGhost();
}

// S:23F880/23F890/23F8A0.
void CCSPlayer::State_Enter_INTRO_CAMERA()
{
	OnEnterIntroCamera();
}

void CCSPlayer::State_PreThink_INTRO_CAMERA()
{
	OnPreThinkIntroCamera();
}

void CCSPlayer::State_Leave_INTRO_CAMERA( CSPlayerState nextState )
{
	OnLeaveIntroCamera();
}

// S:242DB0. The second search wraps point_viewcontrol; the final fallback
// really is info_player_terrorist. A camera without a target ends cycling.
void CCSPlayer::MoveToNextIntroCamera()
{
	m_hIntroCamera = gEntList.FindEntityByClassname( m_hIntroCamera.Get(), "point_viewcontrol" );
	if ( !m_hIntroCamera.Get() )
		m_hIntroCamera = gEntList.FindEntityByClassname( m_hIntroCamera.Get(), "point_viewcontrol" );

	CBaseEntity *target = NULL;
	if ( m_hIntroCamera.Get() )
		target = gEntList.FindEntityByName( NULL, STRING( m_hIntroCamera->m_target ) );
	if ( !m_hIntroCamera.Get() )
		m_hIntroCamera = gEntList.FindEntityByClassname( m_hIntroCamera.Get(), "info_player_terrorist" );

	SetViewOffset( vec3_origin );
	UTIL_SetSize( this, vec3_origin, vec3_origin );
	if ( target )
	{
		Vector direction = target->GetAbsOrigin() - m_hIntroCamera->GetAbsOrigin();
		Vector origin = m_hIntroCamera->GetAbsOrigin();
		VectorNormalize( direction );
		QAngle angles;
		VectorAngles( direction, angles );
		SetAbsOrigin( origin );
		SetAbsAngles( angles );
		SnapEyeAngles( angles );
		m_flNextIntroCameraTime = gpGlobals->curtime + 6.0f;
	}
	else
	{
		if ( m_hIntroCamera.Get() )
			SetAbsOrigin( m_hIntroCamera->GetAbsOrigin() + g_pGameRules->GetViewVectors()->m_vView );
		SetAbsAngles( vec3_angle );
		m_hIntroCamera = NULL;
	}
}

// S:301BD0. Chapter1056 is zero-based, and awaiting13192 is the saved-state
// owner's actual transition flag, not a new admission or MOTD latch.
bool CTerrorPlayer::ShouldShowMotD()
{
	if ( ( TerrorGameRules()->IsVersusMode() && TheDirector && TheDirector->GetChapterNumber() > 0 ) ||
		!engine->IsDedicatedServer() )
		return false;
	return !IsAwaitingTransitionRestore() && motd_enabled.GetBool();
}

// S:2FBBF0, used only by info_director FinishIntro (S:25CD00). Like ShowMotD
// but dedicated-only, keyed on the "hostfile" string-table entry and without a
// "cmd" key.
void CTerrorPlayer::ShowHostFile()
{
	if ( IsBot() || !engine->IsDedicatedServer() || !g_pStringTableInfoPanel ||
		g_pStringTableInfoPanel->FindStringIndex( "hostfile" ) == INVALID_STRING_INDEX )
		return;
	KeyValues *data = new KeyValues( "data" );
	data->SetString( "title", "#L4D_MOTD" );
	data->SetString( "type", "1" );
	data->SetString( "msg", "hostfile" );
	ShowViewPortPanel( "info", true, data );
	data->deleteThis();
}

// S:23FB20. There is no defined boolean result on the early-return paths.
// The native callers consume only the message side effect.
void CCSPlayer::HintMessage( const char *message, bool displayIfDead, bool overrideClientSettings, bool clearQueue )
{
	if ( !displayIfDead && !IsAlive() )
		return;
	if ( !IsNetClient() || !m_pHintMessageQueue || ( !overrideClientSettings && !m_bShowHints ) )
		return;
	if ( clearQueue )
		m_pHintMessageQueue->Reset();
	m_pHintMessageQueue->AddMessage( message, 6.0f, NULL );
}

// S:2FCCC0. The getter must be the pure native2CCA00 handle read, not the
// former refresh-on-query implementation. Rescue refresh owns separate calls.
void CTerrorPlayer::OnEnterWaitingForRescue()
{
	CSurvivorRescue *rescue = NULL;
	for ( ;; )
	{
		rescue = dynamic_cast< CSurvivorRescue * >(
			gEntList.FindEntityByClassname( rescue, "info_survivor_rescue" ) );
		if ( !rescue )
		{
			StartObserverMode( OBS_MODE_ROAMING );
			break;
		}
		if ( rescue->GetRescueTarget() == this )
		{
			SetAbsOrigin( rescue->EyePosition() );
			StartObserverMode( OBS_MODE_FIXED );
			break;
		}
	}
	RemoveAllItems( true );
	RecalculateMaxSpeed();
	if ( VPhysicsGetObject() )
		VPhysicsGetObject()->Sleep();
	OnLeaveActive();
}

// S:2FCDB0. Unlike the enter callback, this class-name traversal uses the
// concrete actor directly; do not add refresh, living or connection filters.
void CTerrorPlayer::OnPreThinkWaitingForRescue()
{
	CSurvivorRescue *rescue = NULL;
	while ( ( rescue = static_cast< CSurvivorRescue * >(
		gEntList.FindEntityByClassname( rescue, "info_survivor_rescue" ) ) ) != NULL )
	{
		if ( rescue->GetRescueTarget() == this )
		{
			SetAbsOrigin( rescue->EyePosition() );
			return;
		}
	}
}

// S:30AC70/2FC9D0/30AD10.
void CTerrorPlayer::OnEnterIntroCamera()
{
	ShowViewPortPanel( "specgui", false, NULL );
	ShowViewPortPanel( "zombie_intro", true, NULL );
	StartObserverMode( OBS_MODE_FIXED );
	SetMoveType( MOVETYPE_NONE );
	AddSolidFlags( FSOLID_NOT_SOLID );
	if ( VPhysicsGetObject() )
		VPhysicsGetObject()->Sleep();
	m_Local.m_iHideHUD |= 0x100;
	MoveToNextIntroCamera();
}

void CTerrorPlayer::OnPreThinkIntroCamera()
{
	if ( m_hIntroCamera.Get() && gpGlobals->curtime >= m_flNextIntroCameraTime )
	{
		ShowViewPortPanel( "specgui", false, NULL );
		MoveToNextIntroCamera();
	}
}

void CTerrorPlayer::OnLeaveIntroCamera()
{
	m_Local.m_iHideHUD &= ~0x100;
	ShowViewPortPanel( "zombie_intro", false, NULL );
}

// S:320D70. Selection, animation retirement and deathfall-camera following
// are separate phases. Do not replace this with generic observer maintenance.
void CTerrorPlayer::OnPreThinkObserverMode()
{
	if ( m_afButtonPressed & IN_ZOOM )
	{
		CTerrorPlayer *survivor = FindHighestFlowSurvivor( 0 );
		if ( survivor && survivor != GetObserverTarget() )
			SetObserverTarget( survivor );
	}

	CStudioHdr *studio = GetModelPtr();
	if ( studio && studio->IsValid() )
	{
		GetTerrorAnimState()->ResetGestureSlots();
		// Animstate +20 (33B9C0): skipped while sequence byte340 is locked; not
		// the unconditional low-level +56 restart (342210).
		GetTerrorAnimState()->RestartMainSequenceIfUnlocked();
		GetTerrorAnimState()->ClearAnimationState();
		for ( int i = 0; i < GetNumAnimOverlays(); ++i )
		{
			CAnimationLayer *layer = GetAnimOverlay( i );
			layer->SetOrder( 15 );
			layer->m_flCycle = 0.0f;
		}
	}

	CBaseEntity *targetCamera = NULL;
	CBaseEntity *target = GetObserverTarget();
	if ( target && target->IsPlayer() )
	{
		CBasePlayer *player = static_cast< CBasePlayer * >( target );
		CBaseEntity *view = player->GetViewEntity();
		if ( view && view != player && FClassnameIs( view, "point_deathfall_camera" ) )
			targetCamera = view;
	}

	CBaseEntity *currentCamera = GetViewEntity();
	if ( !currentCamera || currentCamera == this || !FClassnameIs( currentCamera, "point_deathfall_camera" ) )
		currentCamera = NULL;
	if ( currentCamera != targetCamera )
	{
		ClearZoomOwner();
		SetFOV( this, 0, 0.0f, 0 );
		SetViewEntity( NULL );
		if ( targetCamera )
		{
			variant_t value;
			targetCamera->AcceptInput( "Enable", this, NULL, value, 0 );
		}
	}
}

// S:246C70. The native catalogs contain one survivor and five infected models;
// the independent join-class command still has the inherited1..4/5..8 clamps.
void CCSPlayer::State_Enter_PICKINGCLASS()
{
	if ( CommandLine()->FindParm( "-makereslists" ) )
	{
		engine->ClientCommand( edict(), "joinclass 0\n" );
		return;
	}
	StartObserverMode( GetObserverMode() == OBS_MODE_DEATHCAM ? OBS_MODE_DEATHCAM : OBS_MODE_FIXED );
	m_iClass = 0;
	RecalculateMaxSpeed();
	if ( VPhysicsGetObject() )
		VPhysicsGetObject()->Sleep();
	if ( GetTeamNumber() == TEAM_SURVIVOR && ARRAYSIZE( s_csSurvivorClassModels ) > 1 )
		ShowViewPortPanel( "class_ter", true, NULL );
	else if ( GetTeamNumber() == TEAM_INFECTED && ARRAYSIZE( s_csInfectedClassModels ) > 1 )
		ShowViewPortPanel( "class_ct", true, NULL );
	else
		HandleCommand_JoinClass( 0 );
}

// S:2469D0. This handler changes the real class/model before checking whether
// the player is alive. Bot players do not commit suicide on the alive branch.
bool CCSPlayer::HandleCommand_JoinClass( int playerClass )
{
	if ( playerClass == 0 )
	{
		if ( GetTeamNumber() == TEAM_SURVIVOR )
			playerClass = RandomInt( 1, 4 );
		else if ( GetTeamNumber() == TEAM_INFECTED )
			playerClass = RandomInt( 5, 8 );
	}
	if ( GetTeamNumber() == TEAM_SURVIVOR )
		playerClass = clamp( playerClass, 1, 4 );
	else if ( GetTeamNumber() == TEAM_INFECTED )
		playerClass = clamp( playerClass, 5, 8 );
	else
		playerClass = 0;
	m_iClass = playerClass;
	SetModelFromClass();
	if ( State_Get() == STATE_ACTIVE )
		TerrorGameRules()->CheckWinConditions();
	if ( IsAlive() )
	{
		if ( !IsBot() )
			CommitSuicide( false, false );
	}
	else
	{
		GetIntoGame();
	}
	return true;
}

// S:2458C0. The Terror override remains the full31A3B0 implementation owned by
// terror_model_precache.cpp. Do not substitute this inherited catalog for it.
void CCSPlayer::SetModelFromClass()
{
	if ( GetTeamNumber() == TEAM_SURVIVOR )
	{
		int model = m_iClass - 1;
		if ( model < 0 || model >= ARRAYSIZE( s_csSurvivorClassModels ) )
		{
			model = RandomInt( 0, ARRAYSIZE( s_csSurvivorClassModels ) - 1 );
			m_iClass = model + 1;
		}
		SetModel( s_csSurvivorClassModels[model] );
	}
	else if ( GetTeamNumber() == TEAM_INFECTED )
	{
		int model = m_iClass - 5;
		if ( model < 0 || model >= ARRAYSIZE( s_csInfectedClassModels ) )
		{
			model = RandomInt( 0, ARRAYSIZE( s_csInfectedClassModels ) - 1 );
			m_iClass = model + 5;
		}
		SetModel( s_csInfectedClassModels[model] );
	}
	else
	{
		SetModel( s_csInfectedClassModels[0] );
	}
}

// S:2468B0. Native byte9199 has this speed reader and the CS PlayerSpawn
// equipment reader; its constructor writes zero. No unsupported VIP label.
void CCSPlayer::RecalculateMaxSpeed()
{
	CWeaponCSBase *weapon = static_cast< CWeaponCSBase * >( GetActiveWeapon() );
	float speed;
	if ( GetFlags() & FL_ONTRAIN )
		speed = 900.0f;
	else if ( m_nativeCSSpeedEquipmentFlag == 1 )
		speed = 227.0f;
	else
		speed = weapon ? weapon->GetMaxSpeed() : 240.0f;
	SetMaxSpeed( speed );
}

// S:24B5B0. The rules pointer is retained across state callbacks and Spawn.
// The timestamp is the existing activity clock9228, not a separate spawn timer.
bool CCSPlayer::GetIntoGame()
{
	SetFOV( this, 0, 0.0f, 0 );
	RecalculateMaxSpeed();
	m_flLastActivityTime = gpGlobals->curtime;
	CTerrorGameRules *rules = TerrorGameRules();
	if ( !rules->FPlayerCanRespawn( this ) )
	{
		State_Transition( STATE_OBSERVER_MODE );
		rules->CheckWinConditions();
		return false;
	}
	State_Transition( STATE_ACTIVE );
	Spawn();
	rules->CheckWinConditions();
	return true;
}

// S:30F270. Preserve the pre-base desired-character selection and the distinct
// post-base spawn-area12524 health policy. Current NAV area6676 is not a fallback.
bool CTerrorPlayer::GetIntoGame()
{
	CTerrorGameRules *rules = TerrorGameRules();
	const int team = GetTeamNumber();
	if ( ( team == TEAM_SURVIVOR || team == TEAM_L4D1_SURVIVOR ) &&
		m_iDesiredSurvivorCharacter != SurvivorCharacter_Unknown && rules->FPlayerCanRespawn( this ) )
	{
		SurvivorCharacterType character = SelectSurvivorCharacter( m_iDesiredSurvivorCharacter );
		m_iClass = character + 1;
		const char *model = GetTerrorSurvivorModelForCharacter( character );
		const char *previousModel = STRING( GetModelName() );
		DevMsg( "(SKIN) Survivor %s changed model from %s to %s via GetIntoGame\n",
			GetPlayerName(), previousModel ? previousModel : "", model );
		SetModel( model );
		SetSurvivorCharacter( character );
	}
	bool entered = CCSPlayer::GetIntoGame();
	if ( entered && GetTeamNumber() == TEAM_SURVIVOR )
	{
		TerrorNavArea *spawnArea = ToTerrorNavArea( GetSpawnArea() );
		SetHealth( !spawnArea || spawnArea->HasSpawnAttributes( TERROR_NAV_PLAYER_START ) ?
			GetMaxHealth() : z_survivor_respawn_health.GetInt() );
	}
	return entered;
}

// S:2459D0, CS ClearFlashbangScreenFade. m_blindUntilTime(9132) and
// m_blindStartTime(9136) are plain server floats; m_flFlashDuration(10388)
// and m_flFlashMaxAlpha(10392) are DT_CSPlayer SPROP_NOSCALE floats. L4D2 has
// no flashbang producer, but the storage and this reset are genuine.
void CCSPlayer::ClearFlashbangScreenFade()
{
	if ( m_blindUntilTime > gpGlobals->curtime )
	{
		color32 clear = { 0, 0, 0, 0 };
		UTIL_ScreenFade( this, clear, 0.01f, 0.0f, FFADE_OUT | FFADE_PURGE );
		m_flFlashDuration = 0.0f;
		m_flFlashMaxAlpha = 255.0f;
	}
	m_blindUntilTime = 0.0f;
	m_blindStartTime = 0.0f;
}

// Shared native tail of S:24B2E0/S:246810, inlined in both originals.
void CCSPlayer::RestoreViewForRoundStart()
{
	ShowViewModel( true );                // m_Local.m_bDrawViewmodel, DT_Local+136
	EnableControl( true );                // S:16A4F0(true) = RemoveFlag(FL_FROZEN); m_fFlags is entity+316
	SetViewEntity( NULL );                // S:178400(NULL)
	m_Local.m_iHideHUD &= ~HIDEHUD_ALL;   // bit 0x4
}

// S:24B2E0, player vtable+2056. The state change is conditional, unlike stock
// CS; when taken it is the ordinary leave(next ACTIVE) + enter pair.
void CCSPlayer::RoundRespawn()
{
	StopObserverMode();
	if ( State_Get() != STATE_ACTIVE )
		State_Transition( STATE_ACTIVE );
	Spawn();
	m_nButtons = 0;
	SetNextThink( TICK_NEVER_THINK );
	m_receivesMoneyNextRound = true;      // byte9176
	PrintDamageGiven();
	PrintDamageTaken();
	m_damageGiven.PurgeAndDeleteElements(); // 245490 at RoundRespawn 24B2E0
	m_damageTaken.PurgeAndDeleteElements();
	RestoreViewForRoundStart();
}

// S:246810, player vtable+2060. The Terror slot (2FADF0) is a jump thunk to
// this body, so CTerrorPlayer has no override.
void CCSPlayer::ObserverRoundRespawn()
{
	ClearFlashbangScreenFade();
	if ( m_szNewName[0] )
	{
		ChangeName( m_szNewName );        // pending rename, byte10344
		m_szNewName[0] = 0;
	}
	RestoreViewForRoundStart();
	ShowViewPortPanel( "specgui", true, NULL );
}

// S:31CFB0, Terror vtable+2056. The checkpoint reset is skipped only in
// player-controlled-infected modes.
void CTerrorPlayer::RoundRespawn()
{
	m_ResponseContexts.RemoveAll();       // S:5A590 count=0; elements are trivial
	if ( !TerrorGameRules()->IsPlayerControlledZombies() )
		ResetCheckpointStatistics();      // S:30F960
	m_hasPlayedSpawnMusic = false;        // byte15957, SavedStates owner
	CCSPlayer::RoundRespawn();
}

// S:2FACB0 (Terror) / S:23F940 (CS) at player vtable+2216. The CS body always
// applies the class model; the Terror override only does so for survivor
// teams 2 and 4 (S:350A70).
void CCSPlayer::ApplySpawnModel()
{
	SetModelFromClass();
}

void CTerrorPlayer::ApplySpawnModel()
{
	const int team = GetTeamNumber();
	if ( team == TEAM_SURVIVOR || team == TEAM_L4D1_SURVIVOR )
		SetModelFromClass();
}

// S:24AB40. Counter-Strike's CCSPlayer::Spawn with the L4D2 changes: no VIP,
// buy or radar-hint work; a ghost-state exception on the state re-entry; and a
// developer gate on the out-of-date navigation warning.
void CCSPlayer::Spawn()
{
	m_CommandRateLimit.RemoveAll();      // S:247480, frees the owned key strings
	SetProgressBarTime( 0.0f );
	CreateViewModel( 1 );
	ApplySpawnModel();
	BaseClass::Spawn();
	AddFlag( FL_ONGROUND );
	SetViewOffset( g_pGameRules->GetViewVectors()->m_vView );
	SetMaxSpeed( 250.0f );
	SetFOV( this, 0, 0.0f, 0 );
	m_bIsDefusing = false;
	m_iShotsFired = 0;
	m_iDirection = 0;
	if ( m_pHintMessageQueue )
		m_pHintMessageQueue->Reset();
	m_iDisplayHistoryBits &= ~0x1442;    // native round-clear mask
	if ( State_Get() != STATE_GHOST )
		State_Transition( State_Get() );
	ClearFlashbangScreenFade();
	m_flVelocityModifier = 1.0f;
	m_flStamina = 0.0f;
	++m_iNumSpawns;                      // native9200, shared with FPlayerCanRespawn readers
	m_flLastRadarUpdateTime = 0.0f;      // 9276
	if ( !engine->IsDedicatedServer() && TheNavMesh->IsOutOfDate() &&
		this == UTIL_GetListenServerHost() && developer.GetInt() )
	{
		ClientPrint( this, HUD_PRINTCENTER, "The Navigation Mesh was built using a different version of this map." );
	}
	m_bTeamChanged = false;              // 9208
	m_iOldTeam = TEAM_UNASSIGNED;        // 9204
	m_iRadioMessages = 60;               // 9148
	m_flRadioTime = gpGlobals->curtime;  // 9144
	if ( m_hRagdoll.Get() )
	{
		UTIL_Remove( m_hRagdoll.Get() );
		// Native clears the handle only while it still resolves; removal is
		// deferred, so it does. A stale non-NULL handle is left untouched.
		if ( m_hRagdoll.Get() )
			m_hRagdoll = NULL;
	}
	if ( m_szNewName[0] )
	{
		ChangeName( m_szNewName );
		m_szNewName[0] = 0;
	}
	SetContextThink( &CCSPlayer::PushawayThink, gpGlobals->curtime + 0.05f, "CSPushawayThink" );
	CBaseCombatWeapon *weapon = GetActiveWeapon();
	if ( weapon && !( m_afPhysicsFlags & PFLAG_OBSERVER ) )
	{
		weapon->Deploy();
		m_flNextAttack = gpGlobals->curtime;
	}
	m_explosionDSPTime = 0.0f;          // 10772, D's existing owner (damage.cpp#E64B:1663-1698)
	m_cycleLatch = 0;                    // DT_CSPlayer 10820
	m_cycleLatchTimer.Start( RandomFloat( 0.0f, 0.2f ) );
	StockPlayerAmmo( NULL );
}

static void StockWeaponAmmo( CCSPlayer *player, CWeaponCSBase *weapon )
{
	const int ammoType = weapon->GetPrimaryAmmoType();
	if ( ammoType == -1 )
		return;
	player->GiveAmmo( 9999, GetAmmoDef()->GetAmmoOfIndex( ammoType )->pName, false );
	weapon->m_iClip1 = weapon->GetMaxClip1();
}

void CCSPlayer::StockPlayerAmmo( CBaseCombatWeapon *newWeapon )
{
	if ( CWeaponCSBase *weapon = dynamic_cast< CWeaponCSBase * >( newWeapon ) )
	{
		if ( !( weapon->GetWpnData().iFlags & ITEM_FLAG_EXHAUSTIBLE ) )
			StockWeaponAmmo( this, weapon );
		return;
	}
	if ( CWeaponCSBase *primary = dynamic_cast< CWeaponCSBase * >( Weapon_GetSlot( 0 ) ) )
		StockWeaponAmmo( this, primary );
	if ( CWeaponCSBase *secondary = dynamic_cast< CWeaponCSBase * >( Weapon_GetSlot( 1 ) ) )
		StockWeaponAmmo( this, secondary );
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
void CCSPlayer::Blind( float holdTime, float fadeTime, float startingAlpha )
{
	// S246430. Native CVTTSS2SI + low byte includes unordered input.
	color32 color = { 255, 255, 255, static_cast< byte >( _mm_cvtt_ss2si( _mm_set_ss( startingAlpha ) ) ) };
	const float previousUntil = m_blindUntilTime;
	const float previousStart = m_blindStartTime;
	float until = ( gpGlobals->curtime + holdTime ) + fadeTime * 0.5f;
	if ( previousUntil > until ) until = previousUntil;
	m_blindUntilTime = until;
	m_blindStartTime = gpGlobals->curtime;
	if ( GetObserverMode() != OBS_MODE_NONE && GetObserverMode() != OBS_MODE_IN_EYE )
	{
		if ( !mp_fadetoblack.GetBool() )
		{
			color.a = 150;
			if ( !( fadeTime < 0.5f ) ) fadeTime = 0.5f;
			const float halfFade = fadeTime * 0.5f;
			if ( !( halfFade > holdTime ) ) holdTime = halfFade;
			UTIL_ScreenFade( this, color, fadeTime, holdTime, FFADE_IN );
		}
		return;
	}
	const float duration = static_cast< float >( static_cast< double >( fadeTime ) * ( 5.0 / 7.0 ) );
	if ( gpGlobals->curtime > previousUntil )
	{
		m_flFlashDuration = duration;
		m_flFlashMaxAlpha = startingAlpha;
	}
	else
	{
		float remaining = ( m_flFlashDuration + previousStart ) - gpGlobals->curtime;
		if ( !( remaining > duration ) ) remaining = duration;
		m_flFlashDuration = remaining;
		if ( !( m_flFlashMaxAlpha > startingAlpha ) ) m_flFlashMaxAlpha = startingAlpha;
	}
	NetworkStateChanged();
	IGameEvent *event = gameeventmanager->CreateEvent( "player_blind" );
	if ( event )
	{
		event->SetInt( "userid", GetUserID() );
		gameeventmanager->FireEvent( event );
	}
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

#include "l4d2/player/cs_player_console_server_callbacks.inc"

// S241BC0: new shared footsteps and the two legacy server QC events.
void CCSPlayer::HandleAnimEvent( animevent_t *event )
{
	if ( event->event != 50 && event->event != 51 && event->event != 4001 && event->event != 4002 )
	{
		CBasePlayer::HandleAnimEvent( event );
		return;
	}
	// Native10177630 masks both world and volume contents. The SDK's unmasked
	// point query can instead return a solid prop and hide water behind it.
	class CFootstepWaterFilter : public CTraceFilter
	{
	public:
		virtual bool ShouldHitEntity( IHandleEntity *handle, int contentsMask ) OVERRIDE
		{
			CBaseEntity *entity = EntityFromEntityHandle( handle );
			if ( !entity || !( entity->GetSolidFlags() & FSOLID_VOLUME_CONTENTS ) )
				return false;
			const model_t *model = entity->GetModel();
			return model && modelinfo->GetModelType( model ) == mod_brush;
		}
		virtual TraceType_t GetTraceType() const OVERRIDE { return TRACE_EVERYTHING_FILTER_PROPS; }
	} waterFilter;
	const Vector position = GetAbsOrigin();
	Ray_t waterRay;
	trace_t waterTrace;
	waterRay.Init( position, position );
	enginetrace->TraceRay( waterRay, CONTENTS_WATER, &waterFilter, &waterTrace );
	const bool wading = ( waterTrace.contents & CONTENTS_WATER ) != 0;
	bool shallow = wading;
	if ( !shallow )
	{
		const Vector &point = GetAbsOrigin();
		waterRay.Init( point, point );
		enginetrace->TraceRay( waterRay, CONTENTS_WATER, &waterFilter, &waterTrace );
		shallow = ( waterTrace.contents & CONTENTS_WATER ) != 0;
	}
	bool left = event->event == 50;
	if ( ( event->event == 4001 || event->event == 4002 ) && event->options && event->options[0] == 'l' )
		left = true;
	PlayTerrorFootstepSound( GetAbsOrigin(), left, shallow, wading, false );
}

surfacedata_t *CCSPlayer::GetTerrorFootstepSurface( int &surfaceIndex )
{
	surfaceIndex = m_surfaceProps;
	return GetSurfaceData();
}

bool CCSPlayer::IsTerrorFootstepSurfaceValid( float speed, bool force ) const
{
	return speed > 110.0f || force;
}
