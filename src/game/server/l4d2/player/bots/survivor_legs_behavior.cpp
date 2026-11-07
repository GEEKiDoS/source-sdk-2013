//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/bots/survivor_bot.h"
#include "l4d2/player/bots/survivor_legs_behavior.h"
#include "l4d2/player/bots/survivor_bot_path_cost.h"
#include "l4d2/player/bots/survivor_legs_tuning.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/director/director_scenario.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/nav/terror_nav.h"
#include "l4d2/nav/terror_nav_area.h"
#include "l4d2/nav/terror_nav_search.h"
#include "NextBot/NextBotBodyInterface.h"
#include "tier1/fmtstr.h"
#include "tier0/memdbgon.h"

bool IsSurvivorRescueVehicleArriving()
{
	const CDirectorScenario *scenario = TheDirector->GetScenario();
	return ( scenario->GetFinaleType() == 5 && scenario->HasStartedFirstPanicEvent() && !scenario->IsFirstPanicEvent() ) ||
		( scenario->GetFinaleType() != 5 && scenario->GetFinaleStage() == 6 );
}

void CollectSurvivorBattleStations( TerrorNavArea *start, const Vector &origin, CUtlVector< TerrorNavArea * > *areas, float range )
{
	struct Collector
	{
		CUtlVector< TerrorNavArea * > *result;
		bool operator()( TerrorNavArea *area )
		{
			if ( !area->HasSpawnAttributes( TERROR_NAV_FINALE | TERROR_NAV_BATTLEFIELD ) ) return false;
			if ( area->HasSpawnAttributes( TERROR_NAV_BATTLESTATION ) ) result->AddToTail( area );
			return true;
		}
	} collect = { areas };
	CollectSurroundingTerrorAreas( start, origin, collect, range, 0, TEAM_ANY );
}

static bool IsBattleStationCrowded( SurvivorBot *me, TerrorNavArea *area )
{
	int count = area->GetPlayerCount( TEAM_SURVIVOR );
	if ( me->GetLastKnownArea() && me->GetLastKnownArea()->GetID() == area->GetID() ) --count;
	int capacity = (int)( area->GetSizeY() * area->GetSizeX() * 0.00017777778f );
	if ( capacity < 1 ) capacity = 1;
	return count >= capacity;
}

ActionResult< SurvivorBot > SurvivorLegsStayClose::OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction )
{
	m_checkpointEnteredTimer.Invalidate();
	return Continue();
}

Action< SurvivorBot > *SurvivorLegsStayClose::InitialContainedAction( SurvivorBot *me )
{
	if ( TheDirector->IsAllBotSurvivorTeamAllowed() ) return new SurvivorLegsMoveOn;
	return new SurvivorLegsWait( 0.0f );
}

