//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"

#include "tier0/memdbgon.h"

ConVar z_frustration( "z_frustration", "1", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar z_tank_speed( "z_tank_speed", "210", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar z_tank_speed_vs( "z_tank_speed_vs", "210", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar z_tank_walk_speed( "z_tank_walk_speed", "100", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar z_tank_grenade_roll( "z_tank_grenade_roll", "-10", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
