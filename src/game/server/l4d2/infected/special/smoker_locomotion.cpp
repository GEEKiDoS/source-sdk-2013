//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/terror_shareddefs.h"
#include "l4d2/infected/special/smoker_locomotion.h"
#include "l4d2/infected/boss_zombie_contact.h"
#include "nav_area.h"
#include "tier0/memdbgon.h"

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif
// S45B970 / S45B9B0. Explicit allow takes precedence over blocked NAV.
bool SmokerLocomotion::IsAreaTraversable( const CNavArea *area ) const
{
	if ( area->HasAttributes( 0x10000 ) ) return false;
	if ( area->HasAttributes( 0x20000 ) ) return true;
	return !area->IsBlocked( TEAM_INFECTED, false );
}
bool SmokerLocomotion::IsEntityTraversable( CBaseEntity *entity, TraverseWhenType when ) const
{
	if ( entity->HasSpawnFlags( 0x1000 ) ) return false;
	if ( entity->HasSpawnFlags( 0x2000 ) || IsLowHealthBossBreakable( entity ) ) return true;
	return PlayerLocomotion::IsEntityTraversable( entity, when );
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif
