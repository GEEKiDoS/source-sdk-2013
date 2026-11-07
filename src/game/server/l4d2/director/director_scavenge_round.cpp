//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/director/director_scavenge_mode.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/sound/terror_music.h"
#include "l4d2/weapons/weapon_csbase.h"
#include "entitylist.h"
#include "NextBot/NextBotManager.h"
#include <emmintrin.h>
#include "tier0/memdbgon.h"

extern ConVar scavenge_round_initial_time; // Sole original convar, defined with the existing mode owner.

static ConVar scavenge_overtime_grace_time( "scavenge_overtime_grace_time", "1", 0x00004006,
	"How long survivors can go without a scavenge item before overtime will end." );

namespace
{
	CTerrorPlayer *ScavengePlayer( int index )
	{
		CTerrorPlayer *player = static_cast< CTerrorPlayer * >( UTIL_PlayerByIndex( index ) );
		// Native8048==2 is PlayerDisconnected, NOT CCS picking-team/observer.
		return player && player->edict() && player->entindex() != 0 && player->IsPlayer() && player->IsConnected() ? player : NULL;
	}
	bool NativeScavengeGreater( double first, double second )
	{
		return ( _mm_movemask_pd( _mm_cmpgt_sd( _mm_set_sd( first ), _mm_set_sd( second ) ) ) & 1 ) != 0;
	}
	void ScavengeOutput( const char *name )
	{
		CBaseEntity *director = gEntList.FindEntityByClassname( NULL, "info_director" );
		if ( director ) director->FireNamedOutput( name, variant_t(), NULL, NULL );
	}
	struct FreezeScavengeBot
	{
		bool operator()( INextBot *bot )
		{
			bot->GetEntity()->AddFlag( FL_FROZEN ); // Native104AC8E0(32)->104AB550.
			return true;
		}
	};
}

bool CDirectorScavengeMode::AreNoScavengeItemsHeld()
{
	// Native285C30->2856D0 requires LIFE_ALIVE before querying weapon ID16.
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ScavengePlayer( i );
		if ( !player || player->GetTeamNumber() != TEAM_SURVIVOR || !player->IsAlive() ) continue;
		CWeaponCSBase *weapon = static_cast< CWeaponCSBase * >( player->GetActiveWeapon() );
		if ( weapon && weapon->GetWeaponID() == 16 ) return false;
	}
	return true;
}

void CDirectorScavengeMode::UpdateTimeLevel()
{
	// Native285FE0 calls ceil THEN CVTTSD2SI; no repair for NaN/overflow.
	const int seconds = _mm_cvttsd_si32( _mm_set_sd( ceil( TerrorGameRules()->GetRoundTimeRemaining() ) ) );
	static const int limits[] = { 0, 5, 10, 15, 20, 25, 30, 45, 60, 90, 600 };
	if ( m_nIntensity == 0 ) m_nIntensity = ARRAYSIZE( limits );
	for ( int level = m_nIntensity - 1; level > 0; --level )
	{
		if ( seconds > limits[level] || seconds <= limits[level - 1] ) continue;
		m_nIntensity = level;
		for ( int i = 1; i <= gpGlobals->maxClients; ++i )
			if ( CTerrorPlayer *player = ScavengePlayer( i ) ) player->GetMusic()->OnScavengeLevelChange( level );
		CBaseEntity *director = gEntList.FindEntityByClassname( NULL, "info_director" );
		if ( director )
		{
			variant_t value;
			value.SetInt( level );
			director->FireNamedOutput( "OnScavengeIntensityChanged", value, NULL, NULL );
		}
		return;
	}
}

void CDirectorScavengeMode::StartOvertime()
{
	// Native286090 writes BOTH11/12 before event/music/output, no timer start.
	m_bInOvertime = true;
	m_bWasInOvertime = true;
	IGameEvent *event = gameeventmanager->CreateEvent( "begin_scavenge_overtime" );
	if ( event ) gameeventmanager->FireEvent( event );
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		if ( CTerrorPlayer *player = ScavengePlayer( i ) ) player->GetMusic()->OnScavengeOvertimeStart();
	ScavengeOutput( "OnScavengeOvertimeStart" );
}

void CDirectorScavengeMode::EndOvertime( bool timeAdded )
{
	// Native286110 retains12, invalidates real28 countdown, no invented event.
	m_bInOvertime = false;
	m_OvertimeGraceTimer.Invalidate();
	if ( timeAdded )
	{
		for ( int i = 1; i <= gpGlobals->maxClients; ++i )
			if ( CTerrorPlayer *player = ScavengePlayer( i ) ) player->GetMusic()->OnScavengeOvertimeTimeAdded();
		ScavengeOutput( "OnScavengeOvertimeCancel" );
	}
	else ScavengeOutput( "OnScavengeTimerExpired" );
}

