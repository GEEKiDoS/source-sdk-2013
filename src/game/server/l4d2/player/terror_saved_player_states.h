//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_SAVED_PLAYER_STATES_H
#define TERROR_SAVED_PLAYER_STATES_H
#ifdef _WIN32
#pragma once
#endif
#include "tier1/KeyValues.h"
#include "tier1/utlvector.h"

// Concrete wrapper for the original global saved-player KV vector8494E0
// (size8494EC), reached by player helpers2B6A20/2B6C40/2B7E20.
// This is NOT a native RTTI class or Director+1588 admission/session owner.
class CTerrorPlayer;
class Vector;
class CSavedTerrorPlayerStates
{
public:
	~CSavedTerrorPlayerStates();
	void ClearTransitionRecords();
	void ClearSavedPlayers();
	void AddTransitionRecord( KeyValues *playerData, const char *targetMap );
	void AddSurvivorBotRecord( KeyValues *playerData, const char *targetMap );
	void ArchiveRestoredSurvivorBots();
	int GetPlayerCount() const { return m_SavedPlayers.Count(); }
	KeyValues *GetPlayerData( int index ) const { return m_SavedPlayers[index]; }
	int GetSurvivorBotCount() const { return m_SavedBots.Count(); }
	KeyValues *GetSurvivorBotData( int index ) const { return m_SavedBots[index]; }
	void RemovePlayerByUserID( int userID );
	void GetRestoringPlayerCounts( int team, int *players, int *restoring ) const;
	KeyValues *BeginPlayerRestore( int userID, const char *mapName );
	KeyValues *FindPlayerRestore( int userID, int team, int character, bool matchCharacter ) const;
	void MarkRestoreHealthApplied( KeyValues *playerData );
	void MarkPlayerRestoreDone( int userID );
	bool HasPendingSurvivorCharacter( int character, int excludedIndex ) const;

private:
	CUtlVector< KeyValues * > m_SavedPlayers;
	CUtlVector< KeyValues * > m_SavedBots;
	CUtlVector< KeyValues * > m_RestoredBots;
};
extern CSavedTerrorPlayerStates g_SavedTerrorPlayerStates;
bool FindNearbyTransitionSpawn( CTerrorPlayer *player, Vector *position, int team, bool ducked, float range );
#endif
