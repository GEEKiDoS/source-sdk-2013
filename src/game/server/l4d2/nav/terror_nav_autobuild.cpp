//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/nav/terror_nav.h"
#include "ndebugoverlay.h"
#include <xmmintrin.h>
#include "tier0/memdbgon.h"

extern ConVar nav_max_auto_area_size;
extern ConVar nav_create_place_on_ground;

#pragma float_control( precise, on, push )
namespace
{
// S481E20 / S482300 compare the actual contacted plane, not just the trace height.
void AutoFloorTrace( const Vector &point, CTraceFilterWalkableEntities *filter, trace_t *trace )
{
	UTIL_TraceLine( point, point - Vector( 0, 0, 4 ), 147979, filter, trace );
}
bool AutoSamePlane( const trace_t &trace, const Vector &normal, float distance )
{
	if ( !( 1.0f > trace.fraction || trace.startsolid || trace.allsolid ) ) return false;
	// Native JBE rejects unordered at BOTH plane gates; epsilon is float32 widened.
	if ( !( double( 0.000001f ) > fabs( double( trace.plane.dist ) - double( distance ) ) ) ) return false;
	double dot = double( trace.plane.normal.x ) * normal.x + double( trace.plane.normal.y ) * normal.y + double( trace.plane.normal.z ) * normal.z;
	return dot > 0.999;
}

bool ScanAutoAxis( const Vector &seed, int axis, Vector *low, Vector *high )
{
	*low = *high = seed;
	CTraceFilterWalkableEntities filter( NULL, COLLISION_GROUP_NONE, 15 );
	trace_t trace;
	AutoFloorTrace( seed, &filter, &trace );
	Vector normal = trace.plane.normal;
	float distance = trace.plane.dist;
	Vector point = seed;
	for ( int i = 1; i < 512; ++i )
	{
		point[axis] = seed[axis] - float( i ) * 2.0f;
		AutoFloorTrace( point, &filter, &trace );
		if ( !AutoSamePlane( trace, normal, distance ) ) break;
		( *low )[axis] = point[axis];
	}
	for ( int i = 1; i < 512; ++i )
	{
		point[axis] = seed[axis] + float( i ) * 2.0f;
		AutoFloorTrace( point, &filter, &trace );
		if ( !AutoSamePlane( trace, normal, distance ) ) break;
		( *high )[axis] = point[axis];
	}
	return ( *high )[axis] - ( *low )[axis] > 2.0f;
}

bool GrowAutoRectangle( int axis, int across, Vector *low, Vector *high )
{
	Vector seed = *low;
	CTraceFilterWalkableEntities filter( NULL, COLLISION_GROUP_NONE, 15 );
	trace_t trace;
	AutoFloorTrace( seed, &filter, &trace );
	Vector normal = trace.plane.normal;
	float distance = trace.plane.dist;
	Vector point = seed;
	for ( int side = 0; side < 2; ++side )
	{
		bool keepGoing = true;
		for ( int i = 1; i < 512 && keepGoing; ++i )
		{
			point[axis] = side ? seed[axis] + float( i ) * 2.0f : seed[axis] - float( i ) * 2.0f;
			for ( float along = ( *low )[across]; along <= ( *high )[across]; along += 2.0f )
			{
				point[across] = along;
				AutoFloorTrace( point, &filter, &trace );
				if ( !AutoSamePlane( trace, normal, distance ) ) { keepGoing = false; break; }
				// Native asymmetry: only the negative growth checks point-to-plane distance.
				if ( !side && ( trace.plane.normal.x * point.x + trace.plane.normal.y * point.y + trace.plane.normal.z * point.z ) - trace.plane.dist > 1.0f )
				{ keepGoing = false; break; }
			}
			if ( keepGoing )
			{
				if ( side ) ( *high )[axis] = point[axis];
				else ( *low )[axis] = point[axis];
			}
		}
	}
	return ( *high )[axis] - ( *low )[axis] > 2.0f;
}

bool IntersectsAutoBox( const Extent &left, const Extent &right )
{
	return left.lo.x <= right.hi.x && right.lo.x <= left.hi.x && left.lo.y <= right.hi.y && right.lo.y <= left.hi.y && left.lo.z <= right.hi.z && right.lo.z <= left.hi.z;
}

// S4861C0: append low/middle/high slabs in that order, then FastRemove the source.
void SliceAutoRectangles( CUtlVector< Extent > *rectangles, int axis, const Extent &cut )
{
	for ( int i = rectangles->Count() - 1; i >= 0; --i )
	{
		Extent original = ( *rectangles )[i];
		bool split = false;
		if ( cut.lo[axis] > original.lo[axis] )
		{
			Extent piece = original;
			piece.hi[axis] = original.hi[axis] > cut.lo[axis] ? cut.lo[axis] : original.hi[axis];
			rectangles->AddToTail( piece );
			split = true;
		}
		if ( cut.hi[axis] > original.lo[axis] && original.hi[axis] > cut.lo[axis] )
		{
			Extent piece = original;
			piece.lo[axis] = original.lo[axis] > cut.lo[axis] ? original.lo[axis] : cut.lo[axis];
			piece.hi[axis] = cut.hi[axis] <= original.hi[axis] ? cut.hi[axis] : original.hi[axis];
			rectangles->AddToTail( piece );
			split = true;
		}
		if ( original.hi[axis] > cut.hi[axis] )
		{
			Extent piece = original;
			piece.lo[axis] = cut.hi[axis] > original.lo[axis] ? cut.hi[axis] : original.lo[axis];
			rectangles->AddToTail( piece );
			split = true;
		}
		if ( split ) rectangles->FastRemove( i );
	}
}

void SubtractAutoBox( CUtlVector< Extent > *rectangles, const Extent &cut )
{
	for ( int i = rectangles->Count() - 1; i >= 0; --i )
	{
		if ( !IntersectsAutoBox( ( *rectangles )[i], cut ) ) continue;
		Extent original = ( *rectangles )[i];
		rectangles->FastRemove( i );
		CUtlVector< Extent > pieces;
		pieces.AddToTail( original );
		SliceAutoRectangles( &pieces, 0, cut );
		SliceAutoRectangles( &pieces, 1, cut );
		// This tail is absent from Hex-Rays's S488230 pseudocode, but present in ASM48826E..488486.
		for ( int j = pieces.Count() - 1; j >= 0; --j )
		{
			const Extent &piece = pieces[j];
			if ( piece.lo.x >= cut.lo.x && cut.hi.x >= piece.hi.x && piece.lo.y >= cut.lo.y && cut.hi.y >= piece.hi.y ) pieces.FastRemove( j );
		}
		rectangles->AddVectorToTail( pieces );
	}
}

bool CanMergeAutoBoxes( const Extent &left, const Extent &right )
{
	const double epsilon = double( 0.001f );
	if ( !( fabs( double( right.hi.z ) - right.lo.z ) <= epsilon ) || !( fabs( double( left.hi.z ) - left.lo.z ) <= epsilon ) ) return false;
	if ( fabs( double( right.hi.y ) - right.lo.y - ( double( left.hi.y ) - left.lo.y ) ) <= epsilon && fabs( double( right.lo.y ) - left.lo.y ) <= epsilon )
		if ( fabs( double( right.lo.x ) - left.hi.x ) <= epsilon || fabs( double( right.hi.x ) - left.lo.x ) <= epsilon ) return true;
	if ( !( fabs( double( right.hi.x ) - right.lo.x - ( double( left.hi.x ) - left.lo.x ) ) <= epsilon ) ) return false;
	if ( fabs( double( right.lo.x ) - left.lo.x ) <= epsilon )
		return fabs( double( right.lo.y ) - left.hi.y ) <= epsilon || fabs( double( right.hi.y ) - left.lo.y ) <= epsilon;
	return false;
}
void MergeAutoBoxes( CUtlVector< Extent > *rectangles )
{
	for ( int i = rectangles->Count() - 1; i >= 0; --i )
	{
		for ( int j = 0; j < i; ++j )
		{
			if ( !CanMergeAutoBoxes( ( *rectangles )[j], ( *rectangles )[i] ) ) continue;
			Extent other = ( *rectangles )[i];
			( *rectangles )[j].Encompass( other.lo );
			( *rectangles )[j].Encompass( other.hi );
			rectangles->FastRemove( i );
			break;
		}
	}
}

void SplitAutoBoxesToMaximum( CUtlVector< Extent > *rectangles, int axis, float maximum )
{
	for ( int i = rectangles->Count() - 1; i >= 0; --i )
	{
		if ( !( ( *rectangles )[i].hi[axis] - ( *rectangles )[i].lo[axis] > maximum ) ) continue;
		Extent remainder = ( *rectangles )[i];
		rectangles->FastRemove( i );
		double shortRemainder = double( maximum ) * 1.9;
		while ( remainder.hi[axis] - remainder.lo[axis] > maximum )
		{
			Extent piece = remainder;
			float next = remainder.lo[axis] + maximum;
			if ( shortRemainder > double( remainder.hi[axis] ) - remainder.lo[axis] ) next = ( remainder.hi[axis] + remainder.lo[axis] ) * 0.5f;
			piece.hi[axis] = next;
			remainder.lo[axis] = next;
			rectangles->AddToTail( piece );
		}
		rectangles->AddToTail( remainder );
	}
}
}

