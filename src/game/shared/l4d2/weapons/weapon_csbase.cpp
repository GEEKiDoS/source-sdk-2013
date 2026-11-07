//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#ifdef CLIENT_DLL
#include "l4d2/player/c_terrorplayer.h"
#include "l4d2/hud/hud_terror_crosshair.h"
#else
#include "l4d2/player/terrorplayer.h"
#endif
#include "l4d2/weapons/weapon_csbase.h"
#include "ammodef.h"
#include "in_buttons.h"
#include "engine/ivmodelinfo.h"
#ifndef CLIENT_DLL
#include "recipientfilter.h"
#endif
#include "l4d2/weapons/terror_weapon.h"
#include "l4d2/player/terror_playeranimstate.h"
#ifndef CLIENT_DLL
#include "l4d2/director/zombie_spawn.h"
#include "te_effect_dispatch.h"
#endif
#include <xmmintrin.h>
#ifdef L4D2
#include "precache_register.h"
#endif
#include "l4d2/weapons/weapon_csbase_console_server_cvars.inc"
#include "tier0/memdbgon.h"

#ifdef CLIENT_DLL
LINK_ENTITY_TO_CLASS( weapon_cs_base, CWeaponCSBase );
#endif

#pragma float_control( precise, on, push )
#ifndef CLIENT_DLL
static ConVar survivor_calm_deploy_delay( "survivor_calm_deploy_delay", "2", 
#if defined( GAME_DLL )
	0x00004004
#else
	FCVAR_CHEAT
#endif
, "survivors are not calm for this many seconds after switching weapons" );
#endif

const char *WeaponIDToAlias( int nWeaponID )
{
	// S24EB50: these are weapon IDs, not ammo-definition indices.
	static const char *const aliases[] =
	{
		"none", "pistol", "smg", "pumpshotgun", "autoshotgun", "rifle",
		"hunting_rifle", "smg_silenced", "shotgun_chrome", "rifle_desert",
		"sniper_military", "shotgun_spas", "first_aid_kit", "molotov",
		"pipe_bomb", "pain_pills", "gascan", "propanetank", "oxygentank",
		"melee", "chainsaw", "grenade_launcher", "ammo_pack", "adrenaline",
		"defibrillator", "vomitjar", "rifle_ak47", "gnome", "cola_bottles",
		"fireworkcrate", "upgradepack_incendiary", "upgradepack_explosive",
		"pistol_magnum", "smg_mp5", "rifle_sg552", "sniper_awp",
		"sniper_scout", "rifle_m60", "tank_claw", "hunter_claw",
		"charger_claw", "boomer_claw", "smoker_claw", "spitter_claw",
		"jockey_claw", "machinegun", "vomit", "splat", "pounce", "lounge",
		"pull", "choke", "rock", "physics", "ammo", "upgrade_item"
	};
	return (unsigned int)nWeaponID < ARRAYSIZE( aliases ) ? aliases[nWeaponID] : NULL;
}

// S24EB00 scans the existing alias table in order, using substring matching.
// Do not turn this into an exact-name lookup: e.g. "rifle_desert" first matches "rifle".
int AliasToWeaponID( const char *name )
{
	if ( !name )
		return 0;
	for ( int id = 0; const char *alias = WeaponIDToAlias( id ); ++id )
	{
		if ( Q_stristr( name, alias ) )
			return id;
	}
	return 0;
}

// S24EB70 is distinct from the alias registry; invalid/zero IDs return empty.
const char *WeaponIDToClassname( int nWeaponID )
{
	static const char *const classnames[56] =
	{
		"weapon_none",
		"weapon_pistol",
		"weapon_smg",
		"weapon_pumpshotgun",
		"weapon_autoshotgun",
		"weapon_rifle",
		"weapon_hunting_rifle",
		"weapon_smg_silenced",
		"weapon_shotgun_chrome",
		"weapon_rifle_desert",
		"weapon_sniper_military",
		"weapon_shotgun_spas",
		"weapon_first_aid_kit",
		"weapon_molotov",
		"weapon_pipe_bomb",
		"weapon_pain_pills",
		"weapon_gascan",
		"weapon_propanetank",
		"weapon_oxygentank",
		"weapon_melee",
		"weapon_chainsaw",
		"weapon_grenade_launcher",
		"weapon_ammo_pack",
		"weapon_adrenaline",
		"weapon_defibrillator",
		"weapon_vomitjar",
		"weapon_rifle_ak47",
		"weapon_gnome",
		"weapon_cola_bottles",
		"weapon_fireworkcrate",
		"weapon_upgradepack_incendiary",
		"weapon_upgradepack_explosive",
		"weapon_pistol_magnum",
		"weapon_smg_mp5",
		"weapon_rifle_sg552",
		"weapon_sniper_awp",
		"weapon_sniper_scout",
		"weapon_rifle_m60",
		"weapon_tank_claw",
		"weapon_hunter_claw",
		"weapon_charger_claw",
		"weapon_boomer_claw",
		"weapon_smoker_claw",
		"weapon_spitter_claw",
		"weapon_jockey_claw",
		"weapon_machinegun",
		"vomit",
		"splat",
		"pounce",
		"lounge",
		"pull",
		"choke",
		"rock",
		"physics",
		"ammo",
		"upgrade_item"
	};
	return static_cast< unsigned int >( nWeaponID ) - 1U > 54U ? "" : classnames[nWeaponID];
}

IMPLEMENT_NETWORKCLASS_ALIASED( WeaponCSBase, DT_WeaponCSBase )
BEGIN_NETWORK_TABLE( CWeaponCSBase, DT_WeaponCSBase )
#ifdef CLIENT_DLL
	RecvPropInt( RECVINFO( m_iExtraPrimaryAmmo ) ),
#else
	SendPropExclude( "DT_AnimTimeMustBeFirst", "m_flAnimTime" ),
	SendPropExclude( "DT_BaseAnimating", "m_nSequence" ),
	SendPropInt( SENDINFO( m_iExtraPrimaryAmmo ), 32 ),
#endif
END_NETWORK_TABLE()

#ifdef CLIENT_DLL
BEGIN_PREDICTION_DATA( CWeaponCSBase )
	DEFINE_PRED_FIELD( m_flTimeWeaponIdle, FIELD_FLOAT, FTYPEDESC_OVERRIDE | FTYPEDESC_NOERRORCHECK ),
