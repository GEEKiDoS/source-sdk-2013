//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_BASE_BACKPACK_ITEM_H
#define L4D2_BASE_BACKPACK_ITEM_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/weapons/terror_weapon.h"

#ifdef CLIENT_DLL
#define CBaseBackpackItem C_BaseBackpackItem
#endif

// Original CBaseBackpackItem, server vtable 0x6BE344 / client 0x5EC1AC.
// Action ownership remains on CTerrorPlayer; this item owns only its hold timer.
class CBaseBackpackItem : public CTerrorWeapon
{
public:
	DECLARE_CLASS( CBaseBackpackItem, CTerrorWeapon );
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif

	CBaseBackpackItem();
	virtual void Spawn() OVERRIDE;
	virtual void ItemPostFrame() OVERRIDE;
	virtual void WeaponIdle() OVERRIDE;
#ifdef CLIENT_DLL
	// Backpack item selection depends on ownership, not reserve ammo.
	virtual bool CanBeSelected() OVERRIDE
	{
		return true;
	}
#endif
	virtual void PrimaryAttack() OVERRIDE;
	virtual void SecondaryAttack() OVERRIDE;
	virtual bool Holster( CBaseCombatWeapon *pSwitchingTo = NULL ) OVERRIDE;
#ifndef CLIENT_DLL
	virtual void Drop( const Vector &vecVelocity ) OVERRIDE;
#endif
	virtual bool SendWeaponAnim( int activity ) OVERRIDE;
	virtual bool CanDisplayHelpingHand() const OVERRIDE { return !m_bPerformingAction && BaseClass::CanDisplayHelpingHand(); }
	virtual acttable_t *ActivityList( int &iActivityCount ) OVERRIDE;
	virtual Activity GetPlayerAttackActivity( TerrorPlayerAnimEvent event, Activity mainActivity ) OVERRIDE;
	virtual Activity GetPlayerReloadActivity( TerrorPlayerAnimEvent event, Activity mainActivity ) OVERRIDE;
	virtual Activity GetPlayerDeployActivity( TerrorPlayerAnimEvent event, Activity mainActivity ) OVERRIDE;

	virtual CBaseEntity *FindActionTarget( CTerrorPlayer *pPlayer, int mode );
	virtual bool CanPerformAction( int mode, CTerrorPlayer *pPlayer, CBaseEntity *pTarget );
	virtual bool StartAction( int mode, int buttons );
#ifdef CLIENT_DLL
	virtual void OnActionStarted( int, CTerrorPlayer *, CBaseEntity *, float ) {} // Native C2F0E00 null.
	virtual float GetActionDuration() const { return 0.0f; } // Native C2F0E10 zero.
#else
	virtual void OnActionStarted( int mode, CTerrorPlayer *pPlayer, CBaseEntity *pTarget, float duration ) = 0;
	virtual float GetActionDuration() const = 0;
#endif
#ifndef CLIENT_DLL
	virtual int GetActionCode() const = 0;
#endif
	virtual int GetActionButtons( int mode ) const;
	virtual bool CanContinueAction( CTerrorPlayer *pPlayer, CBaseEntity *pTarget, int buttons );
#ifndef CLIENT_DLL
	virtual void OnActionComplete( CTerrorPlayer *pPlayer, CBaseEntity *pTarget ) = 0;
	virtual void OnActionStopped( CTerrorPlayer *pPlayer ) = 0;
#endif

	bool UseAsPrimary();
	virtual bool IsPerformingAction() const OVERRIDE { return m_bPerformingAction; }
	void CompleteAction();
	void StopAction();
	void ExtendAction();

	CNetworkVar( bool, m_bPerformingAction );

protected:
	void UpdateAction( int buttons );
	CountdownTimer m_actionTimer;
	int m_actionMode;
};

#endif // L4D2_BASE_BACKPACK_ITEM_H
