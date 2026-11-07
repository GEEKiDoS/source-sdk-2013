//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/director/director_item_manager.h"
#include "l4d2/director/director_versus_mode.h"
#include "l4d2/director/director_music_banks.h"
#include "l4d2/entities/terror_transition_entities.h"
#include "filesystem.h"
#include "eventlog.h"
#include <emmintrin.h>
#include "tier0/memdbgon.h"

extern ConVar director_min_start_players; // Existing admission owner, no second ConVar.
extern ConVar z_difficulty;
extern ConVar pain_pills_decay_rate; // Sole shared actor-health owner, never a parallel rate.
extern ConVar z_tank_health; // Actual existing tank_convars.cpp owner.
ConVar director_tank_min_interval( "director_tank_min_interval", "350", 0x00004004 );
ConVar director_tank_max_interval( "director_tank_max_interval", "500", 0x00004004 );
ConVar director_special_original_offer_length( "director_special_original_offer_length", "30", 0x00004004,
	"How long is given to the first player to accept the offer of a special zombie." );
ConVar z_discard_range( "z_discard_range", "2500", 0x00004004, "Discard Zombies farther away than this" );

namespace
{
	bool NativeLifecyclePositive( float value )
	{
		return ( _mm_movemask_ps( _mm_cmpgt_ss( _mm_set_ss( value ), _mm_setzero_ps() ) ) & 1 ) != 0;
	}
}

void CDirector::OnNewGameSession()
{
	// SDK GameShutdown ends the explicit offline session, not a chapter change.
	m_bOfflineSession = false;
	// Native262180. This is not a declaration-only phase or another world store.
	m_pSessionManager->OnMapStart();
	ClearTerrorTransitionEntities(); // Existing255330: weapons/spawns/props/restore/landmark, same actual owners.
	g_SavedTerrorPlayerStates.ClearTransitionRecords(); // Existing2B7890, first real transitioning registry count reset.
	m_bTransitionedToMap = false;
	g_bTerrorTransitionRestore = false;
	g_bTerrorRestartAfterDeath = false;
	if ( TerrorGameRules() )
	{
		TerrorGameRules()->ResetVersusRoundScores( true );
		if ( TerrorGameRules() )
		{
			TerrorGameRules()->ResetScavengeScores( true );
			TerrorGameRules()->ResetRoundDurations();
		}
	}
	m_pVersusMode->ResetCampaignScores( true );
	Reset();
	m_flMissionElapsedTime = 0.0f;
	m_MissionTimer.Start();
	m_flSavedMobInterval = 0.0f;
	m_flSavedSpecialInterval = 0.0f;
	m_nSavedTankControllerUserID = 0;
	m_originalMapName[0] = '\0';
	if ( !engine->IsDedicatedServer() )
	{
		KeyValues *config = new KeyValues( "ServerConfig" );
		config->LoadFromFile( filesystem, "ServerConfig.vdf", "MOD" ); // Native saves regardless of load result.
		config->SetString( z_difficulty.GetName(), z_difficulty.GetString() );
		config->SaveToFile( filesystem, "ServerConfig.vdf", "MOD" );
		config->deleteThis();
		m_pMusicBanks->OnMapStart(); // Native music396 virtual8/285150, ONLY in this listen-server branch.
	}
}

void CDirector::OnRoundEnd( bool saveRemainingTime )
{
	// Native26BBC0. Retain independent mission clock and raw remaining-time
	// saves; old four-line draft incorrectly called item OnLevelEnd here.
	g_bTerrorTransitionRestore = true;
	g_bTerrorRestartAfterDeath = !saveRemainingTime;
	const float elapsed = NativeLifecyclePositive( m_MissionTimer.m_timestamp ) ?
		(float)( (double)gpGlobals->curtime - (double)m_MissionTimer.m_timestamp ) : 99999.8984375f;
	m_flMissionElapsedTime = _mm_cvtss_f32( _mm_add_ss( _mm_set_ss( m_flMissionElapsedTime ), _mm_set_ss( elapsed ) ) );
	m_MissionTimer.Start();
	CUtlVector< CTerrorPlayer * > humans;
	CollectTerrorPlayers( humans, -1, 7, 1, 9 ); // All teams/all states/humans; not survivor-only or alive-only.
	director_min_start_players.SetValue( MAX( 1, humans.Count() ) );
	if ( saveRemainingTime )
	{
		if ( ShouldResetTeamsOnMissionStart() )
		{
			const float minimum = _mm_cvtss_f32( _mm_mul_ss( _mm_set_ss( director_tank_min_interval.GetFloat() ), _mm_set_ss( 0.25f ) ) );
			m_SpecialSpawnTimer.Start( RandomFloat( minimum, director_tank_max_interval.GetFloat() ) );
		}
		m_flSavedMobInterval = (float)( (double)m_MobSpawnTimer.m_timestamp - (double)gpGlobals->curtime );
		m_flSavedSpecialInterval = (float)( (double)m_SpecialSpawnTimer.m_timestamp - (double)gpGlobals->curtime );
		m_nSavedTankControllerUserID = 0;
		if ( m_SpecialZombieOfferPlayers.Count() != 0 )
		{
			CBaseEntity *entity = m_SpecialZombieOfferPlayers[0].Get();
			if ( entity && entity->GetTeamNumber() == TEAM_INFECTED )
				m_nSavedTankControllerUserID = engine->GetPlayerUserId( entity->edict() );
		}
	}
	m_pMusicBanks->OnRoundEnd( saveRemainingTime ); // Actual only child callback here, no invented item/world/reset calls.
}

