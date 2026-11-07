//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/director/director_versus_mode.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/terror_shareddefs.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/entities/trigger_finale.h"
#include "tier1/KeyValues.h"
#include "l4d2/director/director_session_manager.h"
#include "l4d2/nav/terror_nav.h"
#include "l4d2/nav/escape_route.h"
#include "l4d2/director/zombiemanager.h"
#include "l4d2/entities/zombie_border.h"
#include "l4d2/entities/info_changelevel.h"
#include "l4d2/player/bots/survivor_bot.h"
#include "BasePropDoor.h"
#include "datacache/imdlcache.h"
#include "team.h"
#include "tier0/memdbgon.h"

static ConVar versus_boss_debug( "versus_boss_debug", "0", 0x00004004, "Enable debug output for versus tank/witch spawning logic" );
static ConVar versus_boss_flow_min( "versus_boss_flow_min", "0.25", 0x00004004, "Min fraction of map flow for tank/witch spawn location" );
static ConVar versus_boss_flow_max( "versus_boss_flow_max", "0.9", 0x00004004, "Max fraction of map flow for tank/witch spawn location" );
static ConVar versus_boss_flow_min_intro( "versus_boss_flow_min_intro", "0.5", 0x00004004, "Min fraction of map flow for tank/witch spawn location within the first map of a campaign" );
static ConVar versus_boss_flow_max_intro( "versus_boss_flow_max_intro", "0.75", 0x00004004, "Max fraction of map flow for tank/witch spawn location within the first map of a campaign" );
static ConVar versus_boss_flow_min_finale( "versus_boss_flow_min_finale", "0.25", 0x00004004, "Min fraction of map flow for tank/witch spawn location within the finale map of a campaign" );
static ConVar versus_boss_flow_max_finale( "versus_boss_flow_max_finale", "0.6", 0x00004004, "Max fraction of map flow for tank/witch spawn location within the finale map of a campaign" );
static ConVar versus_tank_chance( "versus_tank_chance", "0.75", 0x00004004, "Chance of a tank spawning within a regular versus map" );
static ConVar versus_witch_chance( "versus_witch_chance", "0.75", 0x00004004, "Chance of a witch spawning within a regular versus map" );
static ConVar versus_tank_chance_intro( "versus_tank_chance_intro", "0.3", 0x00004004, "Chance of a tank spawning within the first map of a campaign" );
static ConVar versus_witch_chance_intro( "versus_witch_chance_intro", "0.3", 0x00004004, "Chance of a witch spawning within the first map of a campaign" );
static ConVar versus_tank_chance_finale( "versus_tank_chance_finale", "0.1", 0x00004004, "Chance of a tank spawning within the finale map of a campaign" );
static ConVar versus_witch_chance_finale( "versus_witch_chance_finale", "0.1", 0x00004004, "Chance of a witch spawning within the finale map of a campaign" );
static ConVar versus_tank_flow_team_variation( "versus_tank_flow_team_variation", "0.1", 0x00004004, "Window of flow distance that the tank can spawn in for the second team in versus" );
static ConVar versus_witch_flow_team_variation( "versus_witch_flow_team_variation", "0.0", 0x00004004, "Window of flow distance that the witch can spawn in for the second team in versus" );
static ConVar versus_boss_padding_min( "versus_boss_padding_min", "0.1", 0x00004004, "How far apart the tank/witch have to be along the flow" );
static ConVar versus_boss_padding_max( "versus_boss_padding_max", "0.3", 0x00004004, "How far apart the tank/witch have to be along the flow" );
static ConVar versus_force_start_time( "versus_force_start_time", "90", 0x00000006, "Starts versus game after this amount of time, even if survivors are still inside the safe room", true, 10.0f, false, 10000.0f );
static ConVar versus_marker_num( "versus_marker_num", "3", 0x00000006, "Number of travel markers we expose to survivors in Versus mode." );

CDirectorVersusMode::CDirectorVersusMode()
{
	m_bRoundStarted = m_bTeamsFlipped = m_bShutdownScoreboardShown = m_bIsVoteRestarting = false;
	Q_memset( m_TeamScore, 0, sizeof( m_TeamScore ) ); Q_memset( m_SavedTeamScore, 0, sizeof( m_SavedTeamScore ) );
	Q_memset( m_TankFlow, 0, sizeof( m_TankFlow ) ); Q_memset( m_WitchFlow, 0, sizeof( m_WitchFlow ) );
	Q_memset( m_bTankPending, 0, sizeof( m_bTankPending ) ); Q_memset( m_bWitchPending, 0, sizeof( m_bWitchPending ) );
	m_flFinaleTravelCompletion = 0.0f; m_nFinaleType = 0; m_bFinaleMap = false;
	m_nTravelMarker = m_nTravelMarkerCount = 0;
}
bool CDirectorVersusMode::IsActive() const { return g_TerrorModes.IsBaseMode( "versus" ); } // Native290660 ->2D99D0; valid before Rules exists.
bool CDirectorVersusMode::CanChangeLevel() const
{
	// S290680 calls the same early-lifetime 2D99D0 mode provider as IsActive.
	return !IsActive();
}
void CDirectorVersusMode::OnNewGame() { m_bTeamsFlipped = false; m_bIsVoteRestarting = false; }
void CDirectorVersusMode::Reset() { m_ForceStartTimer.Invalidate(); }
void CDirectorVersusMode::ResetCampaignScores( bool clear )
{
	if ( clear ) { m_TeamScore[0] = m_SavedTeamScore[0] = m_TeamScore[1] = m_SavedTeamScore[1] = 0; }
}
bool CDirectorVersusMode::SwapRoles() { m_bTeamsFlipped = !m_bTeamsFlipped; return m_bTeamsFlipped; }
void CDirectorVersusMode::OnLevelChangeComplete()
{
	ConfigureBossSpawns();
	if ( TheDirector->ShouldResetTeamsOnMissionStart() || !TheDirector->IsChangingMapOrMission() ) m_bTeamsFlipped = false;
	m_bShutdownScoreboardShown = false;
	CFinaleTrigger *finale = dynamic_cast< CFinaleTrigger * >( gEntList.FindEntityByClassname( NULL, "trigger_finale" ) );
	m_bFinaleMap = finale != NULL;
	if ( finale ) { m_nFinaleType = finale->GetFinaleType(); m_flFinaleTravelCompletion = finale->GetVersusTravelCompletion(); }
	else { m_nFinaleType = 0; m_flFinaleTravelCompletion = 0.0f; }
}

