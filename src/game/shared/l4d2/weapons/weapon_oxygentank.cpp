//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/weapon_carried_item.h"
#ifdef CLIENT_DLL
#define COxygenTank C_OxygenTank
#endif
#ifdef L4D2
#include "precache_register.h"
#endif
#include "tier0/memdbgon.h"

class COxygenTank : public CCarriedProp
{
public:
	DECLARE_CLASS( COxygenTank, CCarriedProp );
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();
	virtual int GetWeaponID() const OVERRIDE { return TERROR_WEAPON_OXYGENTANK; }
	virtual const char *GetCarriedPropModel() const OVERRIDE { return "models/props_equipment/oxygentank01.mdl"; }
	virtual Activity GetPlayerAttackActivity( TerrorPlayerAnimEvent event, Activity mainActivity ) OVERRIDE;
	virtual acttable_t *ActivityList( int &iActivityCount ) OVERRIDE;
};

IMPLEMENT_NETWORKCLASS_ALIASED( OxygenTank, DT_OxygenTank )
BEGIN_NETWORK_TABLE( COxygenTank, DT_OxygenTank )
END_NETWORK_TABLE()
BEGIN_PREDICTION_DATA( COxygenTank )
END_PREDICTION_DATA()
LINK_ENTITY_TO_CLASS( weapon_oxygentank, COxygenTank );
#ifndef L4D2
PRECACHE_WEAPON_REGISTER( weapon_oxygentank );
#endif

Activity COxygenTank::GetPlayerAttackActivity( TerrorPlayerAnimEvent event, Activity mainActivity )
{
	return event >= TERROR_ANIMEVENT_MELEE_SWEEP && event <= TERROR_ANIMEVENT_MELEE_STOMP ? CarriedActivity( "ACT_MELEE_SWEEP_O2" ) : ACT_INVALID;
}

acttable_t *COxygenTank::ActivityList( int &iActivityCount )
{
	// S:0x7BB578; the delayed explosion is the model's prop_data, not a
	// weapon think or a second health/damage implementation here.
	static const char *s_pNames[][2] =
	{
		{ "ACT_IDLE", "ACT_IDLE_O2" },
		{ "ACT_CROUCHIDLE", "ACT_CROUCHIDLE_O2" },
		{ "ACT_RUN_CROUCH", "ACT_RUN_CROUCH_O2" },
		{ "ACT_WALK", "ACT_WALK_O2" },
		{ "ACT_RUN", "ACT_RUN_O2" },
		{ "ACT_IDLE_INJURED", "ACT_IDLE_INJURED_O2" },
		{ "ACT_WALK_INJURED", "ACT_WALK_O2" },
		{ "ACT_RUN_INJURED", "ACT_RUN_INJURED_O2" },
		{ "ACT_IDLE_CALM", "ACT_IDLE_CALM_O2" },
		{ "ACT_WALK_CALM", "ACT_WALK_O2" },
		{ "ACT_RUN_CALM", "ACT_RUN_O2" },
		{ "ACT_JUMP", "ACT_JUMP_O2" }
	};
	static acttable_t s_Activities[ARRAYSIZE( s_pNames )];
	static bool s_bInitialized = false;
	if ( !s_bInitialized )
	{
		for ( int i = 0; i < ARRAYSIZE( s_pNames ); ++i )
		{
			s_Activities[i].baseAct = CarriedActivity( s_pNames[i][0] );
			s_Activities[i].weaponAct = CarriedActivity( s_pNames[i][1] );
			s_Activities[i].required = false;
		}
		s_bInitialized = true;
	}
	iActivityCount = ARRAYSIZE( s_Activities );
	return s_Activities;
}

// Native server 0x3dcee0; 0x5c4edc is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace weapon_oxygentankPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "weapon_oxygentank", 0x5c4edc ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_ENTITY, "weapon_oxygentank", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif

#ifndef CLIENT_DLL
BEGIN_DATADESC( COxygenTank )
END_DATADESC()
#endif
