//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/nav/terror_nav.h"
#include "l4d2/nav/terror_nav_search.h"
#include "ndebugoverlay.h"
#include "world.h"
#include <xmmintrin.h>
#include "tier0/memdbgon.h"

extern ConVar nav_trouble_test_area;
extern ConVar nav_trouble_test_debug_duration;
extern ConVar nav_trouble_walkable_backtrace;
extern ConVar nav_flood_select_tolerance;
#pragma float_control( precise, on, push )

namespace
{
// S10852250 is temporary command state, not a persistent user selection.
int s_troubleMarkedID;
const Vector s_troubleMins( -2.5f, -2.5f, 0.0f );
const Vector s_troubleMaxs( 2.5f, 2.5f, 55.0f );
const Vector s_walkMaxs( 2.5f, 2.5f, 17.5f );

struct TroubleAreas
{
	CUtlVector< CNavArea * > areas;
	void Add( CNavArea *area )
	{
		if ( areas.Find( area ) == areas.InvalidIndex() ) areas.AddToTail( area );
	}
};

bool TestTroubleArea( const CNavArea *area )
{
	if ( s_troubleMarkedID > 0 ) return static_cast< unsigned int >( s_troubleMarkedID ) == area->GetID();
	int id = nav_trouble_test_area.GetInt();
	return !id || static_cast< unsigned int >( id ) == area->GetID();
}

bool DrawTroubleArea( const CNavArea *area )
{
	return ( static_cast< unsigned int >( s_troubleMarkedID ) == area->GetID() || static_cast< unsigned int >( nav_trouble_test_area.GetInt() ) == area->GetID() ) && nav_trouble_test_debug_duration.GetFloat() > 0.0f;
}

void TroubleSweptBox( const CNavArea *area, const Vector &from, const Vector &to, const Vector &mins, const Vector &maxs, int r, int g, int b )
{
	if ( DrawTroubleArea( area ) ) NDebugOverlay::SweptBox( from, to, mins, maxs, vec3_angle, r, g, b, 0, nav_trouble_test_debug_duration.GetFloat() );
}
void TroubleBox( const CNavArea *area, const Vector &origin, const Vector &mins, const Vector &maxs, int r, int g, int b )
{
	if ( DrawTroubleArea( area ) ) NDebugOverlay::Box( origin, mins, maxs, r, g, b, 0, nav_trouble_test_debug_duration.GetFloat() );
}

void TroubleResult( const char *name, int before, int after, bool ladders = false )
{
	if ( after <= before ) Msg( "%s passed\n", name );
	else if ( ladders ) Warning( "%s found %d ladders\n", name, after - before );
	else Warning( "%s found %d areas\n", name, after - before );
}

void AddLadder( CUtlVector< CNavLadder * > &list, CNavLadder *ladder )
{
	if ( list.Find( ladder ) == list.InvalidIndex() ) list.AddToTail( ladder );
}

void MovePortalOutward( Vector *point, NavDirType direction, float amount )
{
	switch ( direction )
	{
	case NORTH: point->y -= amount; break;
	case EAST: point->x += amount; break;
	case SOUTH: point->y += amount; break;
	case WEST: point->x -= amount; break;
	default: break;
	}
}


// Exact S2E3710 selection/traversal predicate. Light-intensity bounds are not slope tests.
struct TroubleFlood
{
	TerrorNavMesh *mesh;
	TerrorNavArea *start;
	unsigned int required;
	unsigned int excluded;
	unsigned int population;
	float minLight;
	float maxLight;
	float radius;
	int count;
	bool operator()( TerrorNavArea *area )
	{
		if ( mesh->IsInSelectedSet( area ) ) return false;
		if ( population != 0xFFFF && area->GetPopulationIndex() != population ) return false;
		if ( required && !area->HasSpawnAttributes( required ) ) return false;
		if ( excluded && area->HasSpawnAttributes( excluded ) ) return false;
		if ( radius > 0.0f && start->GetCenter().DistToSqr( area->GetCenter() ) > radius * radius ) return false;
		int tolerance = nav_flood_select_tolerance.GetInt();
		if ( tolerance > 0 )
		{
			int height = _mm_cvtt_ss2si( _mm_set_ss( area->GetCenter().z ) );
			int low = _mm_cvtt_ss2si( _mm_set_ss( start->GetCenter().z - float( tolerance ) ) );
			int high = _mm_cvtt_ss2si( _mm_set_ss( start->GetCenter().z + float( tolerance ) ) );
			if ( height < low || height > high ) return false;
		}
		for ( int corner = 0; corner < NUM_CORNERS; ++corner )
		{
			double light = area->GetLightIntensity( area->GetCorner( static_cast< NavCornerType >( corner ) ) );
			if ( minLight > light ) return false;
			float storedLight = light;
			if ( storedLight > maxLight ) return false;
		}
		mesh->AddToSelectedSet( area );
		++count;
		return true;
	}
};


void CollectUnselectedTrouble( TerrorNavMesh *mesh, TroubleAreas *result, bool excludeFinale = false )
{
	FOR_EACH_VEC( TheNavAreas, i )
	{
		TerrorNavArea *area = ToTerrorNavArea( TheNavAreas[i] );
		if ( !mesh->IsInSelectedSet( area ) && ( !excludeFinale || !area->HasSpawnAttributes( 0x40 ) ) && TestTroubleArea( area ) ) result->Add( area );
	}
}

void CheckCornerConnections( TroubleAreas *result )
{
	int before = result->areas.Count();
	FOR_EACH_VEC( TheNavAreas, i )
	{
		CNavArea *area = TheNavAreas[i];
		if ( !TestTroubleArea( area ) ) continue;
		for ( int d = 0; d < NUM_DIRECTIONS; ++d )
		{
			const NavConnectVector *links = area->GetAdjacentAreas( static_cast< NavDirType >( d ) );
			FOR_EACH_VEC( ( *links ), j )
			{
				CNavArea *other = ( *links )[j].area;
				if ( !other ) continue;
				Vector portal; float width;
				area->ComputePortal( other, static_cast< NavDirType >( d ), &portal, &width );
				if ( width < 1.0f ) { result->Add( area ); result->Add( other ); }
			}
		}
	}
	TroubleResult( "Corner-to-corner connection check", before, result->areas.Count() );
}

void CheckStairClimbs( TroubleAreas *result )
{
	int before = result->areas.Count();
	FOR_EACH_VEC( TheNavAreas, i )
	{
		CNavArea *area = TheNavAreas[i];
		if ( !area->HasAttributes( 0x1000 ) || !TestTroubleArea( area ) ) continue;
		for ( int d = 0; d < NUM_DIRECTIONS; ++d )
		{
			const NavConnectVector *links = area->GetAdjacentAreas( static_cast< NavDirType >( d ) );
			FOR_EACH_VEC( ( *links ), j )
			{
				CNavArea *other = ( *links )[j].area;
				if ( !other ) continue;
				Vector portal; float width;
				area->ComputePortal( other, static_cast< NavDirType >( d ), &portal, &width );
				if ( !( width >= 1.0f ) ) continue;
				Vector left = portal, right = portal;
				if ( d == NORTH || d == SOUTH ) { left.x += width; right.x -= width; }
				else { left.y -= width; right.y += width; }
				Vector closeLeft, closeRight;
				other->GetClosestPointOnArea( left, &closeLeft );
				other->GetClosestPointOnArea( right, &closeRight );
				if ( closeLeft.z > left.z + 18.0f || closeRight.z > right.z + 18.0f ) result->Add( area );
			}
		}
	}
	TroubleResult( "Climb links from stairs check", before, result->areas.Count() );
}

void CheckOverlappingConnections( TroubleAreas *result )
{
	int before = result->areas.Count();
	FOR_EACH_VEC( TheNavAreas, i )
	{
		CNavArea *area = TheNavAreas[i];
		if ( !TestTroubleArea( area ) ) continue;
		Extent source;
		area->GetExtent( &source );
		for ( int d = 0; d < NUM_DIRECTIONS; ++d )
		{
			const NavConnectVector *links = area->GetAdjacentAreas( static_cast< NavDirType >( d ) );
			FOR_EACH_VEC( ( *links ), j )
			{
				CNavArea *other = ( *links )[j].area;
				if ( !other ) continue;
				Extent target;
				other->GetExtent( &target );
				// Native BUG: source is shrunk AGAIN for every non-NULL link and is not reset per direction.
				source.lo.x += 1.0f; source.lo.y += 1.0f;
				source.hi.x -= 1.0f; source.hi.y -= 1.0f;
				target.lo.x += 1.0f; target.lo.y += 1.0f;
				target.hi.x -= 1.0f; target.hi.y -= 1.0f;
				target.lo.z = source.lo.z; target.hi.z = source.hi.z;
				if ( target.hi.x >= source.lo.x && source.hi.x >= target.lo.x && target.hi.y >= source.lo.y && source.hi.y >= target.lo.y && source.hi.z >= source.lo.z && source.hi.z >= target.lo.z )
				{ result->Add( area ); result->Add( other ); }
			}
		}
	}
	TroubleResult( "Connection to overlapping area check", before, result->areas.Count() );
}

void CheckLongHorizontalConnections( TroubleAreas *result )
{
	int before = result->areas.Count();
	CTraceFilterWalkableEntities filter( NULL, COLLISION_GROUP_NONE, 15 );
	FOR_EACH_VEC( TheNavAreas, i )
	{
		CNavArea *area = TheNavAreas[i];
		if ( !TestTroubleArea( area ) ) continue;
		for ( int d = 0; d < NUM_DIRECTIONS; ++d )
		{
			const NavConnectVector *links = area->GetAdjacentAreas( static_cast< NavDirType >( d ) );
			FOR_EACH_VEC( ( *links ), j )
			{
				CNavArea *other = ( *links )[j].area;
				if ( !other ) continue;
				Vector from, to; float width;
				area->ComputePortal( other, static_cast< NavDirType >( d ), &from, &width );
				if ( !( width >= 1.0f ) ) continue;
				other->ComputePortal( area, OppositeDirection( static_cast< NavDirType >( d ) ), &to, &width );
				if ( fabs( from.z - to.z ) <= 5.0f && ( from.y - to.y ) * ( from.y - to.y ) + ( from.x - to.x ) * ( from.x - to.x ) >= 625.0f )
				{
					Vector start = ( from + to ) * 0.5f, end = start;
					end.z -= 41.8f;
					trace_t trace;
					UTIL_TraceHull( start, end, s_troubleMins, s_troubleMaxs, 147979, &filter, &trace );
					if ( trace.startsolid || trace.fraction < 1.0f ) { result->Add( area ); result->Add( other ); }
				}
			}
		}
	}
	TroubleResult( "Long horizontal connection check", before, result->areas.Count() );
}

void CheckVerticalConnections( TroubleAreas *result, bool oneWay )
{
	int before = result->areas.Count();
	FOR_EACH_VEC( TheNavAreas, i )
	{
		TerrorNavArea *area = ToTerrorNavArea( TheNavAreas[i] );
		if ( !TestTroubleArea( area ) ) continue;
		for ( int d = 0; d < NUM_DIRECTIONS; ++d )
		{
			const NavConnectVector *links = area->GetAdjacentAreas( static_cast< NavDirType >( d ) );
			FOR_EACH_VEC( ( *links ), j )
			{
				TerrorNavArea *other = ToTerrorNavArea( ( *links )[j].area );
				if ( !other ) continue;
				if ( oneWay && other->IsConnected( area, OppositeDirection( static_cast< NavDirType >( d ) ) ) ) continue;
				float forward = area->ComputeSurfaceHeightChange( other );
				// S2F668A / S2F680B pass area as BOTH operands. Do not repair to other->area.
				double reverse = -area->ComputeSurfaceHeightChange( area );
				if ( oneWay ? ( fabs( forward ) <= 32.0 && fabs( reverse ) <= 32.0 ) : ( forward > 180.0f || float( reverse ) > 180.0f ) )
				{ result->Add( area ); result->Add( other ); }
			}
		}
	}
	TroubleResult( oneWay ? "Short vertical one way connection check" : "Long vertical connection check", before, result->areas.Count() );
}

bool IsNearNavBlocker( CNavArea *area, const CUtlVector< CBaseEntity * > &blockers )
{
	Extent extent;
	area->GetExtent( &extent );
	FOR_EACH_VEC( blockers, i )
	{
		Vector low, high;
		blockers[i]->CollisionProp()->WorldSpaceAABB( &low, &high );
		if ( high.x + 100.0f >= extent.lo.x && extent.hi.x >= low.x - 100.0f && high.y + 100.0f >= extent.lo.y && extent.hi.y >= low.y - 100.0f && high.z + 100.0f >= extent.lo.z && extent.hi.z >= low.z - 100.0f ) return true;
	}
	return false;
}
void CollectNavBlockers( CUtlVector< CBaseEntity * > *blockers )
{
	for ( CBaseEntity *entity = gEntList.FindEntityByClassname( NULL, "func_nav_blocker" ); entity; entity = gEntList.FindEntityByClassname( entity, "func_nav_blocker" ) ) blockers->AddToTail( entity );
}

void CheckFloatingAreas( TroubleAreas *result )
{
	int before = result->areas.Count();
	CUtlVector< CBaseEntity * > blockers;
	CollectNavBlockers( &blockers );
	CTraceFilterWalkableEntities filter( NULL, COLLISION_GROUP_PLAYER_MOVEMENT, 7 );
	FOR_EACH_VEC( TheNavAreas, i )
	{
		CNavArea *area = TheNavAreas[i];
		if ( !TestTroubleArea( area ) || area->IsDamaging() || area->IsBlocked( TEAM_ANY ) || area->HasAttributes( 0x30000 ) || IsNearNavBlocker( area, blockers ) ) continue;
		Vector from = area->GetCenter() + Vector( 0, 0, 18 ), to = from + Vector( 0, 0, 1000 );
		trace_t trace;
		UTIL_TraceHull( from, to, s_troubleMins, s_walkMaxs, 16395, &filter, &trace );
		if ( trace.startsolid )
		{
			TroubleSweptBox( area, from, to, s_troubleMins, s_walkMaxs, 255, 0, 0 );
			result->Add( area );
			continue;
		}
		from = trace.endpos;
		to.z = area->GetCenter().z + 18.0f;
		UTIL_TraceHull( from, to, s_troubleMins, s_walkMaxs, 16395, &filter, &trace );
		if ( trace.startsolid )
		{
			TroubleSweptBox( area, from, to, s_troubleMins, s_walkMaxs, 0, 255, 0 );
			result->Add( area );
			continue;
		}
		from = area->GetCenter() + Vector( 0, 0, 18 );
		to.z = area->GetCenter().z - 71.0f;
		UTIL_TraceHull( from, to, s_troubleMins, s_walkMaxs, 33702411, &filter, &trace );
		if ( trace.startsolid )
		{
			TroubleBox( area, from, s_troubleMins, s_walkMaxs, 255, 0, 0 );
			if ( trace.m_pEnt && trace.m_pEnt != GetWorldEntity() ) continue;
		}
		else if ( trace.fraction < 1.0f )
		{
			TroubleSweptBox( area, from, to, s_troubleMins, s_walkMaxs, 0, 0, 255 );
			continue;
		}
		else TroubleSweptBox( area, from, to, s_troubleMins, s_walkMaxs, 255, 0, 0 );
		result->Add( area );
	}
	TroubleResult( "Floating/interpenetrating world check", before, result->areas.Count() );
}

void CheckNonWalkableConnections( TroubleAreas *result )
{
	int before = result->areas.Count();
	CUtlVector< CBaseEntity * > blockers;
	CollectNavBlockers( &blockers );
	CUtlVector< CNavArea * > ignored;
	FOR_EACH_VEC( TheNavAreas, i )
	{
		CNavArea *area = TheNavAreas[i];
		if ( area->IsDamaging() || area->IsBlocked( TEAM_ANY ) || IsNearNavBlocker( area, blockers ) || area->HasAttributes( 0x30000 ) ) ignored.AddToTail( area );
	}
	CTraceFilterWalkableEntities filter( NULL, COLLISION_GROUP_PLAYER_MOVEMENT, 15 );
	FOR_EACH_VEC( TheNavAreas, i )
	{
		CNavArea *area = TheNavAreas[i];
		if ( !TestTroubleArea( area ) ) continue;
		bool ignoreFrom = ignored.Find( area ) != ignored.InvalidIndex();
		for ( int d = 0; d < NUM_DIRECTIONS; ++d )
		{
			NavDirType direction = static_cast< NavDirType >( d );
			const NavConnectVector *links = area->GetAdjacentAreas( direction );
			FOR_EACH_VEC( ( *links ), j )
			{
				CNavArea *other = ( *links )[j].area;
				if ( !other ) continue;
				bool ignoreTo = ignored.Find( other ) != ignored.InvalidIndex();
				if ( ignoreFrom && ignoreTo ) continue;
				Vector portal; float width;
				trace_t trace;
				if ( !ignoreFrom )
				{
					Vector from = area->GetCenter() + Vector( 0, 0, 18 );
					area->ComputePortal( other, direction, &portal, &width );
					Vector to = portal + Vector( 0, 0, 18 );
					MovePortalOutward( &to, OppositeDirection( direction ), 2.5f );
					UTIL_TraceHull( from, to, s_troubleMins, s_walkMaxs, 16395, &filter, &trace );
					TroubleSweptBox( area, from, to, s_troubleMins, s_walkMaxs, 0, 0, 255 );
					if ( trace.startsolid || trace.fraction < 1.0f ) result->Add( area );
					else
					{
						Vector reversePortal;
						other->ComputePortal( area, OppositeDirection( direction ), &reversePortal, &width );
						if ( portal.z > reversePortal.z + 18.0f )
						{
							from = to;
							MovePortalOutward( &to, direction, 32.0f );
							UTIL_TraceHull( from, to, s_troubleMins, s_walkMaxs, 16395, &filter, &trace );
							TroubleSweptBox( area, from, to, s_troubleMins, s_walkMaxs, 0, 0, 255 );
							if ( trace.startsolid || trace.fraction < 1.0f ) result->Add( area );
						}
					}
				}
				if ( !ignoreTo && nav_trouble_walkable_backtrace.GetBool() )
				{
					Vector from = other->GetCenter() + Vector( 0, 0, 18 );
					other->ComputePortal( area, OppositeDirection( direction ), &portal, &width );
					Vector to = portal + Vector( 0, 0, 18 );
					MovePortalOutward( &to, direction, 2.5f );
					UTIL_TraceHull( from, to, s_troubleMins, s_walkMaxs, 16395, &filter, &trace );
					TroubleSweptBox( area, from, to, s_troubleMins, s_walkMaxs, 0, 0, 255 );
					if ( trace.startsolid || trace.fraction < 1.0f ) result->Add( area );
				}
			}
		}
	}
	TroubleResult( "Areas with non-walkable connections check", before, result->areas.Count() );
}

void CheckDisconnectedLadders( TerrorNavMesh *mesh, CUtlVector< CNavLadder * > *result )
{
	int before = result->Count();
	NavLadderVector &ladders = mesh->GetLadders();
	FOR_EACH_VEC( ladders, i )
	{
		CNavLadder *ladder = ladders[i];
		bool top = ladder->m_topForwardArea || ladder->m_topLeftArea || ladder->m_topRightArea;
		bool down = false, up = false;
		FOR_EACH_VEC( TheNavAreas, j )
		{
			CNavArea *area = TheNavAreas[j];
			const NavLadderConnectVector *links = area->GetLadders( CNavLadder::LADDER_DOWN );
			FOR_EACH_VEC( ( *links ), k ) if ( ( *links )[k].ladder == ladder ) down = true;
			links = area->GetLadders( CNavLadder::LADDER_UP );
			FOR_EACH_VEC( ( *links ), k ) if ( ( *links )[k].ladder == ladder ) up = true;
		}
		if ( ( down && !ladder->m_bottomArea ) || ( up && !top ) || ( !up && !top && !ladder->m_bottomArea ) ) AddLadder( *result, ladder );
	}
	TroubleResult( "Disconnected ladder check", before, result->Count(), true );
}
void CheckOverlappingLadders( TerrorNavMesh *mesh, CUtlVector< CNavLadder * > *result )
{
	int before = result->Count();
	NavLadderVector &ladders = mesh->GetLadders();
	FOR_EACH_VEC( ladders, i )
	{
		for ( int j = i + 1; j < ladders.Count(); ++j )
		{
			if ( ladders[i]->m_top.DistToSqr( ladders[j]->m_top ) < 2500.0f && ladders[i]->m_bottom.DistToSqr( ladders[j]->m_bottom ) < 2500.0f )
			{ AddLadder( *result, ladders[i] ); AddLadder( *result, ladders[j] ); }
		}
	}
	TroubleResult( "Overlapping ladder check", before, result->Count(), true );
}
void CheckNonClimbableLadders( TerrorNavMesh *mesh, CUtlVector< CNavLadder * > *result )
{
	int before = result->Count();
	NavLadderVector &ladders = mesh->GetLadders();
	FOR_EACH_VEC( ladders, i )
	{
		CNavLadder *ladder = ladders[i];
		Vector low = ladder->m_bottom - Vector( 50, 50, 50 ), high = ladder->m_top + Vector( 50, 50, 50 );
		if ( !gEntList.FindEntityByClassnameWithin( NULL, "func_simpleladder", low, high ) && !gEntList.FindEntityByClassnameWithin( NULL, "func_nav_blocker", low, high ) ) AddLadder( *result, ladder );
	}
	TroubleResult( "Non-climbable ladder check", before, result->Count(), true );
}
}

int TerrorNavMesh::SelectNativeRegion( CNavArea *start, unsigned int required, unsigned int excluded,
	unsigned int population, float minLight, float maxLight, float radius )
{
	// The native caller's subsequent &start->center would be undefined on NULL.
	if ( !start ) return 0;
	TroubleFlood select = { this, ToTerrorNavArea( start ), required, excluded,
		population, minLight, maxLight, radius, 0 };
	CollectSurroundingTerrorAreas( start, start->GetCenter(), select, -1.0f, 3, TEAM_ANY );
	return select.count;
}

void TerrorNavMesh::CommandNavTroubleReport( unsigned int mask, bool suggestions )
{
	TroubleAreas result;
	CUtlVector< CNavLadder * > ladders;
	if ( !suggestions )
	{
		CNavArea *marked = GetMarkedArea();
		s_troubleMarkedID = marked ? marked->GetID() : 0;
	}
	if ( mask & 1 )
	{
		int before = result.areas.Count();
		if ( m_checkpoints.Count() )
		{
			ClearSelectedSet();
			if ( m_checkpoints[0] ) SelectNativeRegion( m_checkpoints[0]->GetLargestSpawnArea(), 0, 0 );
			CollectUnselectedTrouble( this, &result );
		}
		TroubleResult( "Orphan check", before, result.areas.Count() );
	}
	if ( mask & 2 )
	{
		int before = result.areas.Count();
		ClearSelectedSet();
		FOR_EACH_VEC( m_checkpoints, i ) if ( m_checkpoints[i] ) SelectNativeRegion( m_checkpoints[i]->GetLargestSpawnArea(), 0, 0x100 );
		FOR_EACH_VEC( TheNavAreas, i )
		{
			TerrorNavArea *area = ToTerrorNavArea( TheNavAreas[i] );
			if ( area->HasSpawnAttributes( 0x100 ) && area->GetFlowDistance() > -1.0f )
			{ SelectNativeRegion( area, 0x100, 0 ); break; }
		}
		CollectUnselectedTrouble( this, &result );
		TroubleResult( "Missing Battlefield check", before, result.areas.Count() );
	}
	if ( mask & 4 )
	{
		int before = result.areas.Count();
		ClearSelectedSet();
		FOR_EACH_VEC( m_checkpoints, i ) if ( m_checkpoints[i] ) SelectNativeRegion( m_checkpoints[i]->GetLargestSpawnArea(), 0, 0x40 );
		CollectUnselectedTrouble( this, &result, true );
		TroubleResult( "Missing Finale check", before, result.areas.Count() );
	}
	if ( mask & 8 )
	{
		int before = result.areas.Count();
		ClearSelectedSet();
		FOR_EACH_VEC( m_checkpoints, i ) if ( m_checkpoints[i] ) SelectNativeRegion( m_checkpoints[i]->GetLargestSpawnArea(), 0x800, 0 );
		TerrorNavArea *farthest = NULL;
		FOR_EACH_VEC( TheNavAreas, i )
		{
			TerrorNavArea *area = ToTerrorNavArea( TheNavAreas[i] );
			if ( !area->HasSpawnAttributes( 0x800 ) && area->GetFlowDistance() > 0.0f && ( !farthest || area->GetFlowDistance() > farthest->GetFlowDistance() ) ) farthest = area;
		}
		if ( farthest ) SelectNativeRegion( farthest, 0, 0x800 );
		CollectUnselectedTrouble( this, &result );
		TroubleResult( "Missing Checkpoint check", before, result.areas.Count() );
	}
	if ( mask & 0x10 )
	{
		int before = result.areas.Count();
		ClearSelectedSet();
		SelectOverlappingAreas select;
		ForAllAreas( select );
		FOR_EACH_VEC( TheNavAreas, i ) if ( IsInSelectedSet( TheNavAreas[i] ) && TestTroubleArea( TheNavAreas[i] ) ) result.Add( TheNavAreas[i] );
		TroubleResult( "Overlap check", before, result.areas.Count() );
	}
	if ( mask & 0x20 ) CheckCornerConnections( &result );
	if ( mask & 0x40 ) CheckStairClimbs( &result );
	if ( mask & 0x80 ) CheckOverlappingConnections( &result );
	if ( mask & 0x100 ) CheckLongHorizontalConnections( &result );
	if ( mask & 0x200 )
	{
		// S2F6CB0's local seen vector is empty and only appends INSIDE its found branch.
		// That branch is unreachable; native always reports passed, even on duplicate links.
		TroubleResult( "Double connections check", result.areas.Count(), result.areas.Count() );
	}
	if ( mask & 0x400 ) CheckFloatingAreas( &result );
	if ( mask & 0x800 ) CheckNonWalkableConnections( &result );
	if ( mask & 0x10000 ) CheckVerticalConnections( &result, false );
	if ( mask & 0x20000 ) CheckVerticalConnections( &result, true );
	if ( suggestions )
	{
		int before = result.areas.Count();
		FOR_EACH_VEC( TheNavAreas, i )
		{
			CNavArea *area = TheNavAreas[i];
			if ( TestTroubleArea( area ) && area->GetAvoidanceObstacleHeight() != 0.0f && area->GetAvoidanceObstacleHeight() > 41.8f && ( area->GetSizeX() > 250.0f || area->GetSizeY() > 250.0f ) ) result.Add( area );
		}
		TroubleResult( "Large obstruction check", before, result.areas.Count() );
		before = result.areas.Count();
		bool anyPlace = false;
		FOR_EACH_VEC( TheNavAreas, i ) if ( TestTroubleArea( TheNavAreas[i] ) && TheNavAreas[i]->GetPlace() != UNDEFINED_PLACE ) { anyPlace = true; break; }
		if ( anyPlace )
			FOR_EACH_VEC( TheNavAreas, i ) if ( TestTroubleArea( TheNavAreas[i] ) && TheNavAreas[i]->GetPlace() == UNDEFINED_PLACE ) result.Add( TheNavAreas[i] );
		// Native help calls this population, but S2F6BA0 actually reads base Place148, not population344.
		TroubleResult( "Missing population check", before, result.areas.Count() );
	}
	if ( mask & 0x1000 ) CheckDisconnectedLadders( this, &ladders );
	if ( mask & 0x2000 ) CheckOverlappingLadders( this, &ladders );
	if ( mask & 0x4000 ) CheckNonClimbableLadders( this, &ladders );
	if ( mask & 0x8000 )
	{
		int before = result.areas.Count();
		FOR_EACH_VEC( TheNavAreas, i )
			if ( TestTroubleArea( TheNavAreas[i] ) && ( TheNavAreas[i]->GetSizeX() < 1.0f || TheNavAreas[i]->GetSizeY() < 1.0f ) ) result.Add( TheNavAreas[i] );
		TroubleResult( "Tiny nav area check", before, result.areas.Count() );
	}
	if ( !suggestions ) s_troubleMarkedID = 0;
	ClearSelectedSet();
	FOR_EACH_VEC( result.areas, i ) AddToSelectedSet( result.areas[i] );
	if ( GetSelecteSetSize() || ladders.Count() )
	{
		Warning( "Selected %d trouble areas\n", GetSelecteSetSize() );
		FOR_EACH_VEC( ladders, i ) Warning( "Ladder %d isn't fully connected or overlaps another ladder\n", ladders[i]->GetID() );
	}
	else Msg( "No trouble areas selected\n" );
}
#pragma float_control( pop )
