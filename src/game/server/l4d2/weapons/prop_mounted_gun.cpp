//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/prop_mounted_gun.h"
#include "l4d2/player/terrorplayer.h"
#include "ammodef.h"
#include "particle_parse.h"
#include "ipredictionsystem.h"
#include "igameevents.h"
#include "l4d2/entities/visibility_monitor.h"
#include "networkstringtable_gamedll.h"
#include "rumble_shared.h"
#include "in_buttons.h"
#include "tier0/memdbgon.h"

extern ConVar z_health;

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif

ConVar z_mounted_gun_fire_think_interval( "z_mounted_gun_fire_think_interval", "0.05", 0x00004004 );
ConVar mounted_gun_rate_of_fire( "mounted_gun_rate_of_fire", "8", 0x00004004 );
ConVar mounted_gun_overheat_time( "mounted_gun_overheat_time", "15", 0x00006004,
	"Mouted gun overheat time", true, 0.1f, true, 100.0f );
ConVar mounted_gun_cooldown_time( "mounted_gun_cooldown_time", "60", 0x00006004,
	"Mounted gun cooldown time", true, 0.1f, true, 100.0f );
ConVar mounted_gun_overheat_penalty_time( "mounted_gun_overheat_penalty_time", "60", 0x00004004 );
ConVar mounted_gun_mount_cooldown_time( "mounted_gun_mount_cooldown_time", "0.5", 0x00004004 );

LINK_ENTITY_TO_CLASS( prop_minigun, CPropMountedGun );
LINK_ENTITY_TO_CLASS( prop_mounted_machine_gun, CPropMountedGun );

IMPLEMENT_SERVERCLASS_ST( CPropMountedGun, DT_PropMountedGun )
	SendPropExclude( "DT_BaseAnimating", "m_flPoseParameter" ),
END_SEND_TABLE()

BEGIN_DATADESC( CPropMountedGun )
	DEFINE_KEYFIELD( m_maxYaw, FIELD_FLOAT, "MaxYaw" ),
	DEFINE_KEYFIELD( m_maxPitch, FIELD_FLOAT, "MaxPitch" ),
	DEFINE_KEYFIELD( m_minPitch, FIELD_FLOAT, "MinPitch" ),
	DEFINE_THINKFUNC( AngleThink ),
	DEFINE_THINKFUNC( FireThink ),
	DEFINE_OUTPUT( m_OnFireStart, "OnFireStart" ),
	DEFINE_OUTPUT( m_OnFireStop, "OnFireStop" ),
END_DATADESC()

CPropMountedGun::CPropMountedGun()
{
	m_shellAttachment = m_muzzleAttachment = 0;
}

// S:0x3DA790. The sound/effects are authoritative entity emissions, not
// duplicate local firing effects on C_PropMountedGun.
void CPropMountedGun::Precache()
{
	PrecacheScriptSound( "50cal.fire" );
	PrecacheScriptSound( "Default.ClipEmpty_Rifle" );
	PrecacheModel( "models/w_models/weapons/50cal.mdl" );
	g_pStringTableEffectDispatch->AddString( IsServer(), "ParticleTracer" );
	PrecacheParticleSystem( "weapon_tracers" );
	PrecacheParticleSystem( "weapon_muzzle_flash_50cal" );
	PrecacheParticleSystem( "weapon_shell_casing_50cal" );
	PrecacheParticleSystem( "weapon_tracers_50cal" );
	PrecacheParticleSystem( "weapon_tracers_50cal_low" );
	BaseClass::Precache();
}

void CPropMountedGun::Spawn()
{
	Precache();
	BaseClass::Spawn();
	// S:0x3DC320 canonicalizes even the mounted_machine_gun factory name.
	SetClassname( "prop_minigun" );
	SetModel( "models/w_models/weapons/50cal.mdl" );
	InitializeMountedWeapon();
	SetNextThink( gpGlobals->curtime + 0.1f );
	SetThink( &CPropMountedGun::AngleThink );
	VisibilityMonitor_AddEntity( this, 360.0f, NULL, NULL );
	// Original restarts the timer's existing (initially zero) duration here.
	m_mountCooldown.Start( m_mountCooldown.GetCountdownDuration() );
}


void CPropMountedGun::AngleThink()
{
	SetNextThink( gpGlobals->curtime + 0.1f );
	const float interval = gpGlobals->curtime - m_lastAngleThink;
	m_lastAngleThink = gpGlobals->curtime;
	UpdateHeat( interval );
	UpdateParentAngles();
}

void CPropMountedGun::FireOverheatedEvent()
{
	CTerrorPlayer *pPlayer = m_owner.Get();
	if ( !pPlayer )
		return;
	IGameEvent *pEvent = gameeventmanager->CreateEvent( "mounted_gun_overheated" );
	if ( pEvent )
	{
		pEvent->SetInt( "userid", pPlayer->GetUserID() );
		gameeventmanager->FireEvent( pEvent );
	}
}

