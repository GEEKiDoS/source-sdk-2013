//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/gamerules/terror_team_size.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/director/director_survival_mode.h"
#include "l4d2/director/director_versus_mode.h"
#include "l4d2/director/director_scavenge_mode.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/director/zombiemanager.h"
#include "l4d2/director/zombie_spawn.h"
#include "l4d2/nav/terror_nav.h"
#include "l4d2/entities/zombie_border.h"
#include "l4d2/nav/escape_route.h"
#include "l4d2/director/director_scenario.h"
#include "l4d2/director/terror_director_limits_console_server_cvars.inc"
#include "tier0/memdbgon.h"

ConVar z_max_player_zombies( "z_max_player_zombies", "4", 0x00002006, "Max number of humans that can be controlling zombies at one time.", true, 0.0f, true, 4.0f, OnMaxPlayerZombiesChanged );
ConVar z_smoker_limit( "z_smoker_limit", "1", 0x00004004 );
ConVar z_boomer_limit( "z_boomer_limit", "1", 0x00004004 );
ConVar z_hunter_limit( "z_hunter_limit", "1", 0x00004004 );
ConVar z_spitter_limit( "z_spitter_limit", "1", 0x00004004 );
ConVar z_jockey_limit( "z_jockey_limit", "1", 0x00004004 );
ConVar z_charger_limit( "z_charger_limit", "1", 0x00004004 );
static ConVar director_special_respawn_interval( "director_special_respawn_interval", "45", 0x00004004 );
static ConVar versus_special_respawn_interval( "versus_special_respawn_interval", "20", 0x00004004 );
ConVar director_no_bosses( "director_no_bosses", "0", 0x00004004, "Disable boss spawns" );
static ConVar director_always_allow_wanderers( "director_always_allow_wanderers", "0", 0x00004004 );
static ConVar director_intensity_relax_allow_wanderers_threshold( "director_intensity_relax_allow_wanderers_threshold", "0.3", 0x00004004, "All survivors must be below this intensity during RELAX for wandering zombies to be spawned" );
static ConVar director_intensity_relax_allow_wanderers_threshold_hard( "director_intensity_relax_allow_wanderers_threshold_hard", "0.5", 0x00004004, "All survivors must be below this intensity during RELAX for wandering zombies to be spawned" );
static ConVar director_intensity_relax_allow_wanderers_threshold_expert( "director_intensity_relax_allow_wanderers_threshold_expert", "0.8", 0x00004004, "All survivors must be below this intensity during RELAX for wandering zombies to be spawned" );
extern bool IsMapTimeOfDay( int first, int last ); // Actual sole terror_world_time.cpp235FB0, not a local clock.

