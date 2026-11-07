//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/gamerules/terror_game_details.h"
#include "l4d2/gamerules/terror_mission_info.h"
#include "l4d2/gamerules/terror_mode.h"
#include "l4d2/gamerules/terror_round_end.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/director/director_survival_mode.h"
#include "l4d2/director/director_versus_mode.h"
#include "l4d2/director/director_scavenge_mode.h"
#include "l4d2/entities/info_changelevel.h"
#include "l4d2/stats/player_reporter.h"
#include "l4d2/sound/terror_music.h"
#include "recipientfilter.h"
#include "tier0/icommandline.h"
#include "tier1/fmtstr.h"
#include <xmmintrin.h>
#include "tier0/memdbgon.h"

ConVar versus_round_restarttimer( "versus_round_restarttimer", "15", 0x00000006, "" );
ConVar versus_round_restarttimer_finale( "versus_round_restarttimer_finale", "10", 0x00000006, "" );
ConVar versus_level_restart_delay( "versus_level_restart_delay", "4", 0x00004004, "After a successful vote, restart the map after this many seconds." );
ConVar scavenge_round_restart_delay( "scavenge_round_restart_delay", "10", 0x00004006, "After a round, restart the round after this many seconds." );
ConVar scavenge_round_restart_delay_tied( "scavenge_round_restart_delay_tied", "15", 0x00004006, "After a tie, restart the round after this many seconds." );
ConVar scavenge_match_finished_delay( "scavenge_match_finished_delay", "5", 0x00004006, "After a match ends, move on to post-round state after this many seconds." );
extern bool g_bTerrorPressDemo; // Sole gameinterface.cpp owner; native107F7735 startup cache.

namespace
{
	struct FreezeRoundPlayer
	{
		bool operator()( CBasePlayer *base )
		{
			CTerrorPlayer *player = ToTerrorPlayer( base );
			player->ShowRoundStats(); // S2FCAA0: bots and -makedevshots do not receive ShowStats.
			player->AddFlag( FL_FROZEN );
			return true;
		}
	};
	struct ResetRoundReport
	{
		bool operator()( CBasePlayer *base )
		{
			CTerrorPlayer *player = ToTerrorPlayer( base );
			// S2FC690 ->2BC5F0 has this guard before the canonical2BC550 guard.
			if ( player->GetTeamNumber() != TEAM_INFECTED || TerrorGameRules()->IsPlayerControlledZombies() )
				GetPlayerReporter().RemovePlayerReport( player );
			return true;
		}
	};
	struct PrepareFinaleLossPlayer
	{
		bool operator()( CBasePlayer *base )
		{
			ToTerrorPlayer( base )->PrepareForFinaleLoss();
			return true;
		}
	};
	struct NotifyMissionLostPlayer
	{
		bool operator()( CBasePlayer *base )
		{
			ToTerrorPlayer( base )->OnMissionLost();
			return true;
		}
	};
	struct NotifyMissionWonPlayer
	{
		bool operator()( CBasePlayer *base )
		{
			CTerrorPlayer *player = ToTerrorPlayer( base );
			player->GetMusic()->OnMissionWon();
			player->UpdateMissionStats();
			return true;
		}
	};
}
int CDirector::GetMissionDuration() const
{
	const unsigned int bits = FloatBits( m_MissionTimer.m_timestamp );
	const bool positive = bits > 0u && bits <= 0x7f800000u;
	const float elapsed = positive ? (float)( (double)gpGlobals->curtime - (double)m_MissionTimer.m_timestamp ) : 99999.8984375f;
	// S25DB10: x87 difference spills tofloat BEFORE ADDSS and CVTTSS2SI.
	const __m128 total = _mm_add_ss( _mm_set_ss( m_flMissionElapsedTime ), _mm_set_ss( elapsed ) );
	return _mm_cvtt_ss2si( total );
}

bool CDirectorScriptedMode::HandleRoundEnd( TerrorRoundEndInfo * ) const
{
	// S275D40 is literally XOR AL,AL; RET4. This is the original handler,
	// not a fallback for an unimplemented script service.
	return false;
}
void CDirector::FreezeRoundPlayers()
{
	FreezeRoundPlayer action;
	ForEachPlayer( action );
}
void CDirector::ResetRoundReports()
{
	ResetRoundReport action;
	ForEachPlayer( action );
}
void CDirector::PrepareFinaleLossPlayers()
{
	PrepareFinaleLossPlayer action;
	ForEachPlayer( action );
}
void CDirector::NotifyMissionWon()
{
	NotifyMissionWonPlayer action;
	ForEachPlayer( action );
}
void CDirector::EnableFinaleWinCamera()
{
	CBaseEntity *camera = gEntList.FindEntityByName( NULL, "camera_finale_win" );
	if ( !camera ) camera = gEntList.FindEntityByName( NULL, "camera_finale" );
	if ( camera )
	{
		variant_t empty;
		camera->AcceptInput( "Enable", NULL, NULL, empty, 0 );
	}
}
void CDirector::ResetMissionEndState()
{
	// S260BE0. These are the actual936/944/616 writers, not round-event aliases.
	m_MissionTimer.Start();
	m_flMissionElapsedTime = 0.0f;
	m_MissionEndTimer.Invalidate();
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
	m_bInSecondHalfOfRound = false;
	m_bTransitioningToNextMap = false;
}
void CDirector::EndScenario()
{
	// Native269200: these are distinct child endpoint handlers, not
	// ApplyRoundEnd's scripted275D40/survival28EA20 result arbitration.
	if ( m_pScriptedMode->IsActive() && m_pScriptedMode->TryEndScriptedMode() ) return;
	if ( m_pScavengeMode->HandleRoundEnd() ) return;
	if ( m_pVersusMode->IsActive() )
	{
		m_pVersusMode->FinishVersusRound( false ); // Native291B60 ->2913B0(false).
		return;
	}
	if ( TerrorGameRules()->IsSurvivalMode() )
	{
		m_bDelayMissionEndScoreboard = true; // Native28DC50 actual1252 true writer.
		int reason = 14;
		if ( TerrorGameRules()->IsPlayerControlledZombies() )
		{
			const float duration = (float)TerrorGameRules()->GetTeamRoundDuration( TEAM_SURVIVOR );
			const __m128d best = _mm_set_sd( TerrorGameRules()->GetTeamBestRoundTime() );
			if ( _mm_movemask_pd( _mm_cmpgt_sd( best, _mm_set_sd( duration ) ) ) & 1 ) reason = 16;
		}
		EndRound( reason );
		return;
	}
	EndRound( g_bTerrorPressDemo ? 17 : 1 ); // Actual100ED4F0, NOT commentary/edit/HL2MP state.
}
void CDirector::EndRound( int reason )
{
	// S269130 COMISS/JA rejects only an ordered positive deadline: NaN enters.
	if ( m_MissionEndTimer.m_timestamp > 0.0f ) return;
	m_nAllLeftStartAreaMissionID = -1;
	m_nRoundEndReason = reason;
	TerrorRoundEndInfo result = { reason, 0, "", 0.0f };
	ApplyRoundEnd( &result );
	IGameEvent *event = gameeventmanager->CreateEvent( "round_end" );
	if ( event )
	{
		event->SetInt( "winner", result.winner );
		event->SetInt( "reason", reason ); // Original argument, not a mode-mutated result.reason.
		event->SetString( "message", result.message );
		event->SetFloat( "time", result.time );
		gameeventmanager->FireEvent( event );
	}
}
void CDirector::ApplyRoundEnd( TerrorRoundEndInfo *result )
{
	// S268CA0. Scripted275D40 precedes Survival28EA20, including their native
	// guards. Versus/Scavenge are not extra arbitration handlers here.
	if ( m_pScriptedMode->HandleRoundEnd( result ) || m_pSurvivalMode->HandleRoundEnd( result ) ) return;
	const color32 black = { 0, 0, 0, 255 };
	switch ( result->reason )
	{
	case 0:
		m_MissionEndTimer.Start( 1.0f );
		result->message = "#L4D_Scenario_Restart";
		ResetRoundReports();
		if ( GetModeDescription()->IsBaseMode( "scavenge" ) || m_pVersusMode->IsActive() )
		{
			if ( m_bInSecondHalfOfRound ) m_bInSecondHalfOfRound = false;
			else { m_bInSecondHalfOfRound = true; m_bTransitioningToNextMap = false; }
		}
		return;
	case 1:
		UTIL_ScreenFadeAll( black, 7.0f, 0.0f, 26 );
		// Native reason1 alone fades; reason4 enters the same loss body below.
		// Fall through.
	case 4:
		if ( m_pScenario->GetFinaleType() != 5 )
		{
			EnableFinaleWinCamera();
			PrepareFinaleLossPlayers();
			CBaseEntity *trigger = gEntList.FindEntityByClassname( NULL, "trigger_finale" );
			if ( !trigger ) trigger = gEntList.FindEntityByClassname( NULL, "finale_trigger" );
			if ( trigger )
			{
				variant_t empty;
				trigger->AcceptInput( "FinaleLost", NULL, NULL, empty, 0 );
			}
		}
		result->winner = TEAM_INFECTED;
		result->message = "#L4D_Scenario_Survivors_Dead";
		{
			// Native1003D440 COMISS/JBE returns the float sentinel for <=0
			// and unordered timestamps, otherwise an independently widened difference.
			double elapsed = m_MissionTimer.m_timestamp > 0.0f ? (double)gpGlobals->curtime - (double)m_MissionTimer.m_timestamp : (double)99999.8984375f;
			m_flMissionElapsedTime = (float)( elapsed + (double)m_flMissionElapsedTime );
		}
		m_MissionTimer.Start(); // Native restarts the interval after banking the failed attempt.
		m_MissionEndTimer.Start( 7.0f );
		{
			IGameEvent *event = gameeventmanager->CreateEvent( "mission_lost" );
			if ( event ) gameeventmanager->FireEvent( event );
			NotifyMissionLostPlayer action;
			ForEachPlayer( action );
		}
		++m_nMissionLossCount;
		return;
	case 3:
		result->winner = TEAM_SURVIVOR;
		result->message = "#L4D_Scenario_Finale_Won";
		return;
	case 5:
		m_MissionEndTimer.Start( TerrorGameRules()->IsFinalChapter() && m_bInSecondHalfOfRound ? versus_round_restarttimer_finale.GetFloat() : versus_round_restarttimer.GetFloat() );
		result->message = "#L4D_Scenario_Restart";
		return;
	case 6:
		if ( TerrorGameRules()->GetNextChapterInfo() )
		{
			m_bTransitioningToNextMap = true;
			FreezeRoundPlayers();
			m_MissionEndTimer.Start( 4.0f );
		}
		else StartEndGameStats();
		return;
	case 7:
		if ( !ShouldResetTeamsOnMissionStart() ) FreezeRoundPlayers();
		m_MissionEndTimer.Start( versus_level_restart_delay.GetFloat() );
		return;
	case 8:
	case 13:
		{
			CUtlVector< CTerrorPlayer * > players;
			CollectTerrorPlayers( players, -1, 2, 3, 9 );
			FOR_EACH_VEC( players, i ) players[i]->AddFlag( FL_FROZEN );
			TerrorShowViewPortPanel( -1, "vs_shutting_down", true, NULL );
			m_MissionEndTimer.Start( 3.0f );
		}
		return;
	case 9:
		m_MissionEndTimer.Start( scavenge_round_restart_delay.GetFloat() );
		result->message = "#L4D_Scenario_Restart";
		return;
	case 10:
		m_MissionEndTimer.Start( scavenge_round_restart_delay_tied.GetFloat() );
		result->message = "#L4D_Scenario_Restart";
		return;
	case 11:
		FreezeRoundPlayers();
		m_MissionEndTimer.Start( 4.0f );
		return;
	case 12:
		m_MissionEndTimer.Start( scavenge_match_finished_delay.GetFloat() );
		result->message = "#L4D_Scenario_Restart";
		return;
	case 17:
		UTIL_ScreenFadeAll( black, 4.0f, 0.0f, 26 );
		m_MissionEndTimer.Invalidate();
		if ( !( m_ScenarioExitTimer.m_timestamp > 0.0f ) ) m_ScenarioExitTimer.Start( 4.0f ); // Native COMISS/JA: unordered also starts.
		return;
	case 18:
		m_MissionEndTimer.Start( 1.0f );
		result->message = "#L4D_Scenario_Restart";
		ResetRoundReports();
		return;
	default:
		return;
	}
}

void CDirectorScriptedMode::HideScoreboard()
{
	// S2767C0 tests active BYTE0, not enabled BYTE1.
	if ( m_bActive ) TerrorShowViewPortPanel( -1, "message_box", false, NULL );
}
void CDirectorScavengeMode::HideScoreboards()
{
	if ( g_TerrorModes.IsBaseMode( "scavenge" ) ) TerrorShowViewPortPanel( -1, "fullscreen_scavenge_scoreboard", false, NULL );
}
void CDirector::StartEndGameStats()
{
	// S265200 is the end-game stats entity path, NOT the2700D0 restart path.
	m_bMissionEndBlocked = true;
	CBaseEntity *stats = gEntList.FindEntityByClassname( NULL, "env_outtro_stats" );
	if ( !stats ) stats = CBaseEntity::Create( "env_outtro_stats", vec3_origin, vec3_angle );
	if ( stats )
	{
		variant_t empty;
		stats->AcceptInput( "PZEndGame", NULL, NULL, empty, 0 );
		return;
	}
	// This fallback exists only at the ORIGINAL entity-creation-failure guard.
	m_pScriptedMode->HideScoreboard();
	m_pVersusMode->HideScoreboards();
	m_pScavengeMode->HideScoreboards();
	FinishScenarioExit();
}
namespace
{
	struct DiscardRoundReport
	{
		bool operator()( CBasePlayer *base )
		{
			// S2FC710 ->2BC550 has no extra2BC5F0 outer guard.
			GetPlayerReporter().RemovePlayerReport( ToTerrorPlayer( base ) );
			return true;
		}
	};
}
void CDirector::FinishScenarioExit()
{
	// S2617A0: actual owned reports precede shutdown callback and reliable broadcast.
	DevMsg( "Director::FinishScenarioExit\n" );
	DiscardRoundReport action;
	ForEachPlayer( action );
	ShutdownScripts( 4, "" );
	CReliableBroadcastRecipientFilter filter;
	UserMessageBegin( filter, "DisconnectToLobby" );
	MessageEnd();
	m_bSpeechDisabled = false;
}
void CDirector::CheckScenarioExit()
{
	// S26D430: child291AA0 first invalidates GLOBAL TheDirector's timer.
	if ( m_pVersusMode->OnScenarioExit() )
	{
		m_MissionEndTimer.Invalidate();
		EndRound( 3 );
		FinishScenarioExit();
	}
}
void CDirector::HideScoreboards()
{
	// S25C570 is ONLY these actual panel callbacks; it does not clear1255.
	m_pScriptedMode->HideScoreboard();
	m_pVersusMode->HideScoreboards();
	m_pScavengeMode->HideScoreboards();
}
void CDirector::RestartCampaign()
{
	// S26D490 begins with the inlined260BE0 reset chain.
	ResetMissionEndState();
	TerrorGameRules()->ResetScavengeRoundNumber();
	m_bMissionEndBlocked = false;
	const char *current = STRING( gpGlobals->mapname );
	if ( !current ) current = "";
	if ( TerrorGameRules()->IsVersusMode() )
	{
		KeyValues *first = TerrorGameRules()->GetFirstChapterInfo();
		const char *map = first ? first->GetString( "map", current ) : current;
		TheDirector->RestartScenarioFromVote( map ); // Original GLOBAL-vs-self distinction.
	}
	else if ( TerrorGameRules()->IsSurvivalMode() && TerrorGameRules()->IsPlayerControlledZombies() )
		TheDirector->RestartScenarioFromVote( current );
	else EndRound( 18 );
}
void CDirector::RestartScenarioFromVote( const char *map )
{
	// S269260's score reset is NOT the260BE0 mission-clock/half reset.
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
	const char *current = STRING( gpGlobals->mapname );
	if ( !current ) current = "";
	if ( !map || map == current || !Q_stricmp( map, current ) )
	{
		Q_strncpy( m_originalMapName, map ? map : current, sizeof( m_originalMapName ) );
		EndRound( 0 );
		return;
	}
	m_nRoundEndReason = 0;
	DiscardRoundReport action;
	ForEachPlayer( action );
	UTIL_LogPrintf( "RestartScenarioFromVote - changing to map %s\n", map );
	m_flMissionElapsedTime = 0.0f;
	m_MissionTimer.Start();
	Q_strncpy( m_originalMapName, map, sizeof( m_originalMapName ) );
	ClearTerrorTransitionLandmark();
	OnRoundEnd( false );
	TerrorGameRules()->PrepareForChangeLevel( map );
	ChangeTerrorLevel( map, NULL ); // Sole helper owns ShutdownScripts(3,map) then engine change.
}

void CDirector::RestartVersusRoundFromVote()
{
	// Actual290530 tail after the child restores its two saved scores.
	// Do not replace with EndRound(7): native omits269130's admission/event.
	m_nRoundEndReason = 7;
	TerrorRoundEndInfo info = { 7, 0, "", 0.0f };
	ApplyRoundEnd( &info );
}
void CDirectorSurvivalMode::RestartRoundFromVote()
{
	// Native28E940 intentionally overrides the GLOBAL timer even if EndRound
	// rejected the call because another positive mission deadline already exists.
	TheDirector->EndRound( 14 );
	TheDirector->SetMissionEndDelay( 1.0f );
}
void CDirector::ChangeMissionFromVote( const char *missionName )
{
	// Native2618A0 uses only Game metadata from its owned details snapshot.
	// Reuse the independently integrated, real active-server Game projection;
	// this is NOT a fabricated MatchSession/System/network projection.
	KeyValues *details = TerrorBuildActiveServerGameProjection();
	if ( !details ) return;
	details->SetString( "Game/campaign", missionName );
	details->SetInt( "Game/chapter", 1 );
	KeyValues *chapter = g_TerrorModes.GetChapterInfo( details, NULL );
	if ( chapter )
	{
		const char *map = chapter->GetString( "map", "" );
		// Native E0E69B0 adds this filename framing; SDK IsMapValid does not.
		if ( map && *map && engine->IsMapValid( CFmtStr( "maps/%s.bsp", map ) ) )
		{
			DiscardRoundReport action;
			ForEachPlayer( action );
			UTIL_LogPrintf( "change mission now vote - changing to %s (%s)\n", missionName, map );
			m_flMissionElapsedTime = 0.0f;
			m_MissionTimer.Start();
			Q_strncpy( m_originalMapName, map, sizeof( m_originalMapName ) );
			ClearTerrorTransitionLandmark();
			// Native25D910(true), not260BE0's clock/half reset.
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
			OnRoundEnd( false );
			TerrorGameRules()->PrepareForChangeLevel( map );
			ChangeTerrorLevel( map, NULL );
		}
	}
	details->deleteThis();
}
void CDirector::ChangeChapterFromVote( const char *map )
{
	// Native2619D0 deliberately does NOT clear the landmark or reset scores.
	if ( !engine->IsMapValid( CFmtStr( "maps/%s.bsp", map ) ) ) return;
	DiscardRoundReport action;
	ForEachPlayer( action );
	UTIL_LogPrintf( "change chapter now vote - changing to %s\n", map );
	m_flMissionElapsedTime = 0.0f;
	m_MissionTimer.Start();
	Q_strncpy( m_originalMapName, map, sizeof( m_originalMapName ) );
	OnRoundEnd( false );
	TerrorGameRules()->PrepareForChangeLevel( map );
	ChangeTerrorLevel( map, NULL ); // Sole helper performs ShutdownScripts(3,map) then engine change.
}
