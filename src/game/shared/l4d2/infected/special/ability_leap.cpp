//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/infected/special/ability_leap.h"
#include "l4d2/terror_shareddefs.h"
#include "in_buttons.h"
#ifdef CLIENT_DLL
#include "l4d2/player/c_terrorplayer.h"
#else
#include "l4d2/player/terrorplayer.h"
#endif
#include "tier0/memdbgon.h"

#ifdef CLIENT_DLL
LINK_ENTITY_TO_CLASS( ability_leap, CLeap );
#endif

ConVar z_leap_interval( "z_leap_interval", "0.5", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar z_leap_interval_post_ride( "z_leap_interval_post_ride", "6.0", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar z_leap_interval_post_incap( "z_leap_interval_post_incap", "30.0", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar z_leap_power( "z_leap_power", "400", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar z_leap_cooldown( "z_leap_cooldown", "0.0", 
#if defined( CLIENT_DLL )
	0x0000200A
#elif defined( GAME_DLL )
	0x00002006
#else
	FCVAR_REPLICATED
#endif
, "Cooldown after leap where zombies can't attack" );
ConVar z_leap_max_distance( "z_leap_max_distance", "200.0", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
#ifndef CLIENT_DLL
ConVar z_leap_attach_distance( "z_leap_attach_distance", "60.0", 
#if defined( GAME_DLL )
	0x00004004
#else
	FCVAR_CHEAT
#endif
 );
ConVar z_leap_force_attach_distance( "z_leap_force_attach_distance", "40.0", 
#if defined( GAME_DLL )
	0x00004004
#else
	FCVAR_CHEAT
#endif
 );
ConVar z_leap_far_attach_delay( "z_leap_far_attach_delay", "0.4", 
#if defined( GAME_DLL )
	0x00004004
#else
	FCVAR_CHEAT
#endif
 );
LINK_ENTITY_TO_CLASS( ability_leap, CLeap );
EXTERN_SEND_TABLE( DT_CountdownTimer );
#else
EXTERN_RECV_TABLE( DT_CountdownTimer );
#endif

IMPLEMENT_NETWORKCLASS_ALIASED( Leap, DT_Leap )
BEGIN_NETWORK_TABLE( CLeap, DT_Leap )
#ifdef CLIENT_DLL
	RecvPropDataTable( RECVINFO_DT( m_leapAgainTimer ), 0, &REFERENCE_RECV_TABLE( DT_CountdownTimer ) ),
	RecvPropBool( RECVINFO( m_isLeaping ) ),
	RecvPropFloat( RECVINFO( m_leapStartTime ) ),
	RecvPropVector( RECVINFO( m_queuedLeap ) ),
#else
	SendPropDataTable( SENDINFO_DT( m_leapAgainTimer ), &REFERENCE_SEND_TABLE( DT_CountdownTimer ) ),
	SendPropBool( SENDINFO( m_isLeaping ) ),
	SendPropFloat( SENDINFO( m_leapStartTime ), 0, SPROP_NOSCALE ),
	SendPropVector( SENDINFO( m_queuedLeap ), -1, SPROP_NOSCALE ),
#endif
END_NETWORK_TABLE()

BEGIN_PREDICTION_DATA( CLeap )
#ifdef CLIENT_DLL
	DEFINE_PRED_FIELD( m_isLeaping, FIELD_BOOLEAN, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_leapStartTime, FIELD_FLOAT, FTYPEDESC_INSENDTABLE ),
	{ FIELD_EMBEDDED, "m_leapAgainTimer", { (int)_hacky_datamap_offsetof( classNameTypedef, m_leapAgainTimer ), 0 }, 1, FTYPEDESC_SAVE | FTYPEDESC_KEY, NULL, NULL, NULL, &CountdownTimer::m_PredMap },
	DEFINE_PRED_FIELD( m_queuedLeap, FIELD_VECTOR, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_interrupted, FIELD_BOOLEAN, FTYPEDESC_SAVE ),
#endif
END_PREDICTION_DATA()

CLeap::CLeap()
{
	m_isLeaping = false;
	m_leapStartTime = 0.0f;
	m_queuedLeap = vec3_origin;
	m_interrupted = false;
	m_touchedPlayer = false;
	m_damagedDoor = false;
	m_leapDirection = vec3_origin;
}

// S3642C0/C28C2B0. The queued vector and post-flight timer are not reset here.
void CLeap::SetOwner( CTerrorPlayer *owner )
{
	SetRechargeTime( 0.0f, 0.0f );
	m_isLeaping = false;
	m_leapAgainTimer.Invalidate();
	m_touchedPlayer = false;
	m_damagedDoor = false;
	m_interrupted = false;
	m_leapStartTime = 0.0f;
	BaseClass::SetOwner( owner );
}

int CLeap::GetActivationButton() const { return IN_ATTACK; }

// S363D70/S364440 and C28BF80/C28C350 preserve unordered timer branches
// and the original FLOAT-versus-DOUBLE downward-launch threshold.
#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif

bool CLeap::IsWallKickAvailable() const
{
	return !m_interrupted && !( gpGlobals->curtime >= m_leapAgainTimer.m_timestamp );
}

bool CLeap::CanActivate()
{
	CTerrorPlayer *owner = GetOwner();
	if ( !owner || owner->IsGhost() || owner->m_jockeyVictim.Get() ) return false;
	if ( owner->m_pounceVictim.Get() || ( owner->m_nButtons & IN_ATTACK2 ) || IsActivationSuppressed() ) return false;
	return IsWallKickAvailable() || ( !m_isLeaping && BaseClass::CanActivate() );
}

bool CLeap::IsActive() const
{
#ifdef CLIENT_DLL
	return m_isLeaping;
#else
	return m_isLeaping || ( m_attackCooldown.m_timestamp > 0.0f && !( gpGlobals->curtime >= m_attackCooldown.m_timestamp ) );
#endif
}

Vector *CLeap::GetQueuedMovement( bool markChanged )
{
	return markChanged ? &m_queuedLeap.GetForModify() : const_cast<Vector *>( &m_queuedLeap.Get() );
}

// S364440/C28C350. Movement consumes m_queuedLeap; activation never teleports
// or applies a second velocity impulse itself.
bool CLeap::ActivateAbility()
{
	if ( !IsWallKickAvailable() && !CanActivate() ) return false;
	CTerrorPlayer *owner = GetOwner();
	if ( !owner ) return false; // Only owner-dependent work follows the native re-read.
#ifdef CLIENT_DLL
	if ( owner->m_holdingObject ) return false;
#else
	if ( owner->IsHoldingEntity( NULL ) ) return false;
#endif
	if ( !( gpGlobals->curtime >= owner->m_stunTimer.m_timestamp ) ) return false;
	if ( !IsWallKickAvailable() && !owner->GetGroundEntity() && owner->GetMoveType() != MOVETYPE_LADDER ) return false;

	Vector direction;
	if ( m_leapDirection == vec3_origin ) owner->EyeVectors( &direction );
	else { direction = m_leapDirection; VectorNormalize( direction ); }
	if ( IsWallKickAvailable() && DotProduct( owner->GetLadderNormal(), direction ) < 0.2f ) return false;
	Vector velocity = direction * z_leap_power.GetFloat();
	m_isLeaping = true;
	m_leapStartTime = gpGlobals->curtime;
	m_leapAgainTimer.Invalidate();
	if ( owner->GetGroundEntity() )
	{
		const float speed = VectorNormalize( velocity );
		if ( velocity.z < 0.0f && static_cast< double >( velocity.z ) > -0.7 )
		{
			trace_t trace;
			UTIL_TraceLine( owner->EyePosition(), owner->EyePosition() + velocity * 5000.0f,
				0x0202440B, owner, COLLISION_GROUP_NONE, &trace );
			if ( trace.fraction > 0.0f && trace.fraction < 1.0f && trace.endpos.z >= owner->GetAbsOrigin().z - 20.0f )
				velocity.z = 0.0f;
		}
		velocity *= speed;
	}
#ifndef CLIENT_DLL
	if ( owner->GetActiveWeapon() ) owner->GetActiveWeapon()->SendWeaponAnim( TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_LUNGE ) );
#endif
	m_queuedLeap = velocity;
	m_touchedPlayer = false;
	m_damagedDoor = false;
	m_interrupted = false;
	BaseClass::ActivateAbility();
	return true;
}

void CLeap::Update()
{
	CTerrorPlayer *owner = GetOwner();
	if ( !owner ) return;
	const Vector &queued = m_queuedLeap.Get();
	if ( queued.x > -0.01f && queued.x < 0.01f && queued.y > -0.01f && queued.y < 0.01f &&
		queued.z > -0.01f && queued.z < 0.01f && m_isLeaping &&
		( owner->GetMoveType() == MOVETYPE_LADDER || ( owner->GetGroundEntity() && gpGlobals->curtime > m_leapStartTime + 0.3f ) ) )
	{
		m_isLeaping = false;
		m_leapStartTime = 0.0f;
		SetRechargeTime( z_leap_interval.GetFloat(), 0.0f );
		m_attackCooldown.Start( z_leap_cooldown.GetFloat() );
	}
}

void CLeap::StopActivation( float duration )
{
	if ( m_isLeaping )
	{
		m_isLeaping = false;
		m_leapStartTime = 0.0f;
	}
	SuppressActivation( duration );
}

void CLeap::OnOwnerDamaged( const CTakeDamageInfo &info )
{
#ifndef CLIENT_DLL
	if ( GetOwner() && m_isLeaping ) m_interrupted = true;
#endif
}

void CLeap::OnTouch( CBaseEntity *other )
{
#ifndef CLIENT_DLL
	CTerrorPlayer *owner = GetOwner();
	if ( !owner || owner->m_jockeyVictim.Get() ) return;
	CBaseEntity *ground = owner->GetGroundEntity();
	if ( ground && !ground->IsPlayer() ) return;
	if ( !other || !other->IsPlayer() || owner->m_jockeyVictim.Get() || !BaseClass::CanActivate() ) return;
	CTerrorPlayer *victim = ToTerrorPlayer( other );
	if ( victim ) owner->TryJockeyRide( victim );
#endif
}

#ifdef CLIENT_DLL
float CLeap::GetRechargeProgress() const
{
	if ( !GetOwner() ) return 0.0f;
	if ( !m_isLeaping ) return BaseClass::GetRechargeProgress();
	return !m_interrupted && !( gpGlobals->curtime >= m_leapAgainTimer.m_timestamp ) ? 1.0f : 0.0f;
}

const char *CLeap::GetCooldownBackground() const { return "HUD/PZ_charge_bg"; }
const char *CLeap::GetCooldownForeground() const { return "HUD/PZ_charge_jockey"; }
#else

#if defined( GAME_DLL )
CON_COMMAND_F( dismount, "Dismounts a survivor as a jockey", 0x00004004 )
#else
CON_COMMAND_F( dismount, "Dismounts a survivor as a jockey", FCVAR_CHEAT )
#endif
{
	CTerrorPlayer *player = ToTerrorPlayer( UTIL_GetCommandClient() );
	if ( player && player->m_jockeyVictim.Get() ) player->ReleaseJockey( NULL );
}
#endif

#ifdef _MSC_VER
#pragma float_control( pop )
#endif

#ifndef CLIENT_DLL
BEGIN_DATADESC( CLeap )
END_DATADESC()
#endif
