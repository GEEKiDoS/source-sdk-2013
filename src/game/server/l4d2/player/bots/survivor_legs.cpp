//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/bots/survivor_bot.h"
#include "l4d2/player/bots/survivor_legs.h"
#include "l4d2/player/bots/survivor_bot_path_cost.h"
#include "NextBot/NextBotLocomotionInterface.h"
#include "tier0/memdbgon.h"

// Original server 103B8F30 / 103B90E0.
SurvivorLegsApproach::SurvivorLegsApproach( CBaseEntity *goal ) :
	m_goalEntity( goal ), m_goalPosition( vec3_origin ), m_isEntityGoal( true ),
	m_chasePath( ChasePath::DONT_LEAD_SUBJECT )
{
}

SurvivorLegsApproach::SurvivorLegsApproach( const Vector &goal ) :
	m_goalEntity( NULL ), m_goalPosition( goal ), m_isEntityGoal( false ),
	m_chasePath( ChasePath::DONT_LEAD_SUBJECT )
{
}

// 103B8E10. Compute throttle is deliberately not restarted here.
ActionResult< SurvivorBot > SurvivorLegsApproach::OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction )
{
	m_path.Invalidate();
	m_chasePath.Invalidate();
	m_invalidateTimer.Start( 2.0f );
	return Continue();
}

// 103BA850.
void SurvivorLegsApproach::ComputePathToPosition( SurvivorBot *me )
{
	if ( gpGlobals->curtime >= m_computeTimer.m_timestamp )
	{
		m_computeTimer.Start( 0.5f );
		SurvivorBotPathCost cost( me );
		m_path.Compute( me, m_goalPosition, cost, 0.0f );
	}
}

// 103BAAB0. Approaching a goal does not finish this action: its owner must
// pop the legs stack when its own use/attack/revive conditions are satisfied.
ActionResult< SurvivorBot > SurvivorLegsApproach::Update( SurvivorBot *me, float interval )
{
	const int pace = me->GetDesiredPace();
	if ( pace == 0 )
		return Continue();
	if ( pace == 1 )
		me->PressWalkButton();

	if ( gpGlobals->curtime >= m_invalidateTimer.m_timestamp && !me->GetLocomotionInterface()->IsScrambling() )
	{
		m_path.Invalidate();
		m_chasePath.Invalidate();
		m_invalidateTimer.Reset();
	}
	if ( m_isEntityGoal )
	{
		CBaseEntity *goal = m_goalEntity.Get();
		if ( goal && goal->entindex() != me->entindex() )
		{
			SurvivorBotPathCost cost( me );
			m_chasePath.Update( me, goal, cost, NULL );
		}
	}
	else
	{
		if ( !m_path.IsValid() )
			ComputePathToPosition( me );
		m_path.Update( me );
	}
	return Continue();
}

// 103B8EC0.
ActionResult< SurvivorBot > SurvivorLegsApproach::OnResume( SurvivorBot *me, Action< SurvivorBot > *interruptingAction )
{
	m_path.Invalidate();
	m_chasePath.Invalidate();
	return Continue();
}

// 103B8F00. Native stuck resolution is actor-owned and receives the positional
// path even when this action is chasing an entity; retain that original choice.
EventDesiredResult< SurvivorBot > SurvivorLegsApproach::OnStuck( SurvivorBot *me )
{
	me->ResolveStuckSituation( &m_path );
	return TryToSustain();
}

// Original server 103C1040.
SurvivorLegsWait::SurvivorLegsWait( float duration ) : m_duration( duration )
{
}

// 103C0E50. A zero/negative requested duration means TEN SECONDS, not forever.
ActionResult< SurvivorBot > SurvivorLegsWait::OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction )
{
	if ( m_duration <= 0.0f )
		m_duration = 10.0f;
	if ( m_duration > 0.0f )
		m_waitTimer.Start( m_duration );
	return Continue();
}

// 103C0F00. The positive timestamp gate is observable at negative/zero time.
ActionResult< SurvivorBot > SurvivorLegsWait::Update( SurvivorBot *me, float interval )
{
	if ( m_waitTimer.m_timestamp <= 0.0f || gpGlobals->curtime < m_waitTimer.m_timestamp )
		return Continue();
	return Done( "Wait duration elapsed" );
}

// 103C0F60.
ActionResult< SurvivorBot > SurvivorLegsWait::OnSuspend( SurvivorBot *me, Action< SurvivorBot > *interruptingAction )
{
	return Done();
}

// 103C0F80. A shorter pause must not shorten an existing wait.
EventDesiredResult< SurvivorBot > SurvivorLegsWait::OnCommandPause( SurvivorBot *me, float duration )
{
	if ( duration > m_waitTimer.GetRemainingTime() )
		m_waitTimer.Start( duration );
	return TryToSustain();
}

// 103C1020.
EventDesiredResult< SurvivorBot > SurvivorLegsWait::OnCommandResume( SurvivorBot *me )
{
	return TryDone( RESULT_CRITICAL );
}
