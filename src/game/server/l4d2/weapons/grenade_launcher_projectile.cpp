//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/grenade_launcher_projectile.h"
#include "effect_dispatch_data.h"
#include "te_effect_dispatch.h"
#include "particle_parse.h"
#include "soundent.h"
#include "decals.h"
#include "ndebugoverlay.h"
#include "vphysics_interface.h"
#ifdef L4D2
#include "precache_register.h"
#endif
#include "tier0/memdbgon.h"

LINK_ENTITY_TO_CLASS( grenade_launcher_projectile, CGrenadeLauncher_Projectile );
#ifndef L4D2
PRECACHE_REGISTER( grenade_launcher_projectile );
#endif
IMPLEMENT_SERVERCLASS_ST( CGrenadeLauncher_Projectile, DT_GrenadeLauncher_Projectile )
END_SEND_TABLE()

static ConVar grenadelauncher_damage( "grenadelauncher_damage", "400", 0x00004004 );
static ConVar grenadelauncher_force_kill( "grenadelauncher_force_kill", "2000.0", 0x00004004 );
static ConVar grenadelauncher_radius_kill( "grenadelauncher_radius_kill", "180", 0x00004004 );
static ConVar grenadelauncher_radius_stumble( "grenadelauncher_radius_stumble", "250", 0x00004004 );
static ConVar grenadelauncher_show_radius( "grenadelauncher_show_radius", "0", 0x00004004 );

CGrenadeLauncher_Projectile::CGrenadeLauncher_Projectile()
{
	// S:0x3CFD40. The original interval timer is invalid, not a fuse.
	m_spawnTimer.Invalidate();
	m_bitsUpgradeDamageType = 0;
	m_bArmed = false;
}

void CGrenadeLauncher_Projectile::Precache()
{
	PrecacheModel( "models/w_models/weapons/w_HE_grenade.mdl" );
	BaseClass::Precache();
}

#pragma float_control( precise, on, push )
void CGrenadeLauncher_Projectile::Spawn()
{
	// S:0x3CFC60. Replaces the base danger/fuse think with a one-shot arm.
	m_spawnTimer.Invalidate();
	SetModel( "models/w_models/weapons/w_HE_grenade.mdl" );
	BaseClass::Spawn();
	m_bArmed = false;
	SetThink( &CGrenadeLauncher_Projectile::ArmThink );
	SetNextThink( static_cast< float >( static_cast< double >( gpGlobals->curtime ) + 0.15 ) );
}

void CGrenadeLauncher_Projectile::ArmThink()
{
	m_bArmed = true;
}

int CGrenadeLauncher_Projectile::GetDamageType() const
{
	// S:0x3CFCF0. This is the damage-type virtual, NOT a collision mask.
	return m_bArmed ? 0 : 0x10000000;
}

CGrenadeLauncher_Projectile *CGrenadeLauncher_Projectile::Create( const Vector &position,
	const QAngle &angles, const Vector &velocity, const AngularImpulse &angVelocity,
	CBaseCombatCharacter *pThrower, int bitsDamageType )
{
	// S:0x3CFDE0. The native entity list, not the weapon, owns the projectile.
	CGrenadeLauncher_Projectile *pGrenade = static_cast<CGrenadeLauncher_Projectile *>(
		CBaseEntity::Create( "grenade_launcher_projectile", position, angles, pThrower ) );
	if ( !pGrenade )
		return NULL;
	DevMsg( "CGrenadeLauncher_Projectile spawned at %f %f %f, velocity %f %f %f\n",
		position.x, position.y, position.z, velocity.x, velocity.y, velocity.z );
	// Original stores this base deadline, but ArmThink does not subsequently
	// install a fuse think. Do not turn it into a 1.5-second airburst.
	pGrenade->SetTimer( 1.5f );
	pGrenade->SetAbsVelocity( velocity );
	pGrenade->SetInitialVelocity( velocity );
	pGrenade->SetThrower( pThrower );
	pGrenade->SetFriction( 0.4f );
	pGrenade->SetGravity( 0.2f );
	pGrenade->SetElasticity( 0.45f );
	pGrenade->SetTouch( &CGrenadeLauncher_Projectile::GrenadeLauncherTouch );
	pGrenade->SetDamage( grenadelauncher_damage.GetFloat() );
	pGrenade->SetDamageRadius( 0.0f );
	pGrenade->ChangeTeam( pThrower->GetTeamNumber() );
	pGrenade->ApplyLocalAngularVelocityImpulse( angVelocity );
	pGrenade->m_bitsUpgradeDamageType = bitsDamageType;
	return pGrenade;
}

void CGrenadeLauncher_Projectile::GrenadeLauncherTouch( CBaseEntity *pOther )
{
	// S:0x3CFB50 ->0x3CFC10. Custom-movement solids are the exception to
	// the trigger/volume rejection. Sky removes without an explosion.
	if ( !pOther->IsSolidFlagSet( FSOLID_TRIGGER | FSOLID_VOLUME_CONTENTS ) || pOther->GetMoveType() == MOVETYPE_CUSTOM )
	{
		if ( GetTouchTrace().surface.flags & SURF_SKY )
			UTIL_Remove( this );
		else
			CBaseGrenade::ExplodeTouch( pOther );
	}
}

void CGrenadeLauncher_Projectile::Explode( trace_t *pTrace, int bitsDamageType )
{
	// S:0x3CFFA0. Presentation samples the ground/water before the contact
	// trace moves the damage origin out of the surface.
	const Vector origin = GetAbsOrigin();
	trace_t groundTrace;
	UTIL_TraceLine( origin, origin - Vector( 0, 0, 128 ), MASK_SOLID | CONTENTS_DEBRIS, this, COLLISION_GROUP_NONE, &groundTrace );
	if ( enginetrace->GetPointContents( groundTrace.endpos ) & ( CONTENTS_WATER | CONTENTS_SLIME ) )
	{
		trace_t waterTrace;
		UTIL_TraceLine( groundTrace.endpos, origin + Vector( 0, 0, 512 ), CONTENTS_WATER | CONTENTS_SLIME, this, COLLISION_GROUP_NONE, &waterTrace );
		if ( waterTrace.allsolid )
		{
			DispatchParticleEffect( "weapon_grenadelauncher_water", groundTrace.endpos, vec3_angle );
		}
		else
		{
			const Vector &waterOrigin = waterTrace.fractionleftsolid > 0.0f && waterTrace.fractionleftsolid < 1.0f ? waterTrace.startpos : waterTrace.endpos;
			DispatchParticleEffect( "weapon_grenadelauncher_water", waterOrigin, vec3_angle );
			DispatchParticleEffect( "weapon_grenadelauncher_water_splash", waterOrigin, vec3_angle );
		}
	}
	else
	{
		const surfacedata_t *pSurface = groundTrace.fraction < 1.0f ? physprops->GetSurfaceData( groundTrace.surface.surfaceProps ) : NULL;
		// Original material letters D/K/O; target CHAR_TEX_SAND is N.
		if ( pSurface && ( pSurface->game.material == 'D' || pSurface->game.material == 'K' || pSurface->game.material == 'O' ) )
			DispatchParticleEffect( "weapon_grenadelauncher_dirt", groundTrace.endpos, vec3_angle );
		else
			DispatchParticleEffect( "weapon_grenadelauncher", pSurface ? groundTrace.endpos : origin, vec3_angle );
	}

	SetModelName( NULL_STRING );
	AddSolidFlags( FSOLID_NOT_SOLID );
	m_takedamage = DAMAGE_NO;
	if ( pTrace->fraction != 1.0f )
		SetAbsOrigin( pTrace->endpos + pTrace->plane.normal * 0.6f );
	CSoundEnt::InsertSound( SOUND_COMBAT, GetAbsOrigin(), BASEGRENADE_EXPLOSION_VOLUME, 3.0f );
	EmitSound( "GrenadeLauncher.Explode" );

	const Vector damageOrigin = GetAbsOrigin();
	CBaseCombatCharacter *pThrower = GetThrower();
	Vector reportedOrigin = pThrower ? pThrower->GetAbsOrigin() : vec3_origin;
	CTakeDamageInfo damage( this, pThrower, vec3_origin, damageOrigin, GetDamage(), bitsDamageType | 0x01000000, 0, &reportedOrigin );
	const float flForce = grenadelauncher_force_kill.GetFloat();
	damage.SetDamageForce( Vector( flForce, flForce, flForce ) );
	damage.SetDamageCustom( 1 );
	RadiusDamage( damage, damageOrigin, grenadelauncher_radius_kill.GetFloat(), CLASS_NONE, NULL );

	CEffectData effect;
	effect.m_vOrigin = damageOrigin;
	effect.m_flMagnitude = static_cast< float >( static_cast< double >( grenadelauncher_force_kill.GetFloat() ) * 0.1 );
	effect.m_flRadius = grenadelauncher_radius_kill.GetFloat();
	DispatchEffect( "ExplosionForce", effect );

	const int nStumbleType = 0x02000000 | ( m_bitsUpgradeDamageType & 8 );
	CTakeDamageInfo stumble( this, pThrower, vec3_origin, damageOrigin, 1.0f, nStumbleType, 0, &reportedOrigin );
	stumble.SetDamageCustom( 1 );
	RadiusDamage( stumble, damageOrigin, grenadelauncher_radius_stumble.GetFloat(), CLASS_NONE, NULL );
	if ( grenadelauncher_show_radius.GetBool() )
	{
		NDebugOverlay::Sphere( damageOrigin, vec3_angle, grenadelauncher_radius_kill.GetFloat(), 0, 0, 255, 100, false, 3.0f );
		NDebugOverlay::Sphere( damageOrigin, vec3_angle, grenadelauncher_radius_stumble.GetFloat(), 0, 255, 0, 100, false, 3.0f );
	}
	UTIL_DecalTrace( pTrace, "Scorch" );
	SetThink( &CBaseEntity::SUB_Remove );
	SetNextThink( gpGlobals->curtime );
	SetTouch( NULL );
	SetSolid( SOLID_NONE );
	AddEffects( EF_NODRAW );
	SetAbsVelocity( vec3_origin );
}
#pragma float_control( pop )

// Native server 0x3cfad0; 0x5c4cf8 is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace grenade_launcher_projectilePrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "grenade_launcher_projectile", 0x5c4cf8 ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_ENTITY, "grenade_launcher_projectile", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif
