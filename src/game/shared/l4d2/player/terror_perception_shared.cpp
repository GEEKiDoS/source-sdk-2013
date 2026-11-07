//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "tier0/memdbgon.h"

// Original server and client C512110/C512140 register these same replicated
// defaults. The player sampler lives on the server only.
ConVar still_vel_limit( "still_vel_limit", "5", 
#if defined( CLIENT_DLL )
	0x0000200A
#elif defined( GAME_DLL )
	0x00002006
#else
	FCVAR_DEVELOPMENTONLY | FCVAR_REPLICATED
#endif
,
	"if player velocity is lower than this, he is 'still'" );
ConVar still_view_accel_limit( "still_view_accel_limit", "1500", 
#if defined( CLIENT_DLL )
	0x0000200A
#elif defined( GAME_DLL )
	0x00002006
#else
	FCVAR_DEVELOPMENTONLY | FCVAR_REPLICATED
#endif
,
	"if player's view is accelerating less than this, his view is 'still'" );
