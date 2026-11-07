//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/base_shotgun.h"
#ifdef L4D2
#include "precache_register.h"
#endif
#ifdef L4D2
#include "l4d2/weapons/terror_precache_weapon.h"
#endif
#include "tier0/memdbgon.h"

#ifdef CLIENT_DLL
#define CShotgun_Chrome C_Shotgun_Chrome
#endif

class CShotgun_Chrome : public CBaseShotgun
{
public:
	DECLARE_CLASS( CShotgun_Chrome, CBaseShotgun );
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();

	// S:0x3E0C10 / factory S:0x3E0C60, C:0x307370.
	virtual int GetWeaponID() const OVERRIDE { return TERROR_WEAPON_SHOTGUN_CHROME; }
	// S:0x3E0C20. Pellet layout/damage/model/sounds remain this leaf's script.
	virtual double GetFireRate() OVERRIDE { return 0.875f; }
};

IMPLEMENT_NETWORKCLASS_ALIASED( Shotgun_Chrome, DT_Shotgun_Chrome )
BEGIN_NETWORK_TABLE( CShotgun_Chrome, DT_Shotgun_Chrome )
END_NETWORK_TABLE()

BEGIN_PREDICTION_DATA( CShotgun_Chrome )
END_PREDICTION_DATA()

LINK_ENTITY_TO_CLASS( weapon_shotgun_chrome, CShotgun_Chrome );
#ifndef L4D2
PRECACHE_WEAPON_REGISTER( weapon_shotgun_chrome );
#endif

// Native server 0x3e0b20; 0x5c5020 is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace weapon_shotgun_chromePrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "weapon_shotgun_chrome", 0x5c5020 ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		if ( !ignoreConditionals && IsWeaponAllowedForPrecache( "weapon_shotgun_chrome" ) )
			handler->Precache( RESOURCETYPE_ENTITY, "weapon_shotgun_chrome", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif

#ifndef CLIENT_DLL
BEGIN_DATADESC( CShotgun_Chrome )
END_DATADESC()
#endif
