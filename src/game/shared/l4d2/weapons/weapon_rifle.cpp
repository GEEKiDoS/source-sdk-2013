//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/weapon_rifle.h"
#ifdef L4D2
#include "precache_register.h"
#endif
#ifdef L4D2
#include "l4d2/weapons/terror_precache_weapon.h"
#endif
#include "tier0/memdbgon.h"

IMPLEMENT_NETWORKCLASS_ALIASED( AssaultRifle, DT_AssaultRifle )
BEGIN_NETWORK_TABLE( CAssaultRifle, DT_AssaultRifle )
END_NETWORK_TABLE()

BEGIN_PREDICTION_DATA( CAssaultRifle )
END_PREDICTION_DATA()

LINK_ENTITY_TO_CLASS( weapon_rifle, CAssaultRifle );
#ifndef L4D2
PRECACHE_WEAPON_REGISTER( weapon_rifle );
#endif


// Native server 0x3c1160; 0x5c4aa4 is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace weapon_riflePrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "weapon_rifle", 0x5c4aa4 ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		if ( !ignoreConditionals && IsWeaponAllowedForPrecache( "weapon_rifle" ) )
			handler->Precache( RESOURCETYPE_ENTITY, "weapon_rifle", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif


#ifndef CLIENT_DLL
BEGIN_DATADESC( CAssaultRifle )
END_DATADESC()
#endif