CNavArea *TerrorNavMesh::CreateAutoBuiltArea( const Vector &corner, const Vector &otherCorner )
{
	Vector end( otherCorner.x, otherCorner.y, corner.z );
	CNavArea *nearby = GetMarkedArea();
	if ( !nearby ) nearby = GetNearestNavArea( otherCorner + Vector( 0, 0, 35.5f ), false, 10000.0f, true, true );
	if ( !nearby ) nearby = GetNearestNavArea( end + Vector( 0, 0, 35.5f ), false, 10000.0f, true, true );
	if ( !nearby ) nearby = GetNearestNavArea( otherCorner, false, 10000.0f, false, true );
	if ( !nearby ) nearby = GetNearestNavArea( end, false, 10000.0f, false, true );
	CNavArea *created = CreateArea();
	created->Build( corner, end );
	if ( nearby ) created->InheritAttributes( nearby );
	created->m_debugid = TheNavAreas.AddToTail( created );
	AddNavArea( created );
	if ( nav_create_place_on_ground.GetBool() ) created->PlaceOnGround( NUM_CORNERS, 0.0f );
	CNavArea *marked = GetMarkedArea();
	if ( marked )
	{
		Extent bounds;
		marked->GetExtent( &bounds );
		if ( corner.x > bounds.hi.x && otherCorner.x > bounds.hi.x ) { marked->ConnectTo( created, EAST ); created->ConnectTo( marked, WEST ); }
		else if ( corner.x < bounds.lo.x && otherCorner.x < bounds.lo.x ) { marked->ConnectTo( created, WEST ); created->ConnectTo( marked, EAST ); }
		else if ( corner.y > bounds.hi.y && otherCorner.y > bounds.hi.y ) { marked->ConnectTo( created, SOUTH ); created->ConnectTo( marked, NORTH ); }
		else if ( corner.y < bounds.lo.y && otherCorner.y < bounds.lo.y ) { marked->ConnectTo( created, NORTH ); created->ConnectTo( marked, SOUTH ); }
		SetMarkedArea( created );
	}
	TheNavMesh->OnEditCreateNotify( created );
	return created;
}

