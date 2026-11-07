//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/bots/survivor_bot.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/content/terror_model_precache.h"
#include "l4d2/player/terror_saved_player_states.h"
#include "team.h"
#include "tier0/memdbgon.h"

namespace
{
	// Native 25D630. A stale association retires the saved restore record;
	// it does not delete that record or require an alive/connected spectator.
	bool SurvivorBotHasReservedHuman( SurvivorBot *bot )
	{
		CBasePlayer *human = UTIL_PlayerByUserId( bot->GetHumanSpectatorUserID() );
		if ( human )
		{
			DevMsg( 1, "[TAKEOVER]: %s is already spectated by %s (%d)\n",
				GetResponseCharacterName( bot->GetSurvivorCharacter() ), human->GetPlayerName(), bot->GetHumanSpectatorUserID() );
			return true;
		}
		DevMsg( 1, "[TAKEOVER]: %s is spectated by an invalid userid - clearing spectator id.\n",
			GetResponseCharacterName( bot->GetSurvivorCharacter() ) );
		g_SavedTerrorPlayerStates.MarkPlayerRestoreDone( bot->GetHumanSpectatorUserID() );
		bot->ClearHumanSpectator();
		return false;
	}

	// Native 25D6C0. Keep the handle live across each state/observer callback.
	void AssociateHumanWithSurvivorBot( const CHandle< CTerrorPlayer > &player, SurvivorBot *bot )
	{
		player->ChangeTeam( TEAM_SPECTATOR );
		player->State_Transition( STATE_OBSERVER_MODE );
		bot->SetHumanSpectator( player.Get() );
		player->BeginObservingSurvivorBot( bot );
		if ( TerrorGameRules()->IsPlayerControlledZombies() || !bot->IsAlive() ||
			( !bot->HasLeftCheckpoint() && !bot->IsIncapacitated() ) )
			player->TakeOverBot( false );
	}
}

// Native 26CC60. The shared factory allocates the genuine SurvivorBot leaf.
void CDirector::CreateSurvivorBot( SurvivorCharacterType character )
{
	if ( AreSurvivorBotsDisabled() )
		return;
	if ( character == SurvivorCharacter_Unknown )
	{
		CUtlVector< SurvivorCharacterType > available;
		for ( int i = 0; i < 4; ++i )
		{
			const SurvivorCharacterType candidate = static_cast< SurvivorCharacterType >( i );
			if ( !FindSurvivorByCharacter( candidate ) )
				available.AddToTail( candidate );
		}
		if ( !available.Count() )
			return;
		character = available[RandomInt( 0, available.Count() - 1 )];
	}
	else if ( static_cast< unsigned int >( character ) - 4u < 4u && FindSurvivorByCharacter( character ) )
		return;

	SurvivorBot *bot = NextBotCreatePlayerBot< SurvivorBot >( GetSurvivorBotName( character ) );
	if ( bot )
		bot->HandleCommand_JoinTeam( character >= 4 ? TEAM_L4D1_SURVIVOR : TEAM_SURVIVOR, character, false );
}

// Native 264FD0. The chosen names below describe these recovered native helpers;
// they do not introduce another roster, reservation, or transfer representation.
bool CDirector::TryTakeOverSurvivorCharacter( const CHandle< CTerrorPlayer > &player, SurvivorCharacterType character )
{
	DevMsg( 1, "[TAKEOVER]: %s (%d) wants to take over character %s\n",
		player->GetPlayerName(), player->GetUserID(), GetResponseCharacterName( character ) );
	CTerrorPlayer *occupant = FindSurvivorByCharacter( character );
	if ( occupant && !occupant->IsBot() )
	{
		DevMsg( 1, "[TAKEOVER]: %s is already posessed by %s (%d)\n",
			GetResponseCharacterName( character ), occupant->GetPlayerName(), occupant->GetUserID() );
		return false;
	}
	SurvivorBot *bot = dynamic_cast< SurvivorBot * >( occupant );
	if ( bot )
	{
		if ( bot->GetHumanSpectatorUserID() != 0 && SurvivorBotHasReservedHuman( bot ) )
			return false;
		AssociateHumanWithSurvivorBot( player, bot );
		return true;
	}

	static ConVarRef survivor_limit( "survivor_limit" );
	const int count = GetTeamPlayerCount( TEAM_SURVIVOR, 1 );
	if ( count >= survivor_limit.GetInt() )
		return false;
	// Native 2FBCC0 is the raw desired-character integer, NOT 3164E0's
	// model/NAV/name-changing SetSurvivorCharacter operation.
	player->SetDesiredSurvivorCharacter( character );
	player->ChangeTeam( TEAM_SURVIVOR );
	return true;
}

// Native 268A10. The original comparison is not a lexicographic life/health sort.
bool CDirector::TryTakeOverAvailableSurvivor( const CHandle< CTerrorPlayer > &player )
{
	DevMsg( 1, "[TAKEOVER]: %s(%d) is looking for a bot to possess.\n", player->GetPlayerName(), player->GetUserID() );
	if ( IsChangingMapOrMission() && !g_bTerrorRestartAfterDeath && player->IsAwaitingTransitionRestore() )
		return false;

	CUtlVector< CTerrorPlayer * > players;
	CollectTerrorPlayers( players, TEAM_SURVIVOR, 3, 2, 9 );
	SurvivorBot *best = NULL;
	FOR_EACH_VEC( players, i )
	{
		SurvivorBot *candidate = dynamic_cast< SurvivorBot * >( players[i] );
		if ( !candidate || ( candidate->GetHumanSpectatorUserID() != 0 && SurvivorBotHasReservedHuman( candidate ) ) )
			continue;
		if ( !best )
		{
			best = candidate;
			continue;
		}
		if ( ( !best->IsAlive() || candidate->IsAlive() ) && ( best->IsIncapacitated() || !candidate->IsIncapacitated() ) )
		{
			const int bestHealth = best->GetHealth();
			const int candidateHealth = candidate->GetHealth();
			const float candidateTotal = static_cast< float >( static_cast< double >( candidate->GetHealthBuffer() ) + candidateHealth );
			const double bestTotal = static_cast< double >( best->GetHealthBuffer() ) + bestHealth;
			if ( static_cast< double >( candidateTotal ) > bestTotal )
				best = candidate;
		}
	}
	if ( !best )
		return false;
	AssociateHumanWithSurvivorBot( player, best );
	return true;
}

// Native 26CE40. Admission is the SessionManager FIFO consumer, not JoinTeam.
void CDirector::JoinNewPlayer( const CHandle< CTerrorPlayer > &player )
{
	DevMsg( 1, "[TAKEOVER]: Joining new player %s(%d).\n", player->GetPlayerName(), player->GetUserID() );
	int team;
	SurvivorCharacterType character;
	DecodePlayerAvatarInfo( GetPlayerAvatarInfo( player->edict() ), &team, &character );
	if ( AreTeamsFlipped() )
	{
		if ( team == TEAM_SURVIVOR ) team = TEAM_INFECTED;
		else if ( team == TEAM_INFECTED ) team = TEAM_SURVIVOR;
	}

	static ConVarRef survivor_limit( "survivor_limit" );
	bool chooseTeam = team == TEAM_UNASSIGNED;
	if ( !chooseTeam )
	{
		const int count = GetTeamPlayerCount( team, 0 );
		if ( team == TEAM_SURVIVOR ) chooseTeam = count >= survivor_limit.GetInt();
		else if ( team == TEAM_INFECTED ) chooseTeam = !TerrorGameRules()->IsPlayerControlledZombies() || count >= GetMaxSpecialCount();
	}
	if ( chooseTeam )
	{
		if ( TerrorGameRules()->IsPlayerControlledZombies() )
		{
			const int survivors = GetTeamPlayerCount( TEAM_SURVIVOR, 0 );
			const int infected = GetTeamPlayerCount( TEAM_INFECTED, 0 );
			if ( infected <= survivors )
				team = infected == survivors ? 3 - ( RandomInt( 0, 1 ) != 0 ) : TEAM_INFECTED;
			else team = TEAM_SURVIVOR;
		}
		else team = TEAM_SURVIVOR;
	}

	DevMsg( 1, "[TAKEOVER]: New player %s(%d) is joining team %d as character %s.\n",
		player->GetPlayerName(), player->GetUserID(), team, GetResponseCharacterName( character ) );
	if ( team == TEAM_INFECTED )
	{
		const int count = GetTeamPlayerCount( TEAM_INFECTED, 0 );
		if ( TerrorGameRules()->IsPlayerControlledZombies() && count < GetMaxSpecialCount() )
		{
			player->ChangeTeam( TEAM_INFECTED );
			return;
		}
	}
	else if ( team == TEAM_SURVIVOR )
	{
		if ( character != SurvivorCharacter_Unknown )
		{
			CTerrorPlayer *requested = FindSurvivorByCharacter( character );
			if ( requested && requested->IsBot() )
			{
				DevMsg( 1, "[TAKEOVER]: %s(%d) wants to be %s, who is currently a bot.\n",
					player->GetPlayerName(), player->GetUserID(), GetResponseCharacterName( character ) );
				if ( !requested->IsAlive() )
				{
					CUtlVector< CTerrorPlayer * > aliveBots;
					CollectTerrorPlayers( aliveBots, TEAM_SURVIVOR, 2, 2, 9 );
					FOR_EACH_VEC( aliveBots, i )
					{
						SurvivorBot *candidate = dynamic_cast< SurvivorBot * >( aliveBots[i] );
						if ( candidate && candidate->GetHumanSpectatorUserID() == 0 )
						{
							character = candidate->GetSurvivorCharacter();
							break;
						}
					}
				}
			}
			if ( character != SurvivorCharacter_Unknown && TryTakeOverSurvivorCharacter( player, character ) )
				return;
		}
		if ( TryTakeOverAvailableSurvivor( player ) )
			return;
		const int count = GetTeamPlayerCount( TEAM_SURVIVOR, 1 );
		if ( count < survivor_limit.GetInt() )
		{
			player->ChangeTeam( TEAM_SURVIVOR );
			return;
		}
	}
	DevMsg( 1, "[TAKEOVER]: Failed to find a suitable team for %s(%d).\n", player->GetPlayerName(), player->GetUserID() );
	player->ChangeTeam( TEAM_SPECTATOR );
}
