//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/weapon_carried_item.h"
#ifdef CLIENT_DLL
#define CFireworkCrate C_FireworkCrate
#endif
#ifdef L4D2
#include "precache_register.h"
#endif
#include "tier0/memdbgon.h"

// S:0x3CCB00. The native breakable's explosion_type=fireworks creates
// CFireCrackerBlast; carrying this weapon must not run a second inferno.
class CFireworkCrate : public CCarriedProp
{
public:
	DECLARE_CLASS( CFireworkCrate, CCarriedProp );
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();
	virtual int GetWeaponID() const OVERRIDE { return TERROR_WEAPON_FIREWORKCRATE; }
	virtual const char *GetCarriedPropModel() const OVERRIDE { return "models/props_junk/explosive_box001.mdl"; }
};

IMPLEMENT_NETWORKCLASS_ALIASED( FireworkCrate, DT_FireworkCrate )
BEGIN_NETWORK_TABLE( CFireworkCrate, DT_FireworkCrate )
END_NETWORK_TABLE()
BEGIN_PREDICTION_DATA( CFireworkCrate )
END_PREDICTION_DATA()
LINK_ENTITY_TO_CLASS( weapon_fireworkcrate, CFireworkCrate );
#ifndef L4D2
PRECACHE_WEAPON_REGISTER( weapon_fireworkcrate );
#endif

// Native server 0x3cc900; 0x5c4c50 is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace weapon_fireworkcratePrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "weapon_fireworkcrate", 0x5c4c50 ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_ENTITY, "weapon_fireworkcrate", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif

#ifndef CLIENT_DLL
BEGIN_DATADESC( CFireworkCrate )
END_DATADESC()
#endif
