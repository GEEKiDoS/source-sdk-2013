//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/weapon_claw.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "effect_dispatch_data.h"
#include "activitylist.h"
#include "rumble_shared.h"
#ifdef CLIENT_DLL
#include "l4d2/player/c_terrorplayer.h"
#include "c_te_effect_dispatch.h"
#include "c_rumble.h"
#include "c_recipientfilter.h"
#include "l4d2/hud/hud_terror_crosshair.h"
#else
#include "l4d2/player/terrorplayer.h"
#include "l4d2/infected/common/infected.h"
#include "doors.h"
#include "BasePropDoor.h"
#include "l4d2/nav/terror_nav_area.h"
#include "te_effect_dispatch.h"
#endif
#ifdef L4D2
#include "precache_register.h"
#endif
#include "tier0/memdbgon.h"

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif

ConVar claw_swing_interval( "claw_swing_interval", "1", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar claw_swing_miss_interval( "claw_swing_miss_interval", "0.5", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar claw_swing_duration( "claw_swing_duration", "0.2", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar claw_range( "claw_range", "52", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
, "Range of the Claw weapon" );
ConVar claw_range_down( "claw_range_down", "70", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
, "Range of the Claw weapon when looking straight down, so it can reach your feet from your eyes." );
ConVar claw_force( "claw_force", "240", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
, "Force with witch the claw shoves other zombies away" );
#ifndef CLIENT_DLL
ConVar z_attack_movement_penalty( "z_attack_movement_penalty", "0.5", 
#if defined( GAME_DLL )
	0x00000006
#else
	0
#endif
 );
ConVar z_door_pound_damage( "z_door_pound_damage", "60", 
#if defined( GAME_DLL )
	0x00002006
#else
	FCVAR_REPLICATED
#endif
 );
#endif

IMPLEMENT_NETWORKCLASS_ALIASED( Claw, DT_WeaponClaw )
BEGIN_NETWORK_TABLE( CClaw, DT_WeaponClaw )
END_NETWORK_TABLE()
BEGIN_PREDICTION_DATA( CClaw )
END_PREDICTION_DATA()
#ifndef CLIENT_DLL
BEGIN_DATADESC( CClaw )
END_DATADESC()
#endif

void CClaw::Spawn()
{
	if ( IsPrecacheAllowed() )
		Precache();
	m_iPrimaryAmmoType = -1;
	BaseClass::Spawn();
}

void CClaw::Precache()
{
	BaseClass::Precache();
	PrecacheScriptSound( "Claw.Swing" );
	PrecacheScriptSound( "Claw.Hit" );
	PrecacheScriptSound( "Claw.HitFlesh" );
	PrecacheScriptSound( "Claw.Swing" );
	PrecacheScriptSound( "Claw.Hit" );
	PrecacheScriptSound( "Claw.HitFlesh" );
	PrecacheEffect( "KnifeSlash" );
}

void CClaw::PrimaryAttack()
{
	CTerrorPlayer *owner = GetTerrorPlayerOwner();
	if ( !owner || owner->GetPlayerMaxSpeed() < 10.0f )
		return;
	IGameEvent *event = gameeventmanager->CreateEvent( "weapon_fire" );
	if ( event )
	{
		event->SetInt( "userid", owner->GetUserID() );
		const char *weapon = GetClassname();
		if ( weapon && !Q_strncmp( weapon, "weapon_", 7 ) )
			weapon += 7;
		event->SetString( "weapon", weapon ? weapon : "" );
		event->SetInt( "weaponid", GetWeaponID() );
#ifdef CLIENT_DLL
		gameeventmanager->FireEventClientSide( event );
#else
		gameeventmanager->FireEvent( event );
#endif
	}
	Vector forward;
	AngleVectors( owner->EyeAngles(), &forward );
	float range = claw_range.GetFloat();
	// Preserve both zero products: NaN X/Y is not the same as just -Z.
	if ( ( forward.y + forward.x ) * 0.0f - forward.z * 1.0f > 0.95f )
		range = claw_range_down.GetFloat();
	Swing( claw_swing_interval.GetFloat(), claw_swing_duration.GetFloat(), range );
}

// S3CB800/810, C2F88B0/C0 are genuine empty weapon-idle/secondary slots.
void CClaw::SecondaryAttack() {}
void CClaw::WeaponIdle() {}

#ifdef CLIENT_DLL
// C2F88F0, inherited by all six non-Tank concrete claw classes.
void CClaw::DrawCrosshair()
{
	CHudTerrorCrosshair *crosshair = GetTerrorCrosshair();
	if ( crosshair )
		crosshair->SetCrosshairState( 2 );
}
#endif

void CClaw::OnSwing()
{
	CTerrorPlayer *owner = GetTerrorPlayerOwner();
	if ( !owner )
		return;
	SendWeaponAnim( ACT_VM_PRIMARYATTACK );
	owner->DoTerrorAnimationEvent( TERROR_ANIMEVENT_ATTACK, 0 );
#ifdef CLIENT_DLL
	RumbleEffect( 9, 0, RUMBLE_FLAG_RESTART );
#else
	owner->EmitSound( "Claw.Swing" );
#endif
}

void CClaw::OnSwingEnd( bool hitAny )
{
	if ( !hitAny )
	{
		const float interval = claw_swing_interval.GetFloat();
		const float missInterval = claw_swing_miss_interval.GetFloat();
		const float timestamp = m_attackTimer.m_timestamp;
		const double remaining = static_cast< double >( timestamp ) - gpGlobals->curtime;
		const float adjusted = static_cast< float >( remaining -
			( static_cast< double >( interval ) - missInterval ) );
		const double expiry = static_cast< double >( gpGlobals->curtime ) + adjusted;
		if ( m_attackTimer.m_timestamp.Get() != expiry )
			m_attackTimer.m_timestamp.GetForModify() = static_cast< float >( expiry );
		m_attackTimer.m_duration = adjusted;
	}
}

Vector CClaw::GetClawDamageForce( CBaseEntity *target, const Vector &direction ) const
{
	return direction;
}

#ifndef CLIENT_DLL
float CClaw::GetClawDamage( CTerrorPlayer *victim ) const
{
	return 0.0f; // Native non-creatable base S3CB820; every real claw leaf overrides.
}

void CClaw::OnClawHitPlayer( CTerrorPlayer *victim )
{
	CTerrorPlayer *owner = GetTerrorPlayerOwner();
	const char *sound = owner->GetTeamNumber() == TEAM_INFECTED && owner->GetZombieClass() == 6 ? "ChargerZombie.Smash" : "Claw.HitFlesh";
	owner->EmitSound( sound );
	victim->EmitSound( sound );
	if ( victim->GetTeamNumber() == TEAM_SURVIVOR || victim->GetTeamNumber() == TEAM_L4D1_SURVIVOR )
		owner->OnClawHitSurvivorCharacter( victim->GetSurvivorCharacter() );
}
#endif

bool CClaw::OnSwingHit( trace_t &trace, const Vector &direction, bool newHit )
{
	CTerrorPlayer *owner = GetTerrorPlayerOwner();
	if ( !owner || owner->IsGhost() || !trace.m_pEnt )
		return false;
	CBaseEntity *target = trace.m_pEnt;
#ifndef CLIENT_DLL
	if ( newHit )
	{
		float damage;
		CTerrorPlayer *victim = ToTerrorPlayer( target );
		CBaseCombatCharacter *combat = target->MyCombatCharacterPointer();
		if ( FClassnameIs( target, "func_door*" ) || FClassnameIs( target, "prop_door*" ) )
		{
			if ( CBaseDoor *door = dynamic_cast<CBaseDoor *>( target ) )
			{
				if ( !door->IsDoorLocked() )
				{
					door->Use( owner, owner, USE_TOGGLE, 0.0f );
					return false;
				}
			}
			else if ( CBasePropDoor *door = dynamic_cast<CBasePropDoor *>( target ) )
			{
				TerrorNavArea *area = ToTerrorNavArea( owner->GetLastKnownArea() );
				if ( !( area && area->HasSpawnAttributes( TERROR_NAV_CHECKPOINT ) ) && door->IsCheckpointDoor() &&
					( door->IsDoorLocked() || !door->IsUsableByTeam( TEAM_INFECTED ) ) )
				{
					IGameEvent *event = gameeventmanager->CreateEvent( "hit_safe_room" );
					if ( event )
					{
						event->SetInt( "userid", owner->GetUserID() );
						gameeventmanager->FireEvent( event );
					}
					door->PlayPoundSound();
					return false;
				}
				door->PlayPoundSound();
			}
			damage = z_door_pound_damage.GetFloat() * 5.0f;
		}
		else if ( victim )
		{
			if ( owner->GetZombieClass() == 8 &&
				( ( ( victim->GetTeamNumber() == TEAM_SURVIVOR || victim->GetTeamNumber() == TEAM_L4D1_SURVIVOR ) && victim->IsIncapacitated() ) ||
				  ( victim->GetTeamNumber() == TEAM_INFECTED && victim->GetZombieClass() == 7 ) ) )
				return false;
			damage = GetClawDamage( victim );
			owner->SetVelocityModifier( z_attack_movement_penalty.GetFloat() );
			OnClawHitPlayer( victim );
			if ( victim->GetTeamNumber() != TEAM_INFECTED || victim->GetZombieClass() == 8 )
			{
				CSingleUserRecipientFilter filter( owner );
				filter.MakeReliable();
				UserMessageBegin( filter, "Splatter" );
					WRITE_BYTE( 1 );
				MessageEnd();
			}
			else
			{
				Vector shove;
				owner->EyeVectors( &shove );
				VectorNormalize( shove );
				shove.z = shove.z > 0.1f ? shove.z : 0.1f;
				victim->SetShoveForce( shove * claw_force.GetFloat() );
			}
		}
		else if ( combat && combat->GetZombieClass() == 0 && static_cast<Infected *>( combat )->GetGender() != 17 )
		{
			damage = (float)combat->GetHealth();
			owner->EmitSound( "Claw.HitFlesh" );
		}
		else
		{
			static ConVarRef z_breakable_damage( "z_breakable_damage" );
			damage = ( FClassnameIs( target, "finale_trigger" ) || FClassnameIs( target, "trigger_finale" ) ) ?
				( owner->GetTeamNumber() == TEAM_INFECTED && owner->GetZombieClass() == 8 ?
					GetClawDamage( NULL ) : TerrorGameRules()->GetCommonAttackDamage( NULL ) ) : z_breakable_damage.GetFloat();
			owner->EmitSound( "Claw.Hit" );
		}
		if ( damage >= 1.0f )
		{
			const Vector force = GetClawDamageForce( target, direction );
			CTakeDamageInfo info( owner, owner, damage, DMG_CLUB );
			CalculateMeleeDamageForce( &info, force, owner->Weapon_ShootPosition(), 1.0f );
			if ( target->GetNonShadowMass() > 400.0f )
			{
				info.AddDamageType( DMG_PREVENT_PHYSICS_FORCE );
				info.SetDamageForce( info.GetDamageForce() * 0.0f );
			}
			ClearMultiDamage();
			target->DispatchTraceAttack( info, direction, &trace );
			ApplyMultiDamage();
		}
	}
#endif
	if ( trace.surface.surfaceProps > 0 && trace.surface.surfaceProps <= 255 )
	{
		CEffectData effect;
		effect.m_vOrigin = trace.endpos;
		effect.m_vStart = trace.startpos;
		effect.m_nSurfaceProp = trace.surface.surfaceProps;
		effect.m_nDamageType = DMG_SLASH;
		effect.m_nHitBox = trace.hitbox;
		effect.m_vAngles = owner->GetAbsAngles();
		effect.m_fFlags = 1; // IMPACT_NODECAL: KnifeSlash applies the angled decal itself.
#ifdef CLIENT_DLL
		effect.m_hEntity = target->GetRefEHandle();
		if ( target->GetTeamNumber() == TEAM_SURVIVOR )
			RumbleEffect( 41, 0, RUMBLE_FLAG_RESTART );
#else
		effect.m_nEntIndex = target->entindex();
#endif
#ifdef CLIENT_DLL
		// C2F8C21..37 creates PAS recipients, then populates local recipients.
		CPASFilter filter( effect.m_vOrigin );
		filter.AddAllPlayers();
		DispatchEffect( "KnifeSlash", effect, filter );
#else
		DispatchEffect( "KnifeSlash", effect );
#endif
	}
	return false;
}
#ifdef _MSC_VER
#pragma float_control( pop )
#endif

#ifndef CLIENT_DLL
BEGIN_DATADESC( CHunterClaw )
END_DATADESC()
#endif
