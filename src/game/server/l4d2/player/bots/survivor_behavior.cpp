//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/bots/survivor_bot.h"
#include "l4d2/player/bots/survivor_behavior.h"
#include "l4d2/player/bots/survivor_coordinator_actions.h"
#include "l4d2/player/bots/survivor_attack.h"
#include "l4d2/player/bots/survivor_interaction.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/nav/terror_nav.h"
#include "l4d2/nav/terror_nav_world.h"
#include "l4d2/nav/terror_nav_area.h"
#include "l4d2/infected/common/infected.h"
#include "BasePropDoor.h"
#include "doors.h"
#include "props.h"
#include "NextBot/NextBotManager.h"
#include "NextBot/Player/NextBotPlayerBody.h"
#include "obstacle_pushaway.h"
#include "datacache/imdlcache.h"
#include "tier0/memdbgon.h"

extern ConVar z_gun_damage;
ConVar sb_hindrance_range( "sb_hindrance_range", "150", 0x00004004 );
ConVar l4d1_survivor_give_item( "l4d1_survivor_give_item", "0", 0x00000006 );


ActionResult< SurvivorBot > SurvivorBehavior::OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction )
{
	m_exitCheckpoint = TheTerrorNavMesh()->GetExitCheckpoint();
	me->FlashlightTurnOff();
	m_startedLeavingCheckpoint = false;
	return Continue();
}

ActionResult< SurvivorBot > SurvivorBehavior::OnResume( SurvivorBot *me, Action< SurvivorBot > *interruptingAction )
{
	me->FlashlightTurnOff();
	return Continue();
}

Action< SurvivorBot > *SurvivorBehavior::InitialContainedAction( SurvivorBot *me ) { return new SurvivorAttack; }
Action< SurvivorBot > *L4D1SurvivorBehavior::InitialContainedAction( SurvivorBot *me ) { return new SurvivorAttack; }

// S3ABE90: selection order and suspension bookkeeping belong to the root.
ActionResult< SurvivorBot > SurvivorBehavior::Update( SurvivorBot *me, float interval )
{
	SurvivorTeamSituation &team = me->GetTeamSituation();
	if ( !TheDirector->HasAnySurvivorLeftSafeArea() && !m_startedLeavingCheckpoint )
	{
		m_startedLeavingCheckpoint = true;
		return SuspendFor( new SurvivorCheckpointLeaving, "Haven't left the initial checkpoint yet" );
	}
	if ( m_exitCheckpoint && m_exitCheckpoint->GetAreas().Find( ToTerrorNavArea( me->GetLastKnownArea() ) ) != m_exitCheckpoint->GetAreas().InvalidIndex() &&
		!AreSurvivorsInDistress() )
		return SuspendFor( new SurvivorCheckpointReached, "I reached the checkpoint!" );
	if ( me->GetGroundElevator() ) return SuspendFor( new SurvivorElevatorEnter, "I entered an elevator" );
	if ( !me->IsSafeForCombatAction() ) return Continue();
	ActionResult< SurvivorBot > result = me->TrySaveFriendsInImmediateTrouble();
	if ( result.IsRequestingChange() )
		return result.m_type == SUSPEND_FOR ? SuspendFor( result.m_action, result.m_reason ) : result;
	if ( m_selectionTimer.GetRemainingTime() <= 0.0f )
	{
		m_selectionTimer.Start( RandomFloat( 1.0f, 2.0f ) );
		result = me->TryHealFriends();
		if ( !result.IsRequestingChange() ) result = me->TryReviveFriends();
		if ( !result.IsRequestingChange() ) result = me->TryScavenge();
		if ( !result.IsRequestingChange() ) result = me->TryUseObjects();
		if ( result.IsRequestingChange() )
			return result.m_type == SUSPEND_FOR ? SuspendFor( result.m_action, result.m_reason ) : result;
	}
	CTerrorPlayer *leader = team.GetLeader();
	PathFollower *path = leader ? team.GetPathToFriend( leader ) : NULL;
	if ( ( leader && ( !path || path->GetLength() > 750.0f ) ) || me->IsInCombat( false ) )
	{
		m_closeDoorTimer.Invalidate();
		return Continue();
	}
	if ( m_closeDoorTimer.HasStarted() && m_closeDoorTimer.GetRemainingTime() <= 0.0f && m_door.Get() )
	{
		m_closeDoorTimer.Invalidate();
		if ( m_door->IsDoorOpen() ) return SuspendFor( new SurvivorCloseDoor( m_door.Get() ), "Closing door behind us" );
	}
	return Continue();
}

ActionResult< SurvivorBot > L4D1SurvivorBehavior::OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction )
{
	me->FlashlightTurnOff();
	return Continue();
}

ActionResult< SurvivorBot > L4D1SurvivorBehavior::OnResume( SurvivorBot *me, Action< SurvivorBot > *interruptingAction )
{
	me->FlashlightTurnOff();
	return Continue();
}

ActionResult< SurvivorBot > L4D1SurvivorBehavior::Update( SurvivorBot *me, float interval )
{
	if ( !me->IsSafeForCombatAction() || m_selectionTimer.GetRemainingTime() > 0.0f ) return Continue();
	m_selectionTimer.Start( RandomFloat( 1.0f, 2.0f ) );
	ActionResult< SurvivorBot > result = me->TryScavengeForL4D1Survivor();
	if ( result.IsRequestingChange() )
		return result.m_type == SUSPEND_FOR ? SuspendFor( result.m_action, result.m_reason ) : result;
	if ( me->GetSurvivorCharacter() == SurvivorCharacter_Manager )
	{
		result = me->TryUseMinigun();
		if ( result.IsRequestingChange() )
			return result.m_type == SUSPEND_FOR ? SuspendFor( result.m_action, result.m_reason ) : result;
	}
	else if ( l4d1_survivor_give_item.GetBool() ||
		( !me->GetLocomotionInterface()->IsClimbingOrJumping() && TheDirector->ConsumeL4D1SurvivorItemRequest() ) )
		return SuspendFor( new SurvivorAttractAndGiveItem );
	return Continue();
}

class SurvivorOpenDoorReply : public INextBotReply
{
public:
	virtual void OnSuccess( INextBot *bot ) OVERRIDE
	{
		INextBotPlayerInput *input = dynamic_cast< INextBotPlayerInput * >( bot->GetEntity() );
		if ( input ) input->PressUseButton();
	}
};

EventDesiredResult< SurvivorBot > SurvivorBehavior::OnContact( SurvivorBot *me, CBaseEntity *other, CGameTrace *result )
{
	if ( !other ) return TryContinue();
	// The odd-frame gate is native framecount+4, NOT a mode/deathmatch flag.
	if ( ( gpGlobals->framecount & 1 ) && me->IsUseableEntity( other, 0 ) &&
		( FClassnameIs( other, "prop_door*" ) || FClassnameIs( other, "func_door*" ) ) )
	{
		CBasePropDoor *prop = dynamic_cast< CBasePropDoor * >( other );
		CBaseDoor *brush = prop ? NULL : dynamic_cast< CBaseDoor * >( other );
		if ( prop || brush )
		{
			if ( prop ? prop->IsDoorLocked() : brush->m_bLocked ) me->PressUseButton();
			else if ( !prop || ( !prop->IsDoorOpen() && !prop->IsDoorOpening() && !prop->IsDoorClosing() ) )
			{
				static SurvivorOpenDoorReply reply;
				me->GetBodyInterface()->AimHeadTowards( other, IBody::CRITICAL, 1.0f, &reply, "Look at door to open it", true, 0.25f );
			}
		}
	}
	else
	{
		CTakeDamageInfo damage( me, me, me->GetActiveWeapon(), vec3_origin, vec3_origin, z_gun_damage.GetFloat(), DMG_CLUB );
		if ( IsBreakableEntity( other, false, false, &damage ) && TheDirector->GetScriptedMode()->CanSurvivorBotBreak( other ) )
		{
			static PressAltFireButtonReply reply;
			me->GetBodyInterface()->AimHeadTowards( other, IBody::CRITICAL, 1.0f, &reply, "Melee smash breakable in our way", true, 0.25f );
		}
	}
	return TryContinue();
}

EventDesiredResult< SurvivorBot > SurvivorBehavior::OnIgnite( SurvivorBot *me )
{
	return TrySuspendFor( new SurvivorEscapeFlames, RESULT_CRITICAL, "I'm burning!" );
}

EventDesiredResult< SurvivorBot > SurvivorBehavior::OnInjured( SurvivorBot *me, const CTakeDamageInfo &info )
{
	if ( me->GetLocomotionInterface()->IsUsingLadder() )
		return TrySuspendFor( new SurvivorEscapeLadderAmbush, RESULT_CRITICAL, "I'm being attacked while helpless on a ladder!" );
	CBaseEntity *attacker = info.GetAttacker();
	Infected *infected = attacker ? attacker->MyInfectedPointer() : NULL;
	if ( infected ) me->GetIntentionInterface()->AddKnownThreat( infected );
	return TryContinue();
}

static void WarnIncoming( SurvivorBot *me )
{
	SurvivorVision *vision = me->GetVisionInterface();
	const CUtlVector< VisionKnownEntity > &known = vision->GetKnownEntities();
	int count = 0;
	FOR_EACH_VEC( known, i )
	{
		if ( !( gpGlobals->curtime >= known[i].recognizeAt ) ) continue;
		CBaseEntity *entity = known[i].entity.Get();
		INextBot *bot = entity ? entity->MyNextBotPointer() : NULL;
		if ( bot && bot->GetBodyInterface()->IsArousal( IBody::INTENSE ) ) ++count;
	}
	if ( count >= 8 ) me->SpeakConcept( "SurvivorBotWarnIncoming", 2, RandomFloat( 0.5f, 1.0f ), NULL );
}

EventDesiredResult< SurvivorBot > SurvivorBehavior::OnSight( SurvivorBot *me, CBaseEntity *subject ) { WarnIncoming( me ); return TryContinue(); }
EventDesiredResult< SurvivorBot > L4D1SurvivorBehavior::OnSight( SurvivorBot *me, CBaseEntity *subject ) { WarnIncoming( me ); return TryContinue(); }

EventDesiredResult< SurvivorBot > SurvivorBehavior::OnNavAreaChanged( SurvivorBot *me, CNavArea *enteredArea, CNavArea *leftArea )
{
	if ( leftArea && ToTerrorNavArea( leftArea )->GetDoor() &&
		me->GetTeamSituation().GetTrailingFriendFlow() >= ToTerrorNavArea( leftArea )->GetFlowDistance() )
	{
		CBasePropDoor *door = dynamic_cast< CBasePropDoor * >( ToTerrorNavArea( leftArea )->GetDoor() );
		if ( door )
		{
			m_closeDoorTimer.Start( RandomFloat( 0.5f, 0.7f ) );
			m_door = door;
		}
	}
	return TryContinue();
}

EventDesiredResult< SurvivorBot > SurvivorBehavior::OnPickUp( SurvivorBot *me, CBaseEntity *item, CBaseCombatCharacter *giver )
{
	if ( item && giver && !me->IsSelf( giver ) )
		me->SpeakConcept( "SurvivorBotThanksForGift", 1, RandomFloat( 1.0f, 2.0f ), giver );
	return TryContinue();
}

EventDesiredResult< SurvivorBot > SurvivorBehavior::OnShoved( SurvivorBot *me, CBaseEntity *pusher )
{
	CBasePlayer *player = pusher && pusher->IsPlayer() ? static_cast< CBasePlayer * >( pusher ) : NULL;
	if ( player && !player->IsFakeClient() ) me->SpeakConcept( "SurvivorBotWasShoved", 2, RandomFloat( 1.0f, 2.0f ), NULL );
	return TryContinue();
}

EventDesiredResult< SurvivorBot > SurvivorBehavior::OnCommandApproach( SurvivorBot *me, const Vector &position, float range )
{
	return TrySuspendFor( new SurvivorDebugApproach( position ), RESULT_CRITICAL, "Got debug command to move somewhere" );
}

class NearestSurvivorWarningTarget
{
public:
	NearestSurvivorWarningTarget( const Vector &origin, int zombieClass ) : m_origin( origin ), m_class( zombieClass ), m_bestRange( FLT_MAX ), target( NULL ) {}
	bool operator()( INextBot *bot )
	{
		CBaseCombatCharacter *entity = bot->GetEntity();
		float range = ( entity->GetAbsOrigin() - m_origin ).LengthSqr();
		if ( range < m_bestRange && entity->MyCombatCharacterPointer() && GetTerrorEntityZombieClass( entity ) == m_class )
		{ m_bestRange = range; target = entity; }
		return true;
	}
	const Vector &m_origin;
	int m_class;
	float m_bestRange;
	CBaseCombatCharacter *target;
};

EventDesiredResult< SurvivorBot > SurvivorBehavior::OnSpokeConcept( SurvivorBot *me, CBaseCombatCharacter *who, AIConcept_t concept, AI_Response *response )
{
	if ( CompareConcepts( concept, "PlayerAskReady" ) )
	{
		CFmtStr subject( "Subject:%s", GetSurvivorBotSubjectName( who ) );
		me->SpeakIfAllowed( "SurvivorBotYesReady", 1, subject );
	}
	else if ( CompareConcepts( concept, "PlayerHurrah" ) )
	{
		CFmtStr subject( "Subject:%s", GetSurvivorBotSubjectName( who ) );
		me->SpeakIfAllowed( "SurvivorBotHurrahAlso", 1, subject );
	}
	else if ( CompareConcepts( concept, "PlayerWarnWitch" ) || CompareConcepts( concept, "PlayerWarnTank" ) )
	{
		bool witch = CompareConcepts( concept, "PlayerWarnWitch" );
		NearestSurvivorWarningTarget scan( me->GetAbsOrigin(), witch ? 7 : 8 );
		TheNextBots().ForEachBot( scan );
		if ( scan.target ) me->GetIntentionInterface()->AddKnownThreat( scan.target );
		else if ( me->IsDebugging( NEXTBOT_ERRORS ) )
			DevMsg( "%3.2f: %s: Someone called out '%s', but there is no %s!\n", gpGlobals->curtime, me->GetDebugIdentifier(), witch ? "witch" : "tank", witch ? "Witch" : "Tank" );
	}
	if ( CompareConcepts( concept, "PlayerMoveOn" ) || CompareConcepts( concept, "PlayerHurryUp" ) ||
		CompareConcepts( concept, "PlayerStayTogether" ) || CompareConcepts( concept, "PlayerFollowMe" ) )
	{
		CFmtStr subject( "Subject:%s", GetSurvivorBotSubjectName( who ) );
		if ( ( me->GetAbsOrigin() - who->GetAbsOrigin() ).LengthSqr() >= 160000.0f )
		{
			if ( me->IsSafeForCombatAction() ) me->SpeakIfAllowed( "SurvivorBotReassureComing", 1, subject );
		}
		else me->SpeakIfAllowed( "SurvivorBotReassureNearby", 1, subject );
		m_comeHereTimer.Start( RandomFloat( 10.0f, 20.0f ) );
	}
	if ( CompareConcepts( concept, "PlayerThanks" ) && who &&
		( who->GetTeamNumber() == TEAM_SURVIVOR || who->GetTeamNumber() == TEAM_L4D1_SURVIVOR ) )
	{
		CTerrorPlayer *player = ToTerrorPlayer( who );
		unsigned int character = static_cast< unsigned int >( player->GetSurvivorCharacter() );
		if ( character <= 7 && me->GetFriendActionTimer( player->GetSurvivorCharacter() ).HasStarted() &&
			me->GetFriendActionTimer( player->GetSurvivorCharacter() ).GetRemainingTime() > 0.0f )
		{
			CFmtStr subject( "Subject:%s", GetSurvivorBotSubjectName( who ) );
			me->SpeakIfAllowed( "SurvivorBotYouAreWelcome", 1, subject );
		}
	}
	if ( CompareConcepts( concept, "PlayerKillThatLight" ) )
	{
		me->SuppressFlashlight( RandomFloat( 30.0f, 45.0f ) );
		CFmtStr subject( "Subject:%s", GetSurvivorBotSubjectName( who ) );
		me->SpeakIfAllowed( "SurvivorBotYesFlashlightOff", 1, subject );
	}
	return TryContinue();
}

QueryResultType SurvivorBehavior::ShouldHurry( const INextBot *me ) const
{
	if ( m_comeHereTimer.HasStarted() && m_comeHereTimer.GetRemainingTime() > 0.0f ) return ANSWER_YES;
	return TerrorGameRules()->IsPlayerControlledZombies() ? ANSWER_YES : ANSWER_UNDEFINED;
}

static QueryResultType IsVisibleInfectedHindrance( const INextBot *me, CBaseEntity *blocker )
{
	if ( blocker->GetTeamNumber() != TEAM_INFECTED ) return ANSWER_UNDEFINED;
	int zombieClass = GetTerrorEntityZombieClass( blocker );
	if ( zombieClass == 8 || zombieClass == 7 ) return ANSWER_UNDEFINED;
	return me->GetVisionInterface()->IsAbleToSee( blocker, IVision::DISREGARD_FOV ) ? ANSWER_YES : ANSWER_UNDEFINED;
}

QueryResultType L4D1SurvivorBehavior::IsHindrance( const INextBot *me, CBaseEntity *blocker ) const
{
	if ( blocker == IS_ANY_HINDRANCE_POSSIBLE ) return ANSWER_YES;
	if ( me->IsRangeGreaterThan( blocker, sb_hindrance_range.GetFloat() ) ) return ANSWER_NO;
	if ( blocker->GetTeamNumber() == TEAM_SURVIVOR || blocker->GetTeamNumber() == TEAM_L4D1_SURVIVOR ) return ANSWER_NO;
	return IsVisibleInfectedHindrance( me, blocker );
}
QueryResultType SurvivorBehavior::IsHindrance( const INextBot *me, CBaseEntity *blocker ) const
{
	if ( blocker == IS_ANY_HINDRANCE_POSSIBLE ) return ANSWER_YES;
	if ( TerrorGameRules()->IsVersusMode() ) return ANSWER_NO;
	SurvivorBot *actor = static_cast< SurvivorBot * >( me->GetEntity() );
	if ( me->IsRangeGreaterThan( blocker, sb_hindrance_range.GetFloat() ) ||
		blocker->GetTeamNumber() == TEAM_SURVIVOR || blocker->GetTeamNumber() == TEAM_L4D1_SURVIVOR || actor->IsNearCheckpoint() ) return ANSWER_NO;
	SurvivorTeamSituation &team = actor->GetTeamSituation();
	CTerrorPlayer *leader = team.GetLeadingHuman();
	if ( leader && IsSurvivorBehind( actor, leader ) && team.IsTeamNearCheckpoint() ) return ANSWER_NO;
	if ( TheDirector->IsFinale() && TheDirector->IsRescueVehicleReady() ) return ANSWER_NO;
	return IsVisibleInfectedHindrance( me, blocker );
}

static Vector SurvivorAimPoint( const CBaseCombatCharacter *subject )
{
	if ( !subject ) return vec3_origin;
	CBaseCombatCharacter *target = const_cast< CBaseCombatCharacter * >( subject );
	const Vector origin = target->GetAbsOrigin();
	Vector mins, maxs;
	target->CollisionProp()->WorldSpaceAABB( &mins, &maxs );
	float height = 0.85f;
	int zombieClass = GetTerrorEntityZombieClass( target );
	if ( target->GetTeamNumber() == TEAM_INFECTED && ( zombieClass == 7 || zombieClass == 3 ) ) height = 0.25f;
	else if ( target->GetTeamNumber() == TEAM_INFECTED && zombieClass == 5 )
	{
		CTerrorPlayer *jockey = ToTerrorPlayer( target );
		if ( jockey && !jockey->GetJockeyVictim() ) height = 0.25f;
	}
	else
	{
		Infected *infected = target->MyInfectedPointer();
		if ( infected )
		{
			if ( infected->IsShoved() || infected->GetLocomotionInterface()->IsScrambling() )
			{
				MDLCACHE_CRITICAL_SECTION();
				Vector head; QAngle angles;
				target->GetAttachment( "Head", head, angles );
				return head;
			}
			switch ( infected->GetBodyInterface()->GetActualPosture() )
			{
			case IBody::STAND: height = 0.6f; break;
			case IBody::CROUCH: case IBody::SIT: height = 0.5f; break;
			case IBody::LIE: height = 0.25f; break;
			default: break;
			}
		}
	}
	return Vector( origin.x, origin.y, origin.z + ( maxs.z - mins.z ) * height );
}
Vector SurvivorBehavior::SelectTargetPoint( const INextBot *me, const CBaseCombatCharacter *subject ) const { return SurvivorAimPoint( subject ); }
Vector L4D1SurvivorBehavior::SelectTargetPoint( const INextBot *me, const CBaseCombatCharacter *subject ) const { return SurvivorAimPoint( subject ); }

static CBaseEntity *MoreDangerousSurvivorThreat( const INextBot *me, const CBaseCombatCharacter *subject, CBaseEntity *first, CBaseEntity *second )
{
	CTerrorPlayer *player = subject && subject->IsPlayer() ? static_cast< CTerrorPlayer * >( const_cast< CBaseCombatCharacter * >( subject ) ) : NULL;
	SurvivorIntention *intention = static_cast< SurvivorBot * >( me->GetEntity() )->GetIntentionInterface();
	bool firstImmediate = intention->IsImmediateThreat( player, first );
	bool secondImmediate = intention->IsImmediateThreat( player, second );
	if ( firstImmediate || secondImmediate )
	{
		if ( firstImmediate != secondImmediate ) return firstImmediate ? first : second;
	}
	else
	{
		int firstClass = GetTerrorEntityZombieClass( first ), secondClass = GetTerrorEntityZombieClass( second );
		if ( firstClass != secondClass )
		{
			if ( firstClass == 8 ) return first;
			if ( secondClass == 8 ) return second;
		}
		bool firstDangerous = intention->IsThreatDangerous( player, first );
		bool secondDangerous = intention->IsThreatDangerous( player, second );
		if ( firstDangerous != secondDangerous ) return firstDangerous ? first : second;
	}
	const Vector &firstOrigin = first->GetAbsOrigin();
	const Vector &firstSubjectOrigin = player->GetAbsOrigin();
	const float firstRange = ( firstSubjectOrigin - firstOrigin ).LengthSqr();
	const Vector &secondOrigin = second->GetAbsOrigin();
	const Vector &secondSubjectOrigin = player->GetAbsOrigin();
	const float secondRange = ( secondSubjectOrigin - secondOrigin ).LengthSqr();
	return secondRange <= firstRange ? second : first;
}
CBaseEntity *SurvivorBehavior::SelectMoreDangerousThreat( const INextBot *me, const CBaseCombatCharacter *subject, CBaseEntity *first, CBaseEntity *second ) const
{ return MoreDangerousSurvivorThreat( me, subject, first, second ); }
CBaseEntity *L4D1SurvivorBehavior::SelectMoreDangerousThreat( const INextBot *me, const CBaseCombatCharacter *subject, CBaseEntity *first, CBaseEntity *second ) const
{ return MoreDangerousSurvivorThreat( me, subject, first, second ); }

// S:3AC340. Actual CTerrorPlayer::OnEnteredSpit emits this game event.
EventDesiredResult< SurvivorBot > SurvivorBehavior::OnEnteredSpit( SurvivorBot *me )
{
	return TrySuspendFor( new SurvivorEscapeSpit, RESULT_CRITICAL, "I'm in acid spit!" );
}
