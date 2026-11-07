//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/terror_precache_weapon.h"
#include "tier0/memdbgon.h"

extern ConVar mp_gamemode;

// C27D8B0 / S2E1BB0. The target has no match session: Main approved the
// original no-session branch for excludeweapons. Do not infer session state
// from the mode dictionary, command line, or mounted mission metadata.
bool IsWeaponAllowedForPrecache( const char *pClassname )
{
	if ( !Q_stricmp( mp_gamemode.GetString(), "mutation14" ) )
	{
		if ( !Q_stricmp( pClassname, "weapon_grenade_launcher" ) )
			return false;
		if ( !Q_stricmp( pClassname, "weapon_rifle_m60" ) )
			return true;
	}
	return true;
}
