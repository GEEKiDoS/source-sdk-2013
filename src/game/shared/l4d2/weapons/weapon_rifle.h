//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_WEAPON_RIFLE_H
#define L4D2_WEAPON_RIFLE_H
#ifdef _WIN32
#pragma once
#endif
#include "l4d2/weapons/base_rifle.h"
#ifdef CLIENT_DLL
#define CAssaultRifle C_AssaultRifle
#endif

class CAssaultRifle : public CBaseRifle
{
public:
	DECLARE_CLASS( CAssaultRifle, CBaseRifle );
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();
	virtual int GetWeaponID() const OVERRIDE { return TERROR_WEAPON_RIFLE; }
};
#endif // L4D2_WEAPON_RIFLE_H
