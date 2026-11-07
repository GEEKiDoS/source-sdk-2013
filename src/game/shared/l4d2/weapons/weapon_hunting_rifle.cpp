//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/base_sniperrifle.h"
#ifdef L4D2
#include "precache_register.h"
#endif
#ifdef L4D2
#include "l4d2/weapons/terror_precache_weapon.h"
#endif
#include "tier0/memdbgon.h"

#ifdef CLIENT_DLL
#define CSniperRifle C_SniperRifle
#endif

class CSniperRifle : public CBaseSniperRifle
{
public:
	DECLARE_CLASS( CSniperRifle, CBaseSniperRifle );
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();
	virtual int GetWeaponID() const OVERRIDE { return TERROR_WEAPON_HUNTING_RIFLE; }
	virtual int GetViewModelLayer( Activity activity ) OVERRIDE;
};

// S:0x3E1FF0: hunting alone retains DT_BaseSniperRifle in its table chain.
IMPLEMENT_NETWORKCLASS_ALIASED( SniperRifle, DT_SniperRifle )
BEGIN_NETWORK_TABLE( CSniperRifle, DT_SniperRifle )
END_NETWORK_TABLE()
BEGIN_PREDICTION_DATA( CSniperRifle )
END_PREDICTION_DATA()
LINK_ENTITY_TO_CLASS( weapon_hunting_rifle, CSniperRifle );
#ifndef L4D2
PRECACHE_WEAPON_REGISTER( weapon_hunting_rifle );
#endif

int CSniperRifle::GetViewModelLayer( Activity activity )
{
	// S:0x3E1FD0. C:0x022F50 asks with sequenceActivity BEFORE the final
	// family layer remap, hence MELEE_SNIPER rather than its _LAYER form.
	static const Activity s_Melee = SniperActivity( "ACT_VM_MELEE_SNIPER" );
	return activity == s_Melee ? 3 : 0;
}

// Native server 0x3e1f90; 0x5c50e4 is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace weapon_hunting_riflePrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "weapon_hunting_rifle", 0x5c50e4 ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		if ( !ignoreConditionals && IsWeaponAllowedForPrecache( "weapon_hunting_rifle" ) )
			handler->Precache( RESOURCETYPE_ENTITY, "weapon_hunting_rifle", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif

#ifndef CLIENT_DLL
BEGIN_DATADESC( CSniperRifle )
END_DATADESC()
#endif
