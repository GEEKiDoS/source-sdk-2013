//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/director/director_scavenge_mode.h"
#include "l4d2/director/director_survival_mode.h"
#include "l4d2/director/director_item_manager.h"
#include "l4d2/director/info_director.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/nav/terror_nav.h"
#include "l4d2/nav/terror_nav_area.h"
#include "l4d2/infected/common/infected.h"
#include "NextBot/NextBotManager.h"
#include "NextBot/NextBotBodyInterface.h"
#include "entitylist.h"
#include "debugoverlay_shared.h"
#include "l4d2/player/terror_saved_player_states.h"
#include "l4d2/player/bots/survivor_bot.h"
#include "team.h"
#include "tier0/memdbgon.h"

bool g_bTerrorTransitionRestore = false; // original global83B1BD
bool g_bTerrorRestartAfterDeath = false; // original global83B1BC
static ConVar director_force_background( "director_force_background", "0", 0x00004004,
	"Forces background map population logic for testing" );
ConVar z_director_special_spawn_delay( "z_director_special_spawn_delay", "10", 0x00004004 );
static ConVar director_tank_checkpoint_interval( "director_tank_checkpoint_interval", "15", 0x00004004,
	"Min time after leaving a checkpoint that a tank can spawn" );
static ConVar z_mob_spawn_min_interval_easy( "z_mob_spawn_min_interval_easy", "120", 0x00004004 );
static ConVar z_mob_spawn_min_interval_normal( "z_mob_spawn_min_interval_normal", "90", 0x00004004 );
static ConVar z_mob_spawn_min_interval_hard( "z_mob_spawn_min_interval_hard", "90", 0x00004004 );
static ConVar z_mob_spawn_min_interval_expert( "z_mob_spawn_min_interval_expert", "90", 0x00004004 );
static ConVar z_mob_spawn_max_interval_easy( "z_mob_spawn_max_interval_easy", "240", 0x00004004 );
static ConVar z_mob_spawn_max_interval_normal( "z_mob_spawn_max_interval_normal", "180", 0x00004004 );
static ConVar z_mob_spawn_max_interval_hard( "z_mob_spawn_max_interval_hard", "180", 0x00004004 );
static ConVar z_mob_spawn_max_interval_expert( "z_mob_spawn_max_interval_expert", "180", 0x00004004 );