bool CDirectorVersusMode::OverrideSpecialRespawnInterval( float *interval ) const
{
	// Native290470 checks player-controlled-zombies metadata, NOT only versus.
	if ( !TerrorGameRules()->IsPlayerControlledZombies() ) return false;
	*interval = versus_special_respawn_interval.GetFloat(); return true;
}
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
double CDirector::GetSpecialRespawnInterval() const
{
	float interval = director_special_respawn_interval.GetFloat();
	if ( m_pScriptedMode->IsEnabled() )
	{
		float value = GetDirectorOptionFloat( "cm_SpecialRespawnInterval", -1.0f );
		if ( value >= 0.0f ) return value; // Native FCOMIP/JB rejects unordered too.
	}
	if ( m_pSurvivalMode->OverrideSpecialRespawnInterval( &interval ) || m_pVersusMode->OverrideSpecialRespawnInterval( &interval ) ) return interval;
	// Native287910 is Scenario, not Scavenge. Its out-write is real, but its
	// return is always false, so SpecialRespawnInterval still has final say.
	m_pScenario->OverrideSpecialRespawnInterval( &interval );
	return GetDirectorOptionFloat( "SpecialRespawnInterval", interval );
}
double CDirector::GetSpecialSlotCountdownTime() const
{
	float interval = GetSpecialRespawnInterval(); // Native stores/reloads float.
	if ( m_pScriptedMode->IsEnabled() )
	{
		float value = GetDirectorOptionFloat( "cm_SpecialSlotCountdownTime", -1.0f );
		if ( value >= 0.0f ) return value;
	}
	return GetDirectorOptionFloat( "GetSpecialSlotCountdownTime", interval );
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
int CDirector::GetMaxSpecialCount() const
{
	if ( m_pScriptedMode->IsEnabled() )
	{
		int value = GetDirectorOptionInt( "cm_MaxSpecials", -1 );
		if ( value >= 0 ) return value;
	}
	int count;
	if ( m_pSurvivalMode->OverrideMaxSpecials( &count ) ) return count;
	if ( TerrorGameRules()->IsPlayerControlledZombies() ) return z_max_player_zombies.GetInt();
	static const int counts[4][5] = { {1,1,1,2,2}, {1,2,2,3,3}, {1,2,3,3,3}, {3,3,3,3,3} };
	int chapter = GetCampaignMapType();
	if ( chapter > 4 ) chapter = 4; else if ( chapter < 0 ) chapter = 0;
	return GetDirectorOptionInt( "MaxSpecials", counts[TerrorGameRules()->GetDifficultyLevel()][chapter] );
}
int CDirector::GetSpecialClassLimit( int zombieClass ) const
{
	if ( m_pScriptedMode->IsEnabled() )
	{
		// Native275700 ignores the passed class: ONE cm_BaseSpecialLimit key.
		int value = GetDirectorOptionInt( "cm_BaseSpecialLimit", -1 );
		if ( value >= 0 ) return value;
	}
	int limit;
	if ( m_pSurvivalMode->OverrideSpecialClassLimit( zombieClass, &limit ) ) return limit;
	static ConVar *limits[] = { NULL, &z_smoker_limit, &z_boomer_limit, &z_hunter_limit, &z_spitter_limit, &z_jockey_limit, &z_charger_limit };
	// Actual reached native callers supply classes1..6. Do not add a default
	// class/cap, or silently turn common/witch/tank into an ordinary special.
	return limits[zombieClass]->GetInt();
}
int CDirector::GetTankLimit() const
{
	int limit = -1;
	if ( m_pScriptedMode->IsEnabled() )
	{
		int value = GetDirectorOptionInt( "cm_TankLimit", -1 );
		if ( value >= 0 ) return value;
	}
	if ( m_pSurvivalMode->OverrideTankLimit( &limit ) ) return limit;
	return GetDirectorOptionInt( "TankLimit", limit );
}
bool CDirector::AreBossesProhibited() const
{
	if ( m_pScriptedMode->IsEnabled() )
	{
		int value = GetDirectorOptionInt( "cm_ProhibitBosses", -1 );
		if ( value >= 0 ) return value != 0;
	}
	// Actual native285810/2904D0 mode overrides; not absent-service defaults.
	if ( TerrorGameRules()->IsScavengeMode() ) return true;
	if ( TerrorGameRules()->IsVersusMode() ) return false;
	return GetDirectorOptionInt( "ProhibitBosses", director_no_bosses.GetBool() ) != 0;
}

ConVar z_wandering_density( "z_wandering_density", "0.03", 0x00004004 );
static ConVar versus_wandering_zombie_density( "versus_wandering_zombie_density", "0.027", 0x00004014 );
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
double CDirector::GetWanderingZombieDensityModifier() const
{
	float density = z_wandering_density.GetFloat();
	if ( m_pScriptedMode->IsEnabled() )
	{
		float value = GetDirectorOptionFloat( "cm_WanderingZombieDensityModifier", -1.0f );
		if ( value >= 0.0f ) return value; // Native rejects unordered via FCOMIP/JB.
	}
	if ( TerrorGameRules()->IsVersusMode() ) return versus_wandering_zombie_density.GetFloat();
	return GetDirectorOptionFloat( "WanderingZombieDensityModifier", density );
}

float CDirector::GetMaximumSurvivorIntensity() const
{
	// Native260820/25E750. Promote the existing Frame collector, do not keep
	// an anonymous duplicate or substitute network clientIntensity12052.
	float maximum = 0.0f;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CBasePlayer *base = UTIL_PlayerByIndex( i );
		if ( !base || !base->edict() || base->entindex() == 0 || !base->IsPlayer() || !base->IsConnected() || !base->IsAlive() || base->GetTeamNumber() != TEAM_SURVIVOR ) continue;
		const float intensity = static_cast< CTerrorPlayer * >( base )->GetIntensity();
		if ( intensity > maximum ) maximum = intensity;
	}
	return maximum;
}

