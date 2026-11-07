//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/terror_shareddefs.h"
#include "l4d2/infected/special/jockey_locomotion.h"
#include "l4d2/infected/boss_zombie_contact.h"
#include "nav_area.h"
#include "tier0/memdbgon.h"

// S44F680/S44F6C0.
bool JockeyLocomotion::IsAreaTraversable( const CNavArea *area ) const
{
	if ( area->HasAttributes( 0x10000 ) ) return false;
	if ( area->HasAttributes( 0x20000 ) ) return true;
	return !area->IsBlocked( TEAM_INFECTED, false );
}
bool JockeyLocomotion::IsEntityTraversable( CBaseEntity *entity, TraverseWhenType when ) const
{
	if ( entity->HasSpawnFlags( 0x1000 ) ) return false;
	if ( entity->HasSpawnFlags( 0x2000 ) || IsLowHealthBossBreakable( entity ) ) return true;
	return PlayerLocomotion::IsEntityTraversable( entity, when );
}