//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/bots/survivor_attack.h"
#include "l4d2/player/bots/survivor_bot.h"
#include "l4d2/player/bots/survivor_legs.h"
#include "l4d2/player/bots/survivor_body.h"
#include "l4d2/player/bots/survivor_vision.h"
#include "l4d2/player/bots/survivor_locomotion.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/weapons/terror_weapon.h"
#include "NextBot/Player/NextBotPlayerBody.h"
#include "tier0/memdbgon.h"

namespace
{
	PressAltFireButtonReply s_liberationShove;

	bool HasLiberationMeleeWeapon( SurvivorBot *me )
	{
		return me->IsUsingWeaponType( TERROR_WEAPONTYPE_MELEE ) || me->IsUsingWeaponType( TERROR_WEAPONTYPE_CHAINSAW );
	}
}

ActionResult< SurvivorBot > SurvivorEscapeLadderAmbush::OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction )
{
	me->GetIntentionInterface()->PushLegsAction( new SurvivorLegsWait( 0.0f ) );
	return Continue();
}

ActionResult< SurvivorBot > SurvivorEscapeLadderAmbush::Update( SurvivorBot *me, float interval )
{
	me->PressJumpButton();
	me->PressBackwardButton();
	if ( me->GetMoveType() != MOVETYPE_LADDER )
		return Done( "I'm off of the ladder" );
	return Continue();
}

void SurvivorEscapeLadderAmbush::OnEnd( SurvivorBot *me, Action< SurvivorBot > *nextAction )
{
	me->GetIntentionInterface()->PopLegsAction();
}

Action< SurvivorBot > *SurvivorEscapeLadderAmbush::InitialContainedAction( SurvivorBot *me )
{
	return new SurvivorAttack;
}

SurvivorLiberateBesiegedFriend::SurvivorLiberateBesiegedFriend( CTerrorPlayer *friendPlayer, Action< SurvivorBot > *postLiberationAction ) : m_friend( friendPlayer )
{
	m_postLiberationAction = postLiberationAction;
	m_isApproaching = false;
}

ActionResult< SurvivorBot > SurvivorLiberateBesiegedFriend::OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction )
{
	me->GetIntentionInterface()->PushLegsAction( new SurvivorLegsApproach( m_friend.Get() ) );
	m_isApproaching = true;
	return Continue();
}

Action< SurvivorBot > *SurvivorLiberateBesiegedFriend::InitialContainedAction( SurvivorBot *me )
{
	return new SurvivorAttack;
}

void SurvivorLiberateBesiegedFriend::OnEnd( SurvivorBot *me, Action< SurvivorBot > *nextAction )
{
	me->GetIntentionInterface()->PopLegsAction();
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
EventDesiredResult< SurvivorBot > SurvivorLiberateBesiegedFriend::OnInjured( SurvivorBot *me, const CTakeDamageInfo &info )
{
	// S3B7637 captures permanent health before the eventful buffer query.
	const int health = me->GetHealth();
	if ( (double)me->GetHealthBuffer() + (double)health < 50.0 )
		return TryDone( RESULT_IMPORTANT, "Ouch! I have to defend myself!" );
	if ( me->GetLocomotionInterface()->IsUsingLadder() )
		return TrySuspendFor( new SurvivorEscapeLadderAmbush, RESULT_CRITICAL, "I'm being attacked while helpless on a ladder!" );
	return TryToSustain( RESULT_TRY );
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

EventDesiredResult< SurvivorBot > SurvivorLiberateBesiegedFriend::OnThreatChanged( SurvivorBot *me, CBaseCombatCharacter *threat )
{
	return TryToSustain( RESULT_TRY );
}

CBaseEntity *SurvivorLiberateBesiegedFriend::SelectMoreDangerousThreat( const INextBot *bot, const CBaseCombatCharacter *subject, CBaseEntity *first, CBaseEntity *second ) const
{
	if ( !bot->IsSelf( subject ) || !bot->IsRangeLessThan( m_friend.Get(), 300.0f ) ||
		!bot->GetVisionInterface()->IsAbleToSee( m_friend.Get(), IVision::DISREGARD_FOV ) )
		return NULL;
	// S3B65A0 re-enters the same intention relative to the friend. IsSelf,
	// not IsFriend, makes this callback decline the recursive friend query.
	return bot->GetIntentionInterface()->SelectMoreDangerousThreat( bot, m_friend.Get(), first, second );
}

ActionResult< SurvivorBot > SurvivorLiberateBesiegedFriend::Update( SurvivorBot *me, float interval )
{
	CTerrorPlayer *friendPlayer = m_friend.Get();
	if ( !friendPlayer || !friendPlayer->IsAlive() )
	{
		// The original deletes this action here, but not on the other abort
		// paths and not in its destructor. Preserve that ownership asymmetry.
		delete m_postLiberationAction;
		return Done( "We're too late, they didn't make it" );
	}
	if ( !me->CanReachSurvivor( friendPlayer ) )
		return Done( "They are unreachable" );
	if ( TheDirector->IsRescueVehicleReady() && friendPlayer->IsBot() && friendPlayer->IsIncapacitated() && CountHumansNearRescueVehicle() > 0 )
		return Done( "Breaking off rescue because humans have entered the rescue vehicle.\n" );

	bool melee = HasLiberationMeleeWeapon( me );
	float closeRange = 50.0f;
	if ( ( melee && friendPlayer->IsDominated() ) ||
		( friendPlayer->IsIncapacitated() && !friendPlayer->IsDominated() && !friendPlayer->m_isHangingFromLedge &&
			me->WorldSpaceCenter().z - friendPlayer->WorldSpaceCenter().z >= 12.0f ) )
		closeRange = 24.0f;

	if ( me->IsRangeLessThan( friendPlayer, closeRange ) )
	{
		if ( friendPlayer->GetTongueOwner() && m_shoveTimer.GetRemainingTime() <= 0.0f )
		{
			me->GetBodyInterface()->AimHeadTowards( friendPlayer, IBody::CRITICAL, 0.5f, &s_liberationShove, "Shoving friend off tongue", true, 0.25f );
			m_shoveTimer.Start( RandomFloat( 1.0f, 2.0f ) );
		}
		if ( friendPlayer->GetTimeSinceEnemyInjury() > 1.5f || !friendPlayer->IsBot() )
		{
			bool visible;
			if ( friendPlayer->m_isHangingFromLedge )
			{
				me->PressWalkButton();
				visible = me->GetVisionInterface()->IsAbleToSee( friendPlayer->EyePosition(), IVision::DISREGARD_FOV );
			}
			else
			{
				visible = me->GetVisionInterface()->IsAbleToSee( friendPlayer, IVision::DISREGARD_FOV );
			}
			if ( visible )
			{
				if ( m_postLiberationAction )
					return ChangeTo( m_postLiberationAction, "Reached our besieged friend" );
				return Done( "Reached our besieged friend, and have no explicit post-liberation action" );
			}
		}
		return Continue();
	}

	if ( friendPlayer->GetTongueOwner() )
	{
		CTerrorPlayer *smoker = friendPlayer->GetTongueOwner();
		if ( fabs( smoker->WorldSpaceCenter().z - friendPlayer->WorldSpaceCenter().z ) > 60.0f )
		{
			const SurvivorKnownEntity *known = me->GetIntentionInterface()->GetPrimaryKnownThreat();
			CBaseCombatCharacter *threat = known ? known->GetEntity() : NULL;
			if ( !known || threat->GetZombieClass() < 1 || threat->GetZombieClass() > 6 ||
				( threat->GetTeamNumber() == TEAM_INFECTED && threat->GetZombieClass() == 1 ) )
			{
				if ( TheDirector->GetTankCount() > 0 || TheDirector->GetWitchCount() > 0 )
					return Continue();
				// Native INextBot v304 is the L4D 2D range overload, absent
				// from the SDK interface. It compares distance, not squared range.
				float smokerRange = ( me->GetAbsOrigin() - smoker->GetAbsOrigin() ).AsVector2D().Length();
				bool aimAtSmoker = smokerRange > 600.0f || !me->IsRespondingToSurvivorDistress() || melee;
				if ( !aimAtSmoker )
				{
					me->GetIntentionInterface()->ChangeLegsAction( new SurvivorLegsWait( 0.0f ) );
					m_isApproaching = false;
					aimAtSmoker = me->GetVisionInterface()->IsAbleToSee( smoker, IVision::DISREGARD_FOV );
				}
				if ( aimAtSmoker )
					me->GetBodyInterface()->AimHeadTowards( smoker, IBody::CRITICAL, 0.5f, NULL, "Want to shoot the smoker holding my friend!", true, 0.25f );
			}
		}
		return Continue();
	}

	bool closeAndVisible = me->IsRangeLessThan( friendPlayer, 300.0f ) && me->GetVisionInterface()->IsAbleToSee( friendPlayer, IVision::DISREGARD_FOV );
	const SurvivorKnownEntity *known = me->GetIntentionInterface()->GetPrimaryKnownThreat();
	if ( closeAndVisible )
	{
		if ( known && known->IsVisibleNow() )
		{
			me->GetBodyInterface()->AimHeadTowards( known->GetEntity(), IBody::CRITICAL, 0.5f, NULL, "Trying to face the infected that has my friend", true, 0.25f );
			if ( m_isApproaching && me->GetTimeSinceWeaponFired() < 1.0f && !me->IsRespondingToSurvivorDistress() && !melee )
			{
				me->GetIntentionInterface()->ChangeLegsAction( new SurvivorLegsWait( 0.0f ) );
				m_isApproaching = false;
			}
		}
		else if ( !m_isApproaching )
		{
			me->GetIntentionInterface()->ChangeLegsAction( new SurvivorLegsApproach( friendPlayer ) );
			m_isApproaching = true;
		}
		if ( known && ( me->GetIntentionInterface()->IsImmediateThreat( friendPlayer, known->GetEntity() ) || me->GetIntentionInterface()->IsImmediateThreat( me, known->GetEntity() ) ) )
			return Continue();
		if ( friendPlayer->GetTimeSinceEnemyInjury() <= 1.5f )
			return Continue();
		if ( m_postLiberationAction )
			return ChangeTo( m_postLiberationAction, "I don't see any enemies attacking my friend" );
		return Done( "I don't see any enemies attacking my friend, and have no explicit post-liberation action" );
	}
	if ( m_isApproaching )
	{
		if ( known && known->IsVisibleNow() )
			me->GetBodyInterface()->AimHeadTowards( known->GetEntity(), IBody::CRITICAL, 0.5f, NULL, "Trying to face the infected that has my friend", true, 0.25f );
	}
	else
	{
		me->GetIntentionInterface()->ChangeLegsAction( new SurvivorLegsApproach( friendPlayer ) );
		m_isApproaching = true;
	}
	return Continue();
}
