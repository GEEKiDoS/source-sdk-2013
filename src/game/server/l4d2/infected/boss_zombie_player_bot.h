//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_BOSS_ZOMBIE_PLAYER_BOT_H
#define L4D2_BOSS_ZOMBIE_PLAYER_BOT_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/player/terrorplayer.h"
#include "NextBot/Player/NextBotPlayer.h"

// Retail's player-backed infected base. Components and behavior belong to the
// concrete Boomer, Smoker, Hunter, Spitter, Jockey, Charger and Tank classes.
class BossZombiePlayerBot : public NextBotPlayer< CTerrorPlayer >
{
public:
	DECLARE_CLASS( BossZombiePlayerBot, NextBotPlayer< CTerrorPlayer > );

	BossZombiePlayerBot();
	virtual void Spawn() OVERRIDE;
	virtual void PhysicsSimulate() OVERRIDE;
	virtual void Update() OVERRIDE;
	virtual void ChangeTeam( int team ) OVERRIDE;
	virtual void OnPreThinkObserverMode() OVERRIDE;
	virtual void OnLeaveActive() OVERRIDE;

	virtual CBaseCombatCharacter *GetEntity() const OVERRIDE;
	virtual bool ShouldTouch( const CBaseEntity *object ) const OVERRIDE;
	virtual bool IsDebugFilterMatch( const char *name ) const OVERRIDE;
	virtual const char *GetDebugIdentifier() const OVERRIDE;
	virtual bool IsAggressive() const;
	virtual bool ShouldRetreatToCover() const;

	CTerrorPlayer *SelectVictim( CTerrorPlayer *current = NULL, int flags = 0, CTerrorPlayer *ignore = NULL );
	bool IsVictimFarAhead( CTerrorPlayer *victim ) const;
	bool SetHumanSpectator( CTerrorPlayer *human );
	void ClearHumanSpectator();
	int GetHumanSpectatorUserID() const { return m_humanSpectatorUserID; }

private:
	CNetworkVar( int, m_humanSpectatorUserID );
	CountdownTimer m_victimSelectionTimer;
};

CTerrorPlayer *FindClosestBossSurvivor( const Vector &position,
	bool includeImmobile, bool ignoreRescueVehicle = false, CBaseEntity *ignore = NULL );
#endif // L4D2_BOSS_ZOMBIE_PLAYER_BOT_H
