//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/gamerules/terror_gamerules.h"
#ifndef CLIENT_DLL
#include "l4d2/director/terror_director.h"
#include "l4d2/director/director_versus_mode.h"
#include "l4d2/gamerules/terror_mode.h"
#include "l4d2/gamerules/terror_mission_info.h"
#endif
#include <emmintrin.h>
#include "tier0/memdbgon.h"

// Native client C27C9A0. Move the existing server-only ReadScriptedHUDTimer
// into the existing shared rules-provider TU, ONCE for both DLLs. Remove its
// server-only body; promote the declaration and change return type to double.
// Existing callers spill to their existing floats naturally; no new wire/state.
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
double CTerrorGameRules::ReadScriptedHUDTimer( int timer ) const
{
	const int mode = m_iScriptedHUDTimerMode[timer];
	if ( !mode ) return 0.0;
	if ( mode == 3 ) return m_fScriptedHUDTimerAdd[timer];
	float elapsed = gpGlobals->curtime - m_fScriptedHUDTimerBase[timer];
	if ( elapsed < 0.0f ) elapsed = 0.0f;
	const double value = m_fScriptedHUDTimerAdd[timer];
	return mode == 1 ? value + (double)elapsed : value - (double)elapsed;
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
bool CTerrorGameRules::IsRoundInProgress() const { return m_flRoundStartTime != 0.0f && m_flRoundEndTime == 0.0f; }
double CTerrorGameRules::GetRoundElapsedTime() const
{
	float elapsed = ( IsRoundInProgress() ? gpGlobals->curtime : (float)m_flRoundEndTime ) - m_flRoundStartTime;
	// Native2DBBC0 SUBSS then COMISS/JBE: float-rounded, unordered ->0.
	return elapsed > 0.0f ? elapsed : 0.0;
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
int CTerrorGameRules::GetScavengeItemsGoal() const { return m_nScavengeItemsGoal; }
int CTerrorGameRules::GetScavengeItemsRemaining() const { return m_nScavengeItemsRemaining; }
int CTerrorGameRules::GetScavengeRoundNumber() const { return m_nRoundNumber; }
bool CTerrorGameRules::IsScavengeSecondHalf() const { return m_bInSecondHalfOfRound; }
int CTerrorGameRules::GetScavengeRoundLimit() const { return m_nRoundLimit; } // Native2DBCD0/1112.
int CTerrorGameRules::GetVersusMaxCompletionScore() const { return m_nVersusMaxCompletionScore; } // Same plain native S3584/C2752.
int CTerrorGameRules::GetVersusDistancePerSurvivor( int character, int team ) const { return m_iVersusDistancePerSurvivor[character + 4 * ( GetScavengeTeamIndex( team ) - 1 )]; }
int CTerrorGameRules::GetVersusSurvivorDeathDistance( int character, int team ) const
{
	const int index = character + 4 * ( GetScavengeTeamIndex( team ) - 1 );
	// Only native out-of-array reads are skipped under the standing rule.
	return index >= 0 && index < m_iVersusSurvivorDeathDistance.Count() ? m_iVersusSurvivorDeathDistance[index] : 0;
}
int CTerrorGameRules::GetVersusCompletionScore( int team ) const
{
	unsigned int total = 0;
	for ( int character = 0; character < 4; ++character ) total += (unsigned int)GetVersusDistancePerSurvivor( character, team );
	return (int)total;
}
int CTerrorGameRules::GetVersusChapterDamage( int team ) const
{
	const unsigned int element = (unsigned int)GetScavengeTeamIndex( team ) - 1u;
	if ( element >= (unsigned int)m_iChapterDamage.Count() ) return 0;
	return m_iChapterDamage[element];
}
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
double CTerrorGameRules::GetRoundTimeRemaining() const
{
	const float elapsed = (float)GetRoundElapsedTime();
	const float remaining = (float)m_flAccumulatedTime - elapsed;
	return ( _mm_movemask_ps( _mm_cmpgt_ss( _mm_set_ss( remaining ), _mm_setzero_ps() ) ) & 1 ) ? remaining : 0.0;
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

int CTerrorGameRules::GetVersusScore( int team, bool campaign ) const
{
	if ( team <= 0 ) return 0;
	return campaign ? m_iCampaignScore[team - 1] : m_iSurvivorScore[team - 1];
}
#ifndef CLIENT_DLL
ConVar mp_roundlimit( "mp_roundlimit", "3", 
#if defined( GAME_DLL )
	0x00080004
#else
	0x00080000
#endif
, "Maximum number of rounds" ); // Actual ctor2E1ED8 source; native flags retained.
void CTerrorGameRules::SetRoundStartTime( float time )
{
	if ( _mm_movemask_ps( _mm_cmpneq_ss( _mm_set_ss( m_flRoundStartTime.Get() ), _mm_set_ss( time ) ) ) & 1 )
		m_flRoundStartTime.SetDirect( time );
}
void CTerrorGameRules::SetRoundEndTime( float time )
{
	if ( _mm_movemask_ps( _mm_cmpneq_ss( _mm_set_ss( m_flRoundEndTime.Get() ), _mm_set_ss( time ) ) ) & 1 )
		m_flRoundEndTime.SetDirect( time );
	m_flRoundDuration.Set( GetScavengeTeamIndex( TEAM_SURVIVOR ) - 1, m_flRoundEndTime.Get() - m_flRoundStartTime.Get() );
	float duration = GetTeamRoundDuration( TEAM_SURVIVOR );
	// S2DFCD3 COMISS old,input; JA retains old, unordered selects INPUT.
	const float oldBest = m_flTeamBestRoundTime;
	const float best = ( _mm_movemask_ps( _mm_cmpgt_ss( _mm_set_ss( oldBest ), _mm_set_ss( duration ) ) ) & 1 ) ? oldBest : duration;
	if ( _mm_movemask_ps( _mm_cmpneq_ss( _mm_set_ss( oldBest ), _mm_set_ss( best ) ) ) & 1 )
		m_flTeamBestRoundTime.SetDirect( best );
}
void CTerrorGameRules::ResetRoundAccumulatedTime() { m_flAccumulatedTime = 0.0f; }
void CTerrorGameRules::AddRoundAccumulatedTime( float time ) { m_flAccumulatedTime += time; }
int CTerrorGameRules::GetScavengeTeamIndex( int team ) const
{
	if ( !IsPlayerControlledZombies() ) return 1;
	if ( team == TEAM_SURVIVOR ) return ( TheDirector->AreTeamsFlipped() ? 1 : 0 ) + 1;
	if ( team == TEAM_INFECTED ) return ( TheDirector->AreTeamsFlipped() ? 0 : 1 ) + 1;
	return 0;
}
int CTerrorGameRules::GetScavengeTeamScore( int team, int round ) const
{
	if ( team < TEAM_SURVIVOR ) return 0;
	const int teamIndex = GetScavengeTeamIndex( team ); // Native2DBE10 queries this BEFORE the default-round read.
	if ( round <= 0 ) round = MAX( 1, (int)m_nRoundNumber );
	const unsigned int index = (unsigned int)teamIndex + 2u * (unsigned int)round - 3u;
	if ( index >= (unsigned int)m_iScavengeTeamScore.Count() ) return 0; // Native2DBE10; Main decision: only out-of-array read is absent0, no round clamp.
	return m_iScavengeTeamScore[index];
}
void CTerrorGameRules::IncrementScavengeTeamScore( int team )
{
	if ( team < TEAM_SURVIVOR ) return;
	const int round = MAX( 1, (int)m_nRoundNumber );
	const unsigned int index = (unsigned int)GetScavengeTeamIndex( team ) + 2u * (unsigned int)round - 3u;
	if ( index >= (unsigned int)m_iScavengeTeamScore.Count() ) return; // Native2DE670; Main decision: only out-of-array write is absent, no round clamp.
	m_iScavengeTeamScore.Set( index, (int)( (unsigned int)m_iScavengeTeamScore[index] + 1u ) );
}
double CTerrorGameRules::GetTeamRoundDuration( int team ) const
{
	if ( team == TEAM_SURVIVOR && IsRoundInProgress() ) return GetRoundElapsedTime();
	return m_flRoundDuration[GetScavengeTeamIndex( team ) - 1];
}
void CTerrorGameRules::SetScavengeItemsRemaining( int count ) { m_nScavengeItemsRemaining = count; }
void CTerrorGameRules::SetScavengeItemsGoal( int count ) { m_nScavengeItemsGoal = count; }
double CTerrorGameRules::GetRoundStartTime() const { return m_flRoundStartTime; } // Native2DBBB0.
double CTerrorGameRules::GetTeamBestRoundTime() const { return m_flTeamBestRoundTime; } // Native2DBD10.
int CTerrorGameRules::GetScavengeScore( bool firstTeam, int round ) const
{
	if ( round <= 0 ) round = MAX( 1, (int)m_nRoundNumber );
	const unsigned int index = 2u * (unsigned int)round - 2u + ( firstTeam ? 0u : 1u );
	if ( index >= (unsigned int)m_iScavengeTeamScore.Count() ) return 0; // Native2DBE60; Main decision: only out-of-array read is absent0, preserve in-array aliasing.
	return m_iScavengeTeamScore[index];
}
int CTerrorGameRules::GetScavengeMatchScore( int team ) const
{
	return team < TEAM_SURVIVOR ? 0 : m_iScavengeMatchScore[GetScavengeTeamIndex( team ) - 1];
}
void CTerrorGameRules::SetScavengeTeamRoundTime( int teamIndex, float time )
{
	if ( teamIndex > 0 ) m_flTeamRoundTime.Set( teamIndex - 1, time ); // Native2DE700.
}
void CTerrorGameRules::SetTeamRoundDuration( int team, float duration )
{
	m_flRoundDuration.Set( GetScavengeTeamIndex( team ) - 1, duration ); // Native2DE3F0.
}
void CTerrorGameRules::SetScavengeWinningTeam( int teamIndex, int round )
{
	if ( round <= 0 ) round = MAX( 1, (int)m_nRoundNumber );
	const unsigned int index = (unsigned int)round - 1u;
	if ( index >= (unsigned int)m_iWinningTeamNumber.Count() ) return; // Native2DE480, caller286C60; Main decision: only out-of-array write is absent.
	m_iWinningTeamNumber.Set( index, teamIndex );
}
int CTerrorGameRules::GetScavengeWinningTeam( int round ) const
{
	if ( round <= 0 ) round = MAX( 1, (int)m_nRoundNumber );
	const unsigned int index = (unsigned int)round - 1u;
	if ( index >= (unsigned int)m_iWinningTeamNumber.Count() ) return 0; // Native2DBCE0; Main decision: only out-of-array read is absent0, no round clamp.
	return m_iWinningTeamNumber[index];
}
void CTerrorGameRules::IncrementScavengeMatchScore( int teamIndex )
{
	if ( teamIndex > 0 )
	{
		const int index = teamIndex - 1;
		m_iScavengeMatchScore.Set( index, (int)( (unsigned int)m_iScavengeMatchScore[index] + 1u ) );
	}
}
int CTerrorGameRules::GetDamageScoreTeamIndex() const
{
	return IsPlayerControlledZombies() ? ( TheDirector->AreTeamsFlipped() ? 0 : 1 ) : 0;
}
void CTerrorGameRules::UpdateDirectorRoundState()
{
	m_bAreTeamsFlipped = TheDirector->AreTeamsFlipped();
	m_bInSecondHalfOfRound = TheDirector->IsInSecondHalfOfRound();
	m_bIsTransitioningToNextMap = g_bTerrorTransitionRestore && !g_bTerrorRestartAfterDeath; // Native25BD50, NOT Director1254.
	m_bIsVersusVoteRestarting = TheDirector->GetVersusMode()->IsVoteRestarting(); // Native child24, NOT Director277.
}
void CTerrorGameRules::AdvanceScavengeRoundNumber() { m_nRoundNumber = (int)( (unsigned int)m_nRoundNumber + 1u ); }
void CTerrorGameRules::ResetScavengeRoundNumber() { m_nRoundNumber = 0; } // native2DFDE0
KeyValues *CTerrorGameRules::GetFirstChapterInfo( KeyValues **outMission ) const
{
	// Native2D9E40: preserve current chapter if chapter1 lookup fails.
	if ( outMission ) *outMission = NULL;
	KeyValues *details = TerrorBuildActiveServerGameProjection();
	if ( !details ) return NULL;
	KeyValues *chapter = g_TerrorModes.GetChapterInfo( details, outMission );
	details->SetInt( "Game/chapter", 1 );
	KeyValues *first = g_TerrorModes.GetChapterInfo( details, NULL );
	if ( first ) chapter = first;
	details->deleteThis(); return chapter; // Borrowed sole dictionary node.
}
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
int CTerrorGameRules::GetSurvivalMedalForTime( float time ) const
{
	// Native2DA0E0 genuine thresholds; unordered compares all fail ->-1.
	if ( time >= 600.0f ) return 2;
	if ( time >= 420.0f ) return 1;
	if ( time >= 240.0f ) return 0;
	return -1;
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
KeyValues *CTerrorGameRules::GetNextChapterInfo( KeyValues **outMission ) const
{
	if ( outMission ) *outMission = NULL;
	KeyValues *details = TerrorBuildActiveServerGameProjection();
	if ( !details ) return NULL;
	g_TerrorModes.GetChapterInfo( details, outMission );
	details->SetInt( "Game/chapter", details->GetInt( "Game/chapter", 0 ) + 1 );
	KeyValues *next = g_TerrorModes.GetChapterInfo( details, NULL );
	details->deleteThis(); return next;
}
bool CTerrorGameRules::IsFinalChapter() const
{
	// Native2D9FA0 is NEXT-CHAPTER ABSENCE, not mode's singlechapter option.
	KeyValues *details = TerrorBuildActiveServerGameProjection();
	if ( !details ) return true; // Actual original guard.
	details->SetInt( "Game/chapter", details->GetInt( "Game/chapter", 0 ) + 1 );
	bool last = g_TerrorModes.GetChapterInfo( details, NULL ) == NULL;
	details->deleteThis(); return last;
}
int CTerrorGameRules::GetVersusChapterScore( int team ) const { return m_iChapterScore[team - 1]; }
int CTerrorGameRules::GetVersusSurvivalMultiplier( int team ) const { return m_iVersusSurvivalMultiplier[GetScavengeTeamIndex( team ) - 1]; }
int CTerrorGameRules::GetVersusDefibsUsed( int team ) const { return m_iVersusDefibsUsed[GetScavengeTeamIndex( team ) - 1]; }
void CTerrorGameRules::SetVersusCampaignScores( int first, int second ) { m_iCampaignScore.Set( 0, first ); m_iCampaignScore.Set( 1, second ); }
void CTerrorGameRules::AccumulateVersusChapterScores()
{
	m_iChapterScore.Set( 0, (int)( (unsigned int)m_iChapterScore[0] + (unsigned int)m_iSurvivorScore[0] ) );
	m_iChapterScore.Set( 1, (int)( (unsigned int)m_iChapterScore[1] + (unsigned int)m_iSurvivorScore[1] ) );
}
void CTerrorGameRules::ResetRoundDurations()
{
	m_flRoundDuration.Set( 0, 0.0f ); m_flRoundDuration.Set( 1, 0.0f ); // Native2DE430.
}
void CTerrorGameRules::ResetScavengeScores( bool match )
{
	for ( int i = 0; i < 10; ++i ) m_iScavengeTeamScore.Set( i, 0 );
	for ( int i = 0; i < 5; ++i ) m_iWinningTeamNumber.Set( i, 0 );
	for ( int i = 0; i < 2; ++i ) m_flTeamRoundTime.Set( i, 0.0f );
	if ( match ) for ( int i = 0; i < 2; ++i ) m_iScavengeMatchScore.Set( i, 0 );
}
void CTerrorGameRules::ResetVersusRoundScores( bool campaign )
{
	for ( int i = 0; i < 2; ++i )
	{
		m_iSurvivorScore.Set( i, 0 ); m_flTeamRoundTime.Set( i, 0.0f );
		m_iChapterScore.Set( i, 0 ); m_iChapterDamage.Set( i, 0 );
		m_iVersusSurvivalMultiplier.Set( i, 0 ); m_iVersusDefibsUsed.Set( i, 0 );
	}
	if ( campaign ) for ( int i = 0; i < 2; ++i ) m_iCampaignScore.Set( i, 0 );
	m_nCachedVersusDistanceScore = 0; // Plain native1068.
	m_nNativeRoundScore1088 = 0; // Native CNetworkVar reset, NOT max completion3584.
	m_flTeamBestRoundTime = 0.0f;
	for ( int i = 0; i < 8; ++i ) { m_iVersusDistancePerSurvivor.Set( i, 0 ); m_iVersusSurvivorDeathDistance.Set( i, -1 ); }
	Q_memset( m_finaleEscapees, 0, sizeof( m_finaleEscapees ) );
}
#endif

#ifdef CLIENT_DLL
// C27CA90, same fields already received in the existing Rules owner.
int CTerrorGameRules::GetScavengeTeamIndex( int team ) const
{
	if ( !IsPlayerControlledZombies() ) return 1;
	if ( team == TEAM_SURVIVOR ) return ( m_bAreTeamsFlipped ? 1 : 0 ) + 1;
	if ( team == TEAM_INFECTED ) return ( m_bAreTeamsFlipped ? 0 : 1 ) + 1;
	return 0;
}
// C27D190. Preserve in-array cross-team aliasing; only actual OOB reads are0.
int CTerrorGameRules::GetScavengeTeamScore( int team, int round ) const
{
	if ( team < TEAM_SURVIVOR ) return 0;
	const int indexTeam = GetScavengeTeamIndex( team );
	if ( round <= 0 ) round = MAX( 1, (int)m_nRoundNumber );
	const unsigned int index = (unsigned int)indexTeam + 2u * (unsigned int)round - 3u;
	if ( index >= (unsigned int)m_iScavengeTeamScore.Count() ) return 0;
	return m_iScavengeTeamScore[index];
}
// C27D130. No client-only guessed timer or replicated duplicate.
double CTerrorGameRules::GetTeamRoundDuration( int team ) const
{
	if ( team == TEAM_SURVIVOR && IsRoundInProgress() ) return GetRoundElapsedTime();
	const unsigned int index = (unsigned int)GetScavengeTeamIndex( team ) - 1u;
	if ( index >= (unsigned int)m_flRoundDuration.Count() ) return 0.0; // Standing OOB-read adaptation only.
	return m_flRoundDuration[index];
}
int CTerrorGameRules::GetScavengeScore( bool firstTeam, int round ) const
{
	if ( round <= 0 ) round = MAX( 1, (int)m_nRoundNumber );
	const unsigned int element = 2u * (unsigned int)round - 2u + ( firstTeam ? 0u : 1u );
	if ( element >= (unsigned int)m_iScavengeTeamScore.Count() ) return 0;
	return m_iScavengeTeamScore[element];
}
int CTerrorGameRules::GetScavengeMatchScore( int team ) const
{
	if ( team < TEAM_SURVIVOR ) return 0;
	const unsigned int element = (unsigned int)GetScavengeTeamIndex( team ) - 1u;
	if ( element >= (unsigned int)m_iScavengeMatchScore.Count() ) return 0;
	return m_iScavengeMatchScore[element];
}
int CTerrorGameRules::GetScavengeWinningTeam( int round ) const
{
	if ( round <= 0 ) round = MAX( 1, (int)m_nRoundNumber );
	const unsigned int element = (unsigned int)round - 1u;
	if ( element >= (unsigned int)m_iWinningTeamNumber.Count() ) return 0;
	return m_iWinningTeamNumber[element];
}
double CTerrorGameRules::GetRoundStartTime() const { return m_flRoundStartTime; }
double CTerrorGameRules::GetRoundEndTime() const { return m_flRoundEndTime; }
double CTerrorGameRules::GetTeamBestRoundTime() const { return m_flTeamBestRoundTime; }
int CTerrorGameRules::GetVersusSurvivalMultiplier( int team ) const
{
	const unsigned int element = (unsigned int)GetScavengeTeamIndex( team ) - 1u;
	if ( element >= (unsigned int)m_iVersusSurvivalMultiplier.Count() ) return 0;
	return m_iVersusSurvivalMultiplier[element];
}
int CTerrorGameRules::GetVersusDefibsUsed( int team ) const
{
	const unsigned int element = (unsigned int)GetScavengeTeamIndex( team ) - 1u;
	if ( element >= (unsigned int)m_iVersusDefibsUsed.Count() ) return 0;
	return m_iVersusDefibsUsed[element];
}
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
int CTerrorGameRules::GetSurvivalMedalForTime( float time ) const
{
	if ( time >= 600.0f ) return 2;
	if ( time >= 420.0f ) return 1;
	if ( time >= 240.0f ) return 0;
	return -1;
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
int CTerrorGameRules::GetVersusChapterScore( int team ) const
{
	const unsigned int index = (unsigned int)team - 1u;
	if ( index >= (unsigned int)m_iChapterScore.Count() ) return 0;
	return m_iChapterScore[index];
}
#endif
