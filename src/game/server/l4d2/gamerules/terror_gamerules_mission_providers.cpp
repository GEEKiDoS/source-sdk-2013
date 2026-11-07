//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/gamerules/terror_mode.h"
#include "l4d2/gamerules/terror_mission_info.h"
#include "tier1/fmtstr.h"
#include "tier0/memdbgon.h"

KeyValues *CTerrorGameRules::GetMissionInfo() const
{
	// S2D9DE0: destroy the owned details, return the dictionary-owned outMission.
	KeyValues *details = TerrorBuildActiveServerGameProjection();
	if ( !details ) return NULL;
	KeyValues *mission = NULL;
	g_TerrorModes.GetChapterInfo( details, &mission );
	details->deleteThis();
	return mission;
}
KeyValues *CTerrorGameRules::GetChapterInfo() const
{
	// S2D9EC0: returned chapter belongs to the same persistent dictionary.
	KeyValues *details = TerrorBuildActiveServerGameProjection();
	if ( !details ) return NULL;
	KeyValues *chapter = g_TerrorModes.GetChapterInfo( details, NULL );
	details->deleteThis();
	return chapter;
}
int CTerrorGameRules::GetMissionChapterCount() const
{
	KeyValues *mission = GetMissionInfo();
	if ( !mission ) return -1;
	extern ConVar mp_gamemode;
	KeyValues *chapters = mission->FindKey( CFmtStr( "Modes/%s", mp_gamemode.GetString() ) );
	if ( !chapters ) return -1;
	int count = chapters->GetInt( "chapters", 0 );
	return count > 0 ? count : -1;
}
static int s_nSurvivorSet = 0;
static int s_nL4D1Campaign = 0;
bool CTerrorGameRules::IsL4D1Campaign() const
{
	if ( !s_nL4D1Campaign )
	{
		KeyValues *mission = GetMissionInfo();
		const char *suffix = mission ? StringAfterPrefix( mission->GetString( "name", "" ), "L4D2C" ) : NULL;
		const int campaign = suffix ? atoi( suffix ) : 0;
		s_nL4D1Campaign = suffix && static_cast< unsigned int >( campaign ) - 8U <= 4U ? 1 : 2;
	}
	return s_nL4D1Campaign == 1;
}
int CTerrorGameRules::GetSurvivorSet() const
{
	if ( !s_nSurvivorSet )
	{
		KeyValues *mission = GetMissionInfo();
		s_nSurvivorSet = mission ? mission->GetInt( "survivor_set", 2 ) : 2;
	}
	return s_nSurvivorSet;
}
void CTerrorGameRules::ResetSurvivorSetCache()
{
	s_nL4D1Campaign = 0; // Native2DF810 actual order:994 then990.
	s_nSurvivorSet = 0;
}
