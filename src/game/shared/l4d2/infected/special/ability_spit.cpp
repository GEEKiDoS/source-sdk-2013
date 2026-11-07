//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/infected/special/ability_spit.h"
#include "in_buttons.h"
#include "activitylist.h"
#include "rumble_shared.h"
#ifdef CLIENT_DLL
#include "l4d2/player/c_terrorplayer.h"
#else
#include "l4d2/player/terrorplayer.h"
#include "l4d2/infected/special/spitter.h"
#include "l4d2/infected/special/spitter_projectile.h"
#include "NextBot/NextBotUtil.h"
#endif
#include "tier0/memdbgon.h"

#ifdef CLIENT_DLL
LINK_ENTITY_TO_CLASS( ability_spit, CSpitAbility );
#endif

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif

ConVar z_spit_interval( "z_spit_interval", "20", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar z_spit_range( "z_spit_range", "900", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar z_spit_latency( "z_spit_latency", "0.3", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
#ifndef CLIENT_DLL
ConVar z_spit_velocity( "z_spit_velocity", "900", 
#if defined( GAME_DLL )
	0x00004004
#else
	FCVAR_CHEAT
#endif
 );
LINK_ENTITY_TO_CLASS( ability_spit, CSpitAbility );
#endif
IMPLEMENT_NETWORKCLASS_ALIASED( SpitAbility, DT_SpitAbility )
BEGIN_NETWORK_TABLE( CSpitAbility, DT_SpitAbility )
#ifdef CLIENT_DLL
	RecvPropBool( RECVINFO( m_bHasBeenActivated ) ),
#else
	SendPropBool( SENDINFO( m_bHasBeenActivated ) ),
#endif
END_NETWORK_TABLE()
BEGIN_PREDICTION_DATA( CSpitAbility )
#ifdef CLIENT_DLL
	DEFINE_PRED_FIELD( m_bHasBeenActivated, FIELD_BOOLEAN, FTYPEDESC_INSENDTABLE ),
#endif
END_PREDICTION_DATA()

CSpitAbility::CSpitAbility()
{
	m_bHasBeenActivated = false;
}

void CSpitAbility::SetOwner( CTerrorPlayer *owner )
{
	BaseClass::SetOwner( owner );
	SetRechargeTime( 0.5f );
}

bool CSpitAbility::CanActivate()
{
	CTerrorPlayer *owner = GetOwner();
	return !m_bHasBeenActivated && ( !owner || !owner->IsGhost() ) && BaseClass::CanActivate();
}

bool CSpitAbility::ActivateAbility()
{
	CTerrorPlayer *owner = GetOwner();
	if ( !owner || !owner->IsPlayer() )
		return false;
	m_bHasBeenActivated = true;
#ifndef CLIENT_DLL
	IGameEvent *event = gameeventmanager->CreateEvent( "ability_use" );
	if ( event )
	{
		event->SetInt( "userid", owner->GetUserID() );
		event->SetString( "ability", GetClassname() );
		event->SetInt( "context", 0 );
		gameeventmanager->FireEvent( event );
	}
#endif
	CBaseCombatWeapon *weapon = owner->GetActiveWeapon();
	if ( weapon )
		weapon->SendWeaponAnim( (Activity)ActivityList_IndexForName( "ACT_VM_VOMIT" ) );
	SetRechargeTime( 3600.0f );
	m_spitTimer.Start( z_spit_latency.GetFloat() );
	BaseClass::ActivateAbility();
	owner->DoTerrorAnimationEvent( TERROR_ANIMEVENT_SPIT, 0 );
#ifndef CLIENT_DLL
	owner->EmitInfectedSound( "SpitterZombie.Spit", 0.0f, 0.0f );
#endif
	return true;
}

int CSpitAbility::GetActivationButton() const { return IN_ATTACK; }
bool CSpitAbility::IsActive() const { return m_bHasBeenActivated; }
bool CSpitAbility::CanActivateOnGround() const { return true; } // S366290.

#ifdef CLIENT_DLL
bool CSpitAbility::IsReadyToActivate()
{
	CTerrorPlayer *owner = GetOwner();
	Vector forward;
	AngleVectors( owner->EyeAngles(), &forward );
	trace_t trace;
	UTIL_TraceLine( owner->EyePosition(), owner->EyePosition() + forward * z_spit_range.GetFloat(),
		0x46004003, owner, COLLISION_GROUP_NONE, &trace );
	return trace.fraction < 1.0f;
}
#endif

#ifndef CLIENT_DLL
Vector CSpitAbility::GetSpitPosition() const
{
	CTerrorPlayer *owner = GetOwner();
	Vector start = owner->EyePosition();
	Vector forward;
	AngleVectors( owner->EyeAngles(), &forward );
	trace_t trace;
	UTIL_TraceLine( start, start + forward * 24.0f, 0x46004003, owner, COLLISION_GROUP_NONE, &trace );
	if ( trace.fraction == 1.0f )
		start = trace.endpos;
	return start;
}

static CTerrorPlayer *FindSpitWarningTarget( const Vector &position )
{
	CTerrorPlayer *nearest = NULL;
	float nearestRangeSquared = 100000000.0f;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( !player || !player->entindex() || !player->IsConnected() || !player->IsAlive() ||
			player->GetTeamNumber() != TEAM_SURVIVOR || player->IsIncapacitated() ||
			player->IsBeingPounced() || player->IsTongueVictim() || player->IsPummelVictim() || player->IsJockeyVictim() )
			continue;
		float rangeSquared = ( player->WorldSpaceCenter() - position ).LengthSqr();
		if ( nearestRangeSquared > rangeSquared )
		{
			NextBotTraceFilterIgnoreActors filter( NULL, COLLISION_GROUP_NONE );
			trace_t trace;
			UTIL_TraceLine( position, player->WorldSpaceCenter(), 0x02006041, &filter, &trace );
			if ( trace.fraction >= 1.0f && !trace.startsolid )
			{
				nearestRangeSquared = rangeSquared;
				nearest = player;
			}
		}
	}
	return nearest;
}
#endif

void CSpitAbility::Update()
{
	CTerrorPlayer *owner = GetOwner();
	if ( !m_bHasBeenActivated || !owner )
		return;
	if ( m_spitTimer.m_timestamp > 0.0f )
		owner->SetStamina( 3000.0f );
#ifndef CLIENT_DLL
	if ( m_spitTimer.m_timestamp > 0.0f &&
		static_cast< double >( gpGlobals->curtime ) >= m_spitTimer.m_timestamp )
	{
		CTerrorPlayer *warningTarget = FindSpitWarningTarget( owner->GetAbsOrigin() );
		if ( warningTarget && owner->IsLineOfSightClear( warningTarget->WorldSpaceCenter() ) )
			warningTarget->SpeakIfAllowed( "WarnSpitterIncoming" );
		m_spitTimer.Invalidate();
		const float speed = z_spit_velocity.GetFloat();
		Vector start = GetSpitPosition();
		Vector direction;
		if ( owner->IsBot() )
		{
			Spitter *spitter = dynamic_cast< Spitter * >( owner );
			if ( spitter && spitter->GetSpitTarget() )
			{
				CTerrorPlayer *target = spitter->GetSpitTarget();
				Vector aim = target->WorldSpaceCenter();
				trace_t trace;
				UTIL_TraceLine( start, target->WorldSpaceCenter(), 0x46004003,
					owner, COLLISION_GROUP_NONE, &trace );
				if ( trace.fraction < 1.0f && trace.m_pEnt != target )
					aim.z += 50.0f;
				direction = aim - start;
				VectorNormalize( direction );
				float pitch = RandomFloat( -10.0f, 10.0f );
				float yaw = RandomFloat( -10.0f, 10.0f );
				CSpitterProjectile::Create( start, owner->GetAbsAngles(), direction * speed,
					AngularImpulse( pitch, yaw, 0.0f ), owner );
			}
		}
		else
		{
			Vector forward;
			AngleVectors( owner->EyeAngles(), &forward );
			trace_t trace;
			UTIL_TraceLine( owner->EyePosition(), owner->EyePosition() + forward * 16384.0f,
				0x46004003, owner, COLLISION_GROUP_NONE, &trace );
			direction = trace.endpos - start;
			VectorNormalize( direction );
			float pitch = RandomFloat( -10.0f, 10.0f );
			float yaw = RandomFloat( -10.0f, 10.0f );
			CSpitterProjectile::Create( start, owner->GetAbsAngles(), direction * speed,
				AngularImpulse( pitch, yaw, 0.0f ), owner );
			owner->RumbleEffect( 40, 0, RUMBLE_FLAG_RESTART );
		}
	}
	else if ( !( m_spitTimer.m_timestamp > 0.0f ) && owner->IsSequenceFinished() )
	{
		SetRechargeTime( z_spit_interval.GetFloat() );
		m_bHasBeenActivated = false;
	}
#endif
}

void CSpitAbility::StopActivation( float duration )
{
	m_bHasBeenActivated = false;
	m_spitTimer.Invalidate();
	SuppressActivation( duration );
	SetRechargeTime( z_spit_interval.GetFloat() );
}

#ifdef CLIENT_DLL
const char *CSpitAbility::GetCooldownBackground() const { return "HUD/PZ_charge_bg"; }
const char *CSpitAbility::GetCooldownForeground() const { return "HUD/PZ_charge_spitter"; }
#endif
#ifdef _MSC_VER
#pragma float_control( pop )
#endif

#ifndef CLIENT_DLL
BEGIN_DATADESC( CSpitAbility )
END_DATADESC()
#endif
