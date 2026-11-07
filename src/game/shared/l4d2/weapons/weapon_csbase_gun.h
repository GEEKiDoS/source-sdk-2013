//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_WEAPON_CSBASE_GUN_H
#define L4D2_WEAPON_CSBASE_GUN_H
#ifdef _WIN32
#pragma once
#endif
#include "l4d2/weapons/terror_weapon.h"
#ifdef CLIENT_DLL
#define CWeaponCSBaseGun C_WeaponCSBaseGun
#endif

// Native sibling of CTerrorGun, NOT its parent, alias or fallback firearm.
class CWeaponCSBaseGun : public CTerrorWeapon
{
public:
	DECLARE_CLASS( CWeaponCSBaseGun, CTerrorWeapon );
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();
	virtual void Spawn() OVERRIDE;
	virtual bool Deploy() OVERRIDE;
	virtual void ItemPostFrame() OVERRIDE;
	virtual void WeaponIdle() OVERRIDE;
	virtual bool Reload() OVERRIDE;
	virtual void SecondaryAttack() OVERRIDE;
	virtual double GetFireRate() OVERRIDE;
	virtual void UpdateAccuracy();
	virtual void DoFireEffects();
private:
	float m_flZoomFullyActiveTime; // S6104/C3304; no constructor store/wire/SAVE/PRED.
};
#endif // L4D2_WEAPON_CSBASE_GUN_H
