//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/infected/special/hunter_actions.h"
#include "l4d2/infected/boss_zombie_contact.h"
#include "l4d2/infected/boss_zombie_retreat_search.h"
#include "l4d2/infected/common/infected_path_cost.h"
#include "l4d2/infected/base_ability.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/director/director_scenario.h"
#include "l4d2/nav/terror_nav_area.h"
#include "l4d2/director/zombie_spawn.h"
#include "NextBot/NextBotUtil.h"
#include "NextBot/NextBotBodyInterface.h"
#include "NextBot/NextBotVisionInterface.h"
#include "NextBot/Player/NextBotPlayerBody.h"
#include "NextBot/Behavior/BehaviorMoveTo.h"
#include <math.h>
#include "l4d2/infected/special/hunter_actions_console_server_cvars.inc"
#include "tier0/memdbgon.h"

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif

extern ConVar z_allow_ai_to_use_abilities;
extern ConVar z_attack_flow_range;
ConVar z_hunter_lunge_distance( "z_hunter_lunge_distance", "750", 0x00000006, "Distance at which bot hunters will try to lunge", true, 1.0f, false, 5000.0f );
ConVar hunter_pounce_ready_range( "hunter_pounce_ready_range", "500", 0x00004004, "If victim is this close, crouch and get ready" );
ConVar hunter_committed_attack_range( "hunter_committed_attack_range", "75", 0x00004004 );
ConVar hunter_leap_away_give_up_range( "hunter_leap_away_give_up_range", "1000", 0x00004004 );
ConVar hunter_pounce_loft_rate( "hunter_pounce_loft_rate", ".035", 0x00004004, "Hunter adds this angle/distance when pouncing" );
ConVar hunter_pounce_max_loft_angle( "hunter_pounce_max_loft_angle", "45", 0x00004004, "Maximum loft angle for Hunter Pounce angle adjustment" );
ConVar hunter_pounce_air_speed( "hunter_pounce_air_speed", "700", 0x00004004 );

static float HunterDistance( const Vector &a, const Vector &b )
{
	const Vector d = a - b;
	return sqrtf( ( d.y * d.y + d.z * d.z ) + d.x * d.x );
}

static bool HunterVictimRecovering( CTerrorPlayer *victim ) // S43A4D0, sole native11720 timer.
{
	const float deadline = victim->GetIncapacitationRecoveryTimer().m_timestamp.Get();
	return deadline > 0.0f && gpGlobals->curtime < deadline;
}

static void HunterPunchedEvent( Hunter *me, CBaseEntity *pusher, bool lunging )
{
	if ( !pusher || !pusher->IsPlayer() ) return;
	IGameEvent *event = gameeventmanager->CreateEvent( "hunter_punched" );
	if ( event )
	{
		event->SetInt( "userid", static_cast< CBasePlayer * >( pusher )->GetUserID() );
		event->SetInt( "hunteruserid", me->GetUserID() );
		event->SetBool( "islunging", lunging );
		gameeventmanager->FireEvent( event );
	}
}

// S444C24/S445B12: the quarter-width hull skips eventually traversable props.
static bool HunterHasLaunchPath( Hunter *me, CTerrorPlayer *victim )
{
	NextBotTraversableTraceFilter filter( me, ILocomotion::EVENTUALLY );
	IBody *body = me->GetBodyInterface();
	ILocomotion *mover = me->GetLocomotionInterface();
	const float halfWidth = body->GetHullWidth() * 0.25f;
	const Vector mins( -halfWidth, -halfWidth, mover->GetStepHeight() );
	const Vector maxs( halfWidth, halfWidth, body->GetCrouchHullHeight() );
	trace_t trace;
	UTIL_TraceHull( me->GetAbsOrigin(), victim->EyePosition(), mins, maxs, body->GetSolidMask(), &filter, &trace );
	return !trace.DidHit();
}

static void HunterPreparePounce( Hunter *me, CTerrorPlayer *victim )
{
	CTerrorPlayer *nearest = FindClosestBossSurvivor( me->WorldSpaceCenter(), true, true );
	if ( nearest && !nearest->IsBeingCarried() && !nearest->IsPummelVictim() && !nearest->IsTongueVictim() &&
		( ( me->IsRangeLessThan( nearest, hunter_pounce_ready_range.GetFloat() ) &&
			(float)( me->GetFlowDistance() + 500.0 ) >= nearest->GetFlowDistance() ) ||
			( me->IsRangeLessThan( nearest, z_hunter_lunge_distance.GetFloat() ) &&
				me->GetVisionInterface()->IsAbleToSee( victim, IVision::USE_FOV ) ) ) )
		me->PressCrouchButton();
}

static void HunterChaseAndClaw( Hunter *me, CTerrorPlayer *victim, TerrorDirectChasePath &path )
{
	if ( me->IsRangeGreaterThan( victim, 30.0f ) )
	{
		InfectedPathCost cost( me );
		path.Update( me, victim, cost );
		if ( !path.IsValid() ) me->GetLocomotionInterface()->Approach( victim->GetAbsOrigin(), 1.0f );
	}
	if ( !me->GetLocomotionInterface()->IsUsingLadder() && !me->IsPouncing() && me->IsRangeLessThan( victim, 50.0f ) )
		me->PressAltFireButton();
}

Action< Hunter > *HunterBehavior::InitialContainedAction( Hunter *me ) // S445F80
{
	if ( TheDirector->GetScriptedMode()->IsEnabled() && TheDirector->GetDirectorOptionInt( "SpecialInfectedAssault", 0 ) > 0 )
		return new HunterAssault;
	return new HunterAttack;
}
EventDesiredResult< Hunter > HunterBehavior::OnContact( Hunter *me, CBaseEntity *other, CGameTrace *trace ) // S446020
{
	BossZombieContact( me, other, trace, m_lastStuckTime, true );
	return TryContinue();
}
EventDesiredResult< Hunter > HunterBehavior::OnStuck( Hunter *me ) // S445FF0
{
	m_lastStuckTime = gpGlobals->curtime;
	return TryContinue();
}

HunterAssault::HunterAssault() : m_victim( NULL ), m_path( ChasePath::DONT_LEAD_SUBJECT ) {} // S444880
ActionResult< Hunter > HunterAssault::OnStart( Hunter *me, Action< Hunter > *prior ) // S444660
{
	me->GetBodyInterface()->SetArousal( IBody::INTENSE );
	me->GetBodyInterface()->SetDesiredPosture( IBody::STAND );
	return Continue();
}
EventDesiredResult< Hunter > HunterAssault::OnContact( Hunter *me, CBaseEntity *other, CGameTrace *trace ) // S4446E0
{
	CTerrorPlayer *player = ToTerrorPlayer( other );
	if ( player && player->GetTeamNumber() == TEAM_SURVIVOR && !player->IsIncapacitated() ) m_victim = player;
	return TryToSustain( RESULT_TRY );
}
EventDesiredResult< Hunter > HunterAssault::OnInjured( Hunter *me, const CTakeDamageInfo &info ) // S4446C0
{
	return TryToSustain( RESULT_IMPORTANT );
}
EventDesiredResult< Hunter > HunterAssault::OnShoved( Hunter *me, CBaseEntity *pusher ) // S444750
{
	if ( m_victim && m_victim->IsBusy() ) HunterPunchedEvent( me, pusher, false );
	return TryToSustain( RESULT_TRY );
}
ActionResult< Hunter > HunterAssault::Update( Hunter *me, float interval ) // S444960
{
	IBody *body = me->GetBodyInterface();
	ILocomotion *mover = me->GetLocomotionInterface();
	m_victim = me->SelectVictim( m_victim, 4 );
	CTerrorPlayer *victim = m_victim.Get();
	if ( !victim || !victim->IsAlive() ) return Continue();
	CBaseAbility *ability = me->GetCustomAbility();
	if ( !ability ) return Done( "No ability!" );
	if ( m_path.IsValid() && mover->IsUsingLadder() )
	{
		InfectedPathCost cost( me );
		m_path.Update( me, victim, cost );
		return Continue();
	}
	body->AimHeadTowards( victim, IBody::IMPORTANT, 1.0f, NULL, NULL, true, 0.25f );
	if ( z_allow_ai_to_use_abilities.GetBool() )
	{
		HunterPreparePounce( me, victim );
		if ( me->m_Local.m_bDucked && ability->CanActivate() && me->IsRangeLessThan( victim, z_hunter_lunge_distance.GetFloat() ) &&
			me->GetVisionInterface()->IsAbleToSee( victim, IVision::USE_FOV ) && HunterHasLaunchPath( me, victim ) )
			return SuspendFor( new HunterLungeAtVictim( victim ), "Launching Lunge/Pounce attack!" );
	}
	HunterChaseAndClaw( me, victim, m_path );
	return Continue();
}

