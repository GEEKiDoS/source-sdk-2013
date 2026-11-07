//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/bots/survivor_bot.h"
#include "l4d2/player/bots/survivor_legs.h"
#include "l4d2/player/bots/survivor_legs_tuning.h"
#include "l4d2/player/bots/survivor_bot_path_cost.h"
#include "NextBot/NextBotLocomotionInterface.h"
#include "tier0/memdbgon.h"

SurvivorLegsRegroup::SurvivorLegsRegroup( CTerrorPlayer *friendPlayer ) :
	m_friend( friendPlayer ), m_path( ChasePath::DONT_LEAD_SUBJECT )
{
}

// Original server 103BE5B0.
ActionResult< SurvivorBot > SurvivorLegsRegroup::OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction )
{
	m_path.SetMinLookAheadDistance( sb_path_lookahead_range.GetFloat() );
	me->SpeakIfAllowed( "SurvivorBotRegroupWithTeam", 1 );
	m_reuniteTimer.Start();
	return Continue();
}

// 103BE650. An explicit goal is not replaced merely because it died: only
// a missing handle invokes the native closest-reachable-friend selection.
ActionResult< SurvivorBot > SurvivorLegsRegroup::Update( SurvivorBot *me, float interval )
{
	SurvivorTeamSituation &team = me->GetTeamSituation();
	CTerrorPlayer *friendPlayer = m_friend.Get();
	if ( !friendPlayer )
		friendPlayer = team.GetClosestReachableFriend();
	if ( !friendPlayer )
		return Done( "No reachable teammates left alive" );

	const float range = me->IsRushing() ? GetSurvivorRushingRange() :
		static_cast< float >( me->GetSurvivorCharacter() ) * 10.0f + sb_separation_danger_min_range.GetFloat();
	if ( !me->GetLocomotionInterface()->IsUsingLadder() && me->GetLocomotionInterface()->IsOnGround() &&
		me->IsThreatWithinDangerRange( friendPlayer, range ) && team.GetTimeSinceSeenAnyFriend() < 0.5f )
	{
		if ( !me->IsRushing() )
		{
			char modifiers[256];
			V_snprintf( modifiers, sizeof( modifiers ), "TimeToReunite:%f", m_reuniteTimer.GetElapsedTime() );
			me->SpeakIfAllowed( "SurvivorBotHasRegroupedWithTeam", 1, modifiers );
		}
		return Done( "Near our team again" );
	}
	SurvivorBotPathCost cost( me );
	m_path.Update( me, friendPlayer, cost, NULL );
	return Continue();
}

EventDesiredResult< SurvivorBot > SurvivorLegsRegroup::OnStuck( SurvivorBot *me )
{
	me->ResolveStuckSituation( &m_path );
	return TryToSustain();
}

SurvivorLegsCoverOrphan::SurvivorLegsCoverOrphan( CTerrorPlayer *friendPlayer ) :
	m_friend( friendPlayer ), m_path( ChasePath::DONT_LEAD_SUBJECT )
{
}

ActionResult< SurvivorBot > SurvivorLegsCoverOrphan::OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction )
{
	m_path.SetMinLookAheadDistance( sb_path_lookahead_range.GetFloat() );
	m_reuniteTimer.Start();
	return Continue();
}

// Original server 103BE940. The 0.5 and 0.1 second friend-history gates are
// independent of the current IsFriendNearby query and of path reachability.
ActionResult< SurvivorBot > SurvivorLegsCoverOrphan::Update( SurvivorBot *me, float interval )
{
	CTerrorPlayer *friendPlayer = m_friend.Get();
	if ( !friendPlayer )
		return Done( "Orphan is gone" );
	char modifiers[256];
	if ( !friendPlayer->IsAlive() )
	{
		V_snprintf( modifiers, sizeof( modifiers ), "Subject:%s,TimeToReunite:%f", friendPlayer->GetResponseCharacterName(), m_reuniteTimer.GetElapsedTime() );
		me->SpeakIfAllowed( "SurvivorBotOrphanedFriendDiedBeforeReunite", 1, modifiers );
		return Done( "Orphan is dead" );
	}
	if ( !me->GetLocomotionInterface()->IsScrambling() && friendPlayer->GetTimeSinceFriendNearby() < 0.5f && me->IsFriendNearby( friendPlayer ) )
	{
		V_snprintf( modifiers, sizeof( modifiers ), "Subject:%s,TimeToReunite:%f", friendPlayer->GetResponseCharacterName(), m_reuniteTimer.GetElapsedTime() );
		me->SpeakIfAllowed( "SurvivorBotReunitedWithOrphanedFriend", 1, modifiers );
		me->GetBodyInterface()->AimHeadTowards( friendPlayer, IBody::INTERESTING, 1.0f, NULL, "Looking at orphan I've just reunited with", true, 0.25f );
		return Done( "Reunited with orphaned friend" );
	}
	if ( friendPlayer->GetTimeSinceFriendNearby() < 0.1f )
		return Done( "The orphan is no longer an orphan" );
	if ( !me->CanReachSurvivor( friendPlayer ) )
	{
		V_snprintf( modifiers, sizeof( modifiers ), "Subject:%s,TimeToReunite:%f", friendPlayer->GetResponseCharacterName(), m_reuniteTimer.GetElapsedTime() );
		me->SpeakIfAllowed( "SurvivorBotOrphanedFriendBecameUnreachable", 1, modifiers );
		return Done( "The orphan is no longer reachable" );
	}
	SurvivorBotPathCost cost( me );
	m_path.Update( me, friendPlayer, cost, NULL );
	return Continue();
}

EventDesiredResult< SurvivorBot > SurvivorLegsCoverOrphan::OnMoveToFailure( SurvivorBot *me, const Path *path, MoveToFailureType reason )
{
	return TryDone( RESULT_CRITICAL, "Unreachable" );
}

EventDesiredResult< SurvivorBot > SurvivorLegsCoverOrphan::OnStuck( SurvivorBot *me )
{
	me->ResolveStuckSituation( &m_path );
	return TryToSustain();
}

SurvivorLegsCoverFriendInCombat::SurvivorLegsCoverFriendInCombat( CTerrorPlayer *friendPlayer ) :
	m_friend( friendPlayer ), m_path( ChasePath::DONT_LEAD_SUBJECT )
{
}

ActionResult< SurvivorBot > SurvivorLegsCoverFriendInCombat::OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction )
{
	m_path.SetMinLookAheadDistance( sb_path_lookahead_range.GetFloat() );
	m_reuniteTimer.Start();
	return Continue();
}

// Original server 103BEE80.
ActionResult< SurvivorBot > SurvivorLegsCoverFriendInCombat::Update( SurvivorBot *me, float interval )
{
	CTerrorPlayer *friendPlayer = m_friend.Get();
	if ( !friendPlayer )
		return Done( "Friend is gone" );
	char modifiers[256];
	if ( !friendPlayer->IsAlive() )
	{
		V_snprintf( modifiers, sizeof( modifiers ), "Subject:%s,TimeToReunite:%f", friendPlayer->GetResponseCharacterName(), m_reuniteTimer.GetElapsedTime() );
		me->SpeakIfAllowed( "SurvivorBotFriendDiedBeforeSupportArrived", 1, modifiers );
		return Done( "Friend is dead" );
	}
	if ( !friendPlayer->IsInCombat( true ) )
		return Done( "Friend is no longer in combat" );
	if ( !me->CanReachSurvivor( friendPlayer ) )
	{
		V_snprintf( modifiers, sizeof( modifiers ), "Subject:%s,TimeToReunite:%f", friendPlayer->GetResponseCharacterName(), m_reuniteTimer.GetElapsedTime() );
		me->SpeakIfAllowed( "SurvivorBotCantSupportUnreachableFriend", 1, modifiers );
		return Done( "Friend is not reachable" );
	}
	if ( !me->GetLocomotionInterface()->IsScrambling() && me->IsFriendNearby( friendPlayer ) )
		return Done( "In position to assist friend in combat" );
	if ( me->INextBot::IsRangeGreaterThan( friendPlayer, 75.0f ) )
	{
		SurvivorBotPathCost cost( me );
		m_path.Update( me, friendPlayer, cost, NULL );
	}
	else
	{
		Vector forward;
		AngleVectors( friendPlayer->EyeAngles(), &forward );
		const Vector aim = friendPlayer->EyePosition() + forward * 1000.0f;
		me->GetBodyInterface()->AimHeadTowards( aim, IBody::INTERESTING, 0.5f, NULL, "Supporting friend in combat", true, 0.25f );
	}
	return Continue();
}

EventDesiredResult< SurvivorBot > SurvivorLegsCoverFriendInCombat::OnStuck( SurvivorBot *me )
{
	me->ResolveStuckSituation( &m_path );
	return TryToSustain();
}

// Original server 103BF350. Platform identity, not vertical proximity or a
// synthesized elevator waiting point, decides whether the leader was joined.
ActionResult< SurvivorBot > SurvivorLegsApproachElevator::Update( SurvivorBot *me, float interval )
{
	CTerrorPlayer *leader = me->GetTeamSituation().GetLeader();
	if ( !leader )
		return Done( "No leader" );
	CBaseEntity *elevator = leader->GetGroundElevator();
	if ( !elevator )
		return Done( "Leader not on an elevator" );
	if ( me->GetGroundElevator() == elevator )
		return Done( "I'm on the elevator with the team leader" );
	SurvivorBotPathCost cost( me );
	m_path.Update( me, leader, cost, NULL );
	return Continue();
}

EventDesiredResult< SurvivorBot > SurvivorLegsApproachElevator::OnStuck( SurvivorBot *me )
{
	me->ResolveStuckSituation( &m_path );
	return TryToSustain();
}