void TerrorNavMesh::CommandNavAutoBuild()
{
	const Vector &cursor = GetEditCursorPosition();
	// Native CVTTSS2SI, including its INT_MIN result for non-finite/out-of-range input.
	Vector seed( float( _mm_cvtt_ss2si( _mm_set_ss( cursor.x * 2.0f ) ) ) * 0.5f,
		float( _mm_cvtt_ss2si( _mm_set_ss( cursor.y * 2.0f ) ) ) * 0.5f, cursor.z );
	CNavArea *existing = GetNavArea( seed, 4.0f );
	if ( existing )
	{
		Extent bounds;
		existing->GetExtent( &bounds );
		NDebugOverlay::Box( bounds.lo, vec3_origin, bounds.hi - bounds.lo, 255, 255, 0, 0, 10.0f );
		NDebugOverlay::Cross3D( seed, 8.0f, 255, 0, 0, false, 10.0f );
		return;
	}
	Extent xFirst, yFirst;
	float xSize = 0.0f, ySize = 0.0f;
	if ( ScanAutoAxis( seed, 0, &xFirst.lo, &xFirst.hi ) && GrowAutoRectangle( 1, 0, &xFirst.lo, &xFirst.hi ) )
		xSize = ( xFirst.hi.y - xFirst.lo.y ) * ( xFirst.hi.x - xFirst.lo.x );
	if ( ScanAutoAxis( seed, 1, &yFirst.lo, &yFirst.hi ) && GrowAutoRectangle( 0, 1, &yFirst.lo, &yFirst.hi ) )
		ySize = ( yFirst.hi.y - yFirst.lo.y ) * ( yFirst.hi.x - yFirst.lo.x );
	if ( !( ySize > xSize && ySize > 0.0f ) && !( xSize > 0.0f ) ) return;
	Extent chosen = ySize > xSize && ySize > 0.0f ? yFirst : xFirst;
	CUtlVector< CNavArea * > overlaps;
	CollectAreasOverlappingExtent( chosen, &overlaps );
	CUtlVector< Extent > rectangles;
	rectangles.AddToTail( chosen );
	FOR_EACH_VEC( overlaps, i )
	{
		Extent cut;
		overlaps[i]->GetExtent( &cut );
		cut.lo.z -= 0.1f;
		cut.hi.z += 0.1f;
		SubtractAutoBox( &rectangles, cut );
	}
	MergeAutoBoxes( &rectangles );
	float maximum = nav_max_auto_area_size.GetFloat();
	SplitAutoBoxesToMaximum( &rectangles, 0, maximum );
	SplitAutoBoxesToMaximum( &rectangles, 1, maximum );
	FOR_EACH_VEC( rectangles, i ) NDebugOverlay::Box( rectangles[i].lo, vec3_origin, rectangles[i].hi - rectangles[i].lo, 0, 255, 0, 0, 10.0f );
	FOR_EACH_VEC( rectangles, i ) AddToSelectedSet( CreateAutoBuiltArea( rectangles[i].lo, rectangles[i].hi ) );
}
#pragma float_control( pop )
