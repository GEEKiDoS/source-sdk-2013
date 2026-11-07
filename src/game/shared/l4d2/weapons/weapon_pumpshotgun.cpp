//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/base_shotgun.h"
#ifdef L4D2
#include "precache_register.h"
#endif
#ifdef L4D2
#include "l4d2/weapons/terror_precache_weapon.h"
#endif
#include "l4d2/weapons/weapon_pumpshotgun_console_server_cvars.inc"
#include "tier0/memdbgon.h"

#ifdef CLIENT_DLL
#define CPumpShotgun C_PumpShotgun
#endif

class CPumpShotgun : public CBaseShotgun
{
public:
	DECLARE_CLASS( CPumpShotgun, CBaseShotgun );
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();

	// S:0x3DFBF0 / factory S:0x3DFC40, C:0x306600.
	virtual int GetWeaponID() const OVERRIDE { return TERROR_WEAPON_PUMPSHOTGUN; }
	// S:0x3DFC00; fixed pump cycle, including the last loaded shell.
	virtual double GetFireRate() OVERRIDE { return 0.875f; }
};

IMPLEMENT_NETWORKCLASS_ALIASED( PumpShotgun, DT_PumpShotgun )
BEGIN_NETWORK_TABLE( CPumpShotgun, DT_PumpShotgun )
END_NETWORK_TABLE()

BEGIN_PREDICTION_DATA( CPumpShotgun )
END_PREDICTION_DATA()

LINK_ENTITY_TO_CLASS( weapon_pumpshotgun, CPumpShotgun );
#ifndef L4D2
PRECACHE_WEAPON_REGISTER( weapon_pumpshotgun );
#endif

// Native server 0x3dfb00; 0x5c4fa8 is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace weapon_pumpshotgunPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "weapon_pumpshotgun", 0x5c4fa8 ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		if ( !ignoreConditionals && IsWeaponAllowedForPrecache( "weapon_pumpshotgun" ) )
			handler->Precache( RESOURCETYPE_ENTITY, "weapon_pumpshotgun", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif

#ifndef CLIENT_DLL
BEGIN_DATADESC( CPumpShotgun )
END_DATADESC()
#endif
