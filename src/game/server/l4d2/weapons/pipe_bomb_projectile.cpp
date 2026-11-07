//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/pipe_bomb_projectile.h"
#include "particle_parse.h"
#include "effect_dispatch_data.h"
#include "te_effect_dispatch.h"
#include "physics.h"
#ifdef L4D2
#include "precache_register.h"
#endif
#include "tier0/memdbgon.h"

static ConVar pipe_bomb_shake_amplitude( "pipe_bomb_shake_amplitude", "50", 0x00000006 );
static ConVar pipe_bomb_shake_radius( "pipe_bomb_shake_radius", "750", 0x00000006 );
static ConVar pipe_bomb_timer_duration( "pipe_bomb_timer_duration", "6", 0x00000006 );
static ConVar pipe_bomb_initial_beep_interval( "pipe_bomb_initial_beep_interval", "0.5", 0x00000006 );
static ConVar pipe_bomb_beep_interval_delta( "pipe_bomb_beep_interval_delta", "0.025", 0x00000006 );
static ConVar pipe_bomb_beep_min_interval( "pipe_bomb_beep_min_interval", "0.1", 0x00000006 );

LINK_ENTITY_TO_CLASS( pipe_bomb_projectile, CPipeBombProjectile );
IMPLEMENT_SERVERCLASS_ST( CPipeBombProjectile, DT_PipeBombProjectile )
END_SEND_TABLE()

BEGIN_DATADESC( CPipeBombProjectile )
	DEFINE_THINKFUNC( BeepThink ),
END_DATADESC()

void CPipeBombProjectile::Precache()
{
	PrecacheModel( "models/w_models/Weapons/w_eq_pipebomb.mdl" );
	PrecacheScriptSound( "PipeBomb.Bounce" );
	PrecacheScriptSound( "PipeBomb.TimerBeep" );
	PrecacheParticleSystem( "weapon_pipebomb" );
	PrecacheParticleSystem( "weapon_pipebomb_dirt" );
	PrecacheParticleSystem( "weapon_pipebomb_water" );
	PrecacheParticleSystem( "weapon_pipebomb_water_splash" );
	PrecacheParticleSystem( "weapon_pipebomb_fuse" );
	PrecacheParticleSystem( "weapon_pipebomb_blinking_light" );
	BaseClass::Precache();
}

void CPipeBombProjectile::Spawn()
{
	SetModel( "models/w_models/Weapons/w_eq_pipebomb.mdl" );
	m_flBeepInterval = pipe_bomb_initial_beep_interval.GetFloat();
	m_flNextBeepTime = gpGlobals->curtime;
	BaseClass::Spawn();
}

CPipeBombProjectile *CPipeBombProjectile::Create( const Vector &position, const QAngle &angles,
	const Vector &velocity, const AngularImpulse &angularVelocity, CBaseCombatCharacter *pThrower )
{
	CPipeBombProjectile *pGrenade = static_cast< CPipeBombProjectile * >(
		CBaseEntity::Create( "pipe_bomb_projectile", position, angles, pThrower ) );
	if ( !pGrenade )
		return NULL;

	pGrenade->SetTimer( pipe_bomb_timer_duration.GetFloat() );
	pGrenade->SetAbsVelocity( velocity );
	pGrenade->SetInitialVelocity( velocity );
	pGrenade->SetThrower( pThrower );
	pGrenade->SetGravity( 0.4f );
	pGrenade->SetFriction( 0.2f );
	pGrenade->SetElasticity( 0.45f );
	pGrenade->SetDamage( 25.0f );
	pGrenade->SetDamageRadius( 400.0f );
	if ( pThrower )
		pGrenade->ChangeTeam( pThrower->GetTeamNumber() );
	pGrenade->SetLocalAngularVelocity( QAngle( angularVelocity.x, angularVelocity.y, angularVelocity.z ) );
	pGrenade->SetThink( &CBaseCSGrenadeProjectile::DangerSoundThink );
	pGrenade->SetNextThink( gpGlobals->curtime );
	pGrenade->SetContextThink( &CPipeBombProjectile::BeepThink, gpGlobals->curtime, "BEEP_CONTEXT" );
	return pGrenade;
}

void CPipeBombProjectile::BeepThink()
{
	// S3DDFB0: a context think, independent of the fuse/danger think. The
	// scheduled sound time accumulates; delayed thinks do not rebase it.
	if ( GetEFlags() & EFL_KILLME )
		return;
	EmitSound( "PipeBomb.TimerBeep", m_flNextBeepTime );
	m_flBeepInterval -= pipe_bomb_beep_interval_delta.GetFloat();
	if ( pipe_bomb_beep_min_interval.GetFloat() > m_flBeepInterval )
		m_flBeepInterval = pipe_bomb_beep_min_interval.GetFloat();
	m_flNextBeepTime += m_flBeepInterval;
	SetContextThink( &CPipeBombProjectile::BeepThink, gpGlobals->curtime + m_flBeepInterval, "BEEP_CONTEXT" );
	// The mounted original sound script's GameData is the attractor producer.
	// Real infected OnSound/Director consumers belong to M5/M6, not a target scan here.
}

void CPipeBombProjectile::GrenadeTouch( CBaseEntity *pOther )
{
	const trace_t &trace = GetTouchTrace();
	if ( !trace.m_pEnt || !trace.m_pEnt->MyCombatCharacterPointer() )
		CBaseGrenade::BounceTouch( pOther );
}

void CPipeBombProjectile::ResolveFlyCollisionCustom( trace_t &trace, Vector &velocity )
{
	if ( !trace.m_pEnt || !trace.m_pEnt->MyCombatCharacterPointer() )
		BaseClass::ResolveFlyCollisionCustom( trace, velocity );
}

void CPipeBombProjectile::Detonate()
{
	// S3DE0A0: choose the original effect from the ground surface/water trace,
	// before authoritative inherited blast damage. No generic TE explosion.
	const Vector position = GetAbsOrigin();
	trace_t ground;
	UTIL_TraceLine( position, position - Vector( 0, 0, 128 ), 0x0600400B,
		this, COLLISION_GROUP_NONE, &ground );
	if ( UTIL_PointContents( ground.endpos ) & ( CONTENTS_WATER | CONTENTS_SLIME ) )
	{
		trace_t water;
		UTIL_TraceLine( ground.endpos, position + Vector( 0, 0, 512 ),
			CONTENTS_WATER | CONTENTS_SLIME, this, COLLISION_GROUP_NONE, &water );
		if ( water.allsolid )
		{
			DispatchParticleEffect( "weapon_pipebomb_water", ground.endpos, vec3_angle );
		}
		else
		{
			const Vector &surface = water.fractionleftsolid > 0.0f && water.fractionleftsolid < 1.0f ? water.startpos : water.endpos;
			DispatchParticleEffect( "weapon_pipebomb_water", surface, vec3_angle );
			DispatchParticleEffect( "weapon_pipebomb_water_splash", surface, vec3_angle );
		}
	}
	else if ( ground.fraction < 1.0f )
	{
		surfacedata_t *pSurface = physprops->GetSurfaceData( ground.surface.surfaceProps );
		const int material = pSurface ? pSurface->game.material : 0;
		const bool dirt = material == 'D' || material == 'K' || material == 'O';
		DispatchParticleEffect( dirt ? "weapon_pipebomb_dirt" : "weapon_pipebomb", pSurface ? ground.endpos : position, vec3_angle );
	}
	else
	{
		DispatchParticleEffect( "weapon_pipebomb", position, vec3_angle );
	}

	CEffectData data;
	data.m_vOrigin = position;
	data.m_flMagnitude = 100.0f;
	data.m_flRadius = pipe_bomb_shake_radius.GetFloat();
	DispatchEffect( "ExplosionForce", data );
	BaseClass::Detonate();

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
}

void CPipeBombProjectile::BounceSound()
{
	EmitSound( "PipeBomb.Bounce" );
}

float CPipeBombProjectile::GetShakeAmplitude()
{
	return pipe_bomb_shake_amplitude.GetFloat();
}

float CPipeBombProjectile::GetShakeRadius()
{
	return pipe_bomb_shake_radius.GetFloat();
}

// Native server 0x3ddd90; 0x5c4f3c is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace pipe_bomb_projectilePrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "pipe_bomb_projectile", 0x5c4f3c ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_ENTITY, "pipe_bomb_projectile", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif
