//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/director/zombiemanager.h"
#include "l4d2/director/zombie_spawn.h"
#include "l4d2/infected/common/infected.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/director/director_survival_mode.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/nav/terror_nav.h"
#include "l4d2/sound/terror_music.h"
#include "l4d2/stats/l4d_gamestats.h"
#include "team.h"
#include "tier1/utlvector.h"
#include <float.h>

#include "tier0/memdbgon.h"

extern ConVar z_debug;
extern ConVar z_debug_mob_spawn;
extern ConVar z_spawn_mobs_from_selected_set;
extern ConVar z_mob_population_density;
extern ConVar z_spawn_mobs_behind_chance;
extern ConVar z_mob_min_notify_count;
extern ConVar z_debug_spawnable_areas;
extern ConVar nav_edit;

namespace
{
bool IsPopulationPlayer( CTerrorPlayer *player )
{
	return player && player->edict() && player->entindex() != 0 && player->IsPlayer() &&
		player->IsConnected();
}

void CollectLivingSurvivors( CUtlVector< CTerrorPlayer * > *survivors )
{
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( IsPopulationPlayer( player ) && player->GetTeamNumber() == 2 && player->IsAlive() )
			survivors->AddToTail( player );
	}
}

// Native 28FF20/28FFB0 with both native flags true: include incap/pins,
// but exclude survivors in RESCUE_VEHICLE. Compare WorldSpaceCenter.
CTerrorPlayer *FindClosestSurvivor( const Vector &position, CTerrorPlayer *ignore )
{
	CTerrorPlayer *closest = NULL;
	float closestRange = 100000000.0f;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( !IsPopulationPlayer( player ) || player == ignore || !player->IsAlive() || player->GetTeamNumber() != 2 )
			continue;
		TerrorNavArea *area = ToTerrorNavArea( player->GetLastKnownArea() );
		if ( area && area->HasSpawnAttributes( TERROR_NAV_RESCUE_VEHICLE ) )
			continue;
		Vector delta = player->WorldSpaceCenter() - position;
		float range = ( delta.x * delta.x + delta.y * delta.y ) + delta.z * delta.z;
		if ( closestRange > range )
		{
			closestRange = range;
			closest = player;
		}
	}
	return closest;
}

CTerrorPlayer *ChooseRandomLivingSurvivor()
{
	CTeam *team = GetGlobalTeam( 2 );
	int alive = 0;
	for ( int i = 0; i < team->GetNumPlayers(); ++i )
	{
		if ( team->GetPlayer( i )->IsAlive() )
			++alive;
	}
	if ( !alive )
		return NULL;
	int chosen = RandomInt( 0, alive - 1 );
	int current = 0;
	for ( int i = 0; i < team->GetNumPlayers(); ++i )
	{
		if ( team->GetPlayer( i )->IsAlive() && current++ == chosen )
			return ToTerrorPlayer( team->GetPlayer( i ) );
	}
	return NULL;
}
}

