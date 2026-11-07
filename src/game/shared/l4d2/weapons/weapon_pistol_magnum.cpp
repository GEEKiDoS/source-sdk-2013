//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/terror_gun.h"
#include "activitylist.h"
#include "datacache/imdlcache.h"
#include "rumble_shared.h"
#ifdef CLIENT_DLL
#include "l4d2/player/c_terrorplayer.h"
#include "baseviewmodel_shared.h"
#endif
#ifdef L4D2
#include "precache_register.h"
#endif
#include "tier0/memdbgon.h"

#ifdef CLIENT_DLL
#define CMagnumPistol C_MagnumPistol
#endif

class CMagnumPistol : public CTerrorGun
{
public:
	DECLARE_CLASS( CMagnumPistol, CTerrorGun );
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();
	CMagnumPistol();
	virtual void Precache() OVERRIDE;
	virtual bool Deploy() OVERRIDE;
	virtual void WeaponIdle() OVERRIDE;
	virtual int GetWeaponID() const OVERRIDE { return TERROR_WEAPON_PISTOL_MAGNUM; }
	virtual bool IsFullyAutomatic() const OVERRIDE { return false; }
	virtual bool CanDualWield() const OVERRIDE { return false; }
	virtual const char *GetWorldModel() const OVERRIDE;
	virtual const char *GetPlayerModel() const OVERRIDE;
	virtual int GetRumbleEffect() const OVERRIDE;
	virtual Activity GetDrawActivity() OVERRIDE { return ACT_VM_DRAW; }
	virtual Activity GetReloadActivity() OVERRIDE;
	virtual Activity GetPrimaryAttackActivity() OVERRIDE;
	virtual acttable_t *ActivityList( int &iActivityCount ) OVERRIDE;
	virtual Activity GetPlayerAttackActivity( TerrorPlayerAnimEvent event, Activity mainActivity ) OVERRIDE;
	virtual Activity GetPlayerReloadActivity( TerrorPlayerAnimEvent event, Activity mainActivity ) OVERRIDE;
	virtual Activity GetPlayerDeployActivity( TerrorPlayerAnimEvent event, Activity mainActivity ) OVERRIDE;
	virtual Activity TranslateViewModelActivity( Activity activity ) OVERRIDE;
#ifdef CLIENT_DLL
	virtual int GetTerrorMuzzleAttachment() const OVERRIDE;
#endif

private:
	int m_iSingleWorldModelIndex;
};

IMPLEMENT_NETWORKCLASS_ALIASED( MagnumPistol, DT_MagnumPistol )
BEGIN_NETWORK_TABLE( CMagnumPistol, DT_MagnumPistol )
END_NETWORK_TABLE()
BEGIN_PREDICTION_DATA( CMagnumPistol )
END_PREDICTION_DATA()
LINK_ENTITY_TO_CLASS( weapon_pistol_magnum, CMagnumPistol );
#ifndef L4D2
PRECACHE_WEAPON_REGISTER( weapon_pistol_magnum );
#endif

static Activity MagnumActivity( const char *pName )
{
	Activity activity = (Activity)ActivityList_IndexForName( pName );
	return activity == ACT_INVALID ? ActivityList_RegisterPrivateActivity( pName ) : activity;
}

CMagnumPistol::CMagnumPistol()
{
	// S:0x3DF6A0 / C:0x3060F0. No independent Magnum fire state.
	m_inInitialPickup = false;
	m_iSingleWorldModelIndex = 0;
}

void CMagnumPistol::Precache()
{
	BaseClass::Precache();
#ifndef CLIENT_DLL
	const bool bWasLocked = engine->LockNetworkStringTables( false );
#endif
	m_iSingleWorldModelIndex = PrecacheModel( GetTerrorWpnData().szWorldModel );
#ifndef CLIENT_DLL
	engine->LockNetworkStringTables( bWasLocked );
#endif
}

const char *CMagnumPistol::GetWorldModel() const
{
	return GetTerrorWpnData().szWorldModel;
}

const char *CMagnumPistol::GetPlayerModel() const
{
	return GetTerrorWpnData().szWorldModel;
}

bool CMagnumPistol::Deploy()
{
	// S:0x3DF560 / C:0x305F10. This deliberately bypasses Gun::Deploy.
	MDLCACHE_CRITICAL_SECTION();
	return DefaultDeploy( (char *)GetViewModel( 0 ), (char *)GetPlayerModel(), GetDrawActivity(), (char *)GetAnimPrefix() );
}

void CMagnumPistol::WeaponIdle()
{
	// S:0x3DF480. No dual one-round lowered-idle exception on the Magnum.
	if ( HasWeaponIdleTimeElapsed() )
	{
		SetWeaponIdleTime( GetTerrorWpnData().m_flIdleInterval + gpGlobals->curtime );
		SendWeaponAnim( ACT_VM_IDLE );
	}
}

Activity CMagnumPistol::GetReloadActivity()
{
	// S:0x3DF660 retains the inherited dual-state test even though this
	// class cannot acquire a second Magnum through the pickup producer.
	return ( !m_isDualWielding || m_iClip1 > 1 ) && m_iClip1 != 0 ? ACT_VM_RELOAD : MagnumActivity( "ACT_VM_RELOAD_EMPTY" );
}

Activity CMagnumPistol::GetPrimaryAttackActivity()
{
	// S:0x3DF640, sampled after the shared clip decrement.
	return m_iClip1 != 0 ? ACT_VM_PRIMARYATTACK : ACT_VM_DRYFIRE;
}

int CMagnumPistol::GetRumbleEffect() const
{
	// S:0x3DF600 preserves the original inherited-dual quirk.
	return m_isDualWielding && m_iClip1 % 2 != 1 ? RUMBLE_PISTOL_LEFT : BaseClass::GetRumbleEffect();
}

#ifdef CLIENT_DLL
int CMagnumPistol::GetTerrorMuzzleAttachment() const
{
	// C:0x305F80 uses the owner's viewmodel cache, not the carried-model cache.
	CTerrorPlayer *pPlayer = GetTerrorPlayerOwner();
	if ( !pPlayer )
		return 1;
	CBaseViewModel *pViewModel = pPlayer->GetViewModel( 0 );
	if ( !pViewModel )
		return 1;
	const bool right = m_iClip1 % 2 == 1;
	return pViewModel->GetMuzzleFlashAttachment( IsDualWielding(), right );
}
#endif

acttable_t *CMagnumPistol::ActivityList( int &iActivityCount )
{
	// S:0x3DF350, table0x7BBD48.
	static const char *s_pActivityNames[][2] =
	{
		{ "ACT_IDLE", "ACT_IDLE_PISTOL" },
		{ "ACT_CROUCHIDLE", "ACT_CROUCHIDLE_PISTOL" },
		{ "ACT_RUN_CROUCH", "ACT_RUN_CROUCH_PISTOL" },
		{ "ACT_WALK", "ACT_WALK_PISTOL" },
		{ "ACT_RUN", "ACT_RUN_PISTOL" },
		{ "ACT_IDLE_INJURED", "ACT_IDLE_INJURED_PISTOL" },
		{ "ACT_WALK_INJURED", "ACT_WALK_INJURED_PISTOL" },
		{ "ACT_RUN_INJURED", "ACT_RUN_INJURED_PISTOL" },
		{ "ACT_IDLE_CALM", "ACT_IDLE_CALM_PISTOL" },
		{ "ACT_WALK_CALM", "ACT_WALK_CALM_PISTOL" },
		{ "ACT_RUN_CALM", "ACT_RUN_CALM_PISTOL" },
		{ "ACT_JUMP", "ACT_JUMP_PISTOL" }
	};
	static acttable_t s_ActivityTable[ARRAYSIZE( s_pActivityNames )];
	static bool s_bInitialized = false;
	if ( !s_bInitialized )
	{
		for ( int i = 0; i < ARRAYSIZE( s_pActivityNames ); ++i )
		{
			s_ActivityTable[i].baseAct = MagnumActivity( s_pActivityNames[i][0] );
			s_ActivityTable[i].weaponAct = MagnumActivity( s_pActivityNames[i][1] );
			s_ActivityTable[i].required = false;
		}
		s_bInitialized = true;
	}
	iActivityCount = ARRAYSIZE( s_ActivityTable );
	return s_ActivityTable;
}

Activity CMagnumPistol::GetPlayerAttackActivity( TerrorPlayerAnimEvent event, Activity mainActivity )
{
	if ( event >= TERROR_ANIMEVENT_MELEE_SWEEP && event <= TERROR_ANIMEVENT_MELEE_STOMP )
		return BaseClass::GetPlayerAttackActivity( event, mainActivity );
	static const Activity s_Attack = MagnumActivity( "ACT_PRIMARYATTACK_PISTOL" );
	return s_Attack;
}

Activity CMagnumPistol::GetPlayerReloadActivity( TerrorPlayerAnimEvent event, Activity mainActivity )
{
	static const Activity s_Reload = MagnumActivity( "ACT_RELOAD_PISTOL" );
	return s_Reload;
}

Activity CMagnumPistol::GetPlayerDeployActivity( TerrorPlayerAnimEvent event, Activity mainActivity )
{
	static const Activity s_Deploy = MagnumActivity( "ACT_DEPLOY_PISTOL" );
	return s_Deploy;
}

Activity CMagnumPistol::TranslateViewModelActivity( Activity activity )
{
	if ( activity == ACT_VM_PRIMARYATTACK )
		return MagnumActivity( "ACT_VM_PRIMARYATTACK_LAYER" );
	return BaseClass::TranslateViewModelActivity( activity );
}

// Native server 0x3df320; 0x5c4f78 is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace weapon_pistol_magnumPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "weapon_pistol_magnum", 0x5c4f78 ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_ENTITY, "weapon_pistol_magnum", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif

#ifndef CLIENT_DLL
BEGIN_DATADESC( CMagnumPistol )
END_DATADESC()
#endif
