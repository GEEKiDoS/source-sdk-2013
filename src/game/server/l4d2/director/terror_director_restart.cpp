//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/gamerules/terror_mode.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/director/director_versus_mode.h"
#include "l4d2/director/director_scavenge_mode.h"
#include "l4d2/nav/terror_nav.h"
#include "l4d2/nav/terror_nav_world.h"
#include "l4d2/entities/terror_transition_entities.h"
#include "l4d2/entities/info_changelevel.h"
#include "l4d2/director/info_director.h"
#include "l4d2/stats/player_reporter.h"
#include "eventqueue.h"
#include "tier0/memdbgon.h"

ConVar sv_permawipe( "sv_permawipe", "0", 0x00004004,
	"If set, the campaign will restart fully if the Survivors die." );
ConVar versus_winning_team_goes_last( "versus_winning_team_goes_last", "1", 0x00004004,
	"In versus mode, the team that is losing plays as survivors first" );

namespace
{
	CTerrorPlayer *RestartPlayer( int index )
	{
		// Native25EBA0/25EC90/10025BD0/25E640 share this exact admission.
		// Native8048 is connection state: IsConnected includes disconnecting;
		// it is NOT CCS State_Get()/STATE_PICKINGTEAM. Virtual360 is IsPlayer.
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( index ) );
		return player && player->edict() && player->entindex() != 0 && player->IsPlayer() && player->IsConnected() ? player : NULL;
	}
	int CountRestartHumans( int team )
	{
		int count = 0;
		for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			CTerrorPlayer *player = RestartPlayer( i );
			if ( player && ( team == -1 || player->GetTeamNumber() == team ) && !player->IsBot() ) ++count;
		}
		return count;
	}
	void HideRestartPanel( const char *panel )
	{
		for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			CTerrorPlayer *player = RestartPlayer( i );
			if ( player ) player->ShowViewPortPanel( panel, false, NULL );
		}
	}
}

void CDirector::OnScenarioRestart()
{
	// Native virtual64/25C440. CInfoDirector is the REAL factory-owned class,
	// not an arbitrary entity that happens to accept a similarly named input.
	if ( g_TerrorModes.IsBaseMode( "scavenge" ) )
	{
		if ( !m_bInSecondHalfOfRound && TerrorGameRules() ) TerrorGameRules()->ResetRoundDurations();
		CInfoDirector *info = dynamic_cast< CInfoDirector * >( gEntList.FindEntityByClassname( NULL, "info_director" ) );
		if ( info ) g_EventQueue.AddEvent( info, "RefreshInitialSpawnPositions", 0.0f, NULL, NULL, 0 );
	}
	else if ( TerrorGameRules() )
	{
		TerrorGameRules()->ResetScavengeScores( false );
		TerrorGameRules()->ResetRoundDurations();
	}
}

void CDirector::RestartScenario()
{
	// Native2700D0. Preserve the original distinctions: changing chapter654,
	// live transition652, restore-by-character655, next-map1254, and reason644.
	const bool originalChapter = ShouldResetTeamsOnMissionStart() || !m_bTransitionedToMap;
	const bool discardReports = m_nRoundEndReason != 1 || sv_permawipe.GetBool();
	if ( discardReports )
	{
		for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			CTerrorPlayer *player = RestartPlayer( i );
			if ( player ) GetPlayerReporter().RemovePlayerReport( player ); // Native2FC710 ->2BC550, no2FC690 extra guard.
		}
	}
	bool replayMapEntities = originalChapter || !discardReports || g_TerrorModes.IsBaseMode( "survival" );
	m_pScriptedMode->HideScoreboard();
	m_pVersusMode->HideScoreboards();
	m_pScavengeMode->HideScoreboards();
	const int humans = CountRestartHumans( -1 ); // Native raw life filter=-1, team=-1, includeBots=false.
	HideRestartPanel( "info_window" );
	bool swapped = false;
	if ( humans != 0 && TerrorGameRules()->IsPlayerControlledZombies() )
	{
		bool nextMap = false;
		swapped = m_nRoundEndReason != 18 || AreTeamsFlipped();
		if ( m_bTransitioningToNextMap )
		{
			nextMap = true;
			if ( versus_winning_team_goes_last.GetBool() &&
				TerrorGameRules()->GetScavengeMatchScore( TEAM_SURVIVOR ) > TerrorGameRules()->GetScavengeMatchScore( TEAM_INFECTED ) ) swapped = false;
		}
		else replayMapEntities = true;
		if ( m_nRoundEndReason != 18 && g_TerrorModes.IsBaseMode( "scavenge" ) &&
			TerrorGameRules()->GetScavengeWinningTeam( -1 ) == TerrorGameRules()->GetScavengeTeamIndex( TEAM_INFECTED ) ) swapped = false;
		else if ( swapped ) SwapTeams();
		if ( nextMap )
		{
			KeyValues *chapter = TerrorGameRules()->GetNextChapterInfo();
			if ( chapter )
			{
				const char *map = chapter->GetString( "map", "" );
				OnRoundEnd( false );
				TerrorGameRules()->PrepareForChangeLevel( map );
				ChangeTerrorLevel( map, NULL ); // Sole helper: ShutdownScripts(3,map), then engine ChangeLevel.
			}
			else FinishScenarioExit();
			return;
		}
	}
	if ( sv_permawipe.GetBool() ) { FinishScenarioExit(); return; }
	if ( g_TerrorModes.IsBaseMode( "survival" ) )
	{
		HideRestartPanel( "fullscreen_survival_scoreboard" );
		static ConVarRef sb_all_bot_game( "sb_all_bot_game" ); // Actual existing Frame ConVar, not another owner.
		if ( CountRestartHumans( TEAM_SURVIVOR ) <= 0 && !sb_all_bot_game.GetBool() ) { FinishScenarioExit(); return; }
	}
	char landmarkName[32];
	Q_strncpy( landmarkName, GetTerrorTransitionLandmarkName(), sizeof( landmarkName ) );
	Vector landmarkOrigin = vec3_origin; // Native8819A4 uninitialized .data zero object, not the previous landmark transform.
	CBaseEntity *landmark = gEntList.FindEntityByName( NULL, landmarkName );
	if ( landmark ) landmarkOrigin = landmark->GetAbsOrigin();
	ClearTerrorTransitionLandmark();
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = RestartPlayer( i );
		if ( !player ) continue;
		player->ClearZoomOwner(); // Native1007D9F0 is zoom-owner7560, NOT m_hViewEntity.
		player->SetFOV( player, 0, 0.0f, 0 ); // Native1007DA60; same FOV7532/start7540/time7544 owner.
		player->CleanupPlayerState(); // Actual3290F0; full existing actor cleanup, not Kill/Respawn or a compatibility reset.
	}
	if ( replayMapEntities )
	{
		m_bRestoreByCharacter = true;
		if ( discardReports )
		{
			UTIL_LogPrintf( "Staying on original map %s\n", m_originalMapName );
			m_bTransitionedToMap = false;
		}
		else
		{
			g_bTerrorTransitionRestore = true;
			m_bTransitionedToMap = true;
		}
		m_bTransitionInProgress = true;
		ShutdownScripts( swapped ? 2 : 1, "" );
		TerrorGameRules()->RestartRound(); // Full2E0650, never the obsolete timer-only reset.
		g_bTerrorTransitionRestore = false;
		if ( !TerrorGameRules()->IsPlayerControlledZombies() )
		{
			if ( m_nRoundEndReason == 1 && !originalChapter )
			{
				CUtlVector< CTerrorPlayer * > survivors;
				CollectTerrorPlayers( survivors, TEAM_SURVIVOR, 2, 3, 9 );
				SetTerrorTransitionLandmark( landmarkName, landmarkOrigin );
				FOR_EACH_VEC( survivors, i )
				{
					survivors[i]->SetAwaitingTransitionRestore( true ); // Same native13192, not another restore marker.
					survivors[i]->RestorePendingTransition();
				}
				SetTerrorTransitionLandmark( landmarkName, landmarkOrigin );
				CTerrorCheckpoint *checkpoint = TheTerrorNavMesh()->GetTransitionCheckpoint();
				if ( checkpoint ) checkpoint->RespawnPlayers();
			}
			SetTerrorTransitionEntityRestorePending( m_nRoundEndReason != 4 && !originalChapter ); // Native2539D0, actual existing pending flag.
			RestoreTerrorTransitionEntities();
		}
		OnScenarioRestart();
		m_pScenario->CancelFinale(); // Native287980 writes ONLY finale type5.
		m_bTransitionInProgress = false;
		m_bRestoreByCharacter = false;
	}
	else
	{
		char map[32];
		Q_strncpy( map, m_originalMapName, sizeof( map ) );
		UTIL_LogPrintf( "Reloading original map %s\n", map );
		OnRoundEnd( false );
		TerrorGameRules()->PrepareForChangeLevel( map );
		ChangeTerrorLevel( map, NULL );
	}
}
