//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_BASE_BELT_ITEM_H
#define L4D2_BASE_BELT_ITEM_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/weapons/terror_weapon.h"

#ifdef CLIENT_DLL
#define CBaseBeltItem C_BaseBeltItem
#endif

// Pills and adrenaline have their own completion timer, not a backpack/use action.
class CBaseBeltItem : public CTerrorWeapon
{
public:
	DECLARE_CLASS( CBaseBeltItem, CTerrorWeapon );
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif

	CBaseBeltItem();
	virtual void Spawn() OVERRIDE;
	virtual bool Deploy() OVERRIDE;
	virtual bool Holster( CBaseCombatWeapon *pSwitchingTo = NULL ) OVERRIDE;
	virtual bool SendWeaponAnim( int activity ) OVERRIDE;
	virtual void PrimaryAttack() OVERRIDE;
	virtual void SecondaryAttack() OVERRIDE;
	virtual void ItemPostFrame() OVERRIDE;
	virtual void WeaponIdle() OVERRIDE {}
	virtual bool CanBeSelected() OVERRIDE { return true; }
	virtual bool HasPrimaryAmmo() OVERRIDE { return true; }
	virtual bool CanFidget() OVERRIDE { return false; }
	virtual bool CanDisplayHelpingHand() const OVERRIDE;
	virtual acttable_t *ActivityList( int &iActivityCount ) OVERRIDE;
	virtual Activity GetPlayerAttackActivity( TerrorPlayerAnimEvent event, Activity mainActivity ) OVERRIDE;
	virtual Activity GetPlayerReloadActivity( TerrorPlayerAnimEvent event, Activity mainActivity ) OVERRIDE;
	virtual Activity GetPlayerDeployActivity( TerrorPlayerAnimEvent event, Activity mainActivity ) OVERRIDE;

protected:
	virtual bool CanConsume( CTerrorPlayer *pPlayer ) { return true; }
	virtual double GetConsumeDuration();
#ifndef CLIENT_DLL
	virtual void OnConsumeStarted( CTerrorPlayer *pPlayer ) = 0;
	virtual void ApplyConsumable( CTerrorPlayer *pPlayer ) = 0;
#endif
	static Activity BeltActivity( const char *pName );

	// Neither DT_BaseBeltItem nor its prediction map contains this timer.
	CNetworkVarEmbedded( CountdownTimer, m_UseTimer );
};

#endif // L4D2_BASE_BELT_ITEM_H
