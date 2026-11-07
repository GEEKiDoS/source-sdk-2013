//========= Copyright Valve Corporation, All rights reserved. ============//
// Server-only. Compose with actual same-owner header/constructor/paired round
// state/full RulesThink/physics/CleanUpMap/actor lifecycle packets together.
#include "cbase.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/director/zombiemanager.h"
#include "NextBot/NextBotManager.h"
#include "entitylist.h"
#include <emmintrin.h>
#if defined( L4D2 ) && !defined( CLIENT_DLL )
#include "igameevents.h"
#endif
#include "tier0/memdbgon.h"

extern ConVar mp_roundtime;
extern ConVar mp_freezetime;
extern void AbortCurrentCommentary(); // Native09E020, canonical existing active-node abort owner.

namespace
{
	bool NativeRoundFloatUnequal( float first, float second )
	{
		return ( _mm_movemask_ps( _mm_cmpneq_ss( _mm_set_ss( first ), _mm_set_ss( second ) ) ) & 1 ) != 0;
	}
	void ActivateRoundGameModes( const char *input )
	{
		for ( CBaseEntity *entity = gEntList.FindEntityByClassname( NULL, "info_gamemode" ); entity;
			entity = gEntList.FindEntityByClassname( entity, "info_gamemode" ) )
			entity->AcceptInput( input, NULL, NULL, variant_t(), 0 );
	}
}


bool CCSGameRules::IsSpawnPointValid( CBaseEntity *spot, CBasePlayer *player )
{
	// Native23C9D0 ->20E400. Do not use CGameRules' radius128/player-only
	// query: the original checks the actual triggered spot and a stationary hull.
	if ( !spot->IsTriggered( player ) ) return false;
	const Vector origin = spot->GetAbsOrigin();
	const Vector &hullMax = GetViewVectors()->m_vHullMax;
	const Vector &hullMin = GetViewVectors()->m_vHullMin;
	const unsigned int mask = player ? player->PhysicsSolidMaskForEntity() : 0x0200400Bu;
	trace_t trace;
	UTIL_TraceHull( origin, origin, hullMin, hullMax, mask, player, COLLISION_GROUP_NONE, &trace );
	return trace.fraction == 1.0f && !trace.allsolid && !trace.startsolid;
}

void CCSGameRules::CheckMapConditions()
{
	// Native23EDB0. Once per actual rules/map object; RestartRound retains601.
	if ( m_bLevelInitialized ) return;
	m_iNumSpawnTerrorist = 0;
	m_iNumSpawnCT = 0;
	CBaseEntity *spot = NULL;
	while ( ( spot = gEntList.FindEntityByClassname( spot, "info_player_terrorist" ) ) != NULL )
	{
		if ( IsSpawnPointValid( spot, NULL ) ) ++m_iNumSpawnTerrorist;
		else Warning( "Invalid terrorist spawnpoint at (%.1f,%.1f,%.1f)\n", spot->GetAbsOrigin().x,
			spot->GetAbsOrigin().y, spot->GetAbsOrigin().z );
	}
	while ( ( spot = gEntList.FindEntityByClassname( spot, "info_zombie_spawn" ) ) != NULL )
	{
		if ( IsSpawnPointValid( spot, NULL ) ) ++m_iNumSpawnCT;
		else Warning( "Invalid counterterrorist spawnpoint at (%.1f,%.1f,%.1f)\n", spot->GetAbsOrigin().x,
			spot->GetAbsOrigin().y, spot->GetAbsOrigin().z );
	}
	if ( gEntList.FindEntityByClassname( NULL, "info_player_logo" ) ) m_bLogoMap = true;
	m_bLevelInitialized = true;
}

void CCSGameRules::UpdatePeriodicRoundThink()
{
	// Native23F140 tail: CTerrorGameRules virtual696 is PROVEN empty2D9480.
	// Preserve the actual680 writer without inventing a nonempty callback.
	if ( _mm_movemask_ps( _mm_cmpgt_ss( _mm_set_ss( gpGlobals->curtime ),
		_mm_set_ss( m_flNextPeriodicThink ) ) ) & 1 )
		m_flNextPeriodicThink = gpGlobals->curtime + 1.0f;
}

