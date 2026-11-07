//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/weapon_basecsgrenade.h"
#include "activitylist.h"
#ifndef CLIENT_DLL
#include "l4d2/weapons/vomitjar_projectile.h"
#endif
#ifdef L4D2
#include "precache_register.h"
#endif
#include "tier0/memdbgon.h"

#ifdef CLIENT_DLL
#define CItem_VomitJar C_Item_VomitJar
#endif

class CItem_VomitJar : public CBaseCSGrenade
{
public:
	DECLARE_CLASS( CItem_VomitJar, CBaseCSGrenade );
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();
	virtual int GetWeaponID() const OVERRIDE { return TERROR_WEAPON_VOMITJAR; }
	virtual bool CanFidget() OVERRIDE { return false; } // S:0x3D3DE0
	virtual bool SendWeaponAnim( int iActivity ) OVERRIDE;
	virtual Activity TranslateViewModelActivity( Activity activity ) OVERRIDE;
#ifndef CLIENT_DLL
protected:
	virtual void EmitGrenade( const Vector &position, const QAngle &angles, const Vector &velocity,
		const AngularImpulse &angularVelocity, CBaseCombatCharacter *pThrower ) OVERRIDE;
#endif
};

IMPLEMENT_NETWORKCLASS_ALIASED( Item_VomitJar, DT_Item_VomitJar )
BEGIN_NETWORK_TABLE( CItem_VomitJar, DT_Item_VomitJar )
END_NETWORK_TABLE()
BEGIN_PREDICTION_DATA( CItem_VomitJar )
END_PREDICTION_DATA()
LINK_ENTITY_TO_CLASS( weapon_vomitjar, CItem_VomitJar );
#ifndef L4D2
PRECACHE_WEAPON_REGISTER( weapon_vomitjar );
#endif

// S:0x3D3AF0. First translation precedes the common helper cancellation guard.
bool CItem_VomitJar::SendWeaponAnim( int iActivity )
{
	return BaseClass::SendWeaponAnim( TranslateViewModelActivity( (Activity)iActivity ) );
}

static Activity Item_VomitJarActivity( const char *pName )
{
	Activity activity = (Activity)ActivityList_IndexForName( pName );
	return activity == ACT_INVALID ? ActivityList_RegisterPrivateActivity( pName ) : activity;
}

// S:0x3D3B20. The second translation selects the original authored layer.
Activity CItem_VomitJar::TranslateViewModelActivity( Activity activity )
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
			s_Source[i] = Item_VomitJarActivity( s_pNames[i][0] );
			s_Target[i] = Item_VomitJarActivity( s_pNames[i][1] );
		}
		s_bInitialized = true;
	}
	for ( int i = 0; i < ARRAYSIZE( s_pNames ); ++i )
		if ( activity == s_Source[i] )
			return s_Target[i];
	return BaseClass::TranslateViewModelActivity( activity );
}

#ifndef CLIENT_DLL
void CItem_VomitJar::EmitGrenade( const Vector &position, const QAngle &angles, const Vector &velocity,
	const AngularImpulse &angularVelocity, CBaseCombatCharacter *pThrower )
{
	CVomitJarProjectile::Create( position, angles, velocity, angularVelocity, pThrower );
}
#endif

// Native server 0x3d3a50; 0x5c4df0 is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace weapon_vomitjarPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "weapon_vomitjar", 0x5c4df0 ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_ENTITY, "weapon_vomitjar", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif

#ifndef CLIENT_DLL
BEGIN_DATADESC( CItem_VomitJar )
END_DATADESC()
#endif