HunterAttack::HunterAttack() : m_victim( NULL ), m_path( ChasePath::DONT_LEAD_SUBJECT ) {} // S4452D0
ActionResult< Hunter > HunterAttack::OnStart( Hunter *me, Action< Hunter > *prior ) // S444F90
{
	me->GetBodyInterface()->SetArousal( IBody::INTENSE );
	me->GetBodyInterface()->SetDesiredPosture( IBody::STAND );
	return Continue();
}
EventDesiredResult< Hunter > HunterAttack::OnContact( Hunter *me, CBaseEntity *other, CGameTrace *trace ) // S444FF0
{
	CTerrorPlayer *player = ToTerrorPlayer( other );
	if ( player && player->GetTeamNumber() == TEAM_SURVIVOR && !player->IsIncapacitated() &&
		( !m_victim || !m_victim->IsPlayer() || !m_victim->IsIT() ) ) m_victim = player;
	return TryToSustain( RESULT_TRY );
}
EventDesiredResult< Hunter > HunterAttack::OnInjured( Hunter *me, const CTakeDamageInfo &info ) // S445490
{
	if ( ( info.GetDamageType() & DMG_BURN ) || ( m_victim && me->IsRangeGreaterThan( m_victim, hunter_committed_attack_range.GetFloat() ) ) )
		return TryToSustain( RESULT_IMPORTANT );
	return TrySuspendFor( new HunterLeapToCover, RESULT_IMPORTANT, "Ouch! Fleeing to cover!" );
}
EventDesiredResult< Hunter > HunterAttack::OnShoved( Hunter *me, CBaseEntity *pusher ) // S445570
{
	if ( m_victim && m_victim->IsBusy() ) HunterPunchedEvent( me, pusher, false );
	return TrySuspendFor( new HunterLeapToCover, RESULT_IMPORTANT, "Ooof! Fleeing to cover!" );
}
EventDesiredResult< Hunter > HunterAttack::OnCommandAttack( Hunter *me, CBaseEntity *other ) // S4450A0
{
	CTerrorPlayer *player = ToTerrorPlayer( other );
	if ( player && player->GetTeamNumber() != TEAM_L4D1_SURVIVOR ) m_victim = player;
	return TryContinue( RESULT_TRY );
}
EventDesiredResult< Hunter > HunterAttack::OnCommandAssault( Hunter *me ) // S445120
{
	return TryChangeTo( new HunterAssault, RESULT_CRITICAL, "Assault!" );
}
EventDesiredResult< Hunter > HunterAttack::OnCommandApproach( Hunter *me, const Vector &goal, float range ) // S4456B0
{
	return TrySuspendFor( new BehaviorMoveTo< Hunter, InfectedPathCost >( goal ), RESULT_CRITICAL, "Debug move to" );
}
ActionResult< Hunter > HunterAttack::Update( Hunter *me, float interval ) // S445700
{
	IBody *body = me->GetBodyInterface();
	ILocomotion *mover = me->GetLocomotionInterface();
	m_victim = me->SelectVictim( m_victim, 4 );
	CTerrorPlayer *victim = m_victim.Get();
	if ( !victim || !victim->IsAlive() ) return Continue();
	CBaseAbility *ability = me->GetCustomAbility();
	if ( !ability ) return Done( "No ability!" );
	if ( m_path.IsValid() && mover->IsUsingLadder() )
	{
		InfectedPathCost cost( me );
		m_path.Update( me, victim, cost );
		return Continue();
	}
	body->AimHeadTowards( victim, IBody::IMPORTANT, 1.0f, NULL, NULL, true, 0.25f );
	if ( !IsTerrorSpawnEntityVisibleToTeam( me, TEAM_SURVIVOR, 0, 0.0f, NULL ) && !( me->GetFlags() & FL_ONFIRE ) && !me->IsAggressive() )
	{
		const float flow = (float)( me->GetFlowDistance() - z_attack_flow_range.GetFloat() );
		if ( flow > victim->GetFlowDistance() ) return Continue();
	}
	if ( HunterVictimRecovering( victim ) ) return SuspendFor( new HunterLeapToCover, "Let the survivor get back up!" );
	if ( me->ShouldRetreatToCover() ) return SuspendFor( new HunterLeapToCover, "Director told me to retreat!" );
	if ( z_allow_ai_to_use_abilities.GetBool() )
	{
		HunterPreparePounce( me, victim );
		if ( me->m_Local.m_bDucked && ability->CanActivate() && me->IsRangeLessThan( victim, z_hunter_lunge_distance.GetFloat() ) &&
			me->GetVisionInterface()->IsAbleToSee( victim, IVision::USE_FOV ) && HunterHasLaunchPath( me, victim ) )
			return SuspendFor( new HunterLungeAtVictim( victim ), "Launching Lunge/Pounce attack!" );
	}
	if ( me->IsRangeGreaterThan( victim, 30.0f ) && !victim->IsInCombat( false ) && !( me->GetFlags() & FL_ONFIRE ) &&
		!TheDirector->IsFinale() && !TheDirector->GetScenario()->IsFirstPanicEvent() && !me->IsAggressive() && !me->IsVictimFarAhead( victim ) )
	{
		TerrorNavArea *here = ToTerrorNavArea( me->GetLastKnownArea() );
		const Path::Segment *goal = m_path.GetCurrentGoal();
		if ( here && goal && goal->area && goal->area != here && !here->IsPotentiallyVisibleToAnyCharacter() &&
			ToTerrorNavArea( goal->area )->IsPotentiallyVisibleToAnyCharacter() &&
			!IsTerrorSpawnEntityVisibleToTeam( me, TEAM_SURVIVOR, 2, 0.0f, NULL ) ) return Continue();
	}
	HunterChaseAndClaw( me, victim, m_path );
	return Continue();
}

HunterLeapToCover::HunterLeapToCover() : m_cover( NULL ), m_hasLeftGround( false ) {} // S4453B0
ActionResult< Hunter > HunterLeapToCover::OnStart( Hunter *me, Action< Hunter > *prior ) // S4466F0
{
	BossZombieRetreatSearch search( me );
	SearchSurroundingAreas( me->GetLastKnownArea(), me->GetAbsOrigin(), search, z_hunter_lunge_distance.GetFloat(), 0, TEAM_ANY );
	if ( !search.GetArea() || search.GetVisibleSurvivorCount() > 0 ) return Done( "No escape found!" );
	m_cover = search.GetArea();
	if ( me->IsDebugging( NEXTBOT_BEHAVIOR ) ) NDebugOverlay::Line( me->GetAbsOrigin(), m_cover->GetCenter(), 255, 255, 0, true, 10.0f );
	m_aimTimer.Invalidate();
	m_hasLeftGround = false;
	return Continue();
}
ActionResult< Hunter > HunterLeapToCover::Update( Hunter *me, float interval ) // S446400
{
	if ( !me->GetCustomAbility() ) return Done( "No ability!" );
	CTerrorPlayer *nearest = FindClosestBossSurvivor( me->WorldSpaceCenter(), true, true );
	if ( nearest )
	{
		if ( me->IsRangeGreaterThan( nearest, hunter_leap_away_give_up_range.GetFloat() ) ) return Done( "Far away" );
		if ( me->IsRangeLessThan( nearest, hunter_committed_attack_range.GetFloat() ) && !HunterVictimRecovering( nearest ) )
			return Done( "Very close - attack!" );
	}
	me->PressBackwardButton();
	if ( me->GetLocomotionInterface()->IsOnGround() )
	{
		if ( m_hasLeftGround ) return Done( "Escaped!" );
		if ( m_aimTimer.IsElapsed() )
		{
			const float distance = HunterDistance( me->GetAbsOrigin(), m_cover->GetCenter() );
			float loft = hunter_pounce_loft_rate.GetFloat() * distance;
			if ( loft > hunter_pounce_max_loft_angle.GetFloat() ) loft = hunter_pounce_max_loft_angle.GetFloat();
			Vector aim = m_cover->GetCenter();
			aim.z = (float)( tan( (double)loft * (double)0.017453292f ) * (double)distance + (double)aim.z );
			static PressFireButtonReply reply;
			me->GetBodyInterface()->AimHeadTowards( aim, IBody::CRITICAL, 1.0f, &reply, "Leap away loft angle", true, 0.25f );
			m_aimTimer.Start( 0.2f );
		}
	}
	else m_hasLeftGround = true;
	return Continue();
}

HunterLungeAtVictim::HunterLungeAtVictim( CTerrorPlayer *victim ) : m_victim( victim ) {} // S447070
ActionResult< Hunter > HunterLungeAtVictim::OnStart( Hunter *me, Action< Hunter > *prior ) // S446840, ASM overrides broken decompiler stack.
{
	CTerrorPlayer *victim = m_victim.Get();
	if ( !victim ) return Done( "No victim" );
	switch ( TerrorGameRules()->GetDifficultyLevel() )
	{
	case 0: m_reactionTimer.Start( RandomFloat( 0.2f, 0.66000003f ) ); break;
	case 1: m_reactionTimer.Start( RandomFloat( 0.1f, 0.33000001f ) ); break;
	case 2: case 3: m_reactionTimer.Start( 0.1f ); break;
	}
	const float distance = HunterDistance( me->GetAbsOrigin(), victim->GetAbsOrigin() );
	float loft = hunter_pounce_loft_rate.GetFloat() * distance;
	if ( loft > hunter_pounce_max_loft_angle.GetFloat() ) loft = hunter_pounce_max_loft_angle.GetFloat();
	const Vector lead = victim->GetAbsVelocity() * ( distance * 0.0020000001f );
	m_goal = victim->GetAbsOrigin() + lead;
	const float loftHeight = (float)( tan( (double)loft * (double)0.017453292f ) * (double)distance );
	Vector aim = victim->GetAbsOrigin() + Vector( lead.x, lead.y, loftHeight );
	me->GetBodyInterface()->AimHeadTowards( aim, IBody::CRITICAL, 1.0f, NULL, "Pounce loft angle", true, 0.25f );
	if ( me->IsDebugging( NEXTBOT_BEHAVIOR ) )
		NDebugOverlay::Circle( m_goal, Vector( 1.0f, 0.0f, 0.0f ), Vector( 0.0f, 1.0f, 0.0f ), 5.0f, 255, 100, 0, 255, true, 10.0f );
	m_hasFired = false;
	m_unused = true;
	return Continue();
}
ActionResult< Hunter > HunterLungeAtVictim::Update( Hunter *me, float interval ) // S446C70
{
	if ( !m_victim ) return Done( "Lost our victim" );
	CBaseAbility *ability = me->GetCustomAbility();
	if ( !ability ) return Done( "No vomit ability!" ); // Native copy/paste reason.
	me->PressCrouchButton();
	if ( m_reactionTimer.IsElapsed() )
	{
		if ( !m_hasFired && ability->CanActivate() )
		{
			me->PressFireButton();
			m_hasFired = true;
			m_reactionTimer.Invalidate();
			m_landingTimer.Start( 1.0f );
		}
		CTerrorPlayer *victim = m_victim.Get();
		if ( victim ) // Standing handle-re-read rule: skip only target-dependent work.
		{
			m_goal = victim->WorldSpaceCenter();
			me->GetLocomotionInterface()->Approach( m_goal, 1.0f );
		}
	}
	if ( CTerrorPlayer *victim = m_victim.Get() ) me->GetBodyInterface()->AimHeadTowards( victim, IBody::BORING, 0.0f, NULL, NULL );
	if ( m_hasFired && m_landingTimer.IsElapsed() && me->GetLocomotionInterface()->IsOnGround() ) return Done( "Landed" );
	return Continue();
}
EventDesiredResult< Hunter > HunterLungeAtVictim::OnInjured( Hunter *me, const CTakeDamageInfo &info ) // S446F00
{
	CBaseEntity *attacker = info.GetAttacker();
	if ( ( (unsigned int)info.GetDamageType() & 0x40000000u ) && attacker && attacker->IsPlayer() )
	{
		IGameEvent *event = gameeventmanager->CreateEvent( "hunter_headshot" );
		if ( event )
		{
			event->SetInt( "userid", static_cast< CBasePlayer * >( attacker )->GetUserID() );
			event->SetInt( "hunteruserid", me->GetUserID() );
			event->SetBool( "islunging", !me->GetLocomotionInterface()->IsOnGround() && ( !m_victim || !m_victim->IsBusy() ) );
			gameeventmanager->FireEvent( event );
		}
	}
	return TryToSustain( RESULT_CRITICAL );
}
EventDesiredResult< Hunter > HunterLungeAtVictim::OnShoved( Hunter *me, CBaseEntity *pusher ) // S4471B0
{
	if ( !me->GetLocomotionInterface()->IsOnGround() && ( !m_victim || !m_victim->IsBusy() ) ) HunterPunchedEvent( me, pusher, true );
	return TryChangeTo( new HunterLeapToCover, RESULT_IMPORTANT, "Shoved during Pounce - trying to escape now" );
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif