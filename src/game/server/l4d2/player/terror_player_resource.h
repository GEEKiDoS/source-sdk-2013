//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_PLAYER_RESOURCE_H
#define TERROR_PLAYER_RESOURCE_H
#ifdef _WIN32
#pragma once
#endif
#include "player_resource.h"
class CTerrorPlayerResource : public CPlayerResource
{
	DECLARE_CLASS( CTerrorPlayerResource, CPlayerResource );
	DECLARE_DATADESC();
public:
	DECLARE_SERVERCLASS();
	virtual ~CTerrorPlayerResource();
	virtual void Spawn() OVERRIDE;
	virtual void UpdatePlayerData() OVERRIDE;
	void SetBecomeGhostAt( int index, float deadline ); // S343310, actual ghost producer.
	void SetTankTickets( int index, int tickets ); // S343250, preserves raw signed tickets.
private:
	CNetworkArray( int, m_maxHealth, MAX_PLAYERS_ARRAY_SAFE );
	CNetworkArray( int, m_primaryWeapon, MAX_PLAYERS_ARRAY_SAFE );
	CNetworkArray( int, m_grenade, MAX_PLAYERS_ARRAY_SAFE );
	CNetworkArray( int, m_firstAidSlot, MAX_PLAYERS_ARRAY_SAFE );
	CNetworkArray( int, m_pillsSlot, MAX_PLAYERS_ARRAY_SAFE );
	CNetworkArray( bool, m_isGhost, MAX_PLAYERS_ARRAY_SAFE );
	CNetworkArray( bool, m_isIncapacitated, MAX_PLAYERS_ARRAY_SAFE );
	CNetworkArray( bool, m_wantsToPlay, MAX_PLAYERS_ARRAY_SAFE );
	CNetworkArray( int, m_zombieClass, MAX_PLAYERS_ARRAY_SAFE );
	CNetworkArray( int, m_tankTickets, MAX_PLAYERS_ARRAY_SAFE );
	CNetworkArray( bool, m_listenServerHost, MAX_PLAYERS_ARRAY_SAFE );
	CNetworkArray( int, m_TeamSwitchRule, MAX_PLAYERS_ARRAY_SAFE );
	CNetworkArray( float, m_flSurvivalRecordTime, MAX_PLAYERS_ARRAY_SAFE );
	CNetworkArray( int, m_nSurvivalTopMedal, MAX_PLAYERS_ARRAY_SAFE );
	CNetworkArray( float, m_flBecomeGhostAt, MAX_PLAYERS_ARRAY_SAFE );
	CNetworkVar( float, m_tankLotteryEntryRatio );
	CNetworkVar( float, m_tankLotterySelectionRatio );
	CNetworkVar( int, m_pendingTankPlayerIndex );
	CNetworkVar( bool, m_isFinale );
	CNetworkVar( bool, m_isSurvivorTeamReadyTime );
	CNetworkVar( int, m_survivorTeamReadyTime );
	CNetworkVar( int, m_nRoundSetupTimeRemaining );
	CNetworkVar( int, m_tempoState );
	CNetworkVar( bool, m_hasAnySurvivorLeftSafeArea );
	CNetworkVar( bool, m_isTeamFrozen );
	CNetworkVar( int, m_missionDuration );
	CNetworkVar( int, m_missionWipes );
	CNetworkVar( float, m_sharedRandomSeed );
};
#endif
