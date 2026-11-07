//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/nav/terror_nav_area.h"
#include "l4d2/nav/terror_nav_search.h"
#include "l4d2/terror_shareddefs.h"
#include "nav_ladder.h"
#include "tier0/memdbgon.h"

extern ConVar nav_flow_max_survivor_climb_height;
extern ConVar nav_flow_max_survivor_drop_height;
static ConVar director_threat_clear_radius( "director_threat_clear_radius", "500", 0x00004004, "Prevent wandering zombies within this radius of threats" );

// S:2EAEB0. Keep the two stages: enumerate connections, then compare flows.
// Inline capacity avoids the native temporary vector's ordinary heap traffic;
// it does not impose a connection limit or alter insertion/tie order.
TerrorNavArea *TerrorNavArea::GetNextHighestFlowConnectedArea( NavTraverseType *how ) const
{
	struct Connection
	{
		CNavArea *area;
		NavTraverseType how;
	};
	CUtlVectorFixedGrowable< Connection, 16 > connected;
	auto append = [&]( CNavArea *area, NavTraverseType travel )
	{
		Connection entry = { area, travel };
		connected.AddToTail( entry );
	};
	for ( int direction = 0; direction < NUM_DIRECTIONS; ++direction )
	{
		const NavConnectVector *adjacent = GetAdjacentAreas( static_cast< NavDirType >( direction ) );
		FOR_EACH_VEC( *adjacent, i )
		{
			CNavArea *area = ( *adjacent )[i].area;
			Vector from, to;
			GetClosestPointOnArea( area->GetCenter(), &from );
			area->GetClosestPointOnArea( GetCenter(), &to );
			float height = to.z - from.z;
			if ( height > nav_flow_max_survivor_climb_height.GetFloat() ||
				-height > nav_flow_max_survivor_drop_height.GetFloat() ) continue;
			append( area, static_cast< NavTraverseType >( direction ) );
		}
	}
	const NavLadderConnectVector *ladders = GetLadders( CNavLadder::LADDER_UP );
	FOR_EACH_VEC( *ladders, i )
	{
		const CNavLadder *ladder = ( *ladders )[i].ladder;
		if ( !ladder->IsUsableByTeam( TEAM_SURVIVOR ) ) continue;
		if ( ladder->m_topForwardArea ) append( ladder->m_topForwardArea, GO_LADDER_UP );
		if ( ladder->m_topLeftArea ) append( ladder->m_topLeftArea, GO_LADDER_UP );
		if ( ladder->m_topRightArea ) append( ladder->m_topRightArea, GO_LADDER_UP );
	}
	ladders = GetLadders( CNavLadder::LADDER_DOWN );
	FOR_EACH_VEC( *ladders, i )
	{
		const CNavLadder *ladder = ( *ladders )[i].ladder;
		if ( ladder->IsUsableByTeam( TEAM_SURVIVOR ) && ladder->m_bottomArea )
			append( ladder->m_bottomArea, GO_LADDER_DOWN );
	}
	if ( HasAttributes( NAV_MESH_HAS_ELEVATOR ) )
	{
		const NavConnectVector &elevators = GetElevatorAreas();
		FOR_EACH_VEC( elevators, i )
		{
			CNavArea *area = elevators[i].area;
			// Native 2EB32E writes 5 for a non-higher elevator connection,
			// not 8. Preserve the original ladder-down travel-code bug.
			append( area, area->GetCenter().z > GetCenter().z ? GO_ELEVATOR_UP : GO_LADDER_DOWN );
		}
	}
	TerrorNavArea *best = NULL;
	float bestFlow = -10000000.0f;
	FOR_EACH_VEC( connected, i )
	{
		TerrorNavArea *area = ToTerrorNavArea( connected[i].area );
		float flow = area->GetFlowDistance( FLOW_TOWARD_GOAL );
		if ( !( area->GetAttributes() & 0x70000 ) && flow > bestFlow )
		{
			best = area;
			bestFlow = flow;
			if ( how ) *how = connected[i].how;
		}
	}
	return best;
}

// S:272CC0: only population292 changes. This is NOT MarkAsCleared: that
// separate operation also changes character masks and two real timers.
void ClearWanderingPopulationAroundBoss( TerrorNavArea *area )
{
	if ( !area ) return; // Original272CC0 entry guard.
	struct ClearPopulation
	{
		bool operator()( TerrorNavArea *candidate ) const
		{
			candidate->SetWanderingPopulation( 0 );
			return true;
		}
	} clear;
	CollectSurroundingTerrorAreas( area, area->GetCenter(), clear,
		director_threat_clear_radius.GetFloat(), 0, TEAM_ANY );
}