bool CDirectorScavengeMode::UpdateOvertime()
{
	// Native2868F0: losing the held item begins the grace; reacquisition invalidates.
	bool expired = AreNoScavengeItemsHeld();
	if ( expired )
	{
		if ( !NativeScavengeGreater( m_OvertimeGraceTimer.m_timestamp, 0.0 ) )
		{
			const float duration = scavenge_overtime_grace_time.GetFloat();
			m_OvertimeGraceTimer.Start( duration );
		}
		const bool elapsed = ( _mm_movemask_pd( _mm_cmpge_sd( _mm_set_sd( gpGlobals->curtime ),
			_mm_set_sd( m_OvertimeGraceTimer.m_timestamp ) ) ) & 1 ) != 0;
		if ( elapsed )
		{
			EndOvertime( false );
			return true;
		}
		expired = false;
	}
	else m_OvertimeGraceTimer.Invalidate();
	if ( NativeScavengeGreater( TerrorGameRules()->GetRoundTimeRemaining(), 0.0 ) ) EndOvertime( true );
	return expired;
}

bool CDirectorScavengeMode::BeginOvertime()
{
	// Native2869A0 returns true immediately with no item and fires expired output.
	if ( AreNoScavengeItemsHeld() )
	{
		ScavengeOutput( "OnScavengeTimerExpired" );
		return true;
	}
	StartOvertime();
	return false;
}

void CDirectorScavengeMode::CheckMissionStatus()
{
	// Native286B80, real Frame2741C0/26D5F0 caller. No pending round-end guard here.
	if ( !g_TerrorModes.IsBaseMode( "scavenge" ) || !m_bStarted ) return;
	UpdateTimeLevel();
	bool expired = false;
	if ( m_bInOvertime ) expired = UpdateOvertime();
	else if ( !NativeScavengeGreater( TerrorGameRules()->GetRoundTimeRemaining(), 0.0 ) ) expired = BeginOvertime();
	const int survivor = TerrorGameRules()->GetScavengeTeamScore( TEAM_SURVIVOR, -1 );
	const int infected = TerrorGameRules()->GetScavengeTeamScore( TEAM_INFECTED, -1 );
	if ( ( TheDirector->m_nScavengeItemsRemaining > 0 || ( survivor == 0 && infected == 0 ) ) && !expired )
	{
		if ( !TheDirector->IsInSecondHalfOfRound() ) return;
		if ( survivor <= infected )
		{
			if ( survivor == infected && infected != 0 )
			{
				if ( !m_bApproachingRoundWin )
				{
					m_bApproachingRoundWin = true;
					for ( int i = 1; i <= gpGlobals->maxClients; ++i )
						if ( CTerrorPlayer *player = ScavengePlayer( i ) ) player->GetMusic()->OnApproachingScavengeRoundWin();
				}
			}
			else m_bApproachingRoundWin = false;
			return;
		}
	}
	TheDirector->EndScenario(); // Actual269200 ordered child arbitration, not EndRound(reason).
}

bool CDirectorScavengeMode::HandleRoundEnd()
{
	if ( !g_TerrorModes.IsBaseMode( "scavenge" ) ) return false; // Native286F50.
	FinishRound();
	return true; // Even when native286C60's started8 guard did not enter.
}

