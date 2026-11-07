//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_BASE_RIFLE_H
#define L4D2_BASE_RIFLE_H
#ifdef _WIN32
#pragma once
#endif
#include "l4d2/weapons/terror_gun.h"
#ifdef CLIENT_DLL
#define CBaseRifle C_BaseRifle
#endif

class CBaseRifle : public CTerrorGun
{
public:
	DECLARE_CLASS( CBaseRifle, CTerrorGun );
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();
	virtual acttable_t *ActivityList( int &iActivityCount ) OVERRIDE;
	virtual Activity GetPlayerAttackActivity( TerrorPlayerAnimEvent event, Activity mainActivity ) OVERRIDE;
	virtual Activity GetPlayerReloadActivity( TerrorPlayerAnimEvent event, Activity mainActivity ) OVERRIDE;
	virtual Activity TranslateViewModelActivity( Activity activity ) OVERRIDE;
};
#endif // L4D2_BASE_RIFLE_H
