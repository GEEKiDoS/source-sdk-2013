//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/terror_gun.h"
#include "activitylist.h"
#ifdef L4D2
#include "precache_register.h"
#endif
#ifdef L4D2
#include "l4d2/weapons/terror_precache_weapon.h"
#endif
#include "tier0/memdbgon.h"

#ifdef CLIENT_DLL
#define CSMG_Silenced C_SMG_Silenced
#endif

class CSMG_Silenced : public CTerrorGun
{
public:
	DECLARE_CLASS( CSMG_Silenced, CTerrorGun );
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();
	virtual int GetWeaponID() const OVERRIDE { return TERROR_WEAPON_SMG_SILENCED; }
	virtual acttable_t *ActivityList( int &iActivityCount ) OVERRIDE;
	virtual Activity GetPlayerAttackActivity( TerrorPlayerAnimEvent event, Activity mainActivity ) OVERRIDE;
	virtual Activity GetPlayerReloadActivity( TerrorPlayerAnimEvent event, Activity mainActivity ) OVERRIDE;
	virtual Activity TranslateViewModelActivity( Activity activity ) OVERRIDE;
};

IMPLEMENT_NETWORKCLASS_ALIASED( SMG_Silenced, DT_SMG_Silenced )
BEGIN_NETWORK_TABLE( CSMG_Silenced, DT_SMG_Silenced )
END_NETWORK_TABLE()
BEGIN_PREDICTION_DATA( CSMG_Silenced )
END_PREDICTION_DATA()
LINK_ENTITY_TO_CLASS( weapon_smg_silenced, CSMG_Silenced );
#ifndef L4D2
PRECACHE_WEAPON_REGISTER( weapon_smg_silenced );
#endif

static Activity SMG_SilencedActivity( const char *pName )
{
	Activity activity = (Activity)ActivityList_IndexForName( pName );
	return activity == ACT_INVALID ? ActivityList_RegisterPrivateActivity( pName ) : activity;
}

// S:0x3E1330, table0x7BCDB0. The MP5 intentionally uses RIFLE locomotion.
acttable_t *CSMG_Silenced::ActivityList( int &iActivityCount )
{
	static const char *s_pActivityNames[][2] =
	{
		{ "ACT_IDLE", "ACT_IDLE_SMG" },
		{ "ACT_CROUCHIDLE", "ACT_CROUCHIDLE_SMG" },
		{ "ACT_RUN_CROUCH", "ACT_RUN_CROUCH_SMG" },
		{ "ACT_WALK", "ACT_WALK_SMG" },
		{ "ACT_RUN", "ACT_RUN_SMG" },
		{ "ACT_IDLE_INJURED", "ACT_IDLE_INJURED_SMG" },
		{ "ACT_WALK_INJURED", "ACT_WALK_INJURED_SMG" },
		{ "ACT_RUN_INJURED", "ACT_RUN_INJURED_SMG" },
		{ "ACT_IDLE_CALM", "ACT_IDLE_CALM_SMG" },
		{ "ACT_WALK_CALM", "ACT_WALK_CALM_SMG" },
		{ "ACT_RUN_CALM", "ACT_RUN_CALM_SMG" },
		{ "ACT_JUMP", "ACT_JUMP_SMG" }
	};
	static acttable_t s_ActivityTable[ARRAYSIZE( s_pActivityNames )];
	static bool s_bInitialized = false;
	if ( !s_bInitialized )
	{
		for ( int i = 0; i < ARRAYSIZE( s_pActivityNames ); ++i )
		{
			s_ActivityTable[i].baseAct = SMG_SilencedActivity( s_pActivityNames[i][0] );
			s_ActivityTable[i].weaponAct = SMG_SilencedActivity( s_pActivityNames[i][1] );
			s_ActivityTable[i].required = false;
		}
		s_bInitialized = true;
	}
	iActivityCount = ARRAYSIZE( s_ActivityTable );
	return s_ActivityTable;
}

Activity CSMG_Silenced::GetPlayerAttackActivity( TerrorPlayerAnimEvent event, Activity mainActivity )
{
	if ( event >= TERROR_ANIMEVENT_MELEE_SWEEP && event <= TERROR_ANIMEVENT_MELEE_STOMP )
		return BaseClass::GetPlayerAttackActivity( event, mainActivity );
	static const Activity s_Attack = SMG_SilencedActivity( "ACT_PRIMARYATTACK_SMG" );
	return s_Attack;
}

Activity CSMG_Silenced::GetPlayerReloadActivity( TerrorPlayerAnimEvent event, Activity mainActivity )
{
	static const Activity s_Reload = SMG_SilencedActivity( "ACT_RELOAD_SMG" );
	return s_Reload;
}

// S:0x3E1350; these are viewmodel layers, not player activity overrides.
Activity CSMG_Silenced::TranslateViewModelActivity( Activity activity )
{
	if ( activity == ACT_VM_PRIMARYATTACK )
		return SMG_SilencedActivity( "ACT_VM_PRIMARYATTACK_LAYER" );
	return BaseClass::TranslateViewModelActivity( activity );
}

// Native server 0x3e12f0; 0x5c5068 is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace weapon_smg_silencedPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "weapon_smg_silenced", 0x5c5068 ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		if ( !ignoreConditionals && IsWeaponAllowedForPrecache( "weapon_smg_silenced" ) )
			handler->Precache( RESOURCETYPE_ENTITY, "weapon_smg_silenced", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif

#ifndef CLIENT_DLL
BEGIN_DATADESC( CSMG_Silenced )
END_DATADESC()
#endif