void CDirector::ReacceptSavedTankOffer()
{
	// Native26E380's redundant virtual360 is IsPlayer. UTIL_PlayerByUserId
	// already excludes8048==PlayerDisconnected (not disconnect-in-progress).
	// The saved ID clears BEFORE no-Tank/vector admission, with no life/team test.
	if ( m_nSavedTankControllerUserID == 0 ) return;
	CTerrorPlayer *player = static_cast< CTerrorPlayer * >( UTIL_PlayerByUserId( m_nSavedTankControllerUserID ) );
	if ( !player || !player->IsPlayer() ) return;
	m_nSavedTankControllerUserID = 0;
	if ( m_nTankCount > 0 ) return;
	const EHANDLE handle = player;
	if ( m_SpecialZombieOfferPlayers.Find( handle ) != m_SpecialZombieOfferPlayers.InvalidIndex() ) return;
	const float offerLength = director_special_original_offer_length.GetFloat();
	m_SpecialSpawnTimer.Start( RandomFloat( director_tank_min_interval.GetFloat(), director_tank_max_interval.GetFloat() ) );
	char identity[256];
	static_cast< CEventLog * >( GameLogSystem() )->GetEntityIdentity( player, identity, sizeof( identity ) );
	const int seconds = _mm_cvttss_si32( _mm_set_ss( offerLength ) );
	UTIL_LogPrintf( "Offer for %s: %s re-accepted the offer after transition.  Spawning in %d seconds.\n",
		GetZombieClassName( 8 ), identity, seconds );
	m_SpecialZombieOfferPlayers.AddToTail( handle ); // Actual1024, no fake Tank or role-only transfer.
	m_SpecialZombieOfferTimer.Start( offerLength );
	player->EmitPrivateSound( "Event.NewAvailableZombie" ); // Existing2418D0, exact single-user sound owner.
}

void CDirector::UpdateTempHealthDecayRate()
{
	// Native266330 ->25F790. Rebase EFFECTIVE buffers at the OLD rate before
	// changing the real shared ConVar; no alive/team/bot filter is present.
	const float previous = pain_pills_decay_rate.GetFloat();
	const float desired = GetDirectorOptionFloat( "TempHealthDecayRate", previous );
	if ( desired != previous ) // Native UCOMISS/LAHF retains unordered-as-unequal.
	{
		for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
			if ( player && player->edict() && player->entindex() != 0 && player->IsPlayer() && player->IsConnected() )
			{
				const float effective = player->GetHealthBuffer(); // Native x87 ->float spill before3155F0.
				player->SetHealthBuffer( effective );
			}
		}
		pain_pills_decay_rate.SetValue( desired );
		const ConVarRef rate( "pain_pills_decay_rate" );
		TerrorGameRules()->SaveConvar( rate ); // Actual inherited Rules saved-name registry748, AFTER SetValue.
	}
}

double CDirector::GetTankHealth() const
{
	// Native2661C0: health option remains float; multiply WIDENED with the
	// actual float multiplier and return double, no integer/fresh-health cache.
	float multiplier = 1.0f;
	const int difficulty = GetDifficulty();
	if ( difficulty == 0 ) multiplier = 0.75f;
	else if ( (unsigned int)difficulty - 2u <= 1u ) multiplier = 2.0f;
	if ( GetModeDescription()->IsPlayerControlledZombies() ) multiplier = 1.5f;
	const float health = GetDirectorOptionFloat( "ZombieTankHealth", z_tank_health.GetFloat() );
	return (double)health * (double)multiplier;
}

void CDirector::OnPlayerSpawned( CTerrorPlayer *player )
{
	// Native262080 repeats the genuine team3 read, then requires class8.
	if ( player->GetTeamNumber() == TEAM_INFECTED && player->GetTeamNumber() == TEAM_INFECTED && player->GetZombieClass() == 8 )
	{
		m_SpecialSpawnTimer.Start( RandomFloat( director_tank_min_interval.GetFloat(), director_tank_max_interval.GetFloat() ) );
		for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			CTerrorPlayer *observer = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
			if ( observer && observer->edict() && observer->entindex() != 0 && observer->IsPlayer() && observer->IsConnected() )
				observer->OnTankSpawned( player ); // Native25F0C0 calls REAL3176F0, not a no-op event.
		}
	}
}

double CDirector::GetZombieDiscardRange() const
{
	return (double)GetDirectorOptionFloat( "ZombieDiscardRange", z_discard_range.GetFloat() ); // Native2FDDC0.
}

void CDirector::Init()
{
	// Native260550, reached only from one-time100ED800/DLLInit100F1580.
	if ( TerrorGameRules() )
	{
		TerrorGameRules()->ResetVersusRoundScores( true );
		if ( TerrorGameRules() )
		{
			TerrorGameRules()->ResetScavengeScores( true );
			TerrorGameRules()->ResetRoundDurations();
		}
	}
	m_pVersusMode->ResetCampaignScores( true );
	Reset();
	// Native intentionally shuffles AGAIN after virtual Reset's own draw.
	ResetThreatPriorities( 9 );
	ListenForGameEvent( "player_death" );
	ListenForGameEvent( "player_connect_full" );
	ListenForGameEvent( "round_start_post_nav" );
	ListenForGameEvent( "map_transition" );
	ListenForGameEvent( "player_disconnect" );
	ListenForGameEvent( "player_first_spawn" );
}