void CDirectorScavengeMode::FinishRound()
{
	// Native286C60 uses physical-role indices and preserves its score/time tie bugs.
	if ( !m_bStarted ) return;
	m_bStarted = false;
	CTerrorGameRules *rules = TerrorGameRules();
	rules->SetRoundEndTime( gpGlobals->curtime );
	const int survivorIndex = rules->GetScavengeTeamIndex( TEAM_SURVIVOR );
	const int infectedIndex = rules->GetScavengeTeamIndex( TEAM_INFECTED );
	rules->SetScavengeTeamRoundTime( survivorIndex, (float)rules->GetRoundTimeRemaining() );
	rules->GetTeamRoundDuration( TEAM_SURVIVOR ); // Original reached query is retained before the pour-time branch.
	if ( NativeScavengeGreater( m_flLastPourTime, 0.0 ) )
		rules->SetTeamRoundDuration( TEAM_SURVIVOR, (float)( (double)m_flLastPourTime - rules->GetRoundStartTime() ) );
	else if ( !NativeScavengeGreater( rules->GetRoundTimeRemaining(), 0.0 ) && !m_bWasInOvertime )
		rules->SetTeamRoundDuration( TEAM_SURVIVOR, TheDirector->GetDirectorOptionFloat( "ScavengeRoundInitialTime", scavenge_round_initial_time.GetFloat() ) );

	int reason = 9;
	const char *eventName = "scavenge_round_halftime";
	bool matchFinished = false;
	int survivorMatchScore = 0, infectedMatchScore = 0;
	if ( TheDirector->IsInSecondHalfOfRound() )
	{
		int winner = 0;
		const int first = rules->GetScavengeScore( true, -1 );
		const int second = rules->GetScavengeScore( false, -1 );
		if ( first != second ) winner = first > second ? 1 : 2;
		else
		{
			const float survivorDuration = (float)rules->GetTeamRoundDuration( TEAM_SURVIVOR );
			const double infectedDurationValue = rules->GetTeamRoundDuration( TEAM_INFECTED );
			const float infectedDuration = (float)infectedDurationValue;
			if ( _mm_movemask_pd( _mm_cmpneq_sd( _mm_set_sd( survivorDuration ), _mm_set_sd( infectedDurationValue ) ) ) & 1 )
			{
				// Original COMISS/JA: unordered chooses infected, not survivor.
				const bool keepSurvivor = first != 0 ? NativeScavengeGreater( infectedDuration, survivorDuration ) :
					NativeScavengeGreater( survivorDuration, infectedDuration );
				winner = keepSurvivor ? survivorIndex : infectedIndex;
			}
			reason = 10;
		}
		if ( winner == 0 )
		{
			rules->IncrementScavengeMatchScore( 1 );
			rules->IncrementScavengeMatchScore( 2 );
		}
		else rules->IncrementScavengeMatchScore( winner );
		rules->SetScavengeWinningTeam( winner, -1 );
		survivorMatchScore = rules->GetScavengeMatchScore( TEAM_SURVIVOR );
		infectedMatchScore = rules->GetScavengeMatchScore( TEAM_INFECTED );
		const int limit = rules->GetScavengeRoundLimit();
		const int majority = limit / 2 + 1;
		const bool hasMoreRounds = (int)( (unsigned int)limit - (unsigned int)rules->GetScavengeRoundNumber() ) > 0;
		matchFinished = ( survivorMatchScore >= majority || infectedMatchScore >= majority ) &&
			( survivorMatchScore != infectedMatchScore || !hasMoreRounds );
		if ( matchFinished ) { reason = 12; eventName = "scavenge_match_finished"; }
		else eventName = "scavenge_round_finished";
	}

	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ScavengePlayer( i );
		if ( !player ) continue;
		if ( matchFinished ) player->OnScavengeMatchEnd();
		else if ( TheDirector->IsInSecondHalfOfRound() ) player->OnScavengeRoundEnd();
		else player->OnScavengeRoundHalftime();
	}
	IGameEvent *event = gameeventmanager->CreateEvent( eventName, matchFinished );
	if ( event )
	{
		if ( matchFinished ) event->SetInt( "winners", survivorMatchScore > infectedMatchScore ? survivorIndex :
			infectedMatchScore > survivorMatchScore ? infectedIndex : 0 );
		gameeventmanager->FireEvent( event );
	}
	FreezeScavengeBot freeze;
	TheNextBots().ForEachBot( freeze );
	TheDirector->m_bDelayMissionEndScoreboard = true; // Actual1252 writer.
	TheDirector->EndRound( reason );
}

void CDirectorScavengeMode::EndRound()
{
	// Native2868B0: first reason13 waits10, next callback exits; mission resets native0.
	if ( m_bScenarioExitPending ) TheDirector->FinishScenarioExit();
	else if ( TheDirector->GetRoundEndReason() == 13 )
	{
		TheDirector->SetMissionEndDelay( 10.0f );
		m_bScenarioExitPending = true;
	}
	else TheDirector->EndRound( 0 );
}

void CDirectorScavengeMode::ResetRound()
{
	TheDirector->StartEndGameStats(); // Actual285830 ->265200, NOT Reset285840.
}

void CDirectorScavengeMode::CancelReadyCountdown()
{
	if ( !g_TerrorModes.IsBaseMode( "scavenge" ) ) return; // Native286470.
	m_StartTimer.Invalidate();
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		if ( CTerrorPlayer *player = ScavengePlayer( i ) ) player->ShowViewPortPanel( "ready_countdown", false );
}
