//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/stats/terror_client_stats.h"
#include "l4d2/player/c_terrorplayer.h"
#include "l4d2/player/bots/c_survivor_bot.h"
#include "l4d2/player/c_terror_player_resource.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/gamerules/terror_mode.h"
#include "tier1/utlrbtree.h"
#include <limits.h>
#include "tier0/memdbgon.h"

CTerrorPlayerStats::CTerrorPlayerStats() : m_statsReady( false ) {}
void CTerrorPlayerStats::CapturePlayers( int scope, int team )
{
	m_players.RemoveAll();
	C_TerrorPlayerResource *resource = dynamic_cast< C_TerrorPlayerResource * >( g_PR );
	if ( !resource ) return; // 26DF40 retains the prior ready flag on this guard.
	RandomSeed( resource->GetSharedRandomSeed() ); // Raw received Int32 bits, not numeric float conversion.
	bool zero = false;
	if ( TerrorGameRules()->IsVersusVoteRestarting() && scope == 0 )
	{
		if ( m_versusCache.Count() ) { m_players = m_versusCache; m_statsReady = true; return; }
		zero = true;
	}
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		if ( !resource->IsConnected( i ) ) continue;
		C_TerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( !player || player->GetTeamNumber() == TEAM_SPECTATOR || ( team != -1 && player->GetTeamNumber() != team ) ) continue;
		if ( player->IsBot() && player->GetTeamNumber() == TEAM_INFECTED ) continue;
		C_SurvivorBot *bot = dynamic_cast< C_SurvivorBot * >( player );
		Player_t record;
		record.character = player->GetSurvivorCharacter(); record.bot = player->IsBot();
		record.controlledBot = false; record.local = false;
		record.alive = player->m_lifeState == LIFE_ALIVE && !player->m_isIncapacitated;
		record.firstManOut = player->m_bIsFirstManOut;
		if ( scope == 1 && team == TEAM_SURVIVOR && TerrorGameRules()->HasSacrificeEscapees() ) record.alive = TerrorGameRules()->IsSacrificeEscapee( i );
		Q_strncpy( record.name, player->GetPlayerName(), sizeof( record.name ) );
		player_info_t info;
		record.accountID = engine->GetPlayerInfo( i, &info ) ? info.friendsID : 0;
		record.local = C_BasePlayer::GetLocalPlayer() == player; // Recorded single split-screen slot adaptation.
		C_TerrorPlayer *statisticsPlayer = player;
		if ( bot )
		{
			C_TerrorPlayer *human = ToTerrorPlayer( UTIL_PlayerByIndex( engine->GetPlayerForUserID( bot->GetHumanSpectatorUserID() ) ) );
			if ( human ) { record.controlledBot = true; statisticsPlayer = human; }
		}
		for ( int stat = 0; stat <= 56; ++stat )
		{
			const int statTeam = stat >= 34 && stat <= 56 ? TEAM_INFECTED : TEAM_SURVIVOR;
			if ( statisticsPlayer->IsBot() && statisticsPlayer->GetTeamNumber() != statTeam ) record.statistics[stat] = INT_MIN;
			else record.statistics[stat] = zero ? 0 : statisticsPlayer->GetOuttroStatistic( stat, scope );
		}
		m_players.AddToTail( record );
	}
	m_versusCache.RemoveAll();
	if ( g_TerrorModes.IsBaseMode( "versus" ) && scope == 0 ) m_versusCache = m_players;
	m_statsReady = true;
}
bool CTerrorPlayerStats::RankAscending( const Rank_t &a, const Rank_t &b ) { return a.value < b.value; }
bool CTerrorPlayerStats::RankDescending( const Rank_t &a, const Rank_t &b ) { return a.value >= b.value; } // Native NON-STRICT comparator, including ties.
bool CTerrorPlayerStats::RankPlayers( int statistic, RankResult_t &ranked )
{
	if ( !m_statsReady || !m_players.Count() || statistic > 56 ) return false;
	CUtlVector< int > order;
	FOR_EACH_VEC( m_players, i ) { if ( RandomInt( 0, 100 ) >= 50 ) order.AddToHead( i ); else order.AddToTail( i ); }
	const bool descending = statistic != 0 && statistic != 4 && statistic != 5 && statistic != 12;
	CUtlRBTree< Rank_t, unsigned short > tree( 0, 0, descending ? RankDescending : RankAscending );
	FOR_EACH_VEC( order, i )
	{
		Rank_t rank = { m_players[order[i]].statistics[statistic], order[i] };
		if ( rank.value != INT_MIN ) tree.Insert( rank );
	}
	ranked.count = tree.Count();
	int output = 0;
	for ( unsigned short index = tree.FirstInorder(); index != tree.InvalidIndex() && output < 8; index = tree.NextInorder( index ) )
	{
		ranked.players[output].player = m_players[tree[index].player];
		ranked.players[output].value = tree[index].value;
		++output;
	}
	return true;
}
bool CTerrorPlayerStats::HasAnyPositive( int statistic ) const
{
	if ( !m_statsReady ) return false;
	FOR_EACH_VEC( m_players, i ) if ( m_players[i].statistics[statistic] > 0 ) return true;
	return false;
}
int CTerrorPlayerStats::SumStatistic( int statistic ) const
{
	if ( !m_statsReady ) return 0;
	unsigned int sum = 0; // Native ADD wraps; no C++ signed-overflow assumption.
	FOR_EACH_VEC( m_players, i ) sum += (unsigned int)m_players[i].statistics[statistic];
	return (int)sum;
}
