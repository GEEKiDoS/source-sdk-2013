//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/terror_gun.h"
#include "activitylist.h"
#include "datacache/imdlcache.h"
#include "rumble_shared.h"
#ifdef CLIENT_DLL
#include "l4d2/player/c_terrorplayer.h"
#include "baseviewmodel_shared.h"
#define CPistol C_Pistol
#else
#include "l4d2/player/terrorplayer.h"
#include "l4d2/weapons/weapon_spawn.h"
#include "vphysics_interface.h"
#endif
#ifdef L4D2
#include "precache_register.h"
#endif
#include "tier0/memdbgon.h"

class CPistol : public CTerrorGun
{
public:
	DECLARE_CLASS( CPistol, CTerrorGun );
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();
	CPistol();
	virtual void Precache() OVERRIDE;
	virtual bool Deploy() OVERRIDE;
	virtual void PrimaryAttack() OVERRIDE;
	virtual void WeaponIdle() OVERRIDE;
	virtual double GetFireRate() OVERRIDE;
	virtual Activity GetDrawActivity() OVERRIDE { return ACT_VM_DRAW; }
	virtual Activity GetReloadActivity() OVERRIDE;
	virtual Activity GetPrimaryAttackActivity() OVERRIDE;
	virtual int GetWeaponID() const OVERRIDE { return TERROR_WEAPON_PISTOL; }
	virtual bool IsFullyAutomatic() const OVERRIDE { return false; }
	virtual bool CanDualWield() const OVERRIDE { return true; }
	virtual const char *GetWorldModel() const OVERRIDE;
	virtual const char *GetPlayerModel() const OVERRIDE;
	virtual int GetRumbleEffect() const OVERRIDE;
	virtual bool CanFidget() OVERRIDE;
	virtual acttable_t *ActivityList( int &iActivityCount ) OVERRIDE;
	virtual Activity TranslatePlayerActivity( Activity activity ) OVERRIDE;
	virtual Activity GetPlayerAttackActivity( TerrorPlayerAnimEvent event, Activity mainActivity ) OVERRIDE;
	virtual Activity GetPlayerReloadActivity( TerrorPlayerAnimEvent event, Activity mainActivity ) OVERRIDE;
	virtual Activity GetPlayerDeployActivity( TerrorPlayerAnimEvent event, Activity mainActivity ) OVERRIDE;
	virtual Activity TranslateViewModelActivity( Activity activity ) OVERRIDE;
#ifdef CLIENT_DLL
	virtual int GetTerrorMuzzleAttachment() const OVERRIDE;
#else
	virtual void Drop( const Vector &vecVelocity ) OVERRIDE;
#endif

private:
	int m_iDualWorldModelIndex;
	int m_iSingleWorldModelIndex;
};

IMPLEMENT_NETWORKCLASS_ALIASED( Pistol, DT_Pistol )
BEGIN_NETWORK_TABLE( CPistol, DT_Pistol )
END_NETWORK_TABLE()
BEGIN_PREDICTION_DATA( CPistol )
END_PREDICTION_DATA()
LINK_ENTITY_TO_CLASS( weapon_pistol, CPistol );
#ifndef L4D2
PRECACHE_WEAPON_REGISTER( weapon_pistol );
#endif

static Activity PistolActivity( const char *pName )
{
	Activity activity = (Activity)ActivityList_IndexForName( pName );
	return activity == ACT_INVALID ? ActivityList_RegisterPrivateActivity( pName ) : activity;
}

CPistol::CPistol()
{
	// S:0x3DEF40 / C:0x305BE0. All dual state remains CTerrorGun-owned.
	m_inInitialPickup = false;
	m_iDualWorldModelIndex = 0;
	m_iSingleWorldModelIndex = 0;
}

void CPistol::Precache()
{
	// S:0x3DE9A0 / C:0x3055A0; these are player/world assets, not two entities.
	BaseClass::Precache();
#ifndef CLIENT_DLL
	const bool bWasLocked = engine->LockNetworkStringTables( false );
#endif
	m_iDualWorldModelIndex = PrecacheModel( GetTerrorWpnData().m_szDualPlayerModel );
	m_iSingleWorldModelIndex = PrecacheModel( GetTerrorWpnData().szWorldModel );
#ifndef CLIENT_DLL
	engine->LockNetworkStringTables( bWasLocked );
#endif
}

const char *CPistol::GetWorldModel() const
{
	// S:0x3DEA10: dropped/world identity remains the single pistol.
	return GetTerrorWpnData().szWorldModel;
}

const char *CPistol::GetPlayerModel() const
{
	// S:0x3DEA20: the carried model is the dual asset while wielding both.
	return m_isDualWielding ? GetTerrorWpnData().m_szDualPlayerModel : GetTerrorWpnData().szWorldModel;
}