void CDirectorVersusMode::OnFinaleTriggerSpawned()
{
	// S2908A0 writes the same +68/+64/+60 fields as OnLevelChangeComplete.
	CFinaleTrigger *finale = dynamic_cast< CFinaleTrigger * >( gEntList.FindEntityByClassname( NULL, "trigger_finale" ) );
	m_bFinaleMap = finale != NULL;
	if ( finale )
	{
		m_nFinaleType = finale->GetFinaleType();
		m_flFinaleTravelCompletion = finale->GetVersusTravelCompletion();
	}
	else
	{
		m_nFinaleType = 0;
		m_flFinaleTravelCompletion = 0.0f;
	}
}
static float ClampVersusBossFlow( float value, float minimum, float maximum )
{
	return minimum <= value ? ( value > maximum ? maximum : value ) : minimum;
}
void CDirectorVersusMode::ConfigureBossSpawns()
{
	// S290900: sample chapter-type policy, then true mounted chapter override,
	// then independent witch/tank rolls. Do not collapse these RNG calls.
	m_bTankPending[0] = m_bTankPending[1] = m_bWitchPending[0] = m_bWitchPending[1] = false;
	float minimum = versus_boss_flow_min.GetFloat(), maximum = versus_boss_flow_max.GetFloat();
	float tankChance = versus_tank_chance.GetFloat(), witchChance = versus_witch_chance.GetFloat();
	bool allowMix = true;
	int type = TheDirector->GetCampaignMapType();
	if ( type == 0 )
	{
		minimum = versus_boss_flow_min_intro.GetFloat(); maximum = versus_boss_flow_max_intro.GetFloat();
		tankChance = versus_tank_chance_intro.GetFloat(); witchChance = versus_witch_chance_intro.GetFloat(); allowMix = false;
	}
	else if ( type == 4 )
	{
		minimum = versus_boss_flow_min_finale.GetFloat(); maximum = versus_boss_flow_max_finale.GetFloat();
		tankChance = versus_tank_chance_finale.GetFloat(); witchChance = versus_witch_chance_finale.GetFloat(); allowMix = false;
	}
	KeyValues *chapter = TerrorGameRules()->GetChapterInfo();
	KeyValues *policy = chapter ? chapter->FindKey( "versus_boss_spawning" ) : NULL;
	if ( policy )
	{
		minimum = policy->GetFloat( "spawn_pos_min", minimum ); maximum = policy->GetFloat( "spawn_pos_max", maximum );
		tankChance = policy->GetFloat( "tank_chance", tankChance ); witchChance = policy->GetFloat( "witch_chance", witchChance );
		allowMix = policy->GetInt( "allow_boss_mix", allowMix ) != 0;
	}
	bool witch = witchChance > RandomFloat( 0.0f, 1.0f );
	bool tank = tankChance > RandomFloat( 0.0f, 1.0f );
	if ( witch )
	{
		if ( !allowMix ) tank = false;
		m_WitchFlow[0] = RandomFloat( minimum, maximum );
		float half = versus_witch_flow_team_variation.GetFloat() * 0.5f;
		float sampled = RandomFloat( m_WitchFlow[0] - half, m_WitchFlow[0] + half );
		m_WitchFlow[1] = ClampVersusBossFlow( sampled, minimum, maximum );
		m_bWitchPending[0] = m_bWitchPending[1] = true;
	}
	if ( tank )
	{
		m_TankFlow[0] = RandomFloat( minimum, maximum );
		float half = versus_tank_flow_team_variation.GetFloat() * 0.5f;
		float sampled = RandomFloat( m_TankFlow[0] - half, m_TankFlow[0] + half );
		m_TankFlow[1] = ClampVersusBossFlow( sampled, minimum, maximum );
		m_bTankPending[0] = m_bTankPending[1] = true;
		if ( witch ) for ( int team = 0; team < 2; ++team )
		{
			float padding = RandomFloat( versus_boss_padding_min.GetFloat(), versus_boss_padding_max.GetFloat() );
			float tankFlow = m_TankFlow[team], witchFlow = m_WitchFlow[team];
			float upper = maximum - padding;
			if ( tankFlow <= witchFlow )
			{
				tankFlow = ClampVersusBossFlow( tankFlow, minimum, upper );
				float next = tankFlow + padding;
				if ( next <= witchFlow ) next = witchFlow <= maximum ? witchFlow : maximum;
				witchFlow = next;
			}
			else
			{
				witchFlow = ClampVersusBossFlow( witchFlow, minimum, upper );
				float next = witchFlow + padding;
				if ( next <= tankFlow ) next = tankFlow <= maximum ? tankFlow : maximum;
				tankFlow = next;
			}
			m_TankFlow[team] = tankFlow; m_WitchFlow[team] = witchFlow;
		}
	}
	if ( versus_boss_debug.GetBool() ) Msg( "Versus Boss: MinFlow %.2f MaxFlow %.2f - Team 0: Tank %.2f Witch %.2f - Team 1: Tank %.2f Witch %.2f\n",
		minimum, maximum, m_bTankPending[0] ? m_TankFlow[0] : -1.0f, m_bWitchPending[0] ? m_WitchFlow[0] : -1.0f,
		m_bTankPending[1] ? m_TankFlow[1] : -1.0f, m_bWitchPending[1] ? m_WitchFlow[1] : -1.0f );
}
void CDirectorVersusMode::Update()
{
	if ( !IsActive() ) return;
	UpdateBossSpawning();
	if ( gpGlobals->curtime >= m_MarkerTimer.m_timestamp ) UpdateTravelMarkers();
}
void CDirectorVersusMode::UpdateMissionEnd()
{
	if ( !IsActive() ) return;
	if ( m_ForceStartTimer.m_timestamp > 0.0f )
	{
		if ( gpGlobals->curtime >= m_ForceStartTimer.m_timestamp && !TheDirector->HasAnySurvivorLeftSafeArea() ) ForceSurvivorsOutOfCheckpoint();
	}
	else m_ForceStartTimer.Start( versus_force_start_time.GetFloat() );
}
void CDirectorVersusMode::HideScoreboards()
{
	if ( !IsActive() ) return;
	TerrorShowViewPortPanel( -1, "fullscreen_vs_scoreboard", false, NULL );
	TerrorShowViewPortPanel( -1, "fullscreen_vs_results", false, NULL );
}
void CDirectorVersusMode::EndRound()
{
	if ( m_bShutdownScoreboardShown )
	{
		TheDirector->NotifyMissionWon();
		TheDirector->FinishScenarioExit();
	}
	else if ( TheDirector->GetRoundEndReason() == 8 )
	{
		KeyValues *scores = new KeyValues( "scores" );
		scores->SetInt( "t1", m_TeamScore[0] ); scores->SetInt( "t2", m_TeamScore[1] );
		TerrorShowViewPortPanel( -1, "fullscreen_vs_scoreboard", true, scores );
		TerrorShowViewPortPanel( -1, "info_window", false, NULL );
		scores->deleteThis();
		TheDirector->SetMissionEndDelay( 12.0f );
		m_bShutdownScoreboardShown = true;
	}
	else if ( TheDirector->IsInSecondHalfOfRound() )
	{
		TheDirector->EndRound( 6 );
		m_SavedTeamScore[0] = m_TeamScore[0]; m_SavedTeamScore[1] = m_TeamScore[1];
	}
	else TheDirector->EndRound( 0 );
}
void CDirectorVersusMode::AccumulateCampaignScores()
{
	if ( !g_pGameRules ) return;
	for ( int team = 0; team < 2; ++team ) m_TeamScore[team] += TerrorGameRules()->GetVersusScore( team + 1, false );
	TerrorGameRules()->AccumulateVersusChapterScores();
	TerrorGameRules()->SetVersusCampaignScores( m_TeamScore[0], m_TeamScore[1] );
}
void CDirectorVersusMode::OnGameplayStart()
{
	if ( !TerrorGameRules()->IsPlayerControlledZombies() ) return;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( player ) player->ResetVersusDamageStatistics();
	}
	if ( IsActive() ) m_bRoundStarted = true;
	TerrorGameRules()->SetVersusCampaignScores( m_TeamScore[0], m_TeamScore[1] );
	m_nTravelMarker = 0;
	m_nTravelMarkerCount = versus_marker_num.GetInt();
	if ( m_nTravelMarkerCount > 0 ) m_MarkerTimer.Start( 1.0f );
}
void CDirectorVersusMode::UpdateTravelMarkers()
{
	if ( m_nTravelMarker >= m_nTravelMarkerCount || m_nTravelMarkerCount <= 0 ) return;
	int maximum = -1, userID = -1;
	for ( int character = 0; character < 4; ++character )
	{
		int score = TerrorGameRules()->GetVersusDistancePerSurvivor( character, TEAM_SURVIVOR );
		if ( score <= maximum ) continue;
		maximum = score;
		CTerrorPlayer *player = FindSurvivorByCharacter( static_cast< SurvivorCharacterType >( MapTerrorSurvivorCharacter( character ) ) );
		if ( player ) userID = player->GetUserID();
	}
	float percent = ( (float)maximum / (float)( TerrorGameRules()->GetVersusMaxCompletionScore() >> 2 ) ) * 100.0f;
	float next = ( 100.0f / (float)( m_nTravelMarkerCount + 1 ) ) * (float)( m_nTravelMarker + 1 );
	if ( percent >= next )
	{
		++m_nTravelMarker;
		if ( userID > 0 && percent < 80.0f )
		{
			IGameEvent *event = gameeventmanager->CreateEvent( "versus_marker_reached" );
			if ( event ) { event->SetInt( "userid", userID ); event->SetInt( "marker", (int)next ); gameeventmanager->FireEvent( event ); }
		}
	}
	m_MarkerTimer.Start( m_MarkerTimer.GetCountdownDuration() );
}

