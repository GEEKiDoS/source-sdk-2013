//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef C_TERROR_PLAYER_RESOURCE_H
#define C_TERROR_PLAYER_RESOURCE_H
#ifdef _WIN32
#pragma once
#endif
#include "c_playerresource.h"
class C_TerrorPlayerResource : public C_PlayerResource
{
	DECLARE_CLASS( C_TerrorPlayerResource, C_PlayerResource );
public:
	DECLARE_CLIENTCLASS();
	C_TerrorPlayerResource();
	int GetMaxHealth( int index );
	int GetPrimaryWeapon( int index ) const { return m_primaryWeapon[index]; }
	int GetGrenade( int index ) const { return m_grenade[index]; }
	int GetFirstAidSlot( int index ) const { return m_firstAidSlot[index]; }
	int GetPillsSlot( int index ) const { return m_pillsSlot[index]; }
	bool IsGhost( int index ) const { return m_isGhost[index]; }
	bool IsIncapacitated( int index ) const { return m_isIncapacitated[index]; }
	bool WantsToPlay( int index ) const { return m_wantsToPlay[index]; }
	int GetZombieClass( int index ) const { return m_zombieClass[index]; }
	int GetTankTickets( int index ) const { return m_tankTickets[index]; }
	bool IsListenServerHost( int index ) const { return m_listenServerHost[index]; }
	int GetTeamSwitchRule( int index ) const { return m_teamSwitchRule[index]; }
	float GetSurvivalRecordTime( int index ) const { return m_flSurvivalRecordTime[index]; }
	int GetSurvivalTopMedal( int index ) const { return m_nSurvivalTopMedal[index]; }
	double GetBecomeGhostAt( int index ) const
	{
		return static_cast< unsigned int >( index ) - 1u > 31u ? 0.0 : m_flBecomeGhostAt[index];
	}
	float GetTankLotteryEntryRatio() const { return m_tankLotteryEntryRatio; }
	float GetTankLotterySelectionRatio() const { return m_tankLotterySelectionRatio; }
	int GetPendingTankPlayerIndex() const { return m_pendingTankPlayerIndex; }
	bool IsFinale() const { return m_isFinale; }
	bool IsSurvivorTeamReadyTime() const { return m_isSurvivorTeamReadyTime; }
	int GetSurvivorTeamReadyTime() const { return m_survivorTeamReadyTime; }
	int GetRoundSetupTimeRemaining() const { return m_nRoundSetupTimeRemaining; }
	int GetTempoState() const { return m_tempoState; }
	bool HasAnySurvivorLeftSafeArea() const { return m_hasAnySurvivorLeftSafeArea; }
	bool IsTeamFrozen() const { return m_isTeamFrozen; }
	void SetTeamsFrozen( bool frozen ) { m_isTeamFrozen = frozen; }
	int GetMissionDuration() const { return m_missionDuration; }
	int GetMissionWipes() const { return m_missionWipes; }
	float GetStarRating() const { return m_starRating; }
	int GetSharedRandomSeed() const { return static_cast<int>( FloatBits( m_sharedRandomSeed ) ); }
private:
	int m_maxHealth[MAX_PLAYERS_ARRAY_SAFE];
	int m_primaryWeapon[MAX_PLAYERS_ARRAY_SAFE];
	int m_grenade[MAX_PLAYERS_ARRAY_SAFE];
	int m_firstAidSlot[MAX_PLAYERS_ARRAY_SAFE];
	int m_pillsSlot[MAX_PLAYERS_ARRAY_SAFE];
	bool m_isGhost[MAX_PLAYERS_ARRAY_SAFE];
	bool m_isIncapacitated[MAX_PLAYERS_ARRAY_SAFE];
	bool m_wantsToPlay[MAX_PLAYERS_ARRAY_SAFE];
	int m_zombieClass[MAX_PLAYERS_ARRAY_SAFE];
	int m_tankTickets[MAX_PLAYERS_ARRAY_SAFE];
	bool m_listenServerHost[MAX_PLAYERS_ARRAY_SAFE];
	int m_teamSwitchRule[MAX_PLAYERS_ARRAY_SAFE];
	float m_flSurvivalRecordTime[MAX_PLAYERS_ARRAY_SAFE];
	int m_nSurvivalTopMedal[MAX_PLAYERS_ARRAY_SAFE];
	float m_flBecomeGhostAt[MAX_PLAYERS_ARRAY_SAFE];
	float m_tankLotteryEntryRatio;
	float m_tankLotterySelectionRatio;
	int m_pendingTankPlayerIndex;
	bool m_isFinale;
	bool m_isSurvivorTeamReadyTime;
	int m_survivorTeamReadyTime;
	int m_nRoundSetupTimeRemaining;
	int m_tempoState;
	bool m_hasAnySurvivorLeftSafeArea;
	bool m_isTeamFrozen;
	int m_missionDuration;
	int m_missionWipes;
	float m_starRating; // C4348: constructor3; original server has no send row.
	float m_sharedRandomSeed; // raw Int32 wire, NOT numeric float conversion.
};
#endif
