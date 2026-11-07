//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/player/terrorplayer.h"
#include "tier0/memdbgon.h"

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif
ConVar director_tank_bypass_max_flow_travel( "director_tank_bypass_max_flow_travel", "1500", 0x00004004 ); // S592AE0
bool CDirector::HasSurvivorsAdvancedBeyondTank() const // S25C740; genuine452/448 owner state
{
	if ( m_nTankCount > 0 )
	{
		CTerrorPlayer *leader = FindHighestFlowSurvivor( 0 );
		if ( leader && m_flTankStartFlow != -9999.0f )
		{
			const float travel = director_tank_bypass_max_flow_travel.GetFloat();
			if ( leader->GetFlowDistance( 0 ) > (double)travel + (double)m_flTankStartFlow ) return true;
		}
	}
	return false;
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif
