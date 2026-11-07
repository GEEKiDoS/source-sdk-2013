//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "nav_mesh.h"
#include "nav_pathfind.h"
#include "l4d2/nav/terror_nav_path.h"
#include "tier0/memdbgon.h"

class CTerrorShortestPathCost
{
public:
	CTerrorShortestPathCost( CNavArea *start, CNavArea *goal, const Vector *startPosition, const Vector *goalPosition ) :
		m_pStart( start ), m_pGoal( goal ), m_pStartPosition( startPosition ), m_pGoalPosition( goalPosition ) {}
	float operator()( CNavArea *area, CNavArea *from, const CNavLadder *ladder, const CFuncElevator *elevator, float length )
	{
		// S2943A0 changes only floor-connection lengths at the actual endpoints.
		// Ladder/elevator traversal continues to use its real native owner.
		if ( from && !ladder && !elevator && ( from == m_pStart || area == m_pGoal ) )
		{
			const Vector &source = from == m_pStart && m_pStartPosition ? *m_pStartPosition : from->GetCenter();
			const Vector &destination = area == m_pGoal && m_pGoalPosition ? *m_pGoalPosition : area->GetCenter();
			const Vector delta = destination - source;
			length = sqrtf( ( delta.y * delta.y + delta.z * delta.z ) + delta.x * delta.x );
		}
		return m_Cost( area, from, ladder, elevator, length );
	}
private:
	CNavArea *m_pStart, *m_pGoal;
	const Vector *m_pStartPosition, *m_pGoalPosition;
	ShortestPathCost m_Cost;
};
bool BuildTerrorShortestPath( CNavArea *start, CNavArea *goal, CNavArea **closestArea,
	float maxPathLength, int teamID, bool ignoreNavBlockers, const Vector *startPosition, const Vector *goalPosition )
{
	CTerrorShortestPathCost cost( start, goal, startPosition, goalPosition );
	// Native -1 means either blocked-team bit; SDK expresses it as TEAM_ANY.
	if ( teamID == -1 ) teamID = TEAM_ANY;
	return NavAreaBuildPath( start, goal, goalPosition, cost, closestArea, maxPathLength, teamID, ignoreNavBlockers );
}

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1930
#pragma fp_contract( off )
#endif
#endif

float GetTerrorNavTravelDistance( CNavArea *start, CNavArea *goal, float maxPathLength )
{
	if ( !start || !goal ) return -1.0f;
	if ( start == goal ) return 0.0f;
	if ( !BuildTerrorShortestPath( start, goal, NULL, maxPathLength ) ) return -1.0f;
	float distance = 0.0f;
	for ( CNavArea *area = goal; area->GetParent(); area = area->GetParent() )
	{
		const Vector delta = area->GetCenter() - area->GetParent()->GetCenter();
		// S2B16CE..2B16F6: round the Y/Z pair, then X, then each accumulated edge.
		const float edgeLength = sqrtf( ( delta.y * delta.y + delta.z * delta.z ) + delta.x * delta.x );
		distance += edgeLength;
	}
	return distance;
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif
