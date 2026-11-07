//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/bots/survivor_bot.h"
#include "l4d2/player/bots/survivor_legs_behavior.h"
#include "l4d2/player/bots/survivor_legs_tuning.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/director/director_scenario.h"
#include "l4d2/director/director_session_manager.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/nav/terror_nav_area.h"
#include "NextBot/NextBotBodyInterface.h"
#include "tier0/memdbgon.h"

extern ConVar z_witch_threat_hostile_range;

ActionResult< SurvivorBot > SurvivorLegsMoveOn::OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction )
{
	me->UpdateEscapePath();
	me->GetEscapePath().SetMinLookAheadDistance( sb_path_lookahead_range.GetFloat() );
	me->UpdateEscapePath();
	me->GetEscapePath().Invalidate();
	m_battlefieldCheckTimer.Invalidate();
	m_unusedFlag = false;
	m_leaderPauseTimer.Invalidate();
	return Continue();
}

ActionResult< SurvivorBot > SurvivorLegsMoveOn::OnResume( SurvivorBot *me, Action< SurvivorBot > *interruptingAction )
{
	me->UpdateEscapePath();
	me->GetEscapePath().Invalidate();
	return Continue();
}

EventDesiredResult< SurvivorBot > SurvivorLegsMoveOn::OnStuck( SurvivorBot *me )
{
	me->UpdateEscapePath();
	me->ResolveStuckSituation( &me->GetEscapePath() );
	return TryToSustain();
}

const Path *SurvivorLegsMoveOn::GetCurrentPath( const INextBot *bot ) const
{
	SurvivorBot *me = static_cast< SurvivorBot * >( bot->GetEntity() );
	me->UpdateEscapePath();
	return &me->GetEscapePath();
}

void SurvivorLegsMoveOn::DelayForTeam()
{
	if ( !TerrorGameRules()->IsPlayerControlledZombies() && !sb_allow_leading.GetBool() )
		m_leaderPauseTimer.Start( RandomFloat( 1.0f, 2.0f ) );
}

void SurvivorLegsMoveOn::MoveWithTeam( SurvivorBot *me )
{
	SurvivorTeamSituation &team = me->GetTeamSituation();
	CTerrorPlayer *leadingHuman = team.GetLeadingHuman();
	if ( leadingHuman && !leadingHuman->HasLeftCheckpoint() )
	{
		DelayForTeam();
		return;
	}
	if ( TheDirector->GetSessionManager()->IsStartAreaExitBlocked() )
	{
		DelayForTeam();
		return;
	}
	const float maximum = sb_separation_danger_max_range.GetFloat();
	CTerrorPlayer *nearest = team.GetNearestFriend();
	if ( nearest && IsSurvivorBehind( me, nearest ) &&
		( me->GetAbsOrigin() - nearest->GetAbsOrigin() ).LengthSqr() > maximum * maximum )
	{
		me->UpdateEscapePath();
		me->GetEscapePath().Update( me );
		return;
	}
	if ( !team.IsTeamNearCheckpoint() )
	{
		if ( TheDirector->IsFinale() && TheDirector->IsRescueVehicleReady() )
		{
			me->UpdateEscapePath();
			me->GetEscapePath().Update( me );
			return;
		}
		CTerrorPlayer *farthest = team.GetFarthestFriend();
		// S38B840(me,friend) has precisely the reversed S38B910 comparison,
		// including its greater-entindex tie; reuse the same canonical comparator.
		if ( farthest && IsSurvivorBehind( farthest, me ) &&
			( me->GetAbsOrigin() - farthest->GetAbsOrigin() ).LengthSqr() > maximum * maximum &&
			( !farthest->IsIncapacitated() || me->CanReachSurvivor( farthest ) ) )
		{
			DelayForTeam();
			return;
		}
		if ( !TerrorGameRules()->IsVersusMode() && me->GetIntentionInterface()->ShouldHurry( me ) != ANSWER_YES && m_teamPauseCheckTimer.IsElapsed() )
		{
			if ( me->GetTimeSinceWeaponFired() < 1.0f )
			{
				if ( team.GetTeammateCount() == 0 ) DelayForTeam();
				if ( nearest && me->IsThreatWithinDangerRange( nearest, sb_separation_range.GetFloat() ) &&
					( !team.GetLeader() || me->IsThreatWithinDangerRange( team.GetLeader(), maximum ) ) ) DelayForTeam();
			}
			CTerrorPlayer *fighting = team.GetRecentlyFightingFriend();
			if ( fighting && me->IsThreatWithinDangerRange( fighting, sb_separation_range.GetFloat() ) ) DelayForTeam();
			m_teamPauseCheckTimer.Start( RandomFloat( 0.25f, 1.0f ) );
		}
		if ( leadingHuman )
		{
			float humanFlow = leadingHuman->GetFlowDistance( FLOW_TOWARD_GOAL );
			if ( humanFlow > me->GetFlowDistance( FLOW_TOWARD_GOAL ) )
			{
				const SurvivorKnownEntity *known = me->GetIntentionInterface()->GetPrimaryKnownThreat();
				CBaseCombatCharacter *threat = known ? known->GetEntity() : NULL;
				if ( threat && GetTerrorEntityZombieClass( threat ) == 7 &&
					3.0f > gpGlobals->curtime - known->GetTimeWhenLastSeen() &&
					!threat->MyNextBotPointer()->GetBodyInterface()->IsArousal( IBody::INTENSE ) &&
					me->IsRangeGreaterThan( threat, z_witch_threat_hostile_range.GetFloat() ) )
				{
					me->UpdateEscapePath();
					me->GetEscapePath().Update( me );
					return;
				}
			}
			for ( int i = 0; i < team.GetTeammateCount(); ++i )
			{
				CTerrorPlayer *player = team.GetTeammate( i );
				if ( player != leadingHuman && !player->IsFakeClient() &&
					( player->GetAbsOrigin() - leadingHuman->GetAbsOrigin() ).LengthSqr() < 22500.0f &&
					humanFlow > player->GetFlowDistance( FLOW_TOWARD_GOAL ) )
					humanFlow = player->GetFlowDistance( FLOW_TOWARD_GOAL );
			}
			if ( me->GetFlowDistance( FLOW_TOWARD_GOAL ) + 100.0f >= humanFlow ) DelayForTeam();
		}
		if ( m_leaderPauseTimer.HasStarted() && gpGlobals->curtime < m_leaderPauseTimer.m_timestamp.Get() ) return;
	}
	me->UpdateEscapePath();
	me->GetEscapePath().Update( me );
}

ActionResult< SurvivorBot > SurvivorLegsMoveOn::Update( SurvivorBot *me, float interval )
{
	SurvivorTeamSituation &team = me->GetTeamSituation();
	if ( !IsSurvivorRescueVehicleArriving() && m_battlefieldCheckTimer.IsElapsed() && TheDirector->GetTankCount() <= 0 )
	{
		m_battlefieldCheckTimer.Start( RandomFloat( 3.0f, 5.0f ) );
		TerrorNavArea *area = ToTerrorNavArea( me->GetLastKnownArea() );
		if ( area && area->HasSpawnAttributes( TERROR_NAV_FINALE | TERROR_NAV_BATTLEFIELD ) )
		{
			CUtlVector< TerrorNavArea * > stations;
			CollectSurvivorBattleStations( area, me->GetAbsOrigin(), &stations, 500.0f );
			for ( int i = 0; i < stations.Count(); ++i )
				if ( me->GetFlowDistance( FLOW_TOWARD_GOAL ) >= stations[i]->GetFlowDistance() )
					return SuspendFor( new SurvivorLegsBattleStations, "I've entered a battlefield" );
		}
	}
	if ( team.GetTeammateCount() != 0 && !me->GetLocomotionInterface()->IsScrambling() )
	{
		if ( me->IsSafeForCombatAction() )
		{
			CTerrorPlayer *fighting = NULL;
			float nearestPathLength = FLT_MAX;
			for ( int i = 0; i < team.GetTeammateCount(); ++i )
			{
				CTerrorPlayer *player = team.GetTeammate( i );
				if ( !player->IsInCombat( true ) || me->IsFriendNearby( player ) ) continue;
				PathFollower *path = team.GetPathToFriend( player );
				if ( path )
				{
					if ( nearestPathLength > path->GetLength() )
					{
						nearestPathLength = path->GetLength();
						fighting = player;
					}
				}
				else if ( me->IsDebugging( NEXTBOT_PATH ) )
					DevMsg( "%3.2f: ERROR SurvivorBot %s can't move to help %s - no path\n", gpGlobals->curtime, me->GetDebugIdentifier(), player->GetPlayerName() );
			}
			if ( !TerrorGameRules()->IsScavengeMode() && !TerrorGameRules()->IsVersusMode() )
			{
				if ( fighting && ( !fighting->IsBot() || ( !team.IsTeamNearCheckpoint() && !TheDirector->IsRescueVehicleReady() ) ) && me->CanReachSurvivor( fighting ) )
					return SuspendFor( new SurvivorLegsCoverFriendInCombat( fighting ), "A friend is in combat and I'm not - moving to assist" );
				CTerrorPlayer *orphan = team.GetSeparatedFriend();
				if ( orphan && orphan->GetTimeSinceFriendNearby() > 3.0f && !orphan->IsBot() && !me->IsRushing() )
					return SuspendFor( new SurvivorLegsCoverOrphan( orphan ), "A friend is orphaned, and I'm the closest to him" );
			}
			float range = sb_separation_danger_max_range.GetFloat();
			if ( me->IsRushing() ) range = GetSurvivorRushingRange() + 48.0f;
			CTerrorPlayer *human = team.GetNearestHuman();
			if ( human && team.GetNearestHumanRange() > range && me->CanReachSurvivor( human ) )
				return SuspendFor( new SurvivorLegsRegroup( human ), "I'm too far away from the closest human survivor" );
		}
		if ( team.GetTimeSinceSeenAnyFriend() > 3.0f && team.GetClosestReachableFriend() )
			return SuspendFor( new SurvivorLegsRegroup( NULL ), "I've lost sight of the team" );
		CTerrorPlayer *leader = team.GetLeader();
		if ( leader && leader->GetGroundElevator() && me->GetGroundElevator() != leader->GetGroundElevator() )
			return SuspendFor( new SurvivorLegsApproachElevator, "Joining team leader on a platform" );
	}
	const int pace = me->GetDesiredPace();
	if ( pace == 0 ) me->GetLocomotionInterface()->ClearStuckStatus( "Pace == STOP" );
	else
	{
		if ( pace == 1 ) me->PressWalkButton();
		if ( !me->IsRushing() || me->ShouldMoveEvenIfRushing() ) MoveWithTeam( me );
	}
	return Continue();
}