bool CPistol::Deploy()
{
	// S:0x3DED50 / C:0x3058F0. Sidearms use DefaultDeploy directly;
	// CTerrorGun::Deploy's stored-single-clip normalization is not this route.
	MDLCACHE_CRITICAL_SECTION();
	return DefaultDeploy( (char *)GetViewModel( 0 ), (char *)GetPlayerModel(), GetDrawActivity(), (char *)GetAnimPrefix() );
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
void CPistol::PrimaryAttack()
{
	BaseClass::PrimaryAttack();
	if ( IsDualWielding() && m_iClip1 == 1 )
	{
		const double duration = GetViewModelSequenceDuration();
		const double deadline = ( duration + static_cast< double >( gpGlobals->curtime ) ) -
			static_cast< double >( 0.35f );
		const float stored = static_cast< float >( deadline );
		if ( static_cast< double >( static_cast< float >( m_flTimeWeaponIdle ) ) != deadline )
			m_flTimeWeaponIdle.SetDirect( stored );
	}
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

void CPistol::WeaponIdle()
{
	// S:0x3DEEC0 / C:0x305AA0. IdleInterval is not the fire sequence length.
	if ( HasWeaponIdleTimeElapsed() )
	{
		SetWeaponIdleTime( GetTerrorWpnData().m_flIdleInterval + gpGlobals->curtime );
		SendWeaponAnim( m_isDualWielding && m_iClip1 == 1 ? ACT_VM_IDLE_LOWERED : ACT_VM_IDLE );
	}
}

double CPistol::GetFireRate()
{
	// S:0x3DED00. Incapacitation overrides dual cadence through the same
	// original common rate getter used by the single pistol.
	if ( !IsDualWielding() )
		return BaseClass::GetFireRate();
	CTerrorPlayer *pPlayer = GetTerrorPlayerOwner();
	if ( pPlayer && pPlayer->IsIncapacitated() )
		return BaseClass::GetFireRate();
	return m_iClip1 <= 0 ? 0.2 : 0.075; // Native FLD DOUBLE constants, not widened float literals.
}

Activity CPistol::GetReloadActivity()
{
	// S:0x3DEE80. With one combined round remaining, one of the dual guns
	// is empty, so the empty-reload sequence is selected before clip refund.
	return ( !m_isDualWielding || m_iClip1 > 1 ) && m_iClip1 != 0 ? ACT_VM_RELOAD : PistolActivity( "ACT_VM_RELOAD_EMPTY" );
}

Activity CPistol::GetPrimaryAttackActivity()
{
	// S:0x3DEE30 / C:0x305A10, sampled after CTerrorGun decrements the clip.
	if ( !m_isDualWielding )
		return m_iClip1 != 0 ? ACT_VM_PRIMARYATTACK : ACT_VM_DRYFIRE;
	if ( m_iClip1 % 2 == 1 )
		return m_iClip1 - 1 != 0 ? ACT_VM_PRIMARYATTACK : ACT_VM_DRYFIRE;
	return m_iClip1 != 0 ? ACT_VM_SECONDARYATTACK : PistolActivity( "ACT_VM_DRYFIRE_LEFT" );
}

int CPistol::GetRumbleEffect() const
{
	// S:0x3DEDF0: the named left-hand waveform is original ID28, not the
	// target's NUM_RUMBLE_EFFECTS sentinel. Its waveform is shared-owned.
	return m_isDualWielding && m_iClip1 % 2 != 1 ? RUMBLE_PISTOL_LEFT : BaseClass::GetRumbleEffect();
}

bool CPistol::CanFidget()
{
	// S:0x3DEC60. Fidget eligibility is not crosshair visibility.
	return !m_isDualWielding && BaseClass::CanFidget();
}

acttable_t *CPistol::ActivityList( int &iActivityCount )
{
	// S:0x3DE980, table0x7BBB68; dual translation is separate below.
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
			s_ActivityTable[i].baseAct = PistolActivity( s_pActivityNames[i][0] );
			s_ActivityTable[i].weaponAct = PistolActivity( s_pActivityNames[i][1] );
			s_ActivityTable[i].required = false;
		}
		s_bInitialized = true;
	}
	iActivityCount = ARRAYSIZE( s_ActivityTable );
	return s_ActivityTable;
}

Activity CPistol::TranslatePlayerActivity( Activity activity )
{
	// S:0x3DEA70. No duplicate weapon handle or dual locomotion table.
	static const Activity s_DualActivities[][2] =
	{
		{ PistolActivity( "ACT_IDLE" ), PistolActivity( "ACT_IDLE_ELITES" ) },
		{ PistolActivity( "ACT_WALK" ), PistolActivity( "ACT_WALK_ELITES" ) },
		{ PistolActivity( "ACT_RUN" ), PistolActivity( "ACT_RUN_ELITES" ) },
		{ PistolActivity( "ACT_RUN_CROUCH" ), PistolActivity( "ACT_RUN_CROUCH_ELITES" ) },
		{ PistolActivity( "ACT_JUMP" ), PistolActivity( "ACT_JUMP_DUAL_PISTOL" ) },
		{ PistolActivity( "ACT_CROUCHIDLE" ), PistolActivity( "ACT_CROUCHIDLE_ELITES" ) },
		{ PistolActivity( "ACT_IDLE_INJURED" ), PistolActivity( "ACT_IDLE_INJURED_ELITES" ) },
		{ PistolActivity( "ACT_WALK_INJURED" ), PistolActivity( "ACT_WALK_INJURED_ELITES" ) },
		{ PistolActivity( "ACT_RUN_INJURED" ), PistolActivity( "ACT_RUN_INJURED_ELITES" ) },
		{ PistolActivity( "ACT_IDLE_CALM" ), PistolActivity( "ACT_IDLE_CALM_ELITES" ) },
		{ PistolActivity( "ACT_WALK_CALM" ), PistolActivity( "ACT_WALK_CALM_ELITES" ) },
		{ PistolActivity( "ACT_RUN_CALM" ), PistolActivity( "ACT_RUN_CALM_ELITES" ) }
	};
	if ( m_isDualWielding )
	{
		for ( int i = 0; i < ARRAYSIZE( s_DualActivities ); ++i )
		{
			if ( activity == s_DualActivities[i][0] )
				return s_DualActivities[i][1];
		}
	}
	return BaseClass::TranslatePlayerActivity( activity );
}

Activity CPistol::GetPlayerAttackActivity( TerrorPlayerAnimEvent event, Activity mainActivity )
{
	// S:0x3DEBB0. The bullet helper's mode event, not a new parity counter,
	// selects the player hand. Events40/41 already have native producers.
	if ( event >= TERROR_ANIMEVENT_MELEE_SWEEP && event <= TERROR_ANIMEVENT_MELEE_STOMP )
		return BaseClass::GetPlayerAttackActivity( event, mainActivity );
	static const Activity s_Attack = PistolActivity( "ACT_PRIMARYATTACK_PISTOL" );
	static const Activity s_Right = PistolActivity( "ACT_PRIMARYATTACK_ELITES_R" );
	static const Activity s_Left = PistolActivity( "ACT_PRIMARYATTACK_ELITES_L" );
	if ( m_isDualWielding )
		return event == TERROR_ANIMEVENT_FIRE_PRIMARY ? s_Right : s_Left;
	return s_Attack;
}

Activity CPistol::GetPlayerReloadActivity( TerrorPlayerAnimEvent event, Activity mainActivity )
{
	static const Activity s_Single = PistolActivity( "ACT_RELOAD_PISTOL" );
	static const Activity s_Dual = PistolActivity( "ACT_RELOAD_ELITES" );
	return m_isDualWielding ? s_Dual : s_Single;
}

Activity CPistol::GetPlayerDeployActivity( TerrorPlayerAnimEvent event, Activity mainActivity )
{
	return PistolActivity( m_isDualWielding ? "ACT_DEPLOY_ELITES" : "ACT_DEPLOY_PISTOL" );
}

Activity CPistol::TranslateViewModelActivity( Activity activity )
{
	// S:0x3DEA50. Other dual sequences remain model-authored activities.
	if ( activity == ACT_VM_PRIMARYATTACK )
		return PistolActivity( "ACT_VM_PRIMARYATTACK_LAYER" );
	return BaseClass::TranslateViewModelActivity( activity );
}

#ifdef CLIENT_DLL
int CPistol::GetTerrorMuzzleAttachment() const
{
	// C:0x305960 uses the owner's viewmodel cache, not the carried-model cache.
	CTerrorPlayer *pPlayer = GetTerrorPlayerOwner();
	if ( !pPlayer )
		return 1;
	CBaseViewModel *pViewModel = pPlayer->GetViewModel( 0 );
	if ( !pViewModel )
		return 1;
	const bool right = m_iClip1 % 2 == 1;
	return pViewModel->GetMuzzleFlashAttachment( IsDualWielding(), right );
}
#else
void CPistol::Drop( const Vector &vecVelocity )
{
	// S:0x3DF110. A dual pair becomes two actual world entities on drop.
	if ( m_isDualWielding )
	{
		CPistol *pOtherPistol = static_cast<CPistol *>( CreateEntityByName( "weapon_pistol" ) );
		if ( pOtherPistol )
		{
			pOtherPistol->SetAbsOrigin( WorldSpaceCenter() );
			pOtherPistol->AddSpawnFlags( SF_NORESPAWN );
			DispatchSpawn( pOtherPistol );
			IPhysicsObject *pPhysics = pOtherPistol->VPhysicsGetObject();
			if ( pPhysics )
			{
				AngularImpulse angularImpulse( 200.0f, 200.0f, 200.0f );
				pPhysics->SetVelocity( &vecVelocity, &angularImpulse );
			}
		}
		AssociateDroppedTerrorWeaponWithSpawn( pOtherPistol, this );
		RemoveDualWeapons( true );
	}
	BaseClass::Drop( vecVelocity );
}
#endif

// Native server 0x3de950; 0x5c4f60 is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace weapon_pistolPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "weapon_pistol", 0x5c4f60 ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_ENTITY, "weapon_pistol", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif

#ifndef CLIENT_DLL
BEGIN_DATADESC( CPistol )
END_DATADESC()
#endif
