//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/base_rifle.h"
#ifdef L4D2
#include "precache_register.h"
#endif
#ifdef L4D2
#include "l4d2/weapons/terror_precache_weapon.h"
#endif
#include "tier0/memdbgon.h"

#ifdef CLIENT_DLL
#define CRifle_AK47 C_Rifle_AK47
#endif

// S:0x3DFE60; its firing, reload and activity virtuals are CBaseRifle's.
class CRifle_AK47 : public CBaseRifle
{
public:
	DECLARE_CLASS( CRifle_AK47, CBaseRifle );
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();
	virtual int GetWeaponID() const OVERRIDE { return TERROR_WEAPON_RIFLE_AK47; }
};

IMPLEMENT_NETWORKCLASS_ALIASED( Rifle_AK47, DT_Rifle_AK47 )
BEGIN_NETWORK_TABLE( CRifle_AK47, DT_Rifle_AK47 )
END_NETWORK_TABLE()
BEGIN_PREDICTION_DATA( CRifle_AK47 )
END_PREDICTION_DATA()

LINK_ENTITY_TO_CLASS( weapon_rifle_ak47, CRifle_AK47 );
#ifndef L4D2
PRECACHE_WEAPON_REGISTER( weapon_rifle_ak47 );
#endif

// Native server 0x3dfd30; 0x5c4fc0 is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace weapon_rifle_ak47Precache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "weapon_rifle_ak47", 0x5c4fc0 ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		if ( !ignoreConditionals && IsWeaponAllowedForPrecache( "weapon_rifle_ak47" ) )
			handler->Precache( RESOURCETYPE_ENTITY, "weapon_rifle_ak47", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif

#ifndef CLIENT_DLL
BEGIN_DATADESC( CRifle_AK47 )
END_DATADESC()
#endif
