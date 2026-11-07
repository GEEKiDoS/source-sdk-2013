//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_BASE_SHOTGUN_H
#define L4D2_BASE_SHOTGUN_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/weapons/terror_gun.h"

#ifdef CLIENT_DLL
#define CBaseShotgun C_BaseShotgun
#endif

class CBaseShotgun : public CTerrorGun
{
public:
	DECLARE_CLASS( CBaseShotgun, CTerrorGun );
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();

	CBaseShotgun();
	virtual bool ReloadsSingly() const OVERRIDE { return true; }
	virtual void ItemBusyFrame() OVERRIDE;
	virtual void WeaponIdle() OVERRIDE;
	virtual bool Reload() OVERRIDE;
	virtual void CheckReload() OVERRIDE;
	virtual void FinishReload() OVERRIDE;
	virtual void AbortReload() OVERRIDE;
	virtual bool Holster( CBaseCombatWeapon *pSwitchingTo = NULL ) OVERRIDE;
	virtual bool IsFullyAutomatic() const OVERRIDE { return false; }
	virtual double GetReloadAnimationScale() OVERRIDE;
	virtual Activity TranslateViewModelActivity( Activity activity ) OVERRIDE;
	virtual acttable_t *ActivityList( int &iActivityCount ) OVERRIDE;
	virtual Activity GetPlayerAttackActivity( TerrorPlayerAnimEvent event, Activity mainActivity ) OVERRIDE;
	virtual Activity GetPlayerReloadActivity( TerrorPlayerAnimEvent event, Activity mainActivity ) OVERRIDE;
	virtual Activity GetPlayerDeployActivity( TerrorPlayerAnimEvent event, Activity mainActivity ) OVERRIDE;

	CNetworkVar( int, m_reloadState );
	CNetworkVar( int, m_reloadAnimState );
	CNetworkVar( int, m_reloadNumShells );
	CNetworkVar( int, m_shellsInserted );
	CNetworkVar( float, m_reloadStartTime );
	CNetworkVar( float, m_reloadStartDuration );
	CNetworkVar( float, m_reloadInsertDuration );
	CNetworkVar( float, m_reloadEndDuration );

protected:
	virtual void UpdateReloadQueue() OVERRIDE;
	void PlayReloadStage( Activity activity, TerrorPlayerAnimEvent event, int nShells = 0 );
	void SetReloadAttackDelay( float flDuration );
};

#endif // L4D2_BASE_SHOTGUN_H
