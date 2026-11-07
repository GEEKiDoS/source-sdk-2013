//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/effects/glow_cvars.h"

#include "tier0/memdbgon.h"

// S:59E090/59E0C0. Native FCVAR_REPLICATED only (0x2000), not CHEAT.
ConVar sv_disable_glow_survivors( "sv_disable_glow_survivors", "0", 
#if defined( CLIENT_DLL )
	0x0000200A
#elif defined( GAME_DLL )
	0x00002006
#else
	FCVAR_REPLICATED
#endif
,
	"Disable glows around Survivors." );
ConVar sv_disable_glow_faritems( "sv_disable_glow_faritems", "0", 
#if defined( CLIENT_DLL )
	0x0000200A
#elif defined( GAME_DLL )
	0x00002006
#else
	FCVAR_REPLICATED
#endif
,
	"Disable glows around items that are too far away to pickup." );

// C:512AE0. Supply the same genuine replicated console-variable producer
// on the standalone game's server/client rather than a constant provider.
ConVar sv_glowenable( "sv_glowenable", "1", 
#if defined( CLIENT_DLL )
	0x0000200A
#else
	FCVAR_REPLICATED
#endif
,
	"Turns on and off the terror glow highlight effects" );

// C:4FF990. One shared definition supplies the standalone replicated owner.
ConVar glow_use_tolerance( "glow_use_tolerance", "0.85", 
#if defined( CLIENT_DLL )
	0x00006008
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
