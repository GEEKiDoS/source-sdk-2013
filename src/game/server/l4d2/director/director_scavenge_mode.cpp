//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/director/director_scavenge_mode.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/sound/terror_music.h"
#include "l4d2/weapons/weapon_spawn.h"
#include "entitylist.h"
#include <emmintrin.h>
#include "tier0/memdbgon.h"

static ConVar scavenge_cluster_range_squared( "scavenge_cluster_range_squared", "100000", 0x00004006,
	"Cans closer than this range will be combined into a single cluster." );
static ConVar scavenge_cluster_bonus_time( "scavenge_cluster_bonus_time", "15", 0x00004006,
	"Time added to the clock every time a cluster is cleared (in linear scavenge)" );
static ConVar scavenge_score_bonus_time( "scavenge_score_bonus_time", "20", 0x00004006,
	"Time added to the clock every time a team scores" );
ConVar scavenge_round_initial_time( "scavenge_round_initial_time", "90", 0x00004006,
	"Initial time on the clock when a scavenge round starts" );
static ConVar scavenge_round_setup_time( "scavenge_round_setup_time", "45", 0x00004006,
	"Setup time before the round starts" );

CDirectorScavengeMode::CDirectorScavengeMode()
{
	m_nCurrentCluster = 0; // Actual whole-object zero-allocation; BuildClusters later writes-1.
	m_nReplayIndex = 0;
	m_TargetPosition.Init();
	m_bScenarioExitPending = false;
	m_bStarted = false;
	m_bApproachingRoundWin = false;
	m_bTeamsFlipped = false;
	m_bInOvertime = false;
	m_bWasInOvertime = false;
	m_flLastPourTime = 0.0f;
	m_nIntensity = 0;
}
void CDirectorScavengeMode::OnMissionStart()
{
	m_bTeamsFlipped = false; // S:285670; round Reset285840 preserves this flag.
	m_bScenarioExitPending = false; // Native285670 clears byte0, NOT started8.
	m_TargetPosition.Init();
}
bool CDirectorScavengeMode::SwapRoles()
{
	m_bTeamsFlipped = !m_bTeamsFlipped; // S:285690.
	return m_bTeamsFlipped;
}
CDirectorScavengeMode::~CDirectorScavengeMode() { ClearClusters(); }
void CDirectorScavengeMode::ClearClusters()
{
	m_Clusters.Purge();
	m_nCurrentCluster = -1;
}
void CDirectorScavengeMode::Reset()
{
	m_bStarted = false; // Native285840 clears WORD8 and byte11 only.
	m_bApproachingRoundWin = false;
	m_bInOvertime = false;
	if ( TerrorGameRules() )
	{
		TerrorGameRules()->ResetRoundAccumulatedTime();
		TerrorGameRules()->AddRoundAccumulatedTime( TheDirector->GetDirectorOptionFloat( "ScavengeRoundInitialTime", scavenge_round_initial_time.GetFloat() ) );
	}
}
int CDirectorScavengeMode::CompareSpawnDistance( const EHANDLE *left, const EHANDLE *right )
{
	const Vector &target = TheDirector->GetScavengeMode()->m_TargetPosition;
	// S:2859F0 truncates BOTH squared distances BEFORE subtracting. Preserve ties.
	const Vector a = left->Get()->GetAbsOrigin() - target;
	const Vector b = right->Get()->GetAbsOrigin() - target;
	const float first = ( a.y * a.y + a.x * a.x ) + a.z * a.z;
	const float second = ( b.y * b.y + b.x * b.x ) + b.z * b.z;
	const int firstInt = _mm_cvttss_si32( _mm_set_ss( first ) );
	const int secondInt = _mm_cvttss_si32( _mm_set_ss( second ) );
	return (int)( (unsigned int)firstInt - (unsigned int)secondInt );
}
int CDirectorScavengeMode::BuildClusters()
{
	if ( !g_TerrorModes.IsBaseMode( "scavenge" ) ||
		!TheDirector->GetScriptedMode()->QueryOptionBool( "cm_SingleScavengeCluster", false ) )
		return 0;
	CBaseEntity *target = gEntList.FindEntityByClassname( NULL, "point_prop_use_target" );
	if ( target ) m_TargetPosition = target->GetAbsOrigin();
	CUtlVector<EHANDLE> spawns;
	for ( CBaseEntity *spawn = gEntList.FindEntityByClassname( NULL, "weapon_scavenge_item_spawn" ); spawn;
		spawn = gEntList.FindEntityByClassname( spawn, "weapon_scavenge_item_spawn" ) )
		spawns.AddToTail( spawn );
	spawns.Sort( CompareSpawnDistance );
	ClearClusters();
	const float range = scavenge_cluster_range_squared.GetFloat();
	FOR_EACH_VEC( spawns, i )
	{
		int nearest = -1;
		float distance = range;
		FOR_EACH_VEC( m_Clusters, cluster )
		{
			float candidate = ( spawns[i]->GetAbsOrigin() - m_Clusters[cluster][0]->GetAbsOrigin() ).LengthSqr();
			if ( distance > candidate )
			{
				nearest = cluster;
				distance = candidate;
			}
		}
		if ( nearest == -1 ) nearest = m_Clusters.AddToTail();
		m_Clusters[nearest].AddToTail( spawns[i] );
	}
	if ( !TheDirector->IsInSecondHalfOfRound() )
	{
		m_ClusterOrder.RemoveAll();
		m_nReplayIndex = 0;
	}
	SelectNextCluster();
	return spawns.Count();
}
int CDirectorScavengeMode::SpawnScavengeItems()
{
	int count = BuildClusters();
	if ( count <= 0 )
	{
		count = 0;
		for ( CBaseEntity *entity = gEntList.FindEntityByClassname( NULL, "weapon_scavenge_item_spawn" ); entity;
			entity = gEntList.FindEntityByClassname( entity, "weapon_scavenge_item_spawn" ) )
		{
			CWeaponScavengeItemSpawn *spawn = static_cast<CWeaponScavengeItemSpawn *>( entity );
			inputdata_t input;
			input.pActivator = NULL; input.pCaller = NULL; input.nOutputID = 0;
			spawn->InputSpawnScavengeItem( input );
			++count;
		}
	}
	return count;
}
bool CDirectorScavengeMode::SelectNextCluster()
{
	if ( m_nCurrentCluster >= 0 && m_nCurrentCluster < m_Clusters.Count() )
		m_Clusters.Remove( m_nCurrentCluster );
	if ( !m_Clusters.Count() ) return false;
	m_nCurrentCluster = RandomInt( 0, ( m_Clusters.Count() - 1 ) / 2 );
	if ( TheDirector->IsInSecondHalfOfRound() )
	{
		if ( m_nReplayIndex < m_ClusterOrder.Count() )
			m_nCurrentCluster = m_ClusterOrder[m_nReplayIndex++];
	}
	else
		m_ClusterOrder.AddToTail( m_nCurrentCluster );
	CUtlVector<EHANDLE> &items = m_Clusters[m_nCurrentCluster];
	FOR_EACH_VEC( items, i )
	{
		CWeaponScavengeItemSpawn *spawn = static_cast<CWeaponScavengeItemSpawn *>( items[i].Get() );
		if ( spawn )
		{
			inputdata_t input;
			input.pActivator = NULL; input.pCaller = NULL; input.nOutputID = 0;
			spawn->InputSpawnScavengeItem( input );
		}
	}
	return true;
}
void CDirectorScavengeMode::RemoveItem( CBaseHandle item )
{
	if ( !TheDirector->GetScriptedMode()->QueryOptionBool( "cm_SingleScavengeCluster", false ) ) return;
	const EHANDLE handle = EHANDLE::UnsafeFromBaseHandle( item );
	FOR_EACH_VEC( m_Clusters, i )
	{
		int index = m_Clusters[i].Find( handle );
		if ( index != m_Clusters[i].InvalidIndex() )
		{
			m_Clusters[i].Remove( index );
			break;
		}
	}
	if ( m_nCurrentCluster < 0 || m_nCurrentCluster >= m_Clusters.Count() || m_Clusters[m_nCurrentCluster].Count() ) return;
	if ( !SelectNextCluster() ) return;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( player && player->edict() && player->entindex() && player->IsPlayer() && player->IsConnected() )
			player->EmitSound( "Scavenge.ClusterSpawned", gpGlobals->curtime + 1.0f );
	}
	TerrorGameRules()->AddRoundAccumulatedTime( TheDirector->GetDirectorOptionFloat( "ScavengeClusterBonusTime", scavenge_cluster_bonus_time.GetFloat() ) );
}
void CDirectorScavengeMode::StartRound()
{
	if ( m_bStarted ) return;
	DevMsg( "%3.2f: Director::OnScavengeRoundStarted: Scavenge started.\n", gpGlobals->curtime );
	char script[256];
	Q_snprintf( script, sizeof( script ), "%s_scavenge", STRING( gpGlobals->mapname ) );
	TheDirector->RunScript( script, 1, false );
	m_StartTimer.Invalidate();
	m_bStarted = true;
	m_nIntensity = 0;
	m_flLastPourTime = 0;
	m_bWasInOvertime = false;
	if ( TerrorGameRules() )
	{
		TerrorGameRules()->SetRoundStartTime( gpGlobals->curtime );
		TerrorGameRules()->ResetRoundAccumulatedTime();
		TerrorGameRules()->AddRoundAccumulatedTime( TheDirector->GetDirectorOptionFloat( "ScavengeRoundInitialTime", scavenge_round_initial_time.GetFloat() ) );
	}
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( player && player->edict() && player->entindex() && player->IsPlayer() && player->IsConnected() ) player->GetMusic()->OnScavengeRoundStart();
	}
	IGameEvent *event = gameeventmanager->CreateEvent( "scavenge_round_start" );
	if ( event )
	{
		event->SetInt( "round", TerrorGameRules()->GetScavengeRoundNumber() );
		event->SetBool( "firsthalf", !TerrorGameRules()->IsScavengeSecondHalf() );
		gameeventmanager->FireEvent( event );
	}
	CBaseEntity *director = gEntList.FindEntityByClassname( NULL, "info_director" );
	if ( director ) director->FireNamedOutput( "OnScavengeRoundStart", variant_t(), NULL, NULL );
}
void CDirectorScavengeMode::OnGasCanPoured( int team )
{
	if ( !g_TerrorModes.IsBaseMode( "scavenge" ) ) return;
	TheDirector->m_nScavengeItemsRemaining = (int)( (unsigned int)TheDirector->m_nScavengeItemsRemaining - 1u );
	TerrorGameRules()->SetScavengeItemsRemaining( TheDirector->m_nScavengeItemsRemaining );
	m_flLastPourTime = gpGlobals->curtime;
	if ( TerrorGameRules() )
	{
		float bonus = TheDirector->GetDirectorOptionFloat( "ScavengeScoreBonusTime", scavenge_score_bonus_time.GetFloat() );
		const double remaining = TerrorGameRules()->GetRoundTimeRemaining();
		if ( _mm_movemask_pd( _mm_cmpgt_sd( _mm_set_sd( remaining ), _mm_setzero_pd() ) ) & 1 )
			TerrorGameRules()->AddRoundAccumulatedTime( bonus );
		else
		{
			TerrorGameRules()->ResetRoundAccumulatedTime();
			TerrorGameRules()->AddRoundAccumulatedTime( (float)( TerrorGameRules()->GetTeamRoundDuration( TEAM_SURVIVOR ) + (double)bonus ) );
		}
		m_nIntensity = 0;
	}
	if ( TerrorGameRules()->GetScavengeTeamScore( TEAM_SURVIVOR, -1 ) == TerrorGameRules()->GetScavengeTeamScore( TEAM_INFECTED, -1 ) )
	{
		IGameEvent *event = gameeventmanager->CreateEvent( "scavenge_score_tied" );
		if ( event ) gameeventmanager->FireEvent( event );
	}
}
void Script_ForceScavengeStart()
{
	if ( g_TerrorModes.IsBaseMode( "scavenge" ) && !TheDirector->GetScavengeMode()->HasStarted() )
		TheDirector->GetScavengeMode()->StartRound();
}

int CDirectorScavengeMode::GetReadyCountdownSeconds() const
{
	// Native285890: positive-only admission, ordered now>=deadline rejects.
	const float deadline = m_StartTimer.m_timestamp;
	if ( !( _mm_movemask_ps( _mm_cmpgt_ss( _mm_set_ss( deadline ), _mm_setzero_ps() ) ) & 1 ) ) return 0;
	const double now = gpGlobals->curtime;
	if ( _mm_movemask_pd( _mm_cmpge_sd( _mm_set_sd( now ), _mm_set_sd( deadline ) ) ) & 1 ) return 0;
	// __ftol2_sse's SSE2 path converts the double spill directly to signed32.
	return _mm_cvttsd_si32( _mm_set_sd( (double)deadline - (double)gpGlobals->curtime ) );
}
bool CDirectorScavengeMode::ShouldBlockStartAreaExit( CTerrorPlayer *player )
{
	// S:285F70. This is SCAVENGE, not the survival predicate. Admission's
	// byte+24 is authoritative; the round's m_bStarted is a different field.
	if ( g_TerrorModes.IsBaseMode( "scavenge" ) )
	{
		if ( TheDirector->GetSessionManager()->IsStartAreaExitBlocked() )
			return true;
		if ( GetReadyCountdownSeconds() > 0 )
		{
			// Do not Invalidate: this starts a zero-duration countdown,
			// consumed by UpdateReadyCountdown on the subsequent update.
			m_StartTimer.Start( 0.0f );
		}
	}
	return false;
}
void CDirectorScavengeMode::StartReadyCountdown()
{
	// S:2863A0. Restarting this timer is not starting the scavenge round.
	m_StartTimer.Start( scavenge_round_setup_time.GetFloat() );
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( player && player->edict() && player->entindex() && player->IsConnected() )
			player->ShowViewPortPanel( "ready_countdown", true );
	}
}
bool CDirectorScavengeMode::UpdateReadyCountdown()
{
	// S:2867B0. Retail's IsElapsed uses >=; SDK's timer helper uses >.
	const float deadline = m_StartTimer.m_timestamp;
	if ( !g_TerrorModes.IsBaseMode( "scavenge" ) ||
		!( _mm_movemask_ps( _mm_cmpgt_ss( _mm_set_ss( deadline ), _mm_setzero_ps() ) ) & 1 ) ||
		!( _mm_movemask_pd( _mm_cmpge_sd( _mm_set_sd( gpGlobals->curtime ), _mm_set_sd( deadline ) ) ) & 1 ) )
		return true;
	m_StartTimer.Invalidate();
	TheDirector->GetSessionManager()->StartReadyCountdownNow();
	if ( !TheDirector->HasAnySurvivorLeftSafeArea() )
		TheDirector->OnSurvivorLeftSafeArea( NULL );
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( player && player->edict() && player->entindex() && player->IsConnected() )
			player->ShowViewPortPanel( "ready_countdown", false );
	}
	StartRound();
	return false;
}
void CDirectorScavengeMode::OnGameplayStart()
{
	// S:285720: the native round-end reason+644 and role flag+1253, not
	// a mission-won boolean or a synthesized scavenge round number.
	if ( TheDirector->GetRoundEndReason() != 18 && g_TerrorModes.IsBaseMode( "scavenge" ) &&
		TerrorGameRules()->GetScavengeRoundNumber() == 1 && !TheDirector->IsInSecondHalfOfRound() )
	{
		for ( CBaseEntity *entity = gEntList.FindEntityByClassname( NULL, "info_gamemode" ); entity;
			entity = gEntList.FindEntityByClassname( entity, "info_gamemode" ) )
		{
			variant_t value;
			entity->AcceptInput( "FireScavengeMatchStart", NULL, NULL, value, 0 );
		}
	}
}

#include "l4d2/director/director_scavenge_mode_console_server_callbacks.inc"