END_PREDICTION_DATA()
#endif

#ifndef CLIENT_DLL
BEGIN_DATADESC( CWeaponCSBase )
	DEFINE_FIELD( m_iExtraPrimaryAmmo, FIELD_INTEGER ),
	DEFINE_THINKFUNC( FallThink ),
	DEFINE_THINKFUNC( RemovalThink ),
END_DATADESC()
#endif
// External integration fragment. Integrate into existing weapon_csbase owner.
// Do not add a second weapon class/parser/ammo registry or duplicate these methods.
#ifndef CLIENT_DLL
static ConVar terror_ammo_multiplier( "terror_ammo_multiplier", "2", 
#if defined( GAME_DLL )
	0x00004004
#else
	FCVAR_CHEAT
#endif
 ); // S5921E0 flags0x4000, NOT replicated0x2000.
static CUtlVector< CWeaponCSBase * > g_TerrorWeapons; // Native SERVER83A6F8 only.
#endif

CWeaponCSBase::CWeaponCSBase()
{
#ifdef CLIENT_DLL
	SetPredictionEligible( true ); // C430C0; server equivalent is genuine nullsub_8.
#endif
	m_bDelayFire = true; // S5352/C2588.
	m_flOwnerPickupLockUntil = 0.0f; // S5368/C2604; real ctor store on BOTH sides.
	m_pDroppedWeaponOwner = NULL; // S5372/C2608; native RAW CCSPlayer pointer.
	AddSolidFlags( FSOLID_TRIGGER );
	m_iNumPrimaryAmmoClips = 0; // S5376/C2584, not m_iExtraPrimaryAmmo.
#ifndef CLIENT_DLL
	g_TerrorWeapons.AddToTail( this );
	m_bForcePickup = false; // S5380, NO client state or matching client virtual.
#else
	m_bCSAutoReloadAttempted = false; // C2520 only, real CS ctor store.
#endif
	// NO m_iExtraPrimaryAmmo constructor assignment on either native side.
	// NO m_flDecreaseShotsFired/m_iCrosshairAlpha constructor assignment.
}

CWeaponCSBase::~CWeaponCSBase()
{
#ifndef CLIENT_DLL
	g_TerrorWeapons.FindAndRemove( this );
#endif
}

#ifndef CLIENT_DLL
void CWeaponCSBase::Spawn()
{
	BaseClass::Spawn(); // S04CC40 first.
	CollisionProp()->UseTriggerBounds( true, 0.5f );
	SetCollisionGroup( COLLISION_GROUP_WEAPON ); // original value11.
	m_iExtraPrimaryAmmo = static_cast< int >( static_cast< unsigned int >( terror_ammo_multiplier.GetInt() ) *
		static_cast< unsigned int >( m_iNumPrimaryAmmoClips ) ); // Native IMUL wraps low32.
	// This is an INDEPENDENT real native reset, not accidental ctor duplication.
	m_flOwnerPickupLockUntil = 0.0f;
	m_pDroppedWeaponOwner = NULL;
}

bool CWeaponCSBase::KeyValue( const char *pKey, const char *pValue )
{
	// Deliberate original quirk: a successful BASE KeyValue yields false here.
	if ( BaseClass::KeyValue( pKey, pValue ) || Q_stricmp( pKey, "ammo" ) != 0 )
		return false;
	const int clips = atoi( pValue );
	if ( clips < 0 )
		return false;
	m_iNumPrimaryAmmoClips = clips;
	return true;
}
#endif // Client has NO CS Spawn or KeyValue override/reset.

void CWeaponCSBase::Precache()
{
	BaseClass::Precache();
	PrecacheScriptSound( "Default.Zoom" );
	PrecacheEffect( "CS_MuzzleFlash_X" );
	PrecacheEffect( "CS_MuzzleFlash" );
	PrecacheEffect( "gunshotsplash" );
}

bool CWeaponCSBase::Holster( CBaseCombatWeapon *pSwitchingTo )
{
	CCSPlayer *pPlayer = dynamic_cast< CCSPlayer * >( GetOwner() );
	if ( !pPlayer )
		return false;
#ifndef CLIENT_DLL
	pPlayer->SetFOV( pPlayer, 0, 0.0f, 0 );
	pPlayer->SetShieldDrawn( false ); // genuine native nullsub, no shield bit.
#endif
	return BaseClass::Holster( pSwitchingTo );
}

const char *CWeaponCSBase::GetViewModel( int /*viewmodelindex*/ ) const
{
	CCSPlayer *pPlayer = dynamic_cast< CCSPlayer * >( GetOwner() );
	if ( !pPlayer )
		return BaseClass::GetViewModel( 0 );
#ifndef CLIENT_DLL
	if ( pPlayer->HasShield() && GetCSWpnData().m_bCanEquipWithShield )
		return GetCSWpnData().m_szShieldViewModel;
#endif
	return GetCSWpnData().szViewModel;
}

bool CWeaponCSBase::CanDeploy()
{
	CCSPlayer *pPlayer = dynamic_cast< CCSPlayer * >( GetOwner() );
#ifdef CLIENT_DLL
	return pPlayer != NULL;
#else
	return pPlayer && ( !pPlayer->HasShield() || GetCSWpnData().m_bCanEquipWithShield );
#endif
}

bool CWeaponCSBase::Deploy()
{
#ifdef CLIENT_DLL
	m_iCrosshairAlpha = 80; // C2576, NOT a ctor initialization.
#else
	m_flDecreaseShotsFired = gpGlobals->curtime; // S5360; migrate wrong LastDeployTime name, one field.
#endif
	CCSPlayer *pPlayer = dynamic_cast< CCSPlayer * >( GetOwner() );
	if ( pPlayer )
	{
		pPlayer->m_iShotsFired = 0;
		pPlayer->m_bResumeZoom = false;
		pPlayer->m_iLastZoom = 0;
#ifndef CLIENT_DLL
		pPlayer->SetFOV( pPlayer, 0, 0.0f, 0 );
#endif
	}
	return BaseClass::Deploy();
}

#ifndef CLIENT_DLL
bool CWeaponCSBase::Reload()
{
	CCSPlayer *pPlayer = dynamic_cast< CCSPlayer * >( GetOwner() );
	if ( !pPlayer )
		return false;
	pPlayer->m_iShotsFired = 0;
	return BaseClass::Reload();
}

// Server native v382 ADDS virtual dispatch around the generic nonvirtual helper.
// Add this virtual declaration at CS tier, not at unrelated generic weapon tier.
bool CWeaponCSBase::DefaultReload( int clip1, int clip2, int activity )
{
	if ( !BaseClass::DefaultReload( clip1, clip2, activity ) )
		return false;
	CCSPlayer *pPlayer = dynamic_cast< CCSPlayer * >( GetOwner() );
	if ( pPlayer )
		pPlayer->OnTerrorReloadStarted( false, m_iClip1, false ); // Native CCS v530: base249D40 null; Terror307DF0 real speech/event.
	return true;
}
#endif

#ifdef CLIENT_DLL
bool CWeaponCSBase::CanBeGivenTo( CTerrorPlayer *pRecipient )
{
	if ( !pRecipient || ( GetCSWpnData().m_WeaponType != TERROR_WEAPONTYPE_CLAW && pRecipient->GetTeamNumber() == TEAM_INFECTED ) )
		return false;
	return !m_pDroppedWeaponOwner || pRecipient != m_pDroppedWeaponOwner || !( m_flOwnerPickupLockUntil > gpGlobals->curtime );
}
#endif

const CCSWeaponInfo &CWeaponCSBase::GetCSWpnData() const
{
	Assert( GetWpnData().bParsedScript );
	return static_cast< const CCSWeaponInfo & >( GetWpnData() );
}

const CTerrorWeaponInfo &CWeaponCSBase::GetTerrorWpnData() const
{
	Assert( GetWpnData().bParsedScript );
	return static_cast< const CTerrorWeaponInfo & >( GetWpnData() );
}

// S24ECB0/C231710: genuine NONVIRTUAL helper; no sound side effects.
bool CWeaponCSBase::PlayEmptySound()
{
	return false;
}

void CWeaponCSBase::SetExtraPrimaryAmmo( int nAmmo )
{
	m_iExtraPrimaryAmmo = nAmmo;
}

// External integration fragment ONLY. Replace CWeaponCSBase::ItemPostFrame.
// S2502A0/C232300. Both are real PARAMETERLESS primary virtuals.
// Decompiler a2@edi on S is a saved-register artifact; see server_frame_abi.txt.
// Do not move any Terror/Gun interruption, swing, zoom or reload-queue work here.
#pragma float_control( precise, on, push )
void CWeaponCSBase::ItemPostFrame()
{
	CCSPlayer *pPlayer = dynamic_cast< CCSPlayer * >( GetOwner() );
	if ( !pPlayer || !IsWeaponVisible() )
		return;

	UpdateShieldPostFrame(); // S404/C398, genuine paired implementations below.

	// The deadline is the PLAYER's next attack, not either weapon deadline.
	// Native MIN evaluates the selected getter/reserve expression a second time.
	if ( m_bInReload && gpGlobals->curtime >= pPlayer->m_flNextAttack )
	{
		int amount;
		if ( GetMaxClip1() - m_iClip1 >= pPlayer->GetAmmoCount( m_iPrimaryAmmoType ) )
			amount = pPlayer->GetAmmoCount( m_iPrimaryAmmoType );
		else
			amount = GetMaxClip1() - m_iClip1;
		m_iClip1 += amount;
		pPlayer->RemoveAmmo( amount, m_iPrimaryAmmoType );
		m_bInReload = false;
	}

	if ( ( pPlayer->m_nButtons & IN_ATTACK2 ) &&
		 gpGlobals->curtime >= m_flNextSecondaryAttack && CanPerformSecondaryAttack() )
	{
		// Exact clip sentinel, not UsesClipsForAmmo2/secondary-ammo validity.
		if ( m_iClip2 != WEAPON_NOCLIP && pPlayer->GetAmmoCount( GetSecondaryAmmoType() ) == 0 )
			m_bFireOnEmpty = true;
#ifndef CLIENT_DLL
		if ( pPlayer->HasShield() )
			CWeaponCSBase::SecondaryAttack(); // deliberately QUALIFIED.
		else
#endif
			SecondaryAttack();
		pPlayer->m_nButtons &= ~IN_ATTACK2;
		return;
	}

	if ( ( pPlayer->m_nButtons & IN_ATTACK ) && gpGlobals->curtime >= m_flNextPrimaryAttack )
	{
		if ( m_iClip1 == 0 ||
			 ( GetMaxClip1() == WEAPON_NOCLIP && pPlayer->GetAmmoCount( GetPrimaryAmmoType() ) == 0 ) )
			m_bFireOnEmpty = true;
		if ( pPlayer->m_bIsDefusing )
			return;
#ifdef CLIENT_DLL
		if ( pPlayer->State_Get() != STATE_ACTIVE ) // C222D30 reads actual state6260.
			return;
#else
		if ( pPlayer->m_iPlayerState != STATE_ACTIVE || pPlayer->IsShieldDrawn() )
			return;
		// Grenades own their event producer; other CS-frame users emit HERE,
		// before the actual virtual PrimaryAttack, even when that later declines.
		if ( GetCSWpnData().m_WeaponType != TERROR_WEAPONTYPE_GRENADE )
		{
			IGameEvent *pEvent = gameeventmanager->CreateEvent( HasAnyAmmo() ? "weapon_fire" : "weapon_fire_on_empty" );
			if ( pEvent )
			{
				const char *pWeapon = GetClassname();
				if ( !pWeapon )
					pWeapon = "";
				if ( strncmp( pWeapon, "weapon_", 7 ) == 0 )
					pWeapon += 7;
				pEvent->SetInt( "userid", pPlayer->GetUserID() );
				pEvent->SetString( "weapon", pWeapon );
				pEvent->SetInt( "weaponid", GetWeaponID() );
				gameeventmanager->FireEvent( pEvent );
			}
		}
#endif
		PrimaryAttack();
		return;
	}

	if ( ( pPlayer->m_nButtons & IN_RELOAD ) && GetMaxClip1() != WEAPON_NOCLIP &&
		 !m_bInReload && gpGlobals->curtime > m_flNextPrimaryAttack )
	{
#ifndef CLIENT_DLL
		if ( pPlayer->IsShieldDrawn() )
			return;
		IGameEvent *pEvent = gameeventmanager->CreateEvent( "weapon_reload" );
		if ( pEvent )
		{
			pEvent->SetInt( "userid", pPlayer->GetUserID() );
			pEvent->SetBool( "manual", true );
			gameeventmanager->FireEvent( pEvent );
		}
#endif
		Reload(); // regardless of event allocation success or reload success.
		return;
	}

	// Holding either attack button blocks this entire tail even when its
	// deadline/capability prevented an attack. RELOAD alone does NOT block it.
	if ( pPlayer->m_nButtons & ( IN_ATTACK | IN_ATTACK2 ) )
		return;
	if ( m_bDelayFire )
	{
		m_bDelayFire = false;
		if ( pPlayer->m_iShotsFired > 15 )
			pPlayer->m_iShotsFired = 15;
		m_flDecreaseShotsFired = static_cast< float >( static_cast< double >( gpGlobals->curtime ) + 0.4 );
	}
	m_bFireOnEmpty = false;
	if ( GetCSWpnData().m_WeaponType != TERROR_WEAPONTYPE_PISTOL )
	{
		if ( pPlayer->m_iShotsFired > 0 && gpGlobals->curtime > m_flDecreaseShotsFired )
		{
			m_flDecreaseShotsFired = static_cast< float >( static_cast< double >( gpGlobals->curtime ) + 0.0225 );
			--pPlayer->m_iShotsFired;
		}
	}
	else
	{
		pPlayer->m_iShotsFired = 0;
	}

	// Keep the original boolean/evaluation order. No auto-switch here.
	if ( ( !HasCSPrimaryAmmo() && gpGlobals->curtime > m_flNextPrimaryAttack )
#ifdef CLIENT_DLL
		 || m_bCSAutoReloadAttempted
#endif
		 || m_iClip1 != 0 || ( GetWeaponFlags() & ITEM_FLAG_NOAUTORELOAD ) ||
		 !( gpGlobals->curtime > m_flNextPrimaryAttack ) )
	{
		WeaponIdle();
		return;
	}
#ifndef CLIENT_DLL
	IGameEvent *pEvent = gameeventmanager->CreateEvent( "weapon_reload" );
	if ( pEvent )
	{
		pEvent->SetInt( "userid", pPlayer->GetUserID() );
		pEvent->SetBool( "manual", false );
		gameeventmanager->FireEvent( pEvent );
	}
#endif
	Reload();
}
#pragma float_control( pop )

