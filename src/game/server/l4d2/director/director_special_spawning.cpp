//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/director/director_survival_mode.h"
#include "l4d2/director/director_scenario.h"
#include "l4d2/entities/trigger_finale.h"
#include "l4d2/director/zombiemanager.h"
#include "vstdlib/random.h"
#include "tier0/memdbgon.h"

extern ConVar director_no_specials; // One existing director_frame.cpp owner.
ConVar director_special_initial_spawn_delay_min( "director_special_initial_spawn_delay_min", "30", 0x00004004, "" );
ConVar director_special_initial_spawn_delay_max( "director_special_initial_spawn_delay_max", "60", 0x00004004, "" );
ConVar director_special_initial_spawn_delay_max_extra( "director_special_initial_spawn_delay_max_extra", "180", 0x00004004, "" );

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif

// S264A50 / native root virtual92. Native scripted S275820 really returns2
// (no override); it does not consult a fabricated script option or mode flag.
bool CDirector::AreSpecialSpawnsAllowed() const
{
	if ( TerrorGameRules()->IsSurvivalMode() ) return m_pSurvivalMode->HasStarted();
	if ( TerrorGameRules()->IsPlayerControlledZombies() ) return true;
	if ( !HasSurvivorsAdvancedBeyondTank() &&
		!( m_pScenario->GetFinaleType() == 1 && m_pScenario->GetFinaleStage() == 17 ) &&
		GetDirectorOptionInt( "ShouldAllowSpecialsWithTank", 0 ) == 0 &&
		( m_nIntensityState == 3 || m_nIntensityState == 2 || m_nTankCount > 0 ) )
		return false;
	if ( m_pScenario->AreSurvivorsInFinaleAreaBeforePanic() &&
		m_pScenario->GetPanicStage() == 4 && m_pScenario->GetFinaleStage() != 9 &&
		m_pScenario->GetFinaleType() != 1 )
		return false;
	// S25F810 / S25BB10: all living TEAM2, including incapacitated; empty is
	// true, and the attribute is FINALE0x40, not BATTLEFIELD0x100.
	if ( ::AreAllSurvivorsInFinaleArea( true ) && m_pScenario->GetFinaleType() == 5 ) return false;
	return true;
}

// S2656A0 / native root virtual116, sampled after the live class census.
int CDirector::GetDominatorLimit() const
{
	return GetDirectorOptionInt( "DominatorLimit", 2 );
}

// S2656B0 / native root virtual52. Class0 really is admitted immediately.
// The six-class case uses the existing limit query and real population totals.
bool CDirector::IsZombieClassSpawnAllowed( int zombieClass ) const
{
	if ( zombieClass == 0 ) return true;
	if ( ( zombieClass == 8 || zombieClass == 7 ) && AreBossesProhibited() ) return false;
	if ( TerrorGameRules()->IsSurvivalMode() && zombieClass == 7 ) return false;
	const unsigned int classIndex = (unsigned int)zombieClass - 1u;
	if ( classIndex <= 5u )
	{
		static const char *const limitKeys[6] = { "SmokerLimit", "BoomerLimit", "HunterLimit", "SpitterLimit", "JockeyLimit", "ChargerLimit" };
		static const char *const totalKeys[6] = { "TotalSmokers", "TotalBoomers", "TotalHunters", "TotalSpitters", "TotalJockeys", "TotalChargers" };
		static const bool dominator[6] = { true, false, true, false, true, true };
		const int limit = GetDirectorOptionInt( limitKeys[classIndex], GetSpecialClassLimit( zombieClass ) );
		if ( limit == 0 ) return false;
		if ( m_pScriptedMode->IsActive() )
		{
			const int total = GetDirectorOptionInt( totalKeys[classIndex], 0 );
			if ( total != 0 && TheZombieManager->GetPanicSpecialSpawnCount( zombieClass ) >= total ) return false;
		}
		TerrorInfectedClassCounts counts( false );
		counts.Count();
		if ( dominator[classIndex] )
		{
			int dominatorTypes = 0;
			for ( int i = 0; i < 6; ++i ) if ( dominator[i] && counts.special[i] > 0 ) ++dominatorTypes;
			// Native counts TYPES, not individual pinning zombies, and rejects
			// only GREATER THAN the limit. Preserve the equality admission.
			if ( dominatorTypes > GetDominatorLimit() ) return false;
		}
		return counts.special[classIndex] < limit;
	}
	if ( m_pScenario->GetFinaleType() != 5 || m_pSurvivalMode->HasStarted() ) return true;
	if ( zombieClass == 7 ) return !IsInCommentaryMode();
	if ( zombieClass != 8 ) return true;
	if ( !( m_SpecialSpawnTimer.m_timestamp.Get() > 0.0f ) || !m_SpecialSpawnTimer.IsElapsed() ) return false;
	int limit = GetTankLimit();
	if ( limit == -1 ) limit = 1;
	return !IsInCommentaryMode() && m_nTankCount < limit;
}

// S26F910 / native root virtual88. This is the GENERAL Director spawner,
// not an extra survival-only factory/clock. Death S2707E0 is its existing
// class-interval/slot-timer release writer and the frame owns live counts.
void CDirector::UpdateSpecialSpawning()
{
	if ( director_no_specials.GetBool() || IsInCommentaryMode() || !m_bHasAnySurvivorLeftSafeArea ) return;
	if ( m_pScriptedMode->IsActive() )
	{
		const int total = GetDirectorOptionInt( "TotalSpecials", 0 );
		if ( total != 0 && TheZombieManager->GetTotalPanicSpecialSpawns() >= total ) return;
	}
	if ( !( m_BaseSpecialSpawnTimers[0].m_timestamp.Get() > 0.0f ) )
	{
		for ( int i = 0; i < 6; ++i )
		{
			// Original samples both options independently for EVERY timer,
			// max before min, then RandomFloat. Do not hoist them out.
			const float maximum = GetDirectorOptionFloat( "SpecialInitialSpawnDelayMax", director_special_initial_spawn_delay_max.GetFloat() );
			const float minimum = GetDirectorOptionFloat( "SpecialInitialSpawnDelayMin", director_special_initial_spawn_delay_min.GetFloat() );
			m_BaseSpecialSpawnTimers[i].Start( RandomFloat( minimum, maximum ) );
		}
		if ( !Q_strcmp( STRING( gpGlobals->mapname ), "l4d_airport02_offices" ) )
		{
			const float maximum = director_special_initial_spawn_delay_max_extra.GetFloat();
			const float minimum = GetDirectorOptionFloat( "SpecialInitialSpawnDelayMin", director_special_initial_spawn_delay_min.GetFloat() );
			m_BaseSpecialSpawnTimers[0].Start( RandomFloat( minimum, maximum ) );
		}
	}
	if ( !AreSpecialSpawnsAllowed() ) return;
	int slots = GetMaxSpecialCount();
	if ( slots <= 3 ) slots = 3;
	if ( TerrorGameRules()->IsPlayerControlledZombies() )
	{
		slots = (int)( (unsigned int)slots - (unsigned int)m_nHumanInfectedCount );
		if ( slots < 0 ) slots = 0;
	}
	// Same genuine fixed-growable3 vector; newly added timers are invalid.
	// Shrinking destroys no actor or class timer. No capacity policy/clamp.
	if ( slots != m_SpecialSpawnTimers.Count() ) m_SpecialSpawnTimers.SetCountNonDestructively( slots );
	const float interval = (float)GetSpecialRespawnInterval();
	if ( m_nSpecialCount >= GetMaxSpecialCount() ) return;
	for ( ;; )
	{
		int slot = 0;
		while ( slot < slots && !( (double)gpGlobals->curtime >= (double)m_SpecialSpawnTimers[slot].m_timestamp.Get() ) ) ++slot;
		if ( slot == slots ) return;
		int candidates[6];
		int candidateCount = 0;
		for ( int zombieClass = 1; zombieClass <= 6; ++zombieClass )
		{
			CountdownTimer &baseTimer = m_BaseSpecialSpawnTimers[zombieClass - 1];
			if ( !( (double)gpGlobals->curtime >= (double)baseTimer.m_timestamp.Get() ) ) continue;
			const float timestamp = m_ClassSpawnIntervals[zombieClass].m_timestamp.Get();
			const float elapsed = timestamp > 0.0f ? (float)( (double)gpGlobals->curtime - (double)timestamp ) : 99999.9f;
			if ( !( elapsed > interval ) ) continue;
			if ( IsZombieClassSpawnAllowed( zombieClass ) ) candidates[candidateCount++] = zombieClass;
			else baseTimer.Start( 20.0f );
		}
		if ( candidateCount == 0 ) return;
		CTerrorPlayer *spawned;
		for ( ;; )
		{
			const int index = RandomInt( 0, candidateCount - 1 );
			const int zombieClass = candidates[index];
			Vector position;
			spawned = NULL;
			if ( TheZombieManager->GetRandomPZSpawnPosition( zombieClass, 5, NULL, &position ) )
			{
				QAngle angles( 0.0f, RandomFloat( 0.0f, 360.0f ), 0.0f );
				// Sole population dispatcher/factories perform their existing
				// g_iNextZombieClass/Spawn/stat/panic-count writes themselves.
				spawned = TheZombieManager->SpawnSpecial( zombieClass, position, angles );
				if ( spawned && spawned->GetTeamNumber() == TEAM_INFECTED ) break;
			}
			m_BaseSpecialSpawnTimers[zombieClass - 1].Start( 5.0f );
			candidates[index] = candidates[--candidateCount]; // Native FastRemove.
			DevMsg( "Director failed to spawn a special\n" );
			TheZombieManager->SetSpawnCompassMask( 0 ); // Native manager+108, NOT g_iNextZombieClass.
			if ( candidateCount == 0 ) return;
		}
		spawned->m_nSpecialSpawnSlot = slot;
		m_SpecialSpawnTimers[slot].Start( 999.0f );
		m_nSpecialCount = (int)( (unsigned int)m_nSpecialCount + 1u );
		if ( candidateCount == 0 || m_nSpecialCount >= GetMaxSpecialCount() ) return;
	}
}

#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
