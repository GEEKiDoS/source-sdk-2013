//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_C_UPGRADE_ITEM_H
#define L4D2_C_UPGRADE_ITEM_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/weapons/c_weapon_spawn.h"

class C_BaseUpgradeItem : public C_WeaponSpawn
{
public:
	DECLARE_CLASS( C_BaseUpgradeItem, C_WeaponSpawn );
	DECLARE_CLIENTCLASS();
	virtual int GetUseType( C_BaseEntity *pUser ) OVERRIDE;

	int m_iUsedBySurvivorsMask;
};

#endif // L4D2_C_UPGRADE_ITEM_H
