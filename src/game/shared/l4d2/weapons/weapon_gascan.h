//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_WEAPON_GASCAN_H
#define L4D2_WEAPON_GASCAN_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/weapons/weapon_carried_item.h"
#ifdef CLIENT_DLL
#define CGasCan C_GasCan
#else
#include "entityoutput.h"
#endif

class CGasCan : public CCarriedPropWeapon
{
public:
	DECLARE_CLASS( CGasCan, CCarriedPropWeapon );
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();
	CGasCan();
	virtual void Spawn() OVERRIDE;
	virtual void ItemPostFrame() OVERRIDE;
	virtual int GetWeaponID() const OVERRIDE { return TERROR_WEAPON_GASCAN; }
	virtual bool IsPerformingAction() const OVERRIDE { return false; } // S3CE0A0/C2FB630.
#ifndef CLIENT_DLL
	// S3CE0B0.
	virtual int GetUsePriority( CBaseEntity *user ) OVERRIDE
	{
		return 2;
	}
	virtual float GetDropDistance() const OVERRIDE;
#endif
	virtual CBaseEntity *FindActionTarget( CTerrorPlayer *pPlayer, int mode ) OVERRIDE;
	virtual bool CanPerformAction( int mode, CTerrorPlayer *pPlayer, CBaseEntity *pTarget ) OVERRIDE;
	virtual bool StartAction( int mode, int buttons ) OVERRIDE;
	virtual void OnActionStarted( int mode, CTerrorPlayer *pPlayer, CBaseEntity *pTarget, float duration ) OVERRIDE;
	virtual bool CanContinueAction( CTerrorPlayer *pPlayer, CBaseEntity *pTarget, int buttons ) OVERRIDE;
#ifndef CLIENT_DLL
	virtual void OnActionComplete( CTerrorPlayer *pPlayer, CBaseEntity *pTarget ) OVERRIDE;
	virtual void OnActionStopped( CTerrorPlayer *pPlayer ) OVERRIDE;
#endif
	virtual float GetActionDuration() const OVERRIDE;
#ifndef CLIENT_DLL
	virtual int GetActionCode() const OVERRIDE { return 8; }
#endif
	virtual const char *GetCarriedPropModel() const OVERRIDE { return "models/props_junk/gascan001a.mdl"; }
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
	virtual void Equip( CBaseCombatCharacter *pOwner ) OVERRIDE;
	virtual void SetWeaponSpawn( CBaseEntity *pSpawn ) OVERRIDE;
	virtual bool DropCarriedItem( bool bThrow ) OVERRIDE;
	virtual int OnTakeDamage( const CTakeDamageInfo &info ) OVERRIDE;
	virtual void Event_Killed( const CTakeDamageInfo &info ) OVERRIDE;
	virtual void Ignite( float flFlameLifetime, bool bNPCOnly = true, float flSize = 0.0f, bool bCalledByLevelDesigner = false ) OVERRIDE;
	bool IsVulnerableToSpit() const { return m_bVulnerableToSpit; }
	void ClearDetonateTimer();
private:
	int GasCansRemainingToWin() const;
	bool m_bBlockedDrop;
	bool m_bHasBeenPickedUp;
	bool m_bVulnerableToSpit;
	float m_flSpawnTime;
	float m_flLastNoiseTime;
	float m_flSpitDetonateTime;
	COutputEvent m_OnKilled;
#endif
};

#endif // L4D2_WEAPON_GASCAN_H
