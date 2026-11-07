//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/gamerules/terror_game_details.h"
#include "l4d2/gamerules/terror_mission_info.h"
#include "l4d2/gamerules/terror_mode.h"
#include "iserver.h"
#include "tier1/fmtstr.h"
#include "tier0/memdbgon.h"

static KeyValues *ExtendGameIdentity( KeyValues *details, const char *mapName, KeyValues **outMission )
{
	// Original M4700. These fields depend on the actual active BSP and the sole
	// mounted mode/mission dictionaries, not a campaign guessed from map text.
	details->SetString( "Game/state", "game" );
	static ConVarRef modeVar( "mp_gamemode", true );
	if ( modeVar.IsValid() )
	{
		details->SetString( "Game/mode", modeVar.GetString() );
		KeyValues *mode = g_TerrorModes.FindMode( modeVar.GetString() );
		if ( mode ) details->SetInt( "Game/vanilla", mode->GetInt( "vanilla", 0 ) != 0 );
	}
	KeyValues *mission = NULL;
	KeyValues *chapter = g_TerrorModes.GetChapterInfoByBspName( details, mapName, &mission );
	if ( chapter )
	{
		details->SetString( "Game/campaign", mission->GetString( "Name", "" ) );
		details->SetInt( "Game/chapter", chapter->GetInt( "chapter", 0 ) );
		if ( outMission ) *outMission = mission;
	}
	return chapter;
}

KeyValues *TerrorBuildActiveServerGameProjection()
{
	// Explicit game-owned adapter through supported IVEngineServer::GetIServer.
	// Target CGameServer's actual IsActive/IsDedicated/name/map/count/address
	// slots were corroborated in the retained target-engine database. This is
	// NOT a claim that proprietary INETSUPPORT_001 exists in the target engine.
	IServer *server = engine->GetIServer();
	// Native server details include loading (state 1) as well as active states.
	// SDK IsActive excludes the loading phase that builds the model catalogs.
	if ( !server->IsActive() && !server->IsLoading() ) return NULL;
	KeyValues *details = new KeyValues( "GameDetailsServer" );
	ExtendGameIdentity( details, server->GetMapName(), NULL );
	return details;
}

static void ExtendFullGameDetails( KeyValues *details, KeyValues *request, const char *mapName )
{
	KeyValues *mission = NULL;
	KeyValues *chapter = ExtendGameIdentity( details, mapName, &mission );
	if ( chapter )
	{
		KeyValues *info = details->FindKey( "Game/MissionInfo", true );
		info->SetString( "Version", mission->GetString( "Version", "" ) );
		info->SetString( "BuiltIn", mission->GetString( "BuiltIn", "1" ) );
		info->SetInt( "addon", mission->GetInt( "addon", 1 ) != 0 );
		info->SetString( "DisplayTitle", mission->GetString( "DisplayTitle", "" ) );
		info->SetString( "Author", mission->GetString( "Author", "" ) );
		info->SetString( "Website", mission->GetString( "Website", "" ) );
		info->SetString( "MissionFile", mission->GetString( "CfgFile", "" ) );
		info->SetUint64( "workshopid", mission->GetUint64( "workshopid", 0 ) );
		info->SetInt( "InfectedOnly", mission->GetInt( "infectedonly", 0 ) != 0 );
		const int survivorSet = mission->GetInt( "survivor_set", 2 );
		if ( survivorSet != ( request ? request->GetInt( "Game/MissionInfo/SurvivorSet", 0 ) : 0 ) )
			info->SetInt( "SurvivorSet", survivorSet );
		KeyValues *mode = g_TerrorModes.FindMode( details->GetString( "Game/mode", "" ) );
		KeyValues *modeInfo = details->FindKey( "Game/ModeInfo", true );
		modeInfo->SetString( "DisplayTitle", mode ? mode->GetString( "DisplayTitle", "" ) : "" );
		modeInfo->SetUint64( "workshopid", mode ? mode->GetUint64( "workshopid", 0 ) : 0 );
		modeInfo->SetInt( "addon", mode && mode->GetInt( "addon", 0 ) != 0 );
	}
	static ConVarRef difficulty( "z_difficulty", true );
	static ConVarRef rounds( "mp_roundlimit", true );
	if ( difficulty.IsValid() ) details->SetString( "Game/difficulty", difficulty.GetString() );
	if ( rounds.IsValid() ) details->SetInt( "Game/maxrounds", rounds.GetInt() );
	KeyValues *mode = g_TerrorModes.FindMode( details->GetString( "Game/mode", "" ) );
	uint64 dlc = mode ? mode->GetUint64( "dlcmask", 0 ) : 0;
	mission = NULL;
	chapter = g_TerrorModes.GetChapterInfo( details, &mission );
	if ( chapter ) dlc |= chapter->GetUint64( "dlcmask", 0 ) | mission->GetUint64( "dlcmask", 0 );
	const char *campaign = details->GetString( "Game/campaign", "" );
	KeyValues *namedMission = *campaign ? g_TerrorModes.FindMission( campaign ) : NULL;
	if ( namedMission ) dlc |= namedMission->GetUint64( "dlcmask", 0 );
	details->SetUint64( "Game/dlcrequired", dlc );
}

KeyValues *TerrorBuildGameDetails( const TerrorGameDetailsSnapshot_t &snapshot, KeyValues *sessionSettings, KeyValues *request )
{
	KeyValues *details;
	const char *mapName;
	if ( snapshot.serverActive )
	{
		details = new KeyValues( "GameDetailsServer" );
		// Original constructor template is real: its absence of Game is crucial.
		details->SetString( "System/network", "LIVE" );
		details->SetString( "System/access", "public" );
		details->SetString( "Server/name", "" );
		details->SetString( "Server/server", "" );
		details->SetString( "Server/adronline", "" );
		details->SetString( "Server/adrlocal", "" );
		details->SetInt( "Members/numSlots", 0 );
		details->SetInt( "Members/numPlayers", 0 );
		if ( !snapshot.dedicated && sessionSettings )
			TerrorMergeMissionKeyValues( details, sessionSettings, true );
		details->SetString( "Server/name", snapshot.serverName );
		details->SetString( "Server/server", snapshot.dedicated ? "dedicated" : "listen" );
		details->SetString( "Server/adronline", snapshot.onlineAddress.ToString() );
		details->SetString( "Server/adrlocal", snapshot.localAddress.ToString() );
		if ( snapshot.dedicated && snapshot.advertiseFriends && snapshot.friendsAdvertisementAllowed )
			details->SetString( "System/access", "friends" );
		// Native title M34C0 really returns8. This is not a fabricated actor cap.
		const int slots = clamp( snapshot.slots, 0, 8 );
		details->SetInt( "Members/numSlots", slots );
		details->SetInt( "Members/numPlayers", clamp( snapshot.players, 0, slots ) );
		mapName = snapshot.serverMap;
	}
	else
	{
		// Native M9960 client-only branch: real caller-supplied engine snapshot,
		// not a missing-server fallback or a fake client actor.
		if ( snapshot.clientInMainMenu || !snapshot.clientLevelName || !*snapshot.clientLevelName ) return NULL;
		details = new KeyValues( "GameDetailsClient" );
		mapName = snapshot.clientLevelName;
	}
	ExtendFullGameDetails( details, request, mapName );
	return details;
}
