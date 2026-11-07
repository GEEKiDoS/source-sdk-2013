//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#ifdef CLIENT_DLL
#include "l4d2/player/c_terrorplayer.h"
#else
#include "l4d2/player/terrorplayer.h"
#endif
#include "l4d2/weapons/terror_gun.h"
#include "igameevents.h"
#include "l4d2/effects/terror_fx_shared.h"
#include "in_buttons.h"
#include "ammodef.h"
#include "activitylist.h"
#ifndef CLIENT_DLL
#include "recipientfilter.h"
#endif
#include "datacache/imdlcache.h"
#include "tier1/fmtstr.h"
#include "tier1/utlvector.h"
#include "l4d2/terror_message_text.h"
#ifdef CLIENT_DLL
#include "baseviewmodel_shared.h"
#include "particle_property.h"
#include "particle_parse.h"
#include "particles_new.h"
#include "l4d2/weapons/terror_viewmodel.h"
#include "l4d2/effects/terror_flashlight.h"
#include "l4d2/hud/terror_hud_helpers.h"
#include "l4d2/ui/terror_radial_menu.h"
#include "game/client/iviewport.h"
#include "l4d2/hud/hud_terror_crosshair.h"
#include "c_rumble.h"
#include "rumble_shared.h"
#include "eventlist.h"
#include "iinput.h"
#include "itempents.h"
#include "view.h"
#endif
#include "l4d2/weapons/terror_gun_console_server_cvars.inc"
#include "tier0/memdbgon.h"

static ConVar survivor_incapacitated_cycle_time( "survivor_incapacitated_cycle_time", "0.3", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_CHEAT | FCVAR_REPLICATED
#endif
, "New cycle time (RoF) used when incapacitated, overriding weapon." );
static ConVar z_gun_debug_player_index( "z_gun_debug_player_index", "0", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_CHEAT | FCVAR_REPLICATED
#endif
 );
ConVar sv_infinite_ammo( "sv_infinite_ammo", "0", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_CHEAT | FCVAR_REPLICATED
#endif
, "Player's active weapon will never run out of ammo" );
ConVar sv_infinite_primary_ammo( "sv_infinite_primary_ammo", "0", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_CHEAT | FCVAR_REPLICATED
#endif
, "Player's primary weapon will never run out of ammo" );
ConVar survivor_incapacitated_accuracy_penalty( "survivor_incapacitated_accuracy_penalty", "0.4",
	
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
, "Penalty given for shooting while incapacitated." );
ConVar upgrade_laser_sight_spread_factor( "upgrade_laser_sight_spread_factor", "0.4", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar z_gun_debug_spread( "z_gun_debug_spread", "0", 
#if defined( CLIENT_DLL )
	0x0000600C
#else
	FCVAR_GAMEDLL | FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
extern ConVar tongue_victim_accuracy_penalty;
#ifdef CLIENT_DLL
static ConVar z_gun_kick( "z_gun_kick", "0", 
#if defined( CLIENT_DLL )
	0x00006008
#else
	FCVAR_CHEAT | FCVAR_REPLICATED
#endif
, "Firing a gun can knock the player's view this amount of the current spread" );
#endif

#ifdef CLIENT_DLL
IMPLEMENT_CLIENTCLASS( C_TerrorGun, DT_TerrorGun, CTerrorGun )
#else
IMPLEMENT_SERVERCLASS( CTerrorGun, DT_TerrorGun )
#endif
BEGIN_NETWORK_TABLE( CTerrorGun, DT_TerrorGun )
#ifdef CLIENT_DLL
	RecvPropBool( RECVINFO( m_isDualWielding ) ),
	RecvPropBool( RECVINFO( m_hasDualWeapons ) ),
	RecvPropInt( RECVINFO( m_upgradeBitVec ) ),
	RecvPropBool( RECVINFO( m_reloadFromEmpty ) ),
	RecvPropBool( RECVINFO( m_inInitialPickup ) ),
	RecvPropInt( RECVINFO( m_partialReloadStage ) ),
#else
	SendPropBool( SENDINFO( m_isDualWielding ) ),
	SendPropBool( SENDINFO( m_hasDualWeapons ) ),
	SendPropInt( SENDINFO( m_upgradeBitVec ), 32 ),
	SendPropBool( SENDINFO( m_reloadFromEmpty ) ),
	SendPropBool( SENDINFO( m_inInitialPickup ) ),
	SendPropInt( SENDINFO( m_partialReloadStage ), 3, SPROP_UNSIGNED ),
#endif
END_NETWORK_TABLE()

#ifdef CLIENT_DLL
BEGIN_PREDICTION_DATA( CTerrorGun )
	DEFINE_PRED_FIELD( m_isDualWielding, FIELD_BOOLEAN, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_hasDualWeapons, FIELD_BOOLEAN, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_reloadFromEmpty, FIELD_BOOLEAN, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_inInitialPickup, FIELD_BOOLEAN, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_partialReloadStage, FIELD_INTEGER, FTYPEDESC_INSENDTABLE ),
END_PREDICTION_DATA()
#endif

static ConVar z_gun_horiz_punch( "z_gun_horiz_punch", "0", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_CHEAT | FCVAR_REPLICATED
#endif
, "Toggles horizontal punchangles for guns" );
static ConVar z_gun_vertical_punch( "z_gun_vertical_punch", "1", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_CHEAT | FCVAR_REPLICATED
#endif
, "Toggles vertical punchangles for guns" );

CTerrorGun::CTerrorGun()
{
	m_upgradeBitVec = 0;
	m_bReloadsSingly = false;
	m_inInitialPickup = false;
}

// S3E7DC0/C30B4F0: changes preceding the common Deploy are not rolled back.
bool CTerrorGun::Deploy()
{
	if ( !CanDeploy() )
		return false;
	m_flCurrentSpread = 0.0f;
	CTerrorPlayer *player = GetTerrorPlayerOwner();
	if ( !player )
		return false;
#ifndef CLIENT_DLL
	if ( m_hasDualWeapons )
	{
		m_isDualWielding = player->IsAbleToDualWield();
		const int clip = m_iClip1;
		if ( clip > GetMaxClip1() )
		{
			m_nStoredSingleClip = clip / 2;
			m_iClip1 -= clip / 2;
		}
		else if ( m_nStoredSingleClip > 0 )
		{
			const int stored = m_nStoredSingleClip;
			if ( stored + m_iClip1 <= GetMaxClip1() )
			{
				m_iClip1 += stored;
				m_nStoredSingleClip = 0;
			}
		}
	}
#endif
	if ( !BaseClass::Deploy() )
		return false;
	if ( m_inInitialPickup && m_partialReloadStage == 0 )
	{
#ifndef CLIENT_DLL
		const int ammo = GetPrimaryAmmoType();
		const int maximum = GetMaxClip1();
		player->GiveAmmo( maximum, ammo, false );
#endif
		MDLCACHE_CRITICAL_SECTION();
		Reload();
	}
	return true;
}

// S3E7C50/C30C020: the virtual policy is the shotgun writer on BOTH platforms.
void CTerrorGun::Spawn()
{
	m_isDualWielding = false;
	m_hasDualWeapons = false;
	m_bReloadsSingly = ReloadsSingly();
	m_flCurrentSpread = 0.0f;
	m_partialReloadStage = 0;
	m_reloadFromEmpty = false;
#ifndef CLIENT_DLL
	m_nStoredSingleClip = 0;
#endif
	BaseClass::Spawn();
	if ( m_inInitialPickup )
		m_iClip1 = 0;
	m_upgradeBitVec = 0;
}

float CTerrorGun::GetMaxMovementSpread() const
{
	return GetTerrorWpnData().m_flMaxMovementSpread;
}

float CTerrorGun::GetMinStandingSpread() const
{
	return GetTerrorWpnData().m_flMinStandingSpread;
}

float CTerrorGun::GetMinDuckingSpread() const
{
	return GetTerrorWpnData().m_flMinDuckingSpread;
}

#pragma float_control( precise, on, push )
// One process-wide native history; no Spawn/Deploy/debug-toggle reset.
static CUtlVector< float > s_TerrorGunSpreadSamples;

#ifdef CLIENT_DLL
void CTerrorGun::UpdateSpread()
{
	const CTerrorWeaponInfo &info = GetTerrorWpnData();
	CTerrorPlayer *player = GetTerrorPlayerOwner();
	if ( !player )
	{
		m_flCurrentSpread = GetMinStandingSpread();
		return;
	}
	float speed = 220.0f;
	if ( player->GetFlags() & FL_ONGROUND )
	{
		const Vector &velocity = player->GetAbsVelocity();
		const float x2 = velocity.x * velocity.x;
		const float y2 = velocity.y * velocity.y;
		const float z2 = velocity.z * velocity.z;
		speed = sqrtf( ( x2 + y2 ) + z2 );
		if ( speed > 220.0f ) speed = 220.0f;
		else if ( speed < 0.0f ) speed = 0.0f;
		speed = speed * ( ( player->GetFlags() & FL_DUCKING ) ? 0.5f : 0.75f );
	}
	float minimum = GetMinInAirSpread();
	const float maximumMovement = GetMaxMovementSpread();
	if ( player->GetFlags() & FL_ONGROUND )
	{
		if ( ( player->GetFlags() & FL_DUCKING ) && !player->IsIncapacitated() )
			minimum = GetMinDuckingSpread();
		else
			minimum = GetMinStandingSpread();
	}
	const float movementFraction = speed * 0.004545454401522875f;
	float target = minimum + ( maximumMovement - minimum ) * movementFraction;
	if ( player->IsIncapacitated() && GetWeaponID() != TERROR_WEAPON_PISTOL_MAGNUM )
		target = target + survivor_incapacitated_accuracy_penalty.GetFloat() * 10.0f;
	if ( player->m_tongueOwner.Get() )
		target = target + tongue_victim_accuracy_penalty.GetFloat() * 10.0f;
	if ( HasUpgrade( TERROR_UPGRADE_LASER_SIGHT ) )
		target = upgrade_laser_sight_spread_factor.GetFloat() * target;
	const float fov = static_cast< float >( player->GetFOV() );
	const int defaultFOV = player->GetDefaultFOV();
	target = static_cast< float >( ( static_cast< double >( fov ) / static_cast< double >( defaultFOV ) ) *
		static_cast< double >( target ) );
	const float decay = info.m_flSpreadDecay * gpGlobals->frametime;
	const float difference = m_flCurrentSpread - target;
	float spread = m_flCurrentSpread - decay * difference;
	if ( spread > 90.0f ) spread = 90.0f;
	else if ( spread < 0.0f ) spread = 0.0f;
	m_flCurrentSpread = spread;
	if ( z_gun_debug_spread.GetInt() == player->entindex() )
	{
		if ( s_TerrorGunSpreadSamples.Count() >= 30 )
			s_TerrorGunSpreadSamples.Remove( s_TerrorGunSpreadSamples.Count() - 1 );
		s_TerrorGunSpreadSamples.AddToHead( m_flCurrentSpread );
		float sum = 0.0f;
		float maximum = 0.0f;
		for ( int i = 0; i < s_TerrorGunSpreadSamples.Count(); ++i )
		{
			const float sample = s_TerrorGunSpreadSamples[i];
			sum = sum + sample;
			if ( sample > maximum ) maximum = sample;
		}
		const float average = s_TerrorGunSpreadSamples.Count() > 0 ?
			sum / static_cast< float >( s_TerrorGunSpreadSamples.Count() ) : m_flCurrentSpread;
		Msg( "(%.1f) spread %.1f ( avg %.1f ) ( max %.1f )\n", gpGlobals->curtime,
			m_flCurrentSpread, average, maximum );
	}
}
#else
void CTerrorGun::UpdateSpread()
{
	const CTerrorWeaponInfo &info = GetTerrorWpnData();
	CTerrorPlayer *player = GetTerrorPlayerOwner();
	if ( !player )
	{
		m_flCurrentSpread = GetMinStandingSpread();
		return;
	}
	float movement = 220.0f;
	if ( player->GetFlags() & FL_ONGROUND )
	{
		movement = player->GetAbsVelocity().Length();
		if ( movement > 220.0f )
			movement = 220.0f;
		else if ( 0.0f > movement )
			movement = 0.0f;
		movement = ( player->GetFlags() & FL_DUCKING ) ? 0.5f * movement : 0.75f * movement;
	}
	float minimum = GetMinInAirSpread();
	const float maximumMovement = GetMaxMovementSpread();
	if ( player->GetFlags() & FL_ONGROUND )
	{
		if ( !( player->GetFlags() & FL_DUCKING ) || player->IsIncapacitated() )
			minimum = GetMinStandingSpread();
		else
			minimum = GetMinDuckingSpread();
	}
	const float difference = maximumMovement - minimum;
	const float normalizedMovement = movement * 0.004545454401522875f;
	float target = difference * normalizedMovement + minimum;
	if ( player->IsIncapacitated() && GetWeaponID() != TERROR_WEAPON_PISTOL_MAGNUM )
		target += survivor_incapacitated_accuracy_penalty.GetFloat() * 10.0f;
	if ( player->GetTongueOwner() )
		target += tongue_victim_accuracy_penalty.GetFloat() * 10.0f;
	if ( HasUpgrade( TERROR_UPGRADE_LASER_SIGHT ) )
		target = upgrade_laser_sight_spread_factor.GetFloat() * target;
	const int fov = player->GetFOV();
	const int defaultFov = player->GetDefaultFOV();
	const int ratio = fov / defaultFov; // Native CDQ/IDIV, no floating-ratio substitute.
	const float current = m_flCurrentSpread;
	const float baseDecay = info.m_flSpreadDecay;
	const float targetScaled = static_cast< float >( ratio ) * target;
	const float decay = baseDecay * gpGlobals->frametime;
	const float delta = current - targetScaled;
	float spread = current - decay * delta;
	if ( spread > 90.0f )
		spread = 90.0f;
	else if ( 0.0f > spread )
		spread = 0.0f;
	m_flCurrentSpread = spread;
	if ( z_gun_debug_spread.GetInt() == player->entindex() )
	{
		if ( s_TerrorGunSpreadSamples.Count() >= 30 )
			s_TerrorGunSpreadSamples.Remove( s_TerrorGunSpreadSamples.Count() - 1 );
		s_TerrorGunSpreadSamples.AddToHead( m_flCurrentSpread );
		float sum = 0.0f;
		float highest = 0.0f;
		for ( int i = 0; i < s_TerrorGunSpreadSamples.Count(); ++i )
		{
			const float sample = s_TerrorGunSpreadSamples[i];
			sum += sample;
			if ( sample > highest )
				highest = sample;
		}
		const float average = s_TerrorGunSpreadSamples.Count() > 0 ?
			sum / static_cast< float >( s_TerrorGunSpreadSamples.Count() ) : m_flCurrentSpread;
		Msg( "(%.1f) spread %.1f ( avg %.1f ) ( max %.1f )\n", gpGlobals->curtime, m_flCurrentSpread, average, highest );
	}
}
#endif

void CTerrorGun::IncreaseSpread()
{
	CTerrorPlayer *player = GetTerrorPlayerOwner();
	if ( !player )
		return;
	const CTerrorWeaponInfo &info = GetTerrorWpnData();
	const float spread = info.m_flSpreadPerShot;
#ifdef CLIENT_DLL
	const float product = static_cast< float >( static_cast< double >( player->GetFOV() ) * spread );
	float increment = static_cast< float >( static_cast< double >( product ) / player->GetDefaultFOV() );
#else
	const float product = static_cast< float >( player->GetFOV() ) * spread;
	float increment = product / static_cast< float >( player->GetDefaultFOV() );
#endif
	if ( HasUpgrade( TERROR_UPGRADE_LASER_SIGHT ) )
		increment = upgrade_laser_sight_spread_factor.GetFloat() * increment;
	float next = m_flCurrentSpread + increment;
	if ( !( info.m_flMaxSpread > next ) )
		next = info.m_flMaxSpread;
	m_flCurrentSpread = next;
}

void CTerrorGun::ApplyRecoil( CTerrorPlayer *player )
{
	const CTerrorWeaponInfo &info = GetTerrorWpnData();
	const bool airborne = ( player->GetFlags() & FL_ONGROUND ) == 0;
	const bool stationary = player->GetAbsVelocity().Length2D() <= 5.0f;
	const bool ducking = ( player->GetFlags() & FL_DUCKING ) != 0;
	float vertical = info.m_flVerticalPunch;
	float horizontal = info.m_flHorizontalPunch;
	if ( !z_gun_horiz_punch.GetBool() )
		horizontal = 0.0f;
	if ( !z_gun_vertical_punch.GetBool() )
		vertical = 0.0f;
	if ( airborne )
	{
		horizontal *= 2.0f;
		vertical *= 2.0f;
	}
	else if ( ducking )
	{
		if ( stationary )
		{
			vertical *= 0.5f;
			horizontal *= 0.5f;
		}
		else
		{
			vertical *= 0.9f;
			horizontal *= 0.9f;
		}
	}
	else if ( !stationary )
	{
		horizontal *= 1.25f;
		vertical *= 1.25f;
	}
	if ( player->m_iDirection != 0 )
		horizontal = -horizontal;
	const float directionChance = info.m_flHorizontalPunchDirChance;
	if ( directionChance > static_cast< float >( SharedRandomInt( "HorizKickDir", 0, 100, 0 ) ) )
		player->m_iDirection = player->m_iDirection == 0;
	QAngle punch = player->GetPunchAngle();
	punch.x -= vertical;
	punch.y -= horizontal;
	player->SetPunchAngle( punch );
}
#pragma float_control( pop )

float CTerrorGun::GetMinInAirSpread() const
{
	return GetTerrorWpnData().m_flMinInAirSpread;
}

#pragma float_control( precise, on, push )
void CTerrorGun::OnSwing()
{
	BaseClass::OnSwing();
	const CTerrorWeaponInfo &info = GetTerrorWpnData();
	const float maximum = info.m_flMaxSpread;
	const double addition = GetMinInAirSpread();
	if ( !( static_cast< double >( maximum ) > addition + static_cast< double >( m_flCurrentSpread ) ) )
		m_flCurrentSpread = info.m_flMaxSpread;
	else
	{
		const double repeatedAddition = GetMinInAirSpread();
		m_flCurrentSpread = static_cast< float >( repeatedAddition + static_cast< double >( m_flCurrentSpread ) );
	}
#ifdef CLIENT_DLL
	RumbleEffect( RUMBLE_CROWBAR_SWING, 0, RUMBLE_FLAG_RESTART );
#endif
}
#pragma float_control( pop )

#pragma float_control( precise, on, push )
// S3E7320/C30B570: native wide return with the empty-cycle MULSS/F32 spill.
double CTerrorGun::GetFireRate()
{
	CTerrorPlayer *owner = GetTerrorPlayerOwner();
	if ( owner && owner->IsIncapacitated() )
		return survivor_incapacitated_cycle_time.GetFloat();
	if ( m_iClip1 <= 0 )
	{
		const float cycle = GetTerrorWpnData().m_flCycleTime;
		return static_cast< double >( static_cast< float >( cycle * 1.5f ) );
	}
	return GetTerrorWpnData().m_flCycleTime;
}
#pragma float_control( pop )

static void ReportGunAttackBlocked( CTerrorPlayer *player, const char *reason )
{
	if ( z_gun_debug_player_index.GetInt() == player->entindex() && ( player->m_afButtonPressed & IN_ATTACK ) )
	{
#ifdef CLIENT_DLL
		const char *side = "client";
#else
		const char *side = "server";
#endif
		CFmtStrN< 128 > text( "%s can't attack - %s\n", side, reason );
		TerrorMessageText( player, text.String(), 0 );
	}
}

void CTerrorGun::PrimaryAttack()
{
	CTerrorPlayer *player = GetTerrorPlayerOwner();
	if ( !player )
		return;
	if ( !player->CanAttack() )
	{
		ReportGunAttackBlocked( player, "!player->CanAttack()" );
		return;
	}
	if ( !IsWeaponVisible() )
	{
		ReportGunAttackBlocked( player, "!IsWeaponVisible()" );
		return;
	}
	if ( !HandlePrimaryAttackUse() )
		FireBullet();
}

#pragma float_control( precise, on, push )
void CTerrorGun::FireBullet()
{
	CTerrorPlayer *player = GetTerrorPlayerOwner();
	if ( !player )
		return;
	if ( m_isHoldingFireButton && !IsFullyAutomatic() )
	{
		ReportGunAttackBlocked( player, "IsHoldingFireButton() && !IsFullyAutomatic()" );
		return;
	}
	if ( !m_releasedFireButton )
	{
		ReportGunAttackBlocked( player, "IsFirePreventedUntilButtonRelease()" );
		return;
	}
	SuppressHelpingHand( 0.0f );
	MDLCACHE_CRITICAL_SECTION();
	m_isHoldingFireButton = true;
	++player->m_iShotsFired;
#ifndef CLIENT_DLL
	const CTerrorWeaponInfo &info = GetTerrorWpnData();
	IGameEvent *event = gameeventmanager->CreateEvent( m_iClip1 > 0 ? "weapon_fire" : "weapon_fire_on_empty" );
	if ( event )
	{
		const char *name = GetClassname();
		if ( !name ) name = "";
		if ( strncmp( name, "weapon_", 7 ) == 0 ) name += 7;
		event->SetInt( "userid", player->GetUserID() );
		event->SetString( "weapon", name );
		event->SetInt( "weaponid", GetWeaponID() );
		event->SetInt( "count", info.m_iBullets );
		gameeventmanager->FireEvent( event );
	}
#endif
	if ( m_iClip1 <= 0 )
	{
		if ( m_bFireOnEmpty )
		{
			PlayEmptySound();
			m_flNextPrimaryAttack = static_cast< float >( static_cast< double >( gpGlobals->curtime ) + 0.2 );
		}
		ReportGunAttackBlocked( player, "m_iClip1 <= 0" );
		return;
	}
#ifndef CLIENT_DLL
	const int oldClip = m_iClip1;
#endif
	const bool removeAmmo = sv_infinite_ammo.GetInt() >= 0 &&
		( GetSlot() != 0 || sv_infinite_primary_ammo.GetInt() >= 0 );
	if ( removeAmmo )
		--m_iClip1;
#ifndef CLIENT_DLL
	const float oldPercent = static_cast< float >( oldClip ) / GetMaxClip1() * 100.0f;
	const float newPercent = static_cast< float >( m_iClip1 ) / GetMaxClip1() * 100.0f;
	if ( oldPercent > 40.0f && newPercent <= 40.0f )
	{
		event = gameeventmanager->CreateEvent( "weapon_fire_at_40" );
		if ( event )
		{
			const char *name = GetClassname();
			if ( !name ) name = "";
			if ( strncmp( name, "weapon_", 7 ) == 0 ) name += 7;
			event->SetInt( "userid", player->GetUserID() );
			event->SetString( "weapon", name );
			event->SetInt( "weaponid", GetWeaponID() );
			gameeventmanager->FireEvent( event );
		}
	}
#endif
	player->DoMuzzleFlash();
	SendWeaponAnim( GetPrimaryAttackActivity() );
#ifndef CLIENT_DLL
	player->SetNoiseLevel( info.m_flNoiseFactor );
#endif
	const float horizontal = SharedRandomFloat( "CTerrorGun::FireBullet HorizSpread", -m_flCurrentSpread, m_flCurrentSpread, 0 );
	const float vertical = SharedRandomFloat( "CTerrorGun::FireBullet VertSpread", -m_flCurrentSpread, m_flCurrentSpread, 0 );
	QAngle angles = player->EyeAngles();
#ifndef CLIENT_DLL
	if ( player->ShouldAutoaim() )
	{
		int kind;
		const float deflection = GetMaxAutoAimDeflection();
		const float range = GetMaxAutoAimRange();
		const float scale = WeaponAutoAimScale();
		const Vector direction = player->GetAutoaimVector( scale, range, deflection, &kind );
		VectorAngles( direction, angles );
	}
#else
	if ( z_gun_kick.GetFloat() > 0.0f )
	{
		angles.x += z_gun_kick.GetFloat() * horizontal;
		angles.y += z_gun_kick.GetFloat() * vertical;
		engine->SetViewAngles( angles );
		angles = player->EyeAngles();
	}
#endif
	angles.x += horizontal;
	angles.y += vertical;
	const QAngle &punch = player->GetPunchAngle();
	angles.x = punch.x + angles.x;
	angles.y = punch.y + angles.y;
	angles.z = punch.z + angles.z;
#ifdef CLIENT_DLL
	player->OnMountedWeaponFired();
	const int rumble = GetRumbleEffect();
	if ( rumble != RUMBLE_INVALID )
		RumbleEffect( rumble, 0, RUMBLE_FLAG_RESTART );
#endif
	WeaponSound_t sound = SINGLE;
	if ( IsDualWielding() && m_iClip1 % 2 == 1 )
		sound = WPN_DOUBLE;
	else if ( HasUpgrade( TERROR_UPGRADE_INCENDIARY_AMMO ) )
		sound = SHOOT_INCENDIARY;
	if ( !IsFullyAutomatic() ||
		 ( GetCSWpnData().m_WeaponType == TERROR_WEAPONTYPE_SHOTGUN && GetCSWpnData().m_iTier == 2 ) )
	{
		m_flNextPrimaryAttack = static_cast< float >( GetFireRate() + static_cast< double >( gpGlobals->curtime ) );
		FireTerrorBullets( player, player->Weapon_ShootPosition(), angles, GetWeaponID(),
			m_iClip1 % 2 != 1, CBaseEntity::GetPredictionRandomSeed(), gpGlobals->curtime );
		if ( !m_bReloadsSingly || player->IsZoomed() )
			WeaponSound( sound, gpGlobals->curtime );
	}
	else
	{
		const float cycle = static_cast< float >( GetFireRate() );
		if ( gpGlobals->curtime - m_flNextPrimaryAttack > gpGlobals->frametime )
			m_flNextPrimaryAttack = gpGlobals->curtime - gpGlobals->frametime;
		int shots = 0;
		while ( gpGlobals->curtime >= m_flNextPrimaryAttack )
		{
			if ( shots > m_iClip1 )
				break;
			FireTerrorBullets( player, player->Weapon_ShootPosition(), angles, GetWeaponID(),
				m_iClip1 % 2 != 1, CBaseEntity::GetPredictionRandomSeed(), m_flNextPrimaryAttack );
			WeaponSound( sound, m_flNextPrimaryAttack );
			m_flNextPrimaryAttack += cycle;
			if ( shots != 0 )
				--m_iClip1;
			++shots;
		}
	}
#ifndef CLIENT_DLL
	if ( removeAmmo && ( HasUpgrade( TERROR_UPGRADE_INCENDIARY_AMMO ) || HasUpgrade( TERROR_UPGRADE_EXPLOSIVE_AMMO ) ) )
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
#endif
	if ( m_iClip1 == 0 && player->GetAmmoCount( GetPrimaryAmmoType() ) <= 0 )
		player->SetSuitUpdate( "!HEV_AMO0", false, 0 );
	SetWeaponIdleTime( static_cast< float >( static_cast< double >( gpGlobals->curtime ) + 1.8 ) );
	ApplyRecoil( player );
	IncreaseSpread();
	if ( m_iClip1 == 0 )
		m_reloadFromEmpty = true;
}
#pragma float_control( pop )

void CTerrorGun::ItemPostFrame()
{
	CTerrorPlayer *owner = GetTerrorPlayerOwner();
	if ( owner )
	{
		if ( HasScope() && ( owner->m_afButtonPressed & IN_ZOOM ) && !owner->GetUseEntity() )
			ToggleZoom();
		UpdateSpread();
	}
	BaseClass::ItemPostFrame();
#ifndef CLIENT_DLL
	if ( ShouldUnzoom() )
	{
		owner = GetTerrorPlayerOwner();
		if ( owner )
		{
			const int fov = owner->GetFOV();
			if ( fov != owner->GetDefaultFOV() )
				ToggleZoom();
		}
	}
	UpdateDualState();
#endif
}

void CTerrorGun::ItemBusyFrame()
{
	UpdateSpread();
	BaseClass::ItemBusyFrame();
	CBaseCombatCharacter *rawOwner = GetOwner();
	if ( rawOwner && rawOwner->IsPlayer() )
	{
#ifndef CLIENT_DLL
		if ( ShouldUnzoom() )
		{
			CTerrorPlayer *owner = GetTerrorPlayerOwner();
			if ( owner )
			{
				const int fov = owner->GetFOV();
				if ( fov != owner->GetDefaultFOV() )
					ToggleZoom();
			}
		}
		UpdateDualState();
#endif
	}
}

#pragma float_control( precise, on, push )
// S3E8860 / C30C150: native entry, real common reload, then refund/callbacks.
bool CTerrorGun::Reload()
{
	CTerrorPlayer *player = GetTerrorPlayerOwner();
	if ( !player )
		return false;
	if ( player->m_reviveTarget.Get() || player->IsPerformingSelfUseAction( 0 ) ||
		( m_flNextPrimaryAttack > gpGlobals->curtime && !m_inInitialPickup ) )
	{
		m_reloadQueuedStartTime = gpGlobals->curtime + 1.0f;
		return false;
	}
	const bool result = BaseClass::Reload();
	m_flNextSecondaryAttack = gpGlobals->curtime;
	m_reloadStartActivityTime = gpGlobals->curtime;
	if ( !result )
	{
		m_reloadQueuedStartTime = gpGlobals->curtime + 1.0f;
		return false;
	}
	m_reloadQueuedStartTime = 0.0f;
#ifndef CLIENT_DLL
	const int zoomFOV = GetZoomFOV();
	if ( player->GetFOV() == zoomFOV )
		player->SetFOV( player, 0, 0.1f, 0 );
#endif
	const int reserve = player->GetAmmoCount( GetPrimaryAmmoType() );
	if ( reserve != 999 )
	{
		player->SetAmmoCount( reserve + m_iClip1, GetPrimaryAmmoType() );
		m_iClip1 = 0;
	}
#ifndef CLIENT_DLL
	player->OnTerrorReloadStarted( m_bReloadsSingly, m_iClip1, m_partialReloadStage != 0 );
#endif
	return result;
}

#pragma float_control( pop )


// S3E8B80 / C30C2E0: complete base clips first, then the three actual flags.
void CTerrorGun::FinishReload()
{
	BaseClass::FinishReload();
	m_partialReloadStage = 0;
	m_inInitialPickup = false;
	m_reloadFromEmpty = false;
#ifndef CLIENT_DLL
	if ( CTerrorPlayer *player = GetTerrorPlayerOwner() )
		player->OnTerrorReloadFinished();
#endif
}

// S3E6F50 / C30B360: the real base owns client sound stop and reload clear.
void CTerrorGun::AbortReload()
{
	BaseClass::AbortReload();
	SendWeaponAnim( ACT_VM_IDLE );
	if ( CTerrorPlayer *player = GetTerrorPlayerOwner() )
		player->DoTerrorAnimationEvent( TERROR_ANIMEVENT_RELOAD_END, 0 );
}

// S3E7F90 / C30BD00: server restores the dual view; client retires real effects.
bool CTerrorGun::Holster( CBaseCombatWeapon *switchingTo )
{
#ifdef CLIENT_DLL
	m_muzzleFlashTimer.Invalidate();
	CParticleProperty *effects = ParticleProp();
	if ( effects )
		effects->StopEmissionAndDestroyImmediately();
	if ( GetTerrorPlayerOwner() )
	{
		const int viewModelIndex = m_nViewModelIndex;
		CBaseViewModel *viewModel = GetTerrorPlayerOwner()->GetViewModel( viewModelIndex );
		if ( viewModel && viewModel->ParticleProp() )
			viewModel->ParticleProp()->StopEmissionAndDestroyImmediately();
	}
#else
	if ( m_hasDualWeapons )
		m_isDualWielding = true;
#endif
	return BaseClass::Holster( switchingTo );
}

bool CTerrorGun::HasUpgrade( int upgrade ) const
{
	// S3E77D0: reached native callers supply the three bit indexes.
	return ( static_cast< unsigned int >( m_upgradeBitVec.Get() ) & ( 1u << upgrade ) ) != 0;
}

bool CTerrorGun::HasAnyUpgrades()
{
	// S3E7800 normalizes the actual bit vector even in this query.
	m_upgradeBitVec &= 7;
	return m_upgradeBitVec != 0;
}

void CTerrorGun::RemoveUpgrade( int upgrade )
{
	// S3E7AD0 changes only the bit, never loaded ammunition.
	if ( upgrade >= 0 && upgrade <= 2 )
		m_upgradeBitVec &= ~( 1 << upgrade );
}

void CTerrorGun::RemoveAllUpgrades()
{
	// S3E7B40 leaves loaded-count/clip/reserve untouched.
	m_upgradeBitVec = 0;
}

void CTerrorGun::RemoveUpgradeAmmo()
{
	// S352240 deliberately retains the M60 loaded-count byte.
	if ( GetWeaponID() == TERROR_WEAPON_RIFLE_M60 )
	{
		m_iClip1 = static_cast< unsigned char >( m_nUpgradedPrimaryAmmoLoaded );
	}
	else
	{
		m_iClip1 -= static_cast< unsigned char >( m_nUpgradedPrimaryAmmoLoaded );
		m_nUpgradedPrimaryAmmoLoaded = 0;
	}
}

void CTerrorGun::ApplyUpgradeAmmo( int upgrade )
{
	// S3520C0: owner sampling and the native max-carry-plus-clip cap.
	CTerrorPlayer *player = GetTerrorPlayerOwner();
	if ( !player )
		return;
	if ( GetWeaponID() == TERROR_WEAPON_RIFLE_M60 )
	{
		m_nUpgradedPrimaryAmmoLoaded = static_cast< unsigned char >( m_iClip1 );
		return;
	}
	const int ammo = GetPrimaryAmmoType();
	if ( HasUpgrade( upgrade ) )
	{
		player->RemoveAmmo( m_iClip1 - static_cast< unsigned char >( m_nUpgradedPrimaryAmmoLoaded ), ammo );
	}
	else
	{
		player->SetAmmoCount( MIN( GetAmmoDef()->MaxCarry( ammo, player ) + GetMaxClip1(),
			m_iClip1 + player->GetAmmoCount( ammo ) ), ammo );
		m_iClip1 = GetMaxClip1();
	}
	m_nUpgradedPrimaryAmmoLoaded = static_cast< unsigned char >( GetMaxClip1() );
}

bool CTerrorGun::AddUpgrade( int upgrade )
{
	// S3E79B0/S3511E0. Unowned weapons can still acquire their real bit.
	if ( upgrade < 0 || upgrade > 2 )
		return false;
	const bool ammoUpgrade = upgrade != TERROR_UPGRADE_LASER_SIGHT;
	if ( ammoUpgrade && ( !UsesPrimaryAmmo() || !UsesClipsForAmmo1() ) )
		return false;
	if ( HasUpgrade( upgrade ) )
	{
		if ( !ammoUpgrade )
			return false;
		RemoveUpgrade( upgrade );
		RemoveUpgradeAmmo();
	}
	if ( ammoUpgrade )
	{
		const int other = upgrade == TERROR_UPGRADE_INCENDIARY_AMMO ?
			TERROR_UPGRADE_EXPLOSIVE_AMMO : TERROR_UPGRADE_INCENDIARY_AMMO;
		if ( HasUpgrade( other ) )
		{
			RemoveUpgrade( other );
			RemoveUpgradeAmmo();
		}
		ApplyUpgradeAmmo( upgrade );
	}
	else if ( CTerrorPlayer *player = GetTerrorPlayerOwner() )
	{
		player->EmitSound( "Player.LaserOn" );
	}
	m_upgradeBitVec |= 1 << upgrade;
	return true;
}

#ifndef CLIENT_DLL
void CTerrorGun::Use( CBaseEntity *activator, CBaseEntity *caller, USE_TYPE useType, float value )
{
    if ( activator && activator->IsPlayer() && CanBeUsedBy( static_cast< CTerrorPlayer * >( activator ) ) )
        BaseClass::Use( activator, caller, useType, value );
}
#endif

// S3E73C0. The mapped-activity argument is unread in the original helper.
void EmitTerrorWeaponReloadSound( CTerrorPlayer *player, CWeaponCSBase *weapon, WeaponSound_t sound )
{
#ifndef CLIENT_DLL
	CPASFilter filter( player->GetAbsOrigin() );
	filter.RemoveRecipient( player );
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CBasePlayer *observer = UTIL_PlayerByIndex( i );
		if ( observer && observer != player && observer->GetObserverTarget() == player && observer->GetObserverMode() == OBS_MODE_IN_EYE )
			filter.RemoveRecipient( observer );
	}
	CBaseEntity::EmitSound( filter, weapon->entindex(), weapon->GetCSWpnData().aShootSounds[sound] );
#endif
	// The corresponding original client helper is a genuine nullsub.
}

#pragma float_control( precise, on, push )
// S3E74C0 / C30B610: the gun owns the first translation and reload producer.
bool CTerrorGun::SendWeaponAnim( int iActivity )
{
	Activity mapped = TranslateViewModelActivity( static_cast< Activity >( iActivity ) );
	CTerrorPlayer *owner = GetTerrorPlayerOwner();
	if ( !owner )
		return BaseClass::SendWeaponAnim( mapped );
	bool reloadSound = false;
	switch ( LookupTerrorOriginalActivity( static_cast< Activity >( iActivity ) ) )
	{
	case TERROR_ORIGINAL_ACT_VM_DEPLOY:
		if ( m_inInitialPickup && m_partialReloadStage != 2 && !( m_partialReloadStage > 3 && m_partialReloadStage <= 5 ) )
		{
			owner->DoTerrorAnimationEvent( TERROR_ANIMEVENT_RELOAD, 0 );
			mapped = TranslateViewModelActivity( TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_PICKUP ) );
			reloadSound = true;
		}
		break;
	case TERROR_ORIGINAL_ACT_VM_RELOAD:
		if ( !m_bReloadsSingly )
		{
			owner->DoTerrorAnimationEvent( TERROR_ANIMEVENT_RELOAD, 0 );
			mapped = TranslateViewModelActivity( TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_RELOAD ) );
			reloadSound = true;
		}
		break;
	default:
		break;
	}
	if ( reloadSound )
	{
		const WeaponSound_t sound = IsDualWielding() ? RELOAD_DUAL : RELOAD;
		EmitTerrorWeaponReloadSound( owner, this, sound );
	}
	if ( mapped != TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_IDLE ) )
	{
		const int ownerIndex = owner->entindex();
		if ( z_gun_debug_player_index.GetInt() == ownerIndex )
		{
			const char *activityName = ActivityList_NameForIndex( mapped );
			const bool haveSequence = SelectWeightedSequence( mapped ) >= 0;
			if ( ( haveSequence && !Q_stristr( activityName, "idle" ) ) ||
				( !haveSequence && !Q_stristr( activityName, "holster" ) ) )
			{
				// Native captures time, then resolves weapon alias, then player name.
				const double now = gpGlobals->curtime;
				const char *alias = WeaponIDToAlias( GetWeaponID() );
				const char *playerName = owner->GetPlayerName();
#ifdef CLIENT_DLL
				const char *side = "client";
#else
				const char *side = "server";
#endif
				if ( haveSequence )
					DevMsg( "%s %s %s SendWeaponAnim(%s) @ %f\n", side, playerName, alias, activityName, now );
				else
					DevWarning( "%s %s %s SendWeaponAnim(missing %s) @ %f!\n", side, playerName, alias, activityName, now );
			}
		}
	}
	return BaseClass::SendWeaponAnim( mapped );
}

#pragma float_control( pop )

bool CTerrorGun::ShouldUnzoom() const
{
	CTerrorPlayer *owner = GetTerrorPlayerOwner();
	if ( !owner )
		return false;
	return !owner->GetGroundEntity() || owner->GetTongueOwner() ||
		owner->m_pounceAttacker.Get() || owner->m_jockeyAttacker.Get() ||
		owner->IsIncapacitated() || m_bInReload;
}

bool CTerrorGun::IsZoomed() const
{
	CTerrorPlayer *owner = GetTerrorPlayerOwner();
	return owner ? owner->IsZoomed() : false;
}

#pragma float_control( precise, on, push )
void CTerrorGun::ToggleZoom()
{
	CTerrorPlayer *owner = GetTerrorPlayerOwner();
	if ( !owner )
		return;
#ifdef CLIENT_DLL
	const float initialFov = owner->GetFOV();
#else
	const int initialFov = owner->GetFOV();
#endif
	const int defaultFov = owner->GetDefaultFOV();
	if ( initialFov == defaultFov )
	{
		if ( ShouldUnzoom() )
			return;
		owner->SetFOV( owner, GetZoomFOV(), 0.3f, 0 );
	}
	else
	{
#ifdef CLIENT_DLL
		const float fov = owner->GetFOV();
		if ( fov == GetZoomFOV() )
#else
		const int target = GetZoomFOV();
		if ( owner->GetFOV() == target )
#endif
			owner->SetFOV( owner, 0, 0.1f, 0 );
	}
#ifndef CLIENT_DLL
	owner->RecalculateMaxSpeed();
	EmitSound( "Default.Zoom" );
	IGameEvent *event = gameeventmanager->CreateEvent( "weapon_zoom" );
	if ( event )
	{
		event->SetInt( "userid", owner->GetUserID() );
		gameeventmanager->FireEvent( event );
	}
#endif
}
#pragma float_control( pop )

float CTerrorGun::GetMaxAutoAimDeflection()
{
	const double radians = static_cast< double >( GetTerrorWpnData().m_flMaxAutoAimDeflection1 ) *
		static_cast< double >( 0.017453292f );
	return static_cast< float >( cos( radians ) );
}

float CTerrorGun::GetMaxAutoAimRange() const
{
	return GetTerrorWpnData().m_flMaxAutoAimRange1;
}

float CTerrorGun::WeaponAutoAimScale()
{
	return GetTerrorWpnData().m_flWeaponAutoAimScale;
}

#ifndef CLIENT_DLL
bool CTerrorGun::AcquireDualWeapons()
{
	if ( m_hasDualWeapons || m_bInReload )
		return false;
	m_isDualWielding = true;
	m_hasDualWeapons = true;
	CTerrorPlayer *owner = GetTerrorPlayerOwner();
	if ( owner && owner->GetActiveWeapon() == this )
		SetViewModel();
	SetModel( GetViewModel( 0 ) );
	m_inInitialPickup = false;
	m_iClip1 = GetMaxClip1();
	if ( owner )
	{
		if ( owner->GetActiveWeapon() == this )
		{
			// The original preceding clear makes this arm unreachable.
			if ( m_inInitialPickup )
				Reload();
		}
		else
		{
			if ( owner->GetActiveWeapon() )
				owner->GetActiveWeapon()->Holster( NULL );
			owner->SetActiveWeapon( this );
			Deploy();
		}
		CSingleUserRecipientFilter filter( owner );
		for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			CBasePlayer *observer = UTIL_PlayerByIndex( i );
			if ( observer && !observer->IsAlive() && observer->GetObserverMode() == OBS_MODE_IN_EYE &&
				observer->GetObserverTarget() == owner )
				filter.AddRecipient( observer );
		}
		if ( filter.GetRecipientCount() )
			CBaseEntity::EmitSound( filter, owner->entindex(), "Player.PickupWeapon" );
	}
	return true;
}

bool CTerrorGun::RemoveDualWeapons( bool force )
{
	if ( !m_hasDualWeapons || ( !force && ( m_bInReload || m_flNextPrimaryAttack > gpGlobals->curtime ) ) )
		return false;
	m_isDualWielding = false;
	m_hasDualWeapons = false;
	SetViewModel();
	SetModel( GetViewModel( 0 ) );
	SendWeaponAnim( GetDrawActivity() );
	DelayGunAttack( 1.0f );
	m_iClip1 = m_iClip1 / 2;
	return true;
}

void CTerrorGun::UpdateDualState()
{
	if ( !m_hasDualWeapons || m_bInReload || m_flNextPrimaryAttack > gpGlobals->curtime )
		return;
	CTerrorPlayer *owner = GetTerrorPlayerOwner();
	if ( !owner || owner->GetActiveWeapon() != this )
		return;
	if ( IsDualWielding() && !owner->IsAbleToDualWield() )
	{
		m_isDualWielding = false;
		const int oldClip = m_iClip1;
		if ( oldClip > GetMaxClip1() )
		{
			m_nStoredSingleClip = oldClip / 2;
			m_iClip1 -= oldClip / 2;
		}
		SetViewModel();
		SetModel( GetViewModel( 0 ) );
		SendWeaponAnim( GetDeployActivity() );
		return;
	}
	if ( !IsDualWielding() && owner->IsAbleToDualWield() )
	{
		m_isDualWielding = true;
		if ( m_nStoredSingleClip > 0 )
		{
			const int stored = m_nStoredSingleClip;
			if ( stored + m_iClip1 <= GetMaxClip1() )
			{
				m_iClip1 += stored;
				m_nStoredSingleClip = 0;
			}
		}
		SetViewModel();
		SetModel( GetViewModel( 0 ) );
		SendWeaponAnim( GetDeployActivity() );
	}
}

void CTerrorGun::DelayGunAttack( float duration )
{
	CTerrorPlayer *owner = GetTerrorPlayerOwner();
	if ( owner )
		owner->SetNextAttack( gpGlobals->curtime + duration );
	m_flNextPrimaryAttack = gpGlobals->curtime + duration;
	m_flNextSecondaryAttack = gpGlobals->curtime + duration;
	SetWeaponIdleTime( gpGlobals->curtime + duration );
}
#endif

int CTerrorGun::GetMaxClip1() const
{
	int maximum = CBaseCombatWeapon::GetMaxClip1();
	if ( m_isDualWielding )
		maximum *= 2;
	return maximum;
}

#ifdef CLIENT_DLL
extern ConVar r_flashlightmuzzleflash;

void CTerrorGun::DrawCrosshair()
{
	if ( ::crosshair.GetInt() == 0 )
		return;
	HideCrosshair();
	CTerrorPlayer *player = PostProcessViewPlayer(); // Native251B50, IN_EYE only.
	if ( !player )
		return;
	const bool mounted = player->m_usingMountedWeapon;
	if ( !player->CanAttack() && !mounted )
		return;
	if ( !ShouldDrawCrosshair() && !mounted )
		return;
	if ( player->IsInVGuiInputMode() )
		return;
	if ( player->IsThirdPersonCameraActive() )
		return;
	if ( HasNativePickupState() )
		return;
	static ConVarRef ingameMenu( "gameui_ingame_menu_active" );
	if ( ingameMenu.GetInt() != 0 )
	{
		HideCrosshair();
		return;
	}
	CRadialMenu *radial = gViewPortInterface ?
		dynamic_cast< CRadialMenu * >( gViewPortInterface->FindPanelByName( "radialmenu" ) ) : NULL;
	if ( radial && radial->IsVisible() && !radial->IsFading() )
		return;
	if ( mounted )
	{
		static ConVarRef minigunSpread( "z_minigun_spread" );
		const float spread = minigunSpread.GetFloat();
		const float fov = player->GetFOV();
		if ( CHudTerrorCrosshair *hud = GetTerrorCrosshair() )
			hud->DrawGunCrosshair( 0.0f, spread * 0.5f, fov, false );
	}
	else
	{
		const float spread = m_flCurrentSpread;
		const CTerrorWeaponInfo &info = GetTerrorWpnData();
		const bool shotgun = GetCSWpnData().m_WeaponType == TERROR_WEAPONTYPE_SHOTGUN;
		const float fov = player->GetFOV();
		if ( CHudTerrorCrosshair *hud = GetTerrorCrosshair() )
			hud->DrawGunCrosshair( info.m_flPelletScatterYaw, spread, fov, shotgun );
	}
}

void CTerrorGun::OnPreDataChanged( DataUpdateType_t updateType )
{
	m_oldIsDualWielding = m_isDualWielding;
	BaseClass::OnPreDataChanged( updateType );
}

void CTerrorGun::PostDataUpdate( DataUpdateType_t updateType )
{
	// C30BEC0: the dual-weapon acquisition event precedes base PostDataUpdate.
	if ( m_oldIsDualWielding != m_isDualWielding && m_isDualWielding )
	{
		CTerrorPlayer *owner = GetTerrorPlayerOwner();
		if ( owner && owner->IsLocalPlayer() )
		{
			IGameEvent *event = gameeventmanager->CreateEvent( "weapon_pickup" );
			if ( event )
			{
				event->SetInt( "context", 0 );
				event->SetInt( "weaponid", GetWeaponID() );
				event->SetInt( "weaponslot", GetSlot() );
				event->SetInt( "dropped_by_infected", m_DroppedByInfectedGender );
				if ( m_DroppedByInfectedGender != 0 )
					m_DroppedByInfectedGender = 0;
				gameeventmanager->FireEventClientSide( event );
			}
		}
	}
	BaseClass::PostDataUpdate( updateType );
}

CStudioHdr *CTerrorGun::OnNewModel()
{
	CStudioHdr *studio = BaseClass::OnNewModel();
	m_iMuzzleAttachment[0] = LookupAttachment( "muzzle_flash" );
	m_iMuzzleAttachment[1] = LookupAttachment( "muzzle_flash_r" );
	m_iMuzzleAttachment[2] = LookupAttachment( "muzzle_flash_l" );
	m_iShellAttachment[0] = LookupAttachment( "shell" );
	m_iShellAttachment[1] = LookupAttachment( "shell_r" );
	m_iShellAttachment[2] = LookupAttachment( "shell_l" );
	return studio;
}

int CTerrorGun::GetTerrorMuzzleAttachment() const
{
	if ( !m_isDualWielding )
		return m_iMuzzleAttachment[0];
	return m_iMuzzleAttachment[ m_iClip1 % 2 == 1 ? 1 : 2 ];
}

int CTerrorGun::GetEjectBrassAttachment() const
{
	if ( !m_isDualWielding )
		return m_iShellAttachment[0];
	return m_iShellAttachment[ m_iClip1 % 2 == 1 ? 1 : 2 ];
}

void CTerrorGun::EjectBrass()
{
	const CCSWeaponInfo &info = GetCSWpnData();
	EjectParticleBrass( info.m_szEjectBrassEffect, GetEjectBrassAttachment() );
}

void CTerrorGun::NotifyTerrorMuzzleFlash()
{
	if ( !m_muzzleFlashTimer.HasStarted() || !( m_muzzleFlashTimer.GetElapsedTime() < 0.5f ) )
		m_nMuzzleFlashShots = 1;
	else
		m_nMuzzleFlashShots = static_cast< int >( static_cast< uint32 >( m_nMuzzleFlashShots ) + 1U );
	if ( m_bMuzzleSmoke )
	{
		C_BaseViewModel *viewModel = NULL;
		if ( GetTerrorPlayerOwner() )
			viewModel = GetTerrorPlayerOwner()->GetViewModel( 0 );
		StopParticleEffects( this );
		StopParticleEffects( viewModel );
	}
	m_muzzleFlashTimer.Start();
	m_bMuzzleSmoke = false;
	SetNextClientThink( 0.5f ); // Native absolute-time bug.
}

void CTerrorGun::ClientThink()
{
	if ( !m_muzzleFlashTimer.HasStarted() || !( m_muzzleFlashTimer.GetElapsedTime() >= 0.5f ) || m_bMuzzleSmoke )
	{
		SetNextClientThink( 0.1f );
		return;
	}
	if ( m_nMuzzleFlashShots >= 5 )
	{
		C_BasePlayer *local = C_BasePlayer::GetLocalPlayer();
		bool firstPerson = false;
		if ( GetTerrorPlayerOwner() == local )
			firstPerson = !input->CAM_IsThirdPerson();
		// Native observer target is THIS GUN, not its owner. Preserve that bug.
		if ( ( ( local && local->GetObserverMode() == OBS_MODE_IN_EYE && local->GetObserverTarget() == this ) ||
			firstPerson ) && GetTerrorPlayerOwner() )
		{
			C_BaseViewModel *viewModel = GetTerrorPlayerOwner()->GetViewModel( 0 );
			DispatchParticleEffect( "weapon_muzzle_smoke", PATTACH_POINT_FOLLOW, viewModel, 1, false, 0 );
			if ( m_isDualWielding )
				DispatchParticleEffect( "weapon_muzzle_smoke", PATTACH_POINT_FOLLOW, viewModel, 2, false, 0 );
		}
		else
			DispatchParticleEffect( "weapon_muzzle_smoke", PATTACH_POINT_FOLLOW, this, GetTerrorMuzzleAttachment(), false, 0 );
		m_bMuzzleSmoke = true;
	}
	SetNextClientThink( 9999.0f );
}

#pragma float_control( precise, on, push )
// C2589C0: the actual actor countdown, including wide changed-value comparison.
static void DelayLocalGunShotSound( CTerrorPlayer *player )
{
	CountdownTimer &timer = player->m_clientMuzzleFlashTimer;
	const float duration = 0.1f;
	const double deadline = static_cast< double >( gpGlobals->curtime ) + static_cast< double >( duration );
	if ( timer.m_timestamp.Get() != deadline )
		timer.m_timestamp.GetForModify() = static_cast< float >( deadline );
	if ( timer.m_duration.Get() != duration )
		timer.m_duration.GetForModify() = duration;
}

static bool IsLocalGunShotSoundSuppressed( CTerrorPlayer *player )
{
	return player->m_clientMuzzleFlashTimer.m_timestamp.Get() > 0.0f &&
		gpGlobals->curtime < player->m_clientMuzzleFlashTimer.m_timestamp.Get();
}

// C2F30F0: the actual shotgun sound path reached by the viewmodel event.
static void EmitGunViewmodelShotSound( CTerrorPlayer *player, int playerIndex, WeaponSound_t sound,
	const Vector &origin, const CTerrorWeaponInfo *info, float soundTime )
{
	if ( !info )
		return;
	const char *name = info->aShootSounds[sound];
	if ( !name || !name[0] )
		return;
	CBroadcastRecipientFilter filter;
	if ( !te->CanPredict() || info->m_WeaponType != TERROR_WEAPONTYPE_SHOTGUN || player->IsZoomed() )
		return;
	if ( player->IsPlayer() && player->IsLocalPlayer() && IsLocalGunShotSoundSuppressed( player ) )
		return;
	CBaseEntity::EmitSound( filter, playerIndex, name, &origin, soundTime );
	if ( player->IsPlayer() && player->IsLocalPlayer() )
		DelayLocalGunShotSound( player );
}

bool CTerrorGun::OnFireEvent( C_BaseViewModel *viewModel, const Vector &origin, const QAngle &angles,
	int event, const char *options )
{
	CTerrorViewModel *terrorViewModel = dynamic_cast< CTerrorViewModel * >( viewModel );
	if ( event == AE_MUZZLEFLASH )
	{
		CTerrorPlayer *owner = GetTerrorPlayerOwner();
		if ( owner )
		{
			const float fov = static_cast< float >( owner->GetFOV() );
			const int defaultFov = owner->GetDefaultFOV();
			if ( static_cast< double >( defaultFov ) > static_cast< double >( fov ) && HideViewModelWhenZoomed() )
				return true;
		}
		if ( !input->CAM_IsThirdPerson() )
		{
			if ( GetCSWpnData().m_WeaponType == TERROR_WEAPONTYPE_SHOTGUN &&
				owner && owner->IsPlayer() && owner->IsLocalPlayer() )
			{
				if ( !IsLocalGunShotSoundSuppressed( owner ) )
				{
					const CTerrorWeaponInfo *info = GetTerrorWeaponInfo( GetWeaponID() );
					const Vector &shootOrigin = owner->Weapon_ShootPosition();
					const int playerIndex = owner->entindex();
					EmitGunViewmodelShotSound( owner, playerIndex, SINGLE, shootOrigin, info, gpGlobals->curtime );
					DelayLocalGunShotSound( owner );
				}
			}
			int attachment = -1;
			if ( terrorViewModel )
				attachment = terrorViewModel->GetMuzzleFlashAttachment( m_isDualWielding, m_iClip1 % 2 == 1 );
			const char *effect = GetCSWpnData().m_szMuzzleFlashEffect1stPerson;
			if ( effect && Q_strlen( effect ) > 0 && attachment >= 0 )
				DispatchParticleEffect( effect, PATTACH_POINT_FOLLOW, viewModel, attachment, false, -1 );
			if ( r_flashlightmuzzleflash.GetBool() )
				TerrorViewmodelMuzzleFlash();
		}
		return true;
	}
	if ( event == AE_CL_CREATE_PARTICLE_BRASS )
	{
		const Vector &weaponOrigin = GetAbsOrigin();
		const Vector &viewOrigin = MainViewOrigin();
		const float y = viewOrigin.y - weaponOrigin.y;
		const float x = viewOrigin.x - weaponOrigin.x;
		const float z = viewOrigin.z - weaponOrigin.z;
		if ( ( y * y + x * x ) + z * z < 65536.0f )
		{
			char token[256];
			nexttoken( token, sizeof( token ), options, ' ' );
			char effect[256];
			Q_strncpy( effect, token, sizeof( effect ) );
			if ( terrorViewModel )
				viewModel->EjectParticleBrass( effect,
					terrorViewModel->GetEjectBrassAttachment( m_isDualWielding, m_iClip1 % 2 == 1 ) );
		}
		return true;
	}
	return false;
}

// C2FAA50 primary v93, not IClientThinkable::ClientThink.
bool CTerrorGun::Simulate()
{
	BaseClass::Simulate();
	UpdateWorldLaserSight();
	return true;
}

void CTerrorGun::UpdateOnRemove()
{
	BaseClass::UpdateOnRemove();
	if ( m_laserSightWorldmodelEffect )
	{
		ParticleProp()->StopEmission( m_laserSightWorldmodelEffect );
		m_laserSightWorldmodelEffect = NULL;
	}
}

void CTerrorGun::UpdateWorldLaserSight()
{
	if ( IsDormant() || GetOwner() || !HasUpgrade( TERROR_UPGRADE_LASER_SIGHT ) || m_iLaserSightAttachment <= 0 )
	{
		if ( m_laserSightWorldmodelEffect )
		{
			ParticleProp()->StopEmission( m_laserSightWorldmodelEffect );
			m_laserSightWorldmodelEffect = NULL;
		}
		return;
	}
	Vector origin;
	QAngle angles;
	GetAttachment( m_iLaserSightAttachment, origin, angles );
	Vector forward, right, up;
	AngleVectors( angles, &forward, &right, &up );
	Vector end;
	end.x = origin.x + up.x * 100.0f;
	end.y = origin.y + up.y * 100.0f;
	end.z = origin.z + up.z * 100.0f;
	trace_t trace;
	UTIL_TraceLine( origin, end, MASK_SHOT, this, COLLISION_GROUP_NONE, &trace );
	if ( !m_laserSightWorldmodelEffect )
	{
		m_laserSightWorldmodelEffect = ParticleProp()->Create(
			"weapon_laser_sight_worldmodel", PATTACH_POINT_FOLLOW, m_iLaserSightAttachment );
		if ( m_laserSightWorldmodelEffect )
		{
			ParticleProp()->AddControlPoint( m_laserSightWorldmodelEffect, 1, this, PATTACH_CUSTOMORIGIN );
			ParticleProp()->AddControlPoint( m_laserSightWorldmodelEffect, 2, this, PATTACH_CUSTOMORIGIN );
			m_laserSightWorldmodelEffect->SetControlPoint( 1, trace.startpos );
			m_laserSightWorldmodelEffect->SetControlPoint( 2, trace.endpos );
			m_laserSightWorldmodelEffect->SetControlPointForwardVector( 1, up );
		}
	}
	if ( m_laserSightWorldmodelEffect )
	{
		m_laserSightWorldmodelEffect->SetControlPoint( 1, trace.startpos );
		m_laserSightWorldmodelEffect->SetControlPoint( 2, trace.endpos );
		m_laserSightWorldmodelEffect->SetControlPointForwardVector( 1, up );
		Vector endRight, endUp;
		// C35FAC0 differs from the SDK's exact-zero test/float normalization.
		const Vector &normal = trace.plane.normal;
		if ( 1.0e-6 > fabs( static_cast< double >( normal.x ) ) &&
			1.0e-6 > fabs( static_cast< double >( normal.y ) ) )
		{
			endRight.x = 0.0f;
			endRight.y = -1.0f;
			endRight.z = 0.0f;
			endUp.x = -normal.z;
			endUp.y = 0.0f;
			endUp.z = 0.0f;
		}
		else
		{
			endRight.x = normal.y - normal.z * 0.0f;
			endRight.y = normal.z * 0.0f - normal.x;
			endRight.z = normal.x * 0.0f - normal.y * 0.0f;
			const float inverseRight = static_cast< float >( 1.0 /
				( sqrt( static_cast< double >( endRight.y ) * endRight.y +
					static_cast< double >( endRight.x ) * endRight.x +
					static_cast< double >( endRight.z ) * endRight.z ) +
					static_cast< double >( 1.1920928955078125e-7f ) ) );
			endRight.x *= inverseRight;
			endRight.y *= inverseRight;
			endRight.z *= inverseRight;
			endUp.x = normal.z * endRight.y - normal.y * endRight.z;
			endUp.y = normal.x * endRight.z - normal.z * endRight.x;
			endUp.z = normal.y * endRight.x - endRight.y * normal.x;
			const double inverseUp = 1.0 /
				( sqrt( static_cast< double >( endUp.x ) * endUp.x +
					static_cast< double >( endUp.y ) * endUp.y +
					static_cast< double >( endUp.z ) * endUp.z ) +
					static_cast< double >( 1.1920928955078125e-7f ) );
			const float storedInverseUp = static_cast< float >( inverseUp );
			endUp.x *= storedInverseUp;
			endUp.y *= storedInverseUp;
			endUp.z = static_cast< float >( inverseUp * endUp.z );
		}
		endRight.x *= -1.0f;
		endRight.y *= -1.0f;
		endRight.z *= -1.0f;
		m_laserSightWorldmodelEffect->SetControlPointOrientation( 2, endRight, endUp, trace.plane.normal );
	}
}
#pragma float_control( pop )
#endif

#ifndef CLIENT_DLL
BEGIN_DATADESC( CTerrorGun )
END_DATADESC()
#endif
