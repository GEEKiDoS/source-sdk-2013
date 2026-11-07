//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/director/director_scenario.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/sound/terror_music.h"
#include "l4d2/nav/terror_nav_area.h"
#include "l4d2/entities/trigger_finale.h"
#include "l4d2/director/zombiemanager.h"
#include "l4d2/director/zombie_spawn.h"
#include "l4d2/infected/boss/witch.h"
#include "NextBot/NextBotManager.h"
#include "NextBot/NextBotBodyInterface.h"
#include "igameevents.h"
#include "l4d2/director/director_versus_mode.h"
#include "l4d2/director/director_survival_mode.h"
#include "tier1/KeyValues.h"
#include "recipientfilter.h"
#include "engine/IEngineSound.h"
#include "l4d2/stats/l4d_gamestats.h"
#include <time.h>
#include <emmintrin.h>
#include "l4d2/director/director_scenario_console_server_cvars.inc"
#include "tier0/memdbgon.h"

// Pinned Tier0 S:9EF0. Keep its independent second clock sample, local-time
// conversion, zero-based tm_yday, and original unguarded CRT result.
static void GetCurrentDayOfTheYear( int *day )
{
	__time64_t now;
	_time64( &now );
	*day = _localtime64( &now )->tm_yday;
}

static ConVar director_custom_finale_tank_spacing( "director_custom_finale_tank_spacing", "10.0", 0x00004004 );
static ConVar director_finale_panic_waves( "director_finale_panic_waves", "2", 0x00004004 );
static ConVar director_finale_stage_delay( "director_finale_stage_delay", "5", 0x00004004 );
static ConVar director_gauntlet_movement_bonus( "director_gauntlet_movement_bonus", "5.0", 0x00004004, "If you don't cross the movement threshold in DirectorGauntletMovementTimer seconds, you will get this much bonus time added between mobs." );
static ConVar director_gauntlet_movement_bonus_max( "director_gauntlet_movement_bonus_max", "30.0", 0x00004004, "Maximum amount of bonus time you can accumulate from not moving." );
static ConVar director_gauntlet_movement_threshold( "director_gauntlet_movement_threshold", "500.0", 0x00004004, "If you move less than this in DirectorGauntletMovementTimer seconds, you will get an increasing break between mobs." );
static ConVar director_gauntlet_movement_timer_length( "director_gauntlet_movement_timer_length", "10.0", 0x00004004, "If you don't cross the movement threshold in this amount of time, you get increasing break time added between mobs." );
static ConVar director_gauntlet_tank_kite_distance( "director_gauntlet_tank_kite_distance", "3000.0", 0x00004004, "How far you have to progress after a gauntlet tank spawn before the horde fires up again." );
static ConVar director_panic_forever( "director_panic_forever", "0", 0x00004004, "Panic events never end" );
static ConVar director_panic_wave_pause_min( "director_panic_wave_pause_min", "5", 0x00004004 );
static ConVar director_panic_wave_pause_max( "director_panic_wave_pause_max", "7", 0x00004004 );
static ConVar director_clear_count( "director_clear_count", "2.0", 0x00004004 );
static ConVar director_special_battlefield_respawn_interval( "director_special_battlefield_respawn_interval", "10", 0x00004004 );
ConVar z_common_limit( "z_common_limit", "30", 0x00004004, "How many common infecteds we can have at once." );
ConVar z_mega_mob_size( "z_mega_mob_size", "50", 0x00004004 );
static ConVar z_mob_spawn_min_size( "z_mob_spawn_min_size", "10", 0x00004004 );

namespace
{
bool IsSinglePlayerScenarioMode()
{
	KeyValues *mode = TheDirector->GetModeDescription()->GetCurrentMode();
	return mode && mode->GetInt( "maxplayers", 0 ) == 1;
}
// Native slot callbacks retain Disconnecting as well as Connected players.
CTerrorPlayer *ScenarioPlayer( int index )
{
	CBasePlayer *player = UTIL_PlayerByIndex( index );
	if ( !player || !player->edict() || !player->entindex() || !player->IsPlayer() || !player->IsConnected() ) return NULL;
	return static_cast< CTerrorPlayer * >( player );
}
CBaseEntity *FindFinaleTrigger()
{
	CBaseEntity *entity = gEntList.FindEntityByClassname( NULL, "trigger_finale" );
	return entity ? entity : gEntList.FindEntityByClassname( NULL, "finale_trigger" );
}
void FinaleInput( const char *name )
{
	CBaseEntity *entity = FindFinaleTrigger();
	if ( entity ) entity->AcceptInput( name, NULL, NULL, variant_t(), 0 );
}
void ScenarioEvent( const char *name )
{
	IGameEvent *event = gameeventmanager->CreateEvent( name );
	if ( event ) gameeventmanager->FireEvent( event );
}
void ApplySpawnDirections()
{
	int count = TheDirector->GetDirectorOptionInt( "SpawnDirectionCount", 0 );
	int mask = TheDirector->GetDirectorOptionInt( "SpawnDirectionMask", 0 );
	if ( count != 0 )
	{
		if ( mask != 0 && mask != 255 ) Warning( "WARNING: Hey, you set a spawn count (%d) and mask (%x) in script... only set 1\n", count, mask );
		if ( (unsigned int)count > 7 )
		{
			Warning( "WARNING: Ummm, i cant spawn from %d directions, fool... there are only 8\n", count );
			TheZombieManager->SetSpawnCompassMask( 0 );
			return;
		}
		mask = 0;
		for ( int i = 0; i < count; ++i )
		{
			int direction;
			do { direction = 1 << RandomInt( 0, 7 ); } while ( mask & direction );
			mask |= direction;
		}
	}
	TheZombieManager->SetSpawnCompassMask( (unsigned char)mask );
}
class RemoveUnseenFinaleActors
{
public:
	bool operator()( INextBot *bot )
	{
		CBaseCombatCharacter *actor = bot->GetEntity();
		if ( actor->IsAlive() && !dynamic_cast< Witch * >( actor ) && actor->GetTeamNumber() != TEAM_L4D1_SURVIVOR &&
			!bot->GetBodyInterface()->IsArousal( IBody::INTENSE ) &&
			!IsTerrorSpawnEntityVisibleToTeam( actor, TEAM_SURVIVOR, 0, 0.0f, ToTerrorNavArea( actor->GetLastKnownArea() ) ) )
		{
			if ( actor->IsPlayer() ) engine->ServerCommand( CFmtStr( "kickid %d removing bots before the finale\n", static_cast< CBasePlayer * >( actor )->GetUserID() ) );
			else UTIL_Remove( actor );
		}
		return true;
	}
};
}
bool IsSurvivorInCombat()
{
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ScenarioPlayer( i );
		if ( player && player->GetTeamNumber() == TEAM_SURVIVOR && player->IsInCombat( false ) ) return true;
	}
	return false;
}

bool CDirector::IsAnySurvivorInCombat() const
{
	return IsSurvivorInCombat();
}
CDirectorScenario::CDirectorScenario()
{
	// S271110 zero-allocates before S287DB0; native timers construct themselves.
	// The only additional store is +240. Reset would call the unfinished Director.
	m_customFinaleChoice = 0;
}
void CDirectorScenario::Reset()
{
	CancelPanicEvent();
	m_flBossFlow = m_flGauntletMovementThreshold = m_flGauntletMovementBonus = 0.0f;
	m_bStopGauntlet = m_bBossSpawned = m_bWaitingForBoss = false;
	m_gauntletMovementTimer.Invalidate();
	m_gauntletBonusTimer.Invalidate();
	m_bStartedFirstPanicEvent = m_bFirstPanicEvent = false;
	m_nFinaleType = 5;
	m_bSacrificeFinale = m_bSacrificeSucceeded = m_bSacrificeFailed = false;
	m_sacrificeFailureTimer.Invalidate();
	m_bNative336 = false;
	m_nativeTimer284.Invalidate();
	m_bFinishCooldown = false;
	m_nFinaleStage = FINALE_CUSTOM_NONE;
	// Native Reset deliberately retains +137, stage names/counters and timers
	// not written above. The scripted owner reset is a real reached callback.
	TheDirector->GetScriptedMode()->Reset();
}
void CDirectorScenario::OnMapStart()
{
	m_nFinaleType = 5;
	OnFinaleTriggerSpawned();
}
void CDirectorScenario::OnFinaleTriggerSpawned()
{
	CFinaleTrigger *trigger = dynamic_cast< CFinaleTrigger * >( gEntList.FindEntityByClassname( NULL, "trigger_finale" ) );
	if ( trigger ) m_bSacrificeFinale = trigger->IsSacrificeFinale();
}
void CDirectorScenario::CancelPanicEvent()
{
	m_panicTimer.Invalidate();
	TheDirector->m_ScriptedPanicCooldown.Invalidate();
	TheDirector->m_PanicStartCooldown.Invalidate();
	m_nPanicStage = STAGE_DONE;
	m_bStartedFirstPanicEvent = m_bFirstPanicEvent = false;
}
void CDirectorScenario::OnFinaleStarted( int type )
{
	if ( m_nFinaleType != 5 )
	{
		DevMsg( "%3.2f: Director::OnFinaleStarted: ERROR - Finale already started.\n", gpGlobals->curtime );
		return;
	}
	DevMsg( "%3.2f: Director::OnFinaleStarted: Finale started.\n", gpGlobals->curtime );
	m_nFinaleType = type;
	m_bStopGauntlet = m_bEscapePrepared = false;
	TheDirector->m_nCurrentChapterIndex = MAX( TheDirector->m_nCurrentChapterIndex, 1 );
	RemoveUnseenFinaleActors remove;
	TheNextBots().ForEachBot( remove );
	TheZombieManager->ClearPendingMobCount();
	TheZombieManager->ClearSpawnAreaCollections();
	const bool gauntlet = type == 1;
	ChangeFinaleStage( gauntlet ? FINALE_GAUNTLET_START : FINALE_HORDE_ATTACK_1 );
	m_stageTimer.Invalidate();
	m_nQueuedStageAdvances = 0;
	m_minimumStageTimer.Invalidate();
	if ( gauntlet ) TheDirector->BeginLocalScript( "director_gauntlet" );
	else if ( type == 2 || type == 3 || type == 4 )
	{
		char script[256];
		Q_snprintf( script, sizeof( script ), "%s_finale", STRING( gpGlobals->mapname ) );
		TheDirector->RunScript( script, 1, false );
		m_stageInterval.Start();
		m_nCustomStage = m_nStageValue = 0;
		m_bossTimer.Invalidate();
		if ( !TheDirector->IsInSecondHalfOfRound() ) { m_customFinaleChoice = 0; m_nCustomStageCount = 0; }
		ChangeFinaleStage( FINALE_CUSTOM_DELAY );
		m_stageTimer.Start( director_finale_stage_delay.GetFloat() );
		if ( !Q_stricmp( STRING( gpGlobals->mapname ), "c1m4_atrium" ) && TheDirector->IsJimmyGibbsDay() ) TheZombieManager->StartJimmyGibbsSpawnTimer();
	}
	else StartPanicEvent( director_finale_panic_waves.GetInt() );
	m_bWaitingForBoss = false;
	IGameEvent *event = gameeventmanager->CreateEvent( gauntlet ? "gauntlet_finale_start" : "finale_start" );
	if ( gauntlet )
	{
		m_flGauntletMovementThreshold = TheDirector->m_flMaximumSurvivorFlow + TheDirector->GetDirectorOptionFloat( "GauntletMovementThreshold", director_gauntlet_movement_threshold.GetFloat() );
		m_gauntletMovementTimer.Start( TheDirector->GetDirectorOptionFloat( "GauntletMovementTimerLength", director_gauntlet_movement_timer_length.GetFloat() ) );
		m_flGauntletMovementBonus = 0.0f;
		m_gauntletBonusTimer.Invalidate();
	}
	if ( event )
	{
		KeyValues *mission = TerrorGameRules()->GetMissionInfo();
		event->SetString( "campaign", mission ? mission->GetString( "Name", "" ) : "" );
		gameeventmanager->FireEvent( event );
	}
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		if ( CTerrorPlayer *player = ScenarioPlayer( i ) ) player->CapGhostSpawnClockForFinale();
}
#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif

bool CDirectorScenario::OnScriptedPanicEvent( CTerrorPlayer *player, int reason )
{
	if ( reason == 3 )
	{
		m_nFinaleType = 3;
		TheZombieManager->ClearPendingMobCount();
		ChangeFinaleStage( FINALE_CUSTOM_DELAY );
		m_stageTimer.Start( 1.0f );
		m_stageInterval.Invalidate();
		m_nCustomStage = m_nCustomStageCount = m_nStageValue = 0;
		m_bossTimer.Invalidate();
		if ( !TheDirector->IsInSecondHalfOfRound() ) m_customFinaleChoice = 0;
		return true;
	}
	if ( reason != 1 && reason != 2 && TheDirector->m_ScriptedPanicCooldown.m_timestamp > 0.0f && !( gpGlobals->curtime >= TheDirector->m_ScriptedPanicCooldown.m_timestamp ) ) return false;
	TheDirector->m_ScriptedPanicCooldown.Start( 90.0f );
	StartPanicEvent( 0, player );
	return true;
}
void CDirectorScenario::StartPanicEvent( int waves, CTerrorPlayer *player )
{
	if ( TheDirector->m_PanicStartCooldown.m_timestamp > 0.0f && !( gpGlobals->curtime >= TheDirector->m_PanicStartCooldown.m_timestamp ) )
	{
		DevMsg( "%3.2f: Director::StartPanicEvent( %d ): ERROR: It is too soon to restart a PanicEvent.\n", gpGlobals->curtime, waves );
		return;
	}
	TheDirector->m_PanicStartCooldown.Start( 3.0f );
	TheZombieManager->ClearPendingMobCount();
	m_nPanicStage = STAGE_INITIAL_DELAY;
	m_panicTimer.Start( RandomFloat( 1.0f, 2.0f ) );
	m_nPanicWave = 0;
	m_nPanicWaveCount = waves == 0 ? 1 : waves;
	if ( !m_bStartedFirstPanicEvent && !AreAllSurvivorsOutsideBattlefield() &&
		( !TheDirector->GetScriptedMode()->IsEnabled() || TheDirector->GetDirectorOptionInt( "AllowCrescendoEvents", 1 ) != 0 ) )
	{
		m_bStartedFirstPanicEvent = m_bFirstPanicEvent = true;
		if ( waves == 0 ) m_nPanicWaveCount = TheDirector->GetChapterPanicWaveTier() >= 2 ? 2 : 1;
	}
	TheZombieManager->ResetPanicSpecialSpawnCounts();
	if ( TheDirector->GetCommonLimit() != 0 )
	{
		IGameEvent *event = gameeventmanager->CreateEvent( "create_panic_event" );
		if ( event ) { event->SetInt( "userid", player ? player->GetUserID() : 0 ); gameeventmanager->FireEvent( event ); }
	}
}
void CDirectorScenario::ChangeFinaleStage( int stage, const char *music )
{
	DevMsg( "ChangeFinaleStage: %d\n", stage );
	if ( m_nFinaleType != 5 && m_nFinaleType != 3 )
	{
		if ( music ) { if ( *music ) PlayFinaleMusic( music ); }
		else
		{
			HSCRIPT function = g_pScriptVM->LookupFunction( "OnChangeFinaleStage", TheDirector->GetOptionsScope() );
			// Retail releases this existence-probe before looking the function up
			// again for the call. Preserve the callback/option-update boundary.
			g_pScriptVM->ReleaseFunction( function );
			if ( function )
			{
				function = g_pScriptVM->LookupFunction( "OnChangeFinaleStage", TheDirector->GetOptionsScope() );
				if ( function ) { g_pScriptVM->Call( function, TheDirector->GetOptionsScope(), true, NULL, m_nFinaleType, stage ); g_pScriptVM->ReleaseFunction( function ); }
				char buffer[128];
				if ( TheDirector->GetDirectorOptionString( "OnChangeFinaleMusic", NULL, buffer, sizeof( buffer ) ) ) PlayFinaleMusic( buffer );
			}
		}
	}
	m_nFinaleStage = stage;
}
int CDirectorScenario::IncrementCustomStage()
{
	TheDirector->FireCustomStageOutput();
	++m_nCustomStage;
	if ( m_customFinaleChoice )
	{
		Q_snprintf( m_stageValueName, sizeof( m_stageValueName ), "%c_CustomFinale%1d", m_customFinaleChoice, m_nCustomStage );
		if ( TheDirector->GetDirectorOptionInt( m_stageValueName, -1 ) == -1 ) m_customFinaleChoice = 0;
	}
	if ( TheDirector->GetScriptedMode()->IsActive() && TheDirector->GetScriptedMode()->HasNextScriptedStage() )
	{
		if ( TheDirector->GetScriptedMode()->GetNextScriptedStage() != SCRIPT_DONE )
		{
			Msg( "SCRIPTED MODE: Failed to call GetNextScriptedStage\n" );
			return 9;
		}
		m_nStageValue = TheDirector->GetDirectorOptionInt( "ScriptedStageValue", 1 );
		int type = TheDirector->GetDirectorOptionInt( "ScriptedStageType", 9 );
		TheDirector->GetScriptedMode()->SetSpawnSetRuleFromOptions();
		if ( TheDirector->GetDirectorOptionInt( "SpawnSetRule", 0 ) == 3 )
		{
			Vector position( 0, 0, 0 );
			TheDirector->GetDirectorOptionVector( "SpawnSetPosition", &position );
			TheZombieManager->CollectScriptedSpawnAreas( true, position, TheDirector->GetDirectorOptionFloat( "SpawnSetRadius", 1000.0f ) );
		}
		return type;
	}
	if ( m_nFinaleType == 5 ) return 9;
	m_stageValueName[0] = 0;
	if ( !m_customFinaleChoice )
	{
		char choices[] = "ABCDE";
		for ( int i = 0; i < 4; ++i ) { int other = RandomInt( i, 4 ); char c = choices[i]; choices[i] = choices[other]; choices[other] = c; }
		for ( int i = 0; i < 5; ++i )
		{
			Q_snprintf( m_stageValueName, sizeof( m_stageValueName ), "%c_CustomFinale1", choices[i] );
			if ( TheDirector->GetDirectorOptionInt( m_stageValueName, -1 ) != -1 )
			{
				m_customFinaleChoice = choices[i];
				Q_snprintf( m_stageValueName, sizeof( m_stageValueName ), "%c_CustomFinale_StageCount", m_customFinaleChoice );
				m_nCustomStageCount = TheDirector->GetDirectorOptionInt( m_stageValueName, 0 );
				break;
			}
		}
	}
	m_stageInterval.Start();
	Q_snprintf( m_stageValueName, sizeof( m_stageValueName ), "%c_CustomFinale%1d", m_customFinaleChoice, m_nCustomStage );
	int type = TheDirector->GetDirectorOptionInt( m_stageValueName, -1 );
	Q_snprintf( m_stageValueName, sizeof( m_stageValueName ), "%c_CustomFinaleValue%1d", m_customFinaleChoice, m_nCustomStage );
	if ( type == 3 )
	{
		char script[128];
		if ( !TheDirector->GetDirectorOptionString( m_stageValueName, NULL, script, sizeof( script ) ) ) Warning( "Couldn't find a script name for custom scripted stage %s\n", m_stageValueName );
	}
	else m_nStageValue = TheDirector->GetDirectorOptionInt( m_stageValueName, -1 );
	return type;
}
void CDirectorScenario::AdvanceStage()
{
	int type = IncrementCustomStage();
	if ( TheDirector->GetScriptedMode()->IsActive() )
	{
		TheDirector->AddToSpecialSpawnTimers();
		ApplySpawnDirections();
	}
	char key[64], music[128];
	Q_snprintf( key, sizeof( key ), "%c_CustomFinaleMusic%1d", m_customFinaleChoice, m_nCustomStage );
	ApplyCustomStage( type, TheDirector->GetDirectorOptionString( key, NULL, music, sizeof( music ) ) );
	HSCRIPT scope = TheDirector->GetScriptScope( 2 );
	HSCRIPT function = g_pScriptVM->LookupFunction( "OnBeginCustomFinaleStage", scope );
	if ( function ) { g_pScriptVM->Call( function, scope, true, NULL, m_nCustomStage, type ); g_pScriptVM->ReleaseFunction( function ); }
}
void CDirectorScenario::ApplyCustomStage( int type, const char *music )
{
	switch ( type )
	{
	case 0: ChangeFinaleStage( FINALE_CUSTOM_PANIC, music ); break;
	case 1:
		m_bWaitingForBoss = false;
		m_bossTimer.Start( 1.0f );
		ChangeFinaleStage( FINALE_CUSTOM_TANK, music );
		if ( TheDirector->GetDifficulty() == 0 || TerrorGameRules()->IsVersusMode() ) m_nStageValue = 1;
		break;
	case 2: m_stageTimer.Invalidate(); ChangeFinaleStage( FINALE_CUSTOM_DELAY, music ); break;
	case 3:
	{
		ChangeFinaleStage( FINALE_CUSTOM_SCRIPTED, music );
		char script[128];
		if ( TheDirector->GetDirectorOptionString( m_stageValueName, NULL, script, sizeof( script ) ) ) TheDirector->BeginLocalScript( script );
		else Warning( "Couldn't find a script name for custom scripted stage %s\n", m_stageValueName );
		m_minimumStageTimer.Start( TheDirector->GetDirectorOptionFloat( "MinimumStageTime", 1.0f ) );
		break;
	}
	case 4:
		Msg( "Going into Old ClearNWait" );
		m_stageTimer.Invalidate();
		TheDirector->CommandInfectedToAttackSurvivors();
		ChangeFinaleStage( FINALE_CUSTOM_CLEAROUT, music );
		break;
	case 5: StartSetupStage(); ChangeFinaleStage( FINALE_CUSTOM_SETUP, music ); break;
	case 7: StartCustomEscape(); ChangeFinaleStage( FINALE_CUSTOM_ESCAPE, music ); break;
	case 8: m_stageTimer.Invalidate(); ChangeFinaleStage( FINALE_CUSTOM_RESULTS, music ); break;
	case 9: ChangeFinaleStage( FINALE_CUSTOM_NONE, music ); break;
	default:
		if ( m_nFinaleType == 3 ) m_nFinaleType = 5;
		else { StartCustomEscape(); ChangeFinaleStage( FINALE_HORDE_ESCAPE, music ); }
		TheDirector->StopLocalScript();
		break;
	}
}
void CDirectorScenario::StartSetupStage()
{
	if ( m_nStageValue <= 0 ) { m_stageTimer.Invalidate(); Msg( "Starting setup stage of unlimited duration.\n" ); }
	else { m_stageTimer.Start( (float)m_nStageValue ); Msg( "Starting setup stage, will expire in %d second(s).\n", m_nStageValue ); }
}
bool CDirectorScenario::UpdateCustomTank()
{
	if ( m_nStageValue <= 0 )
	{
		if ( m_bWaitingForBoss ) { if ( TheDirector->GetTankCount() > 0 ) m_bWaitingForBoss = false; }
		else if ( TheDirector->GetTankCount() <= 0 ) return false;
		return true;
	}
	if ( m_bossTimer.m_timestamp > 0.0f )
	{
		if ( gpGlobals->curtime >= m_bossTimer.m_timestamp )
		{
			if ( SpawnFinaleTank() ) m_bWaitingForBoss = true;
			else { Warning( "Failed to create tank during custom tank finale phase.\n" ); NotifySpecialDeathMusic( 8 ); }
			--m_nStageValue;
			m_bossTimer.Invalidate();
		}
	}
	else m_bossTimer.Start( director_custom_finale_tank_spacing.GetFloat() );
	return true;
}
bool CDirectorScenario::UpdateDelay()
{
	if ( m_nStageValue == -1 ) return true;
	if ( m_stageTimer.m_timestamp > 0.0f )
	{
		if ( !( gpGlobals->curtime >= m_stageTimer.m_timestamp ) && !m_bFinishCooldown ) return true;
		m_bFinishCooldown = false;
		return false;
	}
	m_stageTimer.Start( (float)m_nStageValue );
	TerrorGameRules()->SetScriptedEventCooldown( gpGlobals->curtime + (float)m_nStageValue );
	return true;
}
bool CDirectorScenario::UpdateResults()
{
	if ( !( m_stageTimer.m_timestamp > 0.0f ) ) m_stageTimer.Start( (float)m_nStageValue );
	else if ( gpGlobals->curtime >= m_stageTimer.m_timestamp ) return false;
	return true;
}
void CDirectorScenario::UpdateClearout()
{
	int commons = GetCommonInfectedCount( 0 );
	int specials = 0;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ScenarioPlayer( i );
		if ( player && player->IsAlive() && player->GetTeamNumber() == TEAM_INFECTED ) ++specials;
	}
	if ( !( m_clearoutTimer.m_timestamp > 0.0f ) || commons != m_nLastClearoutCommons || specials != m_nLastClearoutSpecials )
		m_clearoutTimer.Start( (float)m_nStageValue );
	m_nLastClearoutCommons = commons;
	m_nLastClearoutSpecials = specials;
	if ( TheDirector->GetWitchCount() == 0 && TheDirector->GetTankCount() == 0 &&
		( ( commons <= director_clear_count.GetInt() && specials == 0 ) ||
		( gpGlobals->curtime >= m_clearoutTimer.m_timestamp && commons <= 10 && specials <= 1 ) ) ) AdvanceStage();
}
bool CDirectorScenario::SpawnFinaleTank()
{
	Vector position;
	if ( !TheZombieManager->GetRandomPZSpawnPosition( 8, 10, NULL, &position ) ) return false;
	CTerrorPlayer *tank = TheZombieManager->SpawnTank( position, QAngle( 0.0f, RandomFloat( 0.0f, 360.0f ), 0.0f ) );
	if ( !tank ) return false;
	TheDirector->OfferTank( tank, true );
	return true;
}
void CDirectorScenario::StartGauntletHorde()
{
	TheZombieManager->SetPendingMobCount( 0 );
	TheZombieManager->SpawnMob( TheDirector->GetMobSize() );
}
bool CDirectorScenario::UpdateFinale()
{
	const float delay = director_finale_stage_delay.GetFloat();
	switch ( m_nFinaleStage )
	{
	case FINALE_HORDE_ATTACK_1:
		if ( !TheDirector->m_bForceGauntletTank && ( m_nPanicStage != STAGE_DONE || IsSurvivorInCombat() ) ) break;
		if ( !( m_stageTimer.m_timestamp > 0.0f ) ) { m_stageTimer.Start( delay ); TheDirector->OnFinalePause(); break; }
		if ( !( gpGlobals->curtime >= m_stageTimer.m_timestamp ) || !SpawnFinaleTank() ) break;
		if ( m_nFinaleType == 1 ) TheDirector->StopLocalScript();
		m_bWaitingForBoss = true;
		ChangeFinaleStage( FINALE_HALFTIME_BOSS );
		m_stageTimer.Invalidate();
		TheDirector->m_bForceGauntletTank = false;
		break;
	case FINALE_HALFTIME_BOSS:
		if ( m_bWaitingForBoss ) { if ( TheDirector->GetTankCount() > 0 ) m_bWaitingForBoss = false; break; }
		if ( TheDirector->GetTankCount() > 0 || IsSurvivorInCombat() ) break;
		if ( !( m_stageTimer.m_timestamp > 0.0f ) ) { m_stageTimer.Start( delay ); TheDirector->OnFinalePause(); break; }
		if ( !( gpGlobals->curtime >= m_stageTimer.m_timestamp ) ) break;
		ChangeFinaleStage( m_nFinaleType == 1 ? FINALE_GAUNTLET_2 : FINALE_HORDE_ATTACK_2 );
		StartPanicEvent( m_nFinaleType == 1 ? 1 : 2 );
		if ( m_nFinaleType == 1 ) TheDirector->BeginLocalScript( "director_gauntlet" );
		m_stageTimer.Invalidate();
		break;
	case FINALE_GAUNTLET_2:
		if ( !m_bStopGauntlet ) break;
		ChangeFinaleStage( FINALE_HORDE_ATTACK_2 );
		// Retail intentionally falls through to the second horde branch.
	case FINALE_HORDE_ATTACK_2:
		if ( m_nFinaleType == 1 ) { ChangeFinaleStage( FINALE_HORDE_ESCAPE ); m_stageTimer.Start( RandomFloat( delay, delay * 2.0f ) ); break; }
		if ( m_nPanicStage != STAGE_DONE || IsSurvivorInCombat() ) break;
		if ( !( m_stageTimer.m_timestamp > 0.0f ) ) { m_stageTimer.Start( delay ); TheDirector->OnFinalePause(); break; }
		if ( !( gpGlobals->curtime >= m_stageTimer.m_timestamp ) || !SpawnFinaleTank() ) break;
		m_bWaitingForBoss = true;
		ChangeFinaleStage( FINALE_FINAL_BOSS );
		m_stageTimer.Invalidate();
		break;
	case FINALE_FINAL_BOSS:
		if ( m_bWaitingForBoss ) { if ( TheDirector->GetTankCount() > 0 ) m_bWaitingForBoss = false; break; }
		if ( TheDirector->GetTankCount() > 0 || IsSurvivorInCombat() ) break;
		if ( !( m_stageTimer.m_timestamp > 0.0f ) ) { StartEscape(); m_stageTimer.Start( delay ); TheDirector->OnFinalePause(); break; }
		if ( !( gpGlobals->curtime >= m_stageTimer.m_timestamp ) ) break;
		StartPanicEvent( 1 );
		ChangeFinaleStage( FINALE_HORDE_ESCAPE );
		m_stageTimer.Start( delay * 2.0f );
		break;
	case FINALE_HORDE_ESCAPE:
	case FINALE_CUSTOM_ESCAPE: UpdateEscape(); break;
	case FINALE_CUSTOM_PANIC: if ( m_nPanicStage == STAGE_DONE ) StartPanicEvent( m_nStageValue ); break;
	case FINALE_CUSTOM_TANK:
		if ( !UpdateCustomTank() ) AdvanceStage();
		return TheDirector->GetDirectorOptionInt( "ShouldAllowMobsWithTank", 0 ) == 0;
	case FINALE_CUSTOM_SCRIPTED:
		if ( m_nQueuedStageAdvances <= 0 || !( m_minimumStageTimer.m_timestamp > 0.0f ) || !( gpGlobals->curtime >= m_minimumStageTimer.m_timestamp ) ) return false;
		if ( m_nFinaleStage == FINALE_CUSTOM_SCRIPTED ) TheDirector->StopLocalScript();
		AdvanceStage();
		m_minimumStageTimer.Invalidate();
		--m_nQueuedStageAdvances;
		break;
	case FINALE_CUSTOM_DELAY: if ( !UpdateDelay() ) AdvanceStage(); break;
	case FINALE_CUSTOM_CLEAROUT: UpdateClearout(); break;
	case FINALE_GAUNTLET_START:
		ChangeFinaleStage( FINALE_GAUNTLET_HORDE );
		m_bBossSpawned = false;
		for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			CTerrorPlayer *player = ScenarioPlayer( i );
			if ( player && player->GetTeamNumber() == TEAM_SURVIVOR ) { player->ClearLastKnownArea(); player->UpdateLastKnownArea(); }
		}
		TheZombieManager->ClearSpawnAreaCollections();
		TheZombieManager->SpawnMob( TheDirector->GetMobSize() );
		TheDirector->m_MobSpawnTimer.Start( RandomFloat( TheDirector->GetMobSpawnMinTime(), TheDirector->GetMobSpawnMaxTime() ) );
		break;
	case FINALE_GAUNTLET_HORDE:
		if ( TheDirector->m_MobSpawnTimer.m_timestamp - gpGlobals->curtime > TheDirector->GetMobSpawnMaxTime() )
			TheDirector->m_MobSpawnTimer.Start( RandomFloat( TheDirector->GetMobSpawnMinTime(), TheDirector->GetMobSpawnMaxTime() ) );
		if ( !( gpGlobals->curtime >= TheDirector->m_MobSpawnTimer.m_timestamp ) ) break;
		if ( m_flGauntletMovementBonus > 0.0f ) m_gauntletBonusTimer.Start( m_flGauntletMovementBonus );
		ChangeFinaleStage( FINALE_GAUNTLET_HORDE_BONUSTIME );
		break;
	case FINALE_GAUNTLET_HORDE_BONUSTIME:
		if ( m_gauntletBonusTimer.m_timestamp > 0.0f && !( gpGlobals->curtime >= m_gauntletBonusTimer.m_timestamp ) ) break;
		m_gauntletBonusTimer.Invalidate();
		TheDirector->m_MobSpawnTimer.Start( RandomFloat( TheDirector->GetMobSpawnMinTime(), TheDirector->GetMobSpawnMaxTime() ) );
		StartGauntletHorde();
		ChangeFinaleStage( FINALE_GAUNTLET_HORDE );
		break;
	case FINALE_GAUNTLET_BOSS_INCOMING:
		if ( !( (double)TheDirector->m_flMaximumSurvivorFlow > (double)m_flBossFlow + (double)TheDirector->GetDirectorOptionFloat( "CustomTankKiteDistance", director_gauntlet_tank_kite_distance.GetFloat() ) ) && GetCommonInfectedCount( 0 ) != 0 ) break;
		m_flBossFlow = TheDirector->m_flMaximumSurvivorFlow;
		ChangeFinaleStage( FINALE_GAUNTLET_BOSS );
		break;
	case FINALE_GAUNTLET_BOSS:
		if ( (double)TheDirector->m_flMaximumSurvivorFlow > (double)m_flBossFlow + (double)TheDirector->GetDirectorOptionFloat( "CustomTankKiteDistance", director_gauntlet_tank_kite_distance.GetFloat() ) ) ChangeFinaleStage( FINALE_GAUNTLET_HORDE );
		if ( m_bWaitingForBoss )
		{
			if ( TheDirector->GetTankCount() > 0 ) { m_bBossSpawned = true; m_bWaitingForBoss = false; }
			if ( gpGlobals->curtime >= m_bossTimer.m_timestamp ) { m_bBossSpawned = m_bWaitingForBoss = false; m_bossTimer.Invalidate(); }
			break;
		}
		if ( TheDirector->GetTankCount() > 0 ) break;
		if ( m_bBossSpawned ) { ChangeFinaleStage( FINALE_GAUNTLET_HORDE ); break; }
		if ( SpawnFinaleTank() ) { m_bBossSpawned = false; m_bWaitingForBoss = true; m_bossTimer.Start( 5.0f ); }
		else { DevMsg( "Failed to create tank during custom tank finale phase.\n" ); NotifySpecialDeathMusic( 8 ); }
		break;
	case FINALE_GAUNTLET_ESCAPE:
		if ( m_bWaitingForBoss )
		{
			if ( TheDirector->GetTankCount() > 0 ) { m_bBossSpawned = true; m_bWaitingForBoss = false; m_bossTimer.Invalidate(); }
			if ( gpGlobals->curtime >= m_bossTimer.m_timestamp ) { m_bBossSpawned = m_bWaitingForBoss = false; m_bossTimer.Invalidate(); }
		}
		if ( TheDirector->GetTankCount() <= 0 )
		{
			if ( !( m_bossTimer.m_timestamp > 0.0f ) ) m_bossTimer.Start( 20.0f );
			if ( gpGlobals->curtime >= m_bossTimer.m_timestamp && SpawnFinaleTank() ) { m_bWaitingForBoss = true; m_bossTimer.Start( 5.0f ); }
		}
		if ( !( gpGlobals->curtime >= TheDirector->m_MobSpawnTimer.m_timestamp ) ) break;
		TheDirector->m_MobSpawnTimer.Start( TheDirector->GetMobSpawnMinTime() );
		StartGauntletHorde();
		break;
	case FINALE_CUSTOM_SETUP: if ( m_stageTimer.m_timestamp > 0.0f && gpGlobals->curtime >= m_stageTimer.m_timestamp ) AdvanceStage(); break;
	case FINALE_CUSTOM_RESULTS:
		if ( !UpdateResults() ) { ChangeFinaleStage( FINALE_CUSTOM_NONE ); TheDirector->EndScenario(); }
		break;
	default: break;
	}
	return true;
}
bool CDirectorScenario::Update()
{
	UpdateSacrifice();
	return m_nFinaleType == 5 ? false : UpdateFinale();
}
void CDirectorScenario::UpdateGauntletMovement()
{
	if ( m_nFinaleType != 1 ) return;
	if ( TheDirector->m_flMaximumSurvivorFlow > m_flGauntletMovementThreshold )
	{
		m_flGauntletMovementBonus = 0.0f;
		m_flGauntletMovementThreshold = TheDirector->m_flMaximumSurvivorFlow + TheDirector->GetDirectorOptionFloat( "GauntletMovementThreshold", director_gauntlet_movement_threshold.GetFloat() );
		m_gauntletMovementTimer.Start( TheDirector->GetDirectorOptionFloat( "GauntletMovementTimerLength", director_gauntlet_movement_timer_length.GetFloat() ) );
		m_gauntletBonusTimer.Invalidate();
		TheDirector->m_MobSpawnTimer.Start( 1.0f );
	}
	if ( gpGlobals->curtime >= m_gauntletMovementTimer.m_timestamp )
	{
		m_gauntletMovementTimer.Start( TheDirector->GetDirectorOptionFloat( "GauntletMovementTimerLength", director_gauntlet_movement_timer_length.GetFloat() ) );
		m_flGauntletMovementBonus += TheDirector->GetDirectorOptionFloat( "GauntletMovementBonus", director_gauntlet_movement_bonus.GetFloat() );
		if ( m_flGauntletMovementBonus > TheDirector->GetDirectorOptionFloat( "GauntletMovementBonusMax", director_gauntlet_movement_bonus_max.GetFloat() ) )
			m_flGauntletMovementBonus = TheDirector->GetDirectorOptionFloat( "GauntletMovementBonusMax", director_gauntlet_movement_bonus_max.GetFloat() );
	}
}
void CDirectorScenario::StopGauntletPanic()
{
	if ( m_nFinaleType != 1 ) return;
	switch ( m_nFinaleStage )
	{
	case FINALE_GAUNTLET_1:
		ChangeFinaleStage( FINALE_HORDE_ATTACK_1 );
		TheZombieManager->SetPendingMobCount( RandomInt( 0, TheDirector->GetDirectorOptionInt( "PreTankMobMax", 50 ) ) );
		break;
	case FINALE_GAUNTLET_2:
		ChangeFinaleStage( FINALE_HORDE_ATTACK_2 );
		TheZombieManager->SetPendingMobCount( RandomInt( 0, 20 ) );
		// Retail continues through the gauntlet-horde handling.
	case FINALE_GAUNTLET_HORDE:
	case FINALE_GAUNTLET_HORDE_BONUSTIME:
		if ( m_bBossSpawned ) ChangeFinaleStage( FINALE_GAUNTLET_ESCAPE );
		else { m_bWaitingForBoss = false; TheZombieManager->SetPendingMobCount( 0 ); m_flBossFlow = TheDirector->m_flMaximumSurvivorFlow; ChangeFinaleStage( FINALE_GAUNTLET_BOSS_INCOMING ); }
		break;
	default: m_bStopGauntlet = true; break;
	}
}
void CDirectorScenario::PrepareEscape()
{
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ScenarioPlayer( i );
		if ( !player ) continue;
		if ( player->IsAlive() && player->GetTeamNumber() == TEAM_SURVIVOR )
		{
			// S308370 broadcasts this concept once for EACH living survivor;
			// it is not a single broadcast outside the outer player loop.
			for ( int j = 1; j <= gpGlobals->maxClients; ++j )
			{
				CTerrorPlayer *recipient = ScenarioPlayer( j );
				if ( recipient && recipient->GetTeamNumber() == TEAM_SURVIVOR ) recipient->SpeakIfAllowed( "FinalVehicleSpotted", 3 );
			}
		}
		player->GetMusic()->OnFinaleFinalBattle();
	}
}
void CDirectorScenario::StartEscape()
{
	CBaseEntity *trigger = FindFinaleTrigger();
	if ( trigger ) { trigger->AcceptInput( "FinaleEscapeStarted", NULL, NULL, variant_t(), 0 ); ScenarioEvent( "finale_escape_start" ); }
	if ( !m_bEscapePrepared ) { PrepareEscape(); m_bEscapePrepared = true; }
}
void CDirectorScenario::StartCustomEscape()
{
	m_bWaitingForBoss = false;
	m_bossTimer.Start( 1.0f );
	StartEscape();
	TheDirector->m_nHordeEscapeCommonLimit = TheDirector->GetDirectorOptionInt( "HordeEscapeCommonLimit", -1 );
}
void CDirectorScenario::OnEscapeVehicleReady()
{
	if ( !m_bEscapePrepared ) { PrepareEscape(); m_bEscapePrepared = true; }
}
void CDirectorScenario::UpdateEscape()
{
	if ( m_nPanicStage == STAGE_DONE ) StartPanicEvent( 1 );
	if ( m_nFinaleType == 1 )
	{
		// Original process globals 83E4C4/83E4C8 survive Reset and map start.
		static float lastTankDeathTime;
		static bool hadTank;
		if ( hadTank && TheDirector->GetTankCount() <= 0 ) lastTankDeathTime = gpGlobals->curtime;
		hadTank = TheDirector->GetTankCount() > 0;
		if ( gpGlobals->curtime - lastTankDeathTime < 20.0f ) return;
	}
	if ( gpGlobals->curtime >= m_stageTimer.m_timestamp && TheDirector->GetTankCount() <= 0 && TheDirector->GetDirectorOptionInt( "EscapeSpawnTanks", 1 ) != 0 )
	{
		SpawnFinaleTank();
		m_stageTimer.Start( director_finale_stage_delay.GetFloat() );
	}
}
void CDirectorScenario::UpdateSacrifice()
{
	if ( !m_bSacrificeFinale || !TheDirector->m_bGamePlaying || TheDirector->m_MissionEndTimer.m_timestamp > 0.0f ||
		( TerrorGameRules()->IsVersusMode() && !TheDirector->GetVersusMode()->IsFinaleActive() ) || IsSinglePlayerScenarioMode() ) return;
	if ( TheDirector->GetVersusMode()->IsSacrificeActive() && TheDirector->HaveAllSurvivorsLeftSafeArea() && !m_bSacrificeSucceeded && !m_bSacrificeFailed &&
		TheDirector->m_activeRescueSpots.Count() == 0 && ( m_nFinaleType != 5 || TheDirector->GetVersusMode()->IsFinaleActive() ) )
	{
		CUtlVector< CTerrorPlayer * > bots, humans;
		CollectTerrorPlayers( bots, TEAM_SURVIVOR, 2, 2, 9 );
		CollectTerrorPlayers( humans, TEAM_SURVIVOR, 2, 1, 9 );
		if ( humans.Count() < 1 ) { DevMsg( "DLC3 Finale failed: Less than 1 player alive.\n" ); m_bSacrificeFailed = true; }
		else if ( bots.Count() + humans.Count() < 2 ) { DevMsg( "DLC3 Finale failed: Less than 2 survivors alive.\n" ); m_bSacrificeFailed = true; }
	}
	if ( !m_bSacrificeFailed ) return;
	if ( m_sacrificeFailureTimer.m_timestamp > 0.0f )
	{
		if ( gpGlobals->curtime >= m_sacrificeFailureTimer.m_timestamp && TheDirector->m_bGamePlaying ) TheDirector->EndScenario();
	}
	else
	{
		m_sacrificeFailureTimer.Start( TerrorGameRules()->IsVersusMode() ? 5.0f : 2.0f );
		for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			CTerrorPlayer *player = ScenarioPlayer( i );
			if ( !player || player->IsBot() ) continue;
			KeyValues *window = new KeyValues( "data" );
			window->SetString( "res", TheDirector->IsRescueVehicleReady() ? "resource/ui/riverescapefailed.res" : "resource/ui/riverfinalefailed.res" );
			player->ShowViewPortPanel( "info_window", true, window );
			window->deleteThis();
		}
	}
}
void CDirectorScenario::WarnMegaMobIncoming()
{
	if ( TheDirector->GetCommonLimit() == 0 ) return;
	CUtlVector< CTerrorPlayer * > players;
	CollectTerrorPlayers( players, -1, 7, 3, 9 );
	FOR_EACH_VEC( players, i )
	{
		CTerrorPlayer *player = players[i];
		player->EmitSound( "MegaMobIncoming", 0.0f );
		if ( player->IsAlive() && player->GetTeamNumber() == TEAM_SURVIVOR ) player->SpeakIfAllowed( "PlayerWarnMegaMob", 1 );
	}
}
void CDirectorScenario::UpdatePanic()
{
	switch ( m_nPanicStage )
	{
	case STAGE_INITIAL_DELAY:
		if ( gpGlobals->curtime >= m_panicTimer.m_timestamp ) { m_nPanicStage = STAGE_MEGA_MOB; m_nPanicWave = 0; WarnMegaMobIncoming(); }
		break;
	case STAGE_MEGA_MOB:
		TheDirector->CommandInfectedToAttackSurvivors();
		if ( TheDirector->GetMegaMobSize() > 1 ) TheZombieManager->RequestDelayedMegaMob();
		++m_nPanicWave;
		m_nPanicStage = STAGE_WAIT_FOR_COMBAT_TO_END;
		m_panicTimer.Start( 10.0f );
		break;
	case STAGE_WAIT_FOR_COMBAT_TO_END:
		if ( TheZombieManager->GetPendingMobCount() > 0 ) { m_panicTimer.Start( 10.0f ); if ( TheDirector->GetCommonLimit() <= 0 ) TheZombieManager->ClearPendingMobCount(); }
		if ( TheDirector->GetScriptedMode()->IsActive() )
		{
			static const char *options[] = { "TotalSmokers", "TotalBoomers", "TotalHunters", "TotalSpitters", "TotalJockeys", "TotalChargers" };
			int remaining = 0;
			bool hasLimit = false;
			for ( int i = 0; i < ARRAYSIZE( options ); ++i )
			{
				int limit = TheDirector->GetDirectorOptionInt( options[i], 0 );
				if ( limit != 0 ) { remaining += MAX( limit - TheZombieManager->GetPanicSpecialSpawnCount( i + 1 ), 0 ); hasLimit = true; }
			}
			int total = TheDirector->GetDirectorOptionInt( "TotalSpecials", 0 );
			if ( total != 0 ) { remaining += MAX( total - TheZombieManager->GetTotalPanicSpecialSpawns(), 0 ); hasLimit = true; }
			if ( remaining > 0 ) m_panicTimer.Start( 10.0f );
			else if ( hasLimit && TheDirector->GetDirectorOptionInt( "PanicSpecialsOnly", 0 ) != 0 ) { m_panicTimer.Invalidate(); TheZombieManager->ClearPendingMobCount(); }
		}
		if ( gpGlobals->curtime >= m_panicTimer.m_timestamp )
		{
			bool forever = IsPanicForever();
			if ( m_nPanicWave < m_nPanicWaveCount || forever )
			{
				m_nPanicStage = STAGE_PAUSE;
				float maximum = TheDirector->GetDirectorOptionFloat( "PanicWavePauseMax", director_panic_wave_pause_max.GetFloat() );
				float minimum = TheDirector->GetDirectorOptionFloat( "PanicWavePauseMin", director_panic_wave_pause_min.GetFloat() );
				m_panicTimer.Start( RandomFloat( minimum, maximum ) );
				if ( m_nFinaleType != 5 && !forever ) TheDirector->OnFinalePause();
			}
			else
			{
				TheDirector->ResetMobTimer();
				m_nPanicStage = STAGE_DONE;
				TheDirector->OnPanicEventCompleted();
				m_bFirstPanicEvent = false;
				OnPanicEventFinished();
			}
		}
		break;
	case STAGE_PAUSE:
		if ( gpGlobals->curtime >= m_panicTimer.m_timestamp && ( TheDirector->m_nIntensityState == 0 || TheDirector->GetDirectorOptionInt( "PausePanicWhenRelaxing", 0 ) == 0 ) ) m_nPanicStage = STAGE_MEGA_MOB;
		break;
	default: break;
	}
}
void CDirectorScenario::OnPanicEventFinished()
{
	if ( ( m_nFinaleType == 2 || m_nFinaleType == 3 || m_nFinaleType == 4 || TheDirector->GetScriptedMode()->IsActive() ) && m_nFinaleStage == FINALE_CUSTOM_PANIC ) AdvanceStage();
	ScenarioEvent( "panic_event_finished" );
}
void CDirectorScenario::PlayFinaleMusic( const char *event ) const
{
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ScenarioPlayer( i );
		if ( player && player->IsAlive() ) player->GetMusic()->Play( event );
	}
}
float CDirectorScenario::GetStageTimeRemaining() const { return m_stageTimer.m_timestamp - gpGlobals->curtime; }
void CDirectorScenario::SetCooldown( float worldTime )
{
	if ( !( m_stageTimer.m_timestamp > 0.0f ) ) { Warning( "WARNING: trying to set new Cooldown time, but timer isnt running\n" ); return; }
	m_stageTimer.Start( worldTime - gpGlobals->curtime );
	TerrorGameRules()->SetScriptedEventCooldown( worldTime );
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif

float CDirectorScenario::GetCustomScriptedStageProgress( float fallback ) const
{
	HSCRIPT scope = TheDirector->GetScriptScope( 2 );
	HSCRIPT function = g_pScriptVM->LookupFunction( "GetCustomScriptedStageProgress", scope );
	if ( function )
	{
		ScriptVariant_t value;
		g_pScriptVM->Call( function, scope, true, &value, fallback );
		g_pScriptVM->ReleaseFunction( function );
		// S289E02: other defined scalars convert the signed first DWORD.
		// Main excludes undefined address-bit interpretations of pointer-like kinds.
		switch ( value.GetType() )
		{
		case FIELD_VOID: fallback = 0.0f; break;
		case FIELD_FLOAT: fallback = (float)value; break;
		case FIELD_INTEGER: fallback = (float)(int)value; break;
		case FIELD_BOOLEAN: fallback = (bool)value ? 1.0f : 0.0f; break;
		case FIELD_CHARACTER: fallback = (float)(unsigned char)(char)value; break;
		case FIELD_UINT: fallback = (float)(int)(uint)value; break;
		case FIELD_UINT64: fallback = (float)(int)(uint)(uint64)value; break;
		case FIELD_EHANDLE:
		{
			CBaseHandle handle = (CBaseHandle)value;
			fallback = (float)(int)handle.ToInt();
			break;
		}
		case FIELD_FLOAT64:
		{
			float64 number = (float64)value;
			int firstWord;
			Q_memcpy( &firstWord, &number, sizeof( firstWord ) );
			fallback = (float)firstWord;
			break;
		}
		default: break;
		}
		value.Free();
	}
	return fallback;
}
bool CDirectorScenario::AreAllLivingSurvivorsInBattlefield( bool *noLivingSurvivors ) const
{
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ScenarioPlayer( i );
		if ( !player || !player->IsAlive() || player->GetTeamNumber() != TEAM_SURVIVOR ) continue;
		if ( noLivingSurvivors ) *noLivingSurvivors = false;
		TerrorNavArea *area = ToTerrorNavArea( player->GetLastKnownArea() );
		if ( !area || !area->HasSpawnAttributes( TERROR_NAV_FINALE | TERROR_NAV_BATTLEFIELD ) ) return false;
	}
	return true;
}
bool CDirectorScenario::AreAllSurvivorsOutsideBattlefield() const
{
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ScenarioPlayer( i );
		if ( !player || !player->IsAlive() || player->GetTeamNumber() != TEAM_SURVIVOR ) continue;
		TerrorNavArea *area = ToTerrorNavArea( player->GetLastKnownArea() );
		if ( area && area->HasSpawnAttributes( TERROR_NAV_FINALE | TERROR_NAV_BATTLEFIELD ) ) return false;
	}
	return true;
}
bool CDirectorScenario::AreSurvivorsInFinaleAreaBeforePanic() const
{
	if ( m_bStartedFirstPanicEvent ) return false;
	bool noLivingSurvivors = true;
	return AreAllLivingSurvivorsInBattlefield( &noLivingSurvivors ) && !noLivingSurvivors;
}
bool CDirectorScenario::ShouldAllowMobs() const
{
	if ( m_nFinaleStage == FINALE_CUSTOM_SCRIPTED ||
		( m_nFinaleStage == FINALE_CUSTOM_TANK && TheDirector->GetDirectorOptionInt( "ShouldAllowMobsWithTank", 0 ) != 0 ) )
		return m_nFinaleStage != FINALE_CUSTOM_SETUP && m_nFinaleStage != FINALE_CUSTOM_DELAY;
	if ( m_nPanicStage == STAGE_DONE )
	{
		bool noLivingSurvivors = true;
		if ( m_bStartedFirstPanicEvent || !AreAllLivingSurvivorsInBattlefield( &noLivingSurvivors ) || noLivingSurvivors )
			return m_nFinaleStage != FINALE_CUSTOM_SETUP && m_nFinaleStage != FINALE_CUSTOM_DELAY;
	}
	return false;
}
bool CDirectorScenario::IsPanicForever() const
{
	if ( m_nFinaleType == 1 && m_nFinaleStage != FINALE_GAUNTLET_1 && m_nFinaleStage != FINALE_GAUNTLET_2 ) return director_panic_forever.GetInt() != 0;
	return TheDirector->GetDirectorOptionInt( "PanicForever", director_panic_forever.GetInt() ) == 1;
}
void CDirectorScenario::OverrideSpecialRespawnInterval( float *interval ) const
{
	if ( m_nFinaleType != 5 || m_bFirstPanicEvent ) *interval = director_special_battlefield_respawn_interval.GetFloat();
}
bool CDirectorScenario::IsEscapeStageOrCompletedPanic() const
{
	return m_nFinaleType == 5 ? m_bStartedFirstPanicEvent && !m_bFirstPanicEvent : m_nFinaleStage == FINALE_HORDE_ESCAPE;
}

