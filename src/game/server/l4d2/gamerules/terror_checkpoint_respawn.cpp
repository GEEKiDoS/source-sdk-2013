//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/gamerules/terror_checkpoint_respawn.h"
#include "l4d2/nav/terror_nav_world.h"
#include "l4d2/nav/terror_nav_area.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "tier0/memdbgon.h"

static CTerrorCheckpoint *s_spawnCheckpoint;
extern ConVar z_survivor_respawn_health;
CTerrorCheckpoint *GetTerrorCheckpointSpawnOverride() { return s_spawnCheckpoint; }
void ResetCheckpointSpawnOverride() { s_spawnCheckpoint = NULL; }

namespace
{
int CountLivingSurvivors()
{
	int count = 0;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( player && player->edict() && player->IsConnected() && player->IsAlive() && player->GetTeamNumber() == TEAM_SURVIVOR ) ++count;
	}
	return count;
}
}
void CTerrorCheckpoint::RespawnPlayers()
{
	if ( m_hasRespawnedPlayers ) return;
	m_hasRespawnedPlayers = true;
	DevMsg( "Respawning players at a checkpoint - %d living survivors before\n", CountLivingSurvivors() );
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = static_cast< CTerrorPlayer * >( UTIL_PlayerByIndex( i ) );
		if ( player && player->edict() && player->entindex() && player->IsPlayer() && player->IsConnected() &&
			player->GetTeamNumber() == TEAM_SURVIVOR && !player->IsAlive() ) player->CheckpointRespawn( this );
	}
	DevMsg( "%d living survivors after\n", CountLivingSurvivors() );
	TheDirector->UpdateCheckpointHumanSurvivorCount(); // S259450 -> S25C870, not S25DFA0.
}

// S25C870 writes the same Director+648 count without the separate S25DFA0
// alive-only filter and debug side effects.
void CDirector::UpdateCheckpointHumanSurvivorCount()
{
	m_nHumanSurvivorCount = CountPlayersOnTeam( TEAM_SURVIVOR, false );
}

void CTerrorPlayer::CheckpointRespawn( CTerrorCheckpoint *checkpoint )
{
	DevMsg( "CheckpointRespawn for %s\n", GetPlayerName() );
	s_spawnCheckpoint = checkpoint;
	State_Transition( STATE_ACTIVE );
	Spawn();
	TerrorNavArea *area = ToTerrorNavArea( GetLastKnownArea() );
	if ( !area || !area->HasSpawnAttributes( TERROR_NAV_PLAYER_START ) )
	{
		SetHealth( TheDirector->IsInTransition() && g_bTerrorRestartAfterDeath ? GetMaxHealth() : z_survivor_respawn_health.GetInt() );
		ApplyTempHealthOnlyMutation();
	}
	s_spawnCheckpoint = NULL;
	if ( !GetActiveWeapon() ) GiveDefaultItems();
	if ( GetTeamNumber() == TEAM_SURVIVOR && TerrorGameRules()->IsHealingGnomeMutation() )
	{
		GiveHealthBuffer( 100.0f, float( GetHealth() ) );
		SetReviveCount( 0 );
	}
}