void CTerrorGameRules::RestartRound()
{
	// Native2E0650, including state, actors, entity lifecycle and event order.
	m_flIntermissionEndTime = 0.0f; // Actual inherited32, not a second round clock.
	m_flRestartRoundTime = 0.0f; // Actual CS620.
	m_iRoundWinStatus = 0; // Actual CS604.
	m_nCachedVersusDistanceScore = 0; // Plain1068.
	m_flNextVersusScoreUpdate = 0.0f; // Plain3576.
	Q_memset( m_finaleEscapees, 0, sizeof( m_finaleEscapees ) ); // Four actual1064 bytes.
	m_hSurvivalExploitEntity = NULL; // Plain792 EHANDLE, not a new actor/service.
	m_flSurvivalExploitAccumulator = 0.0f; // Plain796.
	m_flPreviousSurvivalExploitAccumulator = 0.0f; // Plain800.
	m_flSurvivalExploitDecayTime = 0.0f; // Plain804.
	m_flSurvivalExploitDamageTime = 0.0f; // Plain808.
	m_flSurvivalExploitSpitTime = 0.0f; // Plain812.
	m_nSurvivalExploitSpitCount = 0; // Plain816, real RulesThink eligibility consumer.
	// Native scalar floating comparisons retain a stored -0, unlike an
	// unconditional bitwise CNetworkVar setter. NaN is unequal and clears.
	if ( NativeRoundFloatUnequal( m_flRoundStartTime, 0.0f ) ) m_flRoundStartTime = 0.0f;
	if ( NativeRoundFloatUnequal( m_flRoundEndTime, 0.0f ) ) m_flRoundEndTime = 0.0f;
	if ( NativeRoundFloatUnequal( m_flAccumulatedTime, 0.0f ) ) m_flAccumulatedTime = 0.0f;
	m_iTotalRoundsPlayed = (int)( (unsigned int)m_iTotalRoundsPlayed + 1u ); // Native608, ctor-1.
	m_bFreezePeriod = true;
	m_iSacrificeEscapees = 0; // Same actual1180 transport/reader, no shadow mask.
	ReadMultiplayCvars();
	if ( NativeRoundFloatUnequal( m_fLevelStartTime, gpGlobals->curtime ) ) m_fLevelStartTime = gpGlobals->curtime;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = static_cast< CTerrorPlayer * >( UTIL_PlayerByIndex( i ) );
		if ( player && player->IsPlayer() ) player->PrepareForRoundRestart(); // Native does NOT add8048/IsConnected guard here.
	}
	IGameEvent *event = gameeventmanager->CreateEvent( "round_start_pre_entity" );
	if ( event ) gameeventmanager->FireEvent( event );
	AbortCurrentCommentary(); // Native09E020, before map deletion/replay.
	CleanUpMap(); // Actual2DDB10, NOT a generic SDK reset or a timer-only facade.
	ActivateRoundGameModes( "PostSpawnActivate" ); // Native2DB2C0, before Director2659A0.
	TheDirector->OnRoundStart();
	TheNextBots().OnRoundRestart(); // Native104ABB10 ->104AB7B0.
	TheZombieManager->OnRoundStart(); // Native359D70.
	event = gameeventmanager->CreateEvent( "round_start" );
	if ( event )
	{
		event->SetInt( "timelimit", m_iRoundTime );
		event->SetInt( "fraglimit", 0 );
		event->SetString( "objective", "ESCAPE" );
		gameeventmanager->FireEvent( event );
	}
	ActivateRoundGameModes( "PreRoundActivate" ); // Native2DB340, deliberately AFTER round_start.
}

#include "l4d2/gamerules/terror_gamerules_restart_console_server_callbacks.inc"
