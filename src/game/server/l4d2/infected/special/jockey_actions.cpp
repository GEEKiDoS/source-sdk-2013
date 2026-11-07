//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/infected/special/jockey_actions.h"
#include "l4d2/infected/boss_zombie_contact.h"
#include "l4d2/infected/common/infected_path_cost.h"
#include "l4d2/infected/special/ability_leap.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/nav/terror_nav_area.h"
#include "NextBot/NextBotUtil.h"
#include "NextBot/NextBotBodyInterface.h"
#include "NextBot/NextBotVisionInterface.h"
#include "NextBot/Behavior/BehaviorMoveTo.h"
#include <math.h>
#include "l4d2/infected/special/jockey_actions_console_server_cvars.inc"
#include "tier0/memdbgon.h"

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif

ConVar z_jockey_leap_range( "z_jockey_leap_range", "200", 0x00004004, "If victim is this close, leap at them" );
ConVar z_jockey_leap_time( "z_jockey_leap_time", "1.0", 0x00004004, "Time allowed between jockey bot leaps." );
ConVar z_jockey_lookahead( "z_jockey_lookahead", "400.0", 0x00004004 );
ConVar z_jockey_ride_scan_distance( "z_jockey_ride_scan_distance", "800.0", 0x00004004 );
ConVar z_jockey_ride_hazard_scan_distance( "z_jockey_ride_hazard_scan_distance", "500.0", 0x00004004 );
ConVar z_jockey_ride_scan_interval( "z_jockey_ride_scan_interval", "3.0", 0x00004004 );
ConVar z_jockey_min_ledge_distance( "z_jockey_min_ledge_distance", "200.0f", 0x00004004, "z-delta required for the jockey to try to run you off a ledge" );
ConVar z_jockey_area_range_factor( "z_jockey_area_range_factor", "2.0", 0x00004004 );
ConVar z_jockey_area_current_factor( "z_jockey_area_current_factor", "1.0", 0x00004004 );
ConVar z_jockey_area_visibility_factor( "z_jockey_area_visibility_factor", "500.0", 0x00004004 );
ConVar z_jockey_area_hazard_bonus( "z_jockey_area_hazard_bonus", "3000.0", 0x00004004 );
ConVar z_jockey_debug( "z_jockey_debug", "0", 0x00004004 );
ConVar jockey_pounce_loft_rate( "jockey_pounce_loft_rate", ".035", 0x00004004, "Jockey adds this angle/distance when pouncing" );
ConVar jockey_pounce_max_loft_angle( "jockey_pounce_max_loft_angle", "45", 0x00004004, "Maximum loft angle for Jockey Pounce angle adjustment" );
ConVar jockey_pounce_air_speed( "jockey_pounce_air_speed", "700", 0x00004004 );

static float JockeyDistance( const Vector &a, const Vector &b )
{
	const Vector d = a - b;
	return sqrtf( ( d.y * d.y + d.z * d.z ) + d.x * d.x );
}

JockeyRidePathCost::JockeyRidePathCost( Jockey *actor, bool riding ) :
	m_actor( actor ), m_locomotion( actor->GetLocomotionInterface() ), m_riding( riding ) {} // S44F720
float JockeyRidePathCost::operator()( CNavArea *area, CNavArea *from, const CNavLadder *ladder,
	const CFuncElevator *elevator, float length ) const // S44F760
{
	if ( ( m_riding && ( ladder || area->GetElevator() || area->HasAttributes( 2 ) ) ) ||
		area->HasAttributes( 1 ) || !m_locomotion->IsAreaTraversable( area ) ) return -1.0f;
	if ( !from ) return 0.0f;
	const float height = from->ComputeAdjacentConnectionHeightChange( area );
	if ( ( m_riding && height > m_locomotion->GetStepHeight() ) ||
		( height > m_locomotion->GetMaxJumpHeight() && !ladder ) ||
		( m_riding && area->HasAttributes( 0x40000 ) ) ) return -1.0f;
	float multiplier = area->HasAttributes( 0x80 ) ? 10.0f : 1.0f;
	TerrorNavArea *terrorArea = ToTerrorNavArea( area );
	if ( terrorArea->IsCompletelyVisibleToAnyCharacter() ) multiplier *= 10.0f;
	else if ( terrorArea->IsPotentiallyVisibleToAnyCharacter() ) multiplier *= 5.0f;
	if ( area->HasAvoidanceObstacle( 18.0f ) ) multiplier *= 20.0f;
	if ( area->HasAttributes( 0x80000 ) ) multiplier *= 10.0f;
	if ( !( length > 0.0f ) ) length = JockeyDistance( area->GetCenter(), from->GetCenter() );
	return (float)( ( fabs( (double)height ) + (double)length ) * (double)multiplier + (double)from->GetCostSoFar() );
}

// S4515A0/S451880. The native search deliberately gives continuous-damage
// areas a positive bonus; it does not run an extra guessed hazard/ledge scan.
class JockeyRideSearch
{
public:
	JockeyRideSearch( Jockey *actor, CNavArea *previous ) : m_actor( actor ), m_area( NULL ), m_previous( previous ), m_score( 0.0f ) {}
	bool operator()( CNavArea *area )
	{
		if ( area == m_actor->GetLastKnownArea() ) return true;
		JockeyRidePathCost cost( m_actor, true );
		if ( cost( area, area->GetParent(), NULL, NULL, 1.0f ) < 0.0f ) return false;
		int visibleCount = 0;
		for ( int character = 0; character < 4; ++character )
		{
			CTerrorPlayer *victim = m_actor->GetJockeyVictim();
			if ( ( !victim || (int)victim->GetSurvivorCharacter() != character ) &&
				ToTerrorNavArea( area )->IsPotentiallyVisibleByCharacter( static_cast< SurvivorCharacterType >( character ) ) ) ++visibleCount;
		}
		CTerrorPlayer *nearest = FindClosestBossSurvivor( area->GetCenter(), true, true );
		float score = z_jockey_area_range_factor.GetFloat() * ( nearest ? JockeyDistance( nearest->GetAbsOrigin(), area->GetCenter() ) : 0.0f );
		if ( m_previous ) score -= z_jockey_area_current_factor.GetFloat() * JockeyDistance( m_previous->GetCenter(), area->GetCenter() );
		if ( area->IsDamaging() ) score += z_jockey_area_hazard_bonus.GetFloat();
		score += (float)( 4 - visibleCount ) * z_jockey_area_visibility_factor.GetFloat();
		if ( z_jockey_debug.GetBool() )
		{
			Vector origin = area->GetCenter();
			origin.z += 3.0f;
			NDebugOverlay::Text( origin, CFmtStr( "%2.2f", score ), false, 3.0f );
		}
		if ( score > m_score )
		{
			m_score = score;
			m_area = area;
		}
		return true;
	}
	CNavArea *GetArea() const { return m_area; }
private:
	Jockey *m_actor;
	CNavArea *m_area;
	CNavArea *m_previous;
	float m_score;
};

static void UpdateJockeyRidePath( Jockey *me, CNavArea *&goal, PathFollower &path, CountdownTimer &scanTimer )
{
	CTerrorPlayer *victim = me->GetJockeyVictim();
	if ( !victim ) return;
	const bool reached = goal && ( victim->GetAbsOrigin() - goal->GetCenter() ).LengthSqr() < 1600.0f;
	if ( !( scanTimer.m_timestamp > 0.0f ) || scanTimer.IsElapsed() || reached || !goal )
	{
		JockeyRideSearch search( me, goal );
		SearchSurroundingAreas( me->GetLastKnownArea(), me->GetAbsOrigin(), search, z_jockey_ride_scan_distance.GetFloat(), 0, TEAM_ANY );
		if ( search.GetArea() )
		{
			JockeyRidePathCost cost( me, true );
			path.Compute( me, search.GetArea()->GetCenter(), cost );
			goal = search.GetArea();
		}
		scanTimer.Start( z_jockey_ride_scan_interval.GetFloat() );
	}
	if ( goal )
	{
		if ( z_jockey_debug.GetBool() )
		{
			CTerrorPlayer *currentVictim = me->GetJockeyVictim();
			if ( currentVictim ) NDebugOverlay::Line( currentVictim->GetAbsOrigin(), goal->GetCenter(), 255, 255, 0, true, 0.2f );
		}
		path.Update( me );
	}
	if ( !( scanTimer.m_timestamp > 0.0f ) ) scanTimer.Start( z_jockey_ride_scan_interval.GetFloat() );
}

