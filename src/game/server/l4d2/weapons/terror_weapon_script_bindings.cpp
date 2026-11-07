//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/terror_weapon.h"
#include "tier0/memdbgon.h"

// S3F0650 / S3F06A0. Deliberately no clamp to magazine capacity or zero.
void CTerrorWeapon::ScriptSetClip1( int clip )
{
	m_iClip1 = clip;
}

void CTerrorWeapon::ScriptSetClip2( int clip )
{
	m_iClip2 = clip;
}

// S3F2C90: the SCRIPT base is CBaseAnimating, not CWeaponCSBase or
// CBaseCombatWeapon. All ten help strings really are empty strings.
BEGIN_ENT_SCRIPTDESC( CTerrorWeapon, CBaseAnimating, "Weapon" )
	DEFINE_SCRIPTFUNC( GetMaxClip1, "" )
	DEFINE_SCRIPTFUNC( GetMaxClip2, "" )
	DEFINE_SCRIPTFUNC( GetDefaultClip1, "" )
	DEFINE_SCRIPTFUNC( GetDefaultClip2, "" )
	DEFINE_SCRIPTFUNC( Clip1, "" )
	DEFINE_SCRIPTFUNC( Clip2, "" )
	DEFINE_SCRIPTFUNC( GiveDefaultAmmo, "" )
	DEFINE_SCRIPTFUNC( Reload, "" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptSetClip1, "SetClip1", "" )
	DEFINE_SCRIPTFUNC_NAMED( ScriptSetClip2, "SetClip2", "" )
END_SCRIPTDESC()