static ConVar versus_boss_buffer( "versus_boss_buffer", "2200", 0x00004004 );
static bool IsBossSpawnHullClear( const Vector &position )
{
	// Native359400, actual Rules view-vector hull, no invented allsolid test.
	const Vector &maximum = g_pGameRules->GetViewVectors()->m_vHullMax;
	const Vector &minimum = g_pGameRules->GetViewVectors()->m_vHullMin;
	trace_t trace; UTIL_TraceHull( position, position, minimum, maximum, 0x0202420B, NULL, COLLISION_GROUP_NONE, &trace );
	return !trace.startsolid && trace.fraction >= 1.0f;
}
void CDirectorVersusMode::UpdateBossSpawning()
{
	if ( TheDirector->AreBossesProhibited() ) return;
	int team = TerrorGameRules()->GetScavengeTeamIndex( TEAM_SURVIVOR ) - 1;
	if ( (unsigned int)team > 1 ) return;
	int disallowed = TheDirector->GetDirectorOptionInt( "DisallowThreatType", 9 );
	if ( disallowed == 8 ) m_bTankPending[team] = false;
	else if ( disallowed == 7 ) m_bWitchPending[team] = false;
	if ( !m_bTankPending[team] && !m_bWitchPending[team] ) return;
	if ( !TheDirector->HasAnySurvivorLeftSafeArea() ) return;
	TerrorNavMesh *mesh = static_cast< TerrorNavMesh * >( TheNavMesh );
	if ( mesh->GetMaxFlowDistance() <= 0.0f || TheDirector->GetScenario()->GetFinaleType() != 5 || TheDirector->IsInTransition() ||
		TheDirector->IsMissionLostOrWon() || TheDirector->GetSessionManager()->IsTeamFrozen() || TheDirector->GetTankCount() > 0 ) return;
	CTerrorPlayer *leader = FindHighestFlowSurvivor( 0 );
	float highestFlow = leader ? ToTerrorNavArea( leader->GetLastKnownArea() )->GetFlowDistance() : -9999.0f;
	float fraction = highestFlow / mesh->GetMaxFlowDistance();
	if ( versus_boss_debug.GetBool() )
		Msg( " Highest survivor flow distance = %f  Tank at = %f witch at %f\n", fraction,
			m_bTankPending[team] ? m_TankFlow[team] : -1.0f, m_bWitchPending[team] ? m_WitchFlow[team] : -1.0f );
	if ( m_bTankPending[team] && versus_boss_buffer.GetFloat() + highestFlow >= mesh->GetMaxFlowDistance() * m_TankFlow[team] && GetCriticalPath() )
	{
		float at = ( versus_boss_buffer.GetFloat() + highestFlow ) / mesh->GetMaxFlowDistance();
		Vector position = GetCriticalPath()->GetPositionAtFlow( at );
		// Actual48B4C0 applies ONLY to Tank and returns from the WHOLE update.
		if ( CZombieBorder::IsPointBeyondBorder( position ) ) return;
		TerrorNavArea *area = dynamic_cast< TerrorNavArea * >( mesh->GetNavArea( position, 120.0f ) );
		CNavArea::MakeNewMarker();
		while ( area && !area->IsMarked() )
		{
			if ( area->IsValidForWanderingPopulation() && !area->IsUnderwater() )
			{
				Vector center = area->GetCenter(); center.z += 10.0f;
				if ( IsBossSpawnHullClear( center ) && !area->IsPotentiallyVisibleToAnyCharacter() )
				{
					QAngle angles( 0.0f, RandomFloat( 0.0f, 360.0f ), 0.0f );
					CTerrorPlayer *tank = TheZombieManager->SpawnTankInArea( area, angles, false );
					if ( tank )
					{
						ClearWanderingPopulationAroundBoss( area );
						TheDirector->OfferTank( tank, true ); m_bTankPending[team] = false;
					}
					break; // Native also stops searching after a FAILED actual spawn.
				}
			}
			area->Mark(); area = area->GetNextHighestFlowConnectedArea();
		}
		if ( !m_bTankPending[team] ) return; // Successful Tank skips Witch this frame.
		double retry = (double)fraction + 0.1;
		m_TankFlow[team] = retry < 0.99 ? (float)retry : (float)0.99; // Unordered chooses cap.
	}
	if ( m_bWitchPending[team] && versus_boss_buffer.GetFloat() + highestFlow >= mesh->GetMaxFlowDistance() * m_WitchFlow[team] && GetCriticalPath() )
	{
		float at = ( versus_boss_buffer.GetFloat() + highestFlow ) / mesh->GetMaxFlowDistance();
		Vector position = GetCriticalPath()->GetPositionAtFlow( at );
		TerrorNavArea *area = dynamic_cast< TerrorNavArea * >( mesh->GetNavArea( position, 120.0f ) );
		CNavArea::MakeNewMarker();
		while ( area && !area->IsMarked() )
		{
			if ( area->IsValidForWanderingPopulation() && !area->IsUnderwater() )
			{
				Vector center = area->GetCenter(); center.z += 10.0f;
				if ( IsBossSpawnHullClear( center ) && !area->IsPotentiallyVisibleToAnyCharacter() )
				{
					QAngle angles( 0.0f, RandomFloat( 0.0f, 360.0f ), 0.0f );
					if ( TheZombieManager->SpawnWitchInArea( area, angles, false ) ) { ClearWanderingPopulationAroundBoss( area ); m_bWitchPending[team] = false; }
					break;
				}
			}
			area->Mark(); area = area->GetNextHighestFlowConnectedArea();
		}
		if ( m_bWitchPending[team] ) { double retry = (double)fraction + 0.1; m_WitchFlow[team] = retry < 0.99 ? (float)retry : (float)0.99; }
	}
}

void CDirectorVersusMode::OnRoundStarted()
{
	if ( g_pGameRules ) TerrorGameRules()->SetRoundStartTime( gpGlobals->curtime );
	IGameEvent *event = gameeventmanager->CreateEvent( "versus_round_start" );
	if ( event ) gameeventmanager->FireEvent( event );
}
void CDirectorVersusMode::FinishRound()
{
	m_bIsVoteRestarting = false;
	if ( TerrorGameRules()->IsScavengeSecondHalf() ) TheDirector->SwapTeams();
	ClearTerrorTransitionLandmark();
	const char *map = STRING( gpGlobals->mapname ); if ( !map ) map = "";
	TheDirector->OnRoundEnd( false );
	TerrorGameRules()->PrepareForChangeLevel( map );
	ChangeTerrorLevel( map, NULL );
}

void CDirectorVersusMode::FireGameEvent( IGameEvent *event )
{
	if ( IsActive() && !Q_stricmp( event->GetName(), "map_transition" ) ) FinishVersusRound( true );
}

void CDirectorVersusMode::FinishVersusRound( bool success )
{
	if ( !IsActive() || !m_bRoundStarted ) return;
	TerrorGameRules()->UpdateVersusSurvivalMultiplier( success );
	TerrorGameRules()->SetRoundEndTime( gpGlobals->curtime );
	TerrorGameRules()->CacheVersusScores( true );
	bool tiebreak = false;
	if ( TerrorGameRules()->IsScavengeSecondHalf() &&
		TerrorGameRules()->GetVersusSurvivalMultiplier( TEAM_SURVIVOR ) == 4 &&
		TerrorGameRules()->GetVersusSurvivalMultiplier( TEAM_INFECTED ) == 4 &&
		TerrorGameRules()->GetVersusDefibsUsed( TEAM_SURVIVOR ) == TerrorGameRules()->GetVersusDefibsUsed( TEAM_INFECTED ) )
	{
		TerrorGameRules()->BreakVersusTie(); tiebreak = true;
	}
	KeyValues *scores = new KeyValues( "scores" );
	scores->SetInt( "t1", m_TeamScore[0] ); scores->SetInt( "t2", m_TeamScore[1] );
	scores->SetInt( "c1", TerrorGameRules()->GetVersusChapterScore( 1 ) );
	scores->SetInt( "c2", TerrorGameRules()->GetVersusChapterScore( 2 ) );
	scores->SetInt( "tiebreak", tiebreak );
	TerrorShowViewPortPanel( -1, "fullscreen_vs_scoreboard", true, scores );
	TerrorShowViewPortPanel( -1, "info_window", false, NULL ); scores->deleteThis();
	AccumulateCampaignScores();
	TheDirector->EndRound( 5 ); m_bRoundStarted = false;
	if ( TheDirector->IsInSecondHalfOfRound() && TerrorGameRules()->IsFinalChapter() )
	{
		IGameEvent *event = gameeventmanager->CreateEvent( "versus_match_finished", true );
		if ( event )
		{
			event->SetInt( "winners", m_TeamScore[1] < m_TeamScore[0] ? 1 : ( m_TeamScore[1] > m_TeamScore[0] ? 2 : 0 ) );
			gameeventmanager->FireEvent( event );
		}
	}
}

