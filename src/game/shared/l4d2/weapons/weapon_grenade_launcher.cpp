//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/terror_gun.h"
#include "activitylist.h"
#include "rumble_shared.h"
#ifdef CLIENT_DLL
#include "l4d2/player/c_terrorplayer.h"
#include "c_rumble.h"
#include "prediction.h"
#define CGrenadeLauncher C_GrenadeLauncher
#else
#include "l4d2/player/terrorplayer.h"
#include "l4d2/weapons/grenade_launcher_projectile.h"
#include "igameevents.h"
#endif
#ifdef L4D2
#include "precache_register.h"
#endif
#ifdef L4D2
#include "l4d2/weapons/terror_precache_weapon.h"
#endif
#include "tier0/memdbgon.h"

class CGrenadeLauncher : public CTerrorGun
{
public:
	DECLARE_CLASS( CGrenadeLauncher, CTerrorGun );
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();
	virtual int GetWeaponID() const OVERRIDE { return TERROR_WEAPON_GRENADE_LAUNCHER; }
	// S:0x3CEE40 / C:0x2FBB40, not the separate drop-hand query.
	virtual bool ShouldMergeWithWeaponSpawn() const OVERRIDE { return false; }
	virtual void PrimaryAttack() OVERRIDE;
	virtual acttable_t *ActivityList( int &iActivityCount ) OVERRIDE;
	virtual Activity GetPlayerAttackActivity( TerrorPlayerAnimEvent event, Activity mainActivity ) OVERRIDE;
	virtual Activity GetPlayerReloadActivity( TerrorPlayerAnimEvent event, Activity mainActivity ) OVERRIDE;
	virtual Activity GetPlayerDeployActivity( TerrorPlayerAnimEvent event, Activity mainActivity ) OVERRIDE;
	virtual Activity TranslateViewModelLayerActivity( Activity activity ) OVERRIDE;
	// S:0x3CEF60 is the distinct v430 identity override.
	virtual Activity TranslateViewModelActivity( Activity activity ) OVERRIDE { return activity; }

private:
#ifndef CLIENT_DLL
	void LaunchGrenade( CTerrorPlayer *pPlayer );
#endif
};

IMPLEMENT_NETWORKCLASS_ALIASED( GrenadeLauncher, DT_WeaponGrenadeLauncher )
BEGIN_NETWORK_TABLE( CGrenadeLauncher, DT_WeaponGrenadeLauncher )
END_NETWORK_TABLE()
BEGIN_PREDICTION_DATA( CGrenadeLauncher )
END_PREDICTION_DATA()
LINK_ENTITY_TO_CLASS( weapon_grenade_launcher, CGrenadeLauncher );
#ifndef L4D2
PRECACHE_WEAPON_REGISTER( weapon_grenade_launcher );
#endif

#ifndef CLIENT_DLL
static ConVar grenadelauncher_startpos_forward( "grenadelauncher_startpos_forward", "16.0", 
#if defined( GAME_DLL )
	0x00000006
#else
	FCVAR_DEVELOPMENTONLY
#endif
 );
static ConVar grenadelauncher_startpos_right( "grenadelauncher_startpos_right", "4.0", 
#if defined( GAME_DLL )
	0x00000006
#else
	FCVAR_DEVELOPMENTONLY
#endif
 );
static ConVar grenadelauncher_startpos_up( "grenadelauncher_startpos_up", "0.0", 
#if defined( GAME_DLL )
	0x00000006
#else
	FCVAR_DEVELOPMENTONLY
#endif
 );
static ConVar grenadelauncher_vel_up( "grenadelauncher_vel_up", "100.0f", 
#if defined( GAME_DLL )
	0x00000006
#else
	FCVAR_DEVELOPMENTONLY
#endif
 );
static ConVar grenadelauncher_vel_variance( "grenadelauncher_vel_variance", "10.0", 
#if defined( GAME_DLL )
	0x00000006
#else
	FCVAR_DEVELOPMENTONLY
#endif
 );
static ConVar grenadelauncher_velocity( "grenadelauncher_velocity", "1200", 
#if defined( GAME_DLL )
	0x00004004
#else
	FCVAR_CHEAT
#endif
 );

