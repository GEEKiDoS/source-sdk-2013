//========= Copyright Valve Corporation, All rights reserved. ============//

#include "cbase.h"
#include "l4d2/gamerules/cs_team.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

LINK_ENTITY_TO_CLASS( cs_team_manager, CCSTeam );

// The original DT_CSTeam adds only its CTeam base table.
IMPLEMENT_SERVERCLASS_ST( CCSTeam, DT_CSTeam )
END_SEND_TABLE()

// Original server RVAs 0x24BED0 and 0x24BEE0 are genuinely empty.
void CCSTeam::Precache( void )
{
}

void CCSTeam::Think( void )
{
}
