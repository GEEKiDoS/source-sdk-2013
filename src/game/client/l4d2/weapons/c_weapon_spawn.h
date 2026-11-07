//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_C_WEAPON_SPAWN_H
#define L4D2_C_WEAPON_SPAWN_H
#ifdef _WIN32
#pragma once
#endif

#include "c_baseanimating.h"

class C_WeaponSpawn : public C_BaseAnimating
{
public:
	DECLARE_CLASS( C_WeaponSpawn, C_BaseAnimating );
	DECLARE_CLIENTCLASS();

	virtual int ObjectCaps() OVERRIDE;
	virtual ShadowType_t ShadowCastType() OVERRIDE { return SHADOWS_NONE; }
	virtual bool IsUsable( C_BaseEntity *pUser );
	virtual int GetUseType( C_BaseEntity *pUser ) OVERRIDE;
	virtual int GetWeaponID() const { return m_weaponID; }
	virtual CStudioHdr *OnNewModel() OVERRIDE;

	float GetGlowRange() const { return m_flGlowRange; }

protected:
	int m_weaponID;
	float m_flGlowRange;
};

class C_WeaponAmmoSpawn : public C_WeaponSpawn
{
public:
	DECLARE_CLASS( C_WeaponAmmoSpawn, C_WeaponSpawn );
	DECLARE_CLIENTCLASS();
	virtual int ObjectCaps() OVERRIDE;
};

class C_WeaponScavengeItemSpawn : public C_WeaponSpawn
{
public:
	DECLARE_CLASS( C_WeaponScavengeItemSpawn, C_WeaponSpawn );
	DECLARE_CLIENTCLASS();
};

#endif // L4D2_C_WEAPON_SPAWN_H