#pragma float_control( precise, on, push )
void CGrenadeLauncher::LaunchGrenade( CTerrorPlayer *pPlayer )
{
	// S:0x3CF060. No punch/spread/lag compensation or shooter velocity is
	// added. Preserve the two server RNG draws and their order (up, right).
	Vector forward, right, up;
	AngleVectors( pPlayer->EyeAngles(), &forward, &right, &up );
	const Vector shootPosition = pPlayer->Weapon_ShootPosition();
	const float flStartUp = grenadelauncher_startpos_up.GetFloat();
	const float flStartForward = grenadelauncher_startpos_forward.GetFloat();
	const float flStartRight = grenadelauncher_startpos_right.GetFloat();
	const Vector offset = ( forward * flStartForward + right * flStartRight ) + up * flStartUp;
	const Vector start( offset.x + shootPosition.x, shootPosition.y + offset.y, shootPosition.z + offset.z );
	trace_t trace;
	CTraceFilterSimple filter( this, COLLISION_GROUP_NONE );
	UTIL_TraceHull( pPlayer->EyePosition(), start, Vector( -8, -8, -8 ), Vector( 8, 8, 8 ),
		MASK_SOLID_BRUSHONLY, &filter, &trace );
	if ( trace.startsolid )
		return;
	const float flVariance = grenadelauncher_vel_variance.GetFloat();
	const Vector upVariance = up * random->RandomFloat( -flVariance, flVariance );
	const Vector rightVariance = right * random->RandomFloat( -flVariance, flVariance );
	const float flVelocityUp = grenadelauncher_vel_up.GetFloat();
	const float flVelocity = grenadelauncher_velocity.GetFloat();
	const Vector velocity = ( ( forward * flVelocity + up * flVelocityUp ) + rightVariance ) + upVariance;
	int bitsDamageType = 0;
	if ( HasUpgrade( TERROR_UPGRADE_INCENDIARY_AMMO ) )
		bitsDamageType = 8;
	if ( HasUpgrade( TERROR_UPGRADE_EXPLOSIVE_AMMO ) )
		bitsDamageType |= 64;
	CGrenadeLauncher_Projectile::Create( trace.endpos, pPlayer->EyeAngles(), velocity, AngularImpulse( 0, 0, 0 ), pPlayer, bitsDamageType );
}
#pragma float_control( pop )
#endif

#pragma float_control( precise, on, push )
void CGrenadeLauncher::PrimaryAttack()
{
	// S:0x3CF570 / C:0x2FBDB0. This is NOT CTerrorGun::FireBullet:
	// the original has its own semi-auto latch, one projectile and clocks.
	CTerrorPlayer *pPlayer = GetTerrorPlayerOwner();
	if ( !pPlayer || !pPlayer->CanAttack() || !IsWeaponVisible() || HandlePrimaryAttackUse() ||
		m_isHoldingFireButton || !m_releasedFireButton )
		return;
	if ( m_iClip1 <= 0 )
	{
		// Original dry-fire helper is null. It emits neither shot event nor
		// bullet/animation and does not set the held-fire latch.
		if ( m_bFireOnEmpty )
			m_flNextPrimaryAttack = static_cast< float >( static_cast< double >( gpGlobals->curtime ) + 0.2 );
		return;
	}
	if ( m_flNextPrimaryAttack > gpGlobals->curtime )
		return;
#ifndef CLIENT_DLL
	LaunchGrenade( pPlayer );
#endif
	SendWeaponAnim( ACT_VM_PRIMARYATTACK );
#ifdef CLIENT_DLL
	pPlayer->OnMountedWeaponFired();
#else
	pPlayer->NoteTerrorWeaponFired();
#endif
	SuppressHelpingHand( 0.0f );
	m_isHoldingFireButton = true;
	++pPlayer->m_iShotsFired;
#ifndef CLIENT_DLL
	const CTerrorWeaponInfo &info = GetTerrorWpnData();
	IGameEvent *pEvent = gameeventmanager->CreateEvent( m_iClip1 > 0 ? "weapon_fire" : "weapon_fire_on_empty" );
	if ( pEvent )
	{
		const char *pszName = GetClassname();
		if ( !Q_strncmp( pszName, "weapon_", 7 ) )
			pszName += 7;
		pEvent->SetInt( "userid", pPlayer->GetUserID() );
		pEvent->SetString( "weapon", pszName );
		pEvent->SetInt( "weaponid", GetWeaponID() );
		pEvent->SetInt( "count", info.m_iBullets );
		gameeventmanager->FireEvent( pEvent );
	}
	// S:0x3CF7DA intentionally compares clip+1 and clip BEFORE the later
	// decrement. Do not repair this original boundary to match gun code.
	const float flOldPercent = static_cast< float >( m_iClip1 + 1 ) / GetMaxClip1() * 100.0f;
	const float flNewPercent = static_cast< float >( m_iClip1 ) / GetMaxClip1() * 100.0f;
	if ( flOldPercent > 40.0f && flNewPercent <= 40.0f )
	{
		pEvent = gameeventmanager->CreateEvent( "weapon_fire_at_40" );
		if ( pEvent )
		{
			const char *pszName = GetClassname();
			if ( !Q_strncmp( pszName, "weapon_", 7 ) )
				pszName += 7;
			pEvent->SetInt( "userid", pPlayer->GetUserID() );
			pEvent->SetString( "weapon", pszName );
			pEvent->SetInt( "weaponid", GetWeaponID() );
			gameeventmanager->FireEvent( pEvent );
		}
	}
	pPlayer->SetNoiseLevel( info.m_flNoiseFactor );
	if ( HasUpgrade( TERROR_UPGRADE_INCENDIARY_AMMO ) || HasUpgrade( TERROR_UPGRADE_EXPLOSIVE_AMMO ) )
	{
		const unsigned char loaded = static_cast< unsigned char >( m_nUpgradedPrimaryAmmoLoaded );
		if ( loaded != 0 )
			m_nUpgradedPrimaryAmmoLoaded = static_cast< unsigned char >( loaded - 1 );
		if ( static_cast< unsigned char >( m_nUpgradedPrimaryAmmoLoaded ) == 0 )
		{
			RemoveUpgrade( TERROR_UPGRADE_INCENDIARY_AMMO );
			RemoveUpgrade( TERROR_UPGRADE_EXPLOSIVE_AMMO );
		}
	}
#else
	const int nRumble = GetRumbleEffect();
	if ( nRumble != -1 && prediction->IsFirstTimePredicted() )
		::RumbleEffect( nRumble, 0, RUMBLE_FLAG_RESTART );
#endif
	pPlayer->DoMuzzleFlash();
	pPlayer->DoTerrorAnimationEvent( TERROR_ANIMEVENT_FIRE_GRENADE_LAUNCHER );
	--m_iClip1;
	ApplyRecoil( pPlayer );
	WeaponSound( SINGLE );
	m_flNextPrimaryAttack = static_cast< float >( GetFireRate() + static_cast< double >( gpGlobals->curtime ) );
	SetWeaponIdleTime( static_cast< float >( static_cast< double >( gpGlobals->curtime ) + 1.8 ) );
	if ( m_iClip1 == 0 )
		m_reloadFromEmpty = true;
}
#pragma float_control( pop )