void ZombieManager::SpawnMob( int count )
{
	DevMsg( "%3.2f: (MOB) SpawnMob requested of size %d.\n", gpGlobals->curtime, count );
	m_nPendingMobCount += count;
	int maxPending = TheDirector->GetDirectorOptionInt( "MobMaxPending", 0 );
	if ( maxPending > 0 )
		m_nPendingMobCount = MIN( maxPending, m_nPendingMobCount );
	DevMsg( "%3.2f: (MOB) SpawnMob pending count is now %d.\n", gpGlobals->curtime, m_nPendingMobCount );
	if ( m_nPendingMobCount >= z_mob_min_notify_count.GetInt() )
	{
		// Original 304930 only queries IsPlayer on connected slots. It does
		// not emit a global horde event or tell the sound GameData system.
		for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			CBasePlayer *player = UTIL_PlayerByIndex( i );
			if ( player && player->edict() && player->entindex() != 0 )
				player->IsPlayer();
		}
	}
	m_mobSpawnLocation = static_cast< SpawnLocation >( TheDirector->GetDirectorOptionInt( "PreferredMobDirection", -1 ) );
	if ( m_mobSpawnLocation == SPAWN_PREFERRED_POSITION )
	{
		TheDirector->GetDirectorOptionVector( "PreferredMobPosition", &m_preferredMobPosition );
		m_flPreferredMobPositionRange = TheDirector->GetDirectorOptionFloat( "PreferredMobPositionRange", m_flPreferredMobPositionRange );
	}
	if ( m_mobSpawnLocation == -1 )
	{
		if ( TheDirector->GetScenario()->GetFinaleType() != 1 || TheDirector->GetScenario()->GetFinaleStage() == 17 )
		{
			float chance = z_spawn_mobs_behind_chance.GetFloat();
			m_mobSpawnLocation = static_cast< SpawnLocation >( chance > RandomFloat( 0.0f, 100.0f ) );
		}
		else
			m_mobSpawnLocation = SPAWN_IN_FRONT;
	}
	m_mobSpawnAreas.RemoveAll();
	m_recentMobSpawnAreas.RemoveAll();
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( IsPopulationPlayer( player ) && ( TerrorGameRules()->IsPlayerControlledZombies() || player->GetTeamNumber() == 2 ) )
			player->GetMusic()->OnMobSpawn( m_nPendingMobCount, m_mobSpawnLocation );
	}
	L4DGameStats().Event_MobSpawned();
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
// S35DCE0 mixes stored SSE distances with unspilled x87 height comparisons.
void ZombieManager::ChooseMobSpawnAreas()
{
	const CUtlVector< TerrorNavArea * > *candidates = NULL;
	const NavAreaVector *selected = NULL;
	if ( z_spawn_mobs_from_selected_set.GetBool() )
		selected = &TheNavMesh->GetSelectedSet();
	else
		candidates = &CollectSpawnAreas( m_mobSpawnLocation, 0 );
	m_recentMobSpawnAreas.RemoveAll();
	int candidateCount = selected ? selected->Count() : candidates->Count();
	if ( !candidateCount )
	{
		// This is the original direction-state change, not a queue repair or
		// invented candidate fallback. No actors are spawned on this call.
		if ( m_mobSpawnLocation != SPAWN_ANYWHERE )
		{
			if ( ( TheDirector->IsFinale() || !TheDirector->AreAllSurvivorsInFinaleArea() ) && !TheDirector->GetSurvivalMode()->HasStarted() )
				DevMsg( "(MOB) %3.2f: MOB SPAWN ERROR: Empty spawn area set, switching to ANYWHERE\n", gpGlobals->curtime );
			m_mobSpawnLocation = SPAWN_ANYWHERE;
		}
		else if ( ( TheDirector->IsFinale() || !TheDirector->AreAllSurvivorsInFinaleArea() ) && !TheDirector->GetSurvivalMode()->HasStarted() )
			DevMsg( "(MOB) %3.2f: MOB SPAWN ERROR: ANYWHERE = Empty spawn area set\n", gpGlobals->curtime );
		m_mobSpawnAreas.RemoveAll();
		return;
	}
	CUtlVectorFixedGrowable< float, 128 > weights;
	weights.SetCount( candidateCount );
	float totalWeight = 0.0f;
	CUtlVector< CTerrorPlayer * > survivors;
	if ( m_mobSpawnLocation == SPAWN_LARGE_VOLUME )
		CollectLivingSurvivors( &survivors );
	if ( m_mobSpawnLocation == SPAWN_LARGE_VOLUME && survivors.Count() )
	{
		CUtlVector< CTerrorPlayer * > live;
		CollectLivingSurvivors( &live );
		CUtlVectorFixedGrowable< CTerrorPlayer *, 4 > isolated;
		FOR_EACH_VEC( live, i )
		{
			CTerrorPlayer *closest = FindClosestSurvivor( live[i]->WorldSpaceCenter(), live[i] );
			if ( !closest )
				isolated.AddToTail( live[i] );
			else
			{
				const Vector &closestPosition = closest->GetAbsOrigin();
				const Vector &playerPosition = live[i]->GetAbsOrigin();
				Vector delta = closestPosition - playerPosition;
				if ( delta.x * delta.x + delta.y * delta.y > 1000000.0f ||
					fabs( (double)closestPosition.z - (double)playerPosition.z ) > 120.0 )
					isolated.AddToTail( live[i] );
			}
		}
		for ( int i = 0; i < candidateCount; ++i )
		{
			TerrorNavArea *area = selected ? ToTerrorNavArea( ( *selected )[i] ) : ( *candidates )[i];
			float nearest = FLT_MAX;
			float highest = FLT_MIN;
			CTerrorPlayer *nearestPlayer = NULL;
			FOR_EACH_VEC( live, j )
			{
				if ( live[j]->GetAbsOrigin().z > highest )
					highest = live[j]->GetAbsOrigin().z;
				const Vector delta = area->GetCenter() - live[j]->GetAbsOrigin();
				float range = sqrtf( ( delta.y * delta.y + delta.z * delta.z ) + delta.x * delta.x );
				if ( nearest > range )
				{
					nearest = range;
					nearestPlayer = live[j];
				}
			}
			float clampedRange = nearest > 1600.0f ? 1600.0f : ( nearest >= 0.1f ? nearest : 0.1f );
			float scale = 5.0f - ( clampedRange * 4.0f ) * 0.000625f;
			if ( isolated.Find( nearestPlayer ) != isolated.InvalidIndex() )
				scale *= 5.0f;
			if ( fabs( (double)area->GetCenter().z - (double)highest ) < 120.0 )
				scale *= 5.0f;
			weights[i] = scale * ( area->GetSizeY() * area->GetSizeX() );
			totalWeight += weights[i];
		}
	}
	else
	{
		for ( int i = 0; i < candidateCount; ++i )
		{
			TerrorNavArea *area = selected ? ToTerrorNavArea( ( *selected )[i] ) : ( *candidates )[i];
			weights[i] = area->GetSizeY() * area->GetSizeX();
			totalWeight += weights[i];
		}
	}
	m_mobSpawnAreas.RemoveAll();
	int attempts = 2 * ( m_mobSpawnLocation == SPAWN_LARGE_VOLUME ) + 2;
	for ( int attempt = 0; attempt < attempts; ++attempt )
	{
		float chosenWeight = RandomFloat( 0.0f, totalWeight );
		float accumulated = 0.0f;
		for ( int i = 0; i < candidateCount; ++i )
		{
			TerrorNavArea *area = selected ? ToTerrorNavArea( ( *selected )[i] ) : ( *candidates )[i];
			bool alreadySelected = false;
			FOR_EACH_VEC( m_mobSpawnAreas, j )
			{
				if ( m_mobSpawnAreas[j].m_area == area )
				{
					alreadySelected = true;
					break;
				}
			}
			if ( alreadySelected )
				continue;
			accumulated += weights[i];
			if ( accumulated >= chosenWeight )
			{
				MobSpawnArea spawn;
				spawn.m_area = area;
				spawn.m_population = static_cast< int >( ( area->GetSizeY() * area->GetSizeX() ) * z_mob_population_density.GetFloat() );
				m_mobSpawnAreas.AddToTail( spawn );
				m_recentMobSpawnAreas.AddToTail( area );
				totalWeight -= weights[i];
				break;
			}
		}
	}
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

TerrorNavArea *ZombieManager::ChooseAdjacentMobSpawnArea( TerrorNavArea *area )
{
	const CUtlVector< TerrorNavArea * > *candidates = NULL;
	const NavAreaVector *selected = NULL;
	if ( z_spawn_mobs_from_selected_set.GetBool() )
		selected = &TheNavMesh->GetSelectedSet();
	else
		candidates = &CollectSpawnAreas( m_mobSpawnLocation, 0 );
	TerrorNavArea *closest = NULL;
	float rangeSquared = FLT_MAX;
	int candidateCount = selected ? selected->Count() : candidates->Count();
	for ( int i = 0; i < candidateCount; ++i )
	{
		TerrorNavArea *candidate = selected ? ToTerrorNavArea( ( *selected )[i] ) : ( *candidates )[i];
		if ( candidate == area || m_recentMobSpawnAreas.Find( candidate ) != m_recentMobSpawnAreas.InvalidIndex() )
			continue;
		Vector delta = candidate->GetCenter() - area->GetCenter();
		float range = ( delta.y * delta.y + delta.x * delta.x ) + delta.z * delta.z;
		if ( rangeSquared > range )
		{
			rangeSquared = range;
			closest = candidate;
		}
	}
	if ( closest )
	{
		if ( m_recentMobSpawnAreas.Count() >= 10 )
			m_recentMobSpawnAreas.Remove( 0 );
		m_recentMobSpawnAreas.AddToTail( closest );
	}
	return closest;
}

void ZombieManager::StartFrame()
{
	if ( m_bSpawnAreasChanged )
	{
		CollectMapSpawnAreas();
		m_bSpawnAreasChanged = false;
	}
	if ( m_delayedMegaMobTimer.m_timestamp > 0.0f && gpGlobals->curtime >= m_delayedMegaMobTimer.m_timestamp )
	{
		SpawnMob( TheDirector->GetMegaMobSize() );
		m_delayedMegaMobTimer.Invalidate();
	}
	int limit = TheDirector->GetCommonLimit();
	if ( m_nPendingMobCount < 1 && m_nAmbientMobCount > 0 && GetCommonInfectedCount() < limit )
	{
		if ( !m_ambientMobArea )
		{
			CTerrorPlayer *player = ChooseRandomLivingSurvivor();
			CBaseEntity *start = player ? gEntList.FindEntityByClassnameNearest( "info_ambient_mob_start", player->GetAbsOrigin(), 16384.0f ) : NULL;
			m_ambientMobArea = start ? ToTerrorNavArea( TheNavMesh->GetNearestNavArea( start, 4, 10000.0f ) ) : NULL;
			if ( m_ambientMobArea && !m_ambientMobArea->IsPotentiallyVisibleToAnyCharacter() )
			{
				for ( int i = 1; i <= gpGlobals->maxClients; ++i )
				{
					CTerrorPlayer *survivor = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
					if ( IsPopulationPlayer( survivor ) && survivor->GetTeamNumber() == 2 )
						survivor->GetMusic()->OnAmbientMob();
				}
			}
			else
			{
				m_nAmbientMobCount = 0;
				m_ambientMobArea = NULL;
			}
		}
		if ( m_ambientMobArea && SpawnCommonZombie( m_ambientMobArea,
			m_ambientMobArea->GetRandomSpawnPoint(), AMBIENT_MOB ) && --m_nAmbientMobCount < 1 )
			m_ambientMobArea = NULL;
	}
	if ( m_nPendingMobCount > 0 && GetCommonInfectedCount() < limit )
	{
		if ( gpGlobals->curtime >= m_mobSpawnAreaTimer.m_timestamp || !m_mobSpawnAreas.Count() )
		{
			m_mobSpawnAreaTimer.Start( RandomFloat( 5.0f, m_mobSpawnLocation == SPAWN_LARGE_VOLUME ? 7.5f : 15.0f ) );
			ChooseMobSpawnAreas();
		}
		if ( m_mobSpawnAreas.Count() )
		{
			int attempts = MIN( MAX( 2 * m_mobSpawnAreas.Count(), 4 ), m_nPendingMobCount );
			for ( int i = 0; i < attempts; ++i )
			{
				int slot = i % m_mobSpawnAreas.Count();
				TerrorNavArea *area = m_mobSpawnAreas[slot].m_area;
				if ( !area )
					break;
				Vector position = area->GetRandomSpawnPoint();
				Infected *infected = NULL;
				if ( !IsTerrorSpawnPointVisibleToTeam( position, 2, 0, 0.0f, area ) &&
					!IsTerrorSpawnPointVisibleToTeam( position + Vector( 0, 0, 68 ), 2, 0, 0.0f, area ) )
					infected = SpawnCommonZombie( area, position, MOB );
				if ( !infected )
					m_mobSpawnAreaTimer.Start( ( m_mobSpawnAreaTimer.m_timestamp - gpGlobals->curtime ) - 5.0f );
				else
				{
					--m_nPendingMobCount;
					infected->SetInfectedFlags( 0x100 );
					if ( --m_mobSpawnAreas[slot].m_population <= 0 )
					{
						m_mobSpawnAreas[slot].m_area = ChooseAdjacentMobSpawnArea( area );
						TerrorNavArea *next = m_mobSpawnAreas[slot].m_area;
						if ( next )
							m_mobSpawnAreas[slot].m_population = static_cast< int >( ( next->GetSizeY() * next->GetSizeX() ) * z_mob_population_density.GetFloat() );
					}
					DevMsg( "%3.2f: (MOB) Mob zombie spawned, %d remain.\n", gpGlobals->curtime, m_nPendingMobCount );
				}
			}
		}
		if ( m_nPendingMobCount == 0 )
			DevMsg( "%3.2f: (MOB) Pending mob done.\n", gpGlobals->curtime );
	}
	if ( z_debug_spawnable_areas.GetBool() )
		DrawSpawnableAreas();
	TheTerrorNavMesh()->MaintainWanderingPopulation( false );
}

void ZombieManager::DrawSpawnableAreas()
{
	if ( !TheNavMesh->IsGenerating() && !nav_edit.GetBool() )
	{
		const CUtlVector< TerrorNavArea * > &areas = CollectSpawnAreas( SPAWN_ANYWHERE, 9 );
		FOR_EACH_VEC( areas, i )
		{
			if ( areas[i]->HasSpawnAttributes( TERROR_NAV_OBSCURED ) )
				areas[i]->DrawFilled( 0, 0, 128, 64, 0.01023f, true );
			else
				areas[i]->DrawFilled( 0, 128, 0, 64, 0.01023f, true );
		}
	}
}
