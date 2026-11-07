//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "c_basecombatweapon.h"
#include "tier0/memdbgon.h"

// These are the COMPLETE native data-only wrappers, not substitute weapons.
// C2282C0/C228420 call only the real base constructor then network Initialize;
// all 371 primary virtuals are inherited except generated metadata and destruction.
class C_WeaponCycler : public C_BaseCombatWeapon
{
public:
	DECLARE_CLASS( C_WeaponCycler, C_BaseCombatWeapon );
	DECLARE_CLIENTCLASS();
	DECLARE_PREDICTABLE();
private:
	C_WeaponCycler( const C_WeaponCycler & );
public:
	C_WeaponCycler() {}
};
BEGIN_PREDICTION_DATA( C_WeaponCycler )
END_PREDICTION_DATA()
LINK_ENTITY_TO_CLASS( cycler_weapon, C_WeaponCycler );
IMPLEMENT_CLIENTCLASS_DT( C_WeaponCycler, DT_WeaponCycler, CWeaponCycler )
END_RECV_TABLE()

class C_WeaponCubemap : public C_BaseCombatWeapon
{
public:
	DECLARE_CLASS( C_WeaponCubemap, C_BaseCombatWeapon );
	DECLARE_CLIENTCLASS();
	DECLARE_PREDICTABLE();
private:
	C_WeaponCubemap( const C_WeaponCubemap & );
public:
	C_WeaponCubemap() {}
};
BEGIN_PREDICTION_DATA( C_WeaponCubemap )
END_PREDICTION_DATA()
LINK_ENTITY_TO_CLASS( weapon_cubemap, C_WeaponCubemap );
IMPLEMENT_CLIENTCLASS_DT( C_WeaponCubemap, DT_WeaponCubemap, CWeaponCubemap )
END_RECV_TABLE()
