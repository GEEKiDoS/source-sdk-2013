//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/weapon_basecsgrenade.h"
#include "activitylist.h"
#ifndef CLIENT_DLL
#include "l4d2/weapons/pipe_bomb_projectile.h"
#endif
#ifdef L4D2
#include "precache_register.h"
#endif
#include "tier0/memdbgon.h"

#ifdef CLIENT_DLL
#define CPipeBomb C_PipeBomb
#endif

class CPipeBomb : public CBaseCSGrenade
{
public:
	DECLARE_CLASS( CPipeBomb, CBaseCSGrenade );
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();
	virtual int GetWeaponID() const OVERRIDE { return TERROR_WEAPON_PIPE_BOMB; }
	virtual bool CanFidget() OVERRIDE { return false; } // S:0x3DDBA0
	virtual bool SendWeaponAnim( int iActivity ) OVERRIDE;
	virtual Activity TranslateViewModelActivity( Activity activity ) OVERRIDE;
#ifndef CLIENT_DLL
protected:
	virtual void EmitGrenade( const Vector &position, const QAngle &angles, const Vector &velocity,
		const AngularImpulse &angularVelocity, CBaseCombatCharacter *pThrower ) OVERRIDE;
#endif
};

IMPLEMENT_NETWORKCLASS_ALIASED( PipeBomb, DT_PipeBomb )
BEGIN_NETWORK_TABLE( CPipeBomb, DT_PipeBomb )
END_NETWORK_TABLE()
BEGIN_PREDICTION_DATA( CPipeBomb )
END_PREDICTION_DATA()
LINK_ENTITY_TO_CLASS( weapon_pipe_bomb, CPipeBomb );
#ifndef L4D2
PRECACHE_WEAPON_REGISTER( weapon_pipe_bomb );
#endif

// S:0x3DD970. First translation precedes the common helper cancellation guard.
bool CPipeBomb::SendWeaponAnim( int iActivity )
{
	return BaseClass::SendWeaponAnim( TranslateViewModelActivity( (Activity)iActivity ) );
}

static Activity PipeBombActivity( const char *pName )
{
	Activity activity = (Activity)ActivityList_IndexForName( pName );
	return activity == ACT_INVALID ? ActivityList_RegisterPrivateActivity( pName ) : activity;
}

// S:0x3DD9A0. The second translation selects the original authored layer.
Activity CPipeBomb::TranslateViewModelActivity( Activity activity )
{
	static const char *s_pNames[][2] =
	{
		{ "ACT_VM_DEPLOY", "ACT_VM_DEPLOY_PIPEBOMB" },
		{ "ACT_VM_IDLE", "ACT_VM_IDLE_PIPEBOMB" },
		{ "ACT_VM_THROW", "ACT_VM_THROW_PIPEBOMB" },
		{ "ACT_VM_PULLPIN", "ACT_VM_PULLPIN_PIPEBOMB" },
		{ "ACT_VM_ITEMPICKUP_EXTEND", "ACT_VM_ITEMPICKUP_EXTEND_PIPEBOMB" },
		{ "ACT_VM_ITEMPICKUP_LOOP", "ACT_VM_ITEMPICKUP_LOOP_PIPEBOMB" },
		{ "ACT_VM_ITEMPICKUP_RETRACT", "ACT_VM_ITEMPICKUP_RETRACT_PIPEBOMB" },
		{ "ACT_VM_HELPINGHAND_EXTEND", "ACT_VM_HELPINGHAND_EXTEND_PIPEBOMB" },
		{ "ACT_VM_HELPINGHAND_LOOP", "ACT_VM_HELPINGHAND_LOOP_PIPEBOMB" },
		{ "ACT_VM_HELPINGHAND_RETRACT", "ACT_VM_HELPINGHAND_RETRACT_PIPEBOMB" },
		{ "ACT_VM_MELEE", "ACT_VM_MELEE_PIPEBOMB" },
		{ "ACT_VM_ITEMPICKUP_LOOP_PIPEBOMB", "ACT_VM_ITEMPICKUP_LOOP_PIPEBOMB_LAYER" },
		{ "ACT_VM_ITEMPICKUP_EXTEND_PIPEBOMB", "ACT_VM_ITEMPICKUP_EXTEND_PIPEBOMB_LAYER" },
		{ "ACT_VM_ITEMPICKUP_RETRACT_PIPEBOMB", "ACT_VM_ITEMPICKUP_RETRACT_PIPEBOMB_LAYER" },
		{ "ACT_VM_HELPINGHAND_LOOP_PIPEBOMB", "ACT_VM_HELPINGHAND_LOOP_PIPEBOMB_LAYER" },
		{ "ACT_VM_HELPINGHAND_EXTEND_PIPEBOMB", "ACT_VM_HELPINGHAND_EXTEND_PIPEBOMB_LAYER" },
		{ "ACT_VM_HELPINGHAND_RETRACT_PIPEBOMB", "ACT_VM_HELPINGHAND_RETRACT_PIPEBOMB_LAYER" },
		{ "ACT_VM_DEPLOY_PIPEBOMB", "ACT_VM_DEPLOY_PIPEBOMB_LAYER" },
		{ "ACT_VM_PULLPIN_PIPEBOMB", "ACT_VM_PULLPIN_PIPEBOMB_LAYER" },
		{ "ACT_VM_THROW_PIPEBOMB", "ACT_VM_THROW_PIPEBOMB_LAYER" },
		{ "ACT_VM_MELEE_PIPEBOMB", "ACT_VM_MELEE_PIPEBOMB_LAYER" },
	};
	static Activity s_Source[ARRAYSIZE( s_pNames )];
	static Activity s_Target[ARRAYSIZE( s_pNames )];
	static bool s_bInitialized = false;
	if ( !s_bInitialized )
	{
		for ( int i = 0; i < ARRAYSIZE( s_pNames ); ++i )
		{
			s_Source[i] = PipeBombActivity( s_pNames[i][0] );
			s_Target[i] = PipeBombActivity( s_pNames[i][1] );
		}
		s_bInitialized = true;
	}
	for ( int i = 0; i < ARRAYSIZE( s_pNames ); ++i )
		if ( activity == s_Source[i] )
			return s_Target[i];
	return BaseClass::TranslateViewModelActivity( activity );
}

#ifndef CLIENT_DLL
void CPipeBomb::EmitGrenade( const Vector &position, const QAngle &angles, const Vector &velocity,
	const AngularImpulse &angularVelocity, CBaseCombatCharacter *pThrower )
{
	CPipeBombProjectile::Create( position, angles, velocity, angularVelocity, pThrower );
}
#endif

// Native server 0x3dd8d0; 0x5c4f18 is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace weapon_pipe_bombPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "weapon_pipe_bomb", 0x5c4f18 ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_ENTITY, "weapon_pipe_bomb", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif

#ifndef CLIENT_DLL
BEGIN_DATADESC( CPipeBomb )
END_DATADESC()
#endif
