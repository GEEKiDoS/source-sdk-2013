//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/weapon_tank_claw.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/nav/terror_nav_area.h"
#include "BasePropDoor.h"
#include "doors.h"
#include "props.h"
#include "particle_parse.h"
#include "effect_dispatch_data.h"
#include "te_effect_dispatch.h"
#include "igameevents.h"
#include "l4d2/terror_physics_math.h"
#include "tier0/memdbgon.h"

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif
extern ConVar z_tank_throw_force, z_door_pound_damage, z_attack_movement_penalty;
ConVar z_tank_attack_interval( "z_tank_attack_interval", "1.5", 0x00004004 ); // S:593B60

static bool IsTankClawSurvivor( CBaseEntity *entity )
{
	return entity && ( entity->GetTeamNumber() == TEAM_SURVIVOR || entity->GetTeamNumber() == TEAM_L4D1_SURVIVOR );
}
static CBasePropDoor *FindClosedTankClawCheckpointDoor( const Vector &position )
{
	// S:3E58F0 uses only the FIRST FindEntityByClassnameWithin result, then RTTI.
	CBasePropDoor *door = dynamic_cast< CBasePropDoor * >( gEntList.FindEntityByClassnameWithin( NULL, "prop_door*", position, 40.0f ) );
	return door && door->IsCheckpointDoor() && door->IsDoorClosed() ? door : NULL;
}
float CTankClaw::GetClawDamage( CTerrorPlayer *victim ) const // S:3E33A0, separate from Director266050/victim GetTankHitDamage
{
	switch ( TerrorGameRules()->GetDifficultyLevel() )
	{
	case 0: case 1: return 24.0f;
	case 2: return 33.0f;
	default: return 100.0f;
	}
}
bool CTankClaw::OnSwingHit( trace_t &trace, const Vector &direction, bool newHit ) // S:3E58F0
{
	CTerrorPlayer *owner = GetTerrorPlayerOwner();
	CBaseEntity *target = trace.m_pEnt;
	if ( !owner || !target ) return false;
	CTerrorPlayer *victim = ToTerrorPlayer( target );
	if ( newHit )
	{
		static ConVarRef witchHealth( "z_witch_health" );
		float damage = victim ? victim->GetTankHitDamage() : ( target->MyCombatCharacterPointer() ? witchHealth.GetFloat() : 999.90002f ); //266050
		if ( !m_isLowAttack && !owner->IsBot() && victim && victim->IsIncapacitated() && owner->WorldSpaceCenter().z > victim->GetAbsOrigin().z ) damage = 0.0f;
		bool notify = false;
		if ( FClassnameIs( target, "prop_door*" ) )
		{
			CBasePropDoor *door = dynamic_cast< CBasePropDoor * >( target );
			if ( door )
			{
				if ( door->IsCheckpointDoor() )
				{
					TerrorNavArea *area = ToTerrorNavArea( owner->GetLastKnownArea() );
					if ( !( area && area->HasSpawnAttributes( TERROR_NAV_CHECKPOINT ) ) && ( door->IsDoorLocked() || !door->IsUsableByTeam( TEAM_INFECTED ) ) )
					{
						IGameEvent *event = gameeventmanager->CreateEvent( "hit_safe_room" );
						if ( event ) { event->SetInt( "userid", owner->GetUserID() ); gameeventmanager->FireEvent( event ); }
						door->PlayPoundSound();
						return false;
					}
					if ( !door->IsDoorLocked() && door->m_takedamage != DAMAGE_YES )
					{
						variant_t empty;
						door->AcceptInput( door->IsDoorClosed() ? "PlayerOpen" : "PlayerClose", owner, owner, empty, 0 );
						return false;
					}
				}
				door->PlayPoundSound();
				damage = z_door_pound_damage.GetFloat() * 20.0f;
			}
		}
		else if ( FClassnameIs( target, "func_door*" ) )
		{
			if ( CBaseDoor *door = dynamic_cast< CBaseDoor * >( target ) ) door->Use( owner, owner, USE_TOGGLE, 0.0f );
		}
		else if ( !victim )
		{
			// The original queries OWNER's combat-character pointer here, not the
			// struck entity. Preserve that seemingly odd native sound policy.
			if ( owner->MyCombatCharacterPointer() ) { owner->EmitSound( "Claw.HitFlesh" ); owner->EmitSound( "HulkZombie.Punch" ); }
			else owner->EmitSound( "Claw.Hit" );
		}
		else
		{
			if ( damage == 0.0f ) return false;
			if ( IsTankClawSurvivor( victim ) )
			{
				TerrorNavArea *victimArea = ToTerrorNavArea( victim->GetLastKnownArea() ), *ownerArea = ToTerrorNavArea( owner->GetLastKnownArea() );
				if ( victimArea && ownerArea && victimArea->HasSpawnAttributes( TERROR_NAV_CHECKPOINT ) != ownerArea->HasSpawnAttributes( TERROR_NAV_CHECKPOINT ) && FindClosedTankClawCheckpointDoor( WorldSpaceCenter() ) ) return false;
				if ( CTerrorPlayer *pinner = victim->GetPounceAttacker() )
				{
					ClearMultiDamage();
					Vector forward;
					AngleVectors( GetLocalAngles(), &forward );
					CTakeDamageInfo info( this, owner, 250.0f, DMG_CLUB );
					const Vector force = GetClawDamageForce( target, direction ); // native getter precedes shoot-position query
					CalculateMeleeDamageForce( &info, force, owner->Weapon_ShootPosition() );
					pinner->DispatchTraceAttack( info, forward, &trace );
					ApplyMultiDamage(); // routes through actual existing Bash/ability/death owners
				}
				if ( CTerrorPlayer *smoker = victim->GetTongueOwner() ) smoker->ReleaseTongue( false );
			}
			owner->SetVelocityModifier( z_attack_movement_penalty.GetFloat() );
			notify = true;
			CSingleUserRecipientFilter filter( owner );
			filter.MakeReliable();
			UserMessageBegin( filter, "Splatter" ); WRITE_BYTE( 1 ); MessageEnd();
		}
		bool newlyIncapacitated = false;
		if ( damage >= 1.0f )
		{
			const Vector force = GetClawDamageForce( target, direction );
			float forceScale = 1.0f;
			const bool physicsProp = FClassnameIs( target, "prop_physics*" ) || FClassnameIs( target, "prop_car_alarm" );
			if ( physicsProp )
			{
				DispatchParticleEffect( "tank_object_throw", target->GetAbsOrigin(), target->GetAbsAngles() );
				forceScale = GetPhysicsPropForceScale( target );
				if ( CPhysicsProp *prop = dynamic_cast< CPhysicsProp * >( target ) ) prop->SetPhysicsAttacker( owner, gpGlobals->curtime + 5.0f ); //19B410; Source protected seam in packet
			}
			const bool wasIncapacitated = victim && victim->IsIncapacitated();
			CTakeDamageInfo info( this, owner, damage, DMG_CLUB );
			CalculateMeleeDamageForce( &info, force, owner->Weapon_ShootPosition(), forceScale );
			ClearMultiDamage();
			target->DispatchTraceAttack( info, direction, &trace );
			ApplyMultiDamage(); // actual Source TakeDamage->Tank32E7A0 and survivor incap/death chain
			if ( physicsProp )
			{
				if ( CPhysicsProp *prop = dynamic_cast< CPhysicsProp * >( target ) )
				{
					prop->OnHitByTank(); // genuine196B90 fires PRIVATE output with activator/caller BOTH the prop
					if ( prop->m_takedamage != DAMAGE_YES || prop->GetBreakableType() != 0 || prop->IsPotentiallyAbleToObstructNavAreas() )
					{
						if ( TerrorGameRules()->IsPlayerControlledZombies() ) TheDirector->QueueTankPropRemoval( prop, 40.0f, 60.0f ); //26E230
						else TheDirector->RegisterTankProp( prop ); //26E1E0, same sole Director queue/state
					}
				}
			}
			newlyIncapacitated = !wasIncapacitated && victim && victim->IsIncapacitated();
		}
		if ( notify ) OnTankHitPlayer( victim, newlyIncapacitated );
	}
	CEffectData effect;
	effect.m_vOrigin = trace.endpos;
	effect.m_vStart = trace.startpos;
	effect.m_nSurfaceProp = trace.surface.surfaceProps;
	effect.m_nEntIndex = target->entindex();
	effect.m_nDamageType = DMG_SLASH;
	effect.m_nHitBox = trace.hitbox;
	effect.m_vAngles = owner->GetAbsAngles();
	effect.m_fFlags = 1;
	DispatchEffect( "KnifeSlash", effect );
	if ( victim && IsTankClawSurvivor( victim ) ) owner->ResetFrustration();
	return false; // native hit processing never terminates sweep by its return value
}
void CTankClaw::OnTankHitPlayer( CTerrorPlayer *victim, bool newlyIncapacitated ) // S:3E3A00
{
	CTerrorPlayer *owner = GetTerrorPlayerOwner();
	if ( IsTankClawSurvivor( victim ) )
	{
		int affected = m_swingAffectedEntities.Count();
		if ( m_swingAffectedEntities.Find( victim ) == m_swingAffectedEntities.InvalidIndex() ) ++affected;
		// S3E3A41 uses the survival base-mode predicate for the second victim.
		if ( TerrorGameRules()->IsSurvivalMode() && affected >= 2 ) owner->OnTankPhysicsHitVictim( victim, true );
		else if ( owner->IsBot() && !victim->IsIncapacitated() && victim->GetHealth() < 33 ) owner->OnTankPhysicsHitVictim( victim, newlyIncapacitated );
		else if ( !owner->IsBot() && newlyIncapacitated ) owner->OnTankPhysicsHitVictim( victim, true );
		owner->OnClawHitSurvivorCharacter( victim->GetSurvivorCharacter() ); //3E3ABF exact ECX owner + character argument, SOLE3226B0 recorder
	}
	if ( IsTankClawSurvivor( victim ) && victim->IsIncapacitated() )
	{
		DispatchParticleEffect( "tank_survivor_pound", victim->GetAbsOrigin(), victim->GetAbsAngles() );
		owner->EmitSound( "HulkZombie.PunchIncap" );
		victim->SpeakIfAllowed( "PlayerGroundPoundedByTank", 1 );
	}
	else owner->EmitSound( "HulkZombie.Punch" );
	if ( IsTankClawSurvivor( victim ) && !victim->IsIncapacitated() )
	{
		Vector force = victim->GetAbsOrigin() - owner->GetAbsOrigin();
		force.z = 0.0f;
		TerrorPhysicsNormalize( force );
		force.z = 0.2f;
		TerrorPhysicsNormalize( force );
		if ( victim->GetMoveType() == MOVETYPE_LADDER ) victim->SetMoveType( MOVETYPE_WALK );
		victim->SetGroundEntity( NULL );
		trace_t trace;
		UTIL_TraceHull( victim->GetAbsOrigin(), victim->GetAbsOrigin() + Vector( 0.0f, 0.0f, 18.0f ), victim->WorldAlignMins(), victim->WorldAlignMaxs(), victim->PhysicsSolidMaskForEntity(), victim, COLLISION_GROUP_PLAYER_MOVEMENT, &trace );
		if ( trace.fraction == 1.0f && !trace.allsolid && !trace.startsolid ) victim->SetAbsOrigin( trace.endpos );
		victim->ApplyAbsVelocityImpulse( force * z_tank_throw_force.GetFloat() );
		victim->SetLaunchSource( owner ); //3085C0, existing one-second provenance timer
		victim->DoTerrorAnimationEvent( TERROR_ANIMEVENT_HIT_BY_TANK, 0 );
		victim->ClearUseEntity(); //177560
	}
	m_attackTimer.Start( z_tank_attack_interval.GetFloat() );
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif
