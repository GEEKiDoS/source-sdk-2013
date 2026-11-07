//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef SURVIVOR_RESCUE_H
#define SURVIVOR_RESCUE_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/sound/flex_expresser.h"
#include "GameEventListener.h"

class CTerrorPlayer;
class CBasePropDoor;


// A rescue spot consumes a survivor selected by its producer. It does not select
// a dead player, create a bot, or stand in for the Director's closet policy.
class CSurvivorRescue : public CFlexExpresser, public CGameEventListener
{
public:
	DECLARE_CLASS( CSurvivorRescue, CFlexExpresser );
	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();

	CSurvivorRescue();
	virtual void Spawn() OVERRIDE;
	virtual void Precache() OVERRIDE;
	virtual void UpdateOnRemove() OVERRIDE;
	virtual int UpdateTransmitState() OVERRIDE;
	virtual int ObjectCaps() OVERRIDE;
	virtual Vector EyePosition() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;

	void SetRescueTarget( CTerrorPlayer *survivor );
	CTerrorPlayer *GetRescueTarget();
	bool IsCallingForRescue();
	int GetRescueCount() const { return m_rescueCount; }
	float GetFlowDistance() const { return m_flowDistance; }

	// Native door association is produced by the actual nav-area scan. These
	// services also let the real selector close an existing closet before use.
	void AssociateDoor( CBasePropDoor *door );
	bool HasClosedDoor() const;
	bool CloseDoors();

	// Called by CBasePropDoor::DoorOpen after its open/opening rejection and
	// before starting movement or firing OnOpen. Attempts to use a locked door
	// must never enter this callback.
	static void OnDoorOpened( CBaseEntity *door, CBaseEntity *activator );

	void InputRescue( inputdata_t &inputdata );
	void YellThink();
	void RescueThink();
	void AreaScanThink();

protected:
	virtual void ModifyOrAppendCriteria( AI_CriteriaSet &criteria ) OVERRIDE;

private:
	void RefreshRescueTarget();
	void ClearRescueThinks();
	bool OwnsDoor( const CBaseEntity *door ) const;

	CNetworkHandle( CTerrorPlayer, m_survivor );
	Vector m_rescueEyePos;
	float m_flowDistance;
	int m_rescueCount;
	CUtlVector< CHandle< CBasePropDoor > > m_rescueDoors;
};

CSurvivorRescue *GetTerrorRescueSpawnOverride(); // S1084B8D8 synchronous override.

#endif // SURVIVOR_RESCUE_H
