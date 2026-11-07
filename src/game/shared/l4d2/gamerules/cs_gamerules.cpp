//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Counter-Strike rules and their network proxy.
//
//=============================================================================//

#include "cbase.h"
#include "l4d2/gamerules/cs_gamerules.h"
#include "gamerules_register.h"
#ifndef CLIENT_DLL
#include "l4d2/gamerules/cs_team.h"
#include "l4d2/player/cs_player.h"
#include "l4d2/player/terrorplayer.h"
#include "filesystem.h"
#include "igameevents.h"
#include "l4d2/director/zombiemanager.h"
#include "tier1/fmtstr.h"
#include <emmintrin.h>
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "l4d2/gamerules/cs_gamerules_console_server_cvars.inc"
#include "tier0/memdbgon.h"

ConVar mp_playerid( "mp_playerid", "1",
#if defined( CLIENT_DLL )
	0x0000200A
#else
	0x00002006
#endif
	, "Controls what information player see in the status bar: 0 all names; 1 team names; 2 no names", true, 1.0f, true, 1.0f );

ConVar mp_playerid_delay( "mp_playerid_delay", "0",
#if defined( CLIENT_DLL )
	0x0000200A
#else
	0x00002006
#endif
	, "Number of seconds to delay showing information in the status bar", true, 0.0f, true, 1.0f );

ConVar mp_playerid_hold( "mp_playerid_hold", "0.25",
#if defined( CLIENT_DLL )
	0x0000200A
#else
	0x00002006
#endif
	, "Number of seconds to keep showing old information in the status bar", true, 0.0f, true, 1.0f );

BEGIN_NETWORK_TABLE_NOBASE( CCSGameRules, DT_CSGameRules )
#ifdef CLIENT_DLL
	RecvPropBool( RECVINFO( m_bFreezePeriod ) ),
	RecvPropInt( RECVINFO( m_iRoundTime ) ),
	RecvPropFloat( RECVINFO( m_fLevelStartTime ) ),
	RecvPropFloat( RECVINFO( m_flGameStartTime ) ),
	RecvPropInt( RECVINFO( m_iHostagesRemaining ) ),
	RecvPropBool( RECVINFO( m_bMapHasBombTarget ) ),
	RecvPropBool( RECVINFO( m_bMapHasRescueZone ) ),
	RecvPropBool( RECVINFO( m_bLogoMap ) ),
	RecvPropBool( RECVINFO( m_bBlackMarket ) ),
#else
	SendPropBool( SENDINFO( m_bFreezePeriod ) ),
	SendPropInt( SENDINFO( m_iRoundTime ), 16 ),
	SendPropFloat( SENDINFO( m_fLevelStartTime ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_flGameStartTime ), 0, SPROP_NOSCALE ),
	SendPropInt( SENDINFO( m_iHostagesRemaining ), 4 ),
	SendPropBool( SENDINFO( m_bMapHasBombTarget ) ),
	SendPropBool( SENDINFO( m_bMapHasRescueZone ) ),
	SendPropBool( SENDINFO( m_bLogoMap ) ),
	SendPropBool( SENDINFO( m_bBlackMarket ) ),
#endif
END_NETWORK_TABLE()

LINK_ENTITY_TO_CLASS( cs_gamerules, CCSGameRulesProxy );
IMPLEMENT_NETWORKCLASS_ALIASED( CSGameRulesProxy, DT_CSGameRulesProxy )

#ifdef CLIENT_DLL
static void RecvProxy_CSGameRules( const RecvProp *pProp, void **pOut, void *pData, int objectID )
{
	*pOut = CSGameRules();
	Assert( *pOut );
}

BEGIN_RECV_TABLE( CCSGameRulesProxy, DT_CSGameRulesProxy )
	RecvPropDataTable( "cs_gamerules_data", 0, 0, &REFERENCE_RECV_TABLE( DT_CSGameRules ), RecvProxy_CSGameRules ),
END_RECV_TABLE()
#else
static void *SendProxy_CSGameRules( const SendProp *pProp, const void *pStructBase, const void *pData, CSendProxyRecipients *pRecipients, int objectID )
{
	pRecipients->SetAllRecipients();
	return CSGameRules();
}

BEGIN_SEND_TABLE( CCSGameRulesProxy, DT_CSGameRulesProxy )
	SendPropDataTable( "cs_gamerules_data", 0, &REFERENCE_SEND_TABLE( DT_CSGameRules ), SendProxy_CSGameRules ),
END_SEND_TABLE()

ConVar mp_roundtime( "mp_roundtime", "5", 
#if defined( GAME_DLL )
	0x00002106
#else
	FCVAR_NOTIFY | FCVAR_REPLICATED
#endif
, "How many minutes each round takes.", true, 1, true, 9 );
ConVar mp_freezetime( "mp_freezetime", "6", 
#if defined( GAME_DLL )
	0x00002106
#else
	FCVAR_NOTIFY | FCVAR_REPLICATED
#endif
, "how many seconds to keep players frozen when the round starts", true, 0, true, 60 );
#endif

