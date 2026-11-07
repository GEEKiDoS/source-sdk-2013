//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//
#include "cbase.h"
#include "gamevars_shared.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#ifdef GAME_DLL
void MPForceCameraCallback( IConVar *var, const char *pOldString, float flOldValue )
{
	if ( mp_forcecamera.GetInt() < OBS_ALLOW_ALL || mp_forcecamera.GetInt() >= OBS_ALLOW_NUM_MODES )
	{
		mp_forcecamera.SetValue( OBS_ALLOW_TEAM );
	}
}
#endif 

// some shared cvars used by game rules
ConVar mp_forcecamera( 
	"mp_forcecamera", 
#ifdef CSTRIKE
	"0", 
#else
	
#if defined( L4D2 )
	"0"
#else
	"1"
#endif
,
#endif
	
#if defined( L4D2 ) && defined( CLIENT_DLL )
	0x0000200A
#elif defined( L4D2 ) && defined( GAME_DLL )
	0x00002006
#else
	FCVAR_REPLICATED
#endif
,
	"Restricts spectator modes for dead players"
#ifdef GAME_DLL
	, MPForceCameraCallback 
#endif
	);
	
ConVar mp_allowspectators(
	"mp_allowspectators", 
	"1.0", 
	
#if defined( L4D2 ) && defined( CLIENT_DLL )
	0x0000200A
#elif defined( L4D2 ) && defined( GAME_DLL )
	0x00002006
#else
	FCVAR_REPLICATED
#endif
,
	"toggles whether the server allows spectator mode or not" );

#if defined( L4D2 )
ConVar friendlyfire(
	"mp_friendlyfire", "1",
	
#if defined( L4D2 ) && defined( CLIENT_DLL )
	0x0000210A
#elif defined( L4D2 ) && defined( GAME_DLL )
	0x00002106
#else
	FCVAR_DEVELOPMENTONLY | FCVAR_NOTIFY | FCVAR_REPLICATED
#endif
,
	"Allows team members to injure other members of their team", true, 1.0f, true, 1.0f
	);
#else
ConVar friendlyfire(
	"mp_friendlyfire",
	"0",
	FCVAR_REPLICATED | FCVAR_NOTIFY,
	"Allows team members to injure other members of their team"
	);
#endif

ConVar mp_fadetoblack( 
	"mp_fadetoblack", 
	"0", 
	
#if defined( L4D2 ) && defined( CLIENT_DLL )
	0x0000210A
#elif defined( L4D2 ) && defined( GAME_DLL )
	0x00002106
#else
	FCVAR_REPLICATED | FCVAR_NOTIFY
#endif
, 
	"fade a player's screen to black when he dies" );


ConVar sv_hudhint_sound( "sv_hudhint_sound", "1", FCVAR_REPLICATED );