//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//
#include "cbase.h"
#include "game.h"
#include "physics.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

void MapCycleFileChangedCallback( IConVar *var, const char *pOldString, float flOldValue )
{
	if ( Q_stricmp( pOldString, mapcyclefile.GetString() ) != 0 )
	{
		if ( GameRules() )
		{
			// For multiplayer games, forces the mapcyclefile to be reloaded
			GameRules()->ResetMapCycleTimeStamp();
		}
	}
}

ConVar	displaysoundlist( "displaysoundlist","0"
#if defined( L4D2 )
	, 0x00000006
#endif
 );
ConVar  mapcyclefile( "mapcyclefile", "mapcycle.txt", 
#if defined( L4D2 )
	0x00000006
#else
	FCVAR_NONE
#endif
, "Name of the .txt file used to cycle the maps on multiplayer servers ", MapCycleFileChangedCallback );
ConVar  servercfgfile( "servercfgfile","server.cfg"
#if defined( L4D2 )
	, 0x00000006
#endif
 );
ConVar  lservercfgfile( "lservercfgfile","listenserver.cfg"
#if defined( L4D2 )
	, 0x00000006
#endif
 );

// multiplayer server rules
ConVar	teamplay( "mp_teamplay","0", 
#if defined( L4D2 )
	0x00000106
#else
	FCVAR_NOTIFY
#endif
 );
ConVar	falldamage( "mp_falldamage","0", 
#if defined( L4D2 )
	0x00000106
#else
	FCVAR_NOTIFY
#endif
 );
ConVar	weaponstay( "mp_weaponstay","0", 
#if defined( L4D2 )
	0x00000106
#else
	FCVAR_NOTIFY
#endif
 );
ConVar	forcerespawn( "mp_forcerespawn","1", 
#if defined( L4D2 )
	0x00000106
#else
	FCVAR_NOTIFY
#endif
 );
ConVar	footsteps( "mp_footsteps","1", 
#if defined( L4D2 )
	0x00000106
#else
	FCVAR_NOTIFY
#endif
 );
#if defined( L4D2 )
// S587DA0: neutral common vision relies on the survivor's spawn flashlight.
ConVar	flashlight( "mp_flashlight","1", 0x00000106 );
#elif defined( CSTRIKE )
ConVar	flashlight( "mp_flashlight","1", FCVAR_NOTIFY );
#else
ConVar	flashlight( "mp_flashlight","0", FCVAR_NOTIFY );
#endif
ConVar	aimcrosshair( "mp_autocrosshair","1", 
#if defined( L4D2 )
	0x00000106
#else
	FCVAR_NOTIFY
#endif
 );
ConVar	decalfrequency( "decalfrequency","10", 
#if defined( L4D2 )
	0x00000106
#else
	FCVAR_NOTIFY
#endif
 );
ConVar	teamlist( "mp_teamlist","hgrunt;scientist", 
#if defined( L4D2 )
	0x00000106
#else
	FCVAR_NOTIFY
#endif
 );
ConVar	teamoverride( "mp_teamoverride","1"
#if defined( L4D2 )
	, 0x00000006
#endif
 );
ConVar	defaultteam( "mp_defaultteam","0"
#if defined( L4D2 )
	, 0x00000006
#endif
 );
ConVar	allowNPCs( "mp_allowNPCs","1", 
#if defined( L4D2 )
	0x00000106
#else
	FCVAR_NOTIFY
#endif
 );

// Engine Cvars
const ConVar	*g_pDeveloper = NULL;


ConVar suitvolume( "suitvolume", "0.25", 
#if defined( L4D2 )
	0x00000084
#else
	FCVAR_ARCHIVE
#endif
 );

class CGameDLL_ConVarAccessor : public IConCommandBaseAccessor
{
public:
	virtual bool	RegisterConCommandBase( ConCommandBase *pCommand )
	{
		// Remember "unlinked" default value for replicated cvars
		bool replicated = pCommand->IsFlagSet( FCVAR_REPLICATED );
		const char *defvalue = NULL;
		if ( replicated && !pCommand->IsCommand() )
		{
			defvalue = ( ( ConVar * )pCommand)->GetDefault();
		}

		// Link to engine's list instead
		cvar->RegisterConCommand( pCommand );

		// Apply any command-line values.
		const char *pValue = cvar->GetCommandLineValue( pCommand->GetName() );
		if( pValue )
		{
			if ( !pCommand->IsCommand() )
			{
				( ( ConVar * )pCommand )->SetValue( pValue );
			}
		}
		else
		{
			// NOTE:  If not overridden at the command line, then if it's a replicated cvar, make sure that it's
			//  value is the server's value.  This solves a problem where think_limit is defined in shared
			//  code but the value is inside and #if defined( _DEBUG ) block and if you have a debug game .dll
			//  and a release client, then the limiit was coming from the client even though the server value 
			//  was the one that was important during debugging.  Now the server trumps the client value for
			//  replicated ConVars by setting the value here after the ConVar has been linked.
			if ( replicated && defvalue && !pCommand->IsCommand() )
			{
				ConVar *var = ( ConVar * )pCommand;
				var->SetValue( defvalue );
			}
		}

		return true;
	}
};

static CGameDLL_ConVarAccessor g_ConVarAccessor;

// Register your console variables here
// This gets called one time when the game is initialied
void InitializeCvars( void )
{
	// Register cvars here:
	ConVar_Register( FCVAR_GAMEDLL, &g_ConVarAccessor ); 

	g_pDeveloper	= cvar->FindVar( "developer" );
}

