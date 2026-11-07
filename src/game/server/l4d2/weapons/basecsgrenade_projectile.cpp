//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/basecsgrenade_projectile.h"
#include "soundent.h"
#include "effect_dispatch_data.h"
#include "te_effect_dispatch.h"
#include "igameevents.h"
#include "precache_register.h"
#include "tier0/memdbgon.h"

IMPLEMENT_SERVERCLASS( CBaseCSGrenadeProjectile, DT_BaseCSGrenadeProjectile )
BEGIN_SEND_TABLE( CBaseCSGrenadeProjectile, DT_BaseCSGrenadeProjectile )
	SendPropVector( SENDINFO( m_vInitialVelocity ), 20, 0, -3000.0f, 3000.0f ),
END_SEND_TABLE()

BEGIN_DATADESC( CBaseCSGrenadeProjectile )
	DEFINE_THINKFUNC( DangerSoundThink ),
END_DATADESC()

void CBaseCSGrenadeProjectile::Precache()
{
	// S237C50: the generic grenade sound precedes the named splash effect.
	BaseClass::Precache();
	PrecacheEffect( "gunshotsplash" );
}

// S:0x237D60. The size is intentionally smaller than the SDK sample grenade.
void CBaseCSGrenadeProjectile::Spawn()
{
	Precache();
	BaseClass::Spawn();
	SetSolidFlags( FSOLID_NOT_STANDABLE );
	SetMoveType( MOVETYPE_FLYGRAVITY, MOVECOLLIDE_FLY_CUSTOM );
	SetSolid( SOLID_BBOX );
	UTIL_SetSize( this, Vector( -1.25f, -1.25f, -1.25f ), Vector( 1.25f, 1.25f, 1.25f ) );
}

// S:0x237C70.
unsigned int CBaseCSGrenadeProjectile::PhysicsSolidMaskForEntity() const
{
	return BaseClass::PhysicsSolidMaskForEntity() | CONTENTS_UNUSED;
}

// S:0x237C80. Equality is still live; DangerSoundThink detonates strictly after T.
void CBaseCSGrenadeProjectile::SetTimer( float flDuration )
{
	m_flDetonateTime = gpGlobals->curtime + flDuration;
}

void CBaseCSGrenadeProjectile::SetInitialVelocity( const Vector &velocity )
{
	m_vInitialVelocity = velocity;
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
// S:0x237DE0. World think time, not the thrower's command clock.
void CBaseCSGrenadeProjectile::DangerSoundThink()
{
	if ( !IsInWorld() )
	{
		UTIL_Remove( this );
		return;
	}
	if ( gpGlobals->curtime > m_flDetonateTime )
	{
		Detonate();
		return;
	}
	CSoundEnt::InsertSound( SOUND_DANGER, GetAbsOrigin() + GetAbsVelocity() * 0.5f,
		GetAbsVelocity().Length(), 0.2f );
	SetNextThink( static_cast< float >( static_cast< double >( gpGlobals->curtime ) + 0.2 ) );
	if ( GetWaterLevel() != 0 )
		SetAbsVelocity( GetAbsVelocity() * 0.5f );
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

void CBaseCSGrenadeProjectile::GrenadeTouch( CBaseEntity *pOther )
{
	BaseClass::BounceTouch( pOther );
}

// S:0x237FE0. Preserve trace-attack -> ApplyMultiDamage before bounce selection.
void CBaseCSGrenadeProjectile::ResolveFlyCollisionCustom( trace_t &trace, Vector &vecVelocity )
{
	float flSurfaceElasticity = 1.0f;
	if ( trace.m_pEnt && trace.m_pEnt->IsPlayer() )
		flSurfaceElasticity = 0.3f;

	CBaseEntity *pHit = trace.m_pEnt;
	if ( pHit && ( FClassnameIs( pHit, "func_breakable" ) || FClassnameIs( pHit, "func_breakable_surf" ) ||
		( FClassnameIs( pHit, "prop_physics" ) && pHit->GetHealth() <= 10 && pHit->m_takedamage == DAMAGE_YES ) ) )
	{
		CTakeDamageInfo info( this, this, 10.0f, DMG_CLUB );
		CalculateMeleeDamageForce( &info, GetAbsVelocity(), trace.startpos );
		ClearMultiDamage();
		pHit->DispatchTraceAttack( info, GetAbsVelocity(), &trace );
		ApplyMultiDamage();
		if ( pHit->GetHealth() <= 0 )
		{
			SetAbsVelocity( GetAbsVelocity() * 0.4f );
			return;
		}
	}

	const float flTotalElasticity = clamp( GetElasticity() * flSurfaceElasticity, 0.0f, 0.9f );
	Vector vecAbsVelocity;
	PhysicsClipVelocity( GetAbsVelocity(), trace.plane.normal, vecAbsVelocity, 2.0f );
	vecAbsVelocity *= flTotalElasticity;
	if ( vecAbsVelocity.z > 150.0f )
		vecAbsVelocity.z = 150.0f;

	VectorAdd( GetBaseVelocity(), vecAbsVelocity, vecVelocity );
	const float flSpeedSqr = vecVelocity.LengthSqr();
	if ( trace.plane.normal.z > 0.7f )
	{
		SetAbsVelocity( vecAbsVelocity );
		if ( flSpeedSqr < 900.0f )
		{
			if ( pHit && pHit->IsStandable() )
				SetGroundEntity( pHit );
			SetAbsVelocity( vec3_origin );
			SetLocalAngularVelocity( vec3_angle );
			QAngle angles;
			VectorAngles( trace.plane.normal, angles );
			angles.y = random->RandomFloat( 0.0f, 360.0f );
			SetAbsAngles( angles );
		}
		else
		{
			const Vector vecDelta = GetBaseVelocity() - vecAbsVelocity;
			Vector vecBaseDir = GetBaseVelocity();
			VectorNormalize( vecBaseDir );
			const float flScale = DotProduct( vecDelta, vecBaseDir );
			VectorScale( vecAbsVelocity, ( 1.0f - trace.fraction ) * gpGlobals->frametime, vecVelocity );
			VectorMA( vecVelocity, ( 1.0f - trace.fraction ) * gpGlobals->frametime,
				GetBaseVelocity() * flScale, vecVelocity );
			PhysicsPushEntity( vecVelocity, &trace );
		}
	}
	else if ( flSpeedSqr < 900.0f )
	{
		SetAbsVelocity( vec3_origin );
		SetLocalAngularVelocity( vec3_angle );
	}
	else
	{
		SetAbsVelocity( vecAbsVelocity );
	}
	BounceSound();

	CBasePlayer *pThrower = ToBasePlayer( GetThrower() );
	if ( pThrower )
	{
		IGameEvent *pEvent = gameeventmanager->CreateEvent( "grenade_bounce" );
		if ( pEvent )
		{
			pEvent->SetInt( "userid", pThrower->GetUserID() );
			pEvent->SetFloat( "x", GetAbsOrigin().x );
			pEvent->SetFloat( "y", GetAbsOrigin().y );
			pEvent->SetFloat( "z", GetAbsOrigin().z );
			gameeventmanager->FireEvent( pEvent );
		}
	}
}

// S:0x2385A0. The water surface search and its one RNG draw are independent of blast.
void CBaseCSGrenadeProjectile::Splash()
{
	const Vector origin = GetAbsOrigin();
	Vector surface = origin;
	trace_t trace;
	UTIL_TraceLine( origin, origin + Vector( 0, 0, 512 ), MASK_WATER, NULL, COLLISION_GROUP_NONE, &trace );
	if ( trace.startsolid )
	{
		if ( trace.fractionleftsolid != 0.0f )
			surface = origin + Vector( 0, 0, 512.0f * trace.fractionleftsolid );
	}
	else
	{
		UTIL_TraceLine( origin, origin - Vector( 0, 0, 512 ), MASK_WATER, NULL, COLLISION_GROUP_NONE, &trace );
		if ( trace.fraction < 1.0f )
			surface = trace.endpos;
	}
	CEffectData data;
	data.m_vOrigin = surface;
	data.m_vNormal.Init( 0.0f, 0.0f, 1.0f );
	data.m_flScale = random->RandomFloat( 1.0f, 2.0f );
	if ( GetWaterType() & CONTENTS_SLIME )
		data.m_fFlags |= 1;
	DispatchEffect( "gunshotsplash", data );
}