// Private NONVIRTUAL helper, name agreed with M3Integrator5. S24F7C0/C232220.
// Do NOT replace/override generic HasPrimaryAmmo or ReloadOrSwitchWeapons.
bool CWeaponCSBase::HasCSPrimaryAmmo()
{
	CCSPlayer *pPlayer = dynamic_cast< CCSPlayer * >( GetOwner() );
	if ( !pPlayer )
		return false;
	if ( m_iClip1 > 0 )
		return true;
	return pPlayer->GetAmmoCount( GetPrimaryAmmoType() ) > 0 || GetMaxClip1() == WEAPON_NOCLIP;
}

// S40424EDA0/C3982317E0. Return registers in stripped void functions carry
// incidental values, not an owner-return API. No second cached-owner adapter.
void CWeaponCSBase::UpdateShieldPostFrame()
{
	CCSPlayer *pPlayer = dynamic_cast< CCSPlayer * >( GetOwner() );
#ifndef CLIENT_DLL
	if ( pPlayer && !pPlayer->HasShield() )
		pPlayer->SetShieldDrawn( false );
#else
	(void)pPlayer;
#endif
}

// S28324FA80: the shield gate surrounds ALL side effects. Client271231720
// is a genuine native null override. Shield methods are native constants/null.
void CWeaponCSBase::SecondaryAttack()
{
#ifndef CLIENT_DLL
	CCSPlayer *pPlayer = dynamic_cast< CCSPlayer * >( GetOwner() );
	if ( !pPlayer || !pPlayer->HasShield() )
		return;
	pPlayer->SetShieldDrawn( !pPlayer->IsShieldDrawn() );
	SendWeaponAnim( TerrorActivityFromOriginal( pPlayer->IsShieldDrawn() ? TERROR_ORIGINAL_ACT_SHIELD_UP : TERROR_ORIGINAL_ACT_SHIELD_DOWN ) );
	pPlayer->RecalculateMaxSpeed();
	// Two separate curtime reads; secondary store first. Native x87 addition
	// uses double .4 and each CNetworkVar assignment performs float32 storage.
	m_flNextSecondaryAttack = static_cast< float >( static_cast< double >( gpGlobals->curtime ) + 0.4 );
	m_flNextPrimaryAttack = static_cast< float >( static_cast< double >( gpGlobals->curtime ) + 0.4 );
#endif
}

// S407250070/C401231AA0: distinct native CS default primary-clip reload helper,
// NOT the actual virtual Reload S28124FF60 and NOT generic DefaultReload.
// Descriptive target name; keep one canonical name at integration.
bool CWeaponCSBase::ReloadPrimaryClip()
{
	CCSPlayer *pPlayer = dynamic_cast< CCSPlayer * >( GetOwner() );
	if ( !pPlayer )
		return false;
	if ( pPlayer->GetAmmoCount( GetPrimaryAmmoType() ) > 0 )
	{
#ifdef CLIENT_DLL
		if ( !CBaseCombatWeapon::DefaultReload( GetCSWpnData().iMaxClip1, 0, TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_RELOAD ) ) )
#else
		if ( !DefaultReload( GetCSWpnData().iMaxClip1, 0, TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_RELOAD ) ) )
#endif
			return false;
		pPlayer->m_iShotsFired = 0;
#ifdef CLIENT_DLL
		m_bCSAutoReloadAttempted = true; // actual successful native writer, not a default.
#endif
	}
	return true; // genuine native true when no reserve, with NO latch store.
}

// S24ECC0/C231730: genuine one-argument forwarding, no gesture remap here.
bool CWeaponCSBase::SendWeaponAnim( int activity )
{
	return BaseClass::SendWeaponAnim( activity );
}

// S29024EF20: the genuine CS default returns wide one second.
double CWeaponCSBase::GetFireRate()
{
	return 1.0;
}

const Vector &CWeaponCSBase::GetBulletSpread()
{
	// S29324EEE0: all three native components are exact float bits3D8EDE55.
	static const Vector spread( 0.069756471f, 0.069756471f, 0.069756471f );
	return spread;
}

// Native CWeaponCSBase slots389..391 receive the original event AND normalized
// main activity. In particular, every non-IDLE context selects the moving melee.
Activity CWeaponCSBase::GetPlayerAttackActivity( TerrorPlayerAnimEvent event, Activity mainActivity )
{
	const bool bMoving = mainActivity != ACT_IDLE;
	switch ( event )
	{
	case TERROR_ANIMEVENT_MELEE_SWEEP:
		return TerrorActivityFromOriginal( bMoving ? TERROR_ORIGINAL_ACT_MELEE_SWEEP_RIFLE_RUN : TERROR_ORIGINAL_ACT_MELEE_SWEEP_RIFLE_IDLE );
	case TERROR_ANIMEVENT_MELEE_SHOVE:
		return TerrorActivityFromOriginal( bMoving ? TERROR_ORIGINAL_ACT_MELEE_SHOVE_RIFLE_RUN : TERROR_ORIGINAL_ACT_MELEE_SHOVE_RIFLE_IDLE );
	case TERROR_ANIMEVENT_MELEE_STRAIGHT:
		return TerrorActivityFromOriginal( bMoving ? TERROR_ORIGINAL_ACT_MELEE_STRAIGHT_RIFLE_RUN : TERROR_ORIGINAL_ACT_MELEE_STRAIGHT_RIFLE_IDLE );
	case TERROR_ANIMEVENT_MELEE_STOMP:
		return TerrorActivityFromOriginal( bMoving ? TERROR_ORIGINAL_ACT_MELEE_STOMP_RIFLE_WALK : TERROR_ORIGINAL_ACT_MELEE_STOMP_RIFLE_IDLE );
	default:
		return TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_PRIMARYATTACK_RIFLE );
	}
}

Activity CWeaponCSBase::GetPlayerReloadActivity( TerrorPlayerAnimEvent event, Activity mainActivity )
{
	return TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_RELOAD_RIFLE ); // S24D570.
}

Activity CWeaponCSBase::GetPlayerDeployActivity( TerrorPlayerAnimEvent event, Activity mainActivity )
{
	return TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_DEPLOY_RIFLE ); // S24F970.
}

#pragma float_control( precise, on, push )
bool CWeaponCSBase::ShouldMergeWithWeaponSpawn() const
{
    return GetSlot() == 0 || GetSlot() == 2 || GetCSWpnData().m_WeaponType == TERROR_WEAPONTYPE_PISTOL;
}

void CWeaponCSBase::SetWeaponWorldModel( const char *model )
{
    m_iWorldModelIndex = modelinfo->GetModelIndex( model );
}

// External paired pickup/drop bodies. Existing Terror overrides stay distinct.
// Includes recipientfilter.h, vphysics_interface.h at existing owner as needed.
void CWeaponCSBase::DefaultTouch( CBaseEntity *pOther )
{
	if ( !pOther || !pOther->IsPlayer() || !CanBeGivenTo( static_cast< CTerrorPlayer * >( pOther ) ) )
		return;
	BaseClass::DefaultTouch( pOther );
#ifndef CLIENT_DLL
	CCSPlayer *pOwner = dynamic_cast< CCSPlayer * >( GetOwner() );
	if ( pOwner == pOther )
	{
		pOwner->Weapon_Switch( this, 0 ); // S23FC70 at player v284, NOT OnGivenWeapon.
		static_cast< CTerrorPlayer * >( pOther )->SpeakIfAllowed( "PlayerGivenItem", 1, NULL, NULL, 0, NULL );
	}
#endif
}

#ifndef CLIENT_DLL
bool CWeaponCSBase::CanBeGivenTo( CTerrorPlayer *pRecipient )
{
	if ( !pRecipient || ( GetCSWpnData().m_WeaponType != TERROR_WEAPONTYPE_CLAW && pRecipient->GetTeamNumber() == TEAM_INFECTED ) )
		return false;
	if ( m_pDroppedWeaponOwner && pRecipient == m_pDroppedWeaponOwner && m_flOwnerPickupLockUntil > gpGlobals->curtime )
		return false;
	if ( ( GetCSWpnData().iSlot == 0 || GetCSWpnData().iSlot == 1 || GetCSWpnData().iSlot == 2 ) &&
		 !IsForcePickup() && pRecipient->Weapon_SlotOccupied( this ) )
		return false;
	Vector velocity = vec3_origin;
	if ( IPhysicsObject *pPhysics = VPhysicsGetObject() )
		pPhysics->GetVelocity( &velocity, NULL );
	return IsForcePickup() || ( !( 1.0f > ( ( velocity.x * velocity.x + velocity.y * velocity.y ) + velocity.z * velocity.z ) ) && m_pDroppedWeaponOwner != pRecipient );
}

bool CWeaponCSBase::IsRemoveable()
{
	if ( BaseClass::IsRemoveable() )
		return !( m_flOwnerPickupLockUntil > gpGlobals->curtime ); // Equality/NaN allow; native COMISS/JBE.
	return false;
}

void CWeaponCSBase::Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	if ( pActivator && pActivator->IsPlayer() )
		static_cast< CBasePlayer * >( pActivator )->BumpWeapon( this );
}

void CWeaponCSBase::Drop( const Vector &velocity )
{
	SetRemoveable( true );
	StopAnimation();
	StopFollowingEntity();
	SetMoveType( MOVETYPE_FLYGRAVITY );
	SetGravity( 1.0f );
	m_iState = WEAPON_NOT_CARRIED;
	RemoveEffects( EF_NODRAW );
	FallInit();
	SetGroundEntity( NULL );
	m_bInReload = false;
	SetThink( NULL );
	m_flOwnerPickupLockUntil = gpGlobals->curtime + 0.8f;
	m_pDroppedWeaponOwner = dynamic_cast< CCSPlayer * >( GetOwner() );
	SetTouch( &CWeaponCSBase::DefaultTouch ); // +208 is TOUCH, not Think.
	if ( IPhysicsObject *pPhysics = VPhysicsGetObject() )
	{
		const AngularImpulse impulse( 200.0f, 200.0f, 200.0f );
		pPhysics->AddVelocity( &velocity, &impulse );
	}
	else
		SetAbsVelocity( velocity );
	SetNextThink( gpGlobals->curtime );
	SetOwnerEntity( NULL );
	SetOwner( NULL ); // Native S250AC6 ->04D1B0(0), BEFORE return.
}

