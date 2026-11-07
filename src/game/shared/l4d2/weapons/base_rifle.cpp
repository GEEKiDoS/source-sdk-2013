//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/base_rifle.h"
#include "activitylist.h"
#include "tier0/memdbgon.h"

#ifdef CLIENT_DLL
IMPLEMENT_CLIENTCLASS( C_BaseRifle, DT_BaseRifle, CBaseRifle )
#else
IMPLEMENT_SERVERCLASS( CBaseRifle, DT_BaseRifle )
#endif
BEGIN_NETWORK_TABLE( CBaseRifle, DT_BaseRifle )
END_NETWORK_TABLE()

BEGIN_PREDICTION_DATA( CBaseRifle )
END_PREDICTION_DATA()

// S3C27E0/3C27F0, table7B8028: M16, AK47, SCAR and M60 share these
// selectors. Retail activity numbers are not portable; use this DLL's registry.
acttable_t *CBaseRifle::ActivityList( int &iActivityCount )
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
			Activity activity[2];
			for ( int j = 0; j < 2; ++j )
			{
				activity[j] = (Activity)ActivityList_IndexForName( s_pActivityNames[i][j] );
				if ( activity[j] == ACT_INVALID )
					activity[j] = ActivityList_RegisterPrivateActivity( s_pActivityNames[i][j] );
			}
			s_ActivityTable[i].baseAct = activity[0];
			s_ActivityTable[i].weaponAct = activity[1];
			s_ActivityTable[i].required = false;
		}
		s_bInitialized = true;
	}
	iActivityCount = ARRAYSIZE( s_ActivityTable );
	return s_ActivityTable;
}

static Activity RiflePlayerActivity( const char *pName )
{
	Activity activity = (Activity)ActivityList_IndexForName( pName );
	return activity == ACT_INVALID ? ActivityList_RegisterPrivateActivity( pName ) : activity;
}

// S:0x3C2820 ->0x24EA30. Movement is the selector's explicit input,
// not a different primary-fire activity for crouched or standing survivors.
Activity CBaseRifle::GetPlayerAttackActivity( TerrorPlayerAnimEvent event, Activity mainActivity )
{
	const bool bMoving = mainActivity != ACT_IDLE;
	static const Activity s_Activity[][2] =
	{
		{ RiflePlayerActivity( "ACT_MELEE_SWEEP_RIFLE_IDLE" ), RiflePlayerActivity( "ACT_MELEE_SWEEP_RIFLE_RUN" ) },
		{ RiflePlayerActivity( "ACT_MELEE_SHOVE_RIFLE_IDLE" ), RiflePlayerActivity( "ACT_MELEE_SHOVE_RIFLE_RUN" ) },
		{ RiflePlayerActivity( "ACT_MELEE_STRAIGHT_RIFLE_IDLE" ), RiflePlayerActivity( "ACT_MELEE_STRAIGHT_RIFLE_RUN" ) },
		{ RiflePlayerActivity( "ACT_MELEE_STOMP_RIFLE_IDLE" ), RiflePlayerActivity( "ACT_MELEE_STOMP_RIFLE_WALK" ) }
	};
	if ( event >= TERROR_ANIMEVENT_MELEE_SWEEP && event <= TERROR_ANIMEVENT_MELEE_STOMP )
		return s_Activity[event - TERROR_ANIMEVENT_MELEE_SWEEP][bMoving ? 1 : 0];
	static const Activity s_PrimaryAttack = RiflePlayerActivity( "ACT_PRIMARYATTACK_RIFLE" );
	return s_PrimaryAttack;
}

Activity CBaseRifle::GetPlayerReloadActivity( TerrorPlayerAnimEvent event, Activity mainActivity )
{
	// S:0x3C2850; the original is M4, not ACT_RELOAD_RIFLE.
	static const Activity s_Reload = RiflePlayerActivity( "ACT_RELOAD_M4" );
	return s_Reload;
}

// C2F16C0: only primary fire differs from the genuine common translator.
Activity CBaseRifle::TranslateViewModelActivity( Activity activity )
{
	if ( activity == TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_PRIMARYATTACK ) )
		return TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_PRIMARYATTACK_LAYER );
	return BaseClass::TranslateViewModelActivity( activity );
}

#ifndef CLIENT_DLL
BEGIN_DATADESC( CBaseRifle )
END_DATADESC()
#endif
