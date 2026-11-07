//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/nav/terror_nav.h"
#include "l4d2/nav/terror_nav_world.h"
#include "l4d2/nav/terror_nav_search.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/director/terror_director.h"
#include "team.h"
#include "l4d2/entities/info_changelevel.h"
#include "BasePropDoor.h"
#include "doors.h"
#include "l4d2/weapons/weapon_spawn.h"
#include "world.h"
#include "obstacle_pushaway.h"
#include "ispatialpartition.h"
#include "ndebugoverlay.h"
#include "l4d2/nav/terror_nav_world_console_server_cvars.inc"
#include "tier0/memdbgon.h"

extern ConVar z_background_limit;
static ConVar nav_debug_finale_area( "nav_debug_finale_area", "0", 0x00004004,
	"Show extents of finale area and selected finale center" );

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif

namespace
{

bool IsCheckpointArea( const TerrorNavArea *area )
{
	if ( !area->HasSpawnAttributes( TERROR_NAV_CHECKPOINT ) ) return false;
	const Vector northwest = area->GetCorner( NORTH_WEST );
	const Vector southeast = area->GetCorner( SOUTH_EAST );
	// Native258D9C/25A430 JNB rejects >= but admits unordered corners.
	return !( northwest.x >= southeast.x ) && !( northwest.y >= southeast.y );
}


bool IsOutwardChangeLevel( CBaseEntity *entity )
{
	InfoChangelevel *changelevel = dynamic_cast< InfoChangelevel * >( entity );
	return changelevel && changelevel->IsOutward();
}

CBaseEntity *FindCheckpointChangeLevel( const Extent &bounds )
{
	const Vector center = ( bounds.lo + bounds.hi ) * 0.5f;
	const float radius = ( bounds.hi - bounds.lo ).Length();
	return FindNavEntityByClassnameNearest( "info_changelevel", center, radius );
}

Vector NearestCheckpointItem( const Vector &origin )
{
	Vector result = vec3_origin;
	float distance = 10000.0f;
	const CUtlVector< CWeaponSpawn * > &spawns = CWeaponSpawn::GetWeaponSpawns();
	FOR_EACH_VEC( spawns, i )
	{
		CWeaponSpawn *spawn = spawns[i];
		if ( spawn->ClassMatches( "weapon_ammo_spawn" ) ) continue;
		float next = ( spawn->WorldSpaceCenter() - origin ).Length();
		if ( distance > next ) { distance = next; result = spawn->WorldSpaceCenter(); }
	}
	return result;
}
}

CTerrorCheckpoint::CTerrorCheckpoint()
{
	m_totalArea = m_spawnAreaSize = 0.0f;
	m_bounds.lo.Init( 100000.0f, 100000.0f, 100000.0f );
	m_bounds.hi.Init( -100000.0f, -100000.0f, -100000.0f );
	m_isOutward = false;
	m_hasRespawnedPlayers = false;
}

void CTerrorCheckpoint::AddArea( TerrorNavArea *area )
{
	m_areas.AddToTail( area );
	m_totalArea += area->GetSizeX() * area->GetSizeY();
	Extent extent;
	area->GetExtent( &extent );
	for ( int i = 0; i < 3; ++i )
	{
		if ( m_bounds.lo[i] > extent.lo[i] ) m_bounds.lo[i] = extent.lo[i];
		else if ( extent.lo[i] > m_bounds.hi[i] ) m_bounds.hi[i] = extent.lo[i];
		if ( m_bounds.lo[i] > extent.hi[i] ) m_bounds.lo[i] = extent.hi[i];
		else if ( extent.hi[i] > m_bounds.hi[i] ) m_bounds.hi[i] = extent.hi[i];
	}
}


void CTerrorCheckpoint::BindToWorld()
{
	m_hasRespawnedPlayers = false;
	CBaseEntity *changelevel = FindCheckpointChangeLevel( m_bounds );
	// Both native bind entries emit this message even when one was found.
	FlowMessage( 1, "Flow Warning: Checkpoint failed to locate nearby info_change_level!\n" );
	m_isOutward = IsOutwardChangeLevel( changelevel );
	Vector low = m_bounds.lo - Vector( 50, 50, 50 );
	Vector high = m_bounds.hi + Vector( 50, 50, 71 );
	CBaseEntity *entities[16];
	CBotDoorEnumerator enumerator( entities, ARRAYSIZE( entities ) );
	Ray_t doorRay;
	doorRay.Init( vec3_origin, vec3_origin, low, high );
	partition->EnumerateElementsAlongRay( PARTITION_ENGINE_SOLID_EDICTS, doorRay, false, &enumerator );
	int count = enumerator.m_nAlreadyHit;
	m_doors.RemoveAll();
	for ( int i = 0; i < count; ++i )
	{
		CBasePropDoor *door = dynamic_cast< CBasePropDoor * >( entities[i] );
		if ( door && door->m_takedamage == DAMAGE_EVENTS_ONLY && door->ClassMatches( "prop_door_rotating_checkpoint" ) )
		{
			m_doors.AddToTail( door );
			door->SetRequired( true );
		}
	}
	m_spawnAreas.RemoveAll();
	m_spawnAreaSize = 0.0f;
	if ( !m_areas.Count() ) return;
	TerrorNavArea *seed = m_areas[0];
	CBaseEntity *ammo = gEntList.FindEntityByClassnameWithin( NULL, "weapon_ammo_spawn", low, high );
	if ( ammo )
	{
		float distance = FLT_MAX;
		FOR_EACH_VEC( m_areas, i )
		{
			TerrorNavArea *area = m_areas[i];
			if ( area->HasSpawnAttributes( TERROR_NAV_DOOR ) ) continue;
			double next = area->GetDistanceSquaredToPoint( ammo->GetAbsOrigin() );
			float storedNext = (float)next; // Native FST before the unrounded comparison.
			if ( (double)distance > next ) { distance = storedNext; seed = area; }
		}
	}
	else
	{
		FOR_EACH_VEC( m_areas, i )
			if ( !m_areas[i]->HasSpawnAttributes( TERROR_NAV_DOOR ) ) { seed = m_areas[i]; break; }
	}
	struct SpawnAreaCollector
	{
		CUtlVector< TerrorNavArea * > &areas;
		float &size;
		bool operator()( TerrorNavArea *area )
		{
			if ( !IsCheckpointArea( area ) || area->HasSpawnAttributes( TERROR_NAV_DOOR ) ) return false;
			areas.AddToTail( area );
			size += area->GetSizeX() * area->GetSizeY();
			return true;
		}
	};
	SpawnAreaCollector collector = { m_spawnAreas, m_spawnAreaSize };
	CollectSurroundingTerrorAreas( seed, seed->GetCenter(), collector );
}

void CTerrorCheckpoint::OnAreaRemoved( TerrorNavArea *area )
{
	m_areas.FindAndFastRemove( area );
	m_spawnAreas.FindAndFastRemove( area );
	// S:0x257F30 does not recompute cached extents or area sums here.
}

bool CTerrorCheckpoint::AreAllDoorsClosed() const
{
	if ( !m_doors.Count() ) return false;
	FOR_EACH_VEC( m_doors, i )
		if ( m_doors[i] && !m_doors[i]->IsDoorClosed() ) return false;
	return true;
}

CBasePropDoor *CTerrorCheckpoint::GetOpenDoor() const
{
	FOR_EACH_VEC( m_doors, i )
		if ( m_doors[i] && m_doors[i]->IsDoorOpen() ) return m_doors[i];
	return NULL;
}

void CTerrorCheckpoint::CloseAndLockDoors()
{
	variant_t empty;
	FOR_EACH_VEC( m_doors, i )
	{
		if ( !m_doors[i] ) continue;
		m_doors[i]->AcceptInput( "Close", NULL, NULL, empty, 0 );
		m_doors[i]->AcceptInput( "Lock", NULL, NULL, empty, 0 );
	}
}

TerrorNavArea *CTerrorCheckpoint::GetLargestSpawnArea() const
{
	float largest = 0.0f;
	TerrorNavArea *selected = NULL;
	FOR_EACH_VEC( m_spawnAreas, i )
	{
		float size = m_spawnAreas[i]->GetSizeX() * m_spawnAreas[i]->GetSizeY();
		if ( size > largest ) { selected = m_spawnAreas[i]; largest = size; }
	}
	return selected;
}

