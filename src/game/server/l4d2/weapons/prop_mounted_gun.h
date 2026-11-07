//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef PROP_MOUNTED_GUN_H
#define PROP_MOUNTED_GUN_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/weapons/base_mounted_weapon.h"
#include "util_shared.h"

// Both prop_minigun and prop_mounted_machine_gun create this .50-caliber prop.
class CPropMountedGun : public CBaseMountedWeapon
{
public:
	DECLARE_CLASS( CPropMountedGun, CBaseMountedWeapon );
	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();

	CPropMountedGun();
	virtual void Spawn() OVERRIDE;
	virtual void Precache() OVERRIDE;
	virtual void Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value ) OVERRIDE;

	void AngleThink();
	void FireThink();

private:
	void UpdateHeat( float interval );
	void Fire( CTerrorPlayer *pPlayer, const Vector &origin, const Vector &direction, bool hasAim );
	void OnFireStart();
	void OnFireStop();
	void FireOverheatedEvent();

	CountdownTimer m_mountCooldown;
	int m_shellAttachment;
	int m_muzzleAttachment;
	COutputEvent m_OnFireStart;
	COutputEvent m_OnFireStop;
};

#endif // PROP_MOUNTED_GUN_H
