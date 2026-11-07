//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_CLIENT_STATS_H
#define TERROR_CLIENT_STATS_H
#ifdef _WIN32
#pragma once
#endif
#include "tier1/utlvector.h"
// Original plain embedded collector, 26DE00/26DF40/26D950/26D990/26E2F0.
// Each native consumer has its own collector/cache; all share this one body.
class CTerrorPlayerStats
{
public:
	struct PlayerIdentity_t
	{
		int character;
		bool local, bot, controlledBot, alive, firstManOut;
		char name[32];
		unsigned int accountID; // Native player_info friendsID, not server userID.
	};
	struct Player_t : PlayerIdentity_t { int statistics[57]; };
	struct RankedPlayer_t { PlayerIdentity_t player; int value; };
	struct RankResult_t { RankedPlayer_t players[8]; int count; };
	CTerrorPlayerStats();
	void CapturePlayers( int scope, int team );
	bool RankPlayers( int statistic, RankResult_t &ranked );
	bool HasAnyPositive( int statistic ) const;
	int SumStatistic( int statistic ) const;
	const CUtlVector< Player_t > &GetPlayers() const { return m_players; }
private:
	struct Rank_t { int value, player; };
	static bool RankAscending( const Rank_t &a, const Rank_t &b );
	static bool RankDescending( const Rank_t &a, const Rank_t &b );
	CUtlVector< Player_t > m_players;
	CUtlVector< Player_t > m_versusCache;
	bool m_statsReady;
};
#endif
