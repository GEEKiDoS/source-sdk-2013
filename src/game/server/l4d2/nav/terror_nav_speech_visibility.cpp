//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/nav/terror_nav_area.h"
#include "l4d2/nav/terror_nav.h"
#include "l4d2/nav/terror_nav_world.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/entities/info_changelevel.h"
#include "util_shared.h"
#include "tier0/memdbgon.h"

extern ConVar nav_potentially_visible_dot_tolerance;

// S2E32C0. Do not substitute CNavArea::IsVisible: the original skips close
// corner rays and, on the corner trace only, adds the eye height TWICE.
bool TerrorNavArea::IsVisibleFrom( const Vector &eye ) const
{
	CTraceFilterNoNPCsOrPlayer filter( NULL, COLLISION_GROUP_NONE );
	trace_t trace;
	Vector center = GetCenter() + Vector( 0, 0, 53.25f );
	UTIL_TraceLine( eye, center, MASK_BLOCKLOS_AND_NPCS | CONTENTS_IGNORE_NODRAW_OPAQUE, &filter, &trace );
	if ( trace.fraction >= 1.0f )
		return true;
	Vector toCenter = center - eye;
	VectorNormalize( toCenter );
	float tolerance = nav_potentially_visible_dot_tolerance.GetFloat();
	for ( int i = 0; i < NUM_CORNERS; ++i )
	{
		Vector corner = GetCorner( static_cast< NavCornerType >( i ) ) + Vector( 0, 0, 53.25f );
		Vector toCorner = corner - eye;
		VectorNormalize( toCorner );
		if ( DotProduct( toCorner, toCenter ) < tolerance )
		{
			corner.z += 53.25f;
			UTIL_TraceLine( eye, corner, MASK_BLOCKLOS_AND_NPCS | CONTENTS_IGNORE_NODRAW_OPAQUE, &filter, &trace );
			if ( trace.fraction >= 1.0f )
				return true;
		}
	}
	return false;
}

// S257A20. The first checkpoint is accepted even if outward; later outward
// checkpoints cannot replace it. Do not repair that native selection bug.
CTerrorCheckpoint *TerrorNavMesh::GetTransitionCheckpoint() const
{
	if ( !TheDirector->IsChangingMapOrMission() )
		return NULL;
	const char *name = GetTerrorTransitionLandmarkName();
	if ( !name || !name[0] )
		return NULL;
	CBaseEntity *landmark = gEntList.FindEntityByName( NULL, name );
	if ( !landmark )
	{
		DevWarning( "************\nMost gross danger! Cannot find Landmark named %s!\n************", name );
		return NULL;
	}
	const Vector &origin = landmark->WorldSpaceCenter();
	CTerrorCheckpoint *selected = NULL;
	float bestDistance = 0.0f;
	FOR_EACH_VEC( m_checkpoints, i )
	{
		CTerrorCheckpoint *checkpoint = m_checkpoints[i];
		float distance = origin.DistToSqr( checkpoint->GetCenter() );
		if ( !selected || ( bestDistance > distance && !checkpoint->IsOutward() ) )
		{
			selected = checkpoint;
			bestDistance = distance;
		}
	}
	return selected;
}
