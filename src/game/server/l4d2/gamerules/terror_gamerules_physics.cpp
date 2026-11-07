//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/player/terrorplayer.h"
#include "props.h"
#include "physics.h"
#include "takedamageinfo.h"
#include "debugoverlay_shared.h"
#include "team.h"
#include "collisionutils.h"
#include "in_buttons.h"
#include "l4d2/gamerules/terror_gamerules_physics_console_server_cvars.inc"
#include "l4d2/terror_trace_filter.h"
#include "l4d2/terror_physics_math.h"
#include "tier0/memdbgon.h"

ConVar sv_turbophysics_shadow( "sv_turbophysics_shadow", "0", 0x00004004,
	"players have physics shadows even when sv_turbophysics is 1" );
ConVar z_push_force( "z_push_force", "2000", 0x00004004 );
ConVar z_push_mass_max( "z_push_mass_max", "200", 0x00004004 );

namespace
{
	class CTraceFilterOnlyPhysicsDamage : public CTraceFilterSimple
	{
	public:
		CTraceFilterOnlyPhysicsDamage( CBaseEntity *player ) : CTraceFilterSimple( player, COLLISION_GROUP_NONE ) {}
		virtual bool ShouldHitEntity( IHandleEntity *handle, int contentsMask ) OVERRIDE
		{
			// Native2DC320: reject static props before dereferencing the entity.
			if ( staticpropmgr->IsStaticProp( handle ) || !handle ) return false;
			CBaseEntity *entity = static_cast< IServerUnknown * >( handle )->GetBaseEntity();
			return entity && entity->GetMoveType() == MOVETYPE_VPHYSICS && CTraceFilterSimple::ShouldHitEntity( handle, contentsMask );
		}
		virtual TraceType_t GetTraceType() const OVERRIDE { return TRACE_ENTITIES_ONLY; } // Native2D8C60 returns2.
	};
	double PhysicsPushDistance( CBaseEntity *entity, const Vector &point )
	{
		// Native092960 returns the sqrt WITHOUT a float return-value spill.
		Vector local, nearest;
		entity->CollisionProp()->WorldToCollisionSpace( point, &local );
		CalcClosestPointOnAABB( entity->CollisionProp()->OBBMins(), entity->CollisionProp()->OBBMaxs(), local, nearest );
		const float x = local.x - nearest.x, y = local.y - nearest.y, z = local.z - nearest.z;
		const float square = ( y * y + z * z ) + x * x;
		return sqrt( (double)square );
	}
	Vector PhysicsPushDirection( CTerrorPlayer *first, int buttons )
	{
		Vector direction;
		first->EyeVectors( &direction );
		direction.z = 0.0f;
		TerrorPhysicsNormalize( direction );
		// BOTH forward/back chooses forward. Other actors' flags still use
		// FIRST actor's eye vectors in native2E14CD: retain the original bug.
		if ( buttons & IN_BACK )
		{
			if ( !( buttons & IN_FORWARD ) ) direction *= -1.0f;
		}
		else if ( !( buttons & IN_FORWARD ) ) direction = vec3_origin;
		return direction;
	}
	bool IsPhysicsSurvivorTeam( CBaseEntity *entity )
	{
		return entity->GetTeamNumber() == TEAM_SURVIVOR || entity->GetTeamNumber() == TEAM_L4D1_SURVIVOR;
	}
	void DrawCrushTrace( const trace_t &trace, int red, int green, int blue )
	{
		// Native126810 is a LINE, not a hull overlay; both endpoints gain0.1Z.
		Vector start = trace.startpos, end = trace.endpos;
		start.z += 0.1f;
		end.z += 0.1f;
		NDebugOverlay::Line( start, end, red, green, blue, true, -1.0f );
	}
	bool FindPhysicsUnpenetration( CPhysicsProp *prop, unsigned int mask, const Vector &direction, Vector *position )
	{
		// Native2DCBC0: contracted WORLD AABB translated relative to prop origin.
		Vector mins, maxs;
		prop->CollisionProp()->WorldSpaceAABB( &mins, &maxs );
		const Vector origin = prop->GetAbsOrigin();
		mins.x = ( mins.x - origin.x ) + 1.0f;
		mins.y = ( mins.y - origin.y ) + 1.0f;
		mins.z = ( mins.z - origin.z ) + 1.0f;
		maxs.x = ( maxs.x - origin.x ) - 1.0f;
		maxs.y = ( maxs.y - origin.y ) - 1.0f;
		maxs.z = ( maxs.z - origin.z ) - 1.0f;
		if ( maxs.z - mins.z > 4.0f ) mins.z += 2.0f;
		Vector away( -direction.x, -direction.y, 0.0f );
		if ( away.x > -0.01f && away.x < 0.01f && away.y > -0.01f && away.y < 0.01f )
		{
			away.x = RandomFloat( -1.0f, 1.0f );
			away.y = RandomFloat( -1.0f, 1.0f );
		}
		TerrorPhysicsNormalize( away );
		for ( int distance = 1; distance <= 100; distance += 10 )
		{
			const Vector candidate( (float)distance * away.x + origin.x,
				(float)distance * away.y + origin.y, (float)distance * away.z + origin.z );
			Ray_t ray;
			ray.Init( candidate, candidate, mins, maxs );
			CTraceFilterSimple filter( prop, COLLISION_GROUP_NONE );
			trace_t trace;
			enginetrace->TraceRay( ray, mask, &filter, &trace );
			if ( r_visualizetraces.GetInt() != 0 ) DrawCrushTrace( trace, 255, 255, 0 );
			if ( trace.allsolid ) continue; // Native tests ALLSOLID, not STARTSOLID here.
			CTraceFilterWorldOnly world;
			ray.Init( origin, candidate );
			enginetrace->TraceRay( ray, 33570827u, &world, &trace );
			if ( r_visualizetraces.GetInt() != 0 ) DrawCrushTrace( trace, 255, 0, 0 );
			if ( !trace.startsolid && trace.fraction >= 1.0f && !trace.allsolid )
			{
				*position = candidate;
				return true;
			}
		}
		return false;
	}
	bool UnpenetratePhysicsProp( CPhysicsProp *prop, CTerrorPlayer *player )
	{
		// Native2DD020: first away, then a perpendicular, then its NEGATION.
		const unsigned int mask = prop->PhysicsSolidMaskForEntity();
		const QAngle angles = prop->GetAbsAngles();
		Vector direction = player->GetAbsOrigin() - prop->GetAbsOrigin();
		Vector position;
		if ( !FindPhysicsUnpenetration( prop, mask, direction, &position ) )
		{
			if ( direction.x > -0.01f && direction.x < 0.01f && direction.y > -0.01f && direction.y < 0.01f &&
				direction.z > -0.01f && direction.z < 0.01f )
			{
				direction.x = RandomFloat( -1.0f, 1.0f );
				direction.y = RandomFloat( -1.0f, 1.0f );
			}
			TerrorPhysicsNormalize( direction );
			Vector right, up;
			VectorVectors( direction, right, up );
			if ( !FindPhysicsUnpenetration( prop, mask, right, &position ) &&
				!FindPhysicsUnpenetration( prop, mask, -right, &position ) ) return false;
		}
		prop->SetAbsOrigin( position );
		prop->VPhysicsGetObject()->SetVelocity( &vec3_origin, &vec3_origin );
		prop->VPhysicsGetObject()->SetPosition( position, angles, true );
		return true;
	}
	void ApplyPlayerPhysicsCrush( CTerrorPlayer *player )
	{
		// Native2DD270. These are raw alive/ghost/noclip guards, not observer
		// mode or the separate physics-damage immunity timer in the damage owner.
		if ( !player->IsAlive() || player->IsGhost() || player->GetMoveType() == MOVETYPE_NOCLIP ) return;
		CTraceFilterOnlyPhysicsDamage filter( player );
		trace_t trace;
		const unsigned int mask = player->PhysicsSolidMaskForEntity();
		UTIL_TraceEntity( player, player->GetAbsOrigin(), player->GetAbsOrigin(), mask, &filter, &trace );
		if ( trace.fraction != 0.0f || !trace.m_pEnt ) return;
		CPhysicsProp *prop = dynamic_cast< CPhysicsProp * >( trace.m_pEnt );
		if ( !prop || !prop->VPhysicsGetObject() ) return;
		float mass = prop->GetMass();
		if ( mass == 1.0f )
		{
			for ( int i = 1; i <= gpGlobals->maxClients; ++i )
			{
				CTerrorPlayer *holder = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
				// Native here checks only IsPlayer, NOT connected/alive/team.
				if ( !holder || !holder->IsPlayer() || holder == player || !holder->IsHoldingEntity( prop ) ) continue;
				holder->ForceDropOfCarriedPhysObjects( prop ); // Real Terror3144F0 deliberately drops ANY use entity.
				Vector velocity;
				AngularImpulse angular;
				prop->VPhysicsGetObject()->GetVelocity( &velocity, &angular );
				velocity *= -0.5f;
				prop->VPhysicsGetObject()->SetVelocity( &velocity, &angular );
				return;
			}
		}
		if ( !( mass > 20.0f ) ) { UnpenetratePhysicsProp( prop, player ); return; }
		Vector velocity;
		AngularImpulse angular;
		prop->VPhysicsGetObject()->GetVelocity( &velocity, &angular );
		Vector relative = velocity - player->GetAbsVelocity();
		const float speed = (float)TerrorPhysicsNormalize( velocity );
		const double relativeLength = TerrorPhysicsNormalize( relative );
		float relativeSpeed = (float)relativeLength;
		if ( !( (double)speed > relativeLength ) ) relativeSpeed = speed; // Native FCOMIP/JA retains unordered fallthrough.
		if ( mass <= 100.0f && IsPhysicsSurvivorTeam( player ) )
		{
			CBasePlayer *attacker = prop->HasPhysicsAttacker( 1.0f );
			if ( attacker && attacker->IsPlayer() && IsPhysicsSurvivorTeam( attacker ) )
			{
				UnpenetratePhysicsProp( prop, player );
				return;
			}
		}
		CBasePlayer *attacker = prop->HasPhysicsAttacker( 10.0f );
		if ( attacker && !attacker->IsPlayer() ) attacker = NULL;
		if ( mass > 1000.0f ) mass = 1000.0f;
		else if ( mass < 20.0f ) mass = 20.0f;
		if ( relativeSpeed > 100.0f ) relativeSpeed = 100.0f;
		else if ( relativeSpeed < 0.0f ) relativeSpeed = 0.0f;
		if ( relativeSpeed < 100.0f && mass < 900.0f )
		{
			if ( sv_turbophysics_shadow.GetInt() == 0 && !UnpenetratePhysicsProp( prop, player ) &&
				!( DotProduct( player->GetAbsOrigin() - prop->GetAbsOrigin(), velocity ) < 0.0f ) )
			{
				velocity *= -40.0f; // Native uses NORMALIZED prop velocity, not the relative vector.
				prop->VPhysicsGetObject()->SetVelocity( &velocity, &vec3_origin );
			}
			return;
		}
		if ( !player->IsIncapacitated() && relativeSpeed < 50.0f )
		{
			Vector position = player->GetAbsOrigin();
			Vector mins, maxs;
			prop->CollisionProp()->WorldSpaceAABB( &mins, &maxs );
			if ( maxs.z + 1.0f >= position.z )
			{
				for ( float height = position.z; maxs.z + 1.0f >= height; height += 1.0f )
				{
					position.z = height;
					UTIL_TraceEntity( player, position, position, mask, player, COLLISION_GROUP_NONE, &trace );
					if ( !trace.allsolid && trace.fraction > 0.0f )
					{
						const Vector &old = player->GetAbsOrigin();
						UTIL_LogPrintf( "(PHYSICS) %s was moved above a prop (%f %f %f) to (%f %f %f)\n",
							player->GetPlayerName(), old.x, old.y, old.z, trace.endpos.x, trace.endpos.y, trace.endpos.z );
						player->SetAbsOrigin( trace.endpos );
						return;
					}
				}
			}
		}
		const float minimumSpeedFactor = mass > 100.0f ? 0.5f : 0.0f;
		const float massFactor = 1.0f - ( 1000.0f - mass ) * 0.0010204081f;
		const float speedFactor = ( 1.0f - ( 100.0f - relativeSpeed ) * 0.01f ) * ( 1.0f - minimumSpeedFactor ) + minimumSpeedFactor;
		const float damage = ( massFactor * 100.0f ) * speedFactor;
		if ( damage > 0.0f && speed >= 50.0f )
		{
			UTIL_LogPrintf( "(PHYSICS) %s was hit for %.1f damage by a physics obs of mass %.2f(%.2f), moving at a relative speed %.2f(%.2f), moved by %s\n",
				player->GetPlayerName(), damage, mass, massFactor, relativeSpeed, speedFactor, attacker ? attacker->GetPlayerName() : "nobody" );
			CTakeDamageInfo info( prop, attacker, damage, DMG_CRUSH );
			CalculateMeleeDamageForce( &info, relative, player->WorldSpaceCenter(), damage ); // Native101E9640 scale is DAMAGE again.
			ClearMultiDamage();
			player->DispatchTraceAttack( info, relative, &trace );
			ApplyMultiDamage(); // Actual TakeDamage -> sole existing immunity/survivor-physics owner.
		}
	}
}