// S:0x3DAD50. Bot heat is bypassed at this actual owner branch. Overheat
// clears at 1 - penalty/cooldown, not necessarily at zero or after a timer.
void CPropMountedGun::UpdateHeat( float interval )
{
	if ( m_owner.Get() && m_owner->IsBot() )
		return;
	float heat;
	if ( m_firing )
	{
		heat = m_heat + interval / mounted_gun_overheat_time.GetFloat();
		if ( heat > 1.0f )
			heat = 1.0f;
	}
	else
	{
		heat = m_heat - interval / mounted_gun_cooldown_time.GetFloat();
		if ( heat < 0.0f )
			heat = 0.0f;
	}
	m_heat = heat;
	if ( m_heat == 1.0f )
	{
		m_overheated = true;
		m_firing = false;
		OnFireStop();
		FireOverheatedEvent();
	}
	else if ( m_overheated && m_heat <= 1.0f - mounted_gun_overheat_penalty_time.GetFloat() / mounted_gun_cooldown_time.GetFloat() )
	{
		m_overheated = false;
	}
}

void CPropMountedGun::OnFireStart()
{
	m_OnFireStart.FireOutput( this, this );
	if ( m_owner.Get() )
		m_owner->RumbleEffect( RUMBLE_MOUNTED_GUN, 0, RUMBLE_FLAG_LOOP );
}

void CPropMountedGun::OnFireStop()
{
	m_OnFireStop.FireOutput( this, this );
	if ( m_owner.Get() )
		m_owner->RumbleEffect( RUMBLE_MOUNTED_GUN, 0, RUMBLE_FLAG_STOP );
}

// S:0x3DBD50. PlayerUse selects/reaches the target; the prop owns its native
// occupancy, weapon holster/deploy and fire-context lifetime.
void CPropMountedGun::Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	if ( m_owner.Get() && m_owner.Get() != pActivator )
		return;
	CTerrorPlayer *pPlayer = ToTerrorPlayer( pActivator );
	if ( pPlayer )
	{
		if ( pPlayer->GetUseEntity() == this )
		{
			if ( useType != USE_SET )
			{
				pPlayer->SetUseEntity( NULL );
				pPlayer->OnUseEntityChanged();
				if ( m_firing )
					OnFireStop();
				m_owner = NULL;
				RestoreUserWeapon( pPlayer );
			}
			return;
		}
		if ( !OnControls( pPlayer ) )
			return;
		pPlayer->SetUseEntity( this );
		pPlayer->OnUseEntityChanged();
		m_owner = pPlayer;
		if ( pPlayer->GetActiveWeapon() )
			pPlayer->GetActiveWeapon()->Holster( NULL );
		SetContextThink( &CPropMountedGun::FireThink, gpGlobals->curtime + z_mounted_gun_fire_think_interval.GetFloat(), "FireContext" );
		m_lastFireTime = gpGlobals->curtime;
		IGameEvent *pEvent = gameeventmanager->CreateEvent( "mounted_gun_start" );
		if ( pEvent )
		{
			pEvent->SetInt( "userid", pPlayer->GetUserID() );
			pEvent->SetInt( "subject", entindex() );
			gameeventmanager->FireEvent( pEvent );
		}
		m_mountCooldown.Start( mounted_gun_mount_cooldown_time.GetFloat() );
		if ( m_overheated )
			FireOverheatedEvent();
	}
	m_shellAttachment = LookupAttachment( "shell" );
	m_muzzleAttachment = LookupAttachment( "muzzle_flash" );
}

void CPropMountedGun::FireThink()
{
	const bool wasFiring = m_firing;
	CTerrorPlayer *pPlayer = m_owner.Get();
	if ( !pPlayer || pPlayer->GetUseEntity() != this )
	{
		if ( wasFiring )
			OnFireStop();
		m_firing = false;
		m_owner = NULL;
		return;
	}

	SetContextThink( &CPropMountedGun::FireThink, gpGlobals->curtime + z_mounted_gun_fire_think_interval.GetFloat(), "FireContext" );
	UpdateMountedPose( z_mounted_gun_fire_think_interval.GetFloat() );
	if ( !( pPlayer->m_nButtons & IN_ATTACK ) )
		m_firing = false;
	if ( ( pPlayer->m_nButtons & IN_ATTACK ) && pPlayer->CanAttack() && !m_overheated )
	{
		m_firing = true;
		if ( gpGlobals->curtime - m_lastFireTime >= 1.0f / mounted_gun_rate_of_fire.GetFloat() )
		{
			pPlayer->NoteTerrorWeaponFired();
			m_lastFireTime = gpGlobals->curtime;
			Vector origin = GetAbsOrigin();
			Vector direction;
			QAngle angles;
			const int attachment = LookupAttachment( "muzzle_flash" );
			bool hasAim = false;
			if ( attachment >= 0 )
			{
				hasAim = GetAttachment( attachment, origin, angles );
				const float theta = RandomFloat( 0.0f, 2.0f * M_PI );
				const float radius = RandomFloat( 0.0f, 1.0f );
				if ( hasAim )
				{
					angles.y = static_cast< float >( cos( static_cast< double >( theta ) ) * radius + angles.y );
					angles.x = static_cast< float >( sin( static_cast< double >( theta ) ) * radius + angles.x );
					AngleVectors( angles, &direction );
				}
			}
			IGameEvent *pEvent = gameeventmanager->CreateEvent( "weapon_fire" );
			if ( pEvent )
			{
				pEvent->SetInt( "userid", pPlayer->GetUserID() );
				pEvent->SetString( "weapon", GetClassname() );
				// This original event literal is NOT the machinegun weapon alias ID.
				pEvent->SetInt( "weaponid", 54 );
				gameeventmanager->FireEvent( pEvent );
			}
			pPlayer->SetNoiseLevel( 300.0f );
			CDisablePredictionFiltering disablePredictionFiltering;
			DoMuzzleFlash();
			// A failed attachment leaves native aim undefined. The private
			// validity argument skips only aim-dependent work in Fire.
			Fire( pPlayer, origin, direction, hasAim );
		}
	}
	else
	{
		if ( m_mountCooldown.IsElapsed() && ( pPlayer->m_nButtons & ( IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT | IN_ATTACK2 ) ) )
		{
			pPlayer->SetUseEntity( NULL );
			pPlayer->OnUseEntityChanged();
			pPlayer->RumbleEffect( RUMBLE_MOUNTED_GUN, 0, RUMBLE_FLAG_STOP );
			m_owner = NULL;
			RestoreUserWeapon( pPlayer );
		}
		m_firing = false;
	}
	if ( m_firing != wasFiring )
	{
		if ( m_firing )
			OnFireStart();
		else
			OnFireStop();
	}
}

// S:0x3DB320 uses its own three-unit swept hull, one hit and z_health damage.
// It is not a carried rifle or FireTerrorBullets alias and has no penetration.
void CPropMountedGun::Fire( CTerrorPlayer *pPlayer, const Vector &origin, const Vector &direction, bool hasAim )
{
	if ( m_overheated )
	{
		EmitSound( "Default.ClipEmpty_Rifle" );
		return;
	}
	EmitSound( "50cal.fire" );
	UTIL_ScreenShake( pPlayer->WorldSpaceCenter(), 1.0f, 0.5f, 0.3f, 120.0f, SHAKE_START, false );
	CTraceFilterSimpleList filter( COLLISION_GROUP_NONE );
	filter.AddEntityToIgnore( this );
	Vector end;
	const Vector minimum( -3, -3, -3 );
	const Vector maximum( 3, 3, 3 );
	trace_t trace;
	trace.m_pEnt = NULL;
	if ( hasAim )
	{
		end = origin + direction * 16384.0f;
		UTIL_TraceHull( origin, end, minimum, maximum, MASK_SHOT, &filter, &trace );
	}
	DispatchParticleEffect( "weapon_muzzle_flash_50cal", PATTACH_POINT_FOLLOW, this, m_muzzleAttachment );
	DispatchParticleEffect( "weapon_shell_casing_50cal", PATTACH_POINT, this, m_shellAttachment );
	if ( !hasAim || !trace.m_pEnt )
		return;

	CTerrorPlayer *pVictim = ToTerrorPlayer( trace.m_pEnt );
	if ( pVictim && ( pVictim->GetTeamNumber() == TEAM_SURVIVOR || pVictim->GetTeamNumber() == TEAM_L4D1_SURVIVOR ) &&
		( pVictim->m_pounceAttacker.Get() || pVictim->m_pummelAttacker.Get() || pVictim->m_tongueOwner.Get() || pVictim->m_jockeyAttacker.Get() ) )
	{
		filter.AddEntityToIgnore( pVictim );
		UTIL_TraceHull( origin, end, minimum, maximum, MASK_SHOT, &filter, &trace );
	}
	UTIL_ImpactTrace( &trace, static_cast<int>( 0x80000002u ) );
	DispatchParticleEffect( "weapon_tracers_50cal", trace.startpos, trace.endpos, vec3_angle );
	ClearMultiDamage();
	const int ammoType = GetAmmoDef()->Index( "AMMO_TYPE_MINIGUN" );
	CTakeDamageInfo damage( this, pPlayer, z_health.GetFloat(), GetAmmoDef()->DamageType( ammoType ) );
	CalculateBulletDamageForce( &damage, ammoType, direction, trace.endpos, 1.0f );
	damage.SetAmmoType( ammoType );
	// A second trace can miss after skipping the pinned survivor. Keep the
	// native effects/force/multidamage phases, skipping only the NULL dispatch.
	if ( trace.m_pEnt )
		trace.m_pEnt->DispatchTraceAttack( damage, direction, &trace );
	ApplyMultiDamage();
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif
