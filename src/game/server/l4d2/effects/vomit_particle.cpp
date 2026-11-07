//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/effects/vomit_particle.h"
#include "l4d2/player/terrorplayer.h"
#include "te_effect_dispatch.h"
#include "debugoverlay_shared.h"
#include "ipredictionsystem.h"
#include "tier0/memdbgon.h"

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif

ConVar z_vomit_velocity( "z_vomit_velocity", "1700.0", 0x00004004, "Initial velocity of vomit damage entities." );
ConVar z_vomit_drag( "z_vomit_drag", "0.89", 0x00004004, "Air drag of vomit damage entities." );
ConVar z_vomit_float( "z_vomit_float", "-130.0", 0x00004004, "Upward float velocity of vomit damage entities." );
ConVar z_vomit_lifetime( "z_vomit_lifetime", "0.5", 0x00004004, "Time to live of vomit damage entities." );
ConVar z_vomit_vecrand( "z_vomit_vecrand", "0.05", 0x00004004, "Random vector added to initial velocity of vomit damage entities." );
ConVar z_vomit_boxsize( "z_vomit_boxsize", "1.0", 0x00004004, "Size of vomit damage entities." );
ConVar z_vomit_maxdamagedist( "z_vomit_maxdamagedist", "350.0", 0x00004004, "Maximum damage distance for vomit." );
ConVar z_vomit_velocityfadestart( "z_vomit_velocityfadestart", ".3", 0x00004004, "Time at which attacker's velocity contribution starts to fade." );
ConVar z_vomit_velocityfadeend( "z_vomit_velocityfadeend", ".5", 0x00004004, "Time at which attacker's velocity contribution finishes fading." );
extern ConVar z_vomit_debug;
extern ConVar sv_gravity;

LINK_ENTITY_TO_CLASS( vomit_particle, CVomitParticle );
BEGIN_DATADESC( CVomitParticle )
	DEFINE_THINKFUNC( ParticleThink ),
END_DATADESC()

CVomitParticle::CVomitParticle()
{
	m_attacker = NULL;
}

void CVomitParticle::Spawn()
{
	Precache();
	SetSolid( SOLID_BBOX );
	AddSolidFlags( FSOLID_NOT_SOLID | FSOLID_TRIGGER );
	SetMoveType( MOVETYPE_FLY, MOVECOLLIDE_FLY_CUSTOM );
	AddEFlags( EFL_NO_WATER_VELOCITY_CHANGE );
	SetCollisionGroup( COLLISION_GROUP_NONE );
	float size = z_vomit_boxsize.GetFloat();
	UTIL_SetSize( this, Vector( -size, -size, -size ), Vector( size, size, size ) );
	m_takedamage = DAMAGE_NO;
	m_previousPosition = GetAbsOrigin();
	const float lifetime = z_vomit_lifetime.GetFloat();
	const float randomLifetimeScale = RandomFloat( 0.9f, 1.1f );
	m_flLifetime = static_cast< float >( static_cast< double >( randomLifetimeScale ) * static_cast< double >( lifetime ) + static_cast< double >( gpGlobals->curtime ) );
	SetThink( &CVomitParticle::ParticleThink );
	SetNextThink( gpGlobals->curtime );
}

unsigned int CVomitParticle::PhysicsSolidMaskForEntity() const
{
	return 0x02004003;
}

CVomitParticle *CVomitParticle::Create( const Vector &position, const QAngle &angles,
	CTerrorPlayer *owner, CTerrorPlayer *attacker )
{
	if ( !owner || !attacker )
		return NULL;
	CVomitParticle *particle = static_cast< CVomitParticle * >(
		CBaseEntity::Create( "vomit_particle", position, angles, owner ) );
	if ( !particle )
		return NULL;
	particle->SetOwnerEntity( owner );
	particle->m_attacker = attacker;
	particle->ChangeTeam( owner->GetTeamNumber() );
	Vector forward;
	AngleVectors( angles, &forward );
	float speed = z_vomit_velocity.GetFloat();
	particle->m_sprayVelocity = forward * speed;
	float random = z_vomit_vecrand.GetFloat() * speed;
	float minimum = -random;
	float range = random - minimum;
	Vector perturbation;
	perturbation.x = ( static_cast< float >( RandomInt( 0, 32767 ) ) * 0.000030518509f ) * range + minimum;
	perturbation.y = ( static_cast< float >( RandomInt( 0, 32767 ) ) * 0.000030518509f ) * range + minimum;
	perturbation.z = ( static_cast< float >( RandomInt( 0, 32767 ) ) * 0.000030518509f ) * range + minimum;
	particle->m_sprayVelocity += perturbation;
	particle->m_inheritedVelocity = attacker->GetAbsVelocity();
	particle->SetAbsVelocity( particle->m_sprayVelocity );
	particle->SetAbsAngles( angles );
	return particle;
}

void ApplyVomitHit( CTerrorPlayer *attacker, CBaseEntity *victim, CVomitParticle *particle,
	int team, const Vector &velocity, const Vector &position )
{
	Vector direction = velocity;
	VectorNormalize( direction );
	direction.z -= 0.2f;
	trace_t impact;
	UTIL_TraceLine( position, position + direction * 80.0f, 0x46004003,
		particle, COLLISION_GROUP_NONE, &impact );
	VectorNormalize( direction );
	CTraceFilterSimple filter( attacker, COLLISION_GROUP_NONE );
	UTIL_TraceLine( position, position + direction * 120.0f, 0x46004003, &filter, &impact );
	if ( impact.m_pEnt && impact.m_pEnt == victim &&
		( !victim->IsPlayer() || victim->GetTeamNumber() == team ) )
	{
		CDisablePredictionFiltering disablePredictionFiltering;
		UTIL_BloodDecalTrace( &impact, BLOOD_COLOR_RED );
		CEffectData data;
		data.m_vOrigin = impact.endpos;
		data.m_vNormal = -direction;
		data.m_nEntIndex = impact.m_pEnt->entindex();
		data.m_flMagnitude = 20.0f;
		DispatchEffect( "csblood", data );
	}
	if ( victim->GetTeamNumber() == team || !victim->IsPlayer() )
	{
		if ( particle )
			UTIL_Remove( particle );
		return;
	}
	CTerrorPlayer *player = ToTerrorPlayer( victim );
	if ( ( player->GetTeamNumber() == 2 || player->GetTeamNumber() == 4 ) && !player->IsIT() )
		attacker->RecordVomitHit( player->GetSurvivorCharacter() );
	player->OnVomitedUpon( attacker, false );
}

void CVomitParticle::OnHit( CBaseEntity *entity )
{
	CTerrorPlayer *attacker = m_attacker.Get();
	if ( attacker )
	{
		ApplyVomitHit( attacker, entity, this, GetTeamNumber(), GetAbsVelocity(), WorldSpaceCenter() );
		UTIL_Remove( this );
	}
}

void CVomitParticle::ResolveFlyCollisionCustom( trace_t &trace, Vector &velocity )
{
	// Original base custom flight response (S14C680), not grenade bounce or
	// the SDK's frictional slide implementation.
	if ( trace.plane.normal.z > 0.7f )
	{
		velocity = GetAbsVelocity() + GetBaseVelocity();
		float gravity = GetGravity() == 0.0f ? 1.0f : GetGravity();
		if ( ( sv_gravity.GetFloat() * gravity ) * gpGlobals->frametime > velocity.z )
		{
			Vector stopped = GetAbsVelocity();
			stopped.z = 0.0f;
			SetAbsVelocity( stopped );
		}
		if ( trace.m_pEnt && trace.m_pEnt->IsStandable() )
			SetGroundEntity( trace.m_pEnt );
	}
	if ( trace.m_pEnt && trace.m_pEnt != this && trace.m_pEnt != m_attacker.Get() )
	{
		if ( z_vomit_debug.GetBool() )
		{
			NDebugOverlay::Line( trace.startpos, trace.endpos, 255, 100, 0, true, 10.0f );
			NDebugOverlay::EntityBounds( this, 255, 100, 0, 0, 10.0f );
		}
		OnHit( trace.m_pEnt );
	}
}

void CVomitParticle::ParticleThink()
{
	if ( gpGlobals->curtime >= m_flLifetime )
	{
		UTIL_Remove( this );
		return;
	}
	Vector current = GetAbsOrigin();
	const Vector previous = m_previousPosition;
	trace_t trace;
	CTraceFilterSkipTwoEntities filter( this, m_attacker.Get(), COLLISION_GROUP_NONE );
	UTIL_TraceLine( m_previousPosition, current, PhysicsSolidMaskForEntity(), &filter, &trace );
	if ( trace.fraction < 1.0f && trace.m_pEnt )
	{
		if ( z_vomit_debug.GetBool() )
		{
			NDebugOverlay::Line( previous, current, 255, 100, 0, true, 10.0f );
			NDebugOverlay::EntityBounds( this, 255, 100, 0, 0, 10.0f );
		}
		OnHit( trace.m_pEnt );
		return;
	}
	m_previousPosition = current;
	float age = z_vomit_lifetime.GetFloat() - ( m_flLifetime - gpGlobals->curtime );
	float inheritedScale = 1.0f - RemapValClamped( age,
		z_vomit_velocityfadestart.GetFloat(), z_vomit_velocityfadeend.GetFloat(), 0.0f, 1.0f );
	m_sprayVelocity *= z_vomit_drag.GetFloat();
	Vector velocity = m_sprayVelocity + m_inheritedVelocity * inheritedScale;
	velocity.z = ( m_sprayVelocity.z + z_vomit_float.GetFloat() ) + m_inheritedVelocity.z * inheritedScale;
	SetAbsVelocity( velocity );
	if ( z_vomit_debug.GetBool() )
	{
		NDebugOverlay::EntityBounds( this, 0, 100, 255, 0, 10.0f );
		NDebugOverlay::Line( previous, current, 0, 100, 255, true, 10.0f );
	}
	SetNextThink( gpGlobals->curtime );
}
#ifdef _MSC_VER
#pragma float_control( pop )
#endif