bool CTerrorCheckpoint::GetSpawnPosition( Vector *origin, QAngle *angles, TerrorNavArea **result ) const
{
	if ( !origin ) return false;
	*origin = vec3_origin;
	if ( angles ) *angles = vec3_angle;
	CUtlVector< TerrorNavArea * > candidates;
	float total = 0.0f;
	FOR_EACH_VEC( m_spawnAreas, i )
	{
		TerrorNavArea *area = m_spawnAreas[i];
		bool rejected = false;
		FOR_EACH_VEC( m_doors, j )
		{
			if ( m_doors[j] && ( area->IsBlocked( TEAM_ANY ) || area->IsOverlapping( m_doors[j]->WorldSpaceCenter(), 0.0f ) ) )
			{ rejected = true; break; }
		}
		if ( rejected ) continue;
		candidates.AddToTail( area );
		total += area->GetSizeX() * area->GetSizeY();
	}
	for ( int attempt = 0; attempt < 100; ++attempt )
	{
		float choice = RandomFloat( 0.0f, total - 0.1f );
		float sum = 0.0f;
		TerrorNavArea *area = NULL;
		FOR_EACH_VEC( candidates, i )
		{
			sum += candidates[i]->GetSizeX() * candidates[i]->GetSizeY();
			if ( sum > choice ) { area = candidates[i]; break; }
		}
		if ( !area ) { Warning( "Checkpoint::GetSpawnPosition: Selected NULL area\n" ); return false; }
		Extent bounds;
		area->GetExtent( &bounds );
		Vector point;
		point.x = RandomFloat( bounds.lo.x, bounds.hi.x );
		point.y = RandomFloat( bounds.lo.y, bounds.hi.y );
		point.z = area->GetZ( point.x, point.y );
		trace_t trace;
		UTIL_TraceHull( point, point, VEC_HULL_MIN, VEC_HULL_MAX, 0x0201420B, NULL, COLLISION_GROUP_PLAYER_MOVEMENT, &trace );
		if ( trace.startsolid || !( trace.fraction >= 1.0f ) ) continue;
		*origin = point;
		if ( angles )
		{
			Vector eye = point + Vector( 0, 0, 35.5f );
			VectorAngles( NearestCheckpointItem( eye ) - eye, *angles );
			angles->x = 0.0f;
		}
		if ( result ) *result = area;
		return true;
	}
	Warning( "Checkpoint::GetSpawnPosition: Failed after many tries\n" );
	return false;
}

void OrientTerrorSpawnToNearbyItem( const Vector &origin, QAngle *angles )
{
	if ( !angles ) return;
	float distance = 1000.0f;
	CWeaponSpawn *selected = NULL;
	const CUtlVector< CWeaponSpawn * > &spawns = CWeaponSpawn::GetWeaponSpawns();
	FOR_EACH_VEC( spawns, i )
	{
		CWeaponSpawn *spawn = spawns[i];
		if ( spawn->ClassMatches( "weapon_ammo_spawn" ) ) continue;
		if ( ( spawn->WorldSpaceCenter() - origin ).LengthSqr() >= distance * distance ) continue;
		if ( !spawn->FVisible( origin + VEC_VIEW, MASK_BLOCKLOS ) ) continue;
		distance = ( spawn->WorldSpaceCenter() - origin ).Length();
		selected = spawn;
	}
	if ( selected ) VectorAngles( selected->WorldSpaceCenter() - ( origin + VEC_VIEW ), *angles );
}

void TerrorNavMesh::ClearCheckpoints()
{
	ResetCheckpointSpawnOverride();
	m_checkpoints.PurgeAndDeleteElements();
	m_startArea = m_goalArea = NULL;
	m_hasFlowDistances = false;
}

void TerrorNavMesh::BuildCheckpoints()
{
	m_checkpoints.PurgeAndDeleteElements();
	CUtlVector< TerrorNavArea * > remaining;
	FOR_EACH_VEC( TheNavAreas, i )
	{
		TerrorNavArea *area = ToTerrorNavArea( TheNavAreas[i] );
		if ( !IsCheckpointArea( area ) ) continue;
		// S:0x25A3D0 ships this one authored-map exclusion.
		if ( area->GetID() == 529422 && !Q_stricmp( STRING( gpGlobals->mapname ), "c4m4_milltown_b" ) ) continue;
		remaining.AddToTail( area );
	}
	TerrorNavArea *previousSeed = NULL;
	while ( remaining.Count() )
	{
		TerrorNavArea *seed = remaining[0];
		// Stop only the native no-progress outer-loop revisit.
		if ( seed == previousSeed ) break;
		previousSeed = seed;
		CTerrorCheckpoint *checkpoint = new CTerrorCheckpoint;
		m_checkpoints.AddToTail( checkpoint );
		struct CheckpointCollector
		{
			CUtlVector< TerrorNavArea * > &remaining;
			CTerrorCheckpoint *checkpoint;
			bool operator()( TerrorNavArea *area )
			{
				if ( !IsCheckpointArea( area ) ) return false;
				int index = remaining.Find( area );
				if ( index == remaining.InvalidIndex() ) return false;
				remaining.Remove( index );
				checkpoint->AddArea( area );
				return true;
			}
		};
		CheckpointCollector collector = { remaining, checkpoint };
		CollectSurroundingTerrorAreas( seed, seed->GetCenter(), collector );
		checkpoint->BindToWorld();
	}
}

CTerrorCheckpoint *TerrorNavMesh::GetExitCheckpoint() const
{
	FOR_EACH_VEC( m_checkpoints, i )
		if ( m_checkpoints[i]->IsOutward() ) return m_checkpoints[i];
	return NULL;
}

CTerrorCheckpoint *TerrorNavMesh::GetCheckpoint( const Vector &position ) const
{
	for ( float radius = 100.0f; radius <= 1000.0f; radius += 100.0f )
	{
		Extent bounds;
		bounds.lo = position - Vector( radius, radius, radius );
		bounds.hi = position + Vector( radius, radius, radius );
		FOR_EACH_VEC( m_checkpoints, i )
			if ( bounds.IsOverlapping( m_checkpoints[i]->GetBounds() ) ) return m_checkpoints[i];
	}
	return NULL;
}

TerrorNavArea *TerrorNavMesh::SelectFlowGoalArea() const
{
	CBaseEntity *entity = gEntList.FindEntityByName( NULL, "nav_flow_target" );
	TerrorNavArea *area = entity ? ToTerrorNavArea( GetNearestNavArea( entity, 0, 200.0f ) ) : NULL;
	if ( area ) { FlowMessage( 1, "Goal area set by a nav_flow_target!\n" ); return area; }
	CTerrorCheckpoint *checkpoint = GetExitCheckpoint();
	if ( checkpoint && ( area = checkpoint->GetLargestSpawnArea() ) != NULL )
	{ FlowMessage( 1, "Goal area set by checkpoint!\n" ); return area; }
	entity = gEntList.FindEntityByClassname( NULL, "trigger_finale" );
	area = entity ? ToTerrorNavArea( GetNearestNavArea( entity, 0, 200.0f ) ) : NULL;
	if ( area && area->HasSpawnAttributes( TERROR_NAV_FINALE ) )
	{ FlowMessage( 1, "Goal area set by finale attribute and radio!\n" ); return area; }
	entity = NULL;
	while ( ( entity = gEntList.FindEntityByClassname( entity, "prop_health_cabinet" ) ) != NULL )
	{
		area = ToTerrorNavArea( GetNearestNavArea( entity, 0, 200.0f ) );
		if ( area && area->HasSpawnAttributes( TERROR_NAV_FINALE ) )
		{ FlowMessage( 1, "Goal area set by finale attribute and health cabinet!\n" ); return area; }
	}
	FOR_EACH_VEC( TheNavAreas, i )
	{
		area = ToTerrorNavArea( TheNavAreas[i] );
		if ( area->HasSpawnAttributes( TERROR_NAV_FINALE ) && area->HasSpawnAttributes( TERROR_NAV_RESCUE_CLOSET ) )
		{ FlowMessage( 1, "Goal area set by finale and rescue closet attributes, ie. the finale is reachable.\n" ); return area; }
	}
	Extent finale;
	finale.lo.Init( 1000000000.0f, 1000000000.0f, 1000000000.0f );
	finale.hi.Init( -1000000000.0f, -1000000000.0f, -1000000000.0f );
	FOR_EACH_VEC( TheNavAreas, i )
	{
		area = ToTerrorNavArea( TheNavAreas[i] );
		if ( !area->HasSpawnAttributes( TERROR_NAV_FINALE ) ) continue;
		Extent bounds;
		area->GetExtent( &bounds );
		for ( int axis = 0; axis < 3; ++axis )
		{
			if ( finale.lo[axis] > bounds.lo[axis] ) finale.lo[axis] = bounds.lo[axis];
			else if ( bounds.lo[axis] > finale.hi[axis] ) finale.hi[axis] = bounds.lo[axis];
			if ( finale.lo[axis] > bounds.hi[axis] ) finale.lo[axis] = bounds.hi[axis];
			else if ( bounds.hi[axis] > finale.hi[axis] ) finale.hi[axis] = bounds.hi[axis];
		}
	}
	Vector center = ( finale.lo + finale.hi ) * 0.5f;
	float distance = 1000000000.0f;
	TerrorNavArea *goal = NULL;
	FOR_EACH_VEC( TheNavAreas, i )
	{
		area = ToTerrorNavArea( TheNavAreas[i] );
		if ( !area->HasSpawnAttributes( TERROR_NAV_FINALE ) ) continue;
		Vector delta = area->GetCenter() - center;
		float next = ( delta.y * delta.y + delta.x * delta.x ) + delta.z * delta.z;
		if ( distance > next ) { distance = next; goal = area; }
	}
	if ( goal ) FlowMessage( 1, "Goal area set by finale attribute at center of area, note the quality is questionable - ie. may not be reachable.\n" );
	if ( nav_debug_finale_area.GetBool() )
	{
		if ( goal ) goal->DrawFilled( 255, 255, 0, 255, 99999.898f, true );
		NDebugOverlay::Box( vec3_origin, finale.lo, finale.hi, 255, 255, 0, 255, 10000000.0f );
	}
	if ( goal ) return goal;
	entity = gEntList.FindEntityByName( NULL, "nav_cordon_flow_target" );
	if ( !entity ) return NULL;
	area = ToTerrorNavArea( GetNearestNavArea( entity, 0, 200.0f ) );
	if ( gEntList.FindEntityByName( entity, "nav_cordon_flow_target" ) )
		FlowMessage( 1, "Flow Warning: Found multiple nav_cordon_flow_target(s).\n" );
	if ( area ) FlowMessage( 1, "Goal area set by nav_cordon_flow_target.\n" );
	return area;
}


TerrorNavMesh::~TerrorNavMesh()
{
	ClearCheckpoints();
}

void TerrorNavMesh::OnServerActivate()
{
	m_boundarySeeds.RemoveAll();
	m_boundarySpreadAreas.RemoveAll();
	m_boundarySpreadIndex = 0;
	m_maxFlowDistance = m_maxNonCheckpointFlowDistance = 0.0f;
	m_startArea = m_rescueVehicleArea = m_goalArea = NULL;
	m_hasFlowDistances = false;
	m_flowRecomputeTimer.Invalidate();
	BuildCheckpoints();
	BaseClass::OnServerActivate();
	m_debugSelectedArea = NULL;
	TerrorNavArea::ResetAASCollection();
	m_navTimer1700.Invalidate();
}

void TerrorNavMesh::OnRoundRestart()
{
	BaseClass::OnRoundRestart();
	if ( !IsAnalyzed() ) Warning( "The nav mesh needs a full nav_analyze\n" );
	m_wanderingSpawnTimer.Invalidate();
	m_wanderingPopulationTimer.Invalidate();
	InitializeFlowDistances();
	ResetCheckpointSpawnOverride();
	FOR_EACH_VEC( m_checkpoints, i ) m_checkpoints[i]->BindToWorld();
	InitializeBoundaryDistances();
	m_boundarySpreadTimer.Invalidate();
	m_navTimer1700.Invalidate();
	m_flowRecomputeTimer.Invalidate();
	// Native2E3EC0 follows computation/checkpoint/boundary work.
	FOR_EACH_VEC( TheNavAreas, i )
	{
		TerrorNavArea *area = ToTerrorNavArea( TheNavAreas[i] );
		if ( area->HasSpawnAttributes( TERROR_NAV_RESCUE_VEHICLE ) )
			area->MarkAsBlocked( TEAM_ANY, NULL, true );
	}
	IGameEvent *event = gameeventmanager->CreateEvent( "round_start_post_nav" );
	if ( event ) gameeventmanager->FireEvent( event );
	if ( TheDirector->IsBackgroundMap() ) PopulateBackground( z_background_limit.GetInt(), true );
}

namespace
{
class CBindNavDoor
{
public:
	CBindNavDoor( CBaseEntity *door ) : m_door( door ) {}
	bool operator()( CNavArea *area )
	{
		TerrorNavArea *terror = ToTerrorNavArea( area );
		terror->SetDoor( m_door );
		terror->AddSpawnAttributes( TERROR_NAV_DOOR );
		return true;
	}
private:
	CBaseEntity *m_door;
};
}

void TerrorNavMesh::OnDoorCreated( CBaseEntity *entity )
{
	Extent bounds;
	CBaseDoor *brush = dynamic_cast< CBaseDoor * >( entity );
	if ( brush )
	{
		QAngle angles = brush->GetLocalAngles();
		Vector origin = brush->GetLocalOrigin();
		TOGGLE_STATE state = brush->m_toggle_state;
		brush->SetToggleState( TS_AT_BOTTOM );
		brush->CollisionProp()->WorldSpaceSurroundingBounds( &bounds.lo, &bounds.hi );
		brush->SetToggleState( state );
		brush->SetLocalOrigin( origin );
		brush->SetLocalAngles( angles );
	}
	else
	{
		CBasePropDoor *prop = dynamic_cast< CBasePropDoor * >( entity );
		if ( !prop ) return;
		prop->ComputeDoorExtent( &bounds, CBasePropDoor::DOOR_EXTENT_CLOSED );
		for ( int axis = 0; axis < 3; ++axis )
		{
			if ( bounds.lo[axis] > bounds.hi[axis] ) bounds.hi[axis] = bounds.lo[axis];
			if ( bounds.lo[axis] > bounds.hi[axis] ) bounds.lo[axis] = bounds.hi[axis];
		}
	}
	bounds.lo.z -= 18.0f;
	bounds.hi.z -= 18.0f;
	CBindNavDoor bind( entity );
	ForAllAreasOverlappingExtent( bind, bounds );
}

void TerrorNavMesh::BindWorldEntities()
{
	FlowMessage( 0, "Flow Debug...\n" );
	CBaseEntity *entity = NULL;
	while ( ( entity = gEntList.FindEntityByClassname( entity, "prop_door*" ) ) != NULL )
	{
		OnDoorCreated( entity );
		const char *name = STRING( entity->GetEntityName() );
		if ( !Q_stricmp( name, "checkpoint_exit" ) || !Q_stricmp( name, "checkpoint_entrance" ) )
			FlowMessage( 1, "Door(prop): %s (%f %f %f)\n", name,
				(double)entity->GetAbsOrigin().x, (double)entity->GetAbsOrigin().y, (double)entity->GetAbsOrigin().z );
	}
	entity = NULL;
	while ( ( entity = gEntList.FindEntityByClassname( entity, "func_door*" ) ) != NULL )
	{
		OnDoorCreated( entity );
		const char *name = STRING( entity->GetEntityName() );
		if ( !Q_stricmp( name, "checkpoint_exit" ) || !Q_stricmp( name, "checkpoint_entrance" ) )
			FlowMessage( 1, "Door(func): %s (%f %f %f)\n", name,
				(double)entity->GetAbsOrigin().x, (double)entity->GetAbsOrigin().y, (double)entity->GetAbsOrigin().z );
	}
	// Native2EA380 seeds the REAL dirty-breakable queue on post-load. Dynamic
	// creation alone does not cover a reload against already existing props.
	entity = NULL;
	while ( ( entity = gEntList.FindEntityByClassname( entity, "prop_physics" ) ) != NULL )
		if ( entity->GetHealth() != 0 && entity->HasSpawnFlags( 0x3000 ) ) OnBreakableCreated( entity );
	entity = NULL;
	while ( ( entity = gEntList.FindEntityByClassname( entity, "func_breakable" ) ) != NULL )
		if ( entity->GetHealth() != 0 && entity->HasSpawnFlags( 0x3000 ) ) OnBreakableCreated( entity );
	ApplyNavAttributeRegions();
}

void TerrorNavMesh::ApplyNavAttributeRegions()
{
	variant_t empty;
	CBaseEntity *entity = NULL;
	while ( ( entity = gEntList.FindEntityByClassname( entity, "func_nav_attribute_region" ) ) != NULL )
		entity->AcceptInput( "ApplyNavAttributes", NULL, NULL, empty, 0 );
	entity = NULL;
	while ( ( entity = gEntList.FindEntityByClassname( entity, "point_nav_attribute_region" ) ) != NULL )
		entity->AcceptInput( "ApplyNavAttributes", NULL, NULL, empty, 0 );
}

// S2E5E40. This is RemoveNavArea, not OnEditDestroyNotify. The stock removal
// remains FIRST; checkpoint fast-removals retain the native cached sums/bounds.
void TerrorNavMesh::RemoveNavArea( CNavArea *area )
{
	BaseClass::RemoveNavArea( area );
	FOR_EACH_VEC( m_checkpoints, i ) m_checkpoints[i]->OnAreaRemoved( ToTerrorNavArea( area ) );
}

// S2F2710. SDK EndVisibilityComputations previously performed this compression
// too early. Its L4D2 phase is now split to the native PostCustomAnalysis seam.
void TerrorNavMesh::PostCustomAnalysis()
{
	BaseClass::PostCustomAnalysis();
	ApplyNavAttributeRegions();
	FOR_EACH_VEC( TheNavAreas, i )
		ToTerrorNavArea( TheNavAreas[i] )->RemoveSpawnAttributes( TERROR_NAV_RESCUE_CLOSET );
	CBaseEntity *entity = NULL;
	while ( ( entity = gEntList.FindEntityByClassname( entity, "info_survivor_rescue" ) ) != NULL )
	{
		CNavArea *seed = GetNearestNavArea( entity->GetAbsOrigin(), false, 350.0f, true, true, TEAM_ANY, true );
		CUtlVector< CNavArea * > areas;
		// The native analysis caller never reads its door-output vector.
		CollectSurvivorRescueAreas( seed, 300.0f, areas, NULL );
		FOR_EACH_VEC( areas, j ) ToTerrorNavArea( areas[j] )->AddSpawnAttributes( TERROR_NAV_RESCUE_CLOSET );
	}
	struct VisibilityAddressLess
	{
		static int Compare( const CNavArea::AreaBindInfo *first, const CNavArea::AreaBindInfo *second )
		{
			if ( first->area == second->area ) return 0;
			// Original2E49B0 orders original area addresses (difference/384).
			// Compare addresses without an x86 stride or signed overflow.
			return reinterpret_cast< uintp >( first->area ) < reinterpret_cast< uintp >( second->area ) ? -1 : 1;
		}
	};
	FOR_EACH_VEC( TheNavAreas, i )
	{
		TerrorNavArea *area = ToTerrorNavArea( TheNavAreas[i] );
		if ( area->HasNodes() ) area->RemoveSpawnAttributes( TERROR_NAV_RESCUE_CLOSET );
		area->m_potentiallyVisibleAreas.Sort( VisibilityAddressLess::Compare );
	}
	struct GeneratedCheckpointCollector
	{
		bool operator()( CNavArea *area ) const
		{
			if ( area->HasNodes() ) ToTerrorNavArea( area )->AddSpawnAttributes( TERROR_NAV_CHECKPOINT );
			return true;
		}
	} mark;
	entity = NULL;
	while ( ( entity = gEntList.FindEntityByClassname( entity, "info_changelevel" ) ) != NULL )
	{
		Extent bounds;
		entity->CollisionProp()->WorldSpaceAABB( &bounds.lo, &bounds.hi );
		ForAllAreasOverlappingExtent( mark, bounds );
	}
}

bool TerrorNavArea::IsCompletelyVisible( const CNavArea *viewedArea ) const
{
	if ( !viewedArea ) return false;
	if ( viewedArea == this ) return true;
	const TerrorNavArea *viewed = ToTerrorNavArea( viewedArea );
	if ( viewed->HasSpawnAttributes( TERROR_NAV_OBSCURED ) )
	{
		extern ConVar nav_obscure_range;
		const Vector &from = GetCenter();
		const Vector &to = viewed->GetCenter();
		float x = to.x - from.x, y = to.y - from.y, z = to.z - from.z;
		float maximum = nav_obscure_range.GetFloat();
		if ( maximum * maximum <= ( y * y + x * x ) + z * z ) return false;
	}
	return BaseClass::IsCompletelyVisible( viewedArea );
}

bool TerrorNavArea::IsPotentiallyVisible( const CNavArea *viewedArea ) const
{
	return IsPotentiallyVisible( viewedArea, false );
}

bool TerrorNavArea::IsPotentiallyVisible( const CNavArea *viewedArea, bool ignoreObscuredRange ) const
{
	if ( !viewedArea ) return false;
	if ( viewedArea == this ) return true;
	const TerrorNavArea *viewed = ToTerrorNavArea( viewedArea );
	if ( !ignoreObscuredRange && viewed->HasSpawnAttributes( TERROR_NAV_OBSCURED ) )
	{
		extern ConVar nav_obscure_range;
		const Vector &from = GetCenter();
		const Vector &to = viewed->GetCenter();
		float x = to.x - from.x, y = to.y - from.y, z = to.z - from.z;
		float maximum = nav_obscure_range.GetFloat();
		if ( maximum * maximum <= ( y * y + x * x ) + z * z ) return false;
	}
	return BaseClass::IsPotentiallyVisible( viewedArea );
}

bool CTerrorCheckpoint::HasLockedDoor() const
{
	FOR_EACH_VEC( m_doors, i )
	{
		CBasePropDoor *door = m_doors[i].Get();
		if ( door && door->IsDoorLocked() ) return true;
	}
	return false;
}

bool TerrorNavArea::IsCompletelyVisibleToTeam( int teamIndex ) const
{
	extern ConVar nav_obscure_range;
	CTeam *team = GetGlobalTeam( teamIndex );
	for ( int i = 0; i < team->GetNumPlayers(); ++i )
	{
		CBasePlayer *player = team->GetPlayer( i );
		if ( !player->IsAlive() ) continue;
		CNavArea *area = player->GetLastKnownArea();
		if ( !area ) continue;
		if ( area == this ) return true;
		if ( HasSpawnAttributes( TERROR_NAV_OBSCURED ) &&
			!( Sqr( nav_obscure_range.GetFloat() ) > GetCenter().DistToSqr( area->GetCenter() ) ) ) continue;
		if ( area->IsCompletelyVisible( this ) ) return true;
	}
	return false;
}

void TerrorNavMesh::SelectRescueVehicleArea( const CUtlVector< TerrorNavArea * > &rescueAreas )
{
	m_rescueVehicleArea = NULL;
	int bestDepth = -1;
	FOR_EACH_VEC( rescueAreas, i )
	{
		struct RescueBoundaryCollector
		{
			int depth;
			bool operator()( TerrorNavArea *area )
			{
				if ( area->HasSpawnAttributes( TERROR_NAV_RESCUE_VEHICLE ) ) return true;
				CNavArea *parent = area->GetParent();
				int count = 0;
				for ( ; parent && count < 16; ++count ) parent = parent->GetParent();
				if ( count > depth ) depth = count;
				return false;
			}
		} collector = { 0 };
		TerrorNavArea *seed = rescueAreas[i];
		CollectSurroundingTerrorAreas( seed, seed->GetCenter(), collector, -1.0f, 3, TEAM_ANY );
		if ( collector.depth > bestDepth )
		{
			bestDepth = collector.depth;
			m_rescueVehicleArea = seed;
		}
	}
}
#ifdef _MSC_VER
#pragma float_control( pop )
#endif
