//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_BASE_AUTOSHOTGUN_H
#define L4D2_BASE_AUTOSHOTGUN_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/weapons/base_shotgun.h"

#ifdef CLIENT_DLL
#define CBaseAutoShotgun C_BaseAutoShotgun
#endif

class CBaseAutoShotgun : public CBaseShotgun
{
public:
	DECLARE_CLASS( CBaseAutoShotgun, CBaseShotgun );
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();

	virtual void PrimaryAttack() OVERRIDE;
	virtual bool IsFullyAutomatic() const OVERRIDE { return true; }
	virtual double GetFireRate() OVERRIDE;
	virtual acttable_t *ActivityList( int &iActivityCount ) OVERRIDE;
	virtual Activity GetPlayerAttackActivity( TerrorPlayerAnimEvent event, Activity mainActivity ) OVERRIDE;
	virtual Activity GetPlayerReloadActivity( TerrorPlayerAnimEvent event, Activity mainActivity ) OVERRIDE;
};

#endif // L4D2_BASE_AUTOSHOTGUN_H