bool CDirector::IsPlayingIntro() const { return m_bPlayingIntro; }
bool CDirector::IsBackgroundMap() const
{
	return gpGlobals->eLoadType == MapLoad_Background || director_force_background.GetBool();
}
bool CDirector::IsChangingMapOrMission() const { return m_bTransitionedToMap || g_bTerrorTransitionRestore; }
bool CDirector::IsInTransition() const { return m_bTransitionInProgress || g_bTerrorTransitionRestore; }
bool CDirector::IsTransitionRestoreActive() const { return g_bTerrorTransitionRestore; }
float CDirector::GetMobSpawnMinTime() const
{
	float time = GetDirectorOptionFloat( "MobSpawnMinTime", -1.0f );
	if ( time != -1.0f ) return time;
	switch ( TerrorGameRules()->GetDifficultyLevel() )
	{
	case 1: return z_mob_spawn_min_interval_normal.GetFloat();
	case 2: return z_mob_spawn_min_interval_hard.GetFloat();
	case 3: return z_mob_spawn_min_interval_expert.GetFloat();
	default: return z_mob_spawn_min_interval_easy.GetFloat();
	}
}
float CDirector::GetMobSpawnMaxTime() const
{
	float time = GetDirectorOptionFloat( "MobSpawnMaxTime", -1.0f );
	if ( time != -1.0f ) return time;
	switch ( TerrorGameRules()->GetDifficultyLevel() )
	{
	case 1: return z_mob_spawn_max_interval_normal.GetFloat();
	case 2: return z_mob_spawn_max_interval_hard.GetFloat();
	case 3: return z_mob_spawn_max_interval_expert.GetFloat();
	default: return z_mob_spawn_max_interval_easy.GetFloat();
	}
}
static void FireSafeAreaEvent( const char *name, CTerrorPlayer *player, int count )
{
	IGameEvent *event = gameeventmanager->CreateEvent( name );
	if ( event )
	{
		event->SetInt( "userid", player ? player->GetUserID() : 0 );
		event->SetInt( "playercount", count );
		gameeventmanager->FireEvent( event );
	}
}
void CDirector::OnSurvivorLeftSafeArea( CTerrorPlayer *player )
{
	// S:26E500. The source call has exactly one explicit player argument.
	// Admission+24 MUST be released before Director+352 or either event.
	if ( m_pScavengeMode->ShouldBlockStartAreaExit( player ) )
		return;
	m_pSessionManager->OnSurvivorLeftSafeArea( player );
	m_bHasAnySurvivorLeftSafeArea = true;
	int survivorCount = 0;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *survivor = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( survivor && survivor->edict() && survivor->entindex() && survivor->IsConnected() && survivor->GetTeamNumber() == TEAM_SURVIVOR )
			++survivorCount; // Native includes bots and dead/incapacitated players.
	}
	if ( !IsChangingMapOrMission() || g_bTerrorRestartAfterDeath )
	{
		m_MobSpawnTimer.Start( RandomFloat( GetMobSpawnMinTime() * 0.25f, GetMobSpawnMaxTime() ) );
		FireSafeAreaEvent( "player_left_start_area", player, survivorCount );
		if ( player ) player->OnLeftStartArea();
	}
	else
	{
		if ( m_flSavedMobInterval < 30.0f ) m_flSavedMobInterval = 30.0f;
		m_MobSpawnTimer.Start( m_flSavedMobInterval );
		if ( !gEntList.FindEntityByClassname( NULL, "trigger_finale" ) )
			m_SpecialSpawnTimer.Start( MAX( director_tank_checkpoint_interval.GetFloat(), m_flSavedSpecialInterval ) );
		else
			m_SpecialSpawnTimer.Invalidate();
		ReacceptSavedTankOffer(); // S:26E380, genuine infected-offer ownership
	}
	FireSafeAreaEvent( "player_left_safe_area", player, survivorCount );
	m_InitialSpawnDelay.Start( z_director_special_spawn_delay.GetFloat() );
	DevMsg( "Allowing spawning - %s left safe area\n", player ? player->GetPlayerName() : "(null)" );
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *connected = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( !connected || !connected->edict() || !connected->entindex() || !connected->IsConnected() ) continue;
		if ( connected->GetTeamNumber() == TEAM_INFECTED )
			connected->ShowViewPortPanel( "zombie_intro", false );
		connected->DeactivateMovementConstraint();
	}
	if ( GetModeDescription()->IsBaseMode( "versus" ) )
	{
		// S:2905C0, the actual callback does not use instance fields.
		TerrorGameRules()->SetRoundStartTime( gpGlobals->curtime );
		IGameEvent *event = gameeventmanager->CreateEvent( "versus_round_start" );
		if ( event ) gameeventmanager->FireEvent( event );
	}
}
void CDirector::UpdateStartAreaExit()
{
	// S:270030, ordered native player collection supplied by M5NavWorld.
	if ( HasAnySurvivorLeftSafeArea() && HaveAllSurvivorsLeftSafeArea() ) return;
	int insideCount = 0;
	int outsideCount = 0;
	CHandle< CTerrorPlayer > firstOutside;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( !player || !player->edict() || !player->entindex() || !player->IsConnected() ) continue;
		if ( !player->IsAlive() || player->GetTeamNumber() != TEAM_SURVIVOR ) continue;
		TerrorNavArea *area = ToTerrorNavArea( player->GetLastKnownArea() );
		if ( !area ) continue;
		if ( area->HasSpawnAttributes( TERROR_NAV_CHECKPOINT ) ) ++insideCount;
		else
		{
			if ( !outsideCount ) firstOutside = player;
			++outsideCount;
		}
	}
	if ( !outsideCount ) return;
	if ( !HasAnySurvivorLeftSafeArea() ) OnSurvivorLeftSafeArea( firstOutside );
	// Independent of the preceding admission/scavenge veto, including its bug.
	if ( !insideCount && !HaveAllSurvivorsLeftSafeArea() )
	{
		m_bHasAllSurvivorsLeftSafeArea = true;
		OnAllSurvivorsLeftSafeArea();
	}
}
void CDirector::OnAllSurvivorsLeftSafeArea()
{
	if ( !IsChangingMapOrMission() || g_bTerrorRestartAfterDeath )
		m_nAllLeftStartAreaMissionID = m_nMissionID;
	// S:25F4D0's connected-survivor functor calls only native nullsub24.
}
void CDirector::OnSurvivorsReady()
{
	// S:2607B0 hides both panels and releases movement constraints before
	// beginning scavenge's separate setup countdown (NOT its live round).
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( !player || !player->edict() || !player->entindex() || !player->IsConnected() ) continue;
		if ( player->GetTeamNumber() == TEAM_INFECTED ) player->ShowViewPortPanel( "zombie_intro", false );
		player->ShowViewPortPanel( "ready_countdown", false );
		player->DeactivateMovementConstraint();
	}
	if ( TerrorGameRules()->IsScavengeMode() ) m_pScavengeMode->StartReadyCountdown();
}
void CDirector::OnPlayersConnected()
{
	// S:262AC0, invoked immediately by native2659A0 when not frozen, or
	// after the delayed unfreeze. No session/team creation happens here.
	SpawnScavengeItems();
	CInfoDirector *entity = dynamic_cast< CInfoDirector * >( gEntList.FindEntityByClassname( NULL, "info_director" ) );
	if ( entity ) entity->m_OnGameplayStart.FireOutput( NULL, entity, 0.0f );
	m_pScavengeMode->OnGameplayStart();
	m_pSurvivalMode->OnGameplayStart();
	m_pScriptedMode->OnGameplayStart();
}
void CDirector::DrawSessionDebugLine( int line, const char *text, int red, int green, int blue )
{
	DirectorDebugText( 0.01f, 0.0f, line, text, red, green, blue, 255, 0.01023f );
}
class RemoveSessionNextBot
{
public:
	bool operator()( INextBot *bot ) const
	{
		CBaseCombatCharacter *entity = bot->GetEntity();
		if ( entity->IsPlayer() )
		{
			CBasePlayer *player = static_cast< CBasePlayer * >( entity );
			char command[256];
			Q_snprintf( command, sizeof( command ), "kick \"%s\"\n", player->GetPlayerName() );
			engine->ServerCommand( command );
		}
		else UTIL_Remove( entity );
		return true;
	}
};
void CDirector::OnNoHumanPlayersLeft()
{
	RemoveSessionNextBot remove;
	TheNextBots().ForEachBot( remove );
	if ( engine->IsDedicatedServer() ) RestartAfterNoHumanPlayers(); // S:271BA0
}
class CollectLowViolenceInfected
{
public:
	explicit CollectLowViolenceInfected( CUtlVector< Infected * > *result ) : m_result( result ) {}
	bool operator()( INextBot *bot ) const
	{
		// CTerrorPlayer's native virtual1408 is nullsub382; Infected is the
		// real overriding actor. Do not instantiate an empty RTTI identity.
		Infected *infected = dynamic_cast< Infected * >( bot->GetEntity() );
		if ( infected && !infected->IsEFlagSet( EFL_KILLME ) && !bot->GetBodyInterface()->IsArousal( IBody::INTENSE ) )
			m_result->AddToTail( infected );
		return true;
	}
private:
	CUtlVector< Infected * > *m_result;
};
void CDirector::UpdateLowViolenceCombatCharacters()
{
	CUtlVector< Infected * > infected;
	CollectLowViolenceInfected collect( &infected );
	TheNextBots().ForEachBot( collect );
	FOR_EACH_VEC( infected, i ) infected[i]->OnLowViolence();
}
void Infected::OnLowViolence()
{
	// S:3FDFD0, including the original model-specific restriction.
	if ( !Q_stricmp( STRING( GetModelName() ), "models/infected/common_police_male01.mdl" ) )
	{
		CTakeDamageInfo info( this, this, 2000.0f, DMG_CLUB );
		TakeDamage( info );
	}
}

// S25F710's player eligibility, then S25E1D0's stale reservation callback.
// No alive or spectator-team test exists for the human lookup.
static int CountStaleHumanSpectatorBots()
{
	int count = 0;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CBasePlayer *base = UTIL_PlayerByIndex( i );
		if ( !base || !base->edict() || !base->entindex() || !base->IsPlayer() || !base->IsConnected() ) continue;
		CTerrorPlayer *player = static_cast< CTerrorPlayer * >( base );
		if ( player->GetTeamNumber() != TEAM_SURVIVOR || !player->IsBot() ) continue;
		SurvivorBot *bot = dynamic_cast< SurvivorBot * >( player );
		if ( !bot ) continue;
		const int userID = bot->GetHumanSpectatorUserID();
		if ( userID <= 0 ) continue;
		CBasePlayer *human = UTIL_PlayerByUserId( userID );
		if ( !human || !human->IsPlayer() ) ++count;
	}
	return count;
}

// S262670. Count the actual roster, without pending transition records.
int CDirector::GetTeamRosterCount( int teamNumber, int countMode ) const
{
	if ( countMode == 0 ) return CountPlayersOnTeam( teamNumber, false );
	// Native26269A: callers require a valid global team.
	CTeam *team = GetGlobalTeam( teamNumber );
	Assert( team );
	uint32 count = static_cast< uint32 >( team->GetNumPlayers() );
	if ( teamNumber == TEAM_SURVIVOR ) count -= static_cast< uint32 >( CountStaleHumanSpectatorBots() );
	return static_cast< int >( count );
}

// S2625F0. countMode is NOT aliveOnly. Zero counts only human roster slots;
// other modes count the complete actual team, minus stale survivor reservations.
int CDirector::GetTeamPlayerCount( int teamNumber, int countMode ) const
{
	int saved, restoring;
	GetSavedPlayerStates()->GetRestoringPlayerCounts( teamNumber, &saved, &restoring );
	const uint32 pending = static_cast< uint32 >( saved ) - static_cast< uint32 >( restoring );
	return static_cast< int >( pending + static_cast< uint32 >( GetTeamRosterCount( teamNumber, countMode ) ) );
}

// S2626D0. Evaluate the population BEFORE policy/type branches, as in native.
bool CDirector::IsTeamFull( int team, int countMode ) const
{
	const int count = GetTeamPlayerCount( team, countMode );
	if ( team == TEAM_SURVIVOR )
	{
		static ConVarRef survivor_limit( "survivor_limit" );
		return count >= survivor_limit.GetInt();
	}
	if ( team != TEAM_INFECTED ) return false;
	if ( countMode == 2 ) return count >= static_cast< int >( static_cast< uint32 >( GetMaxSpecialCount() ) + 1u );
	// S2D9B70 is playercontrolledzombies>0; S2D9AB0 is base=="scavenge", NOT this gate.
	if ( countMode != 0 || TerrorGameRules()->IsPlayerControlledZombies() ) return count >= GetMaxSpecialCount();
	return true;
}
