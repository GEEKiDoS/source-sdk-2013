//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/infected/special/boomer_actions.h"
#include "l4d2/infected/boss_zombie_retreat_search.h"
#include "l4d2/infected/boss_zombie_contact.h"
#include "l4d2/infected/special/ability_vomit.h"
#include "l4d2/infected/special/ability_selfdestruct.h"
#include "l4d2/infected/common/infected_path_cost.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/gamerules/terror_mode.h"
#include "l4d2/director/director_scenario.h"
#include "l4d2/nav/terror_nav_area.h"
#include "l4d2/director/zombie_spawn.h"
#include "nav_pathfind.h"
#include "particle_parse.h"
#include "NextBot/NextBotBodyInterface.h"
#include "NextBot/NextBotVisionInterface.h"
#include "NextBot/NextBotManager.h"
#include "NextBot/Player/NextBotPlayerBody.h"
#include "NextBot/Behavior/BehaviorMoveTo.h"
#include "tier0/memdbgon.h"

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif

ConVar z_allow_ai_to_use_abilities( "z_allow_ai_to_use_abilities", "1", 0x00004004, "If zero, Hunters wont pounce, Tanks wont throw rocks, etc" ); // S5A88E0, sole shared owner
ConVar z_attack_flow_range( "z_attack_flow_range", "1500", 0x00004004 ); // S5A8AB0, sole shared owner
ConVar boomer_vomit_delay( "boomer_vomit_delay", "1", 0x00004004, "How long the Boomer waits before he vomits on his target on Normal difficulty" ); // S5A88B0
ConVar boomer_exposed_time_tolerance( "boomer_exposed_time_tolerance", "1.0", 0x00004004, "How long an out-of-range Boomer will tolerate being visible before fleeing" ); // S5A8AE0
extern ConVar z_vomit_range;

// S290150/290040/28F430/28FE50 selects IT entities (native virtual339),
// NOT survivor identity, combat state or the unrelated cover selector.
struct FindNearestBoomerITVictim
{
	Boomer *ignored;
	Vector origin;
	CBaseCombatCharacter *result;
	float rangeSquared;
	explicit FindNearestBoomerITVictim( Boomer *me ) : ignored( me ), origin( me->WorldSpaceCenter() ), result( NULL ), rangeSquared( 100000000.0f ) {}
	bool Inspect( CBaseCombatCharacter *player )
	{
		if ( player != ignored && player->IsAlive() && player->IsIT() )
		{
			Vector delta = player->WorldSpaceCenter() - origin;
			float range = ( delta.x * delta.x + delta.y * delta.y ) + delta.z * delta.z;
			if ( rangeSquared > range ) { rangeSquared = range; result = player; }
		}
		return true;
	}
	bool operator()( INextBot *bot ) { return Inspect( bot->GetEntity() ); }
};
static CBaseCombatCharacter *FindNearestBoomerITVictimTo( Boomer *me )
{
	FindNearestBoomerITVictim find( me );
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( player && player->edict() && player->entindex() && player->IsPlayer() && player->IsConnected() && !player->MyNextBotPointer() ) find.Inspect( player );
	}
	TheNextBots().ForEachBot( find );
	return find.result;
}
static bool IsBoomerVictimFurtherAhead( Boomer *me, CTerrorPlayer *victim ) // S424A30
{
	const float threshold = (float)( me->GetFlowDistance( 0 ) + 500.0 );
	return victim->GetFlowDistance( 0 ) > threshold;
}
static void BoomerChase( Boomer *me, CTerrorPlayer *victim, TerrorDirectChasePath &path )
{
	InfectedPathCost cost( me );
	path.Update( me, victim, cost );
	if ( !path.IsValid() ) me->GetLocomotionInterface()->Approach( victim->GetAbsOrigin(), 1.0f );
}
Action< Boomer > *BoomerBehavior::InitialContainedAction( Boomer *me ) // S42FBC0
{
	return TheDirector->GetScriptedMode()->IsEnabled() && TheDirector->GetDirectorOptionInt( "SpecialInfectedAssault", 0 ) > 0 ?
		static_cast< Action< Boomer > * >( new BoomerAssault ) : static_cast< Action< Boomer > * >( new BoomerAttack );
}
EventDesiredResult< Boomer > BoomerBehavior::OnContact( Boomer *me, CBaseEntity *other, CGameTrace *trace ) // S42FC60
{
	BossZombieContact( me, other, trace, m_lastStuckTime, true );
	return TryContinue();
}
EventDesiredResult< Boomer > BoomerBehavior::OnStuck( Boomer *me ) // S42FC30
{
	m_lastStuckTime = gpGlobals->curtime;
	return TryContinue();
}
BoomerAssault::BoomerAssault() : m_victim( NULL ), m_path( ChasePath::LEAD_SUBJECT ) {} // S42E520
ActionResult< Boomer > BoomerAssault::OnStart( Boomer *me, Action< Boomer > *prior ) // S42E3D0
{
	me->GetBodyInterface()->SetArousal( IBody::INTENSE );
	me->GetBodyInterface()->SetDesiredPosture( IBody::STAND );
	return Continue();
}
EventDesiredResult< Boomer > BoomerAssault::OnContact( Boomer *me, CBaseEntity *other, CGameTrace *trace ) // S42E450
{
	CTerrorPlayer *player = ToTerrorPlayer( other );
	if ( player && player->GetTeamNumber() == TEAM_SURVIVOR && !player->IsIncapacitated() ) m_victim = player;
	return TryToSustain();
}
ActionResult< Boomer > BoomerAssault::Update( Boomer *me, float interval ) // S42E600
{
	IBody *body = me->GetBodyInterface();
	ILocomotion *mover = me->GetLocomotionInterface();
	m_victim = me->SelectVictim( m_victim );
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
	if ( z_allow_ai_to_use_abilities.GetBool() && me->m_nVariantType == 0 && ability->CanActivate() &&
		me->IsRangeLessThan( victim, z_vomit_range.GetFloat() ) && me->GetVisionInterface()->IsAbleToSee( victim, IVision::DISREGARD_FOV ) )
		return SuspendFor( new BoomerVomitOnVictim( victim ), "Launching vomit attack!" );
	if ( me->IsRangeGreaterThan( victim, 30.0f ) ) BoomerChase( me, victim, m_path );
	if ( !mover->IsUsingLadder() && me->IsRangeLessThan( victim, 50.0f ) ) me->PressAltFireButton();
	return Continue();
}
BoomerAttack::BoomerAttack() : m_victim( NULL ), m_path( ChasePath::LEAD_SUBJECT ) {} // S42ED70
ActionResult< Boomer > BoomerAttack::OnStart( Boomer *me, Action< Boomer > *prior ) // S42E9C0
{
	me->GetBodyInterface()->SetArousal( IBody::INTENSE );
	me->GetBodyInterface()->SetDesiredPosture( IBody::STAND );
	m_exposedTimer.Invalidate(); // cover-search timer is intentionally retained on start.
	return Continue();
}
EventDesiredResult< Boomer > BoomerAttack::OnContact( Boomer *me, CBaseEntity *other, CGameTrace *trace ) // S42EA70
{
	CTerrorPlayer *player = ToTerrorPlayer( other );
	if ( player && player->GetTeamNumber() == TEAM_SURVIVOR && !player->IsIncapacitated() &&
		( !m_victim || !m_victim->IsPlayer() || !m_victim->IsIT() ) ) m_victim = player;
	return TryToSustain();
}
EventDesiredResult< Boomer > BoomerAttack::OnCommandAttack( Boomer *me, CBaseEntity *entity ) // S42EB40
{
	CTerrorPlayer *player = ToTerrorPlayer( entity );
	if ( player && player->GetTeamNumber() != TEAM_L4D1_SURVIVOR ) m_victim = player;
	return TryContinue();
}
EventDesiredResult< Boomer > BoomerAttack::OnCommandAssault( Boomer *me ) // S42EBC0
{
	return TryChangeTo( new BoomerAssault, RESULT_CRITICAL, "Assault!" );
}
EventDesiredResult< Boomer > BoomerAttack::OnCommandApproach( Boomer *me, const Vector &goal, float range ) // S42EEB0
{
	return TrySuspendFor( new BehaviorMoveTo< Boomer, InfectedPathCost >( goal ), RESULT_CRITICAL, "Debug move to" );
}
ActionResult< Boomer > BoomerAttack::Update( Boomer *me, float interval ) // S42F3B0
{
	IBody *body = me->GetBodyInterface();
	ILocomotion *mover = me->GetLocomotionInterface();
	m_victim = me->SelectVictim( m_victim );
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
	if ( me->m_nVariantType == 1 && me->GetCustomAbility() && me->GetCustomAbility()->CanActivate() &&
		me->IsRangeLessThan( victim, 60.0f ) && me->GetVisionInterface()->IsAbleToSee( victim, IVision::DISREGARD_FOV ) )
		return SuspendFor( new BoomerSelfDestruct );
	bool exposed = me->ShouldHide() || IsTerrorSpawnEntityVisibleToTeam( me, TEAM_SURVIVOR, 0, 0.0f, ToTerrorNavArea( me->GetLastKnownArea() ) );
	if ( exposed )
	{
		if ( !( m_exposedTimer.GetStartTime() > 0.0f ) ) m_exposedTimer.Start();
	}
	else m_exposedTimer.Invalidate();
	// S16B320 compares the x87 difference without a float spill.
	const bool exposedTooLong = m_exposedTimer.GetStartTime() > 0.0f &&
		static_cast< double >( gpGlobals->curtime ) - m_exposedTimer.GetStartTime() > boomer_exposed_time_tolerance.GetFloat();
	if ( z_allow_ai_to_use_abilities.GetBool() && me->m_nVariantType == 0 && ability->CanActivate() &&
		me->IsRangeLessThan( victim, z_vomit_range.GetFloat() ) && me->GetVisionInterface()->IsAbleToSee( victim, IVision::DISREGARD_FOV ) )
		return SuspendFor( new BoomerVomitOnVictim( victim ), "Launching vomit attack!" );
	if ( me->m_nVariantType != 1 )
	{
		if ( !ability->CanActivate() && !exposed ) return Continue();
		if ( me->m_nVariantType != 1 && exposed && !victim->IsInCombat( false ) )
		{
			if ( m_coverSearchTimer.IsElapsed() )
			{
				const char *reason = NULL;
				if ( exposedTooLong && me->IsRangeGreaterThan( victim, z_vomit_range.GetFloat() + 250.0f ) && !TheDirector->IsFinale() ) reason = "I'm exposed and too far away";
				else if ( !ability->CanActivate() && !FindNearestBoomerITVictimTo( me ) ) reason = "I'm exposed and my vomit is recharging";
				else if ( me->ShouldHide() ) reason = "The Director says its relax time";
				if ( reason )
				{
					const float radius = me->GetRangeTo( victim ) * 1.5f;
					BossZombieRetreatSearch search( me );
					SearchSurroundingAreas( me->GetLastKnownArea(), me->GetAbsOrigin(), search, radius, 0, TEAM_ANY );
					if ( search.GetArea() && search.GetVisibleSurvivorCount() == 0 ) return SuspendFor( new BoomerRetreatToCover( search.GetArea() ), reason );
					m_coverSearchTimer.Start( 1.0f );
				}
			}
			goto chase;
		}
	}
	// Original skip-flow branch is NOT run when the earlier cover decision
	// jumps directly to chase (timer pending, no reason, or failed search).
	if ( !me->IsAggressive() )
	{
		const float range = z_attack_flow_range.GetFloat();
		const float threshold = (float)( me->GetFlowDistance( 0 ) - range );
		if ( threshold > victim->GetFlowDistance( 0 ) ) return Continue();
	}
chase:
	if ( me->IsRangeGreaterThan( victim, 30.0f ) )
	{
		if ( !TheDirector->IsFinale() && !TheDirector->GetScenario()->IsFirstPanicEvent() && !me->IsAggressive() &&
			!IsBoomerVictimFurtherAhead( me, victim ) )
		{
			TerrorNavArea *here = ToTerrorNavArea( me->GetLastKnownArea() );
			const Path::Segment *segment = m_path.GetCurrentGoal();
			if ( here && segment && segment->area && segment->area != here && !here->IsCompletelyVisibleToAnyCharacter() &&
				ToTerrorNavArea( segment->area )->IsCompletelyVisibleToAnyCharacter() && !exposed ) return Continue();
		}
		BoomerChase( me, victim, m_path );
	}
	if ( !mover->IsUsingLadder() && me->IsRangeLessThan( victim, 50.0f ) ) me->PressAltFireButton();
	return Continue();
}
BoomerRetreatToCover::BoomerRetreatToCover( CNavArea *cover ) : m_cover( cover ) {} // S4301F0
ActionResult< Boomer > BoomerRetreatToCover::OnStart( Boomer *me, Action< Boomer > *prior ) // S430530
{
	if ( !m_cover ) return Done( "No escape found!" );
	InfectedPathCost cost( me );
	m_path.Compute( me, m_cover->GetCenter(), cost );
	return Continue();
}
ActionResult< Boomer > BoomerRetreatToCover::Update( Boomer *me, float interval ) // S430390
{
	if ( !m_path.IsValid() ) return Done( "Path not valid" );
	m_path.Update( me );
	ILocomotion *mover = me->GetLocomotionInterface();
	if ( mover->IsStuck() && mover->GetStuckDuration() > 3.0f ) return Done( "Stuck!" );
	if ( IsTerrorSpawnEntityVisibleToTeam( me, TEAM_SURVIVOR, 1, 0.0f, NULL ) )
	{
		if ( !me->ShouldHide() )
		{
			CTerrorPlayer *victim = me->SelectVictim();
			if ( victim && me->IsRangeLessThan( victim, z_vomit_range.GetFloat() ) && me->GetVisionInterface()->IsLineOfSightClearToEntity( victim ) ) return Done( "Victim is close, attack!" );
		}
		return Continue();
	}
	me->SetAbsVelocity( vec3_origin );
	return ChangeTo( new BoomerHide, "Reached opportunistic cover" );
}
EventDesiredResult< Boomer > BoomerRetreatToCover::OnMoveToFailure( Boomer *me, const Path *path, MoveToFailureType reason ) // S430190
{
	if ( reason == FAIL_NO_PATH_EXISTS ) TerrorGameRules()->OnInfectedBotPathFailed( 1.6f, me );
	return TryDone( RESULT_CRITICAL );
}
ActionResult< Boomer > BoomerHide::OnStart( Boomer *me, Action< Boomer > *prior ) // S42FFB0
{
	me->GetLocomotionInterface()->Stop();
	m_hideTimer.Start( RandomFloat( 4.0f, 6.0f ) );
	return Continue();
}
ActionResult< Boomer > BoomerHide::Update( Boomer *me, float interval ) // S430070
{
	if ( IsTerrorSpawnEntityVisibleToTeam( me, TEAM_SURVIVOR, 0, 0.0f, ToTerrorNavArea( me->GetLastKnownArea() ) ) ) return Done( "Exposed while hiding!" );
	if ( gpGlobals->curtime < m_hideTimer.m_timestamp || me->ShouldHide() ) return Continue();
	return Done( "Hiding time is up" );
}
ActionResult< Boomer > BoomerSelfDestruct::OnStart( Boomer *me, Action< Boomer > *prior ) // S4305D0
{
	switch ( TerrorGameRules()->GetDifficultyLevel() )
	{
	case 0: m_detonationTimer.Start( 4.0f ); break;
	case 1: m_detonationTimer.Start( 2.0f ); break;
	case 2: case 3: m_detonationTimer.Start( 1.0f ); break;
	default: break;
	}
	me->EmitInfectedSound( "BoomerZombie.Warn", 0.0f, 0.0f );
	DispatchParticleEffect( "boomer_vomit", PATTACH_POINT_FOLLOW, me, me->LookupAttachment( "leak_rectum" ), true );
	me->GetLocomotionInterface()->Stop();
	me->PressFireButton( 1.0f );
	me->PressCrouchButton( 10.0f );
	return Continue();
}
BoomerVomitOnVictim::BoomerVomitOnVictim( CTerrorPlayer *victim ) : m_victim( victim ) {} // S430B20
ActionResult< Boomer > BoomerVomitOnVictim::OnStart( Boomer *me, Action< Boomer > *prior ) // S4307D0
{
	switch ( TerrorGameRules()->GetDifficultyLevel() )
	{
	case 0: m_reactionTimer.Start( boomer_vomit_delay.GetFloat() * 2.0f ); break;
	case 1: m_reactionTimer.Start( boomer_vomit_delay.GetFloat() ); break;
	case 2: m_reactionTimer.Start( 0.2f ); break;
	case 3: m_reactionTimer.Start( 0.0f ); break;
	default: break;
	}
	m_hasFired = false;
	me->EmitInfectedSound( "BoomerZombie.Warn", 0.0f, 0.0f );
	me->GetLocomotionInterface()->Stop();
	return Continue();
}
ActionResult< Boomer > BoomerVomitOnVictim::Update( Boomer *me, float interval ) // S430890
{
	CTerrorPlayer *victim = m_victim.Get();
	if ( !victim || !victim->IsAlive() ) return Done( "No victim" );
	CBaseAbility *ability = me->GetCustomAbility();
	if ( !ability ) return Done( "No vomit ability!" );
	me->GetBodyInterface()->AimHeadTowards( victim, IBody::IMPORTANT, 1.0f, NULL, "Aiming vomit at victim", true, 0.25f );
	if ( gpGlobals->curtime < m_reactionTimer.m_timestamp ) return Continue();
	if ( m_hasFired ) return ability->IsActive() ? Continue() : Done( "Vomit completed" );
	if ( me->IsRangeGreaterThan( victim, z_vomit_range.GetFloat() ) || !me->GetVisionInterface()->IsAbleToSee( victim, IVision::DISREGARD_FOV ) ) return Done( "Lost my vomit target" );
	if ( ability->CanActivate() && me->GetVisionInterface()->IsLookingAt( static_cast< PlayerBody * >( me->GetBodyInterface() )->GetLookAtPosition(), 0.95f ) )
	{
		me->PressFireButton();
		m_hasFired = true;
	}
	return Continue();
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif
