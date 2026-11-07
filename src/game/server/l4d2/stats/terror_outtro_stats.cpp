//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/stats/terror_outtro_stats.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/sound/terror_music.h"
#include "vote_controller.h"
#include "recipientfilter.h"
#include "tier0/memdbgon.h"

LINK_ENTITY_TO_CLASS( env_outtro_stats, COuttroStats );
BEGIN_DATADESC( COuttroStats )
	DEFINE_INPUTFUNC( FIELD_VOID, "PZEndGame", InputPZEndGame ),
	DEFINE_INPUTFUNC( FIELD_VOID, "RollStatsCrawl", InputRollStatsCrawl ),
	DEFINE_INPUTFUNC( FIELD_VOID, "RollCredits", InputRollCredits ),
	DEFINE_INPUTFUNC( FIELD_VOID, "SkipStateChanged", InputSkipStateChanged ),
	DEFINE_OUTPUT( m_OnOuttroStatsDone, "OnOuttroStatsDone" ),
END_DATADESC()

static ConVar sv_pz_endgame_vote_period( "sv_pz_endgame_vote_period", "12", 0x00000006, "How long players get to vote to play again in a game with player controlled zombies." );
static ConVar sv_pz_endgame_vote_post_period( "sv_pz_endgame_vote_post_period", "5", 0x00000006, "Post period after end game vote." );

namespace
{
	CTerrorPlayer *OutroPlayer( int index )
	{
		CBasePlayer *base = UTIL_PlayerByIndex( index );
		// C6390/C65E0 do not add the Director foreach's separate edict guard.
		if ( !base || !base->IsPlayer() || !base->IsConnected() || base->IsBot() ) return NULL;
		return static_cast< CTerrorPlayer * >( base );
	}
	void SendOutroInput( CBaseEntity *stats, const char *input )
	{
		variant_t empty;
		stats->AcceptInput( input, NULL, NULL, empty, 0 );
	}
	void TestOuttroStats( const CCommand & )
	{
		// C68C0 intentionally does NOT create a missing entity.
		CBaseEntity *stats = gEntList.FindEntityByClassname( NULL, "env_outtro_stats" );
		if ( stats ) SendOutroInput( stats, "RollStatsCrawl" );
	}
	void TestOuttroPZEndGame( const CCommand & )
	{
		CBaseEntity *stats = gEntList.FindEntityByClassname( NULL, "env_outtro_stats" );
		if ( !stats ) stats = CBaseEntity::Create( "env_outtro_stats", vec3_origin, vec3_angle );
		if ( stats ) SendOutroInput( stats, "PZEndGame" );
	}
	struct FinishMissionMusic
	{
		bool operator()( CBasePlayer *base )
		{
			ToTerrorPlayer( base )->GetMusic()->OnMissionWonDone();
			return true;
		}
	};
	void OuttroStatsDone( const CCommand & )
	{
		// S261830 ->25EC10 is a music callback then the REAL628 exit clock.
		const char *map = STRING( gpGlobals->mapname );
		if ( !Q_stricmp( map, "credits" ) || ( ( TerrorGameRules()->IsCoopMode() || TerrorGameRules()->IsRealismMode() ) && TheDirector->IsSpeechDisabled() ) )
		{
			FinishMissionMusic action;
			ForEachPlayer( action );
			TheDirector->StartScenarioExit();
		}
	}
	ConCommand test_outtro_stats( "test_outtro_stats", TestOuttroStats, "", 0x00004004 );
	ConCommand test_outtro_pzendgame( "test_outtro_pzendgame", TestOuttroPZEndGame, "", 0x00004004 );
	ConCommand outtro_stats_done( "outtro_stats_done", OuttroStatsDone, "Fire an output when the outtro stats are finished", 0x00000014 );
}

COuttroStats::COuttroStats()
{
	// C6F70 ->100514D0 ->10001060 allocates1188 then MEMSET0 before C69D0.
	// Timers/output use their real constructors; these plain members retain
	// the original allocator state rather than an invented rematch default.
	Q_memset( m_cachedVotes, 0, sizeof( m_cachedVotes ) );
	m_nVoteOutcome = 0;
}
void COuttroStats::InputRollCredits( inputdata_t & )
{
	if ( TerrorGameRules()->IsVersusMode() ) return;
	CReliableBroadcastRecipientFilter filter;
	UserMessageBegin( filter, "creditsMsg" );
	MessageEnd();
}
void COuttroStats::InputRollStatsCrawl( inputdata_t & )
{
	if ( TerrorGameRules()->IsVersusMode() ) return;
	CReliableBroadcastRecipientFilter filter;
	UserMessageBegin( filter, "StatsCrawlMsg" );
	MessageEnd();
	SetThink( &COuttroStats::StatsCrawlThink );
	SetNextThink( gpGlobals->curtime + 1.0f );
	TheDirector->m_bSpeechDisabled = true; // original C6AB0 TRUE writer657
}
void COuttroStats::CollectSkipState( int &skips, int &players )
{
	skips = players = 0;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = OutroPlayer( i );
		if ( !player ) continue;
		int team = player->GetTeamNumber();
		if ( team != TEAM_SURVIVOR && team != TEAM_INFECTED && team != TEAM_SPECTATOR ) continue;
		++players;
		if ( player->HasSkippedOuttro() ) ++skips;
	}
}
void COuttroStats::StatsCrawlThink()
{
	int skips, players;
	CollectSkipState( skips, players );
	if ( skips < players ) SetNextThink( gpGlobals->curtime + 1.0f );
	else TheDirector->StartScenarioExit();
}
void COuttroStats::InputSkipStateChanged( inputdata_t & )
{
	if ( TerrorGameRules()->IsPlayerControlledZombies() )
	{
		VoteStats stats;
		CollectVoteStats( stats );
		SendVoteStats( stats );
	}
	else
	{
		int skips, players;
		CollectSkipState( skips, players );
		DevMsg( "COuttroStats: Skip state changed. %d players, %d skips\n", players, skips );
		if ( players > 1 )
		{
			CReliableBroadcastRecipientFilter filter;
			UserMessageBegin( filter, "StatsSkipState" );
				WRITE_BYTE( skips );
				WRITE_BYTE( players );
			MessageEnd();
		}
	}
}
void COuttroStats::InputPZEndGame( inputdata_t & )
{
	if ( !TerrorGameRules()->IsPlayerControlledZombies() ) return;
	// Actual nullable native vote-controller guard; no fake session/Match host.
	if ( g_voteControllerGlobal )
	{
		g_voteControllerGlobal->CancelVote();
		g_voteControllerGlobal->HoldVotes();
	}
	float duration = sv_pz_endgame_vote_period.GetFloat();
	m_votePeriod.m_timestamp = (float)( (double)gpGlobals->curtime + (double)duration );
	m_votePeriod.m_duration = duration;
	Q_memset( m_cachedVotes, 0, sizeof( m_cachedVotes ) );
	m_nVoteOutcome = 0;
	CReliableBroadcastRecipientFilter filter;
	UserMessageBegin( filter, "PZEndGamePanelMsg" );
		WRITE_BYTE( 1 );
	MessageEnd();
	SetThink( &COuttroStats::PZEndGameThink );
	SetNextThink( gpGlobals->curtime + 1.0f );
	TheDirector->m_bSpeechDisabled = true; // original C6DD0 TRUE writer657
	VoteStats stats;
	CollectVoteStats( stats );
	SendVoteStats( stats );
}
void COuttroStats::CollectVoteStats( VoteStats &stats )
{
	Q_memset( &stats, 0, sizeof( stats ) );
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = OutroPlayer( i );
		if ( !player ) continue;
		int actualTeam = player->GetTeamNumber();
		if ( actualTeam != TEAM_SURVIVOR && actualTeam != TEAM_INFECTED ) continue;
		int team = actualTeam != TEAM_SURVIVOR;
		++stats.players[team];
		CachedVote &cached = m_cachedVotes[i];
		// Native slot cache is deliberately NOT a SteamID map. Same-team reuse
		// may retain a disconnected actor's vote; do not silently repair it.
		if ( ( cached.vote == 0 && player->GetRematchVote() != 0 ) || ( cached.vote != 0 && cached.team != team ) )
		{
			cached.vote = (unsigned char)player->GetRematchVote();
			cached.team = (unsigned char)team;
		}
		if ( cached.vote == 1 ) ++stats.yes[cached.team];
		if ( cached.vote == 2 ) ++stats.no[cached.team];
	}
	bool bothFinished = false;
	for ( int team = 0; team < 2; ++team )
	{
		if ( (double)gpGlobals->curtime >= (double)m_votePeriod.m_timestamp || stats.no[team] + stats.yes[team] == stats.players[team] )
		{
			stats.finished[team] = 1;
			// Native FCOMIP/JNB skips only ordered >=: unordered enters this arm.
			if ( !( (double)gpGlobals->curtime >= (double)m_votePeriod.m_timestamp ) && stats.no[team] >= stats.yes[team] ) bothFinished = true;
		}
	}
	if ( bothFinished ) stats.finished[0] = stats.finished[1] = 1;
	Msg( ">>>REMATCH Stats START\n" );
	for ( int team = 0; team < 2; ++team )
	{
		Msg( "Team %d: %s\n", team, stats.finished[team] ? "Finished" : "Still voting" );
		Msg( "    %d players\n", stats.players[team] );
		Msg( "    %d yes\n", stats.yes[team] );
		Msg( "    %d no\n", stats.no[team] );
	}
	Msg( ">>>>REMATCH Stats END\n" );
}
void COuttroStats::SendVoteStats( const VoteStats &stats )
{
	CReliableBroadcastRecipientFilter filter;
	UserMessageBegin( filter, "PZEndGameVoteStatsMsg" );
	for ( int team = 0; team < 2; ++team )
	{
		WRITE_BYTE( stats.players[team] );
		WRITE_BYTE( stats.yes[team] );
		WRITE_BYTE( stats.no[team] );
		WRITE_BYTE( stats.finished[team] );
	}
	MessageEnd();
}
void COuttroStats::PZEndGameThink()
{
	VoteStats stats;
	CollectVoteStats( stats );
	// Native FCOMIP/JNB: unordered acts like the not-yet-expired arm.
	if ( !( (double)gpGlobals->curtime >= (double)m_votePeriod.m_timestamp ) && ( !stats.finished[0] || !stats.finished[1] ) )
	{
		SetNextThink( gpGlobals->curtime + 1.0f );
		return;
	}
	if ( m_nVoteOutcome == 0 )
	{
		m_nVoteOutcome = ( stats.players[0] && stats.yes[0] <= stats.no[0] ) || ( stats.players[1] && stats.yes[1] <= stats.no[1] ) ? 2 : 1;
		float duration = sv_pz_endgame_vote_post_period.GetFloat();
		m_postPeriod.m_timestamp = (float)( (double)gpGlobals->curtime + (double)duration );
		m_postPeriod.m_duration = duration;
		SendVoteStats( stats );
	}
	if ( !( (double)gpGlobals->curtime >= (double)m_postPeriod.m_timestamp ) )
	{
		SetNextThink( gpGlobals->curtime + 1.0f );
		return;
	}
	if ( g_voteControllerGlobal ) g_voteControllerGlobal->ReleaseVotes();
	TheDirector->HideScoreboards(); // S25C570; NOT a1255 mission-blocked clear.
	CReliableBroadcastRecipientFilter filter;
	UserMessageBegin( filter, "PZEndGamePanelMsg" );
		WRITE_BYTE( 0 );
	MessageEnd();
	if ( m_nVoteOutcome == 1 ) TheDirector->RestartCampaign();
	else TheDirector->FinishScenarioExit();
}