// S44F940/S4515D0: framecount, NOT tickcount; changing victim sets frame to0.
static bool CanJockeyLeap( Jockey *me, CTerrorPlayer *victim, int &lastFrame, bool &cached )
{
	if ( lastFrame != gpGlobals->framecount )
	{
		NextBotTraversableTraceFilter filter( me, ILocomotion::EVENTUALLY );
		IBody *body = me->GetBodyInterface();
		const float halfWidth = body->GetHullWidth() * 0.25f;
		const Vector mins( -halfWidth, -halfWidth, me->GetLocomotionInterface()->GetStepHeight() );
		const Vector maxs( halfWidth, halfWidth, body->GetHullHeight() );
		trace_t trace;
		UTIL_TraceHull( me->WorldSpaceCenter(), victim->WorldSpaceCenter(), mins, maxs, body->GetSolidMask(), &filter, &trace );
		cached = !( trace.fraction < 1.0f || trace.allsolid || trace.startsolid );
		lastFrame = gpGlobals->framecount;
	}
	return cached;
}

static void JockeyPunchedEvent( Jockey *me, CBaseEntity *pusher, bool lunging )
{
	if ( !pusher || !pusher->IsPlayer() ) return;
	IGameEvent *event = gameeventmanager->CreateEvent( "jockey_punched" );
	if ( event )
	{
		event->SetInt( "userid", static_cast< CBasePlayer * >( pusher )->GetUserID() );
		event->SetInt( "jockeyuserid", me->GetUserID() );
		event->SetBool( "islunging", lunging );
		gameeventmanager->FireEvent( event );
	}
}

Action< Jockey > *JockeyBehavior::InitialContainedAction( Jockey *me ) // S4529B0
{
	if ( TheDirector->GetScriptedMode()->IsEnabled() && TheDirector->GetDirectorOptionInt( "SpecialInfectedAssault", 0 ) > 0 )
		return new JockeyAssault;
	return new JockeyAttack;
}
EventDesiredResult< Jockey > JockeyBehavior::OnContact( Jockey *me, CBaseEntity *other, CGameTrace *trace ) // S452A50
{
	BossZombieContact( me, other, trace, m_lastStuckTime, false );
	return TryContinue();
}
EventDesiredResult< Jockey > JockeyBehavior::OnStuck( Jockey *me ) // S452A20
{
	m_lastStuckTime = gpGlobals->curtime;
	return TryContinue();
}

JockeyAssault::JockeyAssault() : m_victim( NULL ), m_path( ChasePath::DONT_LEAD_SUBJECT ), m_rideGoal( NULL ) {} // S44FDB0
ActionResult< Jockey > JockeyAssault::OnStart( Jockey *me, Action< Jockey > *prior ) // S44FB90
{
	me->GetBodyInterface()->SetArousal( IBody::INTENSE );
	me->GetBodyInterface()->SetDesiredPosture( IBody::STAND );
	m_leapTestTick = 0;
	m_canLeap = false;
	return Continue();
}
bool JockeyAssault::CanLeapToVictim( Jockey *me ) { return CanJockeyLeap( me, m_victim.Get(), m_leapTestTick, m_canLeap ); }
void JockeyAssault::UpdateRide( Jockey *me ) { UpdateJockeyRidePath( me, m_rideGoal, m_ridePath, m_rideScanTimer ); }
EventDesiredResult< Jockey > JockeyAssault::OnContact( Jockey *me, CBaseEntity *other, CGameTrace *trace ) // S44FC10
{
	CTerrorPlayer *player = ToTerrorPlayer( other );
	if ( player && player->GetTeamNumber() == TEAM_SURVIVOR && !player->IsIncapacitated() ) m_victim = player;
	return TryToSustain();
}
EventDesiredResult< Jockey > JockeyAssault::OnShoved( Jockey *me, CBaseEntity *pusher ) // S44FC80
{
	if ( m_victim && m_victim->IsBusy() ) JockeyPunchedEvent( me, pusher, false );
	return TryToSustain();
}
ActionResult< Jockey > JockeyAssault::Update( Jockey *me, float interval ) // S450EF0
{
	if ( me->GetJockeyVictim() ) { UpdateRide( me ); return Continue(); }
	CTerrorPlayer *selected = me->SelectVictim( m_victim, 4 );
	if ( selected != m_victim.Get() ) m_leapTestTick = 0;
	m_victim = selected;
	if ( !selected || !selected->IsAlive() ) return Continue();
	CBaseAbility *ability = me->GetCustomAbility();
	if ( !ability ) return Done( "No ability!" );
	ILocomotion *mover = me->GetLocomotionInterface();
	JockeyRidePathCost cost( me, false );
	if ( m_path.IsValid() && mover->IsUsingLadder() )
	{
		m_path.Update( me, selected, cost );
		return Continue();
	}
	if ( ability->CanActivate() && me->IsRangeLessThan( selected, z_jockey_leap_range.GetFloat() ) && CanLeapToVictim( me ) )
	{
		me->GetBodyInterface()->AimHeadTowards( selected, IBody::IMPORTANT, 1.0f, NULL, NULL, true, 0.25f );
		return SuspendFor( new JockeyLeap( selected ), "Leaping!" );
	}
	m_path.Update( me, selected, cost );
	if ( !m_path.IsValid() && CanLeapToVictim( me ) )
	{
		me->GetBodyInterface()->AimHeadTowards( selected, IBody::IMPORTANT, 1.0f, NULL, NULL, true, 0.25f );
		mover->Approach( selected->GetAbsOrigin(), 1.0f );
	}
	if ( !mover->IsUsingLadder() && me->IsRangeLessThan( selected, 50.0f ) ) me->PressAltFireButton();
	return Continue();
}

JockeyAttack::JockeyAttack() : m_victim( NULL ), m_path( ChasePath::DONT_LEAD_SUBJECT ), m_rideGoal( NULL ) {} // S452130
ActionResult< Jockey > JockeyAttack::OnStart( Jockey *me, Action< Jockey > *prior ) // S451820
{
	me->GetBodyInterface()->SetArousal( IBody::INTENSE );
	me->GetBodyInterface()->SetDesiredPosture( IBody::STAND );
	m_leapTestTick = 0;
	m_canLeap = false;
	return Continue();
}
bool JockeyAttack::CanLeapToVictim( Jockey *me ) { return CanJockeyLeap( me, m_victim.Get(), m_leapTestTick, m_canLeap ); }
void JockeyAttack::UpdateRide( Jockey *me ) { UpdateJockeyRidePath( me, m_rideGoal, m_ridePath, m_rideScanTimer ); }
EventDesiredResult< Jockey > JockeyAttack::OnContact( Jockey *me, CBaseEntity *other, CGameTrace *trace ) // S451D20
{
	CTerrorPlayer *player = ToTerrorPlayer( other );
	if ( player && player->GetTeamNumber() == TEAM_SURVIVOR && !player->IsIncapacitated() &&
		( !m_victim || !m_victim->IsPlayer() || !m_victim->IsIT() ) ) m_victim = player;
	return TryToSustain();
}
EventDesiredResult< Jockey > JockeyAttack::OnShoved( Jockey *me, CBaseEntity *pusher ) // S451DD0
{
	if ( m_victim && m_victim->IsBusy() ) JockeyPunchedEvent( me, pusher, false );
	return TryToSustain();
}
EventDesiredResult< Jockey > JockeyAttack::OnCommandAttack( Jockey *me, CBaseEntity *other ) // S451F00
{
	CTerrorPlayer *player = ToTerrorPlayer( other );
	if ( player && player->GetTeamNumber() != TEAM_L4D1_SURVIVOR ) m_victim = player;
	return TryContinue();
}
EventDesiredResult< Jockey > JockeyAttack::OnCommandAssault( Jockey *me ) // S451F80
{
	return TryChangeTo( new JockeyAssault, RESULT_CRITICAL, "Assault!" );
}
EventDesiredResult< Jockey > JockeyAttack::OnCommandApproach( Jockey *me, const Vector &goal, float range ) // S452260
{
	return TrySuspendFor( new BehaviorMoveTo< Jockey, InfectedPathCost >( goal ), RESULT_CRITICAL, "Debug move to" );
}
ActionResult< Jockey > JockeyAttack::Update( Jockey *me, float interval ) // S452300
{
	if ( me->GetJockeyVictim() ) { UpdateRide( me ); return Continue(); }
	me->ShouldRetreatToCover(); // Native query result is discarded, not a transition.
	CTerrorPlayer *selected = me->SelectVictim( m_victim, 4 );
	if ( selected != m_victim.Get() ) m_leapTestTick = 0;
	m_victim = selected;
	if ( !selected || !selected->IsAlive() ) return Continue();
	CBaseAbility *ability = me->GetCustomAbility();
	if ( !ability ) return Done( "No ability!" );
	ILocomotion *mover = me->GetLocomotionInterface();
	JockeyRidePathCost cost( me, false );
	if ( m_path.IsValid() && mover->IsUsingLadder() )
	{
		m_path.Update( me, selected, cost );
		return Continue();
	}
	if ( ability->CanActivate() && me->IsRangeLessThan( selected, z_jockey_leap_range.GetFloat() ) && CanLeapToVictim( me ) )
	{
		me->GetBodyInterface()->AimHeadTowards( selected, IBody::IMPORTANT, 1.0f, NULL, NULL, true, 0.25f );
		return SuspendFor( new JockeyLeap( selected ), "Leaping!" );
	}
	m_path.Update( me, selected, cost );
	if ( !m_path.IsValid() && CanLeapToVictim( me ) )
	{
		me->GetBodyInterface()->AimHeadTowards( selected, IBody::IMPORTANT, 1.0f, NULL, NULL, true, 0.25f );
		mover->Approach( selected->GetAbsOrigin(), 1.0f );
	}
	if ( !mover->IsUsingLadder() && me->IsRangeLessThan( selected, 50.0f ) ) me->PressAltFireButton();
	return Continue();
}

JockeyLeap::JockeyLeap( CTerrorPlayer *victim ) : m_victim( victim ), m_goal( vec3_origin ) {} // S4534F0
ActionResult< Jockey > JockeyLeap::OnStart( Jockey *me, Action< Jockey > *prior ) // S452D90
{
	if ( !m_victim && m_goal.x > -0.01f && m_goal.x < 0.01f && m_goal.y > -0.01f && m_goal.y < 0.01f &&
		m_goal.z > -0.01f && m_goal.z < 0.01f ) return Done( "No victim" );
	switch ( TerrorGameRules()->GetDifficultyLevel() )
	{
	case 0: m_reactionTimer.Start( RandomFloat( 0.2f, 0.66000003f ) ); break;
	case 1: m_reactionTimer.Start( RandomFloat( 0.1f, 0.33000001f ) ); break;
	case 2: case 3: m_reactionTimer.Start( 0.1f ); break;
	}
	m_hasFired = false;
	return Continue();
}
ActionResult< Jockey > JockeyLeap::Update( Jockey *me, float interval ) // S452EB0
{
	if ( !m_victim ) return Done( "Lost our victim" );
	if ( !m_reactionTimer.IsElapsed() ) return Continue();
	CBaseAbility *custom = me->GetCustomAbility();
	if ( !custom ) return Done( "No custom ability!" );
	CLeap *ability = dynamic_cast< CLeap * >( custom );
	if ( !ability ) return Done( "Ability isn't leap!" );
	if ( m_hasFired )
	{
		if ( me->IsJockeyRiding() ) return Done( "Attached" );
		if ( me->GetLocomotionInterface()->IsOnGround() && m_landingTimer.IsElapsed() ) return Done( "Landed" );
		return Continue();
	}
	if ( !ability->CanActivate() ) return Done( "Not ready yet" );
	CTerrorPlayer *victim = m_victim.Get();
	if ( victim )
	{
		const float leadTime = JockeyDistance( me->GetAbsOrigin(), victim->GetAbsOrigin() ) * 0.0024999999f;
		const Vector lead = victim->GetAbsVelocity() * leadTime;
		const Vector aim = victim->WorldSpaceCenter() + lead;
		me->GetBodyInterface()->AimHeadTowards( aim, IBody::BORING, 0.0f, NULL, NULL, true, 0.25f );
		ability->SetLeapDirection( aim - me->WorldSpaceCenter() );
		ability->ActivateAbility();
		ability->SetLeapDirection( vec3_origin );
	}
	m_hasFired = true;
	m_reactionTimer.Invalidate();
	m_landingTimer.Start( 0.4f );
	return Continue();
}
EventDesiredResult< Jockey > JockeyLeap::OnInjured( Jockey *me, const CTakeDamageInfo &info ) // S453230
{
	CBaseEntity *attacker = info.GetAttacker();
	if ( ( (unsigned int)info.GetDamageType() & 0x40000000u ) && attacker && attacker->IsPlayer() )
	{
		IGameEvent *event = gameeventmanager->CreateEvent( "jockey_headshot" );
		if ( event )
		{
			event->SetInt( "userid", static_cast< CBasePlayer * >( attacker )->GetUserID() );
			event->SetInt( "jockeyuserid", me->GetUserID() );
			event->SetBool( "islunging", !me->GetLocomotionInterface()->IsOnGround() && ( !m_victim || !m_victim->IsBusy() ) );
			gameeventmanager->FireEvent( event );
		}
	}
	return TryToSustain( RESULT_CRITICAL );
}
EventDesiredResult< Jockey > JockeyLeap::OnShoved( Jockey *me, CBaseEntity *pusher ) // S4533A0
{
	if ( !me->GetLocomotionInterface()->IsOnGround() && ( !m_victim || !m_victim->IsBusy() ) ) JockeyPunchedEvent( me, pusher, true );
	return TryDone( RESULT_CRITICAL );
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif