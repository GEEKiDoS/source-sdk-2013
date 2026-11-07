//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/demo/cs_hltv_director.h"
#include "tier0/memdbgon.h"

// S23F7A0, original table107A3088. player_hurt is intentionally absent.
const char **CCSHLTVDirector::GetModEvents()
{
	static const char *events[] =
	{
		"hltv_status", "hltv_chat", "player_connect", "player_disconnect",
		"player_team", "player_info", "server_cvar", "player_death",
		"player_chat", "round_start", "round_end", "round_end_message", NULL
	};
	return events;
}

// S23F770. The original makes this discarded virtual query before the base.
void CCSHLTVDirector::CreateShotFromEvent( CHLTVGameEvent *event )
{
	event->m_Event->GetName();
	BaseClass::CreateShotFromEvent( event );
}
