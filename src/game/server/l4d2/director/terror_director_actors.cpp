//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/sound/terror_music.h"
#include "l4d2/nav/terror_nav_area.h"
#include "NextBot/NextBotManager.h"
#include <xmmintrin.h>
#include "tier0/memdbgon.h"

namespace
{
	// Native28FFB0/260260, the fixed true/true caller here allows controlled/
	// incapacitated survivors but excludes spawnattr8000. Actual players only.
	struct FindClosestAttackSurvivor
	{
		Vector origin;
		CBaseEntity *ignore;
		bool botsOnly;
		float rangeSquared;
		CTerrorPlayer *result;
		FindClosestAttackSurvivor( CBaseEntity *source, bool bots ) : origin( source->WorldSpaceCenter() ), ignore( source ), botsOnly( bots ), rangeSquared( 100000000.0f ), result( NULL ) {}
		bool operator()( CBasePlayer *base )
		{
			CTerrorPlayer *player = ToTerrorPlayer( base );
			if ( player == ignore || !player->IsAlive() || player->GetTeamNumber() != TEAM_SURVIVOR || ( botsOnly && !player->IsBot() ) ) return true;
			TerrorNavArea *area = ToTerrorNavArea( player->GetLastKnownArea() );
			if ( area && area->HasSpawnAttributes( 0x8000 ) ) return true;
			Vector offset = player->WorldSpaceCenter() - origin;
			float distanceSquared = ( offset.x * offset.x + offset.y * offset.y ) + offset.z * offset.z;
			if ( rangeSquared > distanceSquared ) { rangeSquared = distanceSquared; result = player; }
			return true;
		}
	};
	struct CommandInfectedToAttack
	{
		int count;
		CommandInfectedToAttack() : count( 0 ) {}
		bool operator()( INextBot *bot )
		{
			CBaseCombatCharacter *entity = bot->GetEntity();
			if ( entity->GetTeamNumber() != TEAM_INFECTED || !entity->IsAlive() ) return true;
			CTerrorPlayer *target = NULL;
			if ( IsInCommentaryMode() )
			{
				FindClosestAttackSurvivor findBot( entity, true ); ForEachPlayer( findBot );
				target = findBot.result;
				if ( target && !target->IsPlayer() ) target = NULL;
			}
			if ( !target )
			{
				FindClosestAttackSurvivor find( entity, false ); ForEachPlayer( find ); target = find.result;
				if ( !target || !target->IsPlayer() ) return true;
			}
			bot->OnCommandAttack( target ); // Actual responder/Action event, NOT attention-focus substitution.
			++count; return true;
		}
	};
}
void CDirector::CommandInfectedToAttackSurvivors()
{
	CommandInfectedToAttack command; TheNextBots().ForEachBot( command );
}
void CTerrorPlayer::OnSurvivalRoundStarted()
{
	m_bWasPresentAtSurvivalStart = true;
	GetMusic()->OnSurvivalStart();
}
void CTerrorPlayer::UpdateSurvivalRecord( float time )
{
	// S2FC6D3 COMISS old,input; JA retains old. Unordered selects INPUT.
	float best = m_flBestSurvivalTime;
	if ( !( _mm_movemask_ps( _mm_cmpgt_ss( _mm_set_ss( best ), _mm_set_ss( time ) ) ) & 1 ) ) best = time;
	m_flBestSurvivalTime = best;
	if ( g_pGameRules ) m_nSurvivalMedal = TerrorGameRules()->GetSurvivalMedalForTime( best );
}
void CTerrorPlayer::ResetVersusDamageStatistics()
{
	// Native317570, all three original CNetworkVar writes (last has no wire prop).
	m_checkpointSurvivorDamage = 0;
	m_missionSurvivorDamage = 0;
	m_totalSurvivorDamage = 0;
}
