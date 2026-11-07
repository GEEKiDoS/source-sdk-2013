//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/base_autoshotgun.h"
#ifdef L4D2
#include "precache_register.h"
#endif
#ifdef L4D2
#include "l4d2/weapons/terror_precache_weapon.h"
#endif
#include "tier0/memdbgon.h"

#ifdef CLIENT_DLL
#define CShotgun_SPAS C_Shotgun_SPAS
#endif

class CShotgun_SPAS : public CBaseAutoShotgun
{
public:
	DECLARE_CLASS( CShotgun_SPAS, CBaseAutoShotgun );
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();

	// S:0x3E0E40 / factory S:0x3E0E80, C:0x307560.
	virtual int GetWeaponID() const OVERRIDE { return TERROR_WEAPON_SHOTGUN_SPAS; }
};

IMPLEMENT_NETWORKCLASS_ALIASED( Shotgun_SPAS, DT_Shotgun_SPAS )
BEGIN_NETWORK_TABLE( CShotgun_SPAS, DT_Shotgun_SPAS )
END_NETWORK_TABLE()

BEGIN_PREDICTION_DATA( CShotgun_SPAS )
END_PREDICTION_DATA()

LINK_ENTITY_TO_CLASS( weapon_shotgun_spas, CShotgun_SPAS );
#ifndef L4D2
PRECACHE_WEAPON_REGISTER( weapon_shotgun_spas );
#endif

// Native server 0x3e0d50; 0x5c5038 is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace weapon_shotgun_spasPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "weapon_shotgun_spas", 0x5c5038 ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		if ( !ignoreConditionals && IsWeaponAllowedForPrecache( "weapon_shotgun_spas" ) )
			handler->Precache( RESOURCETYPE_ENTITY, "weapon_shotgun_spas", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif

#ifndef CLIENT_DLL
BEGIN_DATADESC( CShotgun_SPAS )
END_DATADESC()
#endif