void CTerrorGameRules::UpdatePlayerPhysicsCrush()
{
	// Native2DEA20: real edict/index/player and connection8048!=2 admission.
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( player && player->edict() && player->entindex() != 0 && player->IsPlayer() && player->IsConnected() ) ApplyPlayerPhysicsCrush( player );
	}
}

void CTerrorGameRules::UpdatePlayerPushPhysics()
{
	// Native2E1032..2E1757: actual CTeam registration order, no connected,
	// alive, bot, ghost or ground-entity substitutions.13248 is m_pushEntity.
	CTeam *team = GetGlobalTeam( TEAM_SURVIVOR );
	CUtlVector< CBaseEntity * > processed;
	for ( int index = 0; index < team->GetNumPlayers(); ++index )
	{
		CTerrorPlayer *first = ToTerrorPlayer( team->GetPlayer( index ) );
		if ( !first || !first->IsPlayer() ) continue;
		CBaseEntity *object = first->m_pushEntity.Get();
		if ( !object ) continue;
		IPhysicsObject *physics = object->VPhysicsGetObject();
		if ( !physics ) continue;
		Vector velocity;
		AngularImpulse angular;
		physics->GetVelocity( &velocity, &angular );
		if ( ( velocity.y * velocity.y + velocity.x * velocity.x ) + velocity.z * velocity.z > 10000.0f ) continue;
		if ( ( angular.y * angular.y + angular.x * angular.x ) + angular.z * angular.z > 10000.0f ) continue;
		if ( processed.Find( object ) != processed.InvalidIndex() ) continue;
		processed.AddToTail( object );
		Vector direction = vec3_origin;
		Vector position = vec3_origin;
		direction += PhysicsPushDirection( first, first->m_nButtons );
		position += first->EyePosition(); // Native virtual560, NOT GetAbsOrigin.
		float near = 1.0f;
		if ( PhysicsPushDistance( object, first->WorldSpaceCenter() ) > 64.0 ) near = 0.0f;
		int count = 1;
		for ( int otherIndex = index + 1; otherIndex < team->GetNumPlayers(); ++otherIndex )
		{
			CTerrorPlayer *other = ToTerrorPlayer( team->GetPlayer( otherIndex ) );
			if ( !other || !other->IsPlayer() || other->m_pushEntity.Get() != object ) continue;
			++count;
			direction += PhysicsPushDirection( first, other->m_nButtons );
			position += first->EyePosition(); // Native repeats FIRST actor here too, not other's eye.
			if ( PhysicsPushDistance( object, other->WorldSpaceCenter() ) < 64.0 ) near += 1.0f;
		}
		// Native builds an otherwise unread vector of these same actors.
		// Count is its only live value; do not allocate/copy a dead second list.
		const float inverse = 1.0f / (float)count;
		position.x *= inverse;
		position.y *= inverse;
		position.z *= inverse;
		const float scale = z_push_force.GetFloat();
		Vector force( ( scale * direction.x ) * near, ( direction.y * scale ) * near, ( scale * direction.z ) * near );
		if ( !( force.x > -0.01f && force.x < 0.01f && force.y > -0.01f && force.y < 0.01f && force.z > -0.01f && force.z < 0.01f ) )
		{
			if ( (double)z_push_mass_max.GetFloat() * (double)(float)count >= (double)physics->GetMass() ) physics->ApplyForceOffset( force, position );
		}
	}
}

#ifndef CLIENT_DLL
extern ConVar z_tanks_block_molotovs;

float CTerrorGameRules::WeaponTraceEntity( CBaseEntity *entity, const Vector &start,
	const Vector &end, unsigned int mask, trace_t *trace )
{
	// Native S2DB6D0: the queried flags are damage type, not solid flags.
	if ( entity->GetDamageType() & 0x8 )
	{
		CTraceFilterIgnoreActors filter( entity, entity->GetCollisionGroup(), z_tanks_block_molotovs.GetBool(), -1 );
		UTIL_TraceEntity( entity, start, end, mask, &filter, trace );
	}
	else if ( entity->GetDamageType() & 0x40 )
	{
		CTraceFilterIgnoreActors filter( entity, entity->GetCollisionGroup(), false, -1 );
		UTIL_TraceEntity( entity, start, end, mask, &filter, trace );
	}
	else if ( entity->GetDamageType() & 0x10000000 )
	{
		CTraceFilterIgnoreActors filter( entity, entity->GetCollisionGroup(), false, 2 );
		UTIL_TraceEntity( entity, start, end, mask, &filter, trace );
	}
	else if ( entity->GetDamageType() & 0x400 )
	{
		CTraceFilterIgnoreActors filter( entity, entity->GetCollisionGroup(), false, 3 );
		UTIL_TraceEntity( entity, start, end, mask, &filter, trace );
	}
	else
		UTIL_TraceEntity( entity, start, end, mask, trace );
	return 1.0f;
}
#endif

