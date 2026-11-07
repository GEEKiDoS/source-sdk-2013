//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/director/zombiemanager.h"
#include "l4d2/director/zombie_spawn.h"
#include "l4d2/infected/common/infected.h"
#include "l4d2/infected/boss/witch.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/director/director_scavenge_mode.h"
#include "l4d2/nav/terror_nav.h"
#include "l4d2/stats/l4d_gamestats.h"
#include "NextBot/NextBotManager.h"
#include "NextBot/NextBotBodyInterface.h"
#include "ndebugoverlay.h"

#include "l4d2/director/zombiemanager_console_server_cvars.inc"
#include "tier0/memdbgon.h"

ZombieManager *TheZombieManager = NULL;
bool g_bZombieSpawnBypass = false;
bool g_bIgnoreZombieSpawnRules = false;
bool g_bZombieSpawnPositionOverride = false;
Vector g_vecZombieSpawnPosition( 0, 0, 0 );
bool g_bZombieSpawnAngleOverride = false;
float g_flZombieSpawnAngle = 0.0f;
bool g_bZombieSpawnIsMob = false;
ConVar z_spawn_range( "z_spawn_range", "1500", 0x00004004 );
ConVar z_notice_it_range( "z_notice_it_range", "1500", 0x00004004 );
ConVar z_mob_spawn_max_size( "z_mob_spawn_max_size", "30", 0x00004004 );
ConVar z_debug( "z_debug", "0", 0x00004004 );
ConVar z_debug_spawnable_areas( "z_debug_spawnable_areas", "0", 0x00004004, "Draws areas mobs can spawn from in green (normal) or blue (marked as OBSCURED in the nav)" );
ConVar z_debug_spawn_set( "z_debug_spawn_set", "-1", 0x00004004, "0=Anywhere, 1=Behind, 2=IT, 3=Specials in front, 4=Specials anywhere, 5=Far Away, 6=Above" );
ConVar z_spawn_mobs_from_selected_set( "z_spawn_mobs_from_selected_set", "0", 0x00004004, "Spawn subsequent mobs from the currently selected set via the nav mesh editor" );
ConVar z_debug_mob_spawn( "z_debug_mob_spawn", "0", 0x00004004 );
ConVar z_mob_population_density( "z_mob_population_density", "0.0064", 0x00004004, "Density of mob spawns, per square inch (0.0064 = 4 per 1x1 nav area)" );
ConVar z_spawn_mobs_behind_chance( "z_spawn_mobs_behind_chance", "75", 0x00004004, "Percentage chance that a mob will spawn behind the Survivor team" );
ConVar z_background_limit( "z_background_limit", "20", 0x00004004, "How many common infected are on the background map at once." );
ConVar z_mob_min_notify_count( "z_mob_min_notify_count", "10", 0x00004004, "Mobs at least this large trigger 'mob' events" );
ConVar scavenge_wanderer_respawn_chance( "scavenge_wanderer_respawn_chance", "3", 0x00004004, "Percent chance that a new wanderer will spawn in a cleared nav area." );
ConVar z_fallen_max_count( "z_fallen_max_count", "1", 0x00004004 );
ConVar z_fallen_kill_suppress_time( "z_fallen_kill_suppress_time", "300", 0x00004004 );
extern ConVar z_prevent_burrowing;
extern ConVar cleared_wanderer_respawn_chance;

namespace
{
const unsigned int s_spawnSolidMask = 0x0202420B;
const unsigned int s_spawnBrushMask = 0x0002420B;

class CountCommons
{
public:
	explicit CountCommons( int arousal ) : m_arousal( arousal ), m_count( 0 ) {}
	bool operator()( INextBot *bot )
	{
		Infected *infected = dynamic_cast< Infected * >( bot->GetEntity() );
		if ( infected && ( infected->GetTeamNumber() != 3 || infected->GetZombieClass() == 0 ) &&
			!infected->IsMarkedForDeletion() &&
			( m_arousal == 0 ||
			( m_arousal == 1 && infected->GetBodyInterface()->IsArousal( IBody::INTENSE ) ) ||
			( m_arousal == 2 && !infected->GetBodyInterface()->IsArousal( IBody::INTENSE ) ) ) )
			++m_count;
		return true;
	}
	int m_arousal;
	int m_count;
};

class CommonSpawnFilter : public CTraceFilterSimple
{
public:
	CommonSpawnFilter() : CTraceFilterSimple( NULL, COLLISION_GROUP_PLAYER_MOVEMENT ) {}
	virtual bool ShouldHitEntity( IHandleEntity *handle, int contentsMask ) OVERRIDE
	{
		if ( !CTraceFilterSimple::ShouldHitEntity( handle, contentsMask ) )
			return false;
		CBaseEntity *entity = EntityFromEntityHandle( handle );
		// Ledger306: the native NULL-dependent infected query is undefined.
		// Skip only this candidate predicate; keep the inherited gate/order.
		return entity && !entity->MyInfectedPointer();
	}
};
}

int GetCommonInfectedCount( int arousal )
{
	CountCommons count( arousal );
	TheNextBots().ForEachBot( count );
	return count.m_count;
}

void CreateZombieManager()
{
	delete TheZombieManager;
	TheZombieManager = new ZombieManager;
}

void DestroyZombieManager()
{
	delete TheZombieManager;
	TheZombieManager = NULL;
}

ZombieManager::ZombieManager() :
	m_bCanSpawn( false ), m_bTransferringTank( false ), m_nFallenSurvivorCount( 0 ),
	m_bJimmyGibbsSpawned( false ), m_bJimmyGibbsEligible( false ),
	m_nTotalSpecialSpawns( 0 ), m_nSpawnCompassMask( 0 ), m_nPendingMobCount( 0 ),
	m_mobSpawnLocation( SPAWN_ANYWHERE ), m_preferredMobPosition( vec3_origin ),
	m_flPreferredMobPositionRange( 0.0f ), m_nAmbientMobCount( 0 ),
	m_ambientMobArea( NULL ), m_nCommonSpawnCount( 0 ),
	m_specialSpawnPosition( vec3_origin ), m_bUseSpecialSpawnPosition( false ),
	m_bSpawnAreasChanged( false ), m_commonSpawnType( WANDERER )
{
	m_hJimmyGibbs = NULL;
	for ( int i = 0; i < ARRAYSIZE( m_nSpecialSpawnCount ); ++i )
		m_nSpecialSpawnCount[i] = 0;
}

ZombieManager::~ZombieManager()
{
}

void ZombieManager::OnMapStart()
{
	m_bUseSpecialSpawnPosition = false;
	m_bCanSpawn = true;
	m_bTransferringTank = false;
	m_bSpawnAreasChanged = false;
	m_delayedMegaMobTimer.Invalidate();
	m_bJimmyGibbsEligible = false;
	m_hJimmyGibbs = NULL;
	ClearSpawnAreaCollections();
	m_mobSpawnTimer.Invalidate();
	m_nPendingMobCount = 0;
	m_mobSpawnAreaTimer.Invalidate();
	m_mobSpawnAreas.RemoveAll();
	m_recentMobSpawnAreas.RemoveAll();
	m_nAmbientMobCount = 0;
	m_ambientMobTimer.Invalidate();
	m_ambientMobArea = NULL;
	m_nCommonSpawnCount = 0;
	CollectMapSpawnAreas();
	m_commonSpawnType = WANDERER;
	m_nFallenSurvivorCount = 0;
	m_fallenSurvivorSuppressionTimer.Invalidate();
	m_nSpecialSpawnCount[0] = 0;
	m_nSpawnCompassMask = 255;
	m_preferredMobPosition = vec3_origin;
	m_flPreferredMobPositionRange = 1000.0f;
}

void ZombieManager::OnRoundStart()
{
	m_bUseSpecialSpawnPosition = false;
	m_bCanSpawn = true;
	m_bTransferringTank = false;
	m_bJimmyGibbsEligible = false;
	m_hJimmyGibbs = NULL;
	m_mobSpawnTimer.Invalidate();
	m_nPendingMobCount = 0;
	m_ambientMobTimer.Invalidate();
	m_nAmbientMobCount = 0;
	m_ambientMobArea = NULL;
	m_bJimmyGibbsSpawned = false;
	m_nCommonSpawnCount = 0;
	m_delayedMegaMobTimer.Invalidate();
	ClearSpawnAreaCollections();
	m_nFallenSurvivorCount = 0;
	m_fallenSurvivorSuppressionTimer.Invalidate();
	m_nSpawnCompassMask = 255;
}

void ZombieManager::ClearSpawnAreaCollections()
{
	for ( int i = 0; i < SPAWN_LOCATION_COUNT; ++i )
	{
		m_spawnAreaCollections[i].RemoveAll();
		m_spawnAreaCollectionTimer[i].Invalidate();
	}
}

void ZombieManager::OnNavAreaDestroyed( TerrorNavArea *area )
{
	// Native 35AAE0 invalidates these entire collections, not just dead entries.
	m_mobSpawnAreas.RemoveAll();
	m_finaleSpawnAreas.RemoveAll();
	m_battlefieldSpawnAreas.RemoveAll();
	for ( int i = 0; i < SPAWN_LOCATION_COUNT; ++i )
		m_spawnAreaCollections[i].RemoveAll();
}

float ZombieManager::GetZombieSpawnRange() const
{
	return TheDirector->GetDirectorOptionFloat( "ZombieSpawnRange", z_spawn_range.GetFloat() );
}

void ZombieManager::OnAreaLeftActiveSet( TerrorNavArea *area )
{
	int chance;
	if ( TheDirector->GetScriptedMode()->IsActive() )
		chance = TheDirector->GetDirectorOptionInt( "ClearedWandererRespawnChance", cleared_wanderer_respawn_chance.GetInt() );
	else
	{
		if ( !TheDirector->GetScavengeMode()->HasStarted() )
			return;
		chance = scavenge_wanderer_respawn_chance.GetInt();
	}
	if ( chance > 0 && area->GetWanderingPopulation() == 0 && area->IsValidForWanderingPopulation() &&
		RandomInt( 0, 100 ) <= chance )
	{
		int population = area->GetWanderingPopulation() + 1;
		area->SetWanderingPopulation( population < 0 ? 0 : population );
	}
}

Infected *ZombieManager::SpawnCommonZombie( TerrorNavArea *area, const Vector &position, CommonSpawnType type )
{
	if ( !m_bCanSpawn )
		return NULL;
	Vector hullMin( -15, -15, 0 );
	Vector hullMax( 15, 15, 70 );
	CommonSpawnFilter filter;
	trace_t trace;
	UTIL_TraceHull( position, position, hullMin, hullMax, s_spawnSolidMask, &filter, &trace );
	Vector spawnPosition;
	if ( trace.startsolid || trace.fraction < 1.0f )
	{
		UTIL_TraceHull( position + Vector( 0, 0, 18 ), position, hullMin, hullMax,
			s_spawnSolidMask, &filter, &trace );
		if ( trace.startsolid || trace.fraction == 0.0f )
		{
			if ( z_debug.GetBool() )
				Msg( "%3.2f: SpawnCommonZombie: Spot blocked (%.0f, %.0f, %.0f)\n",
					gpGlobals->curtime, position.x, position.y, position.z );
			return NULL;
		}
		spawnPosition = trace.endpos;
	}
	else
		spawnPosition = position;
	if ( z_prevent_burrowing.GetBool() )
	{
		UTIL_TraceHull( spawnPosition, spawnPosition + Vector( 0, 0, 500 ), hullMin,
			hullMax, s_spawnBrushMask, &filter, &trace );
		if ( trace.fraction > 0.0f )
		{
			Vector top = trace.endpos;
			UTIL_TraceHull( top, spawnPosition, hullMin, hullMax, s_spawnBrushMask, &filter, &trace );
			if ( trace.fraction < 1.0f && trace.IsDispSurface() )
			{
				Warning( "Zombie tried to spawn under displacement in nav area %d, setpos %.1f %.1f %.1f\n",
					area ? area->GetID() : -1, spawnPosition.x, spawnPosition.y, spawnPosition.z );
				return NULL;
			}
		}
	}
	Infected *infected = static_cast< Infected * >( CreateEntityByName( "infected" ) );
	if ( !infected )
		return NULL;
	m_commonSpawnType = type;
	infected->SetAbsOrigin( spawnPosition );
	infected->SetAbsAngles( QAngle( 0, RandomFloat( 0.0f, 360.0f ), 0 ) );
	if ( g_bZombieSpawnAngleOverride )
		infected->SetAbsAngles( QAngle( 0, g_flZombieSpawnAngle, 0 ) );
	DispatchSpawn( infected );
	infected->Update();
	if ( type == MOB )
		infected->m_mobRush = true;
	else if ( type == AMBIENT_MOB )
		infected->SetAmbientMob();
	else
		infected->SetActive( TheDirector->GetActiveCommonCount() <= TheDirector->GetNumReservedWanderers() );
	++m_nCommonSpawnCount;
	L4DGameStats().Event_ZombieSpawned( 0, 1 );
	m_commonSpawnType = WANDERER;
	return infected;
}

