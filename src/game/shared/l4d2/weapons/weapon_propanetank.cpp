//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/weapon_carried_item.h"
#ifdef CLIENT_DLL
#define CPropaneTank C_PropaneTank
#endif
#ifdef L4D2
#include "precache_register.h"
#endif
#include "tier0/memdbgon.h"

// S:0x3DFA10. This is the CCarriedProp weapon, NOT an explosive
// projectile. Dropping it restores the real physics prop/model prop_data.
class CPropaneTank : public CCarriedProp
{
public:
	DECLARE_CLASS( CPropaneTank, CCarriedProp );
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();
	virtual int GetWeaponID() const OVERRIDE { return TERROR_WEAPON_PROPANETANK; }
	virtual const char *GetCarriedPropModel() const OVERRIDE { return "models/props_junk/propanecanister001a.mdl"; }
};

IMPLEMENT_NETWORKCLASS_ALIASED( PropaneTank, DT_PropaneTank )
BEGIN_NETWORK_TABLE( CPropaneTank, DT_PropaneTank )
END_NETWORK_TABLE()
BEGIN_PREDICTION_DATA( CPropaneTank )
END_PREDICTION_DATA()
LINK_ENTITY_TO_CLASS( weapon_propanetank, CPropaneTank );
#ifndef L4D2
PRECACHE_WEAPON_REGISTER( weapon_propanetank );
#endif

// Native server 0x3df830; 0x5c4f8c is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace weapon_propanetankPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "weapon_propanetank", 0x5c4f8c ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_ENTITY, "weapon_propanetank", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif

#ifndef CLIENT_DLL
BEGIN_DATADESC( CPropaneTank )
END_DATADESC()
#endif