void CWeaponCSBase::GiveTo( CBaseEntity *pOther )
{
	RemoveEffects( EF_ITEM_BLINK );
	if ( pOther->IsPlayer() && pOther->IsAlive() )
	{
		CRecipientFilter filter;
		for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			CBasePlayer *pObserver = UTIL_PlayerByIndex( i );
			if ( pObserver && !pObserver->IsAlive() && pObserver->GetObserverMode() == OBS_MODE_IN_EYE )
				filter.AddRecipient( pObserver );
		}
		if ( filter.GetRecipientCount() )
			EmitSound( filter, pOther->entindex(), "Player.PickupWeapon" );
		SetName( NULL_STRING );
	}
	SetRemoveable( false );
}
#endif

// S24F060 is an actual null. Do not transfer extra ammo or invoke Base here.
// Native client does NOT acquire this server OnPickedUp override.
#ifndef CLIENT_DLL
void CWeaponCSBase::OnPickedUp( CBaseCombatCharacter *pNewOwner )
{
}
#endif

// External CS-tier client source. Existing c_terrorplayer.h and
// terror_playeranimstate.h expose actual fields/accessors used here.
#ifdef CLIENT_DLL
// C231980: retiring client prediction belongs to this real data-change owner.
void CWeaponCSBase::OnDataChanged( DataUpdateType_t type )
{
	BaseClass::OnDataChanged( type );
	if ( GetPredictable() && !ShouldPredict() )
		ShutdownPredictable();
}

bool CWeaponCSBase::ShouldDrawWeapon()
{
	if ( IsCarriedByLocalPlayer() ) return false;
	C_CSPlayer *owner = dynamic_cast< C_CSPlayer * >( GetOwner() );
	if ( owner && owner->IsPlayer() )
	{
		C_TerrorPlayer *player = static_cast< C_TerrorPlayer * >( owner );
		if ( owner->GetRenderTeamNumber() == TEAM_INFECTED ||
			player->GetTerrorAnimState()->IsGrenadeProjectilePresentationActive() ||
			player->GetTerrorAnimState()->IsGrenadePresentationSuppressed() ||
			player->m_bSurvivorPositionHidingWeapons )
			return false;
	}
	return C_BaseCombatWeapon::ShouldDrawWeapon();
}

bool CWeaponCSBase::ShouldPredict()
{
	CBaseCombatCharacter *pOwner = GetOwner(); // C231DC0 -> C063550: one lookup.
	return pOwner && pOwner->IsPlayer() && static_cast< C_BasePlayer * >( pOwner )->IsLocalPlayer();
}

bool CWeaponCSBase::IsAllowedToSwitch()
{
	return dynamic_cast< CCSPlayer * >( GetOwner() ) != NULL;
}

int CWeaponCSBase::GetUseType( CBaseEntity *user )
{
	// C231C20 is native v348, distinct from the radial-use priority v236.
	const int extraPrimaryAmmo = m_iExtraPrimaryAmmo;
	const bool hasAnyUpgrades = HasAnyUpgrades();
	const int clip = m_iClip1;
	const int weaponID = GetWeaponID();
	return GetTerrorWeaponGlowType( user, weaponID, clip,
		extraPrimaryAmmo, NULL, hasAnyUpgrades );
}

int CWeaponCSBase::GetTerrorMuzzleAttachment() const
{
	return const_cast< CWeaponCSBase * >( this )->LookupAttachment( "muzzle_flash" ); // C231A60.
}

int CWeaponCSBase::GetEjectBrassAttachment() const
{
	return const_cast< CWeaponCSBase * >( this )->LookupAttachment( "muzzle_flash" ); // C231A80 retains the native brass-name bug.
}

void CWeaponCSBase::EjectBrass()
{
	CBaseCombatCharacter *pOwner = GetOwner();
	if ( pOwner && pOwner->IsPlayer() )
		DevWarning( "CWeaponCSBase::EjectBrass failed to eject brass!\n" ); // C231DE0.
}

// C231A50 is a genuine null; PLAYER muzzle-event owner remains authoritative.
void CWeaponCSBase::ProcessMuzzleFlashEvent()
{
}
#endif

// External server CS tier source. No new respawn manager or ordinary-drop timer.
#ifndef CLIENT_DLL
CBaseEntity *CWeaponCSBase::Respawn()
{
	CBaseEntity *pNewWeapon = CBaseEntity::Create( GetClassname(),
		g_pGameRules->VecWeaponRespawnSpot( this ), GetAbsAngles(), GetOwner() );
	if ( pNewWeapon )
	{
		pNewWeapon->AddEffects( EF_NODRAW );
		pNewWeapon->SetTouch( NULL );
		pNewWeapon->SetThink( &CWeaponCSBase::AttemptToMaterialize );
		UTIL_DropToFloor( this, MASK_SOLID ); // ORIGINAL this, not pNewWeapon.
		pNewWeapon->SetNextThink( static_cast< float >( static_cast< double >( g_pGameRules->FlWeaponRespawnTime( this ) ) + gpGlobals->curtime ) );
	}
	else
		Msg( "Respawn failed to create %s!\n", GetClassname() );
	return pNewWeapon;
}

void CWeaponCSBase::AttemptToMaterialize()
{
	const double wait = g_pGameRules->FlWeaponTryRespawn( this );
	if ( wait == 0.0 )
	{
		Materialize();
		return;
	}
	const float storedWait = static_cast< float >( wait );
	SetNextThink( gpGlobals->curtime + storedWait );
}

void CWeaponCSBase::Materialize()
{
	if ( IsEffectActive( EF_NODRAW ) )
	{
		RemoveEffects( EF_NODRAW );
		DoMuzzleFlash(); // Native modulo4 parity writer S0372C0, NOT sound/drop.
	}
	AddSolidFlags( FSOLID_TRIGGER );
	SetThink( NULL );
	// No generic SetSolid, pickup-touch replacement or materialize sound.
}
#endif

#ifndef CLIENT_DLL
// S24EFD0: actual existing28F980 point/team visibility owner, no second scan.
void CWeaponCSBase::RemovalThink()
{
	if ( dynamic_cast< CCSPlayer * >( GetOwner() ) )
	{
		SetThink( NULL );
		return;
	}
	if ( IsTerrorSpawnPointVisibleToTeam( WorldSpaceCenter(), TEAM_SURVIVOR, 0, 0.0f, NULL, NULL ) )
		SetNextThink( 1.0f ); // Native absolute ONE, not current time plus one.
	else
		UTIL_Remove( this );
}
#endif
#pragma float_control( pop )

#pragma float_control( precise, on, push )
// S24FBC0 / C232630: the activity provider, not the input activity, is used.
bool CWeaponCSBase::DefaultDeploy( char * /*viewModel*/, char *weaponModel, int /*activity*/, char *animationExtension )
{
#ifdef CLIENT_DLL
	C_CSPlayer *csPlayer = dynamic_cast< C_CSPlayer * >( GetOwner() );
#else
	CCSPlayer *csPlayer = dynamic_cast< CCSPlayer * >( GetOwner() );
#endif
	if ( !csPlayer || !csPlayer->IsPlayer() )
		return false;
	CTerrorPlayer *player = static_cast< CTerrorPlayer * >( csPlayer );
	csPlayer->SetAnimationExtension( animationExtension );
	SetViewModel();
	SendWeaponAnim( GetDeployActivity() );
	player->DoTerrorAnimationEvent( TERROR_ANIMEVENT_DEPLOY, 0 );
#ifndef CLIENT_DLL
	player->DelayCalm( survivor_calm_deploy_delay.GetFloat() );
#endif
	const float scale = static_cast< float >( GetDeployAnimationScale() );
	SetPlaybackRate( 1.0f );
	const double duration = SequenceDuration();
#ifdef CLIENT_DLL
	const float delay = static_cast< float >( duration * scale );
#endif
	player->SetNextAttack( gpGlobals->curtime );
	if ( ShouldBlockPrimaryFire() )
	{
#ifndef CLIENT_DLL
		const float delay = static_cast< float >( duration * scale );
#endif
		m_flNextPrimaryAttack = gpGlobals->curtime + delay;
		m_flNextSecondaryAttack = gpGlobals->curtime;
	}
	SetWeaponVisible( true );
#ifndef CLIENT_DLL
	csPlayer->SetShieldDrawn( false ); // Genuine nullsub17 before the real HasShield query.
	if ( csPlayer->HasShield() ) // S23FA30 is the original constantfalse, not fabricated shield state.
		SetWeaponWorldModel( "models/weapons/w_shield.mdl" );
	else
#endif
		SetWeaponWorldModel( weaponModel );
	m_nQueuedAttack = 0;
	m_flTimeAttackQueued = gpGlobals->curtime;
	return true;
}

#pragma float_control( pop )

#ifdef CLIENT_DLL
// Native50E0B0/50E0E0/50E110. cl_bob is registered but genuinely UNUSED here.
static ConVar cl_bobcycle( "cl_bobcycle", "0.8", 
#if defined( CLIENT_DLL )
	0x00004008
#else
	FCVAR_CHEAT
#endif
 );
static ConVar cl_bob( "cl_bob", "0.002", 
#if defined( CLIENT_DLL )
	0x00004008
#else
	FCVAR_CHEAT
#endif
 );
static ConVar cl_bobup( "cl_bobup", "0.5", 
#if defined( CLIENT_DLL )
	0x00004008
#else
	FCVAR_CHEAT
#endif
 );
// Native7B4A60/64/68/7B48AC/7B48A8: ONE file-global lifetime, no map/deploy reset.
static float s_flCSBobTime;
static float s_flCSLastSpeed;
static float s_flCSLastBobTime;
static float s_flCSVerticalBob;
static float s_flCSLateralBob;

// C231E10. Keep native ordered comparisons, F32 spills and the odd SECOND phase.
float CWeaponCSBase::CalcViewmodelBob()
{
	CBaseCombatCharacter *owner = GetOwner();
	C_BasePlayer *player = owner && owner->IsPlayer() ? static_cast< C_BasePlayer * >( owner ) : NULL;
	if ( gpGlobals->frametime == 0.0f || !player || cl_bobcycle.GetFloat() <= 0.0f )
		return 0.0f;
	const float firstUp = cl_bobup.GetFloat();
	if ( firstUp <= 0.0f || firstUp >= 1.0f )
		return 0.0f;

	// Native raw256/260 are the received LOCAL velocity, not GetAbsVelocity.
	const Vector &velocity = player->GetLocalVelocity();
	const float speedSquared = velocity.x * velocity.x + velocity.y * velocity.y;
	float speed = _mm_cvtss_f32( _mm_sqrt_ss( _mm_set_ss( speedSquared ) ) );
	float maximumChange = ( gpGlobals->curtime - s_flCSLastBobTime ) * 320.0f;
	if ( maximumChange < 0.0f )
		maximumChange = 0.0f;
	const float upperSpeed = s_flCSLastSpeed + maximumChange;
	if ( speed > upperSpeed )
		speed = upperSpeed;
	else
	{
		const float lowerSpeed = s_flCSLastSpeed - maximumChange;
		if ( lowerSpeed > speed )
			speed = lowerSpeed;
	}
	if ( speed > 320.0f )
		speed = 320.0f;
	else if ( -320.0f > speed )
		speed = -320.0f;

	const float now = gpGlobals->curtime;
	s_flCSLastSpeed = speed;
	// C02B3D0 is the UNCLAMPED wide remap, not the familiar CSS/clamped helper.
	const double normalizedSpeed = 0.0 + ( 1.0 - 0.0 ) *
		( static_cast< double >( speed ) - 0.0 ) / ( 320.0 - 0.0 );
	const double elapsed = static_cast< double >( now ) - static_cast< double >( s_flCSLastBobTime );
	const float inverseCycle = 1.0f / cl_bobcycle.GetFloat();
	s_flCSLastBobTime = now;
	s_flCSBobTime = static_cast< float >( normalizedSpeed * elapsed + static_cast< double >( s_flCSBobTime ) );
	const float cycleCount = inverseCycle * s_flCSBobTime;
	const int wholeCycle = _mm_cvttss_si32( _mm_set_ss( cycleCount ) );
	const float wrappedCycle = static_cast< float >( wholeCycle ) * cl_bobcycle.GetFloat();
	const float firstPhase = ( s_flCSBobTime - wrappedCycle ) * inverseCycle;
	const double pi = 3.141592653589793;
	double angle;
	if ( firstUp > firstPhase ) // COMISS/JBE selects ELSE for unordered input.
		angle = static_cast< double >( firstPhase ) * pi / static_cast< double >( firstUp );
	else
		angle = ( static_cast< double >( firstPhase ) - static_cast< double >( cl_bobup.GetFloat() ) ) *
			pi / ( 1.0 - static_cast< double >( cl_bobup.GetFloat() ) ) + pi;
	const double verticalSine = sin( angle );
	const float amplitude = speed * 0.005f;
	s_flCSVerticalBob = amplitude;
	const double vertical = static_cast< double >( s_flCSVerticalBob ) * 0.3 +
		static_cast< double >( s_flCSVerticalBob ) * 0.7 * verticalSine;
	s_flCSVerticalBob = static_cast< float >( vertical );
	if ( vertical > 4.0 ) // Native compares the UNSPILLED wide value first.
		s_flCSVerticalBob = 4.0f;
	else if ( -7.0f > s_flCSVerticalBob )
		s_flCSVerticalBob = -7.0f;

	const float cycle = cl_bobcycle.GetFloat();
	const float dividedTime = s_flCSBobTime / cycle;
	const float doubledTime = dividedTime * 2.0f;
	const int wholeDoubleCycle = _mm_cvttss_si32( _mm_set_ss( doubledTime ) );
	const float fullCycles = static_cast< float >( wholeDoubleCycle ) * cycle;
	const float doubledCycles = fullCycles * 2.0f;
	const float twiceCycle = cycle * 2.0f;
	const float secondPhase = ( s_flCSBobTime - doubledCycles ) / twiceCycle;
	const float secondUp = cl_bobup.GetFloat();
	if ( secondUp > secondPhase )
		angle = pi * static_cast< double >( secondPhase ) / static_cast< double >( secondUp );
	else
		angle = ( static_cast< double >( secondPhase ) - static_cast< double >( cl_bobup.GetFloat() ) ) *
			pi / ( 1.0 - static_cast< double >( cl_bobup.GetFloat() ) ) + pi;
	const double lateral = 0.7 * static_cast< double >( amplitude ) * sin( angle ) +
		0.3 * static_cast< double >( amplitude );
	s_flCSLateralBob = static_cast< float >( lateral );
	if ( lateral > 4.0 )
		s_flCSLateralBob = 4.0f;
	else if ( -7.0f > s_flCSLateralBob )
		s_flCSLateralBob = -7.0f;
	return 0.0f;
}

// C231B20: both basis vectors precede the genuine virtual calculator.
void CWeaponCSBase::AddViewmodelBob( CBaseViewModel *viewmodel, Vector &origin, QAngle &angles )
{
	Vector forward, right;
	AngleVectors( angles, &forward, &right, NULL );
	CalcViewmodelBob();
	const float forwardBob = s_flCSVerticalBob * 0.4f;
	origin.x = forward.x * forwardBob + origin.x;
	const float y = forwardBob * forward.y + origin.y;
	origin.z = forwardBob * forward.z + origin.z;
	origin.y = y;
	origin.z = s_flCSVerticalBob * 0.1f + origin.z;
	angles.z = s_flCSVerticalBob * 0.5f + angles.z;
	angles.x -= s_flCSVerticalBob * 0.4f;
	angles.y -= s_flCSLateralBob * 0.3f;
}
#endif
#pragma float_control( pop )
#ifndef CLIENT_DLL
#pragma float_control( precise, on, push )
// S24F4F0/v159: genuine existing PhysicsSplash override; fourth argument unused.
// Fluid contents come from GetWaterType(), NOT the scaled-speed argument.
bool CWeaponCSBase::PhysicsSplash( const Vector &point, const Vector &normal, float rawSpeed, float /*scaledSpeed*/ )
{
	if ( !( rawSpeed > 20.0f ) ) // Native COMISS/JBE rejects equality AND unordered.
		return false;
	const float base = GetCSWpnData().m_WeaponType == TERROR_WEAPONTYPE_PISTOL ? 4.0f : 6.0f;
	float factor = rawSpeed * 0.0024999999f;
	if ( factor < 0.0f )
		factor = 0.0f;
	else if ( factor > 1.0f )
		factor = 1.0f;
	CEffectData data;
	data.m_vOrigin = point;
	data.m_vNormal = normal;
	const float minimum = factor * 3.0f + base;
	const float maximum = minimum + 1.0f;
	data.m_flScale = random->RandomFloat( minimum, maximum );
	if ( GetWaterType() & CONTENTS_SLIME )
		data.m_fFlags |= FX_WATER_IN_SLIME;
	DispatchEffect( "gunshotsplash", data );
	return true;
}
#pragma float_control( pop )
#endif

#ifdef CLIENT_DLL
void CWeaponCSBase::HideCrosshair()
{
	CHudTerrorCrosshair *crosshair = GetTerrorCrosshair();
	if ( crosshair )
		crosshair->SetCrosshairState( 4 );
}

void CWeaponCSBase::DrawCrosshair()
{
	if ( ::crosshair.GetInt() == 0 )
	{
		HideCrosshair();
		return;
	}
	C_CSPlayer *player = static_cast< C_CSPlayer * >( C_BasePlayer::GetLocalPlayer() );
	if ( !player || !player->CanAttack() || player->IsInVGuiInputMode() ||
		player->IsThirdPersonCameraActive() || HasNativePickupState() )
	{
		HideCrosshair();
		return;
	}
	CHudTerrorCrosshair *crosshair = GetTerrorCrosshair();
	if ( crosshair )
		crosshair->SetCrosshairState( 3 );
}
#endif
