//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/terror_saved_player_states.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/terror_shareddefs.h"
#include "l4d2/player/transitioning_player.h"
#include "tier0/memdbgon.h"

CSavedTerrorPlayerStates g_SavedTerrorPlayerStates;
namespace
{
void DeleteSavedRecords( CUtlVector< KeyValues * > &records )
{
	for ( int i = records.Count() - 1; i >= 0; --i )
		records[i]->deleteThis();
	records.Purge();
}
}
CSavedTerrorPlayerStates::~CSavedTerrorPlayerStates()
{
	ClearTransitionRecords();
}
void CSavedTerrorPlayerStates::ClearTransitionRecords()
{
	// S2B7895 clears the existing observer registry count BEFORE any KV clear.
	// This neither deletes its actors nor purges the pointer-vector storage.
	CTransitioningPlayer::ResetTransitioningPlayerRegistry();
	// S:2B7890. These are three distinct native vectors; bot archive records
	// participate in character reassociation, never restoring-player counts.
	ClearSavedPlayers();
	DeleteSavedRecords( m_SavedBots );
	DeleteSavedRecords( m_RestoredBots );
}
void CSavedTerrorPlayerStates::ClearSavedPlayers()
{
	DeleteSavedRecords( m_SavedPlayers );
}

// S:2B7E20 appends a COPY, including duplicate userIDs. Its caller supplies
// the real PlayerSaveData(2B6C40) result. Never replace it with a health/inventory
// snapshot or with a connected-player EHANDLE.
void CSavedTerrorPlayerStates::AddTransitionRecord( KeyValues *playerData, const char *targetMap )
{
	playerData->SetString( "targetMap", targetMap );
	m_SavedPlayers.AddToTail( playerData->MakeCopy() );
}
void CSavedTerrorPlayerStates::AddSurvivorBotRecord( KeyValues *playerData, const char *targetMap )
{
	playerData->SetString( "targetMap", targetMap );
	m_SavedBots.AddToTail( playerData->MakeCopy() );
}
void CSavedTerrorPlayerStates::ArchiveRestoredSurvivorBots()
{
	// S:2B9CF0 clears the old archive in reverse, copies EVERY attempted
	// record, even when creation was refused/failed, then purges the source.
	if ( !m_SavedBots.Count() )
		return;
	DeleteSavedRecords( m_RestoredBots );
	FOR_EACH_VEC( m_SavedBots, i )
		m_RestoredBots.AddToTail( m_SavedBots[i]->MakeCopy() );
	DeleteSavedRecords( m_SavedBots );
}
void CSavedTerrorPlayerStates::RemovePlayerByUserID( int userID )
{
	// S:2B76E0 removes the FIRST matching pending record, preserving order.
	FOR_EACH_VEC( m_SavedPlayers, i )
	{
		if ( m_SavedPlayers[i]->GetInt( "userID", 0 ) == userID )
		{
			m_SavedPlayers[i]->deleteThis();
			m_SavedPlayers.Remove( i );
			break;
		}
	}
	CTransitioningPlayer::RemoveByUserID( userID );
}
void CSavedTerrorPlayerStates::GetRestoringPlayerCounts( int team, int *players, int *restoring ) const
{
	if ( players ) *players = 0;
	if ( restoring ) *restoring = 0;
	if ( !TheDirector->IsInTransition() )
		return;
	FOR_EACH_VEC( m_SavedPlayers, i )
	{
		KeyValues *data = m_SavedPlayers[i];
		if ( data->GetInt( "teamNumber", 0 ) != team )
			continue;
		if ( players ) ++*players;
		if ( data->GetInt( "restoreState", 0 ) != 0 && restoring ) ++*restoring;
	}
}
KeyValues *CSavedTerrorPlayerStates::BeginPlayerRestore( int userID, const char *mapName )
{
	// S:2BA320 writes state1 BEFORE Spawn and the real2BA070/2B8B60 restore.
	FOR_EACH_VEC( m_SavedPlayers, i )
	{
		KeyValues *data = m_SavedPlayers[i];
		if ( data->GetInt( "userID", 0 ) != userID )
			continue;
		if ( data->GetInt( "restoreState", 0 ) != 0 )
			continue;
		if ( Q_stricmp( data->GetString( "targetMap", "" ), mapName ) )
			continue;
		data->SetInt( "restoreState", 1 );
		data->SetInt( "userID", userID );
		return data;
	}
	return NULL;
}
KeyValues *CSavedTerrorPlayerStates::FindPlayerRestore( int userID, int team, int character, bool matchCharacter ) const
{
	FOR_EACH_VEC( m_SavedPlayers, i )
	{
		KeyValues *data = m_SavedPlayers[i];
		if ( matchCharacter )
		{
			if ( data->GetInt( "teamNumber", 0 ) == team && data->GetInt( "character", 0 ) == character )
				return data;
		}
		else if ( data->GetInt( "userID", 0 ) == userID )
			return data;
	}
	if ( matchCharacter )
	{
		FOR_EACH_VEC( m_RestoredBots, i )
		{
			KeyValues *data = m_RestoredBots[i];
			if ( data->GetInt( "teamNumber", 0 ) == team && data->GetInt( "character", 0 ) == character )
				return data;
		}
	}
	return NULL;
}
void CSavedTerrorPlayerStates::MarkRestoreHealthApplied( KeyValues *playerData )
{
	// S:2B8B60's writer follows health/life/stat restoration, precedes weapons.
	// It is NOT a completion event and does not mean inventory was restored.
	if ( playerData->GetInt( "restoreState", 0 ) == 1 )
		playerData->SetInt( "restoreState", 2 );
}
void CSavedTerrorPlayerStates::MarkPlayerRestoreDone( int userID )
{
	// S:2B6B50, called by the actual transition entity timeout/destructor.
	FOR_EACH_VEC( m_SavedPlayers, i )
	{
		if ( m_SavedPlayers[i]->GetInt( "userID", 0 ) == userID )
		{
			m_SavedPlayers[i]->SetInt( "restoreState", 2 );
			break;
		}
	}
}
bool CSavedTerrorPlayerStates::HasPendingSurvivorCharacter( int character, int excludedIndex ) const
{
	if ( !TheDirector->IsInTransition() )
		return false;
	FOR_EACH_VEC( m_SavedPlayers, i )
	{
		KeyValues *data = m_SavedPlayers[i];
		const int team = data->GetInt( "teamNumber", 0 );
		if ( i != excludedIndex && data->GetInt( "restoreState", 0 ) == 0 &&
			( team == TEAM_SURVIVOR || team == TEAM_L4D1_SURVIVOR ) && data->GetInt( "character", 0 ) == character )
			return true;
	}
	return false;
}

// Native S:2B76E0, registered by S:5979C0. Transition actors queue this actual
// server command; Director player_disconnect must not remove a second record.
CON_COMMAND_F( kick_transitioning, "Kick a transitioning player by userID" , 0x00000004)
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() || args.ArgC() != 2 )
		return;
	g_SavedTerrorPlayerStates.RemovePlayerByUserID( atoi( args[1] ) );
}