ActionResult< SurvivorBot > SurvivorLegsStayClose::Update( SurvivorBot *me, float interval )
{
	if ( me->IsProgressBarActive() ) return Continue();
	SurvivorTeamSituation &team = me->GetTeamSituation();
	if ( team.IsTeamNearCheckpoint() ) return ChangeTo( new SurvivorLegsMoveOn );
	if ( TheDirector->GetActivePipeBombCount() > 0 )
	{
		CBaseEntity *bomb = TheDirector->GetNearestPipeBomb( me->GetAbsOrigin() );
		if ( bomb )
		{
			const float range = me->GetThreatDangerRange( bomb ) * 1.5f;
			if ( !( ( me->GetAbsOrigin() - bomb->GetAbsOrigin() ).LengthSqr() > range * range ) )
				return SuspendFor( new SurvivorLegsRetreat( bomb ), "Retreating from nearby pipe bomb!" );
		}
	}
	if ( TheDirector->IsFinale() && IsSurvivorRescueVehicleArriving() && me->ShouldRespondToRescueVehicle() )
		return ChangeTo( new SurvivorLegsMeetRescueVehicleArrival, "Moving to meet the incoming rescue vehicle" );
	if ( TheDirector->IsFinale() && TheDirector->IsRescueVehicleReady() && me->ShouldRespondToRescueVehicle() )
		return ChangeTo( new SurvivorLegsEnterRescueVehicle, "The rescue vehicle is here!!!" );
	const SurvivorKnownEntity *known = me->GetIntentionInterface()->GetPrimaryKnownThreat();
	CTerrorPlayer *leader = team.GetLeader();
	if ( leader && leader->GetGroundElevator() && me->GetGroundElevator() != leader->GetGroundElevator() )
		return SuspendFor( new SurvivorLegsApproachElevator, "Joining team leader on a platform" );
	CBaseCombatCharacter *threat = known ? known->GetEntity() : NULL;
	if ( threat && threat->IsAlive() )
	{
		if ( GetTerrorEntityZombieClass( threat ) == 7 &&
			3.0f > gpGlobals->curtime - known->GetTimeWhenLastSeen() &&
			!threat->MyNextBotPointer()->GetBodyInterface()->IsArousal( IBody::INTENSE ) )
		{
			const int difficulty = TerrorGameRules()->GetDifficultyLevel();
			if ( difficulty < 2 || difficulty > 4 ) me->PressWalkButton();
		}
		if ( me->GetIntentionInterface()->IsImmediateThreat( me, threat ) && known->IsVisibleNow() &&
			me->IsThreatWithinDangerRange( threat, me->GetThreatDangerRange( threat ) ) &&
			( GetTerrorEntityZombieClass( threat ) != 0 || ( !team.IsTeamNearCheckpoint() && !TheDirector->IsRescueVehicleReady() ) ) )
			return SuspendFor( new SurvivorLegsRetreat( threat ), "Retreating from dangerous threat!" );
	}
	if ( !me->GetLocomotionInterface()->IsAttemptingToMove() )
	{
		CTerrorPlayer *human = team.GetNearestHuman();
		if ( human && team.GetNearestHumanRange() > me->GetSurvivorCharacter() * 10.0f + sb_separation_danger_min_range.GetFloat() && me->CanReachSurvivor( human ) )
			return SuspendFor( new SurvivorLegsRegroup( human ), "I'm too far away from the closest human survivor" );
		if ( !TerrorGameRules()->IsScavengeMode() && me->IsSafeForCombatAction() && human && team.GetNearestHumanRange() > 36.0f &&
			human->IsInCombat( false ) && !me->IsInCombat( false ) && !team.IsTeamNearCheckpoint() && !TheDirector->IsRescueVehicleReady() && me->CanReachSurvivor( human ) )
			return SuspendFor( new SurvivorLegsCoverFriendInCombat( human ), "Moving to assist human in combat" );
	}
	if ( sb_debug_team_avoidance.GetBool() && me->GetLocomotionInterface()->IsStuck() )
		NDebugOverlay::Circle( me->GetAbsOrigin(), 10.0f, 255, 0, 0, 255, true, 0.1f );
	return Continue();
}

EventDesiredResult< SurvivorBot > SurvivorLegsStayClose::OnNavAreaChanged( SurvivorBot *me, CNavArea *enteredArea, CNavArea *leftArea )
{
	if ( enteredArea )
	{
		if ( ToTerrorNavArea( enteredArea )->HasSpawnAttributes( TERROR_NAV_PLAYER_START | TERROR_NAV_CHECKPOINT ) )
		{
			if ( !m_checkpointEnteredTimer.HasStarted() )
			{
				m_checkpointEnteredTimer.Start();
				if ( me->IsDebugging( NEXTBOT_BEHAVIOR ) ) DevMsg( "%3.2f: %s entered a checkpoint\n", gpGlobals->curtime, me->GetDebugIdentifier() );
			}
		}
		else
		{
			if ( me->IsDebugging( NEXTBOT_BEHAVIOR ) && m_checkpointEnteredTimer.HasStarted() )
				DevMsg( "%3.2f: %s left a checkpoint\n", gpGlobals->curtime, me->GetDebugIdentifier() );
			m_checkpointEnteredTimer.Invalidate();
		}
	}
	return TryContinue();
}

EventDesiredResult< SurvivorBot > SurvivorLegsStayClose::OnCommandRetreat( SurvivorBot *me, CBaseEntity *threat, float range )
{
	return TrySuspendFor( new SurvivorLegsRetreat( threat ), RESULT_CRITICAL, "Retreating from threat" );
}

EventDesiredResult< SurvivorBot > SurvivorLegsStayClose::OnCommandPause( SurvivorBot *me, float duration )
{
	return TrySuspendFor( new SurvivorLegsWait( 0.0f ), RESULT_CRITICAL, "Standing still..." );
}

ActionResult< SurvivorBot > SurvivorLegsMeetRescueVehicleArrival::OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction )
{
	m_path.Invalidate();
	m_loadingArea = NULL;
	TerrorNavArea *rescue = TheTerrorNavMesh()->GetRescueVehicleArea();
	if ( rescue )
	{
		struct Collector
		{
			TerrorNavArea *loading;
			bool operator()( TerrorNavArea *area )
			{
				if ( area->HasSpawnAttributes( TERROR_NAV_RESCUE_VEHICLE ) ) return true;
				loading = area;
				return false;
			}
		} collect = { NULL };
		CollectSurroundingTerrorAreas( rescue, rescue->GetCenter(), collect, 2000.0f, 2, TEAM_ANY );
		m_loadingArea = collect.loading;
		me->SpeakIfAllowed( "SurvivorBotMovingToRescueVehicle", 1 );
	}
	return m_loadingArea ? Continue() : Done( "No loading area for rescue vehicle found!" );
}

ActionResult< SurvivorBot > SurvivorLegsMeetRescueVehicleArrival::Update( SurvivorBot *me, float interval )
{
	if ( TheDirector->IsFinale() && TheDirector->IsRescueVehicleReady() )
		return ChangeTo( new SurvivorLegsEnterRescueVehicle, "The rescue vehicle is here!!!" );
	if ( !m_loadingArea )
	{
		DevMsg( "%3.2f: %s ERROR: No RESCUE_VEHICLE area!\n", gpGlobals->curtime, me->GetDebugIdentifier() );
		return Continue();
	}
	if ( me->IsRangeLessThan( m_loadingArea->GetCenter(), sb_rescue_vehicle_loading_range.GetFloat() ) ) return Continue();
	if ( m_path.IsValid() && m_path.GetAge() < 3.0f ) m_path.Update( me );
	else
	{
		SurvivorBotPathCost cost( me );
		m_path.Compute( me, m_loadingArea->GetCenter(), cost, 0.0f );
	}
	return Continue();
}

ActionResult< SurvivorBot > SurvivorLegsMeetRescueVehicleArrival::OnResume( SurvivorBot *me, Action< SurvivorBot > *interruptingAction )
{
	m_path.Invalidate();
	return Continue();
}

EventDesiredResult< SurvivorBot > SurvivorLegsMeetRescueVehicleArrival::OnStuck( SurvivorBot *me )
{
	me->ResolveStuckSituation( &m_path );
	return TryToSustain();
}

ActionResult< SurvivorBot > SurvivorLegsEnterRescueVehicle::OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction )
{
	m_path.Invalidate();
	me->SpeakIfAllowed( "SurvivorBotEnteringRescueVehicle", 1 );
	return Continue();
}

ActionResult< SurvivorBot > SurvivorLegsEnterRescueVehicle::Update( SurvivorBot *me, float interval )
{
	TerrorNavArea *rescue = TheTerrorNavMesh()->GetRescueVehicleArea();
	if ( !rescue )
	{
		DevMsg( "%3.2f: %s ERROR: No RESCUE_VEHICLE area!\n", gpGlobals->curtime, me->GetDebugIdentifier() );
		return Continue();
	}
	if ( me->GetLastKnownArea() == rescue )
	{
		if ( CountHumansNearRescueVehicle() < 1 ) return ChangeTo( new SurvivorLegsStayClose, "Leaving vehicle with humans" );
		return Continue();
	}
	if ( m_path.IsValid() && m_path.GetAge() < 3.0f ) m_path.Update( me );
	else
	{
		SurvivorBotPathCost cost( me );
		m_path.Compute( me, rescue->GetCenter(), cost, 0.0f );
	}
	return Continue();
}

ActionResult< SurvivorBot > SurvivorLegsEnterRescueVehicle::OnResume( SurvivorBot *me, Action< SurvivorBot > *interruptingAction )
{
	m_path.Invalidate();
	return Continue();
}

EventDesiredResult< SurvivorBot > SurvivorLegsEnterRescueVehicle::OnStuck( SurvivorBot *me )
{
	me->ResolveStuckSituation( &m_path );
	return TryToSustain();
}

ActionResult< SurvivorBot > SurvivorLegsBattleStations::OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction )
{
	m_path.Invalidate();
	m_battleStation = NULL;
	TerrorNavArea *area = ToTerrorNavArea( me->GetLastKnownArea() );
	CFmtStr modifiers( "Where:%s", area && area->HasSpawnAttributes( TERROR_NAV_FINALE ) ? "FINALE" : "BATTLEFIELD" );
	me->SpeakIfAllowed( "SurvivorBotMovingToBattleStation", 1, modifiers );
	return Continue();
}

TerrorNavArea *SurvivorLegsBattleStations::SelectBattleStation( SurvivorBot *me, TerrorNavArea *current )
{
	SurvivorTeamSituation &team = me->GetTeamSituation();
	TerrorNavArea *start = ToTerrorNavArea( me->GetLastKnownArea() );
	if ( !start || !m_selectionTimer.IsElapsed() ) return current;
	m_selectionTimer.Start( RandomFloat( 0.5f, 2.0f ) );
	CUtlVector< TerrorNavArea * > stations;
	CollectSurvivorBattleStations( start, me->GetAbsOrigin(), &stations );
	if ( stations.Count() == 0 ) return current;
	CTerrorPlayer *human = team.GetNearestHuman();
	if ( !human ) return stations[RandomInt( 0, stations.Count() - 1 )];
	if ( !human->IsStill( true ) || sb_battlestation_human_hold_time.GetFloat() > human->GetStillnessDuration( true ) ) return current;
	const float maximum = sb_max_battlestation_range_from_human.GetFloat();
	if ( current && ( human->GetAbsOrigin() - current->GetCenter() ).LengthSqr() > maximum * maximum ) current = NULL;
	TerrorNavArea *humanArea = ToTerrorNavArea( human->GetLastKnownArea() );
	if ( !humanArea ) return current;
	if ( !humanArea->IsPotentiallyVisible( current, false ) ) current = NULL;
	float rangeSquared = current ? ( human->GetAbsOrigin() - current->GetCenter() ).LengthSqr() * 0.5f : maximum * maximum;
	for ( int i = 0; i < stations.Count(); ++i )
	{
		TerrorNavArea *area = stations[i];
		if ( area->IsBlocked( TEAM_SURVIVOR ) || area->IsDamaging() || IsBattleStationCrowded( me, area ) ) continue;
		const float distance = ( human->GetAbsOrigin() - area->GetCenter() ).LengthSqr();
		if ( rangeSquared > distance && humanArea->IsPotentiallyVisible( area, false ) )
		{
			rangeSquared = distance;
			current = area;
		}
	}
	return current;
}

ActionResult< SurvivorBot > SurvivorLegsBattleStations::Update( SurvivorBot *me, float interval )
{
	if ( TheDirector->IsFinale() && IsSurvivorRescueVehicleArriving() && me->ShouldRespondToRescueVehicle() )
		return ChangeTo( new SurvivorLegsMeetRescueVehicleArrival, "Moving to meet the incoming rescue vehicle" );
	if ( TheDirector->IsFinale() && TheDirector->IsRescueVehicleReady() && me->ShouldRespondToRescueVehicle() )
		return ChangeTo( new SurvivorLegsEnterRescueVehicle, "The rescue vehicle is here!!!" );
	if ( IsSurvivorRescueVehicleArriving() ) return Done( "The battle is over, move on" );
	if ( TheDirector->GetTankCount() > 0 ) return Done( "A Tank is coming" );
	TerrorNavArea *current = ToTerrorNavArea( me->GetLastKnownArea() );
	if ( !current ) return Done( "I'm off the mesh" );
	const int pace = me->GetDesiredPace();
	if ( pace == 0 ) return Continue();
	if ( pace == 1 ) me->PressWalkButton();
	SurvivorTeamSituation &team = me->GetTeamSituation();
	if ( !m_battleStation )
	{
		m_battleStation = SelectBattleStation( me, NULL );
		return m_battleStation ? Continue() : Done( "No practical battlestation found" );
	}
	CTerrorPlayer *human = team.GetNearestHuman();
	if ( current != m_battleStation )
	{
		const float maximum = sb_max_battlestation_range_from_human.GetFloat();
		if ( m_battleStation->IsBlocked( TEAM_SURVIVOR ) || m_battleStation->IsDamaging() || IsBattleStationCrowded( me, m_battleStation ) ||
			( human && !( ( human->GetAbsOrigin() - m_battleStation->GetCenter() ).LengthSqr() <= maximum * maximum ) ) )
		{
			m_battleStation = NULL;
			return Continue();
		}
		if ( m_repathTimer.IsElapsed() && !me->GetLocomotionInterface()->IsScrambling() )
		{
			m_repathTimer.Start( 2.0f );
			SurvivorBotPathCost cost( me );
			if ( !m_path.Compute( me, m_battleStation->GetCenter(), cost, 0.0f ) ) return Done( "No path to battlestation" );
		}
		m_path.Update( me );
		m_reevaluateTimer.Start( RandomFloat( 5.0f, 10.0f ) );
		return Continue();
	}
	if ( !m_reevaluateTimer.IsElapsed() ) return Continue();
	const float giveUpRange = sb_battlestation_give_up_range_from_human.GetFloat();
	if ( human && ( human->GetAbsOrigin() - m_battleStation->GetCenter() ).LengthSqr() > giveUpRange * giveUpRange ) m_battleStation = NULL;
	m_battleStation = SelectBattleStation( me, m_battleStation );
	m_reevaluateTimer.Start( RandomFloat( 0.5f, 2.0f ) );
	return Continue();
}

ActionResult< SurvivorBot > SurvivorLegsBattleStations::OnResume( SurvivorBot *me, Action< SurvivorBot > *interruptingAction )
{
	m_path.Invalidate();
	return Continue();
}

EventDesiredResult< SurvivorBot > SurvivorLegsBattleStations::OnStuck( SurvivorBot *me )
{
	me->ResolveStuckSituation( &m_path );
	return TryToSustain();
}

ActionResult< SurvivorBot > L4D1SurvivorLegsBattleStations::OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction )
{
	m_path.Invalidate();
	m_battleStation = NULL;
	return Continue();
}

TerrorNavArea *L4D1SurvivorLegsBattleStations::SelectBattleStation( SurvivorBot *me, TerrorNavArea *current )
{
	TerrorNavArea *start = ToTerrorNavArea( me->GetLastKnownArea() );
	if ( !start || !m_selectionTimer.IsElapsed() ) return current;
	struct Collector
	{
		CUtlVector< TerrorNavArea * > areas;
		bool operator()( TerrorNavArea *area )
		{
			if ( area->HasSpawnAttributes( TERROR_NAV_BATTLESTATION ) ) areas.AddToTail( area );
			return true;
		}
	} collect;
	// Unlike the ordinary team's collector, this search crosses non-battlefield
	// areas. No nearest-human, PVS, occupancy or damage policy is added here.
	CollectSurroundingTerrorAreas( start, me->GetAbsOrigin(), collect, -1.0f, 0, TEAM_ANY );
	if ( collect.areas.Count() > 0 ) current = collect.areas[RandomInt( 0, collect.areas.Count() - 1 )];
	if ( current ) m_selectionTimer.Start( RandomFloat( 10.0f, 15.0f ) );
	else m_selectionTimer.Start( RandomFloat( 0.5f, 2.0f ) );
	return current;
}

ActionResult< SurvivorBot > L4D1SurvivorLegsBattleStations::Update( SurvivorBot *me, float interval )
{
	TerrorNavArea *current = ToTerrorNavArea( me->GetLastKnownArea() );
	if ( !current ) return Continue();
	m_battleStation = SelectBattleStation( me, m_battleStation );
	if ( !m_battleStation ) return Continue();
	if ( current != m_battleStation )
	{
		if ( m_battleStation->IsBlocked( TEAM_SURVIVOR ) || m_battleStation->IsDamaging() || IsBattleStationCrowded( me, m_battleStation ) )
		{
			m_battleStation = NULL;
			return Continue();
		}
		if ( m_repathTimer.IsElapsed() && !me->GetLocomotionInterface()->IsScrambling() )
		{
			m_repathTimer.Start( 2.0f );
			SurvivorBotPathCost cost( me );
			if ( !m_path.Compute( me, m_battleStation->GetCenter(), cost, 0.0f ) ) return Done( "No path to battlestation" );
		}
		m_path.Update( me );
		m_reevaluateTimer.Start( RandomFloat( 5.0f, 10.0f ) );
	}
	else if ( m_reevaluateTimer.IsElapsed() )
	{
		m_battleStation = SelectBattleStation( me, m_battleStation );
		m_reevaluateTimer.Start( RandomFloat( 0.5f, 2.0f ) );
	}
	return Continue();
}

ActionResult< SurvivorBot > L4D1SurvivorLegsBattleStations::OnResume( SurvivorBot *me, Action< SurvivorBot > *interruptingAction )
{
	m_path.Invalidate();
	return Continue();
}

EventDesiredResult< SurvivorBot > L4D1SurvivorLegsBattleStations::OnStuck( SurvivorBot *me )
{
	me->ResolveStuckSituation( &m_path );
	return TryToSustain( RESULT_IMPORTANT );
}