bool ZombieManager::IsMobSpawnNeeded() const
{
	return m_nPendingMobCount > TheDirector->GetCommonLimit() - GetCommonInfectedCount();
}

void ZombieManager::SpawnITMob( int count )
{
	DevMsg( "%3.2f: (MOB) SpawnITMob requested of size %d.\n", gpGlobals->curtime, count );
	if ( m_nPendingMobCount < count )
		m_nPendingMobCount = count;
	DevMsg( "%3.2f: (MOB) SpawnITMob will make %d.\n", gpGlobals->curtime, m_nPendingMobCount );
	m_mobSpawnLocation = SPAWN_NEAR_IT;
	m_mobSpawnAreas.RemoveAll();
	m_recentMobSpawnAreas.RemoveAll();
}

void ZombieManager::ClearPendingMobCount()
{
	m_nPendingMobCount = 0;
	DevMsg( "%3.2f: ZombieManager::ClearPendingMobCount: All mobs cleared.\n", gpGlobals->curtime );
}

void ZombieManager::SetPendingMobCount( int count )
{
	m_nPendingMobCount = count;
	DevMsg( "%3.2f: ZombieManager::SetPendingMobCount: Pending mob set to %d.\n", gpGlobals->curtime, count );
}

void ZombieManager::StartDelayedMegaMobTimer()
{
	m_delayedMegaMobTimer.Start( RandomFloat( 2.0f, 4.0f ) );
}

void ZombieManager::StartJimmyGibbsSpawnTimer()
{
	m_jimmyGibbsSpawnTimer.Start( RandomFloat( 60.0f, 240.0f ) );
}

void ZombieManager::RequestDelayedMegaMob()
{
	StartDelayedMegaMobTimer();
}

bool ZombieManager::CanSpawnFallenSurvivor() const
{
	if ( m_nFallenSurvivorCount > z_fallen_max_count.GetInt() )
		return false;
	if ( m_fallenSurvivorSuppressionTimer.m_timestamp > 0.0f )
	{
		float ratio = ( m_fallenSurvivorSuppressionTimer.m_timestamp - gpGlobals->curtime ) /
			m_fallenSurvivorSuppressionTimer.m_duration;
		if ( ratio >= 0.0f && ( ratio > 1.0f || ratio > 0.0f ) )
			return false;
	}
	return true;
}

void ZombieManager::OnFallenSurvivorRemoved( bool killed )
{
	if ( killed )
		m_fallenSurvivorSuppressionTimer.Start( z_fallen_kill_suppress_time.GetFloat() );
	--m_nFallenSurvivorCount;
}

void ZombieManager::SetSpecialSpawnPosition( const Vector &position )
{
	m_specialSpawnPosition = position;
	m_bUseSpecialSpawnPosition = true;
}

void ZombieManager::SetPreferredMobSpawnPosition( const Vector &position, float range )
{
	m_preferredMobPosition = position;
	m_flPreferredMobPositionRange = range;
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif

// S3FFAD0's reached one-off selection branch, using the real S3FE710
// readiness query and repeated S2879D0 map query. No rollback on RNG miss.
bool ZombieManager::TrySelectJimmyGibbs()
{
	if ( !TerrorGameRules()->IsCoopMode() )
		return false;
	if ( Q_stricmp( STRING( gpGlobals->mapname ), "c1m4_atrium" ) )
		return false;
	if ( !TheDirector->IsJimmyGibbsDay() || !( m_jimmyGibbsSpawnTimer.m_timestamp > 0.0f ) ||
		!( (double)gpGlobals->curtime >= (double)m_jimmyGibbsSpawnTimer.m_timestamp ) )
		return false;
	if ( m_bJimmyGibbsSpawned )
		return false;
	if ( Q_stricmp( STRING( gpGlobals->mapname ), "c1m4_atrium" ) )
		return false;
	m_bJimmyGibbsSpawned = true;
	return RandomFloat( 0.0f, 100.0f ) < 33.0f;
}

// S358B00. Count is Director444's existing Witch object-lifetime count.
Witch *ZombieManager::SpawnWitch( const Vector &position, const QAngle &angles )
{
	const int limit = TheDirector->GetWitchLimit();
	if ( limit >= 0 && TheDirector->GetWitchCount() >= limit )
		return NULL;
	Witch *witch = static_cast< Witch * >( CreateEntityByName( "witch" ) );
	if ( witch )
	{
		witch->SetAbsOrigin( position );
		witch->SetAbsAngles( angles );
		DispatchSpawn( witch, true );
		L4DGameStats().Event_ZombieSpawned( 7, 1 );
	}
	return witch;
}

// S358B80 deliberately gates only on limit!=0, not count>=limit, and has
// NO ordinary-Witch statistics callback. Spawn must retain this pre-set bit.
Witch *ZombieManager::SpawnWitchBride( const Vector &position, const QAngle &angles )
{
	if ( TheDirector->GetWitchLimit() == 0 )
		return NULL;
	Witch *witch = static_cast< Witch * >( CreateEntityByName( "witch" ) );
	if ( witch )
	{
		witch->SetAbsOrigin( position );
		witch->SetAbsAngles( angles );
		witch->SetInfectedFlags( 0x1000 );
		DispatchSpawn( witch, true );
	}
	return witch;
}

// S358F10. The existing shared solver owns center-first and subsequent RNG.
Witch *ZombieManager::SpawnWitchInArea( TerrorNavArea *area, const QAngle &angles, bool centerFirst )
{
	Vector position;
	if ( FindZombieSpawnPosition( area, 10, &position, centerFirst ) )
		return SpawnWitch( position, angles );
	Warning( "Failed to find a witch spawn position in area %d\n", area->GetID() );
	return NULL;
}

// S358F70. Native calls actual Spawn directly, not DispatchSpawn, for Tank.
CTerrorPlayer *ZombieManager::SpawnTank( const Vector &position, const QAngle &angles )
{
	const int limit = TheDirector->GetTankLimit();
	if ( limit >= 0 && TheDirector->GetTankCount() >= limit )
		return NULL;
	CTerrorPlayer *tank = CreateTerrorInfectedPlayerBot( 8 );
	if ( tank )
	{
		tank->HandleCommand_JoinTeam( TEAM_INFECTED, SurvivorCharacter_Unknown, true );
		tank->State_Transition( STATE_ACTIVE );
		tank->SetAbsOrigin( position );
		tank->SetAbsAngles( angles );
		m_specialSpawnPosition = tank->GetAbsOrigin();
		m_bUseSpecialSpawnPosition = true;
		tank->Spawn();
		m_bUseSpecialSpawnPosition = false;
		L4DGameStats().Event_ZombieSpawned( 8, 1 );
		TheDirector->m_nTankControlPassCount = 0;
	}
	return tank;
}

// S359F60.
CTerrorPlayer *ZombieManager::SpawnTankInArea( TerrorNavArea *area, const QAngle &angles, bool centerFirst )
{
	Vector position;
	if ( FindZombieSpawnPosition( area, 10, &position, centerFirst ) )
		return SpawnTank( position, angles );
	Warning( "Failed to find a tank spawn position in area %d\n", area->GetID() );
	return NULL;
}

// S3590C0, exact concrete table domain1..6. The native JoinTeam character
// argument8 is SurvivorCharacter_Unknown, NOT the requested zombie class.
CTerrorPlayer *ZombieManager::SpawnSpecial( int zombieClass, const Vector &position, const QAngle &angles )
{
	if ( (unsigned int)zombieClass - 1u > 5u )
		return NULL;
	CTerrorPlayer *zombie = CreateTerrorInfectedPlayerBot( zombieClass );
	if ( zombie )
	{
		zombie->HandleCommand_JoinTeam( TEAM_INFECTED, SurvivorCharacter_Unknown, false );
		zombie->State_Transition( STATE_ACTIVE );
		zombie->SetAbsOrigin( position );
		zombie->SetAbsAngles( angles );
		m_specialSpawnPosition = zombie->GetAbsOrigin();
		m_bUseSpecialSpawnPosition = true;
		DispatchSpawn( zombie, true );
		m_bUseSpecialSpawnPosition = false;
		L4DGameStats().Event_ZombieSpawned( zombieClass, 1 );
		++m_nSpecialSpawnCount[0];
		++m_nTotalSpecialSpawns;
		++m_nSpecialSpawnCount[zombieClass];
	}
	return zombie;
}

// S359FC0 is ten RANDOM attempts, unlike the center-first boss wrappers.
CTerrorPlayer *ZombieManager::SpawnSpecialInArea( int zombieClass, TerrorNavArea *area, const QAngle &angles )
{
	Vector position;
	if ( !FindZombieSpawnPosition( area, 10, &position, false ) )
		return NULL;
	return SpawnSpecial( zombieClass, position, angles );
}

// S35AD30. Preserve death BEFORE the new global/context writes, direct Spawn,
// post-Spawn health restoration, the narrow duck transfer and observer tail.
bool ZombieManager::TransferTank( CTerrorPlayer *oldTank, CTerrorPlayer *newTank )
{
	if ( !oldTank->IsAlive() || oldTank->GetTeamNumber() != TEAM_INFECTED ||
		oldTank->GetZombieClass() != 8 || !newTank )
		return false;
	const Vector position = oldTank->GetAbsOrigin();
	const bool ducked = oldTank->m_Local.m_bDucked;
	const int health = oldTank->GetHealth();
	oldTank->KillSilently();
	g_iNextZombieClass = 8;
	m_bTransferringTank = true;
	m_specialSpawnPosition = position;
	m_bUseSpecialSpawnPosition = true;
	newTank->State_Transition( STATE_ACTIVE );
	newTank->Spawn();
	m_bUseSpecialSpawnPosition = false;
	newTank->SetHealth( health );
	m_bTransferringTank = false;
	if ( ducked )
	{
		newTank->m_Local.m_bDucked = true;
		newTank->m_Local.m_bDucking = false;
		newTank->AddFlag( FL_DUCKING );
		UTIL_SetSize( newTank, GameRules()->GetViewVectors()->m_vDuckHullMin,
			GameRules()->GetViewVectors()->m_vDuckHullMax );
	}
	oldTank->SetObserverTarget( newTank );
	return true;
}

#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

#include "l4d2/director/zombiemanager_console_server_callbacks.inc"