REGISTER_GAMERULES_CLASS( CCSGameRules );

CCSGameRules::CCSGameRules()
{
#ifdef CLIENT_DLL
	if ( m_bBlackMarket )
	{
		NetworkStateChanged();
		m_bBlackMarket = false;
	}
#else
	m_iRoundTime = 0;
	m_iRoundWinStatus = 0;
	if ( m_fLevelStartTime.Get() != 0.0f )
		m_fLevelStartTime.SetDirect( 0.0f );
	m_bAllowWeaponSwitch = true;
	m_bFreezePeriod = true;
	m_iNumCTWins = 0;
	m_iNumTerroristWins = 0;
	m_iNumCT = 0;
	m_iNumTerrorist = 0;
	m_flRestartRoundTime = 0.1f;
	m_iNumSpawnableCT = 0;
	m_iNumSpawnableTerrorist = 0;
	m_bGameStarted = false;
	m_iAccountCT = 0;
	m_iAccountTerrorist = 0;
	m_bTargetBombed = false;
	m_bBombDefused = false;
	m_iTotalRoundsPlayed = -1;
	if ( m_flGameStartTime.Get() != 0.0f )
		m_flGameStartTime.SetDirect( 0.0f );
	m_iHostagesRemaining = 0;
	m_bLevelInitialized = false;
	m_bLogoMap = false;
	m_flNextPeriodicThink = 0.0f;
	m_bMapHasBombTarget = false;
	m_bMapHasRescueZone = false;
	m_iNumSpawnTerrorist = 0;
	m_iNumSpawnCT = 0;
	m_bMapHasEscapeZone = false;
	m_iVIPMapState = 0;
	m_hVIP = NULL;
	m_bSuppressBombMapTimeout = false;
	m_bAllowWeaponSwitch = true;
	const __m128 roundSeconds = _mm_mul_ss( _mm_set_ss( mp_roundtime.GetFloat() ), _mm_set_ss( 60.0f ) );
	m_iRoundTime = _mm_cvttss_si32( roundSeconds );
	m_bBlackMarket = false;

	static const char *s_TeamNames[] =
	{
		"Unassigned", "Spectator", "Survivor", "Infected", "L4D1_Survivor"
	};
	for ( int team = 0; team < ARRAYSIZE( s_TeamNames ); ++team )
	{
		CCSTeam *pTeam = static_cast< CCSTeam * >( CreateEntityByName( "cs_team_manager" ) );
		pTeam->Init( s_TeamNames[team], team );
		g_Teams.AddToTail( pTeam );
	}

	const char *mapName = STRING( gpGlobals->mapname );
	if ( !mapName )
		mapName = "";
	if ( filesystem->FileExists( UTIL_VarArgs( "maps/cfg/%s.cfg", mapName ) ) )
	{
		const char *commandMapName = STRING( gpGlobals->mapname );
		if ( !commandMapName )
			commandMapName = "";
		engine->ServerCommand( UTIL_VarArgs( "exec \"%s.cfg\" */maps\n", commandMapName ) );
	}
#endif
}

CCSGameRules::~CCSGameRules()
{
#ifndef CLIENT_DLL
	// Teams belong to the entity list, not the rules object.
	g_Teams.Purge();
#endif
}

#ifndef CLIENT_DLL
extern ConVar nextlevel;

bool CCSGameRules::ClientCommand( CBaseEntity *pEdict, const CCommand &args )
{
	CCSPlayer *pPlayer = ToCSPlayer( pEdict );
	if ( !Q_stricmp( args[0], "changeteam" ) )
		return true;

	if ( !Q_stricmp( args[0], "nextmap" ) )
	{
		// Native23B177 compares curtime against the INT clock using CVTSI2SS;
		// the ordered comparison also rejects an unordered current time.
		const __m128 nextQuery = _mm_cvtsi32_ss( _mm_setzero_ps(), pPlayer->m_nextMapQueryTime );
		if ( !_mm_comigt_ss( _mm_set_ss( gpGlobals->curtime ), nextQuery ) )
			return true;

		char nextMap[32];
		// Native E0E69B0 accepts short names; SDK requires the BSP filename.
		if ( nextlevel.GetString()[0] && engine->IsMapValid( CFmtStr( "maps/%s.bsp", nextlevel.GetString() ) ) )
			Q_strncpy( nextMap, nextlevel.GetString(), sizeof( nextMap ) );
		else
			GetNextLevelName( nextMap, sizeof( nextMap ), false );
		ClientPrint( pPlayer, HUD_PRINTTALK, "#game_nextmap", nextMap );

		// Read curtime again after ClientPrint. CVTTSS2SI preserves the native
		// INT_MIN result for NaN/out-of-range rather than invoking C++ cast UB.
		pPlayer->m_nextMapQueryTime = _mm_cvttss_si32(
			_mm_add_ss( _mm_set_ss( gpGlobals->curtime ), _mm_set_ss( 1.0f ) ) );
		return true;
	}

	if ( pPlayer->ClientCommand( args ) )
		return true;
	return BaseClass::ClientCommand( pEdict, args );
}

