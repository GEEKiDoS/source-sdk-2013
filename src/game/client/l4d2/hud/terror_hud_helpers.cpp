//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/hud/terror_hud_helpers.h"
#include "l4d2/player/c_terrorplayer.h"
#include "l4d2/player/bots/c_survivor_bot.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "vgui/ILocalize.h"
#include "tier3/tier3.h"
#include "tier0/memdbgon.h"

int MapTerrorSurvivorDisplayCharacter( int character )
{
	if ( TerrorGameRules()->GetSurvivorSet() == 1 )
	{
		switch ( character ) { case 0: return 4; case 1: return 5; case 2: return 7; case 3: return 6; }
	}
	return character;
}
const char *GetTerrorSurvivorPortrait( int character, bool incapacitated )
{
	static const char *const portrait[8] = { "s_panel_gambler", "s_panel_producer", "s_panel_coach", "s_panel_mechanic",
		"s_panel_namvet", "s_panel_teenangst", "s_panel_biker", "s_panel_manager" };
	static const char *const incap[8] = { "s_panel_gambler_incap", "s_panel_producer_incap", "s_panel_coach_incap", "s_panel_mechanic_incap",
		"s_panel_namvet_incap", "s_panel_teenangst_incap", "s_panel_biker_incap", "s_panel_manager_incap" };
	character = MapTerrorSurvivorDisplayCharacter( character );
	return character >= 0 && character < ARRAYSIZE( portrait ) ? ( incapacitated ? incap[character] : portrait[character] ) : "";
}
const char *GetTerrorSurvivorDisplayNameToken( int character )
{
	static const char *const names[8] = { "#L4D360UI_Gambler", "#L4D360UI_Producer", "#L4D360UI_Coach", "#L4D360UI_Mechanic",
		"#L4D360UI_NamVet", "#L4D360UI_TeenGirl", "#L4D360UI_Biker", "#L4D360UI_Manager" };
	character = MapTerrorSurvivorDisplayCharacter( character );
	return character >= 0 && character < ARRAYSIZE( names ) ? names[character] : "Survivor";
}
void GetTerrorHudPlayerName( C_TerrorPlayer *player, wchar_t *name, int nameBytes )
{
	C_SurvivorBot *bot = dynamic_cast< C_SurvivorBot * >( player );
	const wchar_t *localized = bot && bot->GetHumanSpectatorUserID() <= 0 ?
		g_pVGuiLocalize->Find( GetTerrorSurvivorDisplayNameToken( player->GetSurvivorCharacter() ) ) : NULL;
	if ( localized ) V_wcsncpy( name, localized, nameBytes );
	else g_pVGuiLocalize->ConvertANSIToUnicode( player->GetPlayerName(), name, nameBytes );
}