CTerrorPlayer *PickLivingSurvivorForForceStart()
{
	// Native28F340 stateless selector, actual CTeam registration order.
	CTeam *team = GetGlobalTeam( TEAM_SURVIVOR );
	int living = 0;
	for ( int i = 0; i < team->GetNumPlayers(); ++i ) if ( team->GetPlayer( i )->IsAlive() ) ++living;
	if ( !living ) return NULL;
	int selected = RandomInt( 0, living - 1 ), ordinal = 0;
	for ( int i = 0; i < team->GetNumPlayers(); ++i )
	{
		if ( !team->GetPlayer( i )->IsAlive() ) continue;
		if ( selected != ordinal ) { ++ordinal; continue; }
		CBasePlayer *player = team->GetPlayer( i );
		return player && player->IsPlayer() ? ToTerrorPlayer( player ) : NULL;
	}
	return NULL;
}
void CDirectorVersusMode::ForceSurvivorsOutOfCheckpoint()
{
	CTerrorPlayer *survivor = PickLivingSurvivorForForceStart();
	if ( !survivor ) return;
	bool retry = false;
	for ( CBaseEntity *entity = gEntList.FindEntityByClassname( NULL, "prop_door_rotating_checkpoint" ); entity;
		entity = gEntList.FindEntityByClassname( entity, "prop_door_rotating_checkpoint" ) )
	{
		CBasePropDoor *door = dynamic_cast< CBasePropDoor * >( entity );
		// Native956 reads6080 = HAS UNLOCK SEQUENCE, not IsCheckpointDoor.
		if ( door->HasUnlockSequence() && door->IsDoorClosed() && !( door->GetSpawnFlags() & 0x8000 ) )
		{
			MDLCACHE_CRITICAL_SECTION();
			if ( door->IsDoorLocked() ) door->Unlock( survivor );
			else
			{
				variant_t empty; door->AcceptInput( "PlayerOpen", survivor, survivor, empty, 0 );
				if ( !door->IsDoorClosed() ) continue;
			}
			retry = true;
		}
	}
	if ( retry ) m_ForceStartTimer.Start( 0.8f );
	else TheDirector->OnSurvivorLeftSafeArea( survivor );
}
bool CDirectorVersusMode::OnScenarioExit()
{
	// S291AA0 intentionally writes the global singleton, not a supplied owner.
	TheDirector->InvalidateMissionEndTimer();
	if ( !IsActive() ) return true;
	FinishVersusRound( true );
	return false;
}

void CDirectorVersusMode::RestartScenarioFromVote()
{
	// Native290530 bypasses EndRound269130's timer guard and round_end event.
	m_bIsVoteRestarting = true;
	m_TeamScore[0] = m_SavedTeamScore[0];
	m_TeamScore[1] = m_SavedTeamScore[1];
	TerrorGameRules()->SetVersusCampaignScores( m_TeamScore[0], m_TeamScore[1] );
	TheDirector->RestartVersusRoundFromVote();
}

#include "l4d2/director/director_versus_mode_console_server_callbacks.inc"
