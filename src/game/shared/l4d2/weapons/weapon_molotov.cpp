//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/weapon_basecsgrenade.h"
#include "activitylist.h"
#ifndef CLIENT_DLL
#include "l4d2/weapons/molotov_projectile.h"
#endif
#ifdef L4D2
#include "precache_register.h"
#endif
#include "tier0/memdbgon.h"

#ifdef CLIENT_DLL
#define CMolotov C_Molotov
#endif

class CMolotov : public CBaseCSGrenade
{
public:
	DECLARE_CLASS( CMolotov, CBaseCSGrenade );
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
	virtual void Precache() OVERRIDE;
#endif
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();
	virtual int GetWeaponID() const OVERRIDE { return TERROR_WEAPON_MOLOTOV; }
	virtual bool CanFidget() OVERRIDE { return false; } // S:0x3D99D0
	virtual bool SendWeaponAnim( int iActivity ) OVERRIDE;
	virtual Activity TranslateViewModelActivity( Activity activity ) OVERRIDE;
#ifndef CLIENT_DLL
protected:
	virtual void EmitGrenade( const Vector &position, const QAngle &angles, const Vector &velocity,
		const AngularImpulse &angularVelocity, CBaseCombatCharacter *pThrower ) OVERRIDE;
#endif
};

IMPLEMENT_NETWORKCLASS_ALIASED( Molotov, DT_Molotov )
BEGIN_NETWORK_TABLE( CMolotov, DT_Molotov )
END_NETWORK_TABLE()
BEGIN_PREDICTION_DATA( CMolotov )
END_PREDICTION_DATA()
LINK_ENTITY_TO_CLASS( weapon_molotov, CMolotov );
#ifndef L4D2
PRECACHE_WEAPON_REGISTER( weapon_molotov );
#endif

#ifndef CLIENT_DLL
void CMolotov::Precache()
{
	BaseClass::Precache();
	PrecacheScriptSound( "Molotov.IdleLoop" );
}
#endif

// S:0x3D97A0. First translation precedes the common helper cancellation guard.
bool CMolotov::SendWeaponAnim( int iActivity )
{
	return BaseClass::SendWeaponAnim( TranslateViewModelActivity( (Activity)iActivity ) );
}

static Activity MolotovActivity( const char *pName )
{
	Activity activity = (Activity)ActivityList_IndexForName( pName );
	return activity == ACT_INVALID ? ActivityList_RegisterPrivateActivity( pName ) : activity;
}

// S:0x3D97D0. The second translation selects the original authored layer.
Activity CMolotov::TranslateViewModelActivity( Activity activity )
{
	static const char *s_pNames[][2] =
	{
		{ "ACT_VM_DEPLOY", "ACT_VM_DEPLOY_MOLOTOV" },
		{ "ACT_VM_IDLE", "ACT_VM_IDLE_MOLOTOV" },
		{ "ACT_VM_THROW", "ACT_VM_THROW_MOLOTOV" },
		{ "ACT_VM_PULLPIN", "ACT_VM_PULLPIN_MOLOTOV" },
		{ "ACT_VM_ITEMPICKUP_EXTEND", "ACT_VM_ITEMPICKUP_EXTEND_MOLOTOV" },
		{ "ACT_VM_ITEMPICKUP_LOOP", "ACT_VM_ITEMPICKUP_LOOP_MOLOTOV" },
		{ "ACT_VM_ITEMPICKUP_RETRACT", "ACT_VM_ITEMPICKUP_RETRACT_MOLOTOV" },
		{ "ACT_VM_HELPINGHAND_EXTEND", "ACT_VM_HELPINGHAND_EXTEND_MOLOTOV" },
		{ "ACT_VM_HELPINGHAND_LOOP", "ACT_VM_HELPINGHAND_LOOP_MOLOTOV" },
		{ "ACT_VM_HELPINGHAND_RETRACT", "ACT_VM_HELPINGHAND_RETRACT_MOLOTOV" },
		{ "ACT_VM_MELEE", "ACT_VM_MELEE_MOLOTOV" },
		{ "ACT_VM_ITEMPICKUP_LOOP_MOLOTOV", "ACT_VM_ITEMPICKUP_LOOP_MOLOTOV_LAYER" },
		{ "ACT_VM_ITEMPICKUP_EXTEND_MOLOTOV", "ACT_VM_ITEMPICKUP_EXTEND_MOLOTOV_LAYER" },
		{ "ACT_VM_ITEMPICKUP_RETRACT_MOLOTOV", "ACT_VM_ITEMPICKUP_RETRACT_MOLOTOV_LAYER" },
		{ "ACT_VM_HELPINGHAND_LOOP_MOLOTOV", "ACT_VM_HELPINGHAND_LOOP_MOLOTOV_LAYER" },
		{ "ACT_VM_HELPINGHAND_EXTEND_MOLOTOV", "ACT_VM_HELPINGHAND_EXTEND_MOLOTOV_LAYER" },
		{ "ACT_VM_HELPINGHAND_RETRACT_MOLOTOV", "ACT_VM_HELPINGHAND_RETRACT_MOLOTOV_LAYER" },
		{ "ACT_VM_DEPLOY_MOLOTOV", "ACT_VM_DEPLOY_MOLOTOV_LAYER" },
		{ "ACT_VM_PULLPIN_MOLOTOV", "ACT_VM_PULLPIN_MOLOTOV_LAYER" },
		{ "ACT_VM_THROW_MOLOTOV", "ACT_VM_THROW_MOLOTOV_LAYER" },
		{ "ACT_VM_MELEE_MOLOTOV", "ACT_VM_MELEE_MOLOTOV_LAYER" },
	};
	static Activity s_Source[ARRAYSIZE( s_pNames )];
	static Activity s_Target[ARRAYSIZE( s_pNames )];
	static bool s_bInitialized = false;
	if ( !s_bInitialized )
	{
		for ( int i = 0; i < ARRAYSIZE( s_pNames ); ++i )
		{
			s_Source[i] = MolotovActivity( s_pNames[i][0] );
			s_Target[i] = MolotovActivity( s_pNames[i][1] );
		}
		s_bInitialized = true;
	}
	for ( int i = 0; i < ARRAYSIZE( s_pNames ); ++i )
		if ( activity == s_Source[i] )
			return s_Target[i];
	return BaseClass::TranslateViewModelActivity( activity );
}

#ifndef CLIENT_DLL
void CMolotov::EmitGrenade( const Vector &position, const QAngle &angles, const Vector &velocity,
	const AngularImpulse &angularVelocity, CBaseCombatCharacter *pThrower )
{
	CMolotovProjectile::Create( position, angles, velocity, angularVelocity, pThrower );
}
#endif

// Native server 0x3d96b0; 0x5c4e70 is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace weapon_molotovPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "weapon_molotov", 0x5c4e70 ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_ENTITY, "weapon_molotov", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif

#ifndef CLIENT_DLL
BEGIN_DATADESC( CMolotov )
END_DATADESC()
#endif
