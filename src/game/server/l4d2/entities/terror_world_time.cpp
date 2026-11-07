//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "world.h"
#include "tier0/memdbgon.h"

static ConVar sv_force_time_of_day( "sv_force_time_of_day", "-1", FCVAR_CHEAT | FCVAR_GAMEDLL, "0 - midnight, 3 - afternoon" );

// S:236E40. Although this field is not in DT_World, the retail setter uses
// the normal native entity network-change notifier. Keep its actual owner.
void CWorld::SetTimeOfDay( int timeOfDay )
{
	m_iTimeOfDay = timeOfDay;
}

// S:235FB0: no range normalization, clock progression or lighting heuristic.
bool IsMapTimeOfDay( int first, int last )
{
	int timeOfDay = sv_force_time_of_day.GetInt();
	if ( timeOfDay == -1 )
	{
		CWorld *world = GetWorldEntity();
		timeOfDay = world ? world->GetTimeOfDay() : 0;
	}
	return timeOfDay >= first && timeOfDay <= last;
}
