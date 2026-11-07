//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef DIRECTOR_SESSION_MANAGER_H
#define DIRECTOR_SESSION_MANAGER_H
#ifdef _WIN32
#pragma once
#endif
#include "l4d2/mem_zero_on_new.h"
#include "tier1/utlvector.h"
#include "ehandle.h"
#include "util_shared.h"
class CTerrorPlayer;
class IGameEvent;

// Retail Director+1588. This object is neither the global saved-player KV
// vector nor a rules-owned join queue. Virtual order follows S:672A94.
class CDirectorSessionManager : public CMemZeroOnNew
{
public:
	CDirectorSessionManager();
	virtual void OnMapStart();                         // S:28CF00
	virtual void OnRoundRestart();                     // S:28C7F0
	virtual bool Update();                             // S:28DAE0
	virtual void UpdateReadyState();                   // S:28CA20
	virtual void OnLevelShutdown();                    // S:28CDC0
	virtual void OnAllPlayersConnected();              // S:28CF10
	virtual void OnSurvivorLeftSafeArea( CTerrorPlayer *player ); // S:28C800
	virtual void OnClientConnected( CTerrorPlayer *player );     // S:28D870
	virtual void OnClientDisconnected( CTerrorPlayer *player );  // S:28D240
	virtual int DrawDebugText( int line );              // S:28CDD0

	void Reset();                                     // S:28C8D0
	void FireGameEvent( IGameEvent *event );            // S:28D8F0, Director forwards
	void OnFullConnect( CTerrorPlayer *player );
	bool IsStartAreaExitBlocked() const { return !m_bSurvivorsReady; } // S:28C780
	int GetSurvivorReadyTime() const;                   // S:28C810; truncates, no clamp
	bool IsLowViolence() const { return m_bLowViolence; } // Native+55 aggregate, not local engine policy.
	bool IsTeamFrozen() const { return m_bTeamFrozen; }
	bool IsLevelActive() const { return m_bLevelActive; }
	void StartReadyCountdownNow();                     // S:28CB90
	bool IsUserIDFullyConnected( int userID ) const { return m_FullyConnectedUserIDs.Find( userID ) != m_FullyConnectedUserIDs.InvalidIndex(); }

private:
	struct Admission
	{
		CHandle< CTerrorPlayer > player;
		float connectedAt;
	};
	void FillSurvivorBotRoster();                      // S:28C790
	void ProcessAdmissions();                          // S:28D060
	void ProcessDisconnects();                         // S:28D440
	bool UpdateConnectingPlayers();                    // S:28D500
	void UnfreezePlayers();                            // S:28D6F0
	void UpdateLowViolence();                          // S:28DA30
	void UpdateIdlePlayers();                          // S:28D170
	void CheckIdlePlayer( CTerrorPlayer *player, bool *allIdle ); // S:28CC00

	CUtlVector< int > m_FullyConnectedUserIDs;          // original +4
	bool m_bSurvivorsReady;                            // +24, NOT Director+352
	CountdownTimer m_ReadyTimer;                       // +28
	bool m_bTeamFrozen;                                // +40
	float m_flUnfreezeTime;                            // +44
	float m_flConnectTimeout;                          // +48
	bool m_bLevelActive;                               // +52
	bool m_bRestoredSavedPlayers;                      // +53
	bool m_bCheckDisconnectedPlayers;                  // +54
	bool m_bLowViolence;                               // +55
	CUtlVector< Admission > m_Admissions;              // +56, ordered/non-deduplicated
};
#endif
