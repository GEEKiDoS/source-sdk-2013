//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_NAV_SEARCH_H
#define TERROR_NAV_SEARCH_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/nav/terror_nav_area.h"
#include "tier1/utlvector.h"

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1930
#pragma fp_contract( off )
#endif
#endif

namespace TerrorNavSearch
{
struct Entry
{
	CNavArea *area;
	float cost;
};

// S:0x2594D0 / 0x258160. Equal insertions rise; equal children do not sink.
class Heap
{
public:
	void Add( CNavArea *area, CNavArea *parent, const Vector &origin, float travelLimit, float rangeSquared )
	{
		if ( !area || area->IsMarked() || area->IsOpen() ) return;
		area->Mark();
		area->SetParent( parent );
		float cost = 0.0f;
		if ( !( travelLimit <= 0.0f ) ) // Native COMISS(0,limit)/JB also takes the range path for unordered.
		{
			Vector low = area->GetCorner( NORTH_WEST );
			Vector high = area->GetCorner( SOUTH_EAST );
			float distanceSquared = 0.0f;
			if ( low.x > origin.x ) distanceSquared = ( low.x - origin.x ) * ( low.x - origin.x );
			else if ( origin.x > high.x ) distanceSquared = ( origin.x - high.x ) * ( origin.x - high.x );
			if ( low.y > origin.y ) distanceSquared += ( low.y - origin.y ) * ( low.y - origin.y );
			else if ( origin.y > high.y ) distanceSquared += ( origin.y - high.y ) * ( origin.y - high.y );
			if ( distanceSquared >= rangeSquared ) return;
			const Vector &from = parent->GetCenter();
			const Vector &to = area->GetCenter();
			float x = to.x - from.x, y = to.y - from.y, z = to.z - from.z;
			cost = parent->GetCostSoFar() + sqrtf( ( y * y + z * z ) + x * x );
			area->SetCostSoFar( cost );
			area->SetTotalCost( cost );
			if ( cost >= travelLimit ) return;
		}
		else
		{
			area->SetCostSoFar( 0.0f );
			area->SetTotalCost( 0.0f );
		}
		Entry entry = { area, cost };
		int child = m_entries.AddToTail( entry );
		while ( child )
		{
			int parentIndex = ( child + 1 ) / 2 - 1;
			if ( m_entries[child].cost > m_entries[parentIndex].cost ) break;
			V_swap( m_entries[child], m_entries[parentIndex] );
			child = parentIndex;
		}
		area->m_openMarker = CNavArea::m_masterMarker; // Native259550: publish after successful heap insertion; Pop retains it.
	}

	CNavArea *Pop()
	{
		if ( !m_entries.Count() ) return NULL;
		CNavArea *area = m_entries[0].area;
		m_entries.FastRemove( 0 );
		int count = m_entries.Count();
		int parent = 0;
		while ( parent < count / 2 )
		{
			int selected = parent;
			int left = parent * 2 + 1;
			int right = parent * 2 + 2;
			if ( left < count && m_entries[selected].cost > m_entries[left].cost ) selected = left;
			if ( right < count && m_entries[selected].cost > m_entries[right].cost ) selected = right;
			if ( selected == parent ) break;
			V_swap( m_entries[parent], m_entries[selected] );
			parent = selected;
		}
		return area;
	}
private:
	CUtlVectorFixedGrowable< Entry, 4096 > m_entries;
};

// S:0x450A90 / 0x330230. Shared native CNavArea traversal.
// options:1 incoming,2 skip blocked test,4 require reciprocal outgoing,
//8 exclude elevator links. Discovery is final even when admission rejects it.
template < typename Collector >
void Search( CNavArea *start, const Vector &origin, Collector &collector,
	float maxRange, unsigned int options, int team )
{
	if ( !start ) return;
	CNavArea::MakeNewMarker();
	CNavArea::ClearSearchLists();
	Heap heap;
	heap.Add( start, NULL, origin, 0.0f, 0.0f );
	float radiusSquared = maxRange;
	float travelLimit = maxRange;
	if ( maxRange > 0.0f )
	{
		radiusSquared = maxRange * maxRange;
		travelLimit = maxRange * 1.5f;
	}
	while ( CNavArea *area = heap.Pop() )
	{
		if ( !( options & 2 ) && area->IsBlocked( team ) ) continue;
		if ( !collector( area ) ) continue;
		for ( int direction = 0; direction < NUM_DIRECTIONS; ++direction )
		{
			const NavConnectVector *outgoing = area->GetAdjacentAreas( static_cast< NavDirType >( direction ) );
			FOR_EACH_VEC( ( *outgoing ), i )
			{
				CNavArea *candidate = ( *outgoing )[i].area;
				if ( ( options & 4 ) && !candidate->IsConnected( area, NUM_DIRECTIONS ) ) continue;
				heap.Add( candidate, area, origin, travelLimit, radiusSquared );
			}
		}
		if ( options & 1 )
		{
			for ( int direction = 0; direction < NUM_DIRECTIONS; ++direction )
			{
				const NavConnectVector *incoming = area->GetIncomingConnections( static_cast< NavDirType >( direction ) );
				FOR_EACH_VEC( ( *incoming ), i )
					heap.Add( ( *incoming )[i].area, area, origin, travelLimit, radiusSquared );
			}
		}
		const NavLadderConnectVector *ladders = area->GetLadders( CNavLadder::LADDER_UP );
		FOR_EACH_VEC( ( *ladders ), i )
		{
			CNavLadder *ladder = ( *ladders )[i].ladder;
			heap.Add( ladder->m_topForwardArea, area, origin, travelLimit, radiusSquared );
			heap.Add( ladder->m_topLeftArea, area, origin, travelLimit, radiusSquared );
			heap.Add( ladder->m_topRightArea, area, origin, travelLimit, radiusSquared );
		}
		ladders = area->GetLadders( CNavLadder::LADDER_DOWN );
		FOR_EACH_VEC( ( *ladders ), i )
			heap.Add( ( *ladders )[i].ladder->m_bottomArea, area, origin, travelLimit, radiusSquared );
		if ( !( options & 8 ) && area->HasAttributes( NAV_MESH_HAS_ELEVATOR ) )
		{
			const NavConnectVector &elevators = area->GetElevatorAreas();
			FOR_EACH_VEC( elevators, i )
				heap.Add( elevators[i].area, area, origin, travelLimit, radiusSquared );
		}
	}
}
}

// Keep the TerrorNavArea callback contract on the same native traversal as SearchSurroundingAreas.
template < typename Collector >
void CollectSurroundingTerrorAreas( CNavArea *start, const Vector &origin, Collector &collector,
	float maxRange = -1.0f, unsigned int options = 0, int team = TEAM_ANY )
{
	auto collect = [ &collector ]( CNavArea *area ) { return collector( ToTerrorNavArea( area ) ); };
	TerrorNavSearch::Search( start, origin, collect, maxRange, options, team );
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif

#endif // TERROR_NAV_SEARCH_H
