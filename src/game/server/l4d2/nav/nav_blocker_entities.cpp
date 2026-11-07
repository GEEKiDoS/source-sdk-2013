//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "nav_mesh.h"
#include "l4d2/nav/nav_blocker_entities.h"
#include "mathlib/vmatrix.h"
#include "tier0/memdbgon.h"

// The GapConsoleServer packet supplies this one actual console definition.
extern ConVar nav_show_connectionblockers;
LINK_ENTITY_TO_CLASS( func_nav_connection_blocker, CFuncNavConnectionBlocker );
LINK_ENTITY_TO_CLASS( script_nav_blocker, CScriptNavBlocker );
BEGIN_DATADESC( CScriptNavBlocker )
	DEFINE_KEYFIELD( m_vExtent, FIELD_VECTOR, "extent" ),
END_DATADESC()

ConVar nav_show_connectionblockers( "nav_show_connectionblockers", "0", 0x00004004, "Debug: Show connection blocker models." );

void CFuncNavConnectionBlocker::Spawn()
{
	// S48B440. No ordinary entity Spawn, registry, Enable/Disable or think.
	SetSolid( SOLID_BSP );
	AddSolidFlags( FSOLID_NOT_SOLID );
	SetMoveType( MOVETYPE_NONE );
	SetModel( STRING( GetModelName() ) );
	if ( !nav_show_connectionblockers.GetBool() )
		AddEffects( EF_NODRAW );
}

#pragma float_control( precise, on, push )
bool CFuncNavConnectionBlocker::IsBlocking( const Ray_t &ray )
{
	// S48B050. Explicit ClipRayToEntity sees the non-solid brush.
	trace_t trace;
	enginetrace->ClipRayToEntity( ray, MASK_ALL, this, &trace );
	return trace.startsolid || trace.fraction != 1.0f;
}
#pragma float_control( pop )

void CScriptNavBlocker::Spawn()
{
	// S48C880: transform this one signed corner, then abs each component.
	// This deliberately preserves cancellation; it is NOT the enclosing AABB
	// of a rotated box (do not replace with abs(rotation)*extent).
	BaseClass::Spawn();
	VMatrix transform;
	transform.SetupMatrixOrgAngles( vec3_origin, GetAbsAngles() );
	Vector transformed;
	transform.V3Mul( m_vExtent, transformed );
	m_vExtent.Init( fabsf( transformed.x ), fabsf( transformed.y ), fabsf( transformed.z ) );
	CollisionProp()->SetCollisionBounds( -m_vExtent, m_vExtent );
	CollisionProp()->WorldSpaceAABB( &m_CachedMins, &m_CachedMaxs );
}

void CNavMesh::CollectConnectionBlockers()
{
	// S49AE80: purge, then raw pointers in entity-list traversal order.
	m_connectionBlockers.Purge();
	for ( CBaseEntity *entity = gEntList.FindEntityByClassname( NULL, "func_nav_connection_blocker" );
		entity; entity = gEntList.FindEntityByClassname( entity, "func_nav_connection_blocker" ) )
		m_connectionBlockers.AddToTail( static_cast< CFuncNavConnectionBlocker * >( entity ) );
	Msg( "%d connection blockers found!\n", m_connectionBlockers.Count() );
}
