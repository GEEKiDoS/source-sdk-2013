//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Counter-Strike rules layer used by Terror.
//
//=============================================================================//

#ifndef CS_GAMERULES_H
#define CS_GAMERULES_H
#ifdef _WIN32
#pragma once
#endif

#include "teamplay_gamerules.h"
#ifndef CLIENT_DLL
#include "ehandle.h"
class CCSPlayer;
#endif

#ifdef CLIENT_DLL
#define CCSGameRules C_CSGameRules
#define CCSGameRulesProxy C_CSGameRulesProxy
#endif

class CCSGameRulesProxy : public CGameRulesProxy
{
public:
	DECLARE_CLASS( CCSGameRulesProxy, CGameRulesProxy );
	DECLARE_NETWORKCLASS();
};

class CCSGameRules : public CTeamplayRules
{
public:
	DECLARE_CLASS( CCSGameRules, CTeamplayRules );
#ifdef CLIENT_DLL
	DECLARE_CLIENTCLASS_NOBASE();
#else
	DECLARE_SERVERCLASS_NOBASE();
#endif

	CCSGameRules();
	virtual ~CCSGameRules();
	bool ShouldCollide( int collisionGroup0, int collisionGroup1 ) OVERRIDE;

	bool IsFreezePeriod() const { return m_bFreezePeriod; }
	int GetRoundTime() const { return m_iRoundTime; }
	float GetRoundStartTime() const { return m_fLevelStartTime; }
	float GetGameStartTime() const { return m_flGameStartTime; }

#ifndef CLIENT_DLL
	virtual void Think() OVERRIDE;
	virtual bool ClientCommand( CBaseEntity *pEdict, const CCommand &args ) OVERRIDE;
	virtual void ClientSettingsChanged( CBasePlayer *player ) OVERRIDE;
	virtual void ClientDisconnected( edict_t *pClient ) OVERRIDE;
	virtual void EndGameFrame() OVERRIDE;
	virtual bool IsSpawnPointValid( CBaseEntity *spot, CBasePlayer *player ) OVERRIDE;
	virtual bool FShouldSwitchWeapon( CBasePlayer *player, CBaseCombatWeapon *weapon ) OVERRIDE;
	virtual bool FlPlayerFallDeathDoesScreenFade( CBasePlayer *player ) OVERRIDE;
	virtual void SetAllowWeaponSwitch( bool allow );
	virtual bool IsWeaponSwitchAllowed() const;

protected:
	void CheckFreezePeriod();
	void ReadMultiplayCvars();
	void CheckMapConditions();
	void UpdatePeriodicRoundThink();
	CHandle< CCSPlayer > m_hVIP;
	bool m_bLevelInitialized;
	int m_iRoundWinStatus;
	int m_iTotalRoundsPlayed;
	float m_flRestartRoundTime;
	int m_iNumTerrorist;
	int m_iNumCT;
	int m_iNumSpawnableTerrorist;
	int m_iNumSpawnableCT;
	bool m_bGameStarted;
	int m_iAccountTerrorist;
	int m_iAccountCT;
	short m_iNumCTWins;
	short m_iNumTerroristWins;
	int m_iNumSpawnTerrorist;
	int m_iNumSpawnCT;
	float m_flNextPeriodicThink;
	bool m_bMapHasEscapeZone;
	int m_iVIPMapState;
	bool m_bTargetBombed;
	bool m_bBombDefused;
	bool m_bSuppressBombMapTimeout;
	bool m_bAllowWeaponSwitch;
#endif

protected:
#ifdef CLIENT_DLL
	bool m_bFreezePeriod;
	int m_iRoundTime;
	float m_fLevelStartTime;
	float m_flGameStartTime;
	int m_iHostagesRemaining;
	bool m_bMapHasBombTarget;
	bool m_bMapHasRescueZone;
	bool m_bLogoMap;
	bool m_bBlackMarket;
#else
	CNetworkVar( bool, m_bFreezePeriod );
	CNetworkVar( int, m_iRoundTime );
	CNetworkVar( float, m_fLevelStartTime );
	CNetworkVar( float, m_flGameStartTime );
	CNetworkVar( int, m_iHostagesRemaining );
	CNetworkVar( bool, m_bMapHasBombTarget );
	CNetworkVar( bool, m_bMapHasRescueZone );
	CNetworkVar( bool, m_bLogoMap );
	CNetworkVar( bool, m_bBlackMarket );
#endif
};

inline CCSGameRules *CSGameRules()
{
	return static_cast<CCSGameRules *>( g_pGameRules );
}

#endif // CS_GAMERULES_H
