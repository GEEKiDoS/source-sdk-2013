//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/bots/survivor_coordinator_actions.h"
#include "l4d2/player/bots/survivor_bot.h"
#include "l4d2/player/bots/survivor_legs.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/nav/terror_nav_area.h"
#include "l4d2/weapons/prop_mounted_gun.h"
#include "l4d2/infected/common/infected.h"
#include "nav_mesh.h"
#include "NextBot/NextBotKnownEntity.h"
#include "tier0/vprof.h"
#include "tier0/memdbgon.h"

extern ConVar sb_dont_shoot;
extern ConVar sb_vomit_blind_time;
extern ConVar sb_allow_shoot_through_survivors;
extern ConVar sb_close_threat_range;
extern ConVar z_witch_wander_hear_radius;
extern ConVar car_alarm_distance;

SurvivorApproachMinigun::SurvivorApproachMinigun( CPropMountedGun *gun ) : m_gun( gun )
{
	m_usedTimer.Invalidate();
}

// S:3A9890/3A9970 use separate process-wide scratch vectors. Neither position
// is the mounted weapon's character-scaled GetUserPosition result.
const Vector &SurvivorApproachMinigun::GetUsePosition()
{
	static Vector position;
	position = m_gun->WorldSpaceCenter();
	Vector forward;
	AngleVectors( m_gun->GetAbsAngles(), &forward );
	position += forward * -30.0f;
	return position;
}

const Vector &SurvivorApproachMinigun::GetAimPosition( SurvivorBot *me )
{
	static Vector position;
	position = m_gun->WorldSpaceCenter();
	Vector forward;
	AngleVectors( m_gun->GetAbsAngles(), &forward );
	position += forward * 30.0f;
	position.z = me->EyePosition().z;
	return position;
}

ActionResult< SurvivorBot > SurvivorApproachMinigun::OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction )
{
	if ( !m_gun.Get() ) return Done();
	me->GetIntentionInterface()->PushLegsAction( new SurvivorLegsApproach( GetUsePosition() ) );
	return Continue();
}

void SurvivorApproachMinigun::OnEnd( SurvivorBot *me, Action< SurvivorBot > *nextAction )
{
	me->GetIntentionInterface()->PopLegsAction();
}

ActionResult< SurvivorBot > SurvivorApproachMinigun::OnSuspend( SurvivorBot *me, Action< SurvivorBot > *interruptingAction )
{
	return Done();
}

EventDesiredResult< SurvivorBot > SurvivorApproachMinigun::OnStuck( SurvivorBot *me )
{
	return TryDone( RESULT_CRITICAL, "Giving up because I'm stuck" );
}

EventDesiredResult< SurvivorBot > SurvivorApproachMinigun::OnInjured( SurvivorBot *me, const CTakeDamageInfo &info )
{
	CBaseEntity *attacker = info.GetAttacker();
	if ( !attacker ) return TryContinue();
	me->GetBodyInterface()->AimHeadTowards( attacker->WorldSpaceCenter(), IBody::IMPORTANT, 2.0f, NULL, "Looking at attacker", true, 0.25f );
	return TryDone( RESULT_CRITICAL );
}

bool SurvivorApproachMinigun::ShouldGiveUp( SurvivorBot *me )
{
	const SurvivorKnownEntity *known = me->GetIntentionInterface()->GetPrimaryKnownThreat();
	if ( known && known->GetEntity() && me->GetIntentionInterface()->IsImmediateThreat( me, known->GetEntity() ) ) return true;
	if ( m_reachabilityTimer.IsElapsed() )
	{
		m_reachabilityTimer.Start( RandomFloat( 1.0f, 2.0f ) );
		if ( !me->IsReachable( m_gun.Get() ) )
		{
			if ( me->IsDebugging( NEXTBOT_BEHAVIOR ) || me->IsDebugging( NEXTBOT_ERRORS ) )
				ConColorMsg( Color( 0, 255, 255 ), "%3.2f: %s giving up, object is UNREACHABLE\n", gpGlobals->curtime, me->GetDebugIdentifier() );
			return true;
		}
	}
	return false;
}

