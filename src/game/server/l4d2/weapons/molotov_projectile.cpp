//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/molotov_projectile.h"
#include "l4d2/weapons/inferno.h"
#include "particle_parse.h"
#include "l4d2/terror_shareddefs.h"
#ifdef L4D2
#include "precache_register.h"
#endif
#include "tier0/memdbgon.h"

LINK_ENTITY_TO_CLASS( molotov_projectile, CMolotovProjectile );
IMPLEMENT_SERVERCLASS_ST( CMolotovProjectile, DT_MolotovProjectile )
END_SEND_TABLE()

BEGIN_DATADESC( CMolotovProjectile )
	DEFINE_ENTITYFUNC( GrenadeTouch ),
	DEFINE_THINKFUNC( StillThink ),
END_DATADESC()

void CMolotovProjectile::Precache()
{
	PrecacheModel( "models/w_models/weapons/w_eq_molotov.mdl" );
	PrecacheScriptSound( "Molotov.Throw" );
	PrecacheScriptSound( "Molotov.Loop" );
	PrecacheParticleSystem( "weapon_molotov_thrown" );
	PrecacheParticleSystem( "weapon_molotov_held" );
	BaseClass::Precache();
}

void CMolotovProjectile::Spawn()
{
	m_stillTimer.Invalidate();
	SetModel( "models/w_models/weapons/w_eq_molotov.mdl" );
	BaseClass::Spawn();
}

CMolotovProjectile *CMolotovProjectile::Create( const Vector &position, const QAngle &angles,
	const Vector &velocity, const AngularImpulse &angularVelocity, CBaseCombatCharacter *pThrower )
{
	CMolotovProjectile *pGrenade = static_cast< CMolotovProjectile * >(
		CBaseEntity::Create( "molotov_projectile", position, angles, pThrower ) );
	if ( !pGrenade )
		return NULL;

	// S3DA4E0: the 1.5s base timer is not a molotov airburst fuse.
	pGrenade->SetTimer( 1.5f );
	pGrenade->SetAbsVelocity( velocity );
	pGrenade->SetInitialVelocity( velocity );
	pGrenade->SetThrower( pThrower );
	pGrenade->SetGravity( 0.4f );
	pGrenade->SetFriction( 0.2f );
	pGrenade->SetElasticity( 0.45f );
	pGrenade->SetTouch( &CMolotovProjectile::GrenadeTouch );
	pGrenade->SetThink( &CMolotovProjectile::StillThink );
	pGrenade->SetNextThink( gpGlobals->curtime + 2.0f );
	pGrenade->SetDamage( 200.0f );
	pGrenade->SetDamageRadius( 300.0f );
	pGrenade->ChangeTeam( pThrower ? pThrower->GetTeamNumber() : TEAM_SURVIVOR );
	pGrenade->SetLocalAngularVelocity( QAngle( angularVelocity.x, angularVelocity.y, angularVelocity.z ) );
	pGrenade->EmitSound( "Molotov.Throw" );
	pGrenade->EmitSound( "Molotov.Loop" );

	if ( pThrower && pThrower->IsPlayer() )
	{
		IGameEvent *pEvent = gameeventmanager->CreateEvent( "molotov_thrown" );
		if ( pEvent )
		{
			pEvent->SetInt( "userid", ToBasePlayer( pThrower )->GetUserID() );
			gameeventmanager->FireEvent( pEvent );
		}
	}
	return pGrenade;
}

void CMolotovProjectile::GrenadeTouch( CBaseEntity *pOther )
{
	// S3D9D30 deliberately ignores actors; the bile jar does not.
	if ( pOther->IsSolidFlagSet( FSOLID_TRIGGER | FSOLID_VOLUME_CONTENTS ) || pOther == GetThrower() ||
		FClassnameIs( pOther, "func_breakable" ) || FClassnameIs( pOther, "func_breakable_surf" ) )
		return;

	if ( FClassnameIs( pOther, "prop_car_alarm" ) || FClassnameIs( pOther, "prop_car_glass" ) )
	{
		CTakeDamageInfo info( this, GetThrower(), 10.0f, DMG_GENERIC );
		pOther->TakeDamage( info );
	}

	const trace_t &trace = GetTouchTrace();
	if ( trace.m_pEnt && trace.m_pEnt->MyCombatCharacterPointer() )
		return;
	if ( trace.plane.normal.z > 0.0f )
	{
		Detonate();
	}
	else
	{
		const Vector &velocity = GetAbsVelocity();
		SetAbsVelocity( Vector( velocity.x * 0.5f, velocity.y * 0.5f, -10.0f ) );
	}
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
void CMolotovProjectile::StillThink()
{
	// S3D9EF0: ordered speed >5 invalidates the clock; otherwise start
	// unless its timestamp is ordered-positive. Half-second equality waits.
	if ( !( GetAbsVelocity().LengthSqr() > 25.0f ) )
	{
		if ( !( m_stillTimer.GetStartTime() > 0.0f ) )
			m_stillTimer.Start();
	}
	else
	{
		m_stillTimer.Invalidate();
	}

	if ( m_stillTimer.GetStartTime() > 0.0f && m_stillTimer.GetElapsedTime() > 0.5f )
		Detonate();
	else
		SetNextThink( gpGlobals->curtime + 0.1f );
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

void CMolotovProjectile::Detonate()
{
	// S3DA030 retains the original sky early-outs, including the live entity.
	const trace_t &touch = GetTouchTrace();
	if ( touch.surface.flags & SURF_SKY )
		return;

	CBasePlayer *pThrower = ToBasePlayer( GetThrower() );
	if ( pThrower )
	{
		IGameEvent *pEvent = gameeventmanager->CreateEvent( "hegrenade_detonate" );
		if ( pEvent )
		{
			pEvent->SetInt( "userid", pThrower->GetUserID() );
			pEvent->SetFloat( "x", GetAbsOrigin().x );
			pEvent->SetFloat( "y", GetAbsOrigin().y );
			pEvent->SetFloat( "z", GetAbsOrigin().z );
			gameeventmanager->FireEvent( pEvent );
		}
	}

	trace_t ground;
	const trace_t *pTrace = &touch;
	if ( !touch.DidHit() )
	{
		UTIL_TraceLine( GetAbsOrigin() + Vector( 0, 0, 10 ),
			GetAbsOrigin() - Vector( 0, 0, 999999.875f ), 0x0200400B,
			this, COLLISION_GROUP_NONE, &ground );
		if ( ground.surface.flags & SURF_SKY )
			return;
		pTrace = &ground;
	}

	Vector spreadVelocity = m_vInitialVelocity;
	VectorNormalize( spreadVelocity );
	spreadVelocity *= GetAbsVelocity().Length();
	CInferno::Create( GetThrower(), pTrace->endpos, pTrace->plane.normal, spreadVelocity );
	StopSound( "Molotov.Loop" );
	UTIL_Remove( this );
}

void CMolotovProjectile::BounceSound()
{
	EmitSound( "GlassBottle.ImpactHard" );
}

// Native server 0x3d9bc0; 0x5c4e7c is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace molotov_projectilePrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "molotov_projectile", 0x5c4e7c ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_ENTITY, "molotov_projectile", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif
