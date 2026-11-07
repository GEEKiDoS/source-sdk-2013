//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_BASE_SNIPERRIFLE_H
#define L4D2_BASE_SNIPERRIFLE_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/weapons/terror_gun.h"

#ifdef CLIENT_DLL
#define CBaseSniperRifle C_BaseSniperRifle
#endif

// The original scoped family derives directly from CTerrorGun, not CBaseRifle.
// FOV/zoom ownership stays on the native player; this class owns no zoom state.
class CBaseSniperRifle : public CTerrorGun
{
public:
	DECLARE_CLASS( CBaseSniperRifle, CTerrorGun );
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();

	virtual bool IsFullyAutomatic() const OVERRIDE { return false; }
	virtual bool HasScope() const OVERRIDE { return true; }
	virtual float GetMaxAutoAimDeflection() OVERRIDE;
	virtual float GetMaxMovementSpread() const OVERRIDE;
	virtual float GetMinStandingSpread() const OVERRIDE;
	virtual float GetMinDuckingSpread() const OVERRIDE;
	virtual bool CanFidget() OVERRIDE;
	virtual acttable_t *ActivityList( int &iActivityCount ) OVERRIDE;
	virtual Activity TranslatePlayerActivity( Activity activity ) OVERRIDE;
	virtual Activity GetPlayerAttackActivity( TerrorPlayerAnimEvent event, Activity mainActivity ) OVERRIDE;
	virtual Activity GetPlayerReloadActivity( TerrorPlayerAnimEvent event, Activity mainActivity ) OVERRIDE;
	virtual Activity TranslateViewModelActivity( Activity activity ) OVERRIDE;
	virtual int GetViewModelLayer( Activity activity ) OVERRIDE;


protected:
	static Activity SniperActivity( const char *pName );
};

#endif // L4D2_BASE_SNIPERRIFLE_H
