//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/infected/special/ability_lunge.h"
#include "l4d2/terror_shareddefs.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "in_buttons.h"
#include "gamemovement.h"
#ifdef CLIENT_DLL
#include "l4d2/player/c_terrorplayer.h"
#else
#include "l4d2/player/terrorplayer.h"
#endif
#include "ipredictionsystem.h"
#include "tier0/memdbgon.h"

#ifdef CLIENT_DLL
LINK_ENTITY_TO_CLASS( ability_lunge, CLunge );
#endif

ConVar z_lunge_interval( "z_lunge_interval", "0.1", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar z_pounce_crouch_delay( "z_pounce_crouch_delay", "1", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar z_lunge_power( "z_lunge_power", "600", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar z_lunge_reflect( "z_lunge_reflect", "0", 
#if defined( CLIENT_DLL )
	0x0000200A
#elif defined( GAME_DLL )
	0x00002006
#else
	FCVAR_REPLICATED
#endif
, "Reflects wall-kick lunges" );
ConVar z_lunge_cooldown( "z_lunge_cooldown", "0.1", 
#if defined( CLIENT_DLL )
	0x0000200A
#elif defined( GAME_DLL )
	0x00002006
#else
	FCVAR_REPLICATED
#endif
, "Cooldown after lunge where zombies can't attack" );
#ifndef CLIENT_DLL
ConVar z_pounce_door_damage( "z_pounce_door_damage", "500", 
#if defined( GAME_DLL )
	0x00004004
#else
	FCVAR_CHEAT
#endif
 );
LINK_ENTITY_TO_CLASS( ability_lunge, CLunge );
EXTERN_SEND_TABLE( DT_CountdownTimer );
#else
EXTERN_RECV_TABLE( DT_CountdownTimer );
#endif

IMPLEMENT_NETWORKCLASS_ALIASED( Lunge, DT_Lunge )
BEGIN_NETWORK_TABLE( CLunge, DT_Lunge )
#ifdef CLIENT_DLL
	RecvPropDataTable( RECVINFO_DT( m_lungeAgainTimer ), 0, &REFERENCE_RECV_TABLE( DT_CountdownTimer ) ),
	RecvPropBool( RECVINFO( m_isLunging ) ),
	RecvPropFloat( RECVINFO( m_lungeStartTime ) ),
	RecvPropVector( RECVINFO( m_queuedLunge ) ),
#else
	SendPropDataTable( SENDINFO_DT( m_lungeAgainTimer ), &REFERENCE_SEND_TABLE( DT_CountdownTimer ) ),
	SendPropBool( SENDINFO( m_isLunging ) ),
	SendPropFloat( SENDINFO( m_lungeStartTime ), 0, SPROP_NOSCALE ),
	SendPropVector( SENDINFO( m_queuedLunge ), -1, SPROP_NOSCALE ),
#endif
END_NETWORK_TABLE()
BEGIN_PREDICTION_DATA( CLunge )
#ifdef CLIENT_DLL
	DEFINE_PRED_FIELD( m_isLunging, FIELD_BOOLEAN, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_lungeStartTime, FIELD_FLOAT, FTYPEDESC_INSENDTABLE ),
	{ FIELD_EMBEDDED, "m_lungeAgainTimer", { (int)_hacky_datamap_offsetof( classNameTypedef, m_lungeAgainTimer ), 0 }, 1, FTYPEDESC_SAVE | FTYPEDESC_KEY, NULL, NULL, NULL, &CountdownTimer::m_PredMap },
	DEFINE_PRED_FIELD( m_queuedLunge, FIELD_VECTOR, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_interrupted, FIELD_BOOLEAN, FTYPEDESC_SAVE ),
#endif
END_PREDICTION_DATA()

CLunge::CLunge()
{
	m_isLunging = false;
	m_lungeStartTime = 0.0f;
	m_queuedLunge = vec3_origin;
	m_interrupted = false;
	m_touchedPlayer = false;
	m_damagedDoor = false;
}

void CLunge::SetOwner( CTerrorPlayer *owner )
{
	SetRechargeTime( 0.0f, 0.0f );
	m_isLunging = false;
	m_lungeAgainTimer.Invalidate();
	m_touchedPlayer = false;
	m_damagedDoor = false;
	m_interrupted = false;
	m_lungeStartTime = 0.0f;
	BaseClass::SetOwner( owner );
}

int CLunge::GetActivationButton() const { return IN_ATTACK; }
bool CLunge::IsWallKickAvailable() const { return !m_interrupted && gpGlobals->curtime < m_lungeAgainTimer.m_timestamp; }

bool CLunge::CanActivate()
{
	CTerrorPlayer *owner = GetOwner();
	if ( !owner || owner->IsGhost() || owner->m_pounceVictim.Get() ||
		( owner->m_nButtons & IN_ATTACK2 ) || IsActivationSuppressed() ) return false;
	if ( TerrorGameRules()->IsPlayerControlledZombies() && !owner->m_Local.m_bDucked )
	{
		IGameEvent *event = gameeventmanager->CreateEvent( "pounce_fail" );
		if ( event )
		{
			event->SetInt( "userid", owner->GetUserID() );
			gameeventmanager->FireEvent( event );
		}
		return false;
	}
	return IsWallKickAvailable() || ( !m_isLunging && BaseClass::CanActivate() );
}

bool CLunge::IsActive() const
{
#ifdef CLIENT_DLL
	return m_isLunging;
#else
	return m_isLunging || ( m_attackCooldown.m_timestamp > 0.0f && gpGlobals->curtime < m_attackCooldown.m_timestamp );
#endif
}

Vector *CLunge::GetQueuedMovement( bool markChanged )
{
	return markChanged ? &m_queuedLunge.GetForModify() : const_cast<Vector *>( &m_queuedLunge.Get() );
}

bool CLunge::ActivateAbility()
{
	if ( !IsWallKickAvailable() && !CanActivate() ) return false;
	CTerrorPlayer *owner = GetOwner();
#ifdef CLIENT_DLL
	if ( owner->m_holdingObject ) return false;
#else
	if ( owner->IsHoldingEntity( NULL ) ) return false;
#endif
	if ( gpGlobals->curtime < owner->m_stunTimer.m_timestamp ) return false;
	if ( !IsWallKickAvailable() && !owner->GetGroundEntity() && owner->GetMoveType() != MOVETYPE_LADDER ) return false;
	Vector direction;
	owner->EyeVectors( &direction );
	if ( IsWallKickAvailable() )
	{
		const float dot = DotProduct( owner->GetLadderNormal(), direction );
		if ( z_lunge_reflect.GetBool() )
		{
			if ( dot < 0.0f )
			{
				// S3656B2/C28D1E9 use the native clipped reflection, including
				// the component stop epsilon, before applying lunge power.
				Vector reflected;
				PhysicsClipVelocity( direction, owner->GetLadderNormal(), reflected, 2.0f );
				direction = reflected;
			}
		}
		else if ( dot < 0.2f ) return false;
	}
	Vector velocity = direction * z_lunge_power.GetFloat();
#ifndef CLIENT_DLL
	owner->BeginLunge();
#endif
	m_isLunging = true;
	m_lungeStartTime = gpGlobals->curtime;
	m_lungeAgainTimer.Invalidate();
	if ( owner->GetGroundEntity() )
	{
		const float speed = VectorNormalize( velocity );
		if ( velocity.z < 0.0f && velocity.z > -0.7f )
		{
			trace_t trace;
			UTIL_TraceLine( owner->EyePosition(), owner->EyePosition() + velocity * 5000.0f,
				0x0202440B, owner, COLLISION_GROUP_NONE, &trace );
			if ( trace.fraction > 0.0f && trace.fraction < 1.0f && trace.endpos.z >= owner->GetAbsOrigin().z - 20.0f ) velocity.z = 0.0f;
		}
		velocity *= speed;
#ifndef CLIENT_DLL
		if ( owner->m_isAttemptingToPounce && owner->GetActiveWeapon() ) owner->GetActiveWeapon()->SendWeaponAnim( TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_LUNGE ) );
#endif
	}
	SetRechargeTime( z_lunge_interval.GetFloat(), 0.0f );
	m_queuedLunge = velocity;
	m_touchedPlayer = false;
	m_damagedDoor = false;
	m_interrupted = false;
#ifndef CLIENT_DLL
	IGameEvent *event = gameeventmanager->CreateEvent( "ability_use" );
	if ( event )
	{
		event->SetInt( "userid", owner->GetUserID() );
		event->SetString( "ability", GetClassname() );
		event->SetInt( "context", owner->m_isAttemptingToPounce ? 1 : 0 );
		gameeventmanager->FireEvent( event );
	}
#endif
	BaseClass::ActivateAbility();
	return true;
}

void CLunge::Update()
{
	if ( !m_isLunging ) return;
	CTerrorPlayer *owner = GetOwner();
	if ( !owner ) return;
	const Vector &queued = m_queuedLunge.Get();
	if ( queued.x > -0.01f && queued.x < 0.01f && queued.y > -0.01f && queued.y < 0.01f &&
		queued.z > -0.01f && queued.z < 0.01f &&
		( owner->GetMoveType() == MOVETYPE_LADDER || ( owner->GetGroundEntity() && gpGlobals->curtime > m_lungeStartTime + 0.5f ) ) )
	{
		m_isLunging = false;
		m_lungeStartTime = 0.0f;
		SetRechargeTime( z_lunge_interval.GetFloat(), 0.0f );
		m_attackCooldown.Start( z_lunge_cooldown.GetFloat() );
#ifndef CLIENT_DLL
		if ( !owner->IsGhost() ) owner->EmitSound( owner->m_isAttemptingToPounce ? "HunterZombie.Pounce.Miss" : "HunterZombie.LungeLand" );
		owner->m_isAttemptingToPounce = false;
#endif
	}
}

void CLunge::StopActivation( float duration )
{
	if ( m_isLunging )
	{
		m_isLunging = false;
		m_lungeStartTime = 0.0f;
	}
	SuppressActivation( duration );
}

void CLunge::OnOwnerDamaged( const CTakeDamageInfo &info )
{
#ifndef CLIENT_DLL
	if ( GetOwner() && m_isLunging ) m_interrupted = true;
#endif
}

void CLunge::OnOwnerLanded()
{
	const float delay = z_pounce_crouch_delay.GetFloat();
	if ( delay > 0.0f && delay > GetActivationTimerRemaining() ) SetRechargeTime( delay, 0.0f );
}

void CLunge::OnTouch( CBaseEntity *other )
{
	CTerrorPlayer *owner = GetOwner();
	if ( !owner ) return;
	if ( other->entindex() != 0 || owner->GetGroundEntity() )
	{
		if ( other->IsPlayer() ) m_touchedPlayer = true;
	}
	else if ( !m_touchedPlayer ) m_lungeAgainTimer.Start( 0.5f );
}

bool CLunge::OnPlayerMoveCollision( CBaseEntity *other, const Vector &position,
	const Vector &direction, const trace_t &trace, CMoveData *move )
{
#ifndef CLIENT_DLL
	CTerrorPlayer *owner = GetOwner();
	if ( !owner || !owner->m_isAttemptingToPounce ) return false;
	if ( other && other->IsPlayer() && ( other->GetTeamNumber() == TEAM_SURVIVOR || other->GetTeamNumber() == TEAM_L4D1_SURVIVOR ) )
	{
		CTerrorPlayer *victim = ToTerrorPlayer( other );
		if ( !victim->IsHangingFromLedge() )
		{
			owner->OnPounceContact( victim );
			move->SetAbsOrigin( victim->GetAbsOrigin() );
			move->m_vecVelocity = victim->GetAbsVelocity();
		}
		return false;
	}
	// S103650A0..AA tests the impulse-use capability, not entity m_fFlags.
	if ( m_damagedDoor || !( other->ObjectCaps() & FCAP_IMPULSE_USE ) || !FClassnameIs( other, "prop_door*" ) ) return false;
	m_damagedDoor = true;
	CTakeDamageInfo info( owner, owner, z_pounce_door_damage.GetFloat(), DMG_CRUSH );
	// S365104 calls the explosive force helper: capped impulse and one RNG draw.
	CalculateExplosiveDamageForce( &info, direction, trace.endpos );
	CDisablePredictionFiltering disablePredictionFiltering;
	other->TakeDamage( info );
	return true;
#else
	return false;
#endif
}

#ifdef CLIENT_DLL
float CLunge::GetRechargeProgress() const
{
	CTerrorPlayer *owner = GetOwner();
	if ( !owner || owner->m_pounceVictim.Get() ) return 0.0f;
	const int activity = TerrorActivityToOriginal( owner->GetTerrorActivity() );
	if ( ( activity >= 621 && activity <= 636 ) || ( activity >= 765 && activity <= 768 ) ) return 0.0f;
	if ( m_isLunging ) return IsWallKickAvailable() ? 1.0f : 0.0f;
	if ( ( owner->m_Local.m_bDucked || owner->m_Local.m_bDucking ) && !owner->IsDuckUntilOnGround() &&
		( owner->m_nButtons & IN_DUCK ) ) return BaseClass::GetRechargeProgress();
	return 0.0f;
}
const char *CLunge::GetCooldownBackground() const { return "HUD/PZ_charge_bg"; }
const char *CLunge::GetCooldownForeground() const { return "HUD/PZ_charge_lunge"; }
#endif

#ifndef CLIENT_DLL
BEGIN_DATADESC( CLunge )
END_DATADESC()
#endif
