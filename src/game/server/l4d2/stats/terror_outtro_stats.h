//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_OUTTRO_STATS_H
#define TERROR_OUTTRO_STATS_H
#ifdef _WIN32
#pragma once
#endif
#include "baseentity.h"
#include "entityoutput.h"
#include "util_shared.h"

// Native COuttroStats, NOT a replacement HUD/session host. Its five datamap
// bindings and CBaseEntity factory are the actual env_outtro_stats surface.
class COuttroStats : public CBaseEntity
{
public:
	DECLARE_CLASS( COuttroStats, CBaseEntity );
	DECLARE_DATADESC();
	COuttroStats();
	void InputPZEndGame( inputdata_t &data );
	void InputRollStatsCrawl( inputdata_t &data );
	void InputRollCredits( inputdata_t &data );
	void InputSkipStateChanged( inputdata_t &data );
private:
	struct VoteStats
	{
		int players[2], yes[2], no[2], finished[2];
	};
	struct CachedVote
	{
		unsigned char team, vote;
	};
	void PZEndGameThink();
	void StatsCrawlThink();
	void CollectVoteStats( VoteStats &stats );
	static void CollectSkipState( int &skips, int &players );
	static void SendVoteStats( const VoteStats &stats );
	COutputEvent m_OnOuttroStatsDone; // native1068, exposed original output
	CountdownTimer m_votePeriod; // native1092
	CountdownTimer m_postPeriod; // native1104
	CachedVote m_cachedVotes[33]; // native1116; slot0 unused, original32-player bound
	int m_nVoteOutcome; // native1184:0 voting,1 rematch,2 exit
};
#endif // TERROR_OUTTRO_STATS_H