void CCSGameRules::ReadMultiplayCvars()
{
	// Native23EAD0; fully closed unobservable616 cache/stores are elided.
	const __m128 seconds = _mm_mul_ss( _mm_set_ss( mp_roundtime.GetFloat() ), _mm_set_ss( 60.0f ) );
	m_iRoundTime = _mm_cvttss_si32( seconds );
}

void CCSGameRules::SetAllowWeaponSwitch( bool allow )
{
	m_bAllowWeaponSwitch = allow;
}

bool CCSGameRules::IsWeaponSwitchAllowed() const
{
	return m_bAllowWeaponSwitch;
}

bool CCSGameRules::FShouldSwitchWeapon( CBasePlayer *player, CBaseCombatWeapon *weapon )
{
	bool givingNamedItem = false;
	if ( player && player->IsPlayer() )
		givingNamedItem = static_cast< CCSPlayer * >( player )->IsGivingNamedItem();
	if ( ( !player->IsBot() || givingNamedItem ) && IsWeaponSwitchAllowed() )
		return CMultiplayRules::FShouldSwitchWeapon( player, weapon );
	return false;
}

void CCSGameRules::EndGameFrame()
{
	// Native23B430: actual manager frame runs at the END, before100FD630.
	TheZombieManager->StartFrame();
	BaseClass::EndGameFrame();
}

void CCSGameRules::CheckFreezePeriod()
{
	if ( !IsFinite( m_fLevelStartTime.Get() ) )
	{
		Warning( "Infinite round start time!\n" );
		m_fLevelStartTime = gpGlobals->curtime;
	}
	else if ( m_fLevelStartTime > gpGlobals->curtime )
	{
		return;
	}

	UTIL_LogPrintf( "World triggered \"Round_Start\"\n" );
	// 0x23EB20 consumes this draw even though its selected radio strings are not sent.
	RandomInt( 0, 3 );
	m_bFreezePeriod = false;

	IGameEvent *pEvent = gameeventmanager->CreateEvent( "round_freeze_end" );
	if ( pEvent )
	{
		gameeventmanager->FireEvent( pEvent );
	}

	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *pPlayer = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( pPlayer && pPlayer->State_Get() == STATE_ACTIVE && pPlayer->GetTeamNumber() != TEAM_SPECTATOR )
		{
			pPlayer->SetMaxSpeed( pPlayer->GetPlayerMaxSpeed() );
		}
	}
}

void CCSGameRules::Think()
{
	// 0x23F140 calls the common rules Think, not deathmatch/team frag-limit policy.
	CGameRules::Think();
	for ( int i = 0; i < GetNumberOfTeams(); ++i )
	{
		GetGlobalTeam( i )->Think();
	}

	if ( m_bFreezePeriod )
	{
		CheckFreezePeriod();
	}
	// The Terror round-time callback (0x2E04A0) is genuinely empty.
	CheckMapConditions();
	UpdatePeriodicRoundThink();
}

void CCSGameRules::ClientDisconnected( edict_t *pClient )
{
	BaseClass::ClientDisconnected( pClient );
	// The remaining CS win-condition callback dispatches to Terror's empty 0x2E0490.
}

bool CCSGameRules::FlPlayerFallDeathDoesScreenFade( CBasePlayer *player )
{
	// Native23F620, inherited by CTerrorGameRules; keep the ordinary death camera.
	return false;
}
#endif

// S23CAF0. CTerrorGameRules inherits this same collision-policy virtual.
bool CCSGameRules::ShouldCollide( int a, int b )
{
	if ( a > b ) V_swap( a, b );
	if ( a == COLLISION_GROUP_PLAYER_MOVEMENT && b == COLLISION_GROUP_WEAPON ) return false;
	if ( ( a == COLLISION_GROUP_PLAYER_MOVEMENT || a == COLLISION_GROUP_PLAYER ) && b == COLLISION_GROUP_PUSHAWAY ) return false;
	if ( a == COLLISION_GROUP_DEBRIS && b == COLLISION_GROUP_PUSHAWAY ) return true;
	if ( b == COLLISION_GROUP_L4D2_20 ) return a == COLLISION_GROUP_PLAYER || a == COLLISION_GROUP_PLAYER_MOVEMENT;
	return BaseClass::ShouldCollide( a, b );
}

#include "l4d2/gamerules/cs_gamerules_console_server_callbacks.inc"
