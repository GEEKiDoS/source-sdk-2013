//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/terror_shareddefs.h"
#include "l4d2/infected/special/boomer_locomotion.h"
#include "l4d2/infected/special/spitter_locomotion.h"
#include "l4d2/infected/boss_zombie_contact.h"
#include "nav_area.h"
#include "tier0/memdbgon.h"

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif
bool BoomerLocomotion::IsAreaTraversable( const CNavArea *area ) const // S42E330
{
	if ( area->HasAttributes( 0x10000 ) ) return false;
	if ( area->HasAttributes( 0x20000 ) ) return true;
	return !area->IsBlocked( TEAM_INFECTED );
}
bool SpitterLocomotion::IsAreaTraversable( const CNavArea *area ) const // S467420
{
	if ( area->HasAttributes( 0x10000 ) ) return false;
	if ( area->HasAttributes( 0x20000 ) ) return true;
	return !area->IsBlocked( TEAM_INFECTED );
}
bool BoomerLocomotion::IsEntityTraversable( CBaseEntity *entity, TraverseWhenType when ) const // S42E370
{
	if ( entity->GetSpawnFlags() & 0x1000 ) return false;
	if ( ( entity->GetSpawnFlags() & 0x2000 ) || IsLowHealthBossBreakable( entity ) ) return true;
	return ILocomotion::IsEntityTraversable( entity, when );
}
bool SpitterLocomotion::IsEntityTraversable( CBaseEntity *entity, TraverseWhenType when ) const // S467460
{
	if ( entity->GetSpawnFlags() & 0x1000 ) return false;
	if ( ( entity->GetSpawnFlags() & 0x2000 ) || IsLowHealthBossBreakable( entity ) ) return true;
	return ILocomotion::IsEntityTraversable( entity, when );
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif
