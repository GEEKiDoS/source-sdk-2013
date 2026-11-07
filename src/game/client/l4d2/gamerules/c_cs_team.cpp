//========= Copyright Valve Corporation, All rights reserved. ============//

#include "cbase.h"
#include "l4d2/gamerules/c_cs_team.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// C_Team owns roster reception and registration/removal in g_Teams.
IMPLEMENT_CLIENTCLASS_DT( C_CSTeam, DT_CSTeam, CCSTeam )
END_RECV_TABLE()
