//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/terror_shareddefs.h"
#include "steam/steam_api.h"
#include "l4d2/player/bots/survivor_bot.h"
#include "team.h"
#include "tier0/memdbgon.h"

static uint64 PlayerAvatarIdentity( edict_t *player )
{
	// SDK's genuine public identity adapter, not a fabricated lobby/session ID.
	const CSteamID *identity = engine->GetClientSteamID( player );
	return identity ? identity->ConvertToUint64() : 0;
}
KeyValues *CDirector::GetPlayerAvatarInfo( edict_t *player ) const
{
	uint64 identity = PlayerAvatarIdentity( player );
	if ( !identity ) return NULL; // Native26CDD0's actual zero-XUID guard.
	int index = m_playerAvatars.Find( identity );
	return index == m_playerAvatars.InvalidIndex() ? NULL : m_playerAvatars[index];
}
void CDirector::OnPlayerAvatarInfoReceived( edict_t *player, KeyValues *info )
{
	uint64 identity = PlayerAvatarIdentity( player );
	if ( identity ) UpdatePlayerAvatarInfo( identity, info );
}
void CDirector::UpdatePlayerAvatarInfo( uint64 identity, KeyValues *info )
{
	// Native2740A0: replace with an OWNED MakeCopy, notify with BOTH live
	// old/new values, then delete old. NULL means actual removal, not empty KV.
	int index = m_playerAvatars.Find( identity );
	if ( index == m_playerAvatars.InvalidIndex() && !info ) return;
	KeyValues *old = index == m_playerAvatars.InvalidIndex() ? NULL : m_playerAvatars[index];
	KeyValues *updated = NULL;
	if ( info )
	{
		updated = info->MakeCopy();
		if ( index == m_playerAvatars.InvalidIndex() ) m_playerAvatars.Insert( identity, updated );
		else m_playerAvatars[index] = updated;
	}
	else m_playerAvatars.RemoveAt( index );
	DevMsg( 2, "Director::OnPlayerAvatarsUpdated for xuid %llx\n", identity );
	CKeyValuesDumpContextAsDevMsg dump( 2 );
	if ( old ) old->Dump( &dump, 0, true );
	if ( updated ) updated->Dump( &dump, 0, true );
	if ( old ) old->deleteThis();
}
static SurvivorCharacterType ParseAvatarCharacter( const char *name )
{
	// Exact350F60 vocabulary, no model-name/display-name heuristic.
	if ( !Q_stricmp( name, "Coach" ) ) return (SurvivorCharacterType)2;
	if ( !Q_stricmp( name, "Mechanic" ) || !Q_stricmp( name, "Ellis" ) ) return (SurvivorCharacterType)3;
	if ( !Q_stricmp( name, "Gambler" ) || !Q_stricmp( name, "Nick" ) ) return (SurvivorCharacterType)0;
	if ( !Q_stricmp( name, "Producer" ) || !Q_stricmp( name, "Rochelle" ) ) return (SurvivorCharacterType)1;
	if ( !Q_stricmp( name, "NamVet" ) || !Q_stricmp( name, "Bill" ) ) return (SurvivorCharacterType)4;
	if ( !Q_stricmp( name, "TeenGirl" ) || !Q_stricmp( name, "Zoey" ) || !Q_stricmp( name, "TeenAngst" ) ) return (SurvivorCharacterType)5;
	if ( !Q_stricmp( name, "Biker" ) || !Q_stricmp( name, "Francis" ) ) return (SurvivorCharacterType)6;
	if ( !Q_stricmp( name, "Manager" ) || !Q_stricmp( name, "Louis" ) ) return (SurvivorCharacterType)7;
	return SurvivorCharacter_Unknown;
}
void CDirector::DecodePlayerAvatarInfo( KeyValues *info, int *team, SurvivorCharacterType *character )
{
	*team = TEAM_UNASSIGNED; *character = SurvivorCharacter_Unknown;
	if ( !info ) return;
	const char *value = info->GetString( "team", "" );
	*team = atoi( value );
	if ( !*team )
	{
		if ( !Q_stricmp( value, "Survivor" ) ) *team = TEAM_SURVIVOR;
		else if ( !Q_stricmp( value, "Infected" ) ) *team = TEAM_INFECTED;
		else if ( !Q_stricmp( value, "Spectator" ) ) *team = TEAM_SPECTATOR;
	}
	const char *avatar = info->GetString( "avatar", "" );
	if ( *avatar ) *character = ParseAvatarCharacter( avatar );
}

namespace
{
	struct CollectSwapRoster
	{
		CUtlVectorFixedGrowable< CTerrorPlayer *, 32 > &players;
		int team;
		CollectSwapRoster( CUtlVectorFixedGrowable< CTerrorPlayer *, 32 > &out, int which ) : players( out ), team( which ) {}
		bool operator()( CBasePlayer *base )
		{
			CTerrorPlayer *player = ToTerrorPlayer( base );
			if ( player->GetTeamNumber() == team ) players.AddToTail( player );
			return true;
		}
	};
}
void CDirector::SwapTeams()
{
	// Native26D830 brackets the REAL transfer, not merely its role flags.
	m_bSwappingTeams = true;
	SwapRoleProviders();
	CUtlVectorFixedGrowable< CTerrorPlayer *, 32 > survivors, infected;
	CollectSwapRoster spectators( survivors, TEAM_SPECTATOR ); ForEachPlayer( spectators );
	FOR_EACH_VEC( survivors, i )
	{
		CTerrorPlayer *player = survivors[i];
		if ( !player || !player->IsPlayer() || !player->edict() || player->entindex() == 0 ) continue;
		CBaseEntity *target = player->GetObserverTarget();
		if ( target && target->IsPlayer() && static_cast< CBasePlayer * >( target )->IsBot() ) player->TakeOverBot( false );
	}
	survivors.RemoveAll();
	CollectSwapRoster collectSurvivors( survivors, TEAM_SURVIVOR ); ForEachPlayer( collectSurvivors );
	CollectSwapRoster collectInfected( infected, TEAM_INFECTED ); ForEachPlayer( collectInfected );
	FOR_EACH_VEC( survivors, i )
	{
		CTerrorPlayer *player = survivors[i];
		if ( !player || !player->IsPlayer() || !player->edict() || player->entindex() == 0 ) continue;
		player->CommitSuicide( false ); // Native virtual2268, before IsBot/team transfer.
		if ( player->IsBot() )
		{
			Msg( "Switching Survivor Bot '%s' from team %d to SPECTATOR\n", player->GetPlayerName(), player->GetTeamNumber() );
			player->ChangeTeam( TEAM_SPECTATOR );
		}
		else
		{
			Msg( "Switching Survivor '%s' from team %d to ZOMBIE\n", player->GetPlayerName(), player->GetTeamNumber() );
			player->ChangeTeam( TEAM_INFECTED );
		}
		Msg( "   -> Player is now on team %d\n", player->GetTeamNumber() );
	}
	FOR_EACH_VEC( infected, i )
	{
		CTerrorPlayer *player = infected[i];
		if ( !player || !player->IsPlayer() || !player->edict() || player->entindex() == 0 ) continue;
		if ( player->IsBot() ) continue;
		int team;
		SurvivorCharacterType character;
		DecodePlayerAvatarInfo( GetPlayerAvatarInfo( player->edict() ), &team, &character );
		if ( character == SurvivorCharacter_Unknown || FindSurvivorByCharacter( static_cast< SurvivorCharacterType >( MapTerrorSurvivorCharacter( character ) ) ) )
		{
			Msg( "Switching Zombie '%s' from team %d to SURVIVOR\n", player->GetPlayerName(), player->GetTeamNumber() );
			player->SetDesiredSurvivorCharacter( SurvivorCharacter_Unknown );
		}
		else
		{
			Msg( "Switching Zombie '%s' from team %d to SURVIVOR %s\n", player->GetPlayerName(), player->GetTeamNumber(), ::GetResponseCharacterName( character ) );
			player->SetDesiredSurvivorCharacter( character );
		}
		player->ChangeTeam( TEAM_SURVIVOR );
		Msg( "   -> Player is now on team %d\n", player->GetTeamNumber() );
	}
	if ( AreSurvivorBotsDisabled() ) { m_bSwappingTeams = false; return; }
	CTeam *survivorTeam = GetGlobalTeam( TEAM_SURVIVOR );
	if ( !survivorTeam ) { m_bSwappingTeams = false; return; }
	if ( survivorTeam->GetNumPlayers() < 4 )
	{
		int needed = 4 - survivorTeam->GetNumPlayers(); // Native deliberately queries twice.
		for ( int i = 0; i < needed; ++i )
		{
			CreateSurvivorBot( SurvivorCharacter_Unknown );
			Msg( "Adding a survivor bot to fill out Survivor team\n" );
		}
	}
	m_bSwappingTeams = false;
}
void CDirector::OnPlayerTeamChanged( CTerrorPlayer *player, int )
{
	// S2709D0 genuinely ignores oldTeam. It reaccepts the real saved offer;
	// this is not an infected-count refresh or a replacement timer.
	if ( player && player->GetTeamNumber() == TEAM_INFECTED && HasAnySurvivorLeftSafeArea() )
		ReacceptSavedTankOffer();
}
