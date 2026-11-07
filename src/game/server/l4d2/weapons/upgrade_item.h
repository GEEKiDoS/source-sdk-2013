//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_UPGRADE_ITEM_H
#define L4D2_UPGRADE_ITEM_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/weapons/weapon_spawn.h"
#include "triggers.h"
#include "l4d2/terror_shareddefs.h"


class CBaseUpgradeItem : public CWeaponSpawn
{
public:
	DECLARE_CLASS( CBaseUpgradeItem, CWeaponSpawn );
	DECLARE_SERVERCLASS();

	CBaseUpgradeItem();
	virtual void Spawn() OVERRIDE;
	virtual void Precache() OVERRIDE;
	virtual void Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value ) OVERRIDE;
	virtual int GetWeaponID() const OVERRIDE;
	virtual int GetUseType( CBaseEntity *user ) OVERRIDE;
	virtual int GetUpgradeType() const = 0;
	virtual bool IsLimitedUse() const { return true; }
	bool HasBeenUsedBySurvivor( unsigned int character ) const;
	void MarkUsedBySurvivor( unsigned int character );

	CNetworkVar( int, m_iUsedBySurvivorsMask );
};

class CUpgradeAmmoIncendiary : public CBaseUpgradeItem
{
public:
	DECLARE_CLASS( CUpgradeAmmoIncendiary, CBaseUpgradeItem );
	virtual const char *GetWeaponModel() const OVERRIDE;
	virtual int GetUpgradeType() const OVERRIDE { return TERROR_UPGRADE_INCENDIARY_AMMO; }
};

class CUpgradeAmmoExplosive : public CBaseUpgradeItem
{
public:
	DECLARE_CLASS( CUpgradeAmmoExplosive, CBaseUpgradeItem );
	virtual const char *GetWeaponModel() const OVERRIDE;
	virtual int GetUpgradeType() const OVERRIDE { return TERROR_UPGRADE_EXPLOSIVE_AMMO; }
};

class CUpgradeLaserSight : public CBaseUpgradeItem
{
public:
	DECLARE_CLASS( CUpgradeLaserSight, CBaseUpgradeItem );
	virtual const char *GetWeaponModel() const OVERRIDE;
	virtual int GetUpgradeType() const OVERRIDE { return TERROR_UPGRADE_LASER_SIGHT; }
	virtual bool IsLimitedUse() const OVERRIDE { return false; }
};

class CTriggerUpgradeLaserSight : public CBaseTrigger
{
public:
	DECLARE_CLASS( CTriggerUpgradeLaserSight, CBaseTrigger );
	virtual void Spawn() OVERRIDE;
	virtual int GetUpgradeType() const { return TERROR_UPGRADE_LASER_SIGHT; }
	void UpgradeTouch( CBaseEntity *pOther );
};

#endif // L4D2_UPGRADE_ITEM_H