// Actual Director state and trigger I/O are distinct from scenario+0/+4.
void CDirector::OnFinaleStarted( int type )
{
	m_nGameState = 0; // S25C5D0 root356 precedes child validation/callbacks.
	m_pScenario->OnFinaleStarted( type );
}
bool CDirector::OnScriptedPanicEvent( CTerrorPlayer *player, int reason )
{
	// S25CC60 delegates to the distinct +1600 owner FIRST. Its return 2
	// means fall through to +1592; 1 means handled, not a finale-type value.
	int disposition = m_pSurvivalMode->OnScriptedPanicEvent( player, reason );
	return disposition == 2 ? m_pScenario->OnScriptedPanicEvent( player, reason ) : disposition == 1;
}
void CDirector::ForceGauntletTank() { if ( m_pScenario->GetFinaleType() == 1 ) m_bForceGauntletTank = true; }
void CDirector::OnFinaleVehicleIncoming()
{
	IGameEvent *event = gameeventmanager->CreateEvent( "finale_vehicle_incoming" );
	if ( event )
	{
		KeyValues *mission = TerrorGameRules()->GetMissionInfo();
		event->SetString( "campaign", mission ? mission->GetString( "Name", "" ) : "" );
		gameeventmanager->FireEvent( event );
	}
}
void CDirector::OnFinalePause() { if ( m_pScenario->GetFinaleType() != 5 ) FinaleInput( "FinalePause" ); }
void CDirector::OnFinaleVehicleReady()
{
	m_nGameState = 1;
	m_pScenario->OnEscapeVehicleReady();
	IGameEvent *event = gameeventmanager->CreateEvent( "finale_vehicle_ready" );
	if ( event )
	{
		KeyValues *mission = TerrorGameRules()->GetMissionInfo();
		event->SetString( "campaign", mission ? mission->GetString( "Name", "" ) : "" );
		gameeventmanager->FireEvent( event );
	}
}
void CDirector::OnFinaleVehicleLeaving( const CUtlVector< CTerrorPlayer * > &escaped )
{
	m_pScriptedMode->OnFinaleVehicleLeaving( escaped );
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ScenarioPlayer( i );
		if ( player && player->GetTeamNumber() == TEAM_SURVIVOR && player->IsNetClient() )
		{
			CSingleUserRecipientFilter filter( player );
			enginesound->SetPlayerDSP( filter, 0, false );
		}
	}
	m_nGameState = 2;
	IGameEvent *event = gameeventmanager->CreateEvent( "finale_vehicle_leaving" );
	if ( event )
	{
		FOR_EACH_VEC( escaped, i ) Msg( "ESCAPED: %s %s\n", escaped[i]->GetPlayerName(), escaped[i]->IsIncapacitated() ? "(INCAP)" : "" );
		event->SetInt( "survivorcount", escaped.Count() );
		gameeventmanager->FireEvent( event );
	}
	TerrorGameRules()->RecordFinaleEscapees( escaped );
	PositionFinaleEscapees( escaped );
}
void CDirector::OnFinaleWon()
{
	if ( m_pScenario->GetFinaleType() == 5 && !m_pScriptedMode->IsActive() ) return;
	m_nGameState = 3;
	FinaleInput( "FinaleWon" );
	IGameEvent *event = gameeventmanager->CreateEvent( "finale_win" );
	if ( event )
	{
		event->SetString( "map_name", STRING( gpGlobals->mapname ) );
		event->SetInt( "difficulty", TerrorGameRules()->GetDifficultyLevel() );
		gameeventmanager->FireEvent( event );
	}
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ScenarioPlayer( i );
		if ( player ) { player->GetMusic()->OnMissionWon(); player->UpdateMissionStats(); }
	}
	// The native CDirector v+104 (S271B80) is genuinely constant true.
	// Its versus child S290640 returns !IsVersusMode, so only versus starts
	// this countdown here. Do not invert this retail asymmetry.
	if ( TerrorGameRules()->IsVersusMode() ) StartScenarioExit();
}
void CDirector::StartScenarioExit()
{
	DevMsg( "Director::StartScenarioExit\n" );
	if ( m_ScenarioExitTimer.m_timestamp <= 0.0f ) m_ScenarioExitTimer.Start( 2.0f );
}
void CDirector::BeginLocalScript( const char *script ) { StopLocalScript(); RunScript( script, 2, false ); }
void CDirector::StopLocalScript()
{
	if ( m_hScriptScope[2] == INVALID_HSCRIPT || !g_pScriptVM ) return;
	g_pScriptVM->ReleaseScope( m_hScriptScope[2] );
	if ( g_pScriptVM->ValueExists( m_hScriptScope[1], "LocalScript" ) )
	{
		Warning( "Failed to correctly clean up local script" );
		g_pScriptVM->ClearValue( m_hScriptScope[1], "LocalScript" );
	}
	m_hScriptScope[2] = g_pScriptVM->CreateScope( "LocalScript", m_hScriptScope[1] != INVALID_HSCRIPT ? m_hScriptScope[1] : NULL );
	if ( m_hDirectorOptions != INVALID_HSCRIPT ) { g_pScriptVM->ReleaseScope( m_hDirectorOptions ); m_hDirectorOptions = INVALID_HSCRIPT; }
	BindDirectorOptions();
}

int CDirector::GetChapterPanicWaveTier() const
{
	if ( m_nCurrentChapterIndex == 0 ) return 0;
	int chapterCount = TerrorGameRules()->GetMissionChapterCount();
	if ( m_nCurrentChapterIndex + 1 == chapterCount ) return 4;
	if ( chapterCount < 5 ) return m_nCurrentChapterIndex + 1;
	return MIN( m_nCurrentChapterIndex, 4 );
}
int CDirector::GetCommonLimit() const
{
	int limit = z_common_limit.GetInt();
	if ( limit == 0 ) return limit;
	// S276450's BYTE1 mutation override precedes ordinary finale policy.
	if ( m_pScriptedMode->IsEnabled() )
	{
		for ( int i = 0; i <= gpGlobals->maxClients; ++i )
		{
			CTerrorPlayer *player = ScenarioPlayer( i );
			if ( player && player->IsIT() && player->IsAlive() )
			{
				int bile = GetDirectorOptionInt( "BileMobSize", -1 );
				if ( bile >= 0 ) return bile;
			}
		}
		int mutationLimit = GetDirectorOptionInt( "cm_CommonLimit", -1 );
		if ( mutationLimit >= 0 ) return mutationLimit;
	}
	if ( m_pScenario->GetFinaleStage() == CDirectorScenario::FINALE_HORDE_ESCAPE && m_nHordeEscapeCommonLimit != -1 ) return m_nHordeEscapeCommonLimit;
	int type = m_pScenario->GetFinaleType();
	if ( type == 1 )
	{
		for ( int i = 0; i <= gpGlobals->maxClients; ++i )
		{
			CTerrorPlayer *player = ScenarioPlayer( i );
			if ( player && player->IsIT() && player->IsAlive() && player->GetTeamNumber() == TEAM_SURVIVOR ) return limit;
		}
	}
	if ( type == 2 || type == 3 || type == 4 )
	{
		for ( int i = 0; i <= gpGlobals->maxClients; ++i )
		{
			CTerrorPlayer *player = ScenarioPlayer( i );
			if ( player && player->IsIT() && player->IsAlive() ) return GetDirectorOptionInt( "BileMobSize", GetDirectorOptionInt( "CommonLimit", limit ) );
		}
	}
	return GetDirectorOptionInt( "CommonLimit", limit );
}
int CDirector::GetMegaMobSize() const
{
	int count = z_mega_mob_size.GetInt();
	return count > 1 ? GetDirectorOptionInt( "MegaMobSize", count ) : count;
}
int CDirector::GetMobSize() const
{
	return (int)GetDirectorOptionFloat( "MobSpawnSize", m_flMobSize );
}
void CDirector::ResetMobTimer()
{
	m_flMobSize = GetDirectorOptionFloat( "MobMinSize", z_mob_spawn_min_size.GetFloat() );
	m_flMobSizeIncrement = 0.0f;
	float maximum = GetMobSpawnMaxTime();
	float minimum = GetMobSpawnMinTime();
	m_MobSpawnTimer.Start( RandomFloat( minimum, maximum ) );
}
void CDirector::OnPanicEventCompleted()
{
	L4DGameStats().Event_PanicEventFinished();
}

bool CDirector::IsRescueVehicleReady() const
{
	if ( m_pScenario->GetFinaleType() == 5 ) return false;
	return m_nGameState == 1 && !( m_MissionEndTimer.m_timestamp > 0.0f );
}
void CTerrorPlayer::CapGhostSpawnClockForFinale()
{
	if ( IsGhost() && m_ghostSpawnClockMaxDelay > 2 )
	{
		m_ghostSpawnClockMaxDelay = 2;
		m_ghostSpawnClockCurrentDelay = 2;
	}
}

void CDirector::AddToSpecialSpawnTimers()
{
	float add = GetDirectorOptionFloat( "AddToSpawnTimer", 0.0f );
	if ( add == 0.0f ) return;
	for ( int i = 0; ; ++i )
	{
		int limit = GetMaxSpecialCount() <= 3 ? 3 : GetMaxSpecialCount();
		if ( i >= limit || i >= m_SpecialSpawnTimers.Count() ) break;
		float remaining = m_SpecialSpawnTimers[i].m_timestamp - gpGlobals->curtime;
		if ( remaining < 0.0f ) remaining = 0.0f;
		if ( add > remaining )
		{
			float duration = 0.0f;
			if ( add > 0.0f ) duration = RandomFloat( add * 0.5f, ( add * 3.0f ) * 0.5f ) + remaining;
			m_SpecialSpawnTimers[i].Start( duration );
		}
	}
}
bool CDirector::IsJimmyGibbsDay() const
{
	int day, month, year, dayOfYear;
	GetCurrentDate( &day, &month, &year );
	GetCurrentDayOfTheYear( &dayOfYear );
	return ( year > 2009 || dayOfYear >= 331 ) && ( 365 * year + dayOfYear - 733616 ) % 15 == 0;
}