// S:3AA310. Unlike the ordinary object action, this has a 32-unit use point,
// a different look point, settled-head admission and a genuine mounted child.
ActionResult< SurvivorBot > SurvivorApproachMinigun::Update( SurvivorBot *me, float interval )
{
	SurvivorBody *body = me->GetBodyInterface();
	CPropMountedGun *gun = m_gun.Get();
	if ( !gun ) return Done( "The object we wanted to use is gone" );
	if ( gun->GetOwnerEntity() && gun->GetOwnerEntity()->IsPlayer() ) return Done( "The object we wanted to use is owned by someone" );
	if ( ShouldGiveUp( me ) ) return Done( "Situation changed - giving up" );
	if ( me->IsDebugging( NEXTBOT_BEHAVIOR ) )
	{
		NDebugOverlay::Line( me->EyePosition(), gun->WorldSpaceCenter(), 255, 255, 0, true, 0.1f );
		NDebugOverlay::Line( me->EyePosition(), me->EyePosition() + body->GetViewVector() * 100.0f, 255, 0, 0, true, 0.1f );
	}
	if ( m_usedTimer.HasStarted() )
	{
		if ( m_usedTimer.IsElapsed() )
		{
			if ( me->m_usingMountedGun ) return ChangeTo( new L4D1SurvivorMinigunAttack, "Used a minigun, now going to attack with it" );
			return Done( "used the gun, but we're no longer using it" );
		}
		return Continue();
	}
	const Vector &position = GetUsePosition();
	if ( ( 1024.0f <= ( me->EyePosition() - position ).LengthSqr() &&
		1024.0f <= ( me->GetAbsOrigin() - position ).LengthSqr() ) || me->GetLocomotionInterface()->IsScrambling() )
	{
		me->UpdateAreaClear();
		me->ReloadCombatWeapon();
		return Continue();
	}
	body->AimHeadTowards( GetAimPosition( me ), IBody::MANDATORY, 1.0f, NULL, "Look at minigun to use", true, 0.25f );
	if ( body->IsHeadAimingOnTarget() && body->GetHeadSteadyDuration() > 0.2f )
	{
		gun->Use( me, me, USE_ON, 0.0f );
		me->OnPlayerUsed( gun, USE_TOGGLE );
		if ( me->IsDebugging( NEXTBOT_BEHAVIOR ) )
			ConColorMsg( Color( 0, 255, 255 ), "%3.2f: %s used %s\n", gpGlobals->curtime, me->GetDebugIdentifier(), gun->GetClassname() );
		m_usedTimer.Start( 1.0f );
		me->GetIntentionInterface()->ChangeLegsAction( new SurvivorLegsWait );
	}
	return Continue();
}

ActionResult< SurvivorBot > L4D1SurvivorMinigunAttack::OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction )
{
	m_target = NULL;
	m_targetFrame = 0;
	me->GetIntentionInterface()->PushLegsAction( new SurvivorLegsWait );
	return Continue();
}

void L4D1SurvivorMinigunAttack::OnEnd( SurvivorBot *me, Action< SurvivorBot > *nextAction )
{
	me->GetIntentionInterface()->PopLegsAction();
}

EventDesiredResult< SurvivorBot > L4D1SurvivorMinigunAttack::OnSound( SurvivorBot *me, CBaseEntity *source, const Vector &position, KeyValues *keys )
{
	return TryToSustain( RESULT_TRY );
}

// S:3A8A00/3A8620. Selection uses the sole intention vector, not a second
// target list, and preserves the frame-zero cache and first far-target rule.
CBaseCombatCharacter *L4D1SurvivorMinigunAttack::SelectTarget( SurvivorBot *me )
{
	if ( me->IsIT() && !( sb_vomit_blind_time.GetFloat() <= me->m_itTimer.GetElapsedTime() ) ) return NULL;
	if ( m_targetFrame != gpGlobals->framecount )
	{
		Vector forward;
		me->EyeVectors( &forward );
		CBaseCombatCharacter *selected = NULL, *farTarget = NULL;
		float bestScore = FLT_MAX;
		const CUtlVector< SurvivorKnownEntity > &known = me->GetIntentionInterface()->GetKnownThreats();
		FOR_EACH_VEC( known, i )
		{
			CBaseCombatCharacter *candidate = known[i].GetEntity();
			if ( !candidate || !candidate->IsAlive() ) continue;
			if ( me->IsRangeLessThan( candidate, sb_close_threat_range.GetFloat() ) )
			{
				Vector direction = candidate->EyePosition() - me->EyePosition();
				direction.z = 0.0f;
				VectorNormalize( direction );
				float score = 1.0f - ( DotProduct( forward, direction ) + 1.0f ) * 0.5f;
				if ( bestScore > score && me->GetVisionInterface()->IsLineOfSightClearToEntity( candidate ) )
				{
					selected = candidate;
					bestScore = score;
				}
			}
			else if ( !farTarget && me->GetVisionInterface()->IsLineOfSightClearToEntity( candidate ) ) farTarget = candidate;
		}
		m_target = selected ? selected : farTarget;
		m_targetFrame = gpGlobals->framecount;
	}
	return m_target.Get();
}

void L4D1SurvivorMinigunAttack::AimAtTarget( SurvivorBot *me )
{
	CBaseCombatCharacter *target = SelectTarget( me );
	if ( target ) me->GetBodyInterface()->AimHeadTowards( target, IBody::IMPORTANT, 3.0f, NULL, "Aiming at selected target", true, 0.25f );
}

namespace
{
	class MountedBoomerFilter : public INextBotEntityFilter
	{
	public:
		virtual bool IsAllowed( CBaseEntity *entity ) const OVERRIDE
		{
			return entity && entity->IsPlayer() && entity->IsAlive() && entity->GetTeamNumber() == TEAM_INFECTED &&
				static_cast< CTerrorPlayer * >( entity )->GetZombieClass() == 2;
		}
	};

	bool MountedNotFiring( SurvivorBot *me, const char *reason )
	{
		if ( me->IsDebugging( NEXTBOT_BEHAVIOR ) )
			DevMsg( "%3.2f: %s: NOT FIRING because %s\n", gpGlobals->curtime, me->GetDebugIdentifier(), reason );
		return false;
	}
}

// S:3A8BC0. This is intentionally not the ordinary carried-weapon policy:
// no melee range, weapon burst/revive gates, headshot-only point or sidestep.
bool L4D1SurvivorMinigunAttack::FireWeapon( SurvivorBot *me )
{
	VPROF_BUDGET( "L4D1SurvivorMinigunAttack::FireWeapon", "L4D1SurvivorMinigunAttack::FireWeapon" );
	if ( sb_dont_shoot.GetBool() ) return false;
	CBaseCombatCharacter *target = SelectTarget( me );
	if ( !target || !target->IsAlive() ) return MountedNotFiring( me, "target is dead/gone" );
	if ( GetTerrorEntityZombieClass( target ) == 7 )
	{
		Infected *witch = dynamic_cast< Infected * >( target );
		if ( !witch->GetBodyInterface()->IsArousal( IBody::INTENSE ) ) return MountedNotFiring( me, "target Witch is calm" );
	}
	else if ( TheDirector->GetWitchCount() > 0 && IsNearLivingWitch( me, z_witch_wander_hear_radius.GetFloat() + 24.0f ) )
	{
		DevMsg( "%3.2f: %s: NOT FIRING because I am very close to a calm witch.\n", gpGlobals->curtime, me->GetDebugIdentifier() );
		return false;
	}
	Vector eye = me->EyePosition();
	Vector direction = target->WorldSpaceCenter() - eye;
	float range = VectorNormalize( direction );
	float steadyTime;
	if ( range >= 500.0f )
		steadyTime = range <= 3000.0f ? ( range - 500.0f ) * 0.7f * 0.0004f + 0.3f : 1.0f;
	else steadyTime = 0.3f;
	bool closeTarget = range < 150.0f && me->GetVisionInterface()->IsInFieldOfView( target );
	if ( !closeTarget && ( !me->GetBodyInterface()->IsHeadSteady() || steadyTime > me->GetBodyInterface()->GetHeadSteadyDuration() ) )
		return MountedNotFiring( me, "our head hasn't been still long enough" );
	const Vector &size = target->CollisionProp()->OBBSize();
	float width = size.x <= size.y ? size.y : size.x;
	if ( !closeTarget && cos( atan2( width / range, 1.0 ) ) > DotProduct( me->GetBodyInterface()->GetViewVector(), direction ) )
		return MountedNotFiring( me, "our aim is off" );
	if ( !me->IsIncapacitated() )
	{
		CTraceFilterSimple filter( me, COLLISION_GROUP_NONE );
		Vector forward;
		me->EyeVectors( &forward );
		trace_t trace;
		UTIL_TraceHull( me->EyePosition(), eye + forward * range, Vector( -2, -2, -2 ), Vector( 2, 2, 2 ), 0x46004003, &filter, &trace );
		bool safe = true;
		if ( trace.DidHitNonWorldEntity() && trace.m_pEnt->IsPlayer() && trace.m_pEnt->IsAlive() )
		{
			CTerrorPlayer *friendPlayer = static_cast< CTerrorPlayer * >( trace.m_pEnt );
			if ( friendPlayer->GetTeamNumber() == TEAM_SURVIVOR || friendPlayer->GetTeamNumber() == TEAM_L4D1_SURVIVOR )
				safe = sb_allow_shoot_through_survivors.GetBool() ||
					( friendPlayer->IsIncapacitated() && !friendPlayer->m_isHangingFromLedge && !friendPlayer->m_isHangingFromTongue ) ||
					friendPlayer->m_jockeyAttacker.Get() || friendPlayer->m_pummelAttacker.Get() || friendPlayer->m_pounceAttacker.Get() ||
					friendPlayer->GetTongueOwner() || friendPlayer->m_isHangingFromLedge;
		}
		const CUtlVector< EHANDLE > &alarms = TheDirector->GetCarAlarms();
		for ( int i = 0; i < alarms.Count() && safe; ++i )
		{
			CBaseEntity *alarm = alarms[i].Get();
			if ( alarm && me->IsRangeLessThan( alarm, car_alarm_distance.GetFloat() * 1.1f ) )
			{
				Vector toAlarm = alarm->WorldSpaceCenter() - me->EyePosition();
				VectorNormalize( toAlarm );
				if ( DotProduct( toAlarm, forward ) > 0.8f )
				{
					CNavArea *alarmArea = TheNavMesh->GetNearestNavArea( alarm, 5, 10000.0f );
					CNavArea *myArea = me->GetLastKnownArea();
					if ( alarmArea && myArea && myArea->IsPotentiallyVisible( alarmArea ) )
					{
						safe = false;
						MountedNotFiring( me, "we might hit the car alarm" );
					}
				}
			}
		}
		if ( !me->IsIT() )
		{
			MountedBoomerFilter boomerFilter;
			CTerrorPlayer *boomer = ToTerrorPlayer( me->GetVisionInterface()->GetClosestKnown( boomerFilter ) );
			if ( boomer && me->GetVisionInterface()->IsLookingAt( boomer, 0.8f ) && !IsSafeToPopBoomer( boomer ) )
				return MountedNotFiring( me, "we would pop the Boomer on a survivor" );
		}
		if ( !safe ) return false;
	}
	if ( !me->GetVisionInterface()->IsLineOfSightClearToEntity( target ) ) return false;
	me->PressFireButton( RandomFloat( 0.1f, 0.15f ) );
	if ( me->IsDebugging( NEXTBOT_BEHAVIOR ) ) DevMsg( "%3.2f: %s: CONTINUOUS FIRE!\n", gpGlobals->curtime, me->GetDebugIdentifier() );
	return true;
}

ActionResult< SurvivorBot > L4D1SurvivorMinigunAttack::Update( SurvivorBot *me, float interval )
{
	if ( !me->m_usingMountedGun ) return Done();
	CBaseCombatCharacter *target = SelectTarget( me );
	if ( target && ( me->IsDebugging( NEXTBOT_BEHAVIOR ) || me->IsDebugging( NEXTBOT_LOOK_AT ) ) )
		NDebugOverlay::HorzArrow( me->WorldSpaceCenter(), target->WorldSpaceCenter(), 2.0f, 255, 0, 0, 255, true, 0.01023f );
	AimAtTarget( me );
	if ( target )
	{
		if ( me->GetIntentionInterface()->IsThreatDangerous( me, target ) ) FireWeapon( me );
		else MountedNotFiring( me, "threat is not dangerous" );
	}
	return Continue();
}
