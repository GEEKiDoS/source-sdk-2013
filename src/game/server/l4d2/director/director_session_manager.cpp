//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/director/director_session_manager.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/director/director_scavenge_mode.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/player/terrorplayer.h"
#include "nav_mesh.h"
#include "team.h"
#include "recipientfilter.h"
#include "usermessages.h"
#include "iserver.h"
#include "tier0/memdbgon.h"

ConVar director_min_start_players( "director_min_start_players", "1", 0x00000006,
	"Freezes players until this # of players have joined, then starts mission" );
static ConVar director_unfreeze_time( "director_unfreeze_time", "55", 0x00004004,
	"Duration (in seconds) to wait to unfreeze a team after the first player has connected" );
ConVar director_ready_duration( "director_ready_duration", "60", 0x00004004,
	"If nonzero, survivor team has this amount of time to get ready before starting out" );

static int CountHumanPlayers( int team )
{
	int count = 0;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( player && !player->IsBot() && player->GetTeamNumber() == team )
			++count;
	}
	return count;
}
static bool IsSessionPlayer( CTerrorPlayer *player )
{
	return player && player->edict() && player->entindex() != 0 && player->IsConnected();
}

CDirectorSessionManager::CDirectorSessionManager()
{
	// S:271110 allocates/zeroes all 76 bytes BEFORE S:28D3D0. Reset is a
	// separate round operation; do not run it here against a half-built Director.
	m_bSurvivorsReady = false;
	m_bTeamFrozen = false;
	m_flUnfreezeTime = 0.0f;
	m_flConnectTimeout = 0.0f;
	m_bLevelActive = true;
	m_bRestoredSavedPlayers = true;
	m_bCheckDisconnectedPlayers = false;
	m_bLowViolence = false;
}
void CDirectorSessionManager::OnMapStart()
{
	m_FullyConnectedUserIDs.RemoveAll();
}
void CDirectorSessionManager::OnRoundRestart()
{
	m_bRestoredSavedPlayers = false;
}
void CDirectorSessionManager::OnLevelShutdown()
{
	m_bLevelActive = false;
	m_FullyConnectedUserIDs.RemoveAll();
}
void CDirectorSessionManager::Reset()
{
	m_bLevelActive = true;
	m_ReadyTimer.Invalidate();
	m_bTeamFrozen = director_min_start_players.GetInt() > 0 &&
		!TheDirector->IsBackgroundMap() &&
		( TheDirector->IsFirstChapter() || !TheDirector->IsChangingMapOrMission() ||
		  TerrorGameRules()->IsScavengeMode() ) && m_FullyConnectedUserIDs.Count() == 0;
	DevMsg( "[INTROS] Team %s\n", m_bTeamFrozen ? "is frozen" : "is NOT frozen" );
	if ( !m_bTeamFrozen )
	{
		if ( director_min_start_players.GetInt() <= 0 )
			DevMsg( "[INTROS] Min start players is %d\n", director_min_start_players.GetInt() );
		if ( m_FullyConnectedUserIDs.Count() )
			DevMsg( "[INTROS] Players already connected, we're restarting on the same map\n" );
		if ( TheDirector->IsBackgroundMap() )
			DevMsg( "[INTROS] It's the background map\n" );
		if ( TheDirector->IsChangingMapOrMission() && !TheDirector->IsFirstChapter() )
			DevMsg( "[INTROS] We've transitioned and this isn't the mission start map\n" );
	}
	m_flUnfreezeTime = 0.0f;
	m_flConnectTimeout = 0.0f;
	m_bSurvivorsReady = director_ready_duration.GetFloat() <= 0.0f && !m_bTeamFrozen;
}
void CDirectorSessionManager::OnSurvivorLeftSafeArea( CTerrorPlayer *player )
{
	// S:28C800 RET 4. The extra PUSH ESI in S:26E500 is saved by its
	// matching POP ESI, not a second callback parameter.
	m_bSurvivorsReady = true;
}
int CDirectorSessionManager::GetSurvivorReadyTime() const
{
	return m_bSurvivorsReady ? 0 : (int)m_ReadyTimer.GetRemainingTime();
}
void CDirectorSessionManager::StartReadyCountdownNow()
{
	if ( !m_bSurvivorsReady )
		m_ReadyTimer.Start( 0.0f );
}
void CDirectorSessionManager::FireGameEvent( IGameEvent *event )
{
	if ( !Q_strcmp( event->GetName(), "player_connect_full" ) )
		OnFullConnect( ToTerrorPlayer( UTIL_PlayerByUserId( event->GetInt( "userid" ) ) ) );
}
void CDirectorSessionManager::OnFullConnect( CTerrorPlayer *player )
{
	if ( !player || player->IsBot() )
		return;
	const int userID = player->GetUserID();
	if ( m_FullyConnectedUserIDs.Find( userID ) == m_FullyConnectedUserIDs.InvalidIndex() )
	{
		m_FullyConnectedUserIDs.AddToTail( userID );
		DevMsg( "[INTROS] Adding %s to fully connected userid's. New count: %d\n",
			player->GetPlayerName(), m_FullyConnectedUserIDs.Count() );
	}
	// Retail deduplicates only the userid list. Repeated full-connect events
	// still append to the admission FIFO, including their original timestamp.
	OnClientConnected( player );
}
void CDirectorSessionManager::OnClientConnected( CTerrorPlayer *player )
{
	if ( player->IsBot() )
		return;
	Admission entry;
	entry.player = player;
	entry.connectedAt = gpGlobals->curtime;
	m_Admissions.AddToTail( entry );
	if ( m_bTeamFrozen && !engine->GetIServer()->IsPaused() )
		player->LockPlayerInPlace();
}
void CDirectorSessionManager::OnClientDisconnected( CTerrorPlayer *player )
{
	if ( !player->IsBot() )
	{
		m_FullyConnectedUserIDs.FindAndRemove( player->GetUserID() );
		m_bCheckDisconnectedPlayers = true;
	}
	// S:28D240 does NOT remove pending EHANDLEs. The FIFO drain resolves them.
}
void CDirectorSessionManager::FillSurvivorBotRoster()
{
	CTeam *team = GetGlobalTeam( TEAM_SURVIVOR );
	if ( team && team->GetNumPlayers() < 4 )
	{
		const int missing = 4 - team->GetNumPlayers();
		for ( int i = 0; i < missing; ++i )
			TheDirector->CreateSurvivorBot( SurvivorCharacter_Unknown );
	}
}
void CDirectorSessionManager::ProcessAdmissions()
{
	if ( m_bTeamFrozen )
		return;
	static ConVarRef director_test_loop( "director_test_loop" );
	if ( director_test_loop.GetInt() > 0 && engine->IsDedicatedServer() )
		FillSurvivorBotRoster();
	if ( !m_Admissions.Count() )
		return;
	if ( !m_bRestoredSavedPlayers && gpGlobals->curtime > 1.0f )
	{
		m_bRestoredSavedPlayers = true;
		TheDirector->RestoreSavedPlayers(); // S:2BA640, real actor/transition producer
	}
	while ( m_Admissions.Count() )
	{
		CTerrorPlayer *player = m_Admissions[0].player.Get();
		if ( player && player->GetTeamNumber() <= TEAM_UNASSIGNED )
		{
			TheDirector->JoinNewPlayer( m_Admissions[0].player ); // S:26CE40
			if ( !TheDirector->AreSurvivorBotsDisabled() )
				FillSurvivorBotRoster();
		}
		m_Admissions.Remove( 0 );
	}
}
bool CDirectorSessionManager::UpdateConnectingPlayers()
{
	if ( !m_bTeamFrozen )
		return false;
	if ( m_flUnfreezeTime == 0.0f )
	{
		int expected = director_min_start_players.GetInt();
		int count = 0;
		for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
			if ( IsSessionPlayer( player ) && m_FullyConnectedUserIDs.Find( player->GetUserID() ) != m_FullyConnectedUserIDs.InvalidIndex() )
				++count;
		}
		if ( count > 0 && m_flConnectTimeout == 0.0f )
		{
			m_flConnectTimeout = gpGlobals->curtime + director_unfreeze_time.GetFloat();
#if defined( _X360 )
			if ( IsX360() && TheDirector->IsConsoleDedicatedServer() )
			{
				expected = 4;
				director_min_start_players.SetValue( 4 );
			}
#endif
		}
		const bool timedOut = m_flConnectTimeout != 0.0f && gpGlobals->curtime > m_flConnectTimeout;
		if ( count >= expected && !timedOut )
			DevMsg( "[INTROS] %d fully connected players matches %d exptected players. Unfreezing team.\n", count, expected );
		else if ( timedOut )
			DevMsg( "[INTROS] Time is up, unfreezing players\n" );
		else
			return false;
		director_min_start_players.SetValue( count );
		m_flUnfreezeTime = gpGlobals->curtime + 3.0f;
		m_flConnectTimeout = 0.0f;
		return false;
	}
	if ( gpGlobals->curtime < m_flUnfreezeTime )
		return false;
	m_bTeamFrozen = false;
	m_flUnfreezeTime = 0.0f;
	return true;
}
void CDirectorSessionManager::UnfreezePlayers()
{
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( !IsSessionPlayer( player ) )
			continue;
		player->UnlockPlayer();
		player->ShowViewPortPanel( "lobby", false );
		if ( !TerrorGameRules()->IsPlayerControlledZombies() )
			m_ReadyTimer.Start( 0.0f );
	}
	OnAllPlayersConnected();
	CReliableBroadcastRecipientFilter filter;
	UserMessageBegin( filter, "AllPlayersConnectedGameStarting" );
	MessageEnd();
}
void CDirectorSessionManager::OnAllPlayersConnected()
{
	TheDirector->OnPlayersConnected(); // native Director vtable+44
	if ( TheDirector->IsPlayingIntro() )
		m_ReadyTimer.Invalidate();
}
void CDirectorSessionManager::ProcessDisconnects()
{
	if ( !m_bCheckDisconnectedPlayers || TheDirector->IsTransitionInProgress() )
		return;
	m_bCheckDisconnectedPlayers = false;
	static ConVarRef sb_all_bot_game( "sb_all_bot_game" );
	if ( sb_all_bot_game.GetBool() )
		return;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( IsSessionPlayer( player ) && !player->IsBot() )
			return;
	}
	TheDirector->OnNoHumanPlayersLeft(); // S:28D440 reached native teardown
}
void CDirectorSessionManager::UpdateReadyState()
{
	if ( TheDirector->IsPlayingIntro() || !TheDirector->GetScavengeMode()->UpdateReadyCountdown() || m_bSurvivorsReady )
		return;
	if ( !m_ReadyTimer.HasStarted() || m_bTeamFrozen )
	{
		static ConVarRef sb_all_bot_game( "sb_all_bot_game" );
		if ( !sb_all_bot_game.GetBool() )
		{
			if ( CountHumanPlayers( TEAM_SURVIVOR ) == 0 && CountHumanPlayers( TEAM_INFECTED ) == 0 )
				return;
			if ( TheDirector->GetTeamRosterCount( TEAM_SURVIVOR, 1 ) != 4 )
			{
				m_ReadyTimer.Start( director_ready_duration.GetFloat() );
				return;
			}
		}
		m_ReadyTimer.Start( 0.0f );
		return;
	}
	static ConVarRef nav_edit( "nav_edit" );
	if ( gpGlobals->curtime >= m_ReadyTimer.m_timestamp || nav_edit.GetInt() != 0 || director_ready_duration.GetInt() == 0 || TheDirector->IsFirstChapter() )
	{
		m_bSurvivorsReady = true;
		TheDirector->OnSurvivorsReady(); // native Director vtable+84
		return;
	}
	int players, restoring;
	TerrorGameRules()->GetRestoringPlayerCounts( TEAM_SURVIVOR, &players, &restoring );
	if ( players >= restoring )
	{
		if ( !TerrorGameRules()->IsPlayerControlledZombies() )
			m_ReadyTimer.Start( 0.0f );
		else
		{
			TerrorGameRules()->GetRestoringPlayerCounts( TEAM_INFECTED, &players, &restoring );
			if ( players > 0 && players >= restoring )
				m_ReadyTimer.Start( 0.0f );
		}
	}
}
void CDirectorSessionManager::UpdateLowViolence()
{
	// S:28DA30 selects real NextBot combat characters through S:26BAA0
	// (mode2). Do not replace its body/component predicate with IsBot.
	TheDirector->UpdateLowViolenceCombatCharacters();
}
void CDirectorSessionManager::CheckIdlePlayer( CTerrorPlayer *player, bool *allIdle )
{
	if ( player->IsBot() || player->IsSplitScreenPlayer() )
		return;
	if ( CountHumanPlayers( TEAM_SURVIVOR ) == 1 && CountHumanPlayers( TEAM_INFECTED ) == 0 && player->GetTeamNumber() == TEAM_SURVIVOR )
	{
		if ( gpGlobals->curtime - player->GetLastActivityTime() <= 300.0f )
			*allIdle = false;
		return;
	}
	if ( player->GetTeamNumber() != TEAM_SPECTATOR )
		player->RefreshSpectatorIdleTime(); // S:2FAB10, curtime+sv_spectatoridletime*60
	float idleAt = player->GetSpectatorIdleTime();
	const CUtlVector< EHANDLE > &splitPlayers = player->GetSplitScreenPlayers();
	FOR_EACH_VEC( splitPlayers, i )
	{
		CTerrorPlayer *split = ToTerrorPlayer( splitPlayers[i].Get() );
		if ( split )
		{
			if ( split->GetTeamNumber() != TEAM_SPECTATOR )
				split->RefreshSpectatorIdleTime();
			idleAt = MAX( idleAt, split->GetSpectatorIdleTime() );
		}
	}
	if ( gpGlobals->curtime < idleAt )
		*allIdle = false;
}
void CDirectorSessionManager::UpdateIdlePlayers()
{
	static ConVarRef sb_all_bot_game( "sb_all_bot_game" );
	if ( sb_all_bot_game.GetBool() || TheDirector->IsTransitionRestoreActive() )
		return;
	bool allIdle = true;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( IsSessionPlayer( player ) )
			CheckIdlePlayer( player, &allIdle );
	}
	if ( !allIdle || !( engine->IsDedicatedServer() || TheDirector->IsPublicLiveSession() ) )
		return;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( IsSessionPlayer( player ) && !player->IsBot() && !player->IsSplitScreenPlayer() )
		{
			char command[64];
			Q_snprintf( command, sizeof( command ), "kickid %d All players idle\n", player->GetUserID() );
			engine->ServerCommand( command );
		}
	}
}
bool CDirectorSessionManager::Update()
{
	const bool unfreeze = UpdateConnectingPlayers();
	ProcessAdmissions();
	ProcessDisconnects();
	if ( unfreeze )
		UnfreezePlayers();
	// Native order intentionally admits players even during nav generation.
	if ( TheNavMesh->IsGenerating() || TheDirector->IsTransitionRestoreActive() )
		return false;
	const bool lowViolence = engine->IsLowViolence();
	if ( m_bLowViolence != lowViolence )
	{
		m_bLowViolence = lowViolence;
		DevMsg( lowViolence ? "Low Violence client connected\n" : "Low Violence client(s) disconnected\n" );
		if ( lowViolence )
			UpdateLowViolence();
	}
	UpdateIdlePlayers();
	UpdateReadyState();
	return true;
}
int CDirectorSessionManager::DrawDebugText( int line )
{
	TheDirector->DrawSessionDebugLine( line, "SESSION STATS", 255, 255, 255 );
	if ( m_bSurvivorsReady )
		return line + 1;
	char text[256];
	if ( m_ReadyTimer.HasStarted() )
		Q_snprintf( text, sizeof( text ), "  Survivor ready time: %0.0f seconds", (double)m_ReadyTimer.m_timestamp - (double)gpGlobals->curtime );
	else
		Q_strncpy( text, "  Waiting for survivors", sizeof( text ) );
	TheDirector->DrawSessionDebugLine( line + 1, text, 255, 255, 0 );
	return line + 2;
}
