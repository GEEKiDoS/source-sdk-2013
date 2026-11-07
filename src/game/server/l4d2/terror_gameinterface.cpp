//========= Copyright Valve Corporation, All rights reserved. ============//

#include "cbase.h"
#include "gameinterface.h"
#include "AI_ResponseSystem.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/gamerules/terror_mode.h"
#include "iserver.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// The SDK has no MatchSession. This is a one-shot launch setting, not a
// player-count heuristic or an archived replacement for the match framework.
static ConVar l4d2_offline( "l4d2_offline", "0", FCVAR_HIDDEN,
	"Create the next listen-server game as an offline session." );
static ConVar sv_pausable_dev( "sv_pausable_dev", "1", FCVAR_DEVELOPMENTONLY,
	"Whether listen server is pausable when running -dev and playing solo against bots" );

extern ConVar mp_gamemode;
extern ConVar mp_roundlimit;
extern ConVar director_min_start_players;

void CServerGameDLL::ApplyGameSettings( KeyValues *settings, bool background )
{
	// S0F05A0: ExecGameTypeCfg projects the selected mounted descriptor into
	// the same settings request consumed by the ordinary setter path below.
	// No target MatchSession, reservation or recursive map command is invented.
	if ( !settings )
		return;
	// S0F0658: the SDK supplies the map_background arm directly to LevelInit.
	if ( background )
	{
		mp_gamemode.SetValue( "coop" );
		z_difficulty.SetValue( "normal" );
		director_min_start_players.SetValue( 0 );
		return;
	}
	if ( !Q_stricmp( settings->GetName(), "::ExecGameTypeCfg" ) )
	{
		if ( !*settings->GetString( "map/mapname", "" ) )
			return;
		KeyValues *mode = g_TerrorModes.GetCurrentMode();
		if ( mode )
		{
			settings->SetString( "Game/mode", mode->GetName() );
			settings->SetInt( "Members/numSlots", mode->GetInt( "maxplayers", 4 ) );
		}
		DevMsg( "  Adjusting game mode %s, %d slots.\n",
			settings->GetString( "Game/mode", "(unknown)" ),
			settings->GetInt( "Members/numSlots", 0 ) );
		settings->SetName( "left4dead2" );
	}
	if ( Q_stricmp( settings->GetName(), "left4dead2" ) )
		return;

	const char *mode = settings->GetString( "Game/mode", "" );
	if ( *mode )
		mp_gamemode.SetValue( mode );
	DevMsg( "            game mode = %s\n", mode );

	const char *difficulty = settings->GetString( "Game/difficulty", NULL );
	if ( difficulty )
	{
		DevMsg( "            difficulty = %s\n", difficulty );
		z_difficulty.SetValue( difficulty );
	}
	const int rounds = settings->GetInt( "Game/maxrounds", 0 );
	if ( rounds )
	{
		DevMsg( "            roundlimit = %d\n", rounds );
		mp_roundlimit.SetValue( rounds );
	}
	const int players = settings->GetInt( "Members/numPlayers", -1 );
	if ( players != -1 )
	{
		DevMsg( "            num players = %d\n", players );
		director_min_start_players.SetValue( players );
	}
}

void CDirector::BeginGameSession()
{
	m_bOfflineSession = !engine->IsDedicatedServer() && l4d2_offline.GetBool();
	l4d2_offline.SetValue( 0 );
}

CON_COMMAND_F( l4d2_gameui_pause, "GameUI pause state for the local listen-server host.", FCVAR_HIDDEN )
{
	if ( args.ArgC() != 2 || engine->IsDedicatedServer() )
		return;

	CBasePlayer *host = UTIL_GetListenServerHost();
	IServer *server = engine->GetIServer();
	if ( !host || UTIL_GetCommandClient() != host || !server->IsActive() )
		return;

	const bool paused = Q_atoi( args[1] ) != 0;
	static ConVarRef developer( "developer" );
	static ConVarRef pausable( "sv_pausable" );
	// E122480/E1360E0: the native -dev arm actually reads developer != 0,
	// and counts connected clients minus fake clients, not survivor entities.
	const bool allowed = TheDirector->IsOfflineSession() || pausable.GetBool() ||
		( developer.GetInt() != 0 && sv_pausable_dev.GetBool() &&
		  server->GetNumClients() - server->GetNumFakeClients() == 1 );
	// E134B50 also admits unpause when the server is already paused, even if
	// its eligibility changed while the menu was open. Do not change sv_pausable.
	if ( allowed || ( !paused && server->IsPaused() ) )
		engine->SetPausedForced( paused );
}

void CServerGameClients::GetPlayerLimits( int &minplayers, int &maxplayers, int &defaultMaxPlayers ) const
{
	// These are engine client slots, not the game mode's human-player limit.
	minplayers = 18;
	maxplayers = 18;
	defaultMaxPlayers = minplayers;
}

void CServerGameDLL::LevelInit_ParseAllEntities( const char *pMapEntities )
{
	// The common LevelInit path has already parsed the map entities.
	// The original comparison is against the entire, case-sensitive map name.
	if ( Q_strcmp( STRING( gpGlobals->mapname ), "cs_" ) != 0 )
	{
		extern IResponseSystem *g_pResponseSystem;
		g_pResponseSystem->PrecacheResponses( false );
	}
}
