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
#define CAutoShotgun C_AutoShotgun
#endif

class CAutoShotgun : public CBaseAutoShotgun
{
public:
	DECLARE_CLASS( CAutoShotgun, CBaseAutoShotgun );
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();

	// S:0x3C1590 / factory S:0x3C15D0, C:0x2F07E0.
	virtual int GetWeaponID() const OVERRIDE { return TERROR_WEAPON_AUTOSHOTGUN; }
};

IMPLEMENT_NETWORKCLASS_ALIASED( AutoShotgun, DT_AutoShotgun )
BEGIN_NETWORK_TABLE( CAutoShotgun, DT_AutoShotgun )
END_NETWORK_TABLE()

BEGIN_PREDICTION_DATA( CAutoShotgun )
END_PREDICTION_DATA()

LINK_ENTITY_TO_CLASS( weapon_autoshotgun, CAutoShotgun );
#ifndef L4D2
PRECACHE_WEAPON_REGISTER( weapon_autoshotgun );
#endif

// Native server 0x3c1460; 0x5c4abc is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace weapon_autoshotgunPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "weapon_autoshotgun", 0x5c4abc ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		if ( !ignoreConditionals && IsWeaponAllowedForPrecache( "weapon_autoshotgun" ) )
			handler->Precache( RESOURCETYPE_ENTITY, "weapon_autoshotgun", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif

#ifndef CLIENT_DLL
BEGIN_DATADESC( CAutoShotgun )
END_DATADESC()
#endif
