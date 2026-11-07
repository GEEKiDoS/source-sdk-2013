//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/infected/special/smoker_actions.h"
#include "l4d2/infected/special/ability_tongue.h"
#include "l4d2/infected/boss_zombie_contact.h"
#include "l4d2/infected/boss_zombie_retreat_search.h"
#include "l4d2/infected/common/infected_path_cost.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/nav/terror_nav_area.h"
#include "l4d2/director/zombie_spawn.h"
#include "nav_pathfind.h"
#include "NextBot/Behavior/BehaviorMoveTo.h"
#include "NextBot/NextBotBodyInterface.h"
#include "NextBot/NextBotLocomotionInterface.h"
#include "NextBot/NextBotVisionInterface.h"
#include "tier0/memdbgon.h"

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif

extern ConVar z_allow_ai_to_use_abilities;
extern ConVar z_attack_flow_range;
extern ConVar tongue_range;
ConVar Smoker_escape_range( "Smoker_escape_range", "750", 0x00004004 ); // S5A9680
ConVar smoker_tongue_delay( "smoker_tongue_delay", "1.5", 0x00004004 ); // S5A96D0

// S45BA10/45BA50, not InfectedPathCost's generic infected penalties.
SmokerPathCost::SmokerPathCost( Smoker *actor, CTerrorPlayer *victim ) :
	m_actor( actor ), m_locomotion( actor->GetLocomotionInterface() ), m_victim( victim ) {}
float SmokerPathCost::operator()( CNavArea *area, CNavArea *from, const CNavLadder *ladder,
	const CFuncElevator *elevator, float length ) const
{
	if ( !m_locomotion->IsAreaTraversable( area ) ) return -1.0f;
	if ( !from ) return 0.0f;
	float multiplier = 1.0f;
	if ( m_victim )
	{
		Vector closest;
		area->GetClosestPointOnArea( m_victim->GetAbsOrigin(), &closest );
		if ( !( closest.z > m_victim->GetAbsOrigin().z + 100.0f ) ) multiplier = 2.0f;
	}
	const TerrorNavArea *terrorArea = ToTerrorNavArea( area );
	if ( terrorArea->IsPotentiallyVisibleToAnyCharacter() ) multiplier *= 2.0f;
	float distance;
	if ( ladder )
	{
		float down = from->GetCenter().z > area->GetCenter().z ? 3.0f : 1.0f;
		distance = ladder->m_length * down;
	}
	else
	{
		float dz = from->ComputeAdjacentConnectionHeightChange( area );
		if ( dz > m_locomotion->GetMaxJumpHeight() ) return -1.0f;
		distance = length;
		if ( !( distance > 0.0f ) )
		{
			const Vector delta = area->GetCenter() - from->GetCenter();
			distance = sqrtf( ( delta.y * delta.y + delta.z * delta.z ) + delta.x * delta.x );
		}
		if ( dz < 0.0f ) dz *= 2.0f;
		else if ( dz > 0.0f ) dz = 0.0f;
		distance += fabsf( dz );
		if ( terrorArea->IsCompletelyVisibleToAnyCharacter() ) distance *= 10.0f;
		else if ( terrorArea->IsPotentiallyVisibleToAnyCharacter() ) distance *= 5.0f;
	}
	return static_cast< float >( static_cast< double >( distance ) * multiplier + from->GetCostSoFar() );
}

// S45CB30: assault is a scripted option, not the aggression predicate.
Action< Smoker > *SmokerBehavior::InitialContainedAction( Smoker *me )
{
	if ( TheDirector->GetScriptedMode()->IsEnabled() && TheDirector->GetDirectorOptionInt( "SpecialInfectedAssault", 0 ) > 0 )
		return new SmokerAssault;
	return new SmokerAttack;
}
EventDesiredResult< Smoker > SmokerBehavior::OnContact( Smoker *me, CBaseEntity *other, CGameTrace *trace )
{
	BossZombieContact( me, other, trace, m_lastStuckTime, true );
	return TryContinue();
}
EventDesiredResult< Smoker > SmokerBehavior::OnStuck( Smoker *me )
{
	m_lastStuckTime = gpGlobals->curtime;
	return TryContinue();
}
SmokerAssault::SmokerAssault() : m_victim( NULL ), m_path( ChasePath::LEAD_SUBJECT ) {}
ActionResult< Smoker > SmokerAssault::OnStart( Smoker *me, Action< Smoker > *prior )
{
	me->GetBodyInterface()->SetArousal( IBody::INTENSE );
	me->GetBodyInterface()->SetDesiredPosture( IBody::STAND );
	return Continue();
}
// S45BE80. Ability target readiness and cooldown admissibility are independent.
ActionResult< Smoker > SmokerAssault::Update( Smoker *me, float interval )
{
	m_victim = me->SelectVictim( m_victim, 4 );
	CTerrorPlayer *victim = m_victim;
	if ( !victim || !victim->IsAlive() ) return Continue();
	CBaseAbility *ability = me->GetCustomAbility();
	if ( !ability ) return Done( "No ability!" );
	ILocomotion *locomotion = me->GetLocomotionInterface();
	if ( m_path.IsValid() && locomotion->IsUsingLadder() )
	{
		SmokerPathCost cost( me, victim );
		m_path.Update( me, victim, cost );
		return Continue();
	}
	me->GetBodyInterface()->AimHeadTowards( victim, IBody::IMPORTANT, 1.0f );
	if ( ability->IsReadyToActivate() && ability->CanActivate() && z_allow_ai_to_use_abilities.GetBool() )
		return SuspendFor( new SmokerTongueVictim( victim ), "Victim is close, attack!" );
	if ( me->IsRangeGreaterThan( victim, tongue_range.GetFloat() * 0.89999998f ) ||
		!me->GetVisionInterface()->IsAbleToSee( victim, IVision::DISREGARD_FOV ) )
	{
		InfectedPathCost cost( me );
		m_path.Update( me, victim, cost );
		if ( !m_path.IsValid() ) locomotion->Approach( victim->GetAbsOrigin() );
	}
	if ( !locomotion->IsUsingLadder() && me->IsRangeLessThan( victim, 50.0f ) ) me->PressAltFireButton();
	return Continue();
}
EventDesiredResult< Smoker > SmokerAssault::OnContact( Smoker *me, CBaseEntity *other, CGameTrace *trace )
{
	CTerrorPlayer *victim = ToTerrorPlayer( other );
	if ( victim && victim->GetTeamNumber() == TEAM_SURVIVOR && !victim->IsIncapacitated() ) m_victim = victim;
	return TryToSustain();
}
SmokerAttack::SmokerAttack() : m_victim( NULL ), m_path( ChasePath::LEAD_SUBJECT ) {}
ActionResult< Smoker > SmokerAttack::OnStart( Smoker *me, Action< Smoker > *prior )
{
	me->GetBodyInterface()->SetArousal( IBody::INTENSE );
	me->GetBodyInterface()->SetDesiredPosture( IBody::STAND );
	return Continue();
}
// S45C780: no opportunistic generic chase/melee is added to this action.
ActionResult< Smoker > SmokerAttack::Update( Smoker *me, float interval )
{
	m_victim = me->SelectVictim( m_victim, 4 );
	CTerrorPlayer *victim = m_victim;
	if ( !victim || !victim->IsAlive() ) return Continue();
	CBaseAbility *ability = me->GetCustomAbility();
	if ( !ability ) return Done( "No ability!" );
	if ( m_path.IsValid() && me->GetLocomotionInterface()->IsUsingLadder() )
	{
		SmokerPathCost cost( me, victim );
		m_path.Update( me, victim, cost );
		return Continue();
	}
	me->GetBodyInterface()->AimHeadTowards( victim, IBody::IMPORTANT, 1.0f );
	if ( IsTerrorSpawnEntityVisibleToTeam( me, TEAM_SURVIVOR ) || me->IsAggressive() ||
		!( me->GetFlowDistance() - z_attack_flow_range.GetFloat() > victim->GetFlowDistance() ) )
	{
		if ( ability->CanActivate() && z_allow_ai_to_use_abilities.GetBool() && !me->ShouldRetreatToCover() )
			return SuspendFor( new SmokerMoveToAttackPosition( victim ), "Moving into attack position..." );
		if ( IsTerrorSpawnEntityVisibleToTeam( me, TEAM_SURVIVOR, 1 ) )
			return SuspendFor( new SmokerRetreatToCover );
	}
	return Continue();
}
EventDesiredResult< Smoker > SmokerAttack::OnContact( Smoker *me, CBaseEntity *other, CGameTrace *trace )
{
	CTerrorPlayer *victim = ToTerrorPlayer( other );
	if ( victim && victim->GetTeamNumber() == TEAM_SURVIVOR && !victim->IsIncapacitated() &&
		( !m_victim || !m_victim->IsPlayer() || !m_victim->IsIT() ) ) m_victim = victim;
	return TryToSustain();
}
EventDesiredResult< Smoker > SmokerAttack::OnCommandAttack( Smoker *me, CBaseEntity *victim )
{
	if ( victim->IsPlayer() && victim->GetTeamNumber() != TEAM_L4D1_SURVIVOR ) m_victim = ToTerrorPlayer( victim );
	return TryContinue();
}
EventDesiredResult< Smoker > SmokerAttack::OnCommandAssault( Smoker *me )
{
	return TryChangeTo( new SmokerAssault, RESULT_CRITICAL, "Assault!" );
}
EventDesiredResult< Smoker > SmokerAttack::OnCommandApproach( Smoker *me, const Vector &goal, float range )
{
	return TrySuspendFor( new BehaviorMoveTo< Smoker, InfectedPathCost >( goal ), RESULT_CRITICAL, "Debug move to" );
}

// S45EBD0. The nearest selector retains disabled survivors and ignores rescue NAV.
ActionResult< Smoker > SmokerRetreatToCover::OnStart( Smoker *me, Action< Smoker > *prior )
{
	BossZombieRetreatSearch search( me );
	SearchSurroundingAreas( me->GetLastKnownArea(), me->GetAbsOrigin(), search, Smoker_escape_range.GetFloat(), 0, TEAM_ANY );
	if ( !search.GetArea() ) return Done( "No escape found!" );
	CTerrorPlayer *victim = FindClosestBossSurvivor( me->WorldSpaceCenter(), true, true, me );
	SmokerPathCost cost( me, victim );
	m_path.Compute( me, search.GetArea()->GetCenter(), cost );
	return Continue();
}
ActionResult< Smoker > SmokerRetreatToCover::Update( Smoker *me, float interval )
{
	m_path.IsValid(); // Native45E9D0 calls and discards this query before Update.
	m_path.Update( me );
	IsTerrorSpawnEntityVisibleToTeam( me, TEAM_SURVIVOR, 1 ); // Native observable visibility query is not a gate.
	CTerrorPlayer *victim = me->SelectVictim();
	CBaseAbility *ability = me->GetCustomAbility();
	if ( victim && ability && ability->CanActivate() && me->IsRangeLessThan( victim, tongue_range.GetFloat() ) &&
		me->GetVisionInterface()->IsLineOfSightClearToEntity( victim ) )
		return Done( "Opportunistic attack possible, giving up" );
	return Continue();
}
EventDesiredResult< Smoker > SmokerRetreatToCover::OnMoveToFailure( Smoker *me, const Path *path, MoveToFailureType reason )
{
	if ( reason == FAIL_NO_PATH_EXISTS ) TerrorGameRules()->OnInfectedBotPathFailed( 1.6f, me );
	return TryDone( RESULT_CRITICAL, "Path failed" );
}

// S45D310: positive elevation above the victim is the selection weight.
class SmokerAttackAreaCollector
{
public:
	SmokerAttackAreaCollector( Smoker *actor, CTerrorPlayer *victim ) :
		m_actor( actor ), m_start( actor->GetLastKnownArea() ), m_victim( victim ),
		m_target( victim->GetLastKnownArea() ), m_totalWeight( 0.0f ) {}
	bool operator()( CNavArea *area )
	{
		if ( !m_start || !m_target ) return true;
		Vector from, to;
		area->GetClosestPointOnArea( m_target->GetCenter(), &from );
		m_target->GetClosestPointOnArea( area->GetCenter(), &to );
		float range = tongue_range.GetFloat() * 0.89999998f;
		if ( !area->IsCompletelyVisible( m_target ) || ( from - to ).LengthSqr() > range * range ) return true;
		InfectedPathCost cost( m_actor );
		if ( !( cost( area, area->GetParent(), NULL, NULL, 1.0f ) < 0.0f ) && area->GetCenter().z > m_victim->GetAbsOrigin().z + 18.0f )
		{
			WeightedArea entry;
			entry.area = area;
			entry.weight = area->GetCenter().z - m_victim->GetAbsOrigin().z;
			m_areas.AddToTail( entry );
			m_totalWeight += entry.weight;
		}
		return true;
	}
	CNavArea *Choose()
	{
		if ( m_areas.Count() == 0 ) return NULL;
		float choice = RandomFloat( 0.0f, m_totalWeight - 1.0f );
		FOR_EACH_VEC( m_areas, i )
		{
			choice -= m_areas[i].weight;
			if ( choice <= 0.0f ) return m_areas[i].area;
		}
		return NULL;
	}
private:
	struct WeightedArea { CNavArea *area; float weight; };
	Smoker *m_actor;
	CNavArea *m_start;
	CTerrorPlayer *m_victim;
	CNavArea *m_target;
	CUtlVector< WeightedArea > m_areas;
	float m_totalWeight;
};
SmokerMoveToAttackPosition::SmokerMoveToAttackPosition( CTerrorPlayer *victim ) : m_victim( victim ) {}
// S45E470; selected area and fallback consume precisely one weighted RNG call.
void SmokerMoveToAttackPosition::ComputePath( Smoker *me )
{
	if ( !m_victim || me->GetLocomotionInterface()->IsScrambling() ) return;
	m_repathTimer.Start( 3.0f );
	SmokerAttackAreaCollector search( me, m_victim );
	SearchSurroundingAreas( me->GetLastKnownArea(), me->GetAbsOrigin(), search, tongue_range.GetFloat() * 1.25f, 0, TEAM_ANY );
	CNavArea *goal = search.Choose();
	if ( !goal )
	{
		if ( me->IsDebugging( NEXTBOT_BEHAVIOR ) )
			ConColorMsg( Color( 255, 255, 0, 255 ), "Can't find a good attack area, moving directly towards victim" );
		goal = m_victim->GetLastKnownArea();
		m_stuckTimer.Start( 2.0f );
	}
	if ( goal )
	{
		if ( me->IsDebugging( NEXTBOT_BEHAVIOR ) ) goal->DrawFilled( 255, 255, 0, 255, 999.0f, true );
		SmokerPathCost cost( me, m_victim );
		m_path.Compute( me, goal->GetCenter(), cost );
	}
}
ActionResult< Smoker > SmokerMoveToAttackPosition::OnStart( Smoker *me, Action< Smoker > *prior )
{
	ComputePath( me );
	return Continue();
}
ActionResult< Smoker > SmokerMoveToAttackPosition::Update( Smoker *me, float interval )
{
	CBaseAbility *ability = me->GetCustomAbility();
	if ( !ability ) return Done( "No ability!" );
	CTerrorPlayer *victim = me->SelectVictim( m_victim );
	if ( victim )
	{
		me->GetBodyInterface()->AimHeadTowards( victim, IBody::IMPORTANT, 1.0f, NULL, "Aiming at victim" );
		if ( ability->IsReadyToActivate() ) return ChangeTo( new SmokerTongueVictim( victim ), "Victim is close, attack!" );
		if ( !m_victim || victim->entindex() != m_victim->entindex() )
			return ChangeTo( new SmokerMoveToAttackPosition( victim ), "Changing to new victim" );
	}
	m_path.Update( me );
	if ( !m_path.IsValid() ) me->SetAbsVelocity( vec3_origin );
	if ( !me->GetLocomotionInterface()->IsScrambling() &&
		( ( m_stuckTimer.HasStarted() && m_stuckTimer.IsElapsed() ) || ( !m_path.IsValid() && m_repathTimer.IsElapsed() ) ) )
		ComputePath( me );
	return Continue();
}
EventDesiredResult< Smoker > SmokerMoveToAttackPosition::OnStuck( Smoker *me )
{
	m_stuckTimer.Start( 0.1f );
	return TryContinue();
}

// S45ECE0/45EDB0. Do not substitute IsTongueExtended for native IsActive:
// CTongue intentionally inherits the genuine false base IsActive3606E0.
ActionResult< Smoker > SmokerTongueVictim::OnStart( Smoker *me, Action< Smoker > *prior )
{
	switch ( TheDirector->GetDifficulty() )
	{
	case 0: m_reactionTimer.Start( smoker_tongue_delay.GetFloat() * 2.0f ); break;
	case 1: m_reactionTimer.Start( smoker_tongue_delay.GetFloat() ); break;
	case 2: case 3: m_reactionTimer.Start( 0.0f ); break;
	}
	m_hasFired = false;
	me->EmitInfectedSound( "SmokerZombie.Warn", 0.0f, 0.0f );
	me->DoTerrorAnimationEvent( TERROR_ANIMEVENT_TONGUE_PREPARE );
	me->GetLocomotionInterface()->Stop();
	return Continue();
}
ActionResult< Smoker > SmokerTongueVictim::Update( Smoker *me, float interval )
{
	if ( !m_victim || !m_victim->IsAlive() ) return Done( "No victim" );
	CBaseAbility *ability = me->GetCustomAbility();
	if ( !ability ) return Done( "No ability!" );
	me->GetBodyInterface()->AimHeadTowards( m_victim, IBody::IMPORTANT, 1.0f, NULL, "Aiming at victim" );
	if ( m_reactionTimer.IsElapsed() )
	{
		if ( m_hasFired )
		{
			if ( !ability->IsActive() ) return Done( "Tongue completed" );
		}
		else
		{
			if ( !ability->IsReadyToActivate() ) return Done( "Lost my target" );
			if ( ability->CanActivate() && ability->IsReadyToActivate() )
			{
				me->PressFireButton();
				m_hasFired = true;
			}
		}
	}
	return Continue();
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif
