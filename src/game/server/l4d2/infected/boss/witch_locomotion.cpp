//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/infected/boss/witch_locomotion.h"
#include "l4d2/infected/common/infected.h"
#include "l4d2/nav/terror_nav_area.h"
#include "l4d2/director/terror_director.h"

#include "tier0/memdbgon.h"

ConVar z_witch_speed( "z_witch_speed", "300", 0x00006004 );
ConVar z_witch_speed_inured( "z_witch_speed_inured", "200", 0x00006004 );

// S:0x41C4B0 initializes the concrete locomotor's only additional field.
WitchLocomotion::WitchLocomotion( Infected *me ) : ZombieBotLocomotion( me ), m_speedLimit( 1000.0f )
{
}

void WitchLocomotion::SetSpeedLimit( float speed )
{
	m_speedLimit = speed;
}

float WitchLocomotion::GetSpeedLimit() const
{
	return m_speedLimit;
}

float WitchLocomotion::GetRunSpeed() const
{
	return z_witch_speed.GetFloat();
}

// S:0x4222A0/0x4222C0 use the speed LIMIT, not actual velocity or run speed.
float WitchLocomotion::GetMaxAcceleration() const
{
	return m_speedLimit > 35.0f ? BaseClass::GetMaxAcceleration() : 10.0f;
}

float WitchLocomotion::GetMaxDeceleration() const
{
	return m_speedLimit > 35.0f ? BaseClass::GetMaxDeceleration() : 45.0f;
}

bool WitchLocomotion::IsAreaTraversable( const CNavArea *area ) const
{
	if ( area->IsBlocked( TEAM_INFECTED, false ) )
		return false;

	const CNavArea *current = GetBot()->GetEntity()->GetLastKnownArea();
	if ( current && !( ToTerrorNavArea( current )->GetSpawnAttributes() & 0x880 ) &&
		!TheDirector->AreWitchesAllowedInCheckpoints() &&
		( ToTerrorNavArea( area )->GetSpawnAttributes() & 0x880 ) )
		return false;

	return BaseClass::IsAreaTraversable( area );
}

// S:0x41AE30 writes only the native ground locomotor's velocity vector.
void WitchLocomotion::SetVelocity( const Vector &velocity )
{
	NextBotGroundLocomotion::SetVelocity( velocity );
}
