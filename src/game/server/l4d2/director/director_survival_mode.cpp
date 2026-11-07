//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/director/director_survival_mode.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/gamerules/terror_round_end.h"
#include "l4d2/director/zombiemanager.h"
#include <emmintrin.h>
#include "l4d2/director/director_survival_mode_console_server_cvars.inc"
#include "tier0/memdbgon.h"
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif

static ConVar director_survival_setup_time( "director_survival_setup_time", "0", 0x00000006, "Time limit to set up before the round auto-starts." );
static ConVar survival_round_restart_delay( "survival_round_restart_delay", "15", 0x00004006, "After a loss, restart the round after this many seconds." );
static ConVar survival_horde_stage_interval( "survival_horde_stage_interval", "60", 0x00004006, "Seconds between horde attacks" );
static ConVar survival_horde_stage_interval_decay( "survival_horde_stage_interval_decay", "2", 0x00004006, "After each horde attack, reduce the interval by this many seconds" );
static ConVar survival_tank_stage_interval( "survival_tank_stage_interval", "80", 0x00004006, "Seconds between tank attacks" );
static ConVar survival_tank_stage_interval_decay( "survival_tank_stage_interval_decay", "20", 0x00004006, "After some number of waves, reduce the interval by this many seconds" );
static ConVar survival_special_spawn_interval( "survival_special_spawn_interval", "20", 0x00004006, "Seconds between special spawn waves" );
static ConVar survival_special_spawn_interval_decay( "survival_special_spawn_interval_decay", "1", 0x00004006, "After each special wave, reduce the spawn interval by this many seconds" );
static ConVar survival_tank_multiple_spawn_delay( "survival_tank_multiple_spawn_delay", "10", 0x00004006, "Delay between the two tanks during double spawns." );
static ConVar survival_lull_time( "survival_lull_time", "15", 0x00004006, "Lull duration between survival waves." );
static ConVar survival_lull_time_increment( "survival_lull_time_increment", "15", 0x00004006, "Increment for the lull time per wave." );
static ConVar survival_lull_time_max( "survival_lull_time_max", "60", 0x00004006, "Max lull duration." );
static ConVar survival_special_stage_interval( "survival_special_stage_interval", "60", 0x00004006, "After this many seconds, increase special zombie limits" );
static ConVar survival_smoker_limit_increase( "survival_smoker_limit_increase", "1", 0x00004006, "After each special wave, increase max smokers by this amount." );
static ConVar survival_boomer_limit_increase( "survival_boomer_limit_increase", "0", 0x00004006, "After each special wave, increase max boomers by this amount." );
static ConVar survival_hunter_limit_increase( "survival_hunter_limit_increase", "1", 0x00004006, "After each special wave, increase max hunters by this amount." );
static ConVar survival_charger_limit_increase( "survival_charger_limit_increase", "0", 0x00004006, "After each special wave, increase max chargers by this amount." );
static ConVar survival_spitter_limit_increase( "survival_spitter_limit_increase", "1", 0x00004006, "After each special wave, increase max spitters by this amount." );
static ConVar survival_jockey_limit_increase( "survival_jockey_limit_increase", "1", 0x00004006, "After each special wave, increase max jockeys by this amount." );
static ConVar survival_special_limit_increase( "survival_special_limit_increase", "1", 0x00004006, "After each special wave, increase max specials by this amount." );
static ConVar survival_max_smokers( "survival_max_smokers", "4", 0x00004006, "Max smokers alive at one time." );
static ConVar survival_max_boomers( "survival_max_boomers", "2", 0x00004006, "Max boomers alive at one time." );
static ConVar survival_max_hunters( "survival_max_hunters", "3", 0x00004006, "Max hunters alive at one time." );
static ConVar survival_max_chargers( "survival_max_chargers", "2", 0x00004006, "Max chargers alive at one time." );
static ConVar survival_max_spitters( "survival_max_spitters", "3", 0x00004006, "Max spitters alive at one time." );
static ConVar survival_max_jockeys( "survival_max_jockeys", "2", 0x00004006, "Max jockeys alive at one time." );
static ConVar survival_max_specials( "survival_max_specials", "8", 0x00004006, "Max number of special zombies alive at one time." );
CON_COMMAND_F( survival_start, "Force the survival round to start", 0x00004006 )
{
	// Native28E8F0 has NO mode guard.
	TheDirector->GetSurvivalMode()->OnRoundStarted( 1, true );
}

CDirectorSurvivalMode::CDirectorSurvivalMode()
{
	// S28DE00: six CountdownTimer member ctors run before this WORD store.
	// CMemZeroOnNew supplies the other native allocation-zero fields.
	m_bStarted = m_bRestartPending = false;
}
void CDirectorSurvivalMode::Reset() { m_bStarted = false; m_Timers[3].Invalidate(); }
void CDirectorSurvivalMode::OnLevelChangeComplete() { m_bRestartPending = false; }
void CDirectorSurvivalMode::OnGameplayStart()
{
	m_Timers[5].Invalidate(); m_bRoundClockStarted = false;
	float setupTime = TheDirector->GetDirectorOptionFloat( "SurvivalSetupTime", director_survival_setup_time.GetFloat() );
	if ( setupTime > 0.0f )
	{
		m_Timers[5].Start( setupTime );
		// S28E040 FISTP with downward rounding; positive finite input is
		// truncation, and overflow/+infinity retain native INT_MIN.
		const int seconds = _mm_cvttss_si32( _mm_set_ss( setupTime ) );
		m_nNextSetupAnnouncement = seconds - seconds % 10;
	}
}
bool CDirectorSurvivalMode::OverrideSpecialRespawnInterval( float *interval ) const
{
	if ( !TerrorGameRules()->IsSurvivalMode() ) return false;
	*interval = m_flSpecialSpawnInterval; return true;
}
bool CDirectorSurvivalMode::OverrideMaxSpecials( int *count ) const
{
	if ( !m_bStarted ) return false;
	*count = m_SpecialLimits[6]; return true;
}
bool CDirectorSurvivalMode::OverrideSpecialClassLimit( int zombieClass, int *count ) const
{
	if ( !m_bStarted ) return false;
	*count = m_SpecialLimits[zombieClass - 1]; return true;
}
bool CDirectorSurvivalMode::OverrideTankLimit( int *count ) const
{
	if ( !m_bStarted ) return false;
	*count = m_nTankLimit; return true;
}
void CDirectorSurvivalMode::OnRoundStarted( int startType, bool startClock )
{
	if ( startClock && !m_bRoundClockStarted )
	{
		if ( g_pGameRules ) TerrorGameRules()->SetRoundStartTime( gpGlobals->curtime );
		m_bRoundClockStarted = true;
	}
	if ( m_bStarted ) return;
	DevMsg( "%3.2f: Director::OnSurvivalRoundStarted: Survival started.\n", gpGlobals->curtime );
	m_Timers[5].Invalidate();
	char script[256]; Q_snprintf( script, sizeof( script ), "%s_survival", STRING( gpGlobals->mapname ) );
	TheDirector->RunScript( script, 1, false );
	m_nStartType = startType; m_bStarted = true;
	m_nTanksThisStage = 1; m_nTanksSpawnedThisStage = m_nTotalTankStages = 0;
	for ( int i = 0; i < 6; ++i ) m_SpecialLimits[i] = 1;
	m_nTankLimit = 2; m_nWave = m_nTankStagesPerWave = 1; m_nCompletedTankStages = 0;
	m_SpecialLimits[6] = m_nMaxTankStagesPerWave = 3;
	m_flHordeInterval = survival_horde_stage_interval.GetFloat();
	m_flTankInterval = survival_tank_stage_interval.GetFloat();
	m_flSpecialSpawnInterval = survival_special_spawn_interval.GetFloat();
	m_flLullDuration = survival_lull_time.GetFloat();
	m_Timers[0].Start( 0.0f ); m_Timers[1].Start( survival_tank_stage_interval.GetFloat() * 0.5f );
	m_Timers[2].Start( survival_special_stage_interval.GetFloat() ); m_Timers[3].Start( 0.0f ); m_Timers[4].Invalidate();
	TheZombieManager->ClearSpawnAreaCollections();
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( player && player->IsConnected() ) player->OnSurvivalRoundStarted();
	}
	IGameEvent *event = gameeventmanager->CreateEvent( "survival_round_start" );
	if ( event ) gameeventmanager->FireEvent( event );
}
int CDirectorSurvivalMode::OnScriptedPanicEvent( CTerrorPlayer *player, int reason )
{
	if ( !TerrorGameRules()->IsSurvivalMode() ) return 2;
	OnRoundStarted( 1, true ); return 1;
}
static void RecordSurvivalTimes( bool humansOnly )
{
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( player && player->IsConnected() && ( !humansOnly || !player->IsBot() ) && player->m_bWasPresentAtSurvivalStart )
			player->UpdateSurvivalRecord( (float)TerrorGameRules()->GetTeamRoundDuration( TEAM_SURVIVOR ) );
	}
}
bool CDirectorSurvivalMode::HandleRoundEnd( TerrorRoundEndInfo *info )
{
	if ( info->reason < 14 || info->reason > 16 ) return false;
	if ( m_bRoundClockStarted && m_Timers[3].m_timestamp > 0.0f && g_pGameRules )
	{
		TerrorGameRules()->SetRoundEndTime( gpGlobals->curtime );
		if ( info->reason != 16 ) { RecordSurvivalTimes( true ); info->time = TerrorGameRules()->GetTeamRoundDuration( TEAM_SURVIVOR ); }
	}
	TheDirector->SetMissionEndDelay( info->reason == 14 ? survival_round_restart_delay.GetFloat() : 3.0f );
	m_bStarted = false;
	if ( info->reason == 14 ) info->message = "#L4D_Scenario_Restart";
	else
	{
		// The native collector/FL_FROZEN loop was optimized out of HexRays.
		// Full28EA20 ASM proves ANY team/alive nonghost/both controllers/class9.
		CUtlVector< CTerrorPlayer * > players; CollectTerrorPlayers( players, -1, 2, 3, 9 );
		FOR_EACH_VEC( players, i ) players[i]->AddFlag( FL_FROZEN );
		if ( info->reason == 15 ) TerrorShowViewPortPanel( -1, "survival_shutting_down", true, NULL );
	}
	return true;
}
void CDirectorSurvivalMode::EndRound()
{
	if ( m_bRestartPending ) TheDirector->FinishScenarioExit();
	else if ( TheDirector->GetRoundEndReason() == 15 ) { TheDirector->SetMissionEndDelay( 5.0f ); m_bRestartPending = true; }
	else if ( TheDirector->GetRoundEndReason() == 16 ) TheDirector->StartEndGameStats();
	else TheDirector->EndRound( 0 );
}
void CDirectorSurvivalMode::SpawnHorde()
{
	IGameEvent *event = gameeventmanager->CreateEvent( "create_panic_event" );
	if ( event ) { event->SetInt( "userid", 0 ); gameeventmanager->FireEvent( event ); }
	DevMsg( 2, "[SURVIVAL]: STAGE_MEGA_MOB\n" );
	// Unlike Scenario.WarnMegaMobIncoming, native28EC60 has no CommonLimit gate.
	CUtlVector< CTerrorPlayer * > players; CollectTerrorPlayers( players, -1, 7, 3, 9 );
	FOR_EACH_VEC( players, i )
	{
		CTerrorPlayer *player = players[i]; player->EmitSound( "MegaMobIncoming", 0.0f );
		if ( player->IsAlive() && player->GetTeamNumber() == TEAM_SURVIVOR ) player->SpeakIfAllowed( "PlayerWarnMegaMob", 1 );
	}
	TheDirector->CommandInfectedToAttackSurvivors();
	if ( TheDirector->GetMegaMobSize() > 1 ) TheZombieManager->RequestDelayedMegaMob();
}
bool CDirectorSurvivalMode::Update()
{
	if ( !TerrorGameRules()->IsSurvivalMode() ) return false;
	if ( !m_bStarted )
	{
		if ( m_Timers[5].m_timestamp > 0.0f )
		{
			if ( gpGlobals->curtime >= m_Timers[5].m_timestamp ) { OnRoundStarted( 1, false ); return true; }
			if ( m_nNextSetupAnnouncement > 0 && (double)m_nNextSetupAnnouncement >= (double)m_Timers[5].m_timestamp - (double)gpGlobals->curtime )
			{
				char seconds[8]; Q_snprintf( seconds, sizeof( seconds ), "%d", m_nNextSetupAnnouncement );
				UTIL_ClientPrintAll( 5, "#Survival_Spawn_Start_Warning", seconds ); m_nNextSetupAnnouncement -= 10;
			}
		}
		if ( !m_bStarted ) return true;
	}
	bool lullEnded = false;
	if ( m_Timers[4].m_timestamp > 0.0f )
	{
		if ( !( gpGlobals->curtime >= m_Timers[4].m_timestamp ) ) return true;
		DevMsg( 2, "[SURVIVAL]: Lull ended." ); m_Timers[4].Invalidate(); m_Timers[1].Start( m_flTankInterval ); lullEnded = true;
	}
	if ( gpGlobals->curtime >= m_Timers[0].m_timestamp || lullEnded )
	{
		SpawnHorde(); DevMsg( 2, "[SURVIVAL]: HORDE ATTACK!\n" ); m_Timers[0].Start( m_flHordeInterval );
		m_flHordeInterval -= survival_horde_stage_interval_decay.GetFloat();
		if ( !( m_flHordeInterval > 10.0f ) ) m_flHordeInterval = 10.0f;
		DevMsg( 2, "[SURVIVAL]: Horde Stage interval: %f\n", m_flHordeInterval );
	}
	if ( gpGlobals->curtime >= m_Timers[2].m_timestamp )
	{
		// Native107A66D8/F4 order is literal: do not 'repair' indices3..5.
		static ConVar *increase[] = { &survival_smoker_limit_increase, &survival_boomer_limit_increase, &survival_hunter_limit_increase, &survival_charger_limit_increase, &survival_spitter_limit_increase, &survival_jockey_limit_increase, &survival_special_limit_increase };
		static ConVar *maximum[] = { &survival_max_smokers, &survival_max_boomers, &survival_max_hunters, &survival_max_chargers, &survival_max_spitters, &survival_max_jockeys, &survival_max_specials };
		for ( int i = 0; i < 7; ++i )
		{
			m_SpecialLimits[i] = (int)( (unsigned int)m_SpecialLimits[i] + (unsigned int)increase[i]->GetInt() );
			const int limit = maximum[i]->GetInt();
			if ( m_SpecialLimits[i] > limit ) m_SpecialLimits[i] = limit;
		}
		const double nextStage = (double)gpGlobals->curtime + (double)m_Timers[2].m_duration.Get();
		if ( (double)m_Timers[2].m_timestamp.Get() != nextStage ) m_Timers[2].m_timestamp.SetDirect( (float)nextStage );
		m_flSpecialSpawnInterval -= survival_special_spawn_interval_decay.GetFloat();
		if ( !( m_flSpecialSpawnInterval > 1.0f ) ) m_flSpecialSpawnInterval = 1.0f;
		DevMsg( 2, "[SURVIVAL]: Special Spawn Interval: %f\n", m_flSpecialSpawnInterval );
	}
	Vector position;
	if ( gpGlobals->curtime >= m_Timers[1].m_timestamp && TheZombieManager->GetRandomPZSpawnPosition( 8, 10, NULL, &position ) )
	{
		QAngle angles( 0.0f, RandomFloat( 0.0f, 360.0f ), 0.0f );
		CTerrorPlayer *tank = TheZombieManager->SpawnTank( position, angles );
		if ( tank )
		{
			if ( TerrorGameRules()->IsPlayerControlledZombies() ) TheDirector->OfferTank( tank, true );
			static const int tanksPerStage[] = {1,1,1,1,2,1,1,1,1,1,2};
			m_nTanksThisStage = (unsigned int)m_nTotalTankStages >= ARRAYSIZE( tanksPerStage ) ? 2 : tanksPerStage[m_nTotalTankStages];
			if ( TerrorGameRules()->IsPlayerControlledZombies() ) m_nTanksThisStage = TheDirector->GetTankLimit() >= m_nTanksThisStage ? m_nTanksThisStage : TheDirector->GetTankLimit();
			DevMsg( 2, "[SURVIVAL]: TANK ATTACK! Tanks this stage: %d (Stage %d)\n", m_nTanksThisStage, m_nTotalTankStages );
			float delay;
			m_nTanksSpawnedThisStage = (int)( (unsigned int)m_nTanksSpawnedThisStage + 1u );
			if ( m_nTanksSpawnedThisStage >= m_nTanksThisStage )
			{
				m_nCompletedTankStages = (int)( (unsigned int)m_nCompletedTankStages + 1u );
				if ( m_nCompletedTankStages >= m_nTankStagesPerWave )
				{
					m_nWave = (int)( (unsigned int)m_nWave + 1u );
					m_nCompletedTankStages = 0;
					m_Timers[4].Start( m_flLullDuration );
					DevMsg( 2, "[SURVIVAL]: Starting a lull. Next wave: %d\n", m_nWave );
					double elapsed = (double)gpGlobals->curtime - (double)m_Timers[3].m_timestamp + (double)m_Timers[3].m_duration.Get();
					if ( !( elapsed < 600.0 ) )
					{
						m_flLullDuration -= survival_lull_time_increment.GetFloat(); float minimum = survival_lull_time.GetFloat();
						if ( !( m_flLullDuration > minimum ) ) m_flLullDuration = minimum;
					}
					else
					{
						m_flLullDuration += survival_lull_time_increment.GetFloat(); float maximum = survival_lull_time_max.GetFloat();
						if ( !( maximum > m_flLullDuration ) ) m_flLullDuration = maximum;
					}
					if ( m_nWave % 2 == 0 )
					{
						m_nTankStagesPerWave = (int)( (unsigned int)m_nTankStagesPerWave + 1u );
						if ( m_nTankStagesPerWave >= m_nMaxTankStagesPerWave ) m_nTankStagesPerWave = m_nMaxTankStagesPerWave;
						m_flTankInterval -= survival_tank_stage_interval_decay.GetFloat(); if ( !( m_flTankInterval > 20.0f ) ) m_flTankInterval = 20.0f;
						DevMsg( 2, "[SURVIVAL]: Tank Stage interval: %f\n", m_flTankInterval );
					}
				}
				m_nTotalTankStages = (int)( (unsigned int)m_nTotalTankStages + 1u );
				m_nTanksSpawnedThisStage = 0;
				delay = m_flTankInterval;
			}
			else delay = survival_tank_multiple_spawn_delay.GetFloat();
			m_Timers[1].Start( delay );
		}
	}
	return true;
}

namespace
{
int SurvivalDebugSeconds( const CountdownTimer &timer )
{
	int seconds = _mm_cvttsd_si32( _mm_set_sd( ceil( (double)timer.m_timestamp - (double)gpGlobals->curtime ) ) );
	if ( seconds < 0 ) return 0;
	// Native28E080 intentionally samples again on the nonnegative path.
	return _mm_cvttsd_si32( _mm_set_sd( ceil( (double)timer.m_timestamp - (double)gpGlobals->curtime ) ) );
}
}
int CDirectorSurvivalMode::DrawDebugText( int line )
{
	CFmtStrN<256, true> text;
	if ( !m_bStarted ) return line;
	TerrorInfectedClassCounts counts( false );
	counts.Count();
	DirectorDebugText( 0.01f, 0.0f, line++, "SURVIVAL MODE", 255, 255, 255, 255, 0.01023f );
	for ( int i = 0; i < 6; ++i )
	{
		text.sprintf( "  %d %sS (%d max)", counts.special[i], GetZombieClassName( i + 1 ), m_SpecialLimits[i] );
		DirectorDebugText( 0.01f, 0.0f, line++, text, 255, 255, 0, 255, 0.01023f );
	}
	if ( m_bStarted ) // Native repeats this state read after the six rows.
	{
		const int limit = m_SpecialLimits[6];
		const int teamCount = TheDirector->GetTeamPlayerCount( TEAM_INFECTED, 2 );
		text.sprintf( "  %d Specials on Team (%d max)", teamCount - counts.tanks, limit );
		DirectorDebugText( 0.01f, 0.0f, line++, text, 255, 255, 0, 255, 0.01023f );
	}
	text.sprintf( "  %d Tanks (%d max)", counts.tanks, m_nTankLimit );
	DirectorDebugText( 0.01f, 0.0f, line++, text, 255, 255, 0, 255, 0.01023f );
	int seconds = SurvivalDebugSeconds( m_Timers[0] );
	text.sprintf( "  [%02ds] %s (Interval: %f)", seconds, "Horde Wave", (double)m_flHordeInterval );
	DirectorDebugText( 0.01f, 0.0f, line++, text, 255, 255, 0, 255, 0.01023f );
	seconds = SurvivalDebugSeconds( m_Timers[1] );
	text.sprintf( "  [%02ds] %s (Interval: %f)", seconds, "Tank Wave", (double)m_flTankInterval );
	DirectorDebugText( 0.01f, 0.0f, line++, text, 255, 255, 0, 255, 0.01023f );
	seconds = SurvivalDebugSeconds( m_Timers[2] );
	text.sprintf( "  [%02ds] %s (Interval: %f)", seconds, "Special Wave", (double)m_flSpecialSpawnInterval );
	DirectorDebugText( 0.01f, 0.0f, line++, text, 255, 255, 0, 255, 0.01023f );
	seconds = SurvivalDebugSeconds( m_Timers[4] );
	text.sprintf( "  [%02ds] %s (Interval: %f)", seconds, "Lull", (double)m_flLullDuration );
	DirectorDebugText( 0.01f, 0.0f, line++, text, 255, 255, 0, 255, 0.01023f );
	return line;
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
