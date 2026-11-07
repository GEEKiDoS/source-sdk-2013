//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/infected/special/charger_actions.h"
#include "l4d2/infected/special/ability_charge.h"
#include "l4d2/infected/boss_zombie_contact.h"
#include "l4d2/infected/boss_zombie_retreat_search.h"
#include "l4d2/infected/common/infected_path_cost.h"
#include "l4d2/director/terror_director.h"
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
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif

extern ConVar z_charge_duration;
extern ConVar z_charge_max_speed;

// S208620/S208540 use the body forward and origin-to-origin direction.
// The inherited SDK IsLookingTowards instead checks three eye-relative points.
static bool IsChargerFacingVictim( Charger *me, CTerrorPlayer *victim, float tolerance )
{
	if ( !me || !victim ) return false;
	const Vector &victimOrigin = victim->GetAbsOrigin();
	Vector forward;
	me->GetVectors( &forward, NULL, NULL );
	Vector direction = victimOrigin - me->GetAbsOrigin();
	VectorNormalize( direction );
	const float dot = ( direction.x * forward.x + forward.y * direction.y ) + forward.z * direction.z;
	return dot >= tolerance;
}

// S439380/43A220. The two concrete actions have the same native admission test.
static bool CanChargeVictim( Charger *me, CTerrorPlayer *victim )
{
	float range = ( z_charge_duration.GetFloat() * z_charge_max_speed.GetFloat() ) * 0.75f;
	float actorZ = me->GetAbsOrigin().z;
	float victimZ = victim->GetAbsOrigin().z;
	bool differentHeight = fabs( static_cast< double >( actorZ ) - static_cast< double >( victimZ ) ) > 120.0;
	if ( me->IsRangeGreaterThan( victim, range ) || me->IsRangeLessThan( victim, 72.0f ) ||
		!IsChargerFacingVictim( me, victim, 0.89999998f ) ) return false;
	if ( me->IsChargeBlocked( me->WorldSpaceCenter(), victim->WorldSpaceCenter() ) ) return false;
	trace_t trace;
	UTIL_TraceHull( me->WorldSpaceCenter(), victim->WorldSpaceCenter(), Vector( -20.0f, -20.0f, -20.0f ),
		Vector( 20.0f, 20.0f, 20.0f ), 33701899u, me, COLLISION_GROUP_NONE, &trace );
	if ( trace.fraction == 1.0f || trace.m_pEnt == victim ) return !differentHeight;
	return trace.m_pEnt && trace.m_pEnt->VPhysicsGetObject() &&
		!( trace.m_pEnt->VPhysicsGetObject()->GetMass() > 250.0f ) && !differentHeight;
}
// S43A160, distinct from the absolute120-unit charge-height constraint.
static bool IsSurvivorAboveCharger( Charger *me, CTerrorPlayer *victim )
{
	if ( !( victim->GetAbsOrigin().z - me->GetAbsOrigin().z > 36.0f ) ) return false;
	return ( victim->GetAbsOrigin() - me->GetAbsOrigin() ).Length2DSqr() <= 14400.0f;
}
// S424A30 compares the two actual native flow services; it is not line of sight.
static bool IsVictimFartherAlongFlow( Charger *me, CTerrorPlayer *victim )
{
	if ( !victim ) return false;
	const float minimumFlow = static_cast< float >( me->GetFlowDistance() + 500.0 );
	return victim->GetFlowDistance() > minimumFlow;
}
// Native43A14C/43B5DC use the real melee-button path after a center trace.
static void TryChargerMelee( Charger *me, CTerrorPlayer *victim )
{
	if ( !me->IsRangeLessThan( victim, 36.0f ) || !IsChargerFacingVictim( me, victim, 0.80000001f ) ) return;
	trace_t trace;
	UTIL_TraceLine( me->WorldSpaceCenter(), victim->WorldSpaceCenter(), 33570827u, me, COLLISION_GROUP_NONE, &trace );
	if ( trace.fraction == 1.0f || trace.m_pEnt == victim ) me->PressAltFireButton();
}
Action< Charger > *ChargerBehavior::InitialContainedAction( Charger *me )
{
	if ( TheDirector->GetScriptedMode()->IsEnabled() && TheDirector->GetDirectorOptionInt( "SpecialInfectedAssault", 0 ) > 0 )
		return new ChargerAssault;
	return new ChargerAttack;
}
EventDesiredResult< Charger > ChargerBehavior::OnContact( Charger *me, CBaseEntity *other, CGameTrace *trace )
{
	BossZombieContact( me, other, trace, m_lastStuckTime, true );
	return TryContinue();
}
EventDesiredResult< Charger > ChargerBehavior::OnStuck( Charger *me )
{
	m_lastStuckTime = gpGlobals->curtime;
	return TryContinue();
}
ChargerAssault::ChargerAssault() : m_victim( NULL )
{
	m_unusedTimer.Invalidate();
	m_attackTimer.Invalidate();
	m_offMeshTimer.Invalidate();
}
// S439630; the interval is started, while the off-mesh countdown is invalidated.
ActionResult< Charger > ChargerAssault::OnStart( Charger *me, Action< Charger > *prior )
{
	me->GetBodyInterface()->SetArousal( IBody::INTENSE );
	me->GetBodyInterface()->SetDesiredPosture( IBody::STAND );
	m_offMeshTimer.Invalidate();
	m_startedTimer.Start();
	return Continue();
}
// S439710: pummel target is the existing player owner, not the carry victim.
ActionResult< Charger > ChargerAssault::OnResume( Charger *me, Action< Charger > *interrupt )
{
	if ( me && me->IsPlayer() && me->GetPummelTarget() ) m_attackTimer.Start( 2.0f );
	m_path.Invalidate();
	m_offMeshTimer.Start( 0.1f );
	return Continue();
}
// S439C10, including original lack of waiting-in-cover behavior in assault.
ActionResult< Charger > ChargerAssault::Update( Charger *me, float interval )
{
	ILocomotion *locomotion = me->GetLocomotionInterface();
	if ( m_offMeshTimer.HasStarted() && m_offMeshTimer.IsElapsed() && me->GetLastKnownArea() )
	{
		m_offMeshTimer.Invalidate();
		if ( !me->GetLastKnownArea()->Contains( me->GetAbsOrigin() ) )
			return SuspendFor( new ChargerReturnToNavMesh, "Off the mesh, returning" );
	}
	m_victim = me->SelectVictim( m_victim, 4 );
	CTerrorPlayer *victim = m_victim;
	if ( !victim || !victim->IsAlive() ) { locomotion->Stop(); return Continue(); }
	if ( m_path.IsValid() && locomotion->IsUsingLadder() )
	{
		InfectedPathCost cost( me );
		m_path.Update( me, victim, cost );
		return Continue();
	}
	me->GetBodyInterface()->AimHeadTowards( victim, IBody::IMPORTANT, 1.0f );
	if ( me->IsRangeGreaterThan( victim, 36.0f ) )
	{
		InfectedPathCost cost( me );
		m_path.Update( me, victim, cost );
		if ( !m_path.IsReachable() ) locomotion->Approach( victim->GetAbsOrigin() );
	}
	if ( locomotion->IsUsingLadder() ) return Continue();
	CBaseAbility *ability = me->GetCustomAbility();
	if ( ability && ability->CanActivate() && CanChargeVictim( me, victim ) )
	{
		me->SetWaitingInCover( false );
		m_attackTimer.Start( 3.0f );
		return SuspendFor( new ChargerChargeAtVictim( victim ), "Charging!!" );
	}
	if ( m_attackTimer.IsElapsed() ) TryChargerMelee( me, victim );
	return Continue();
}
EventDesiredResult< Charger > ChargerAssault::OnContact( Charger *me, CBaseEntity *other, CGameTrace *trace )
{
	CTerrorPlayer *victim = ToTerrorPlayer( other );
	if ( victim && victim->GetTeamNumber() == TEAM_SURVIVOR && !victim->IsIncapacitated() ) m_victim = victim;
	return TryToSustain();
}
ChargerAttack::ChargerAttack() : m_victim( NULL )
{
	m_unusedTimer.Invalidate();
	m_attackTimer.Invalidate();
	m_offMeshTimer.Invalidate();
}
ActionResult< Charger > ChargerAttack::OnStart( Charger *me, Action< Charger > *prior )
{
	me->GetBodyInterface()->SetArousal( IBody::INTENSE );
	me->GetBodyInterface()->SetDesiredPosture( IBody::STAND );
	m_offMeshTimer.Invalidate();
	m_startedTimer.Start();
	return Continue();
}
ActionResult< Charger > ChargerAttack::OnResume( Charger *me, Action< Charger > *interrupt )
{
	if ( me && me->IsPlayer() && me->GetPummelTarget() ) m_attackTimer.Start( 2.0f );
	m_path.Invalidate();
	m_offMeshTimer.Start( 0.1f );
	return Continue();
}
// S43AEC0. Cover, survivor get-up, ladder, charge and melee gates are ordered.
ActionResult< Charger > ChargerAttack::Update( Charger *me, float interval )
{
	ILocomotion *locomotion = me->GetLocomotionInterface();
	bool chased = false;
	if ( m_offMeshTimer.HasStarted() && m_offMeshTimer.IsElapsed() && me->GetLastKnownArea() )
	{
		m_offMeshTimer.Invalidate();
		if ( !me->GetLastKnownArea()->Contains( me->GetAbsOrigin() ) )
			return SuspendFor( new ChargerReturnToNavMesh, "Off the mesh, returning" );
	}
	m_victim = me->SelectVictim( m_victim, 4 );
	CTerrorPlayer *victim = m_victim;
	if ( !victim || !victim->IsAlive() ) { locomotion->Stop(); return Continue(); }
	if ( m_path.IsValid() && locomotion->IsUsingLadder() )
	{
		InfectedPathCost cost( me );
		m_path.Update( me, victim, cost );
		return Continue();
	}
	me->GetBodyInterface()->AimHeadTowards( victim, IBody::IMPORTANT, 1.0f );
	if ( me->IsWaitingInCover() && !me->ShouldRetreatToCover() )
	{
		if ( ( me->GetFlags() & 0x08000000 ) || TheDirector->IsFinale() || TheDirector->GetScenario()->IsFirstPanicEvent() ||
			IsTerrorSpawnEntityVisibleToTeam( me, TEAM_SURVIVOR ) || me->IsAggressive() )
			me->SetWaitingInCover( false );
		if ( TheDirector->GetMobSpawnTimeRemaining() < 2.0 || IsVictimFartherAlongFlow( me, victim ) )
			me->SetWaitingInCover( false );
		return Continue();
	}
	const CountdownTimer &recovery = victim->GetIncapacitationRecoveryTimer();
	if ( recovery.m_timestamp > 0.0f && gpGlobals->curtime < recovery.m_timestamp )
		return SuspendFor( new ChargerEvade, "Let the survivor get back up!" );
	if ( me->ShouldRetreatToCover() ) return SuspendFor( new ChargerEvade, "Director told me to retreat!" );
	if ( me->IsRangeGreaterThan( victim, 36.0f ) )
	{
		InfectedPathCost cost( me );
		m_path.Update( me, victim, cost );
		chased = true;
		if ( !m_path.IsReachable() )
		{
			if ( IsSurvivorAboveCharger( me, victim ) ) return SuspendFor( new ChargerEvade );
			locomotion->Approach( victim->GetAbsOrigin() );
		}
	}
	if ( locomotion->IsUsingLadder() ) return Continue();
	CBaseAbility *ability = me->GetCustomAbility();
	if ( ability && ability->CanActivate() && CanChargeVictim( me, victim ) )
	{
		me->SetWaitingInCover( false );
		m_attackTimer.Start( 3.0f );
		return SuspendFor( new ChargerChargeAtVictim( victim ), "Charging!!" );
	}
	if ( !m_attackTimer.IsElapsed() || !me->IsRangeLessThan( victim, 36.0f ) ) return Continue();
	if ( IsChargerFacingVictim( me, victim, 0.80000001f ) )
	{
		trace_t trace;
		UTIL_TraceLine( me->WorldSpaceCenter(), victim->WorldSpaceCenter(), 33570827u, me, COLLISION_GROUP_NONE, &trace );
		if ( trace.fraction == 1.0f || trace.m_pEnt == victim ) me->PressAltFireButton();
		return Continue();
	}
	if ( !chased && IsSurvivorAboveCharger( me, victim ) ) return SuspendFor( new ChargerEvade );
	return Continue();
}
EventDesiredResult< Charger > ChargerAttack::OnContact( Charger *me, CBaseEntity *other, CGameTrace *trace )
{
	CTerrorPlayer *victim = ToTerrorPlayer( other );
	if ( !victim || victim->GetTeamNumber() != TEAM_SURVIVOR || victim->IsIncapacitated() ) return TryToSustain();
	if ( victim->GetAbsOrigin().z > me->EyePosition().z )
		return TrySuspendFor( new ChargerEvade, RESULT_IMPORTANT, "Getting out from under survivor" );
	if ( !m_victim || !m_victim->IsPlayer() || !m_victim->IsIT() ) m_victim = victim;
	return TryToSustain();
}
EventDesiredResult< Charger > ChargerAttack::OnCommandAttack( Charger *me, CBaseEntity *victim )
{
	if ( victim->IsPlayer() && victim->GetTeamNumber() != TEAM_L4D1_SURVIVOR ) m_victim = ToTerrorPlayer( victim );
	return TryContinue();
}
EventDesiredResult< Charger > ChargerAttack::OnCommandAssault( Charger *me )
{
	return TryChangeTo( new ChargerAssault, RESULT_CRITICAL, "Assault!" );
}
EventDesiredResult< Charger > ChargerAttack::OnCommandApproach( Charger *me, const Vector &goal, float range )
{
	return TrySuspendFor( new BehaviorMoveTo< Charger, InfectedPathCost >( goal ), RESULT_CRITICAL, "Debug move to" );
}
// S43A9C0, with no OnStart state or path object added to the original action.
ActionResult< Charger > ChargerReturnToNavMesh::Update( Charger *me, float interval )
{
	CNavArea *area = me->GetLastKnownArea();
	if ( area )
	{
		if ( area->Contains( me->GetAbsOrigin() ) ) return Done();
		me->GetBodyInterface()->AimHeadTowards( area->GetCenter(), IBody::IMPORTANT, 0.0f );
		me->GetLocomotionInterface()->Approach( area->GetCenter() );
	}
	return Continue();
}
ActionResult< Charger > ChargerChargeAtVictim::OnStart( Charger *me, Action< Charger > *prior )
{
	if ( !m_victim ) return Done( "No victim" );
	switch ( TheDirector->GetDifficulty() )
	{
	case 0: m_reactionTimer.Start( RandomFloat( 0.2f, 0.66000003f ) ); break;
	case 1: m_reactionTimer.Start( RandomFloat( 0.1f, 0.33000001f ) ); break;
	case 2: case 3: m_reactionTimer.Start( 0.1f ); break;
	}
	m_hasFired = false;
	return Continue();
}
// S43BB60. Actual CCharge owns charge/carry/pummel; only the fire button is sent.
ActionResult< Charger > ChargerChargeAtVictim::Update( Charger *me, float interval )
{
	if ( !m_victim ) return Done( "Lost our victim" );
	CBaseAbility *ability = me->GetCustomAbility();
	if ( !ability ) return Done( "No ability exists!" );
	if ( m_hasFired && !( me->GetFlags() & FL_FROZEN ) ) return Done( "Finished Charge" );
	if ( IsChargerFacingVictim( me, m_victim, 0.60000002f ) )
		me->GetBodyInterface()->AimHeadTowards( m_victim, IBody::BORING, 0.0f );
	if ( m_reactionTimer.IsElapsed() && !( me->GetCustomAbility() && me->GetCustomAbility()->IsActive() ) &&
		!m_hasFired && ability->CanActivate() )
	{
		me->PressFireButton();
		m_hasFired = true;
		m_reactionTimer.Invalidate();
	}
	return Continue();
}
// S43C170. A merely lower exposure candidate is not sufficient for a Charger.
ActionResult< Charger > ChargerEvade::OnStart( Charger *me, Action< Charger > *prior )
{
	me->GetBodyInterface()->SetArousal( IBody::INTENSE );
	me->GetBodyInterface()->SetDesiredPosture( IBody::STAND );
	BossZombieRetreatSearch search( me );
	SearchSurroundingAreas( me->GetLastKnownArea(), me->GetAbsOrigin(), search, 1800.0f, 0, TEAM_ANY );
	if ( !search.GetArea() || search.GetVisibleSurvivorCount() != 0 ) return Done( "Noplace to evade to!" );
	m_area = search.GetArea();
	InfectedPathCost cost( me );
	m_path.Compute( me, m_area->GetCenter(), cost );
	return Continue();
}
ActionResult< Charger > ChargerEvade::Update( Charger *me, float interval )
{
	if ( !m_path.IsValid() ) return Done( "Path Invalid" );
	m_path.Update( me );
	ILocomotion *locomotion = me->GetLocomotionInterface();
	if ( locomotion->IsStuck() && locomotion->GetStuckDuration() > 3.0f ) return Done( "Stuck!" );
	if ( !IsTerrorSpawnEntityVisibleToTeam( me, TEAM_SURVIVOR, 1 ) )
	{
		me->SetAbsVelocity( vec3_origin );
		me->SetWaitingInCover( true );
		return Done( "Stopping because I reached a hidden place" );
	}
	return Continue();
}
EventDesiredResult< Charger > ChargerEvade::OnCommandAttack( Charger *me, CBaseEntity *victim )
{
	Msg( ">>>>Evading charger ignoring command to attack!\n" );
	return TryContinue();
}
EventDesiredResult< Charger > ChargerEvade::OnCommandApproach( Charger *me, const Vector &goal, float range )
{
	return TrySuspendFor( new BehaviorMoveTo< Charger, InfectedPathCost >( goal ), RESULT_CRITICAL, "Debug move to" );
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif
