//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/infected/special/spitter_projectile.h"
#include "l4d2/infected/special/insect_swarm.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/sound/terror_music.h"
#include "l4d2/nav/terror_nav_area.h"
#include "nav_mesh.h"
#include "particle_parse.h"
#ifdef L4D2
#include "precache_register.h"
#endif
#include "tier0/memdbgon.h"

ConVar z_spit_detonate_delay( "z_spit_detonate_delay", "0.1", 0x00004004 );

LINK_ENTITY_TO_CLASS( spitter_projectile, CSpitterProjectile );
IMPLEMENT_SERVERCLASS_ST( CSpitterProjectile, DT_SpitterProjectile )
END_SEND_TABLE()

BEGIN_DATADESC( CSpitterProjectile )
	DEFINE_ENTITYFUNC( GrenadeTouch ),
	DEFINE_THINKFUNC( ProjectileThink ),
END_DATADESC()

void CSpitterProjectile::Precache()
{
	PrecacheModel( "models/infected/limbs/exploded_boomer_steak1.mdl" );
	PrecacheScriptSound( "Flies.Swarm" );
	PrecacheScriptSound( "SpitProjectile.Bounce" );
	PrecacheParticleSystem( "spitter_projectile" );
	BaseClass::Precache();
}

void CSpitterProjectile::Spawn()
{
	m_stillTimer.Invalidate();
	SetModel( "models/infected/limbs/exploded_boomer_steak1.mdl" );
	BaseClass::Spawn();
}

unsigned int CSpitterProjectile::PhysicsSolidMaskForEntity() const
{
	return BaseClass::PhysicsSolidMaskForEntity() & ~0x8u;
}

CSpitterProjectile *CSpitterProjectile::Create( const Vector &position, const QAngle &angles,
	const Vector &velocity, const AngularImpulse &angularVelocity, CBaseCombatCharacter *thrower )
{
	CSpitterProjectile *projectile = static_cast< CSpitterProjectile * >(
		CBaseEntity::Create( "spitter_projectile", position, angles, thrower ) );
	if ( !projectile )
		return NULL;
	DevMsg( "Spitter projectile spawned at %f %f %f, velocity %f %f %f\n",
		position.x, position.y, position.z, velocity.x, velocity.y, velocity.z );
	projectile->SetTimer( 1.5f );
	projectile->SetAbsVelocity( velocity );
	projectile->SetInitialVelocity( velocity );
	projectile->SetThrower( thrower );
	projectile->SetMoveType( MOVETYPE_FLY, MOVECOLLIDE_FLY_BOUNCE );
	projectile->SetFriction( 0.2f );
	projectile->SetElasticity( 0.75f );
	projectile->SetTouch( &CSpitterProjectile::GrenadeTouch );
	projectile->SetThink( &CSpitterProjectile::ProjectileThink );
	projectile->SetNextThink( gpGlobals->curtime + 0.1f );
	projectile->SetDamage( 200.0f );
	projectile->SetDamageRadius( 300.0f );
	projectile->ChangeTeam( thrower ? thrower->GetTeamNumber() : 3 );
	projectile->SetLocalAngularVelocity( QAngle( angularVelocity.x, angularVelocity.y, angularVelocity.z ) );
	projectile->m_flSpawnTime = gpGlobals->curtime;
	return projectile;
}

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif
void CSpitterProjectile::GrenadeTouch( CBaseEntity *other )
{
	if ( ( other->IsSolidFlagSet( FSOLID_TRIGGER | FSOLID_VOLUME_CONTENTS ) &&
		other->GetCollisionGroup() != COLLISION_GROUP_WEAPON ) || other == GetThrower() )
		return;
	int team = other->GetTeamNumber();
	if ( team == 2 || team == 4 )
	{
		AddEffects( EF_NODRAW );
		SetAbsOrigin( other->GetAbsOrigin() );
		Detonate();
		return;
	}
	if ( team == 3 && ( GetTeamNumber() != 3 || GetZombieClass() != 0 ) )
		return;
	SetMoveType( MOVETYPE_FLYGRAVITY, MOVECOLLIDE_FLY_BOUNCE );
	if ( FClassnameIs( other, "func_breakable" ) || FClassnameIs( other, "func_breakable_surf" ) )
		return;
	const trace_t &touch = GetTouchTrace();
	if ( !( GetFlags() & FL_ONGROUND ) )
		BounceSound();
	if ( touch.plane.normal.z < 0.0f )
	{
		const Vector &velocity = GetAbsVelocity();
		SetAbsVelocity( Vector( velocity.x * 0.5f, velocity.y * 0.5f, -10.0f ) );
	}
	else if ( touch.plane.normal.z > 0.5f )
		Detonate();
}

void CSpitterProjectile::ProjectileThink()
{
	if ( m_flSpawnTime != -1.0f && gpGlobals->curtime - m_flSpawnTime >= 1.0f )
	{
		SetMoveType( MOVETYPE_FLYGRAVITY, MOVECOLLIDE_FLY_BOUNCE );
		const Vector &velocity = GetAbsVelocity();
		SetAbsVelocity( Vector( velocity.x * 0.3f, velocity.y * 0.3f, velocity.z ) );
		m_flSpawnTime = -1.0f;
	}
	if ( !( GetAbsVelocity().LengthSqr() > 25.0f ) )
	{
		if ( !( m_stillTimer.GetStartTime() > 0.0f ) )
			m_stillTimer.Start();
	}
	else
		m_stillTimer.Invalidate();
	if ( m_stillTimer.GetStartTime() > 0.0f && m_stillTimer.GetElapsedTime() > z_spit_detonate_delay.GetFloat() )
		Detonate();
	else
		SetNextThink( gpGlobals->curtime + 0.1f );
}
#ifdef _MSC_VER
#pragma float_control( pop )
#endif

void CSpitterProjectile::Detonate()
{
	const trace_t &touch = GetTouchTrace();
	if ( touch.surface.flags & SURF_SKY )
		return;
	TerrorNavArea *area = ToTerrorNavArea( TheNavMesh->GetNearestNavArea(
		GetAbsOrigin(), false, 10000.0f, false, true, TEAM_UNASSIGNED ) );
	bool checkpoint = area && area->HasSpawnAttributes( TERROR_NAV_CHECKPOINT );
	if ( checkpoint && !TerrorGameRules()->IsSurvivalMode() && !TerrorGameRules()->IsScavengeMode() &&
		!TheDirector->HasAnySurvivorLeftSafeArea() )
	{
		UTIL_Remove( this );
		return;
	}

	Vector position = touch.endpos;
	Vector normal = touch.plane.normal;
	CBaseEntity *parent = NULL;
	if ( !touch.DidHit() )
	{
		trace_t ground;
		UTIL_TraceLine( GetAbsOrigin() + Vector( 0, 0, 10 ),
			GetAbsOrigin() - Vector( 0, 0, 999999.875f ), 0x400B, this, COLLISION_GROUP_NONE, &ground );
		if ( ground.surface.flags & SURF_SKY )
			return;
		if ( ground.m_pEnt && !ground.m_pEnt->IsWorld() )
			parent = ground.m_pEnt;
		position = ground.endpos;
		normal = ground.plane.normal;
	}
	CInsectSwarm *swarm = static_cast< CInsectSwarm * >(
		CBaseEntity::Create( "insect_swarm", position, vec3_angle, GetThrower() ) );
	if ( parent )
		swarm->SetParent( parent, -1 );
	swarm->StartBurning( position, normal, vec3_origin, 0 );
	if ( checkpoint && !TerrorGameRules()->IsSurvivalMode() && !TerrorGameRules()->IsScavengeMode() )
		swarm->SetMaxFlames( 2 );
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( player && player->entindex() && player->IsConnected() && player->GetTeamNumber() == 2 )
		{
			if ( player->IsAlive() )
				player->GetMusic()->OnSpitterSpit( swarm->entindex() );
		}
	}
	CTerrorPlayer *thrower = ToTerrorPlayer( GetThrower() );
	if ( thrower )
	{
		IGameEvent *event = gameeventmanager->CreateEvent( "spit_burst" );
		if ( event )
		{
			event->SetInt( "userid", thrower->GetUserID() );
			event->SetInt( "subject", swarm->entindex() );
			gameeventmanager->FireEvent( event );
		}
	}
	UTIL_Remove( this );
}

void CSpitterProjectile::BounceSound()
{
	EmitSound( "SpitProjectile.Bounce" );
}

// Native server 0x3e23e0; 0x5c510c is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace spitter_projectilePrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "spitter_projectile", 0x5c510c ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_ENTITY, "spitter_projectile", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif
