//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "nav_mesh.h"
#include "nav_area.h"
#include "tier0/memdbgon.h"

// Original server 104A25B0. This is a directed two-dimensional walk across
// intersected portals, not a path search or a line-of-sight test.
bool CNavMesh::IsConnected( const Vector &from, const Vector &to ) const
{
	CNavArea *area = GetNavArea( from, 120.0f );
	if ( !area )
		return false;
	CNavArea *goal = GetNavArea( to, 120.0f );
	if ( !goal )
		return false;
	if ( area == goal )
		return true;

	const bool positiveX = to.x > from.x;
	const bool positiveY = to.y > from.y;
	const Vector sign( positiveX ? 1.0f : -1.0f, positiveY ? 1.0f : -1.0f, 0.0f );
	const int quadrant = positiveX ? ( positiveY ? 1 : 0 ) : ( positiveY ? 2 : 3 );
	const NavDirType firstDirection = static_cast< NavDirType >( quadrant );
	const NavDirType secondDirection = static_cast< NavDirType >( ( quadrant + 1 ) % NUM_DIRECTIONS );
	const NavCornerType corner = static_cast< NavCornerType >( ( quadrant + 1 ) % NUM_CORNERS );
	const Vector delta( to.x - from.x, to.y - from.y, 0.0f );
	const Vector reciprocal( delta.x == 0.0f ? 0.0f : 1.0f / delta.x,
							 delta.y == 0.0f ? 0.0f : 1.0f / delta.y, 0.0f );

	CNavArea::MakeNewMarker();
	for ( ;; )
	{
		area->Mark();
		const Vector offset = area->GetCorner( corner ) - from;
		const NavDirType direction = offset.x * delta.y - offset.y * delta.x > 0.0f ? secondDirection : firstDirection;
		const int axis = direction != EAST && direction != WEST ? 1 : 0;
		const int otherAxis = 1 - axis;
		if ( offset[axis] * sign[axis] > delta[axis] * sign[axis] )
			return false;

		const NavConnectVector *adjacent = area->GetAdjacentAreas( direction );
		bool crossed = false;
		FOR_EACH_VEC( ( *adjacent ), i )
		{
			CNavArea *next = ( *adjacent )[i].area;
			if ( next->IsMarked() )
				continue;

			NavCornerType first, second;
			GetCornerTypesInDirection( OppositeDirection( direction ), &first, &second );
			const Vector edgeStart = next->GetCorner( first );
			const Vector edgeEnd = next->GetCorner( second );
			const float intercept = ( ( edgeStart[axis] - from[axis] ) * reciprocal[axis] ) * delta[otherAxis] + from[otherAxis];
			if ( intercept < edgeStart[otherAxis] || edgeEnd[otherAxis] < intercept )
				continue;

			if ( next == goal )
				return true;
			area = next;
			crossed = true;
			break;
		}
		if ( !crossed )
			return false;
	}
}
