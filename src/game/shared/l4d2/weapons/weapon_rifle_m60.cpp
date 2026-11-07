//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/base_rifle.h"
#include "activitylist.h"
#include "igameevents.h"
#ifdef CLIENT_DLL
#include "l4d2/player/c_terrorplayer.h"
#define CRifle_M60 C_Rifle_M60
#else
#include "l4d2/player/terrorplayer.h"
#endif
#ifdef L4D2
#include "precache_register.h"
#endif
#ifdef L4D2
#include "l4d2/weapons/terror_precache_weapon.h"
#endif
#include "tier0/memdbgon.h"

class CRifle_M60 : public CBaseRifle
{
public:
	DECLARE_CLASS( CRifle_M60, CBaseRifle );
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();
	CRifle_M60();
	virtual int GetWeaponID() const OVERRIDE { return TERROR_WEAPON_RIFLE_M60; }
	// S:0x3E0540: exhaustion/drop never merges back into a weapon spawner.
	virtual bool ShouldMergeWithWeaponSpawn() const OVERRIDE { return false; }
	virtual void PrimaryAttack() OVERRIDE;
	virtual void WeaponIdle() OVERRIDE;
	virtual acttable_t *ActivityList( int &iActivityCount ) OVERRIDE;
#ifndef CLIENT_DLL
	virtual int ObjectCaps() OVERRIDE;
#endif

private:
#ifdef CLIENT_DLL
	bool m_bFiringStreak;
#else
	CountdownTimer m_sustainedFireTimer;
#endif
};

IMPLEMENT_NETWORKCLASS_ALIASED( Rifle_M60, DT_Rifle_M60 )
BEGIN_NETWORK_TABLE( CRifle_M60, DT_Rifle_M60 )
END_NETWORK_TABLE()
BEGIN_PREDICTION_DATA( CRifle_M60 )
END_PREDICTION_DATA()
LINK_ENTITY_TO_CLASS( weapon_rifle_m60, CRifle_M60 );
#ifndef L4D2
PRECACHE_WEAPON_REGISTER( weapon_rifle_m60 );
#endif

CRifle_M60::CRifle_M60()
{
#ifdef CLIENT_DLL
	// C:0x306E20; this local event latch is not a predicted/network field.
	m_bFiringStreak = false;
#else
	// S:0x3E04A0; the speech timer exists on the server only.
	m_sustainedFireTimer.Invalidate();
#endif
}

void CRifle_M60::PrimaryAttack()
{
	BaseClass::PrimaryAttack();
#ifdef CLIENT_DLL
	// C:0x306D20: set even when the common attack did not produce a shot.
	m_bFiringStreak = true;
#else
	// S:0x3E0580. WeaponIdle, not button release polling, ends this timer.
	if ( m_isHoldingFireButton && !m_sustainedFireTimer.HasStarted() )
		m_sustainedFireTimer.Start( 2.0f );
	CTerrorPlayer *pPlayer = GetTerrorPlayerOwner();
	if ( pPlayer )
	{
		if ( m_sustainedFireTimer.HasStarted() && m_sustainedFireTimer.IsElapsed() )
			pPlayer->SpeakIfAllowed( "MGUsed", 1, NULL, NULL, 0, NULL );
		if ( m_iClip1 == 0 )
		{
			pPlayer->DropWeapon( this, true, NULL );
			pPlayer->SwitchToNextBestWeapon( NULL );
			// S:0x066C00 is SUB_StartFadeOut, not immediate UTIL_Remove.
			SUB_StartFadeOut( 2.0f, false );
		}
	}
#endif
}

void CRifle_M60::WeaponIdle()
{
#ifdef CLIENT_DLL
	// C:0x306D40 emits the local streak boundary once, only in-game.
	if ( m_bFiringStreak && engine->IsInGame() && engine->IsConnected() )
	{
		m_bFiringStreak = false;
		IGameEvent *pEvent = gameeventmanager->CreateEvent( "m60_streak_ended" );
		if ( pEvent )
			gameeventmanager->FireEventClientSide( pEvent );
	}
#else
	// S:0x3E06C0.
	m_sustainedFireTimer.Invalidate();
#endif
	BaseClass::WeaponIdle();
}

#ifndef CLIENT_DLL
int CRifle_M60::ObjectCaps()
{
	// S:0x3E0450. Empty M60s are no longer impulse-use pickups.
	return m_iClip1 != 0 ? BaseClass::ObjectCaps() : BaseClass::ObjectCaps() & ~FCAP_IMPULSE_USE;
}
#endif

static Activity M60Activity( const char *pName )
{
	Activity activity = (Activity)ActivityList_IndexForName( pName );
	return activity == ACT_INVALID ? ActivityList_RegisterPrivateActivity( pName ) : activity;
}

// S:0x3E0370/0x3E0380, table0x7BC528. M60 uses military-sniper
// locomotion but retains the rifle's attack, reload and jump selectors.
acttable_t *CRifle_M60::ActivityList( int &iActivityCount )
{
	static const char *s_pActivityNames[][2] =
	{
		{ "ACT_IDLE", "ACT_IDLE_SNIPER_MILITARY" },
		{ "ACT_CROUCHIDLE", "ACT_CROUCHIDLE_SNIPER_MILITARY" },
		{ "ACT_RUN", "ACT_RUN_SNIPER_MILITARY" },
		{ "ACT_IDLE_INJURED", "ACT_IDLE_INJURED_SNIPER_MILITARY" },
		{ "ACT_RUN_CROUCH", "ACT_RUN_CROUCH_SNIPER_MILITARY" },
		{ "ACT_WALK", "ACT_WALK_SNIPER_MILITARY" },
		{ "ACT_WALK_INJURED", "ACT_WALK_INJURED_SNIPER_MILITARY" },
		{ "ACT_RUN_INJURED", "ACT_RUN_INJURED_SNIPER_MILITARY" },
		{ "ACT_IDLE_CALM", "ACT_IDLE_CALM_SNIPER_MILITARY" },
		{ "ACT_WALK_CALM", "ACT_WALK_CALM_SNIPER_MILITARY" },
		{ "ACT_RUN_CALM", "ACT_RUN_CALM_SNIPER_MILITARY" },
		{ "ACT_JUMP", "ACT_JUMP_RIFLE" }
	};
	static acttable_t s_ActivityTable[ARRAYSIZE( s_pActivityNames )];
	static bool s_bInitialized = false;
	if ( !s_bInitialized )
	{
		for ( int i = 0; i < ARRAYSIZE( s_pActivityNames ); ++i )
		{
			s_ActivityTable[i].baseAct = M60Activity( s_pActivityNames[i][0] );
			s_ActivityTable[i].weaponAct = M60Activity( s_pActivityNames[i][1] );
			s_ActivityTable[i].required = false;
		}
		s_bInitialized = true;
	}
	iActivityCount = ARRAYSIZE( s_ActivityTable );
	return s_ActivityTable;
}

// Native server 0x3e0390; 0x5c4ff0 is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace weapon_rifle_m60Precache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "weapon_rifle_m60", 0x5c4ff0 ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		if ( !ignoreConditionals && IsWeaponAllowedForPrecache( "weapon_rifle_m60" ) )
			handler->Precache( RESOURCETYPE_ENTITY, "weapon_rifle_m60", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif

#ifndef CLIENT_DLL
BEGIN_DATADESC( CRifle_M60 )
END_DATADESC()
#endif
