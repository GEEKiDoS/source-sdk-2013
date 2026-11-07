//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/nav/terror_nav.h"
#include "l4d2/nav/terror_nav_world.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/director/zombiemanager.h"
#include "l4d2/nav/escape_route.h"
#include "world.h"
#include "props.h"
#include "func_break.h"
#include "entitylist.h"
#include "SharedFunctorUtils.h"
#include "functorutils.h"
#include "tier1/utlstring.h"
#include "tier0/vprof.h"
#include "ndebugoverlay.h"
#include <float.h>
#include "tier0/memdbgon.h"

extern ConVar z_debug;
static ConVar z_spew_areas( "z_spew_areas", "0", 0x00004004 );
static ConVar nav_recompute_flow_optimization( "nav_recompute_flow_optimization", "1", 0x00004004 );

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif
// S:258220. Keep one native message, including indentation, and no fixed
// formatting limit. The real z_debug owner is ZombieManager, not this TU.
void FlowMessage( int indent, const char *format, ... )
{
	if ( !z_debug.GetBool() ) return;
	CUtlStringBuilder text;
	for ( int i = 0; i < indent; ++i ) text.Append( "  " );
	va_list args;
	va_start( args, format );
	text.VAppendFormat( format, args );
	va_end( args );
	Msg( "%s", text.String() );
}

namespace
{

// S:2E6280. Actual box queries of the two real breakable classes. This is
// NOT a nav blocker stand-in or a hull trace. Both outputs accumulate.
void GetBreakableNavAttributes( const Extent &extent, bool *tankOnly, bool *mobOnly )
{
	*tankOnly = *mobOnly = false;
	CBaseEntity *entity = NULL;
	while ( ( entity = gEntList.FindEntityByClassnameWithin( entity, "prop_physics", extent.lo, extent.hi ) ) != NULL )
	{
		CPhysicsProp *prop = dynamic_cast< CPhysicsProp * >( entity );
		if ( prop && prop->GetHealth() > 0 )
		{
			*mobOnly |= prop->HasSpawnFlags( 0x2000 );
			*tankOnly |= prop->HasSpawnFlags( 0x1000 );
		}
	}
	// Native retains the exhausted NULL iterator as this loop's initial start.
	while ( ( entity = gEntList.FindEntityByClassnameWithin( entity, "func_breakable", extent.lo, extent.hi ) ) != NULL )
	{
		CBreakable *brush = dynamic_cast< CBreakable * >( entity );
		if ( brush && brush->GetHealth() > 0 )
		{
			*mobOnly |= brush->HasSpawnFlags( 0x2000 );
			*tankOnly |= brush->HasSpawnFlags( 0x1000 );
		}
	}
}

// S:2E9CE0/2EC810/2EBA50/2E5E80. BOTH independent anchor queries are highest
// flow(flags0); do not repair the second into a lowest-flow survivor.
class FlowScrimmageSnapshot
{
public:
	FlowScrimmageSnapshot()
	{
		m_front = FindHighestFlowSurvivor( 0 );
		m_frontFlow = m_front ? (float)m_front->GetFlowDistance( 0 ) : 0.0f;
		m_rear = FindHighestFlowSurvivor( 0 );
		m_rearFlow = m_rear ? (float)m_rear->GetFlowDistance( 0 ) : 0.0f;
	}
	bool operator()( CBasePlayer *base )
	{
		CTerrorPlayer *player = ToTerrorPlayer( base );
		if ( player && !player->IsAlive() && player->GetTeamNumber() == TEAM_INFECTED )
		{
			double flow = player->GetLineOfScrimmageFlow();
			float storedFlow = (float)flow;
			if ( flow > 0.0 )
			{
				Entry entry = { player, storedFlow - m_frontFlow, storedFlow - m_rearFlow };
				m_players.AddToTail( entry );
			}
		}
		return true;
	}
	void Restore()
	{
		// Raw original player pointers and fresh NAV readings, no replacement
		// roster query, EHANDLE resolution, alive guard, or timer reset.
		m_frontFlow = m_front ? (float)m_front->GetFlowDistance( 0 ) : 0.0f;
		m_rearFlow = m_rear ? (float)m_rear->GetFlowDistance( 0 ) : 0.0f;
		FOR_EACH_VEC( m_players, i )
			m_players[i].m_player->UpdateLineOfScrimmage( m_players[i].m_frontOffset + m_frontFlow,
				m_players[i].m_rearOffset + m_rearFlow );
	}
private:
	struct Entry { CTerrorPlayer *m_player; float m_frontOffset; float m_rearOffset; };
	CTerrorPlayer *m_front;
	float m_frontFlow;
	CTerrorPlayer *m_rear;
	float m_rearFlow;
	CUtlVector< Entry > m_players;
};
}

// S:2EBAD0/2EBB60. The genuine existing CNavMesh virtual lifecycle owns both
// producers. Duplicates/NULL handles and the broken-NULL dereference survive.
void TerrorNavMesh::OnBreakableCreated( CBaseEntity *breakable )
{
	m_changedBreakables.AddToTail( breakable );
}

void TerrorNavMesh::OnBreakableBroken( CBaseEntity *broken )
{
	m_changedBreakables.FindAndRemove( broken );
	Extent extent;
	broken->CollisionProp()->WorldSpaceSurroundingBounds( &extent.lo, &extent.hi );
	extent.lo.z -= 55.0f;
	m_changedBreakableBounds.AddToTail( extent );
	if ( z_debug.GetBool() )
		NDebugOverlay::Box( vec3_origin, extent.lo, extent.hi, 0, 255, 0, 64, 5.0f );
}

// S:2EBC50, called first by2F2060 and last by2F50D0. Reuse the SDK's
// identical native NavAreaCollector(true)/ForAllAreasOverlappingExtent.
void TerrorNavMesh::UpdateBreakableNavAttributes()
{
	NavAreaCollector collect( true );
	FOR_EACH_VEC( m_changedBreakables, i )
	{
		CBaseEntity *entity = m_changedBreakables[i].Get();
		if ( !entity ) continue;
		Extent extent;
		entity->CollisionProp()->WorldSpaceSurroundingBounds( &extent.lo, &extent.hi );
		extent.lo.z -= 55.0f;
		ForAllAreasOverlappingExtent( collect, extent );
	}
	FOR_EACH_VEC( m_changedBreakableBounds, i )
		ForAllAreasOverlappingExtent( collect, m_changedBreakableBounds[i] );
	int changes = 0;
	FOR_EACH_VEC( collect.m_area, i )
	{
		CNavArea *area = collect.m_area[i];
		Extent extent;
		area->GetExtent( &extent );
		extent.hi.z += 55.0f;
		bool tankOnly, mobOnly;
		GetBreakableNavAttributes( extent, &tankOnly, &mobOnly );
		bool hasTank = area->HasAttributes( 0x10000 );
		if ( hasTank != tankOnly )
		{
			++changes;
			// Native enabling ASSIGNS the whole attribute word, not OR.
			if ( tankOnly ) area->SetAttributes( 0x10000 );
			else area->RemoveAttributes( 0x10000 );
			const Vector &center = area->GetCenter();
			FlowMessage( 1, "Area %d(%f %f %f) is %sNAV_TANK_ONLY\n", area->GetID(),
				(double)center.x, (double)center.y, (double)center.z, tankOnly ? "" : "not " );
			DevMsg( "Area %d is %sNAV_TANK_ONLY\n", area->GetID(), tankOnly ? "" : "not " );
		}
		// Read the attributes AFTER the first whole-word assignment.
		bool hasMob = area->HasAttributes( 0x20000 );
		if ( hasMob != mobOnly )
		{
			++changes;
			if ( mobOnly ) area->SetAttributes( 0x20000 );
			else area->RemoveAttributes( 0x20000 );
			const Vector &center = area->GetCenter();
			FlowMessage( 1, "Area %d(%f %f %f) is %sNAV_MOB_ONLY\n", area->GetID(),
				(double)center.x, (double)center.y, (double)center.z, mobOnly ? "" : "not " );
			DevMsg( "Area %d is %sNAV_MOB_ONLY\n", area->GetID(), mobOnly ? "" : "not " );
		}
	}
	m_changedBreakables.RemoveAll();
	m_changedBreakableBounds.RemoveAll();
	if ( changes > 0 && !nav_recompute_flow_optimization.GetBool() && !( m_flowRecomputeTimer.m_timestamp > 0.0f ) )
		m_flowRecomputeTimer.Start( 3.0f );
}

void TerrorNavMesh::UpdateFlowRecomputeTimer()
{
	if ( m_flowRecomputeTimer.m_timestamp > 0.0f &&
		(double)gpGlobals->curtime >= (double)m_flowRecomputeTimer.m_timestamp )
	{
		m_flowRecomputeTimer.Invalidate();
		RecomputeFlowDistances();
	}
}

// S:2EB900/2EDFF0. Original PC engine VEngineServer022 slot126 (+504),
// engine1319E0, is proven XOR AL,AL; RET. Its suppression branch is never
// taken on this pinned build. Do not invent its semantic name or interface.
void TerrorNavMesh::ShowFlowErrors( CTerrorPlayer *onlyPlayer ) const
{
	static ConVarRef developer( "developer" );
	if ( !Q_stricmp( STRING( gpGlobals->mapname ), "credits" ) || !developer.GetBool() ||
		( m_hasFlowDistances && !m_visibilityNeedsRebuild && IsAnalyzed() ) ) return;
	CUtlVector< CTerrorPlayer * > players;
	CollectTerrorPlayers( players, -1, 2, 3, 9 );
	FOR_EACH_VEC( players, i )
	{
		CTerrorPlayer *player = players[i];
		if ( player->IsBot() || ( onlyPlayer && player != onlyPlayer ) ) continue;
		KeyValues *data = new KeyValues( "data" );
		data->SetString( "res", "Resource/UI/NavErrorMessageBox.res" );
		player->ShowViewPortPanel( "message_box", true, data );
		data->deleteThis();
	}
}

// S:2EA300. Called after either executed flow pass fails, and after both
// succeed. Early spawn/goal failures do not produce this diagnostic scan.
void TerrorNavMesh::ReportUnconnectedFlowAreas() const
{
	if ( !z_spew_areas.GetBool() ) return;
	FlowMessage( 1, "Unconnected Area(s):\n" );
	FOR_EACH_VEC( TheNavAreas, i )
	{
		TerrorNavArea *area = ToTerrorNavArea( TheNavAreas[i] );
		if ( area && area->GetFlowDistance() == -9999.0f )
		{
			const Vector &center = area->GetCenter();
			FlowMessage( 2, "Area %d(%f %f %f)\n", area->GetID(),
				(double)center.x, (double)center.y, (double)center.z );
		}
	}
}

// S:2F2060. All three temporary vectors remain scoped to this computation;
// none impersonates the independent AAS boundary registries1568/1592.
TerrorNavArea *TerrorNavMesh::ComputeFlowDistances()
{
	VPROF_BUDGET( "TerrorNavMesh::ComputeFlowDistances", "ZOMBIE" );
	UpdateBreakableNavAttributes();
	CUtlVector< TerrorNavArea * > battleStations;
	CUtlVector< TerrorNavArea * > rescueAreas;
	CWorld *world = GetWorldEntity();
	Vector low, high;
	world->GetWorldBounds( low, high );
	FOR_EACH_VEC( TheNavAreas, i )
	{
		TerrorNavArea *area = ToTerrorNavArea( TheNavAreas[i] );
		if ( !area->IsBlocked( TEAM_ANY, true ) )
		{
			bool outside = area->HasAttributes( 0x10000000 );
			for ( int corner = 0; !outside && corner < NUM_CORNERS; ++corner )
				outside = !area->GetCorner( static_cast< NavCornerType >( corner ) ).WithinAABox( low, high );
			if ( outside ) area->MarkAsBlocked( TEAM_ANY, world, false );
		}
		area->m_flowDistance[FLOW_TOWARD_GOAL] = area->m_flowDistance[FLOW_FROM_START] = -9999.0f;
		area->RemoveSpawnAttributes( TERROR_NAV_ESCAPE_ROUTE );
		if ( area->HasSpawnAttributes( TERROR_NAV_BATTLESTATION ) ) battleStations.AddToTail( area );
		if ( area->HasSpawnAttributes( TERROR_NAV_RESCUE_VEHICLE ) ) rescueAreas.AddToTail( area );
	}
	m_maxFlowDistance = m_maxNonCheckpointFlowDistance = 0.0f; // Native after the complete per-area reset/block loop.
	if ( z_debug.GetBool() && z_spew_areas.GetBool() )
	{
		FlowMessage( 1, "Battle Stations = %d:\n", battleStations.Count() );
		FOR_EACH_VEC( battleStations, i )
		{
			TerrorNavArea *area = battleStations[i];
			if ( area ) FlowMessage( 2, "Station: %d(%f %f %f)\n", area->GetID(),
				(double)area->GetCenter().x, (double)area->GetCenter().y, (double)area->GetCenter().z );
		}
		FlowMessage( 1, "Rescue Vehicles = %d:\n", rescueAreas.Count() );
		FOR_EACH_VEC( rescueAreas, i )
		{
			TerrorNavArea *area = rescueAreas[i];
			if ( area ) FlowMessage( 2, "Vehicle: %d(%f %f %f)\n", area->GetID(),
				(double)area->GetCenter().x, (double)area->GetCenter().y, (double)area->GetCenter().z );
		}
	}
	// Main2026-10-23: only this caller-local diagnostic was uninitialized
	// when the real spawn selector returns false without writing its output.
	Vector position = vec3_origin;
	TerrorNavArea *start = NULL;
	bool spawned = TerrorGameRules()->GetPlayerSpawnPosition( SurvivorCharacter_Unknown, &position, NULL, &start );
	if ( !spawned || !start )
	{
		if ( !start ) FlowMessage( 1, "Flow Error: Failed to spawn player at (%f %f %f), NO starting area!\n",
			(double)position.x, (double)position.y, (double)position.z );
		else FlowMessage( 1, "Flow Error: Failed to spawn player at (%f %f %f), in area %d(%f %f %f)!\n",
			(double)position.x, (double)position.y, (double)position.z, start->GetID(),
			(double)start->GetCenter().x, (double)start->GetCenter().y, (double)start->GetCenter().z );
		return NULL;
	}
	FlowMessage( 1, "Player starting area = %d(%f %f %f)\n", start->GetID(),
		(double)start->GetCenter().x, (double)start->GetCenter().y, (double)start->GetCenter().z );
	m_goalArea = SelectFlowGoalArea();
	if ( !m_goalArea ) { FlowMessage( 1, "Flow Error: No goal area found!\n" ); return NULL; }
	if ( m_goalArea->IsBlocked( TEAM_ANY ) ) { FlowMessage( 1, "Flow Error: Goal area is blocked!\n" ); return NULL; }
	FlowMessage( 1, "Goal area = %d(%f %f %f)\n", m_goalArea->GetID(),
		(double)m_goalArea->GetCenter().x, (double)m_goalArea->GetCenter().y, (double)m_goalArea->GetCenter().z );
	bool reached = ComputeFlowDistance( start, m_goalArea, FLOW_TOWARD_GOAL );
	if ( reached ) reached = ComputeFlowDistance( start, m_goalArea, FLOW_FROM_START );
	ReportUnconnectedFlowAreas();
	if ( !reached ) return NULL;
	m_earliestBattleStationFlow = FLT_MAX;
	FOR_EACH_VEC( battleStations, i )
	{
		float flow = battleStations[i]->GetFlowDistance();
		if ( m_earliestBattleStationFlow > flow ) m_earliestBattleStationFlow = flow;
	}
	SelectRescueVehicleArea( rescueAreas );
	return start;
}

void TerrorNavMesh::InitializeFlowDistances()
{
	VPROF_BUDGET( "TerrorNavMesh::InitializeFlowDistances", "ZOMBIE" );
	FlowMessage( 0, "Initializing Flow Distances...\n" );
	TerrorNavArea *start = ComputeFlowDistances();
	m_hasFlowDistances = start != NULL;
	ShowFlowErrors( NULL );
	if ( m_hasFlowDistances ) TheZombieManager->OnFlowDistancesInitialized( start );
	else FlowMessage( 1, "Flow Error: No starting area found via flow!\n" );
	// Native Director nullsub20 is proven empty, not an omitted service.
	FlowMessage( 0, "...Flow done!\n" );
}

void TerrorNavMesh::RecomputeFlowDistances()
{
	VPROF_BUDGET( "TerrorNavMesh::RecomputeFlowDistances", "ZOMBIE" );
	float begin = (float)Plat_FloatTime();
	FlowMessage( 0, "Recomputing Flow Distance...\n" );
	FlowScrimmageSnapshot snapshot;
	ForEachPlayer( snapshot );
	TerrorNavArea *start = ComputeFlowDistances();
	m_hasFlowDistances = start != NULL;
	ShowFlowErrors( NULL );
	if ( m_hasFlowDistances )
	{
		TheZombieManager->OnFlowDistancesRecomputed( start );
		// Native Director nullsub21 is proven empty.
		snapshot.Restore();
		FlowMessage( 0, "...Flow done(%f ms)!\n", ( Plat_FloatTime() - (double)begin ) * 1000.0 );
	}
	else
	{
		FlowMessage( 1, "Flow Error: No starting area found via flow!\n" );
		FlowMessage( 0, "...Flow done!\n" );
	}
	// Original2E8DF0 destroys the SAME temporary record vector on both tails.
}

CON_COMMAND_F( nav_recompute_flow, "Recomputes flow distance", 0x00004004 )
{
	if ( args.ArgC() >= 2 ) TheTerrorNavMesh()->RecomputeFlowDistances();
	else TheTerrorNavMesh()->InitializeFlowDistances();
}

// S:2EA670, actual derived OnEditDestroyNotify vtable28. The original does
// NOT tailcall CNavMesh's editor cleanup or general player area removal.
void TerrorNavMesh::OnEditDestroyNotify( CNavArea *deadArea )
{
	TerrorNavArea *area = ToTerrorNavArea( deadArea );
	m_boundarySeeds.FindAndRemove( area ); // Native1568: ordered removal.
	m_boundarySpreadAreas.FindAndFastRemove( area ); //1592: first-match FastRemove.
	CUtlVector< CTerrorPlayer * > players;
	CollectTerrorPlayers( players, TEAM_SURVIVOR, 1 | 2 | 4, 3, 9 );
	FOR_EACH_VEC( players, i ) players[i]->OnNavAreaDestroyed( area );
	CEscapeRoute *route = static_cast< CEscapeRoute * >( gEntList.FindEntityByClassname( NULL, "escape_route" ) );
	if ( route ) route->OnNavAreaDestroyed( area ); // First map entity, not singleton.
	if ( m_startArea == area ) m_startArea = NULL;
	if ( m_rescueVehicleArea == area ) m_rescueVehicleArea = NULL;
	if ( m_goalArea == area ) m_goalArea = NULL;
}
#ifdef _MSC_VER
#pragma float_control( pop )
#endif

#include "l4d2/nav/nav_flow_blocker_callbacks.inc"