int CDirector::GetTankCount() const { return m_nTankCount; }
int CDirector::GetWitchCount() const { return m_nWitchCount; }
void CDirector::OnWitchSpawn() { ++m_nWitchCount; }
void CDirector::OnWitchRemoved() { --m_nWitchCount; }

double CDirectorScenario::GetStageElapsedTime() const
{
	// Native COMISS/JBE sends nonpositive AND unordered timestamps to the
	// original float sentinel; do not turn this into a <=0-only check.
	if ( !( m_stageInterval.m_timestamp > 0.0f ) ) return (double)99999.8984375f;
	return (double)gpGlobals->curtime - (double)m_stageInterval.m_timestamp;
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
namespace
{
const char *const s_DebugFinaleStages[] =
{
	"GAUNTLET_1", "HORDE_ATTACK_1", "HALFTIME_BOSS", "GAUNTLET_2", "HORDE_ATTACK_2", "FINAL_BOSS", "HORDE_ESCAPE",
	"CUSTOM_PANIC", "CUSTOM_TANK", "CUSTOM_SCRIPTED", "CUSTOM_DELAY", "CUSTOM_CLEARNWAIT", "GAUNTLET_START",
	"GAUNTLET_HORDE", "GAUNTLET_HORDE_BONUSTIME", "GAUNTLET_BOSS_INCOMING", "GAUNTLET_BOSS", "GAUNTLET_ESCAPE",
	"CUSTOM_NONE", "CUSTOM_SETUP", "CUSTOM_SPECIAL_EVENT", "CUSTOM_ESCAPE", "CUSTOM_RESULTS"
};
const char *const s_DebugPanicStages[] = { "INITIAL_DELAY", "MEGA_MOB", "WAIT_FOR_COMBAT_TO_END", "PAUSE", "DONE" };
const char *const s_DebugScriptedStages[] =
{
	"STAGE_PANIC", "STAGE_TANK", "STAGE_DELAY", "STAGE_SCRIPTED", "STAGE_CLEAROUT", "STAGE_SETUP",
	"STAGE_SPECIAL_EVENT", "STAGE_ESCAPE", "STAGE_RESULTS", "STAGE_NONE"
};
int ScenarioDebugTruncate( double value ) { return _mm_cvttsd_si32( _mm_set_sd( value ) ); }
int ScenarioDebugCeilFloat( const CountdownTimer &timer )
{
	// CUSTOM stage diagnostics spill ceil's result tofloat BEFORE CVTTSS2SI.
	const float seconds = (float)ceil( (double)timer.m_timestamp - (double)gpGlobals->curtime );
	return _mm_cvttss_si32( _mm_set_ss( seconds ) );
}
}
int CDirectorScenario::DrawDebugText( int line )
{
	CFmtStrN<256, true> text;
	if ( TheDirector->GetScriptedMode()->IsActive() ) // Native byte0, not Enabled/byte1.
	{
		const int value = TheDirector->GetDirectorOptionInt( "ScriptedStageValue", 1 );
		const int type = TheDirector->GetDirectorOptionInt( "ScriptedStageType", 9 );
		// Main's S288520 undefined-native disposition: skip ONLY this row when
		// the script type would index outside its real ten-entry table.
		if ( static_cast< unsigned int >( type ) < ARRAYSIZE( s_DebugScriptedStages ) )
		{
			if ( type == 9 ) text.sprintf( "Mutation: %s Stage %s", TheDirector->GetGameMode(), s_DebugFinaleStages[m_nFinaleStage] );
			else text.sprintf( "Mutation: %s Stage %s val %d (script %s)", TheDirector->GetGameMode(), s_DebugFinaleStages[m_nFinaleStage], value, s_DebugScriptedStages[type] );
			DirectorDebugText( 0.01f, 0.0f, line++, text, 155, 255, 155, 255, 0.01023f );
		}
	}
	if ( m_nFinaleType == 1 )
	{
		++line; // Native blank row before GAUNTLET FINALE.
		DirectorDebugText( 0.01f, 0.0f, line++, "GAUNTLET FINALE", 255, 255, 255, 255, 0.01023f );
		text.sprintf( "  Common Limit = %d", TheDirector->GetCommonLimit() );
		DirectorDebugText( 0.01f, 0.0f, line++, text, 255, 255, 0, 255, 0.01023f );
		const int maximum = ScenarioDebugTruncate( TheDirector->GetMobSpawnMaxTime() ); // Native maximum first.
		const int minimum = ScenarioDebugTruncate( TheDirector->GetMobSpawnMinTime() );
		text.sprintf( "  Mob Spawn Time Range: %d to %d seconds", minimum, maximum );
		DirectorDebugText( 0.01f, 0.0f, line++, text, 255, 255, 0, 255, 0.01023f );
		text.sprintf( "  Max Mob Spawn Distance: %d", ScenarioDebugTruncate( TheZombieManager->GetZombieSpawnRange() ) );
		DirectorDebugText( 0.01f, 0.0f, line++, text, 255, 255, 0, 255, 0.01023f );
		text.sprintf( "  Movement threshold: %d", _mm_cvttss_si32( _mm_set_ss( m_flGauntletMovementThreshold ) ) );
		DirectorDebugText( 0.01f, 0.0f, line++, text, 255, 255, 0, 255, 0.01023f );
		text.sprintf( "  Movement time: %02d seconds.", ScenarioDebugTruncate( ceil( (double)m_gauntletMovementTimer.m_timestamp - (double)gpGlobals->curtime ) ) );
		DirectorDebugText( 0.01f, 0.0f, line++, text, 255, 255, 0, 255, 0.01023f );
		text.sprintf( "  Current Bonus: %02d seconds.", ScenarioDebugTruncate( ceil( (double)m_flGauntletMovementBonus ) ) );
		DirectorDebugText( 0.01f, 0.0f, line++, text, 255, 255, 0, 255, 0.01023f );
		if ( m_gauntletBonusTimer.m_timestamp > 0.0f )
		{
			text.sprintf( "  Bonus countdown: %2d seconds remaining.", ScenarioDebugTruncate( ceil( (double)m_gauntletBonusTimer.m_timestamp - (double)gpGlobals->curtime ) ) );
			DirectorDebugText( 0.01f, 0.0f, line++, text, 255, 255, 0, 255, 0.01023f );
		}
		++line;
	}
	if ( m_nFinaleType != 5 )
	{
		// Native first formats PANIC EVENT for type3, then OVERWRITES it below.
		if ( m_nFinaleType == 3 ) text.sprintf( "PANIC EVENT - CUSTOM (%c)", m_customFinaleChoice );
		if ( m_nFinaleType == 2 || m_nFinaleType == 3 || m_nFinaleType == 4 ) text.sprintf( "FINALE - CUSTOM (%c)", m_customFinaleChoice );
		else if ( m_nFinaleType == 1 ) text.sprintf( "FINALE - GAUNTLET" );
		else text.sprintf( "FINALE - STANDARD" );
		DirectorDebugText( 0.01f, 0.0f, line++, text, 0, 255, 0, 255, 0.01023f );
		switch ( m_nFinaleStage )
		{
		case FINALE_CUSTOM_PANIC:
			text.sprintf( "  stage #%d: %s (%d waves remaining, timer at %d)", m_nCustomStage, s_DebugFinaleStages[m_nFinaleStage], m_nPanicWaveCount - m_nPanicWave, ScenarioDebugCeilFloat( m_panicTimer ) );
			break;
		case FINALE_CUSTOM_TANK:
			text.sprintf( "  stage #%d: %s (%d tanks active)", m_nCustomStage, s_DebugFinaleStages[m_nFinaleStage], TheDirector->GetTankCount() );
			break;
		case FINALE_CUSTOM_SCRIPTED:
			text.sprintf( "  stage #%d: %s (scripted, at least %d seconds remaining)", m_nCustomStage, s_DebugFinaleStages[m_nFinaleStage], ScenarioDebugCeilFloat( m_minimumStageTimer ) );
			break;
		case FINALE_CUSTOM_DELAY:
			text.sprintf( "  stage #%d: %s (%d seconds remaining)", m_nCustomStage, s_DebugFinaleStages[m_nFinaleStage], ScenarioDebugCeilFloat( m_stageTimer ) );
			break;
		case FINALE_CUSTOM_CLEAROUT:
			text.sprintf( " stage #%d: %s (%d common/%d special, timer at %d)", m_nCustomStage, s_DebugFinaleStages[m_nFinaleStage], m_nDiagnosticClearoutCommons, m_nDiagnosticClearoutSpecials, ScenarioDebugCeilFloat( m_nativeTimer284 ) );
			break;
		case FINALE_GAUNTLET_ESCAPE:
			if ( TheDirector->GetTankCount() > 0 ) text.sprintf( "  escape stage: %s", s_DebugFinaleStages[m_nFinaleStage] );
			else
			{
				const float remaining = (float)( (double)m_bossTimer.m_timestamp - (double)gpGlobals->curtime );
				text.sprintf( "  escape stage: %s - %ds until next tank.", s_DebugFinaleStages[m_nFinaleStage], ScenarioDebugTruncate( ceil( remaining ) ) );
			}
			break;
		default: text.sprintf( "  stage: %s", s_DebugFinaleStages[m_nFinaleStage] ); break;
		}
		DirectorDebugText( 0.01f, 0.0f, line++, text, 255, 255, 0, 255, 0.01023f );
	}
	if ( m_nPanicStage == STAGE_DONE ) return line;
	DirectorDebugText( 0.01f, 0.0f, line++, "PANIC EVENT", 255, 255, 255, 255, 0.01023f );
	text.sprintf( "  stage: %s", s_DebugPanicStages[m_nPanicStage] );
	DirectorDebugText( 0.01f, 0.0f, line++, text, 255, 255, 0, 255, 0.01023f );
	text.sprintf( "  wave: %d (of %d total)", m_nPanicWave, m_nPanicWaveCount );
	DirectorDebugText( 0.01f, 0.0f, line++, text, 255, 255, 0, 255, 0.01023f );
	return line;
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

#include "l4d2/director/director_scenario_console_server_callbacks.inc"
