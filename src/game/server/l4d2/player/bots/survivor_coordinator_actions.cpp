//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/bots/survivor_coordinator_actions.h"
#include "l4d2/player/bots/survivor_bot.h"
#include "l4d2/player/bots/survivor_attack.h"
#include "l4d2/player/bots/survivor_legs.h"
#include "l4d2/player/bots/survivor_interaction.h"
#include "l4d2/player/bots/survivor_bot_path_cost.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/nav/terror_nav.h"
#include "l4d2/nav/terror_nav_world.h"
#include "l4d2/nav/terror_nav_area.h"
#include "l4d2/nav/terror_nav_search.h"
#include "l4d2/weapons/terror_weapon.h"
#include "l4d2/weapons/terror_gun.h"
#include "BasePropDoor.h"
#include "doors.h"
#include "NextBot/Behavior/BehaviorBackUp.h"
#include "tier0/memdbgon.h"

static ConVar sb_close_checkpoint_door_interval( "sb_close_checkpoint_door_interval", "2", 0x00004004 );
static ConVar sb_debug_apoproach_wait_time( "sb_debug_apoproach_wait_time", "5", 0x00004004,
	"How long a SurvivorBot waits once it reaches its debug move-to spot" );

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
// S:257770. Strictly positive distance is required even for the first area.
TerrorNavArea *CTerrorCheckpoint::GetFarthestSpawnArea( const Vector &position ) const
{
	TerrorNavArea *farthest = NULL;
	float farthestRange = 0.0f;
	FOR_EACH_VEC( m_spawnAreas, i )
	{
		const double range = m_spawnAreas[i]->GetDistanceSquaredToPoint( position );
		if ( range > (double)farthestRange )
		{
			farthestRange = (float)range;
			farthest = m_spawnAreas[i];
		}
	}
	return farthest;
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

Action< SurvivorBot > *SurvivorCheckpointLeaving::InitialContainedAction( SurvivorBot *me ) { return new SurvivorAttack; }
Action< SurvivorBot > *SurvivorCheckpointReached::InitialContainedAction( SurvivorBot *me ) { return new SurvivorAttack; }
Action< SurvivorBot > *SurvivorCloseDoor::InitialContainedAction( SurvivorBot *me ) { return new SurvivorAttack; }
Action< SurvivorBot > *SurvivorElevatorEnter::InitialContainedAction( SurvivorBot *me ) { return new SurvivorAttack; }
Action< SurvivorBot > *SurvivorElevatorRide::InitialContainedAction( SurvivorBot *me ) { return new SurvivorAttack; }
Action< SurvivorBot > *SurvivorEscapeFlames::InitialContainedAction( SurvivorBot *me ) { return new SurvivorAttack; }
Action< SurvivorBot > *SurvivorEscapeSpit::InitialContainedAction( SurvivorBot *me ) { return new SurvivorAttack; }

// S:3B3550/3B3C60. The NULL initial legs action is the actual native hold.
ActionResult< SurvivorBot > SurvivorCheckpointLeaving::OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction )
{
	m_hasPickedUpMedkit = false;
	m_initialWaitTimer.Start( RandomFloat( 2.0f, 4.0f ) );
	me->GetIntentionInterface()->PushLegsAction( NULL );
	m_wanderArea = NULL;
	m_wanderTimer.Invalidate();
	m_wanderGoal.Init();
	return Continue();
}

void SurvivorCheckpointLeaving::OnEnd( SurvivorBot *me, Action< SurvivorBot > *nextAction )
{
	me->GetIntentionInterface()->PopLegsAction();
}

EventDesiredResult< SurvivorBot > SurvivorCheckpointLeaving::OnPickUp( SurvivorBot *me, CBaseEntity *item, CBaseCombatCharacter *giver )
{
	if ( item && item->ClassMatches( "weapon_first_aid_kit*" ) ) m_hasPickedUpMedkit = true;
	return TryContinue();
}

// S:3B33E0 calls prop-door956, NOT the distinct952 class-identity query.
EventDesiredResult< SurvivorBot > SurvivorCheckpointLeaving::OnContact( SurvivorBot *me, CBaseEntity *other, CGameTrace *trace )
{
	CBasePropDoor *propDoor = dynamic_cast< CBasePropDoor * >( other );
	if ( propDoor )
	{
		m_wanderArea = NULL;
		m_wanderTimer.Start( RandomFloat( 1.0f, 3.0f ) );
		if ( propDoor->HasUnlockSequence() ) me->GetLocomotionInterface()->ClearStuckStatus( "" );
	}
	else if ( dynamic_cast< CBaseDoor * >( other ) )
	{
		m_wanderArea = NULL;
		m_wanderTimer.Start( RandomFloat( 1.0f, 3.0f ) );
	}
	return TheDirector->IsAllBotSurvivorTeamAllowed() ? TryContinue() : TryToSustain( RESULT_CRITICAL );
}

// S:3B3670. The independent wait and legs dispatch preserve RNG and action order.
ActionResult< SurvivorBot > SurvivorCheckpointLeaving::Update( SurvivorBot *me, float interval )
{
	if ( m_initialWaitTimer.GetRemainingTime() > 0.0f ) return Continue();
	me->m_isReadyToLeaveCheckpoint = false;
	ActionResult< SurvivorBot > selected = me->TrySaveFriendsInImmediateTrouble();
	if ( !selected.IsRequestingChange() ) selected = me->TryHealFriends();
	if ( !selected.IsRequestingChange() ) selected = me->TryReviveFriends();
	if ( !selected.IsRequestingChange() ) selected = me->TryScavenge();
	if ( selected.IsRequestingChange() )
		return selected.m_type == SUSPEND_FOR ? SuspendFor( selected.m_action, selected.m_reason ) : selected;
	if ( me->IsSafeForCombatAction() && TerrorGameRules()->GetDifficultyLevel() != 3 && me->GetHealth() < 75 && me->GetWeaponOfType( 12 ) )
		return SuspendFor( new SurvivorHealSelf );
	CTerrorWeapon *secondary = dynamic_cast< CTerrorWeapon * >( me->Weapon_GetSlot( 1 ) );
	if ( me->Weapon_GetSlot( 0 ) || ( secondary &&
		( secondary->GetWeaponID() == 19 || secondary->GetWeaponID() == 32 ||
		( secondary->GetWeaponID() == 1 && static_cast< CTerrorGun * >( secondary )->IsDualWielding() ) ) ) )
		me->m_isReadyToLeaveCheckpoint = true;
	SurvivorTeamSituation &team = me->GetTeamSituation();
	if ( TheDirector->IsAllBotSurvivorTeamAllowed() && team.AreAllTeammatesBots() )
	{
		bool everyoneReady = true;
		for ( int i = 0; i < team.GetTeammateCount(); ++i )
		{
			CTerrorPlayer *player = team.GetTeammate( i );
			if ( player->IsFakeClient() && !static_cast< SurvivorBot * >( player )->m_isReadyToLeaveCheckpoint )
			{
				everyoneReady = false;
				break;
			}
		}
		if ( everyoneReady ) return Done( "Everyone is ready - moving out!" );
	}
	if ( me->HasLeftCheckpoint() ) return Done( "I've walked out of the checkpoint" );
	if ( TheDirector->HasAnySurvivorLeftSafeArea() )
	{
		me->SpeakIfAllowed( "SurvivorBotLeavingCheckpoint", 1 );
		return Done( "Someone has left the safe area, time to go!" );
	}
	me->PressWalkButton();
	CTerrorPlayer *friendPlayer = team.GetNearestFriend();
	if ( friendPlayer )
	{
		Vector direction = friendPlayer->GetAbsOrigin() - me->GetAbsOrigin();
		if ( VectorNormalize( direction ) < 50.0f && DotProduct( me->GetBodyInterface()->GetViewVector(), direction ) > 0.5f )
		{
			m_wanderArea = NULL;
			m_wanderTimer.Start( RandomFloat( 1.0f, 3.0f ) );
			me->GetIntentionInterface()->ChangeLegsAction( new SurvivorLegsWait );
		}
	}
	if ( !m_wanderArea )
	{
		if ( m_wanderTimer.HasStarted() && me->IsRangeLessThan( m_wanderGoal, 20.0f ) )
			me->GetIntentionInterface()->ChangeLegsAction( new SurvivorLegsWait );
		if ( m_wanderTimer.IsElapsed() )
		{
			TerrorNavArea *current = ToTerrorNavArea( me->GetLastKnownArea() );
			if ( current && ( !m_wanderArea || current == m_wanderArea ) )
			{
				m_wanderArea = ToTerrorNavArea( current->GetRandomAdjacentArea( static_cast< NavDirType >( RandomInt( 0, 3 ) ) ) );
				if ( m_wanderArea )
				{
					if ( m_wanderArea->GetDoor() || !m_wanderArea->HasSpawnAttributes( TERROR_NAV_CHECKPOINT ) ) m_wanderArea = NULL;
					if ( !m_wanderArea || m_wanderArea->IsUnderwater() ||
						me->GetLocomotionInterface()->GetStepHeight() * 0.5f <= fabsf( current->ComputeAdjacentConnectionHeightChange( m_wanderArea ) ) )
						m_wanderArea = NULL;
					else m_wanderGoal = m_wanderArea->GetRandomPoint();
				}
			}
		}
	}
	if ( m_wanderArea )
	{
		Vector lookAt( m_wanderGoal.x, m_wanderGoal.y, me->EyePosition().z );
		me->GetBodyInterface()->AimHeadTowards( lookAt, IBody::INTERESTING, 1.0f, NULL, "Looking towards next wander spot", true, 0.25f );
		if ( me->GetBodyInterface()->IsHeadAimingOnTarget() )
		{
			me->GetIntentionInterface()->ChangeLegsAction( new SurvivorLegsApproach( m_wanderGoal ) );
			m_wanderArea = NULL;
			m_wanderTimer.Start( RandomFloat( 3.0f, 20.0f ) );
		}
	}
	return Continue();
}

ActionResult< SurvivorBot > SurvivorCheckpointReached::OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction )
{
	m_checkpoint = TheTerrorNavMesh()->GetExitCheckpoint();
	m_selectionTimer.Invalidate();
	m_closeDoorTimer.Start( sb_close_checkpoint_door_interval.GetFloat() );
	me->GetIntentionInterface()->PushLegsAction( new SurvivorLegsWait );
	me->SpeakIfAllowed( "SurvivorBotReachedCheckpoint", 1 );
	return Continue();
}

void SurvivorCheckpointReached::OnEnd( SurvivorBot *me, Action< SurvivorBot > *nextAction )
{
	me->GetIntentionInterface()->PopLegsAction();
}

// S:3B3D80/3B3EB0. Living humans only affect the incapacitated-human flag;
// the closest eligible BOT (not a new elected leader) is assigned door use.
static bool IsCheckpointDoorCloser( SurvivorBot *me, CTerrorCheckpoint *checkpoint, CBasePropDoor *door )
{
	bool allInside = true, allHumansDown = true;
	CTerrorPlayer *nearestBot = NULL;
	float nearestRange = 1e8f;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( !player || !player->edict() || player->entindex() == 0 || !player->IsPlayer() || !player->IsConnected() ||
			player->GetTeamNumber() != TEAM_SURVIVOR || !player->IsAlive() ) continue;
		if ( checkpoint->GetAreas().Find( ToTerrorNavArea( player->GetLastKnownArea() ) ) == checkpoint->GetAreas().InvalidIndex() ) allInside = false;
		if ( player->IsBot() )
		{
			if ( !player->m_reviveTarget.Get() )
			{
				float range = ( door->WorldSpaceCenter() - player->WorldSpaceCenter() ).LengthSqr();
				if ( nearestRange > range ) { nearestRange = range; nearestBot = player; }
			}
		}
		else if ( !player->IsIncapacitated() ) allHumansDown = false;
	}
	return ( TheDirector->IsAllBotSurvivorTeamAllowed() || allHumansDown ) && allInside && me->IsSelf( nearestBot );
}

ActionResult< SurvivorBot > SurvivorCheckpointReached::Update( SurvivorBot *me, float interval )
{
	if ( !m_checkpoint || m_checkpoint->GetAreas().Find( ToTerrorNavArea( me->GetLastKnownArea() ) ) == m_checkpoint->GetAreas().InvalidIndex() )
		return Done( "We left the checkpoint" );
	if ( AreSurvivorsInDistress() ) return Done( "We're leaving the checkpoint to help a teammate" );
	if ( m_selectionTimer.GetRemainingTime() <= 0.0f )
	{
		m_selectionTimer.Start( RandomFloat( 1.0f, 2.0f ) );
		CBasePropDoor *door = m_checkpoint->GetOpenDoor();
		if ( door )
		{
			if ( m_closeDoorTimer.IsElapsed() )
			{
				m_closeDoorTimer.Start( sb_close_checkpoint_door_interval.GetFloat() );
				if ( IsCheckpointDoorCloser( me, m_checkpoint, door ) )
				{
					me->SpeakIfAllowed( "SurvivorBotMovingToCloseCheckpointDoor", 1 );
					me->GetBodyInterface()->AimHeadTowards( door->WorldSpaceCenter(), IBody::CRITICAL, 5.0f, NULL,
						"Look at door we're closing", true, 0.25f );
					return SuspendFor( new SurvivorUseObject( door ), "Closing the checkpoint door" );
				}
			}
			if ( !me->IsRangeLessThan( door, 150.0f ) ) me->GetIntentionInterface()->ChangeLegsAction( new SurvivorLegsWait );
			else
			{
				TerrorNavArea *area = m_checkpoint->GetFarthestSpawnArea( door->WorldSpaceCenter() );
				if ( area ) me->GetIntentionInterface()->ChangeLegsAction( new SurvivorLegsApproach( area->GetRandomPoint() ) );
			}
		}
	}
	ActionResult< SurvivorBot > selected = me->TrySaveFriendsInImmediateTrouble();
	if ( !selected.IsRequestingChange() ) selected = me->TryHealFriends();
	if ( !selected.IsRequestingChange() ) selected = me->TryReviveFriends();
	if ( !selected.IsRequestingChange() ) selected = me->TryScavenge();
	return selected.IsRequestingChange() ? ( selected.m_type == SUSPEND_FOR ? SuspendFor( selected.m_action, selected.m_reason ) : selected ) : Continue();
}

SurvivorCloseDoor::SurvivorCloseDoor( CBasePropDoor *door ) : m_door( door ) {}

ActionResult< SurvivorBot > SurvivorCloseDoor::OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction )
{
	me->SpeakIfAllowed( "SurvivorBotMovingToCloseDoorBehindUs", 1 );
	m_timeout.Start( 5.0f );
	me->GetIntentionInterface()->PushLegsAction( new SurvivorLegsApproach( m_door->WorldSpaceCenter() ) );
	return Continue();
}

ActionResult< SurvivorBot > SurvivorCloseDoor::Update( SurvivorBot *me, float interval )
{
	CBasePropDoor *door = m_door.Get();
	if ( !door ) return Done( "Door was destroyed" );
	if ( door->IsDoorClosed() ) return Done( "Door is closed" );
	if ( m_timeout.IsElapsed() ) return Done( "Taking too much time - giving up" );
	Vector position = door->WorldSpaceCenter();
	if ( me->IsRangeLessThan( position, 200.0f ) )
	{
		me->GetBodyInterface()->AimHeadTowards( position, IBody::INTERESTING, 1.0f, NULL, "Look at door we're closing", true, 0.25f );
		if ( me->IsRangeLessThan( position, 100.0f ) && door->IsDoorOpen() )
		{
			Vector direction = position - me->EyePosition();
			VectorNormalize( direction );
			if ( DotProduct( me->GetBodyInterface()->GetViewVector(), direction ) > 0.7f )
			{
				door->Use( me, me, USE_ON, 0.0f );
				me->OnPlayerUsed( door, USE_TOGGLE );
				me->SpeakIfAllowed( "SurvivorBotClosingDoorBehindUs", 1 );
				return Done( "Tried to close the door" );
			}
		}
	}
	return Continue();
}

ActionResult< SurvivorBot > SurvivorCloseDoor::OnResume( SurvivorBot *me, Action< SurvivorBot > *interruptingAction )
{
	return Done( "Exiting on resume" );
}

void SurvivorCloseDoor::OnEnd( SurvivorBot *me, Action< SurvivorBot > *nextAction )
{
	me->GetIntentionInterface()->PopLegsAction();
}

ActionResult< SurvivorBot > SurvivorDebugApproach::OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction )
{
	me->GetIntentionInterface()->PushLegsAction( new SurvivorLegsApproach( m_position ) );
	return Continue();
}

ActionResult< SurvivorBot > SurvivorDebugApproach::Update( SurvivorBot *me, float interval )
{
	if ( me->IsRangeLessThan( m_position, 25.0f ) )
	{
		if ( !m_waitTimer.HasStarted() ) m_waitTimer.Start( sb_debug_apoproach_wait_time.GetFloat() );
		else if ( m_waitTimer.IsElapsed() ) return Done( "Debug move to finished" );
	}
	return Continue();
}

void SurvivorDebugApproach::OnEnd( SurvivorBot *me, Action< SurvivorBot > *nextAction )
{
	me->GetIntentionInterface()->PopLegsAction();
}

namespace
{
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
	struct FlameEscapeGoal
	{
		SurvivorBot *actor;
		TerrorNavArea *area;
		float score;
		bool operator()( TerrorNavArea *candidate )
		{
			if ( candidate->GetCostSoFar() > score ) return false;
			SurvivorBotPathCost cost( actor );
			if ( cost( candidate, candidate->GetParent(), NULL, NULL, 1.0f ) < 0.0f ) return false;
			if ( !candidate->IsDamaging() )
			{
				float value = ( (float)candidate->IsUnderwater() * 200.0f /
					( candidate->GetSizeY() * candidate->GetSizeX() * 0.0004f ) ) + candidate->GetCostSoFar();
				if ( !( value > score ) ) { area = candidate; score = value; }
			}
			return true;
		}
	};
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
	struct SpitEscapeGoal
	{
		SurvivorBot *actor;
		TerrorNavArea *area;
		float score;
		bool operator()( TerrorNavArea *candidate )
		{
			if ( candidate->GetCostSoFar() > score ) return false;
			SurvivorBotPathCost cost( actor );
			if ( cost( candidate, candidate->GetParent(), NULL, NULL, 1.0f ) < 0.0f ) return false;
			if ( !candidate->IsDamaging() ) { area = candidate; score = candidate->GetCostSoFar(); }
			return true;
		}
	};
}

// S:3AE190/3AE370. The actual fire/acid damage events refresh these intervals;
// this action does not manufacture an independent inferno or damage clock.
ActionResult< SurvivorBot > SurvivorEscapeFlames::OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction )
{
	m_timeSinceFire.Start();
	m_repathTimer.Invalidate();
	m_escapeArea = NULL;
	me->GetIntentionInterface()->PushLegsAction( NULL );
	me->SpeakIfAllowed( "SurvivorBotEscapingFlames", 1 );
	return Continue();
}

void SurvivorEscapeFlames::OnEnd( SurvivorBot *me, Action< SurvivorBot > *nextAction )
{
	me->SpeakIfAllowed( "SurvivorBotHasEscapedFlames", 1 );
	me->GetIntentionInterface()->PopLegsAction();
}

EventDesiredResult< SurvivorBot > SurvivorEscapeFlames::OnIgnite( SurvivorBot *me )
{
	m_timeSinceFire.Start();
	return TryToSustain( RESULT_CRITICAL );
}

EventDesiredResult< SurvivorBot > SurvivorEscapeFlames::OnInjured( SurvivorBot *me, const CTakeDamageInfo &info )
{
	if ( info.GetDamageType() & DMG_BURN ) m_timeSinceFire.Start();
	return TryToSustain( RESULT_CRITICAL );
}

// S:3AED40/3AECD0/3AD990. Equal or unordered scores replace the previous area.
// The native underwater penalty keeps its zero-area division and float order.
ActionResult< SurvivorBot > SurvivorEscapeFlames::Update( SurvivorBot *me, float interval )
{
	TerrorNavArea *current = ToTerrorNavArea( me->GetLastKnownArea() );
	if ( !current ) return Done( "No last known area!" );
	if ( !m_timeSinceFire.HasStarted() || m_timeSinceFire.GetElapsedTime() >= 0.75f )
	{
		if ( me->GetLocomotionInterface()->IsAttemptingToMove() )
			me->GetIntentionInterface()->ChangeLegsAction( new SurvivorLegsWait );
		if ( !m_timeSinceFire.HasStarted() || m_timeSinceFire.GetElapsedTime() > 3.0f )
			return Done( "No longer in a damaging area" );
		return Continue();
	}
	if ( me->GetLocomotionInterface()->IsAttemptingToMove() && m_escapeArea &&
		!m_escapeArea->IsDamaging() && !current->IsDamaging() ) return Continue();
	if ( m_repathTimer.GetRemainingTime() > 0.0f ) return Continue();
	FlameEscapeGoal find = { me, NULL, FLT_MAX };
	CollectSurroundingTerrorAreas( current, me->GetAbsOrigin(), find, 1000.0f, 0, TEAM_ANY );
	m_escapeArea = find.area;
	if ( m_escapeArea )
	{
		me->GetIntentionInterface()->ChangeLegsAction( new SurvivorLegsApproach( m_escapeArea->GetCenter() ) );
		m_repathTimer.Start( 1.0f );
		return Continue();
	}
	Vector avoid = me->GetLocomotionInterface()->GetFeet() + me->GetLocomotionInterface()->GetMotionVector() * 10.0f;
	return ChangeTo( new BehaviorBackUp< SurvivorBot >( avoid ), "Can't find a way out of the fire, so I'm backing up!" );
}

// S:3AE280 preserves the original shared "EscapingFlames" concept for spit.
ActionResult< SurvivorBot > SurvivorEscapeSpit::OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction )
{
	m_timeSinceSpit.Start();
	m_repathTimer.Invalidate();
	m_escapeArea = NULL;
	me->GetIntentionInterface()->PushLegsAction( NULL );
	me->SpeakIfAllowed( "SurvivorBotEscapingFlames", 1 );
	m_nativeSpitFlag = false;
	return Continue();
}

void SurvivorEscapeSpit::OnEnd( SurvivorBot *me, Action< SurvivorBot > *nextAction )
{
	me->SpeakIfAllowed( "SurvivorBotHasEscapedSpit", 1 );
	me->GetIntentionInterface()->PopLegsAction();
}

EventDesiredResult< SurvivorBot > SurvivorEscapeSpit::OnEnteredSpit( SurvivorBot *me )
{
	m_timeSinceSpit.Start();
	m_nativeSpitFlag = false;
	return TryToSustain( RESULT_CRITICAL );
}

EventDesiredResult< SurvivorBot > SurvivorEscapeSpit::OnInjured( SurvivorBot *me, const CTakeDamageInfo &info )
{
	if ( info.GetDamageType() & 0x400 )
	{
		m_timeSinceSpit.Start();
		m_nativeSpitFlag = false;
	}
	return TryToSustain( RESULT_CRITICAL );
}

ActionResult< SurvivorBot > SurvivorEscapeSpit::Update( SurvivorBot *me, float interval )
{
	TerrorNavArea *current = ToTerrorNavArea( me->GetLastKnownArea() );
	if ( !current ) return Done( "No last known area!" );
	if ( !m_timeSinceSpit.HasStarted() || m_timeSinceSpit.GetElapsedTime() >= 1.0f )
	{
		if ( me->GetLocomotionInterface()->IsAttemptingToMove() )
			me->GetIntentionInterface()->ChangeLegsAction( new SurvivorLegsWait );
		if ( !m_timeSinceSpit.HasStarted() || m_timeSinceSpit.GetElapsedTime() > 6.0f )
			return Done( "Finished waiting out the spit" );
		return Continue();
	}
	if ( me->GetLocomotionInterface()->IsAttemptingToMove() && m_escapeArea &&
		!m_escapeArea->IsDamaging() && !current->IsDamaging() ) return Continue();
	if ( m_repathTimer.GetRemainingTime() > 0.0f ) return Continue();
	m_escapeArea = NULL;
	if ( !me->IsOnElevatorThatIsMoving() )
	{
		SpitEscapeGoal find = { me, NULL, FLT_MAX };
		CollectSurroundingTerrorAreas( current, me->GetAbsOrigin(), find, 1200.0f, 0, TEAM_ANY );
		m_escapeArea = find.area;
	}
	if ( m_escapeArea )
	{
		me->GetIntentionInterface()->ChangeLegsAction( new SurvivorLegsApproach( m_escapeArea->GetCenter() ) );
		m_repathTimer.Start( 1.0f );
		return Continue();
	}
	Vector avoid = me->GetLocomotionInterface()->GetFeet() + me->GetLocomotionInterface()->GetMotionVector() * 10.0f;
	return ChangeTo( new BehaviorBackUp< SurvivorBot >( avoid ), "Can't find a way out of the spit, so I'm backing up!" );
}
