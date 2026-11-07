//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/base_autoshotgun.h"
#include "activitylist.h"
#include "tier0/memdbgon.h"

#ifdef CLIENT_DLL
IMPLEMENT_CLIENTCLASS( C_BaseAutoShotgun, DT_BaseAutoShotgun, CBaseAutoShotgun )
#else
IMPLEMENT_SERVERCLASS( CBaseAutoShotgun, DT_BaseAutoShotgun )
#endif
BEGIN_NETWORK_TABLE( CBaseAutoShotgun, DT_BaseAutoShotgun )
END_NETWORK_TABLE()

BEGIN_PREDICTION_DATA( CBaseAutoShotgun )
END_PREDICTION_DATA()

static Activity AutoShotgunActivity( const char *pName )
{
	Activity activity = (Activity)ActivityList_IndexForName( pName );
	return activity == ACT_INVALID ? ActivityList_RegisterPrivateActivity( pName ) : activity;
}

#pragma float_control( precise, on, push )
void CBaseAutoShotgun::PrimaryAttack()
{
	// S:0x3C1790 / C:0x2F0970. This extra 0.2-second held-fire gate
	// is in addition to IsFullyAutomatic and the inherited release gate.
	// It is command time minus the existing deadline, not time since shot;
	// native COMISS/JA blocks only ordered deltas below the threshold.
	if ( GetTerrorPlayerOwner() && ( !m_isHoldingFireButton || !( 0.2f > gpGlobals->curtime - m_flNextPrimaryAttack ) ) )
		BaseClass::PrimaryAttack();
}
#pragma float_control( pop )

double CBaseAutoShotgun::GetFireRate()
{
	// S:0x3C1580 / C:0x2F0700. Neither CycleTime from the script nor
	// CTerrorGun's 1.5x empty-clip rate is the original auto-shotgun getter.
	return 0.25f;
}

acttable_t *CBaseAutoShotgun::ActivityList( int &iActivityCount )
{
	// S:0x3C1690/0x3C16A0, table S:0x7B7CD0. The mixed shotgun /
	// pumpshotgun injured and calm activities are intentional original data.
	static const char *s_pNames[][2] =
	{
		{ "ACT_IDLE", "ACT_IDLE_SHOTGUN" },
		{ "ACT_CROUCHIDLE", "ACT_CROUCHIDLE_SHOTGUN" },
		{ "ACT_RUN_CROUCH", "ACT_RUN_CROUCH_SHOTGUN" },
		{ "ACT_WALK", "ACT_WALK_SHOTGUN" },
		{ "ACT_RUN", "ACT_RUN_SHOTGUN" },
		{ "ACT_IDLE_INJURED", "ACT_IDLE_INJURED_PUMPSHOTGUN" },
		{ "ACT_WALK_INJURED", "ACT_WALK_INJURED_PUMPSHOTGUN" },
		{ "ACT_RUN_INJURED", "ACT_RUN_INJURED_PUMPSHOTGUN" },
		{ "ACT_IDLE_CALM", "ACT_IDLE_CALM_SHOTGUN" },
		{ "ACT_WALK_CALM", "ACT_WALK_CALM_PUMPSHOTGUN" },
		{ "ACT_RUN_CALM", "ACT_RUN_CALM_PUMPSHOTGUN" },
		{ "ACT_JUMP", "ACT_JUMP_SHOTGUN" }
	};
	static acttable_t s_Table[ARRAYSIZE( s_pNames )];
	static bool s_bInitialized = false;
	if ( !s_bInitialized )
	{
		for ( int i = 0; i < ARRAYSIZE( s_pNames ); ++i )
		{
			s_Table[i].baseAct = AutoShotgunActivity( s_pNames[i][0] );
			s_Table[i].weaponAct = AutoShotgunActivity( s_pNames[i][1] );
			s_Table[i].required = false;
		}
		s_bInitialized = true;
	}
	iActivityCount = ARRAYSIZE( s_Table );
	return s_Table;
}

Activity CBaseAutoShotgun::GetPlayerAttackActivity( TerrorPlayerAnimEvent event, Activity mainActivity )
{
	// S:0x3C16B0: inherited rifle-name melee translations, XM1014 fire.
	if ( event >= TERROR_ANIMEVENT_MELEE_SWEEP && event <= TERROR_ANIMEVENT_MELEE_STOMP )
		return BaseClass::GetPlayerAttackActivity( event, mainActivity );
	static const Activity s_Primary = AutoShotgunActivity( "ACT_PRIMARYATTACK_XM1014" );
	return s_Primary;
}

Activity CBaseAutoShotgun::GetPlayerReloadActivity( TerrorPlayerAnimEvent event, Activity mainActivity )
{
	// S:0x3C16E0; do not choose these from the retained VM stage field.
	static const Activity s_Loop = AutoShotgunActivity( "ACT_RELOAD_SHOTGUN_LOOP" );
	static const Activity s_Start = AutoShotgunActivity( "ACT_RELOAD_SHOTGUN_START" );
	static const Activity s_End = AutoShotgunActivity( "ACT_RELOAD_SHOTGUN_END" );
	if ( event == TERROR_ANIMEVENT_RELOAD_LOOP )
		return s_Loop;
	if ( event == TERROR_ANIMEVENT_RELOAD_START )
		return s_Start;
	return s_End;
}

#ifndef CLIENT_DLL
BEGIN_DATADESC( CBaseAutoShotgun )
END_DATADESC()
#endif