static Activity GrenadeLauncherActivity( const char *pName )
{
	Activity activity = (Activity)ActivityList_IndexForName( pName );
	return activity == ACT_INVALID ? ActivityList_RegisterPrivateActivity( pName ) : activity;
}

acttable_t *CGrenadeLauncher::ActivityList( int &iActivityCount )
{
	// S:0x3CEDE0/0x3CEDF0, twelve original table entries S:0x7B9878.
	static const char *s_pNames[][2] =
	{
		{ "ACT_IDLE", "ACT_IDLE_PUMPSHOTGUN" },
		{ "ACT_CROUCHIDLE", "ACT_CROUCHIDLE_PUMPSHOTGUN" },
		{ "ACT_RUN_CROUCH", "ACT_RUN_CROUCH_PUMPSHOTGUN" },
		{ "ACT_WALK", "ACT_WALK_PUMPSHOTGUN" },
		{ "ACT_RUN", "ACT_RUN_PUMPSHOTGUN" },
		{ "ACT_IDLE_INJURED", "ACT_IDLE_INJURED_PUMPSHOTGUN" },
		{ "ACT_WALK_INJURED", "ACT_WALK_INJURED_PUMPSHOTGUN" },
		{ "ACT_RUN_INJURED", "ACT_RUN_INJURED_PUMPSHOTGUN" },
		{ "ACT_IDLE_CALM", "ACT_IDLE_CALM_PUMPSHOTGUN" },
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
			s_Table[i].baseAct = GrenadeLauncherActivity( s_pNames[i][0] );
			s_Table[i].weaponAct = GrenadeLauncherActivity( s_pNames[i][1] );
			s_Table[i].required = false;
		}
		s_bInitialized = true;
	}
	iActivityCount = ARRAYSIZE( s_Table );
	return s_Table;
}

Activity CGrenadeLauncher::GetPlayerAttackActivity( TerrorPlayerAnimEvent event, Activity mainActivity )
{
	const bool bMoving = mainActivity != ACT_IDLE;
	// S:0x3CEF70 intentionally uses XM1014, not GRENADELAUNCHER fire.
	static const Activity s_Melee[][2] =
	{
		{ GrenadeLauncherActivity( "ACT_MELEE_SWEEP_RIFLE_IDLE" ), GrenadeLauncherActivity( "ACT_MELEE_SWEEP_RIFLE_RUN" ) },
		{ GrenadeLauncherActivity( "ACT_MELEE_SHOVE_RIFLE_IDLE" ), GrenadeLauncherActivity( "ACT_MELEE_SHOVE_RIFLE_RUN" ) },
		{ GrenadeLauncherActivity( "ACT_MELEE_STRAIGHT_RIFLE_IDLE" ), GrenadeLauncherActivity( "ACT_MELEE_STRAIGHT_RIFLE_RUN" ) },
		{ GrenadeLauncherActivity( "ACT_MELEE_STOMP_RIFLE_IDLE" ), GrenadeLauncherActivity( "ACT_MELEE_STOMP_RIFLE_WALK" ) }
	};
	if ( event >= TERROR_ANIMEVENT_MELEE_SWEEP && event <= TERROR_ANIMEVENT_MELEE_STOMP )
		return s_Melee[event - TERROR_ANIMEVENT_MELEE_SWEEP][bMoving ? 1 : 0];
	static const Activity s_Primary = GrenadeLauncherActivity( "ACT_PRIMARYATTACK_XM1014" );
	return s_Primary;
}

Activity CGrenadeLauncher::GetPlayerReloadActivity( TerrorPlayerAnimEvent event, Activity mainActivity )
{
	// S:0x3CEE20.
	static const Activity s_Reload = GrenadeLauncherActivity( "ACT_RELOAD_GRENADE_LAUNCHER" );
	return s_Reload;
}

Activity CGrenadeLauncher::GetPlayerDeployActivity( TerrorPlayerAnimEvent event, Activity mainActivity )
{
	// S:0x3CEE30 intentionally uses shotgun deploy.
	static const Activity s_Deploy = GrenadeLauncherActivity( "ACT_DEPLOY_SHOTGUN" );
	return s_Deploy;
}

Activity CGrenadeLauncher::TranslateViewModelLayerActivity( Activity activity )
{
	// S:0x3CEE50 / C:0x2FBB50. This exact map does not inherit the
	// gun's extra rifle-specific activity translations.
	static const char *s_pNames[][2] =
	{
		{ "ACT_VM_DEPLOY", "ACT_VM_DEPLOY_LAYER" },
		{ "ACT_VM_PRIMARYATTACK", "ACT_VM_PRIMARYATTACK_LAYER" },
		{ "ACT_VM_RELOAD", "ACT_VM_RELOAD_LAYER" },
		{ "ACT_VM_PICKUP", "ACT_VM_PICKUP_LAYER" },
		{ "ACT_VM_ITEMPICKUP_EXTEND", "ACT_VM_ITEMPICKUP_EXTEND_LAYER" },
		{ "ACT_VM_ITEMPICKUP_LOOP", "ACT_VM_ITEMPICKUP_LOOP_LAYER" },
		{ "ACT_VM_ITEMPICKUP_RETRACT", "ACT_VM_ITEMPICKUP_RETRACT_LAYER" },
		{ "ACT_VM_HELPINGHAND_EXTEND", "ACT_VM_HELPINGHAND_EXTEND_LAYER" },
		{ "ACT_VM_HELPINGHAND_LOOP", "ACT_VM_HELPINGHAND_LOOP_LAYER" },
		{ "ACT_VM_HELPINGHAND_RETRACT", "ACT_VM_HELPINGHAND_RETRACT_LAYER" },
		{ "ACT_VM_MELEE", "ACT_VM_MELEE_LAYER" }
	};
	static Activity s_Activities[ARRAYSIZE( s_pNames )][2];
	static bool s_bInitialized = false;
	if ( !s_bInitialized )
	{
		for ( int i = 0; i < ARRAYSIZE( s_pNames ); ++i )
			for ( int j = 0; j < 2; ++j )
				s_Activities[i][j] = GrenadeLauncherActivity( s_pNames[i][j] );
		s_bInitialized = true;
	}
	for ( int i = 0; i < ARRAYSIZE( s_pNames ); ++i )
		if ( activity == s_Activities[i][0] )
			return s_Activities[i][1];
	return activity;
}

// Native server 0x3ceda0; 0x5c4cd4 is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace weapon_grenade_launcherPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "weapon_grenade_launcher", 0x5c4cd4 ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		if ( !ignoreConditionals && IsWeaponAllowedForPrecache( "weapon_grenade_launcher" ) )
			handler->Precache( RESOURCETYPE_ENTITY, "weapon_grenade_launcher", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif

#ifndef CLIENT_DLL
BEGIN_DATADESC( CGrenadeLauncher )
END_DATADESC()
#endif
