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
#define CSMG_MP5 C_SMG_MP5
#endif

class CSMG_MP5 : public CTerrorGun
{
public:
	DECLARE_CLASS( CSMG_MP5, CTerrorGun );
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();
	virtual int GetWeaponID() const OVERRIDE { return TERROR_WEAPON_SMG_MP5; }
	virtual acttable_t *ActivityList( int &iActivityCount ) OVERRIDE;
	virtual Activity GetPlayerAttackActivity( TerrorPlayerAnimEvent event, Activity mainActivity ) OVERRIDE;
	virtual Activity GetPlayerReloadActivity( TerrorPlayerAnimEvent event, Activity mainActivity ) OVERRIDE;
	virtual Activity TranslateViewModelActivity( Activity activity ) OVERRIDE;
};

IMPLEMENT_NETWORKCLASS_ALIASED( SMG_MP5, DT_SMG_MP5 )
BEGIN_NETWORK_TABLE( CSMG_MP5, DT_SMG_MP5 )
END_NETWORK_TABLE()
BEGIN_PREDICTION_DATA( CSMG_MP5 )
END_PREDICTION_DATA()
LINK_ENTITY_TO_CLASS( weapon_smg_mp5, CSMG_MP5 );
#ifndef L4D2
PRECACHE_WEAPON_REGISTER( weapon_smg_mp5 );
#endif

static Activity SMG_MP5Activity( const char *pName )
{
	Activity activity = (Activity)ActivityList_IndexForName( pName );
	return activity == ACT_INVALID ? ActivityList_RegisterPrivateActivity( pName ) : activity;
}

// S:0x3E0FB0, table0x7BCBD0. The MP5 intentionally uses RIFLE locomotion.
acttable_t *CSMG_MP5::ActivityList( int &iActivityCount )
{
	static const char *s_pActivityNames[][2] =
	{
		{ "ACT_IDLE", "ACT_IDLE_RIFLE" },
		{ "ACT_CROUCHIDLE", "ACT_CROUCHIDLE_RIFLE" },
		{ "ACT_RUN_CROUCH", "ACT_RUN_CROUCH_RIFLE" },
		{ "ACT_WALK", "ACT_WALK_RIFLE" },
		{ "ACT_RUN", "ACT_RUN_RIFLE" },
		{ "ACT_IDLE_INJURED", "ACT_IDLE_INJURED_RIFLE" },
		{ "ACT_WALK_INJURED", "ACT_WALK_INJURED_RIFLE" },
		{ "ACT_RUN_INJURED", "ACT_RUN_INJURED_RIFLE" },
		{ "ACT_IDLE_CALM", "ACT_IDLE_CALM_RIFLE" },
		{ "ACT_WALK_CALM", "ACT_WALK_CALM_RIFLE" },
		{ "ACT_RUN_CALM", "ACT_RUN_CALM_RIFLE" },
		{ "ACT_JUMP", "ACT_JUMP_RIFLE" }
	};
	static acttable_t s_ActivityTable[ARRAYSIZE( s_pActivityNames )];
	static bool s_bInitialized = false;
	if ( !s_bInitialized )
	{
		for ( int i = 0; i < ARRAYSIZE( s_pActivityNames ); ++i )
		{
			s_ActivityTable[i].baseAct = SMG_MP5Activity( s_pActivityNames[i][0] );
			s_ActivityTable[i].weaponAct = SMG_MP5Activity( s_pActivityNames[i][1] );
			s_ActivityTable[i].required = false;
		}
		s_bInitialized = true;
	}
	iActivityCount = ARRAYSIZE( s_ActivityTable );
	return s_ActivityTable;
}

Activity CSMG_MP5::GetPlayerAttackActivity( TerrorPlayerAnimEvent event, Activity mainActivity )
{
	if ( event >= TERROR_ANIMEVENT_MELEE_SWEEP && event <= TERROR_ANIMEVENT_MELEE_STOMP )
		return BaseClass::GetPlayerAttackActivity( event, mainActivity );
	static const Activity s_Attack = SMG_MP5Activity( "ACT_PRIMARYATTACK_SMG" );
	return s_Attack;
}

Activity CSMG_MP5::GetPlayerReloadActivity( TerrorPlayerAnimEvent event, Activity mainActivity )
{
	static const Activity s_Reload = SMG_MP5Activity( "ACT_RELOAD_RIFLE" );
	return s_Reload;
}

// S:0x3E0FD0; these are viewmodel layers, not player activity overrides.
Activity CSMG_MP5::TranslateViewModelActivity( Activity activity )
{
	if ( activity == ACT_VM_PRIMARYATTACK )
		return SMG_MP5Activity( "ACT_VM_PRIMARYATTACK_LAYER" );
	if ( activity == SMG_MP5Activity( "ACT_VM_RELOAD_CLIPOUT" ) )
		return SMG_MP5Activity( "ACT_VM_RELOAD_CLIPOUT_SMG_LAYER" );
	return BaseClass::TranslateViewModelActivity( activity );
}

// Native server 0x3e0f70; 0x5c5050 is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace weapon_smg_mp5Precache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "weapon_smg_mp5", 0x5c5050 ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		if ( !ignoreConditionals && IsWeaponAllowedForPrecache( "weapon_smg_mp5" ) )
			handler->Precache( RESOURCETYPE_ENTITY, "weapon_smg_mp5", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif

#ifndef CLIENT_DLL
BEGIN_DATADESC( CSMG_MP5 )
END_DATADESC()
#endif