bool CDirector::ShouldSpawnWanderers() const
{
	// Native2857E0's ONLY override is false before real scavenge start.
	if ( g_TerrorModes.IsBaseMode( "scavenge" ) && !m_pScavengeMode->HasStarted() ) return false;
	if ( GetDirectorOptionInt( "AlwaysAllowWanderers", director_always_allow_wanderers.GetBool() ) != 0 ) return true;
	if ( m_nTankCount > 0 && HasSurvivorsAdvancedBeyondTank() ) return true; // Sole actual25C740, boss_bot_director_queries.cpp.
	if ( m_nIntensityState != 3 && m_nIntensityState != 2 ) return true;
	// Native2648D0: time-of-day2..4, NOT player count, triples this FLOAT
	// before the actual option lookup. Expert/hard defaults differ.
	float threshold = director_intensity_relax_allow_wanderers_threshold.GetFloat();
	const int difficulty = GetDifficulty();
	if ( difficulty == 3 ) threshold = director_intensity_relax_allow_wanderers_threshold_expert.GetFloat();
	else if ( difficulty == 2 ) threshold = director_intensity_relax_allow_wanderers_threshold_hard.GetFloat();
	if ( IsMapTimeOfDay( 2, 4 ) ) threshold *= 3.0f;
	threshold = GetDirectorOptionFloat( "IntensityRelaxAllowWanderersThreshold", threshold );
	const float maximum = GetMaximumSurvivorIntensity();
	return threshold > maximum; // Native COMISS/JBE rejects unordered too.
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif

extern ConVar director_no_specials;
extern ConVar z_debug;
static ConVar director_force_witch( "director_force_witch", "0", 0x00004004 );
static ConVar director_force_tank( "director_force_tank", "0", 0x00004004 );

// S264A10/275780: only a nonnegative enabled-script override wins.
int CDirector::GetWitchLimit() const
{
	if ( m_pScriptedMode->IsEnabled() )
	{
		const int limit = GetDirectorOptionInt( "cm_WitchLimit", -1 );
		if ( limit >= 0 )
			return limit;
	}
	return GetDirectorOptionInt( "WitchLimit", -1 );
}

// S265FD0. Keep the original short-circuit order: an allowed Tank consumes
// its draw before even the final disallowed-type comparison. A discarded
// threat advances the same real three-entry shuffle, never a second queue.
int CDirector::GetNextThreatType()
{
	int type = m_ThreatOrder[m_nThreatOrderIndex];
	const int disallowed = GetDirectorOptionInt( "DisallowThreatType", 0 );
	while ( ( type == 8 || type == disallowed ) &&
		( !m_bAllowTankThreat || RandomInt( 1, 100 ) > 75 || type == disallowed ) )
	{
		if ( ++m_nThreatOrderIndex >= 3 )
			ResetThreatPriorities( 9 );
		type = m_ThreatOrder[m_nThreatOrderIndex];
	}
	return type;
}

// S274BC0. This is CDirector's actual NAV threat callback, not a manager
// alias. In particular TRUE can mean a failed actor spawn, skipped Tank,
// or a no-actor threat. NAV consumes the threat bit using this return value.
bool CDirector::SpawnThreat( TerrorNavArea *area )
{
	if ( AreBossesProhibited() || IsInCommentaryMode() || director_no_specials.GetBool() ||
		m_pVersusMode->IsActive() || m_pScenario->GetFinaleType() != 5 ||
		TerrorGameRules()->IsSurvivalMode() ||
		( m_pScriptedMode->IsEnabled() && GetDirectorOptionInt( "IgnoreNavThreatAreas", 0 ) != 0 ) )
		return false;
	if ( !area || area->HasSpawnAttributes( 0x40 ) )
		return false;

	int type = GetNextThreatType();
	if ( director_force_witch.GetBool() )
		type = 7;
	else if ( director_force_tank.GetBool() )
		type = 8;

	if ( type == 7 || type == 8 )
	{
		const float width = type == 7 ? 26.0f : 32.0f;
		const float height = type == 7 ? 68.0f : 72.0f;
		TerrorNavArea *candidate = area;
		bool valid = true;
		bool useRandomArea = false;
		int attempts = 0;
		for ( ;; )
		{
			++attempts;
			if ( valid && !IsTerrorSpawnHullVisibleToTeam( candidate->GetCenter(),
				TEAM_SURVIVOR, 3, candidate, width, height ) )
			{
				area = candidate;
				break;
			}
			candidate = candidate->GetNextHighestFlowConnectedArea( NULL );
			valid = true;
			if ( !candidate || candidate == TheTerrorNavMesh()->GetGoalArea() ||
				CZombieBorder::IsPointBeyondBorder( candidate->GetCenter() ) )
			{
				useRandomArea = true;
				break;
			}
			if ( candidate->HasSpawnAttributes( 0x80000 ) || candidate->GetSizeX() < 24.0f || candidate->GetSizeY() < 24.0f )
				valid = false;
			if ( attempts >= 100 )
			{
				// Native has not assigned area=candidate on this path. A
				// valid100th successor retains the original threat area.
				useRandomArea = !valid;
				break;
			}
		}
		if ( useRandomArea )
		{
			if ( z_debug.GetBool() )
				Msg( "Tank/Witch threat spawn: No non-visible valid areas between threat area and goal area! Choosing random area.\n" );
			for ( int attempt = 0; attempt < 5; ++attempt )
			{
				Vector position;
				// Parameter2 is TRIES, not SPAWN_FAR_AWAY despite equal5.
				if ( TheZombieManager->GetRandomPZSpawnPosition( 8, 5, NULL, &position ) )
				{
					TerrorNavArea *randomArea = ToTerrorNavArea( TheNavMesh->GetNavArea( position, 120.0f ) );
					if ( randomArea && !randomArea->HasSpawnAttributes( 0x90882 ) &&
						!CZombieBorder::IsPointBeyondBorder( randomArea->GetCenter() ) )
					{
						area = randomArea;
						break;
					}
				}
			}
			// Failure of all five requests retains the original area.
		}
	}

	if ( type == 7 )
	{
		QAngle angles( 0.0f, RandomFloat( 0.0f, 360.0f ), 0.0f );
		if ( !TheZombieManager->SpawnWitchInArea( area, angles, true ) )
			return true;
		ClearWanderingPopulationAroundBoss( area );
	}
	else if ( type == 8 && ( m_nTankCount == 0 || !TerrorGameRules()->IsPlayerControlledZombies() ) )
	{
		QAngle angles( 0.0f, RandomFloat( 0.0f, 360.0f ), 0.0f );
		CTerrorPlayer *tank = TheZombieManager->SpawnTankInArea( area, angles, true );
		if ( !tank )
		{
			Vector position;
			if ( TheZombieManager->GetRandomPZSpawnPosition( 8, 10, NULL, &position ) )
			{
				QAngle fallbackAngles( 0.0f, RandomFloat( 0.0f, 360.0f ), 0.0f );
				tank = TheZombieManager->SpawnTank( position, fallbackAngles );
			}
		}
		if ( !tank )
			return true;
		m_bAllowTankThreat = false;
		OfferTank( tank, true );
	}
	if ( ++m_nThreatOrderIndex >= 3 )
		ResetThreatPriorities( type );
	return true;
}

#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
