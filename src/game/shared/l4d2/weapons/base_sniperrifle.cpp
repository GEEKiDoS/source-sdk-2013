//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/base_sniperrifle.h"
#include "activitylist.h"
#ifdef CLIENT_DLL
#include "l4d2/player/c_terrorplayer.h"
#else
#include "l4d2/player/terrorplayer.h"
#endif
#include "tier0/memdbgon.h"

#ifdef CLIENT_DLL
IMPLEMENT_CLIENTCLASS( C_BaseSniperRifle, DT_BaseSniperRifle, CBaseSniperRifle )
#else
IMPLEMENT_SERVERCLASS( CBaseSniperRifle, DT_BaseSniperRifle )
#endif
BEGIN_NETWORK_TABLE( CBaseSniperRifle, DT_BaseSniperRifle )
END_NETWORK_TABLE()
BEGIN_PREDICTION_DATA( CBaseSniperRifle )
END_PREDICTION_DATA()

Activity CBaseSniperRifle::SniperActivity( const char *pName )
{
	Activity activity = (Activity)ActivityList_IndexForName( pName );
	return activity == ACT_INVALID ? ActivityList_RegisterPrivateActivity( pName ) : activity;
}

float CBaseSniperRifle::GetMaxAutoAimDeflection()
{
	// S:0x3C41F0, original v304. This is not an attack2/zoom callback.
	return IsZoomed() ? 0.99989998f : BaseClass::GetMaxAutoAimDeflection();
}

float CBaseSniperRifle::GetMaxMovementSpread() const
{
	// S:0x3C3D40. In-air spread is deliberately NOT overridden.
	return IsZoomed() ? 0.0f : GetTerrorWpnData().m_flMaxMovementSpread;
}

float CBaseSniperRifle::GetMinStandingSpread() const
{
	return IsZoomed() ? 0.0f : GetTerrorWpnData().m_flMinStandingSpread;
}

float CBaseSniperRifle::GetMinDuckingSpread() const
{
	return IsZoomed() ? 0.0f : GetTerrorWpnData().m_flMinDuckingSpread;
}

bool CBaseSniperRifle::CanFidget()
{
	// S:0x3C41D0; the idle fidget, not crosshair visibility, is inhibited.
	return !IsZoomed() && BaseClass::CanFidget();
}

acttable_t *CBaseSniperRifle::ActivityList( int &iActivityCount )
{
	// S:0x3C3D20/0x3C3D30, table0x7B82C8.
	static const char *s_pActivityNames[][2] =
	{
		{ "ACT_IDLE", "ACT_IDLE_SNIPER" },
		{ "ACT_CROUCHIDLE", "ACT_CROUCHIDLE_SNIPER" },
		{ "ACT_RUN", "ACT_RUN_SNIPER" },
		{ "ACT_IDLE_INJURED", "ACT_IDLE_INJURED_SNIPER" },
		{ "ACT_RUN_CROUCH", "ACT_RUN_CROUCH_SNIPER" },
		{ "ACT_WALK", "ACT_WALK_SNIPER" },
		{ "ACT_WALK_INJURED", "ACT_WALK_INJURED_SNIPER" },
		{ "ACT_RUN_INJURED", "ACT_RUN_INJURED_SNIPER" },
		{ "ACT_IDLE_CALM", "ACT_IDLE_CALM_SNIPER" },
		{ "ACT_WALK_CALM", "ACT_WALK_CALM_SNIPER" },
		{ "ACT_RUN_CALM", "ACT_RUN_CALM_SNIPER" },
		{ "ACT_JUMP", "ACT_JUMP_RIFLE" }
	};
	static acttable_t s_ActivityTable[ARRAYSIZE( s_pActivityNames )];
	static bool s_bInitialized = false;
	if ( !s_bInitialized )
	{
		for ( int i = 0; i < ARRAYSIZE( s_pActivityNames ); ++i )
		{
			s_ActivityTable[i].baseAct = SniperActivity( s_pActivityNames[i][0] );
			s_ActivityTable[i].weaponAct = SniperActivity( s_pActivityNames[i][1] );
			s_ActivityTable[i].required = false;
		}
		s_bInitialized = true;
	}
	iActivityCount = ARRAYSIZE( s_ActivityTable );
	return s_ActivityTable;
}

Activity CBaseSniperRifle::TranslatePlayerActivity( Activity activity )
{
	// S:0x3C4110. Running while scoped selects WALK_SNIPER.
	static const Activity s_ZoomActivities[][2] =
	{
		{ SniperActivity( "ACT_IDLE" ), SniperActivity( "ACT_IDLE_SNIPER_ZOOMED" ) },
		{ SniperActivity( "ACT_RUN" ), SniperActivity( "ACT_WALK_SNIPER" ) },
		{ SniperActivity( "ACT_CROUCHIDLE" ), SniperActivity( "ACT_CROUCHIDLE_SNIPER_ZOOMED" ) },
		{ SniperActivity( "ACT_IDLE_INJURED" ), SniperActivity( "ACT_IDLE_INJURED_SNIPER_ZOOMED" ) }
	};
	if ( IsZoomed() )
	{
		for ( int i = 0; i < ARRAYSIZE( s_ZoomActivities ); ++i )
		{
			if ( activity == s_ZoomActivities[i][0] )
				return s_ZoomActivities[i][1];
		}
	}
	return BaseClass::TranslatePlayerActivity( activity );
}

Activity CBaseSniperRifle::GetPlayerAttackActivity( TerrorPlayerAnimEvent event, Activity mainActivity )
{
	if ( event >= TERROR_ANIMEVENT_MELEE_SWEEP && event <= TERROR_ANIMEVENT_MELEE_STOMP )
		return BaseClass::GetPlayerAttackActivity( event, mainActivity );
	static const Activity s_Attack = SniperActivity( "ACT_PRIMARYATTACK_RIFLE" );
	return s_Attack;
}

Activity CBaseSniperRifle::GetPlayerReloadActivity( TerrorPlayerAnimEvent event, Activity mainActivity )
{
	static const Activity s_Reload = SniperActivity( "ACT_RELOAD_RIFLE" );
	return s_Reload;
}

Activity CBaseSniperRifle::TranslateViewModelActivity( Activity activity )
{
	// S:0x3C3DA0. Both the first model translation and the later VM layer
	// translation use this mapping; hunting's reload/pickup stages are real.
	static const Activity s_ActivityMap[][2] =
	{
		{ SniperActivity( "ACT_VM_DEPLOY" ), SniperActivity( "ACT_VM_DEPLOY_SNIPER" ) },
		{ SniperActivity( "ACT_VM_RELOAD_EMPTY" ), SniperActivity( "ACT_VM_RELOAD_EMPTY_SNIPER" ) },
		{ SniperActivity( "ACT_VM_IDLE" ), SniperActivity( "ACT_VM_IDLE_SNIPER" ) },
		{ SniperActivity( "ACT_VM_PRIMARYATTACK" ), SniperActivity( "ACT_VM_PRIMARYATTACK_SNIPER" ) },
		{ SniperActivity( "ACT_VM_RELOAD" ), SniperActivity( "ACT_VM_RELOAD_SNIPER" ) },
		{ SniperActivity( "ACT_VM_PICKUP" ), SniperActivity( "ACT_VM_PICKUP_SNIPER" ) },
		{ SniperActivity( "ACT_VM_FIDGET_LAYER" ), SniperActivity( "ACT_VM_FIDGET_SNIPER_LAYER" ) },
		{ SniperActivity( "ACT_VM_ITEMPICKUP_EXTEND" ), SniperActivity( "ACT_VM_ITEMPICKUP_EXTEND_SNIPER" ) },
		{ SniperActivity( "ACT_VM_ITEMPICKUP_LOOP" ), SniperActivity( "ACT_VM_ITEMPICKUP_LOOP_SNIPER" ) },
		{ SniperActivity( "ACT_VM_ITEMPICKUP_RETRACT" ), SniperActivity( "ACT_VM_ITEMPICKUP_RETRACT_SNIPER" ) },
		{ SniperActivity( "ACT_VM_HELPINGHAND_EXTEND" ), SniperActivity( "ACT_VM_HELPINGHAND_EXTEND_SNIPER" ) },
		{ SniperActivity( "ACT_VM_HELPINGHAND_LOOP" ), SniperActivity( "ACT_VM_HELPINGHAND_LOOP_SNIPER" ) },
		{ SniperActivity( "ACT_VM_HELPINGHAND_RETRACT" ), SniperActivity( "ACT_VM_HELPINGHAND_RETRACT_SNIPER" ) },
		{ SniperActivity( "ACT_VM_ITEMPICKUP_LOOP_SNIPER" ), SniperActivity( "ACT_VM_ITEMPICKUP_LOOP_SNIPER_LAYER" ) },
		{ SniperActivity( "ACT_VM_ITEMPICKUP_EXTEND_SNIPER" ), SniperActivity( "ACT_VM_ITEMPICKUP_EXTEND_SNIPER_LAYER" ) },
		{ SniperActivity( "ACT_VM_ITEMPICKUP_RETRACT_SNIPER" ), SniperActivity( "ACT_VM_ITEMPICKUP_RETRACT_SNIPER_LAYER" ) },
		{ SniperActivity( "ACT_VM_HELPINGHAND_LOOP_SNIPER" ), SniperActivity( "ACT_VM_HELPINGHAND_LOOP_SNIPER_LAYER" ) },
		{ SniperActivity( "ACT_VM_HELPINGHAND_EXTEND_SNIPER" ), SniperActivity( "ACT_VM_HELPINGHAND_EXTEND_SNIPER_LAYER" ) },
		{ SniperActivity( "ACT_VM_HELPINGHAND_RETRACT_SNIPER" ), SniperActivity( "ACT_VM_HELPINGHAND_RETRACT_SNIPER_LAYER" ) },
		{ SniperActivity( "ACT_VM_MELEE" ), SniperActivity( "ACT_VM_MELEE_SNIPER" ) },
		{ SniperActivity( "ACT_VM_PICKUP_CLIPIN" ), SniperActivity( "ACT_VM_PICKUP_CLIPIN_SNIPER" ) },
		{ SniperActivity( "ACT_VM_PICKUP_CHARGING" ), SniperActivity( "ACT_VM_PICKUP_CHARGING_SNIPER" ) },
		{ SniperActivity( "ACT_VM_RELOAD_CLIPOUT" ), SniperActivity( "ACT_VM_RELOAD_CLIPOUT_SNIPER" ) },
		{ SniperActivity( "ACT_VM_RELOAD_EMPTY_CLIPOUT" ), SniperActivity( "ACT_VM_RELOAD_EMPTY_CLIPOUT_SNIPER" ) },
		{ SniperActivity( "ACT_VM_RELOAD_EMPTY_CLIPIN" ), SniperActivity( "ACT_VM_RELOAD_EMPTY_CLIPIN_SNIPER" ) },
		{ SniperActivity( "ACT_VM_PICKUP_SNIPER" ), SniperActivity( "ACT_VM_PICKUP_SNIPER_LAYER" ) },
		{ SniperActivity( "ACT_VM_PICKUP_CLIPIN_SNIPER" ), SniperActivity( "ACT_VM_PICKUP_CLIPIN_SNIPER_LAYER" ) },
		{ SniperActivity( "ACT_VM_PICKUP_CHARGING_SNIPER" ), SniperActivity( "ACT_VM_PICKUP_CHARGING_SNIPER_LAYER" ) },
		{ SniperActivity( "ACT_VM_DEPLOY_SNIPER" ), SniperActivity( "ACT_VM_DEPLOY_SNIPER_LAYER" ) },
		{ SniperActivity( "ACT_VM_PRIMARYATTACK_SNIPER" ), SniperActivity( "ACT_VM_SHOOT_SNIPER_LAYER" ) },
		{ SniperActivity( "ACT_VM_RELOAD_SNIPER" ), SniperActivity( "ACT_VM_RELOAD_SNIPER_LAYER" ) },
		{ SniperActivity( "ACT_VM_RELOAD_CLIPOUT_SNIPER" ), SniperActivity( "ACT_VM_RELOAD_CLIPOUT_SNIPER_LAYER" ) },
		{ SniperActivity( "ACT_VM_RELOAD_EMPTY_SNIPER" ), SniperActivity( "ACT_VM_RELOAD_EMPTY_SNIPER_LAYER" ) },
		{ SniperActivity( "ACT_VM_RELOAD_EMPTY_CLIPOUT_SNIPER" ), SniperActivity( "ACT_VM_RELOAD_EMPTY_CLIPOUT_SNIPER_LAYER" ) },
		{ SniperActivity( "ACT_VM_RELOAD_EMPTY_CLIPIN_SNIPER" ), SniperActivity( "ACT_VM_RELOAD_EMPTY_CLIPIN_SNIPER_LAYER" ) },
		{ SniperActivity( "ACT_VM_MELEE_SNIPER" ), SniperActivity( "ACT_VM_MELEE_SNIPER_LAYER" ) }
	};
	for ( int i = 0; i < ARRAYSIZE( s_ActivityMap ); ++i )
	{
		if ( activity == s_ActivityMap[i][0] )
			return s_ActivityMap[i][1];
	}
	return BaseClass::TranslateViewModelActivity( activity );
}

int CBaseSniperRifle::GetViewModelLayer( Activity activity )
{
	// S:0x3C41B0; hunting has its own selector for the sniper melee form.
	static const Activity s_Melee = SniperActivity( "ACT_VM_MELEE_LAYER" );
	return activity == s_Melee ? 3 : 0;
}

#ifndef CLIENT_DLL
BEGIN_DATADESC( CBaseSniperRifle )
END_DATADESC()
#endif
