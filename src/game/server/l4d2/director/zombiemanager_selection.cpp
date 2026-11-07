//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/director/zombiemanager.h"
#include "l4d2/director/zombie_spawn.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/director/director_scenario.h"
#include "l4d2/director/director_survival_mode.h"
#include "l4d2/director/director_scavenge_mode.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/nav/terror_nav.h"
#include "l4d2/entities/zombie_border.h"
#include "team.h"
#include "l4d2/infected/common/infected.h"
#include "NextBot/NextBotManager.h"
#include "igameevents.h"
#include <float.h>

#include "l4d2/director/zombiemanager_selection_console_server_cvars.inc"
#include "tier0/memdbgon.h"

extern ConVar z_debug;
extern ConVar z_debug_spawn_set;
ConVar z_spawn_safety_range( "z_spawn_safety_range", "550", 0x00004004 );
ConVar z_finale_spawn_safety_range( "z_finale_spawn_safety_range", "300", 0x00004004 );
ConVar z_finale_spawn_tank_safety_range( "z_finale_spawn_tank_safety_range", "600", 0x00004004 );
ConVar z_finale_spawn_mob_safety_range( "z_finale_spawn_mob_safety_range", "600", 0x00004004 );
ConVar z_large_volume_mob_too_far_xy( "z_large_volume_mob_too_far_xy", "1600", 0x00004004 );
ConVar z_large_volume_mob_too_far_z( "z_large_volume_mob_too_far_z", "128", 0x00004004 );
extern ConVar nav_obscure_range;
static ConVar spawn_behind_survivors_distance( "spawn_behind_survivors_distance", "0", 0x00004004 );

