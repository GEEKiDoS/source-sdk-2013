//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/bots/survivor_bot.h"
#include "l4d2/player/bots/survivor_coordinator_actions.h"
#include "l4d2/player/bots/survivor_interaction.h"
#include "l4d2/player/bots/survivor_legs.h"
#include "l4d2/entities/func_elevator.h"
#include "trains.h"
#include "entitylist.h"
#include "tier0/memdbgon.h"

ActionResult< SurvivorBot > SurvivorElevatorEnter::OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction )
{
	m_buttonWaitTimer.Start();
	return Continue();
}

ActionResult< SurvivorBot > SurvivorElevatorEnter::Update( SurvivorBot *me, float interval )
{
	if ( !me->GetGroundElevator() ) return Done( "No longer on an elevator" );
	ActionResult< SurvivorBot > result = me->TrySaveFriendsInImmediateTrouble();
	if ( !result.IsRequestingChange() ) result = me->TryHealFriends();
	if ( !result.IsRequestingChange() ) result = me->TryReviveFriends();
	if ( !result.IsRequestingChange() ) result = me->TryUseObjects();
	if ( result.IsRequestingChange() )
		return result.m_type == SUSPEND_FOR ? SuspendFor( result.m_action, result.m_reason ) : result;
	if ( me->IsOnElevatorThatIsMoving() )
		return ChangeTo( new SurvivorElevatorRide, "The elevator is moving" );
	if ( !me->GetTeamSituation().AreAllTeammatesBots() || !m_buttonWaitTimer.IsGreaterThen( 5.0f ) )
		return Continue();

	CBaseEntity *elevator = me->GetGroundElevator();
	if ( dynamic_cast< CFuncTrackTrain * >( elevator ) )
	{
		CBaseEntity *button = gEntList.FindEntityByClassnameNearest( "func_button", me->WorldSpaceCenter(), 500.0f );
		if ( !button )
		{
			m_buttonWaitTimer.Reset();
			return Continue();
		}
		if ( me->IsSelf( FindClosestSurvivorPlayer( button->WorldSpaceCenter(), false, true ) ) )
			return SuspendFor( new SurvivorUseObject( button ), "Pressing the button to start the track train" );
		return Continue();
	}

	CFuncElevator *floorElevator = dynamic_cast< CFuncElevator * >( elevator );
	if ( !floorElevator ) return Continue();
	const FloorInfo *floor = floorElevator->GetFloor( floorElevator->GetCurrentFloor() );
	if ( !floor ) return Continue();
	CBaseEntity *button = floor->button.Get();
	if ( !button ) button = gEntList.FindEntityByClassnameNearest( "func_button", me->WorldSpaceCenter(), 500.0f );
	if ( !button ) return Continue();
	if ( me->IsSelf( FindClosestSurvivorPlayer( button->WorldSpaceCenter(), false, true ) ) )
		return SuspendFor( new SurvivorUseObject( button ), "Pressing the button to start the elevator" );
	return Continue();
}

ActionResult< SurvivorBot > SurvivorElevatorRide::OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction )
{
	me->GetIntentionInterface()->PushLegsAction( new SurvivorLegsWait( 0.0f ) );
	return Continue();
}

ActionResult< SurvivorBot > SurvivorElevatorRide::Update( SurvivorBot *me, float interval )
{
	if ( !me->GetGroundElevator() ) return Done( "No longer on an elevator" );
	if ( !me->IsOnElevatorThatIsMoving() ) return Done( "The elevator stopped at its destination" );
	ActionResult< SurvivorBot > result = me->TrySaveFriendsInImmediateTrouble();
	if ( !result.IsRequestingChange() ) result = me->TryHealFriends();
	if ( !result.IsRequestingChange() ) result = me->TryReviveFriends();
	if ( result.IsRequestingChange() )
		return result.m_type == SUSPEND_FOR ? SuspendFor( result.m_action, result.m_reason ) : result;
	return Continue();
}

void SurvivorElevatorRide::OnEnd( SurvivorBot *me, Action< SurvivorBot > *nextAction )
{
	me->GetIntentionInterface()->PopLegsAction();
}
