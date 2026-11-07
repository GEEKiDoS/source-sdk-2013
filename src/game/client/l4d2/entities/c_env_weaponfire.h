//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_C_ENV_WEAPONFIRE_H
#define L4D2_C_ENV_WEAPONFIRE_H
#ifdef _WIN32
#pragma once
#endif
#include "c_baseentity.h"
class C_EnvWeaponFire : public C_BaseEntity
{
public:
	DECLARE_CLASS( C_EnvWeaponFire, C_BaseEntity );
	DECLARE_CLIENTCLASS();
	DECLARE_PREDICTABLE();
	C_EnvWeaponFire();
	virtual void OnDataChanged( DataUpdateType_t type ) OVERRIDE;
	virtual void ClientThink() OVERRIDE;
private:
	void SelectWeapon();
	void FireEffects();
	int m_iWeaponType;
	int m_iShotsTaken;
	Vector m_vecShotDest;
	int m_iLastShotsTaken;
	int m_nWeaponID;
	int m_nAmmoType;
};
#endif // L4D2_C_ENV_WEAPONFIRE_H