namespace
{
bool IsPopulationPlayer( CTerrorPlayer *player )
{
	return player && player->edict() && player->entindex() != 0 && player->IsPlayer() &&
		static_cast< const CBasePlayer * >( player )->IsConnected();
}

bool IsTankRun()
{
	return TheDirector->GetScriptedMode()->IsEnabled() &&
		TheDirector->GetDirectorOptionInt( "cm_TankRun", 0 ) != 0;
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

CTerrorPlayer *FindClosestSurvivor( const Vector &position, bool excludeRescueVehicle, float *rangeSquared )
{
	CTerrorPlayer *closest = NULL;
	float closestRange = excludeRescueVehicle ? 100000000.0f : 1000000000.0f;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = static_cast< CTerrorPlayer * >( UTIL_PlayerByIndex( i ) );
		if ( !IsPopulationPlayer( player ) || !player->IsAlive() || player->GetTeamNumber() != 2 )
			continue;
		TerrorNavArea *area = ToTerrorNavArea( player->GetLastKnownArea() );
		if ( excludeRescueVehicle && area && area->HasSpawnAttributes( TERROR_NAV_RESCUE_VEHICLE ) )
			continue;
		Vector delta = player->WorldSpaceCenter() - position;
		float squared = ( delta.x * delta.x + delta.y * delta.y ) + delta.z * delta.z;
		if ( closestRange > squared )
		{
			closestRange = squared;
			closest = player;
		}
	}
	if ( rangeSquared )
		*rangeSquared = closestRange;
	return closest;
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
float GetSurvivorFlowThreshold( bool maximum, bool excludeIncapacitated )
{
	float threshold = maximum ? -9999.0f : 1000000000.0f;
	CTerrorPlayer *selected = NULL;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( !IsPopulationPlayer( player ) || !player->IsAlive() || player->GetTeamNumber() != 2 ||
			( excludeIncapacitated && player->IsIncapacitated() ) )
			continue;
		TerrorNavArea *area = ToTerrorNavArea( player->GetLastKnownArea() );
		if ( !area || !( area->GetFlowDistance() > -9999.0f ) )
			continue;
		if ( !selected || ( maximum ? area->GetFlowDistance() > threshold : threshold > area->GetFlowDistance() ) )
		{
			selected = player;
			threshold = area->GetFlowDistance();
		}
	}
	return threshold;
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

class CollectScriptedAreas
{
public:
	explicit CollectScriptedAreas( CUtlVector< TerrorNavArea * > *areas ) : m_areas( areas ) {}
	bool operator()( CNavArea *area )
	{
		m_areas->AddToTail( ToTerrorNavArea( area ) );
		return true;
	}
	CUtlVector< TerrorNavArea * > *m_areas;
};
}

void ZombieManager::CollectMapSpawnAreas()
{
	m_finaleSpawnAreas.RemoveAll();
	m_battlefieldSpawnAreas.RemoveAll();
	m_scriptedSpawnAreas.RemoveAll();
	Vector compassOrigin( 0, 0, 0 );
	Vector compassForward( 1, 0, 0 );
	CBaseEntity *compass = gEntList.FindEntityByName( NULL, "Compass" );
	if ( compass )
	{
		compassOrigin = compass->GetAbsOrigin();
		AngleVectors( compass->GetAbsAngles(), &compassForward );
	}
	float compassAngle = atan2( compassForward.x, compassForward.y );
	FOR_EACH_VEC( TheNavAreas, i )
	{
		TerrorNavArea *area = ToTerrorNavArea( TheNavAreas[i] );
		if ( area->HasSpawnAttributes( TERROR_NAV_FINALE ) && !area->HasSpawnAttributes( 0x18000 ) )
			m_finaleSpawnAreas.AddToTail( area );
		if ( area->HasSpawnAttributes( TERROR_NAV_BATTLEFIELD ) && !area->HasSpawnAttributes( 0x18000 ) )
			m_battlefieldSpawnAreas.AddToTail( area );
		int octant = static_cast< int >( ( atan2( area->GetCenter().x - compassOrigin.x,
			area->GetCenter().y - compassOrigin.y ) + 0.3926990816987241 - compassAngle + 6.283185307179586 ) * 1.273239544735163 ) % 8;
		// Native AddSpawnAttributes ORs, including on a repeated refresh.
		area->AddSpawnAttributes( static_cast< unsigned int >( octant ) << 24 );
	}
}

void ZombieManager::CollectScriptedSpawnAreas( bool clear, const Vector &position, float radius )
{
	if ( clear )
		m_scriptedSpawnAreas.RemoveAll();
	CollectScriptedAreas collect( &m_scriptedSpawnAreas );
	TheNavMesh->ForAllAreasInRadius( collect, position, radius );
}

void ZombieManager::CollectActiveSpawnAreas( SpawnLocation location, int zombieClass,
	float threshold, CUtlVector< TerrorNavArea * > *result, bool draw )
{
	CUtlVector< CTerrorPlayer * > survivors;
	CollectLivingSurvivors( &survivors );
	FOR_EACH_VEC( survivors, i )
		AccumulateSpawnAreaCollection( survivors[i]->GetPotentiallyVisibleAreas(), location,
			zombieClass, threshold, result, draw, false );
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
const CUtlVector< TerrorNavArea * > &ZombieManager::CollectSpawnAreas( SpawnLocation location, int zombieClass )
{
	CUtlVector< TerrorNavArea * > &areas = m_spawnAreaCollections[location];
	if ( gpGlobals->curtime < m_spawnAreaCollectionTimer[location].m_timestamp )
		return areas;
	m_spawnAreaCollectionTimer[location].Start( 2.0f );
	bool draw = z_debug_spawn_set.GetInt() == location;
	float threshold = -1.0f;
	bool tankRun = IsTankRun();
	switch ( location )
	{
	case SPAWN_BEHIND_SURVIVORS:
		threshold = GetSurvivorFlowThreshold( tankRun, tankRun );
		if ( !( (double)threshold - (double)TheDirector->GetDirectorOptionFloat( "SpawnBehindSurvivorsDistance", spawn_behind_survivors_distance.GetFloat() ) > 0.0 ) )
			threshold = 0.0f;
		else
			threshold = (float)( (double)threshold - (double)TheDirector->GetDirectorOptionFloat( "SpawnBehindSurvivorsDistance", spawn_behind_survivors_distance.GetFloat() ) );
		break;
	case SPAWN_NEAR_IT:
	{
		threshold = 1000000000.0f;
		float remaining = 0.0f;
		for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
			if ( IsPopulationPlayer( player ) && player->IsAlive() && player->GetTeamNumber() == 2 &&
				player->IsIT() && !( (double)remaining > (double)player->m_itTimer.m_timestamp - (double)gpGlobals->curtime ) )
			{
				TerrorNavArea *area = ToTerrorNavArea( player->GetLastKnownArea() );
				if ( area && !( area->GetFlowDistance() < 0.0f ) )
				{
					threshold = area->GetFlowDistance();
					remaining = (float)( (double)player->m_itTimer.m_timestamp - (double)gpGlobals->curtime );
				}
			}
		}
		break;
	}
	case SPAWN_SPECIALS_IN_FRONT:
	case SPAWN_IN_FRONT:
		threshold = GetSurvivorFlowThreshold( true, location == SPAWN_SPECIALS_IN_FRONT );
		break;
	case SPAWN_ABOVE_SURVIVORS:
	{
		threshold = -100000000.0f;
		CUtlVector< CTerrorPlayer * > survivors;
		CollectLivingSurvivors( &survivors );
		FOR_EACH_VEC( survivors, i )
		{
			if ( survivors[i]->GetAbsOrigin().z > threshold )
				threshold = survivors[i]->GetAbsOrigin().z;
		}
		break;
	}
	default:
		break;
	}
	areas.RemoveAll();
	TerrorNavArea::BeginAASCollection();
	CDirectorScenario *scenario = TheDirector->GetScenario();
	int finaleType = scenario->GetFinaleType();
	if ( TheDirector->IsFinale() && finaleType != 1 && finaleType != 3 )
	{
		if ( location == SPAWN_LARGE_VOLUME )
		{
			AccumulateSpawnAreaCollection( m_finaleSpawnAreas, SPAWN_LARGE_VOLUME, zombieClass, threshold, &areas, draw, true );
			if ( !areas.Count() )
			{
				TerrorNavArea::BeginAASCollection();
				AccumulateSpawnAreaCollection( m_finaleSpawnAreas, SPAWN_ANYWHERE, zombieClass, threshold, &areas, draw, true );
			}
		}
		else
			AccumulateSpawnAreaCollection( m_finaleSpawnAreas, TerrorGameRules()->IsVersusMode() ? SPAWN_FINALE : SPAWN_ANYWHERE,
				zombieClass, threshold, &areas, draw, true );
	}
	else if ( scenario->IsFirstPanicEvent() && finaleType != 1 )
		AccumulateSpawnAreaCollection( m_battlefieldSpawnAreas, SPAWN_ANYWHERE, zombieClass, threshold, &areas, draw, true );
	else if ( TheDirector->GetSurvivalMode()->HasStarted() )
	{
		int state = TheDirector->GetSurvivalMode()->GetSpawnState();
		if ( state == 0 || state == 1 )
			AccumulateSpawnAreaCollection( state == 0 ? m_finaleSpawnAreas : m_battlefieldSpawnAreas,
				SPAWN_ANYWHERE, zombieClass, threshold, &areas, draw, true );
		if ( !areas.Count() )
		{
			TerrorNavArea::BeginAASCollection();
			CollectActiveSpawnAreas( SPAWN_ANYWHERE, zombieClass, threshold, &areas, draw );
		}
	}
	else if ( TheDirector->GetScavengeMode()->HasStarted() )
	{
		if ( TheDirector->AreAllSurvivorsInFinaleArea() )
			AccumulateSpawnAreaCollection( m_finaleSpawnAreas, location, zombieClass, threshold, &areas, draw, true );
		else if ( scenario->AreSurvivorsInFinaleAreaBeforePanic() )
			AccumulateSpawnAreaCollection( m_battlefieldSpawnAreas, location, zombieClass, threshold, &areas, draw, true );
		if ( !areas.Count() )
		{
			TerrorNavArea::BeginAASCollection();
			CollectActiveSpawnAreas( SPAWN_ANYWHERE, zombieClass, threshold, &areas, draw );
		}
	}
	else if ( TheDirector->GetScriptedMode()->IsActive() )
	{
		switch ( TheDirector->GetScriptedMode()->GetSpawnRule() )
		{
		case 0:
			AccumulateSpawnAreaCollection( m_finaleSpawnAreas, SPAWN_LARGE_VOLUME, zombieClass, threshold, &areas, draw, true );
			if ( !areas.Count() )
			{
				TerrorNavArea::BeginAASCollection();
				AccumulateSpawnAreaCollection( m_finaleSpawnAreas, SPAWN_ANYWHERE, zombieClass, threshold, &areas, draw, true );
			}
			break;
		case 1:
			CollectActiveSpawnAreas( location, zombieClass, threshold, &areas, draw );
			break;
		case 2:
			AccumulateSpawnAreaCollection( m_battlefieldSpawnAreas, SPAWN_ANYWHERE, zombieClass, threshold, &areas, draw, true );
			break;
		case 3:
			AccumulateSpawnAreaCollection( m_scriptedSpawnAreas, SPAWN_ANYWHERE, zombieClass, threshold, &areas, draw, true );
			break;
		default:
			Warning( "Unsupported ScriptedMode Spawn Rule\n" );
			break;
		}
	}
	else
		CollectActiveSpawnAreas( location, zombieClass, threshold, &areas, draw );
	return areas;
}

void ZombieManager::AccumulateSpawnAreaCollection( const CUtlVector< TerrorNavArea * > &input,
	SpawnLocation location, int zombieClass, float threshold,
	CUtlVector< TerrorNavArea * > *result, bool draw, bool checkZombieBorders )
{
	int excludedAreaID = -1;
	if ( TerrorGameRules()->IsVersusMode() && TheDirector->IsFinale() &&
		!Q_strnicmp( STRING( gpGlobals->mapname ), "c4m5", 4 ) )
		excludedAreaID = 486375;
	CUtlVector< CTerrorPlayer * > survivors;
	float largeVolumeRangeSquared = 0.0f;
	float largeVolumeZ = 0.0f;
	if ( location == SPAWN_LARGE_VOLUME )
	{
		if ( TheDirector->GetDirectorOptionInt( "ShouldConstrainLargeVolumeSpawn", 1 ) )
		{
			largeVolumeRangeSquared = z_large_volume_mob_too_far_xy.GetFloat() * z_large_volume_mob_too_far_xy.GetFloat();
			largeVolumeZ = z_large_volume_mob_too_far_z.GetFloat();
		}
		else
			largeVolumeRangeSquared = largeVolumeZ = FLT_MAX;
		CollectLivingSurvivors( &survivors );
	}
	bool tankRun = IsTankRun();
	bool ignoreClearState = true;
	if ( location == SPAWN_SPECIALS_IN_FRONT || location == SPAWN_SPECIALS_ANYWHERE ||
		location == SPAWN_PREFERRED_POSITION || tankRun || TheDirector->IsFinale() ||
		TheDirector->GetSurvivalMode()->HasStarted() || TheDirector->GetScenario()->IsFirstPanicEvent() )
		ignoreClearState = TheDirector->GetDirectorOptionInt( "ShouldIgnoreClearStateForSpawn", 0 ) != 0;
	FOR_EACH_VEC( input, i )
	{
		TerrorNavArea *area = input[i];
		if ( area->IsAASCollectionMarked() )
			continue;
		area->MarkAASCollection();
		unsigned int attributes = area->GetAttributes();
		unsigned int spawn = area->GetSpawnAttributes();
		if ( ( attributes & 3 ) || ( attributes & 0x30000 ) ||
			( ( spawn & TERROR_NAV_PLAYER_START ) && !tankRun ) || ( spawn & 0x18000 ) )
			continue;
		if ( m_nSpawnCompassMask != 0 && m_nSpawnCompassMask != 255 &&
			!( ( 1 << ( ( spawn >> 24 ) & 7 ) ) & m_nSpawnCompassMask ) )
			continue;
		if ( excludedAreaID >= 0 && area->GetID() == excludedAreaID )
			continue;
		if ( ( spawn & TERROR_NAV_NO_MOBS ) && !tankRun )
		{
			if ( TerrorGameRules()->IsPlayerControlledZombies() )
			{
				if ( zombieClass == 0 || zombieClass == 8 )
					continue;
			}
			else if ( location != SPAWN_ABOVE_SURVIVORS && location != SPAWN_SPECIALS_IN_FRONT && location != SPAWN_SPECIALS_ANYWHERE )
				continue;
		}
		if ( ( ( spawn & TERROR_NAV_CHECKPOINT ) &&
			TheTerrorNavMesh()->GetMaxNonCheckpointFlowDistance() > area->GetFlowDistance() && !tankRun ) ||
			area->IsBlocked( 3, false ) || ( checkZombieBorders && CZombieBorder::IsPointBeyondBorder( area->GetCenter() ) ) )
			continue;
		bool enforceFinaleRules = !TheDirector->GetScriptedMode()->IsEnabled() ||
			TheDirector->GetDirectorOptionInt( "EnforceFinaleNavSpawnRules", 1 ) != 0;
		if ( location != SPAWN_NEAR_IT && !TerrorGameRules()->IsScavengeMode() && enforceFinaleRules )
		{
			if ( TheDirector->GetSurvivalMode()->HasStarted() )
			{
				if ( TheDirector->GetSurvivalMode()->GetSpawnState() == 0 && !( spawn & TERROR_NAV_FINALE ) )
					continue;
			}
			else if ( !TheDirector->IsFinale() || TheDirector->GetScenario()->GetFinaleType() == 3 )
			{
				if ( spawn & TERROR_NAV_FINALE )
					continue;
			}
			else if ( !( spawn & TERROR_NAV_FINALE ) )
				continue;
		}
		if ( ( !ignoreClearState && !area->IsAvailableForSpawn() ) ||
			( !tankRun && area->IsCompletelyVisibleToAnyCharacter() && !TheDirector->IsSpawnPositionInFog( area->GetCenter() ) ) )
			continue;
		bool eligible = true;
		switch ( location )
		{
		case SPAWN_BEHIND_SURVIVORS:
			eligible = !( area->GetFlowDistance() > threshold );
			break;
		case SPAWN_NEAR_IT:
			eligible = !( area->GetFlowDistance() > threshold + 2000.0f || threshold - 1000.0f > area->GetFlowDistance() );
			break;
		case SPAWN_SPECIALS_IN_FRONT:
		case SPAWN_IN_FRONT:
			eligible = area->GetFlowDistance() > -9999.0f && !( threshold > area->GetFlowDistance() );
			break;
		case SPAWN_ABOVE_SURVIVORS:
			eligible = !( threshold + 100.0f > area->GetCenter().z );
			break;
		case SPAWN_LARGE_VOLUME:
			eligible = false;
			FOR_EACH_VEC( survivors, j )
			{
				const Vector &position = survivors[j]->GetAbsOrigin();
				const Vector &center = area->GetCenter();
				float x = position.x - center.x, y = position.y - center.y;
				// S35D194 retains the vertical subtraction in x87 through the strict comparison.
				if ( largeVolumeRangeSquared > y * y + x * x &&
					(double)largeVolumeZ > fabs( (double)position.z - (double)center.z ) )
				{
					eligible = true;
					break;
				}
			}
			break;
		case SPAWN_PREFERRED_POSITION:
			eligible = !( ( area->GetCenter() - m_preferredMobPosition ).LengthSqr() > m_flPreferredMobPositionRange * m_flPreferredMobPositionRange );
			break;
		default:
			break;
		}
		if ( eligible )
			result->AddToTail( area );
	}
	if ( location == SPAWN_FAR_AWAY || location == SPAWN_FINALE )
	{
		float minimumSquared = 0.0f;
		float maximumSquared = 0.0f;
		if ( location == SPAWN_FINALE )
		{
			float range = zombieClass == 0 ? z_finale_spawn_mob_safety_range.GetFloat() :
				( zombieClass == 8 ? z_finale_spawn_tank_safety_range.GetFloat() : z_finale_spawn_safety_range.GetFloat() );
			maximumSquared = ( range * 2.0f ) * ( range * 2.0f );
			minimumSquared = range * range;
		}
		for ( int i = result->Count() - 1; i >= 0; --i )
		{
			TerrorNavArea *area = ( *result )[i];
			CTerrorPlayer *closest = FindClosestSurvivor( area->GetCenter(), true, NULL );
			if ( closest )
			{
				Vector delta = closest->GetAbsOrigin() - area->GetCenter();
				float squared = ( delta.y * delta.y + delta.x * delta.x ) + delta.z * delta.z;
				if ( minimumSquared > squared || ( location == SPAWN_FINALE && squared > maximumSquared ) )
					result->FastRemove( i );
			}
		}
	}
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

bool ZombieManager::CanZombieSpawnHere( const Vector &position, TerrorNavArea *area,
	int zombieClass, bool mob, CTerrorPlayer *player )
{
	if ( TheDirector->IsTransitionRestoreActive() || TheDirector->IsTransitionInProgress() )
		return false;
	if ( zombieClass == 8 && g_bZombieSpawnBypass )
		return true;
	if ( zombieClass != 0 && area->HasAttributes( 1 ) )
		return false;
	const CViewVectors *view = g_pGameRules->GetViewVectors();
	unsigned int mask = player ? player->PlayerSolidMask() : 0x0202420B;
	trace_t trace;
	UTIL_TraceHull( position, position, view->m_vHullMin, view->m_vHullMax, mask,
		player, COLLISION_GROUP_PLAYER_MOVEMENT, &trace );
	if ( trace.startsolid || trace.fraction < 1.0f )
		return false;
	if ( !area )
		area = ToTerrorNavArea( TheNavMesh->GetNavArea( position, 120.0f ) );
	bool tankRun = IsTankRun();
	if ( !g_bIgnoreZombieSpawnRules )
	{
		if ( area )
		{
			if ( ( area->HasSpawnAttributes( TERROR_NAV_CHECKPOINT ) && !TheDirector->HasAnySurvivorLeftSafeArea() ) ||
				( area->HasSpawnAttributes( TERROR_NAV_PLAYER_START ) && !tankRun ) || area->IsBlocked( 3, false ) ||
				CZombieBorder::IsPointBeyondBorder( area->GetCenter() ) )
				return false;
		}
		if ( area && area->HasSpawnAttributes( TERROR_NAV_OBSCURED ) )
		{
			float closestRange = 1000000.0f;
			CTerrorPlayer *closest = NULL;
			for ( int i = 1; i <= gpGlobals->maxClients; ++i )
			{
				CTerrorPlayer *survivor = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
				if ( survivor && survivor->edict() && survivor->entindex() != 0 &&
					survivor->IsAlive() && survivor->GetTeamNumber() == 2 )
				{
					float range = ( survivor->WorldSpaceCenter() - position ).Length();
					if ( closestRange > range )
					{
						closestRange = range;
						closest = survivor;
					}
				}
			}
			if ( closest && nav_obscure_range.GetFloat() > closestRange )
				return false;
		}
		else if ( !tankRun &&
			( IsTerrorSpawnPointVisibleToTeam( position + Vector( 0, 0, 56.8f ), 2, 3, 0.0f, area ) ||
			IsTerrorSpawnPointVisibleToTeam( position + Vector( 0, 0, 35.5f ), 2, 3, 0.0f, area ) ||
			IsTerrorSpawnPointVisibleToTeam( position + Vector( 0, 0, 1 ), 2, 3, 0.0f, area ) ) )
		{
			if ( !TheDirector->IsSpawnPositionInFog( position ) )
			{
				if ( area )
					area->MarkAsCleared( NULL );
				return false;
			}
		}
	}
	g_bZombieSpawnIsMob = mob;
	if ( zombieClass == 9 )
		return false;
	g_iNextZombieClass = zombieClass;
	if ( zombieClass != 0 && !g_bZombieSpawnBypass && !g_bIgnoreZombieSpawnRules )
	{
		CTerrorPlayer *closest = FindClosestSurvivor( position, false, NULL );
		if ( !closest )
			return false;
		Vector delta = closest->GetAbsOrigin() - position;
		float safety = z_spawn_safety_range.GetFloat() - fabs( delta.z );
		if ( safety * safety > ( delta.y * delta.y + delta.x * delta.x ) + delta.z * delta.z )
			return false;
	}
	if ( g_iNextZombieClass == 7 && !g_bIgnoreZombieSpawnRules &&
		area->GetFlowDistance() < 0.0f && !TheDirector->IsFinale() )
	{
		g_iNextZombieClass = 0;
		Warning( "Witch converted to a normal Infected!\n" );
	}
	return true;
}

bool ZombieManager::GetRandomPZSpawnPosition( int zombieClass, int tries,
	CTerrorPlayer *player, Vector *position )
{
	if ( !position )
		return false;
	CDirectorScenario *scenario = TheDirector->GetScenario();
	SpawnLocation location = SPAWN_SPECIALS_IN_FRONT;
	if ( ( TheDirector->IsFinale() || TheDirector->GetSurvivalMode()->HasStarted() || scenario->IsFirstPanicEvent() ) &&
		!( zombieClass == 8 && scenario->GetFinaleType() == 1 ) )
		location = SPAWN_SPECIALS_ANYWHERE;
	if ( scenario->GetFinaleType() == 1 && scenario->GetFinaleStage() == 17 )
		location = SPAWN_SPECIALS_ANYWHERE;
	if ( TerrorGameRules()->IsVersusMode() && TheDirector->IsFinale() )
		location = scenario->GetFinaleType() != 1 ? SPAWN_FINALE : SPAWN_SPECIALS_IN_FRONT;
	if ( zombieClass == 1 )
		location = SPAWN_ABOVE_SURVIVORS;
	else if ( zombieClass == 6 && RandomInt( 1, 100 ) <= 10 )
		location = SPAWN_BEHIND_SURVIVORS;
	int preferred = TheDirector->GetDirectorOptionInt( "PreferredSpecialDirection", -1 );
	if ( preferred != -1 )
		location = static_cast< SpawnLocation >( preferred );
	const CUtlVector< TerrorNavArea * > *areas = &CollectSpawnAreas( location, zombieClass );
	if ( !areas->Count() )
		areas = &CollectSpawnAreas( SPAWN_SPECIALS_ANYWHERE, zombieClass );
	bool restrictToFront = false;
	if ( scenario->GetFinaleType() == 1 && zombieClass == 8 && !IsTankRun() )
	{
		tries += 5;
		restrictToFront = true;
	}
	if ( areas->Count() )
	{
		int fallbackAttempt = 2 * tries / 3;
		for ( int i = 0; i < tries; ++i )
		{
			if ( i > fallbackAttempt && !restrictToFront )
			{
				areas = &CollectSpawnAreas( SPAWN_SPECIALS_ANYWHERE, zombieClass );
				if ( !areas->Count() )
					break;
			}
			TerrorNavArea *area = ( *areas )[RandomInt( 0, areas->Count() - 1 )];
			*position = area->GetRandomSpawnPoint();
			if ( CanZombieSpawnHere( *position, area, zombieClass, true, player ) )
				return true;
			if ( !area->HasAttributes( 0x20000000 ) )
			{
				position->z += 18.0f;
				if ( CanZombieSpawnHere( *position, area, zombieClass, true, player ) )
					return true;
			}
		}
	}
	if ( z_debug.GetBool() )
		Msg( "ZombieManager::GetRandomPZSpawnPosition failed! type: %d spawn set count: %d tries: %d location requirement: %d\n",
			zombieClass, areas->Count(), tries, location );
	return false;
}


#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif

//========= Copyright Valve Corporation, All rights reserved. ============//
// Integration fragment: append to zombiemanager_selection.cpp, not a new TU.
// This intentionally reuses that TU's native nearest-survivor query rather
// than exposing or duplicating its anonymous FindClosestSurvivor helper.

// S:35E840..35EADC. The sole caller is CTerrorPlayer's culling update3493C0.
// Despite the name, this method never kills/removes an actor on failure.
bool ZombieManager::RelocateOrCullZombie( CTerrorPlayer *player )
{
	if ( !player->IsAlive() ||
		( player->GetTeamNumber() == TEAM_INFECTED && player->GetZombieClass() == 8 ) ||
		player->GetTeamNumber() != TEAM_INFECTED )
	{
		return false;
	}

	const CUtlVector< TerrorNavArea * > &areas = CollectSpawnAreas(
		SPAWN_SPECIALS_IN_FRONT, player->GetZombieClass() ); // S:35E89F..35E8A4: location3 only.
	if ( areas.Count() == 0 )
		return false;

	TerrorNavArea *area = areas[RandomInt( 0, areas.Count() - 1 )];
	Vector position = area->GetRandomSpawnPoint(); // S:2E3050, including its own +10 Z.
	position.z += 5.0f;
	if ( !CanZombieSpawnHere( position, area, player->GetZombieClass(), false, player ) )
	{
		if ( z_debug.GetBool() )
		{
			// The literal "(mob) " is native even though the passed mob flag is false.
			Msg( "RelocateOrCullZombie: Spot blocked (%.0f, %.0f, %.0f) for a %s%s\n",
				position.x, position.y, position.z, "(mob) ", GetZombieClassName( player->GetZombieClass() ) );
		}
		return false;
	}

	QAngle angles = player->GetAbsAngles();
	// S:2FA3A0/2FA410/304590: living team2, no incap/bot/rescue-vehicle
	// exclusion, strict squared-distance improvement from 1e9, slot order.
	CTerrorPlayer *closest = FindClosestSurvivor( position, false, NULL );
	if ( closest )
	{
		Vector direction = closest->GetAbsOrigin() - position;
		VectorAngles( direction, angles );
	}

	player->Teleport( &position, &angles, &vec3_origin ); // S:35E9F3..35EA0A: three args only.
	player->SetPunchAngle( vec3_angle ); // S:35EA0C..35EA77: native conditional network write.
	TerrorGameRules()->AddRecentlySpawnedPlayer( player ); // S:35EA7C..35EA83 -> 2E0D20, SAME queue892.

	IGameEvent *event = gameeventmanager->CreateEvent( "relocated" );
	if ( event )
	{
		event->SetInt( "userid", player->GetUserID() );
		gameeventmanager->FireEvent( event );
	}
	return true;
}

//========= Copyright Valve Corporation, All rights reserved. ============//
// Integration fragment: append to zombiemanager_selection.cpp, in the SAME
// translation unit as the existing anonymous FindClosestSurvivor query.

// Native server byte8523D1. The existing Director::OnInfectedKilled reader
// shares this exact storage; it is not a general death-suppression switch.
bool g_bSuppressSpecialRespawnInterval = false;

static ConVar z_discard_min_range( "z_discard_min_range", "1000", 0x00004004,
	"Don't relocate PZs closer than this regardless of flow distance" );
static ConVar z_cull_timeout( "z_cull_timeout", "5", 0x00004004,
	"Grace period before player zombie is culled for being too far away" );

// S:305020. Distinct from GetTimeSinceEnemySeen and its12592 interval.
// The real CheckEnemySightings34CCB0 writer owns this12584 interval.
double CTerrorPlayer::GetTimeSinceSeenByEnemy() const
{
	const float timestamp = m_seenByEnemyInterval.m_timestamp;
	if ( timestamp > 0.0f )
		return (double)gpGlobals->curtime - (double)timestamp;
	return 99999.8984375;
}

// S:31D600..31D7B9. Producer called by Director26AB60's player THEN NextBot
// passes, not by the player's PostThink or a new roster/visibility service.
void CTerrorPlayer::UpdateCullingEligibility()
{
	if ( GetTeamNumber() != TEAM_INFECTED || !IsAlive() || IsEFlagSet( EFL_KILLME ) ||
		GetAliveDuration() < 1.0 ||
		( GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 8 ) ||
		( GetFlags() & FL_ONFIRE ) )
	{
		EndCulling();
		return;
	}

	TerrorNavArea *area = ToTerrorNavArea( GetLastKnownArea() );
	if ( area && area->IsPotentiallyVisibleToAnyCharacter() )
	{
		EndCulling();
		return;
	}

	float closestRangeSquared;
	CTerrorPlayer *closest = FindClosestSurvivor( GetAbsOrigin(), false, &closestRangeSquared );
	if ( !closest )
	{
		EndCulling();
		return;
	}

	TerrorNavArea *closestArea = ToTerrorNavArea( closest->GetLastKnownArea() );
	TerrorNavArea *currentArea = ToTerrorNavArea( GetLastKnownArea() );
	const double discardRange = TheDirector->GetZombieDiscardRange();
	const float roundedDiscardRange = (float)discardRange; // Native store31D6E3 before x87 square.
	bool shouldCull = (double)closestRangeSquared > discardRange * discardRange;
	if ( !shouldCull && !( GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 8 ) &&
		GetTimeSinceSeenByEnemy() > 5.0 )
	{
		const float minimumRange = z_discard_min_range.GetFloat();
		const float minimumRangeSquared = minimumRange * minimumRange;
		if ( closestRangeSquared > minimumRangeSquared && GetFlowDistance( 0 ) >= 0.0 )
		{
			// S:31D745 repeats the native IsPlayer cast even though the selector
			// already admitted players. Do not remove this virtual query.
			CTerrorPlayer *closestPlayer = ToTerrorPlayer( closest );
			if ( closestPlayer )
			{
				const float flow = (float)GetFlowDistance( 0 );
				if ( fabs( (double)flow - closestPlayer->GetFlowDistance( 0 ) ) > (double)roundedDiscardRange )
					shouldCull = true;
			}
		}
	}

	if ( ( closestArea && currentArea && currentArea->GetFlowDistance() > closestArea->GetFlowDistance() ) ||
		!shouldCull )
	{
		EndCulling();
	}
	else
	{
		BeginCulling();
	}
}

// S:314510..31466A. Keep the second Tank test: although the first guard
// normally makes its timeout branch unreachable, the native re-queries it.
void CTerrorPlayer::BeginCulling()
{
	if ( ( GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 8 ) || m_isCulling )
		return;

	m_isCulling = true;
	m_isRelocating = false;
	if ( GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 8 )
	{
		m_cullingTimer.Start( z_cull_timeout.GetFloat() );
	}
	else if ( IsBot() )
	{
		m_isRelocating = true;
		m_cullingTimer.Start( 0.5f );
	}
	else
	{
		m_cullingTimer.Invalidate();
	}
}

// S:314670..3146E3. Neither the deadline nor duration is reset here.
void CTerrorPlayer::EndCulling()
{
	m_isCulling = false;
	m_isRelocating = false;
}

// S:3493C0..349460. Called ONLY at PostThink338B94, inside its scheduled,
// State_Get()==STATE_ACTIVE, IsAlive branch after the actual life update34A290.
void CTerrorPlayer::UpdateCulling()
{
	if ( !m_isCulling || !( m_cullingTimer.m_timestamp > 0.0f ) ||
		!( (double)gpGlobals->curtime >= (double)m_cullingTimer.m_timestamp ) )
	{
		return;
	}

	if ( IsBot() && !TerrorGameRules()->IsPlayerControlledZombies() )
	{
		g_bSuppressSpecialRespawnInterval = true;
		CommitSuicide( false ); // Actual virtual2FACE0; not a ghost-spawn callback.
		g_bSuppressSpecialRespawnInterval = false;
	}

	if ( TheZombieManager->RelocateOrCullZombie( this ) )
		EndCulling();
	else
		m_cullingTimer.Start( 0.5f );
}

//========= Copyright Valve Corporation, All rights reserved. ============//
// Integration fragment: append to zombiemanager_selection.cpp after the actor
// fragment. It reuses the ONE existing anonymous nearest-survivor query.

static ConVar z_no_cull( "z_no_cull", "0", 0x00004004,
	"If nonzero, the Director will not cull common infected" );

// S:3FE270/3FE2B0. Independent ordinary server IntervalTimers7344/7352;
// their real producer is the beginning of Infected::Update405910 below.
double Infected::GetTimeSincePotentiallyVisible() const
{
	const float timestamp = m_potentiallyVisibleTimer.m_timestamp;
	return timestamp > 0.0f ? (double)gpGlobals->curtime - (double)timestamp : 99999.8984375;
}

double Infected::GetTimeSinceInPlayerAAS() const
{
	const float timestamp = m_inPlayerAASTimer.m_timestamp;
	return timestamp > 0.0f ? (double)gpGlobals->curtime - (double)timestamp : 99999.8984375;
}

// S:405910 at4059AC..405A02, then full400380. The two NAV queries and
// independent PVS-byte125/AAS-mask340 tests are intentional.
void Infected::UpdateCullingVisibility()
{
	TerrorNavArea *area = ToTerrorNavArea( GetLastKnownArea() );
	if ( area && area->IsPotentiallyVisibleToAnyCharacter() )
		m_potentiallyVisibleTimer.Start();

	m_bIsInPlayerAAS = false;
	area = ToTerrorNavArea( GetLastKnownArea() );
	if ( area && area->IsInAnyPlayerAAS() )
	{
		m_inPlayerAASTimer.Start();
		m_bIsInPlayerAAS = true;
	}
}

// S:4001B0..400372, native Infected primary vtable+1396. Culling is actual
// deferred UTIL_Remove, not death/Retire or a ghost/player replacement.
bool Infected::Cull()
{
	if ( z_no_cull.GetBool() || IsEFlagSet( EFL_KILLME ) )
		return false;

	TerrorNavArea *area = ToTerrorNavArea( GetLastKnownArea() );
	if ( !area || TheDirector->IsBackgroundMap() || ( GetInfectedFlags() & 0x80 ) )
		return false;

	bool checkVisibilityAndRange;
	if ( GetBodyInterface()->IsArousal( IBody::INTENSE ) )
	{
		// Native FCOMIP/JBE admits unordered age in this INTENSE branch.
		if ( 30.0 > GetAliveDuration() )
			return false;
		checkVisibilityAndRange = true;
	}
	else
	{
		if ( GetInfectedFlags() & 1 )
			return false;
		checkVisibilityAndRange = TheDirector->ShouldSpawnWanderers() ||
			IsTerrorSpawnEntityVisibleToTeam( this, TEAM_SURVIVOR, 0, 0.0f, NULL );
		if ( checkVisibilityAndRange && 2.0 > GetAliveDuration() )
			return false;
	}

	if ( checkVisibilityAndRange )
	{
		if ( area->IsPotentiallyVisibleToAnyCharacter() )
			return false;

		float closestRangeSquared;
		if ( !FindClosestSurvivor( GetAbsOrigin(), false, &closestRangeSquared ) )
			return false;
		const double discardRange = TheDirector->GetZombieDiscardRange();
		if ( discardRange * discardRange > (double)closestRangeSquared &&
			( 3.0 > GetTimeSincePotentiallyVisible() || 1.0 > GetTimeSinceInPlayerAAS() ) )
		{
			return false;
		}

		// Original repeats GetLastKnownArea here rather than passing area.
		if ( IsTerrorSpawnEntityVisibleToTeam( this, TEAM_SURVIVOR, 0, 0.0f,
			ToTerrorNavArea( GetLastKnownArea() ) ) )
		{
			return false;
		}
		// Native INC wraps before the signed-negative clamp. Do not invoke
		// signed-overflow UB or replace this with saturating INT_MAX logic.
		int population = (int)( (unsigned int)area->GetWanderingPopulation() + 1u );
		area->SetWanderingPopulation( population < 0 ? 0 : population );
	}

	// When wanderers are disallowed AND the first actual visibility query
	// fails, native jumps here without age/PVS/closest/range/population work.
	UTIL_Remove( this );
	return true;
}

namespace
{
// S:25C150 and NextBot iterator263B10. RTTI dispatch precedes BOTH original
// IsPlayer calls. No IsAlive/team/connection filtering belongs in this functor.
class CullInfectedActors
{
public:
	bool operator()( CBaseCombatCharacter *actor ) const
	{
		Infected *infected = dynamic_cast< Infected * >( actor );
		if ( infected )
			infected->Cull();
		else if ( actor->IsPlayer() && actor->IsPlayer() )
			static_cast< CTerrorPlayer * >( actor )->UpdateCullingEligibility();
		return true;
	}
};
}

// S:26AB60 ->267690. The main Director frame calls virtual108 at274A2B,
// AFTER branch_zoey preference handling and immediately before leaving Update.
void CDirector::UpdateInfectedCulling()
{
	if ( GetScenario()->GetFinaleType() != 5 && !GetModeDescription()->IsBaseMode( "versus" ) )
		return;

	CullInfectedActors cull;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CBasePlayer *player = UTIL_PlayerByIndex( i );
		if ( player && player->edict() && player->entindex() != 0 && player->IsPlayer() &&
			static_cast< const CBasePlayer * >( player )->IsConnected() && !player->MyNPCPointer() )
		{
			cull( player );
		}
	}
	// Always visit the actual registry, including when maxClients is zero.
	// A NextBotPlayer admitted above is deliberately visited a second time.
	TheNextBots().ForEachCombatCharacter( cull );
}

static ConVar director_num_reserved_wanderers( "director_num_reserved_wanderers", "0", 0x00004004 );

int CDirector::GetNumReservedWanderers() const
{
	return GetDirectorOptionInt( "NumReservedWanderers", director_num_reserved_wanderers.GetInt() );
}

// S3FDF40. The real actor bit and Director924 are the only owners. In
// particular, direct removal/destruction does not repair this native count.
void Infected::SetActive( bool active )
{
	if ( ( ( m_nInfectedFlags & 1 ) != 0 ) == active )
		return;
	if ( active )
	{
		m_nInfectedFlags |= 1;
		++TheDirector->m_nActiveCommonCount;
	}
	else
	{
		m_nInfectedFlags &= ~1;
		--TheDirector->m_nActiveCommonCount;
	}
}

namespace
{
// S290150/28F430/28FE50/290040. Connected indexed players run first,
// then the actual NextBot registration list. Equal distances retain the
// earlier result; neither team nor survivor membership is a condition.
class FindClosestITVictim
{
public:
	explicit FindClosestITVictim( CBaseCombatCharacter *me ) :
		m_position( me->WorldSpaceCenter() ), m_ignore( me ),
		m_result( NULL ), m_rangeSquared( 100000000.0f ) {}
	bool operator()( CBasePlayer *player )
	{
		if ( !player->MyNPCPointer() )
			Consider( player );
		return true;
	}
	bool operator()( INextBot *bot )
	{
		Consider( bot->GetEntity() );
		return true;
	}
	CBaseCombatCharacter *GetResult() const { return m_result; }
private:
	void Consider( CBaseCombatCharacter *entity )
	{
		if ( entity == m_ignore || !entity->IsAlive() || !entity->IsIT() )
			return;
		const Vector delta = entity->WorldSpaceCenter() - m_position;
		const float rangeSquared = ( delta.x * delta.x + delta.y * delta.y ) + delta.z * delta.z;
		if ( m_rangeSquared > rangeSquared )
		{
			m_rangeSquared = rangeSquared;
			m_result = entity;
		}
	}
	Vector m_position;
	CBaseCombatCharacter *m_ignore;
	CBaseCombatCharacter *m_result;
	float m_rangeSquared;
};

// S28F340 uses the actual team's player order, not indexed-player order.
// It draws once even when there is exactly one living team member.
CTerrorPlayer *SelectRandomLivingMobSurvivor()
{
	CTeam *team = GetGlobalTeam( TEAM_SURVIVOR );
	int count = 0;
	for ( int i = 0; i < team->GetNumPlayers(); ++i )
	{
		if ( team->GetPlayer( i )->IsAlive() )
			++count;
	}
	if ( count == 0 )
		return NULL;
	const int selected = RandomInt( 0, count - 1 );
	count = 0;
	for ( int i = 0; i < team->GetNumPlayers(); ++i )
	{
		if ( team->GetPlayer( i )->IsAlive() )
		{
			if ( selected == count )
				return ToTerrorPlayer( team->GetPlayer( i ) );
			++count;
		}
	}
	return NULL;
}

// S3FF340 invokes the ordinary maximum-flow functor only for fake
// survivor players. Commentary does not nominate the human listener.
class FindCommentaryMobSurvivor
{
public:
	FindCommentaryMobSurvivor() : m_result( NULL ), m_flow( -9999.0f ) {}
	bool operator()( CBasePlayer *base )
	{
		CTerrorPlayer *player = static_cast< CTerrorPlayer * >( base );
		if ( !player->IsAlive() || player->GetTeamNumber() != TEAM_SURVIVOR || !player->IsFakeClient() )
			return true;
		TerrorNavArea *area = ToTerrorNavArea( player->GetLastKnownArea() );
		if ( area && area->GetFlowDistance() > -9999.0f &&
			( !m_result || area->GetFlowDistance() > m_flow ) )
		{
			m_result = player;
			m_flow = area->GetFlowDistance();
		}
		return true;
	}
	CTerrorPlayer *GetResult() const { return m_result; }
private:
	CTerrorPlayer *m_result;
	float m_flow;
};
}

CBaseCombatCharacter *FindClosestCharacterWhoIsIT( CBaseCombatCharacter *entity )
{
	FindClosestITVictim find( entity );
	ForEachPlayer( find );
	TheNextBots().ForEachBot( find );
	return find.GetResult();
}
// S405910 after native animation publication (3FFE80). The independent
// Cull() virtual at1396 is not called here; this is its reached1392 gate
// and the original periodic excess-population removal branch.
void Infected::UpdatePopulation()
{
	if ( !TheDirector->IsBackgroundMap() )
	{
		if ( m_nInfectedFlags & 1 )
		{
			TerrorNavArea *area = ToTerrorNavArea( GetLastKnownArea() );
			if ( !area || GetLowestSurvivorFlow() > area->GetFlowDistance() )
				SetActive( false );
		}
		if ( !z_no_cull.GetBool() && IsCullable( 0 ) &&
			(double)gpGlobals->curtime >= (double)m_populationCullTimer.m_timestamp &&
			GetAliveDuration() > 3.0 && !( m_nInfectedFlags & 1 ) )
		{
			m_populationCullTimer.Start( 3.0f );
			const int limit = TheDirector->GetCommonLimit();
			if ( GetCommonInfectedCount() > limit || TheZombieManager->IsMobSpawnNeeded() )
			{
				if ( !GetBodyInterface()->IsArousal( IBody::INTENSE ) )
				{
					TerrorNavArea *area = ToTerrorNavArea( GetLastKnownArea() );
					if ( ( !area || !area->IsPotentiallyVisibleToAnyCharacter() ) &&
						!IsTerrorSpawnEntityVisibleToTeam( this, TEAM_SURVIVOR, 0, 0.0f, NULL ) )
					{
						if ( area )
						{
							const int population = (int)( (unsigned int)area->GetWanderingPopulation() + 1u );
							area->SetWanderingPopulation( population < 0 ? 0 : population );
						}
						UTIL_Remove( this );
						return;
					}
				}
			}
		}
	}

	if ( m_mobRush && !GetBodyInterface()->IsArousal( IBody::INTENSE ) )
	{
		CBaseEntity *victim = FindClosestCharacterWhoIsIT( this );
		if ( !victim )
		{
			if ( TheDirector->IsFinale() || TheDirector->GetSurvivalMode()->HasStarted() )
				victim = SelectRandomLivingMobSurvivor();
			else if ( !IsInCommentaryMode() )
				victim = FindHighestFlowSurvivor( 0 );
			else
			{
				FindCommentaryMobSurvivor findBot;
				ForEachPlayer( findBot );
				victim = findBot.GetResult();
				if ( !victim )
					victim = SelectRandomLivingMobSurvivor();
			}
		}
		if ( victim )
			OnCommandAttack( victim );
	}

	if ( m_bAmbientMob )
	{
		CBaseEntity *start = gEntList.FindEntityByClassnameNearest( "info_ambient_mob_start", GetAbsOrigin(), 16384.0f );
		if ( start )
		{
			CBaseEntity *destination = NULL;
			while ( ( destination = gEntList.FindEntityByClassname( destination, "info_ambient_mob_end" ) ) != NULL )
			{
				if ( destination->GetEntityName() == start->GetEntityName() )
					break;
			}
			if ( !destination )
				Msg( "ERROR - Could not find any ambient mob destination ents!\n" );
			m_bAmbientMob = false;
			// Native dispatches NULL too after the diagnostic; do not turn
			// malformed map data into an invented retry or random destination.
			OnCommandApproach( destination );
		}
	}
}

#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
