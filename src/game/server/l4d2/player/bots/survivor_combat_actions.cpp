//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/bots/survivor_attack.h"
#include "l4d2/player/bots/survivor_bot.h"
#include "l4d2/player/bots/survivor_body.h"
#include "l4d2/player/terrorplayer.h"
#include "NextBot/Player/NextBotPlayerBody.h"
#include "tier0/memdbgon.h"

namespace
{
	PressFireButtonReply s_fireWhenAimed;
	PressAltFireButtonReply s_shoveWhenAimed;
}

SurvivorAmbushBoomer::SurvivorAmbushBoomer( CTerrorPlayer *boomer ) : SurvivorUseObject( boomer )
{
	m_hasShoved = false;
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
bool SurvivorAmbushBoomer::ShouldGiveUp( SurvivorBot *me )
{
	if ( m_hasShoved )
		return true;
	CTerrorPlayer *boomer = ToTerrorPlayer( m_object.Get() );
	return !boomer || boomer->IsNearShoveLimit() || !boomer->m_stunTimer.IsElapsed();
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

void SurvivorAmbushBoomer::OnObjectUsed( SurvivorBot *me )
{
	me->PressAltFireButton();
	m_hasShoved = true;
	SurvivorUseObject::OnObjectUsed( me );
}

SurvivorDislodgeVictim::SurvivorDislodgeVictim( CTerrorPlayer *victim, bool shove ) : SurvivorUseObject( victim )
{
	m_shove = shove;
}

ActionResult< SurvivorBot > SurvivorDislodgeVictim::OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction )
{
	me->ReleaseCrouchButton();
	return SurvivorUseObject::OnStart( me, priorAction );
}

Action< SurvivorBot > *SurvivorDislodgeVictim::InitialContainedAction( SurvivorBot *me )
{
	return new SurvivorAttack;
}

const Vector &SurvivorDislodgeVictim::GetUsePosition( SurvivorBot *me, CBaseEntity *object )
{
	return object->WorldSpaceCenter();
}

bool SurvivorDislodgeVictim::ShouldGiveUp( SurvivorBot *me )
{
	CTerrorPlayer *victim = ToTerrorPlayer( m_object.Get() );
	if ( !victim )
		return true;
	// The original tests exactly these three victim-side pin owners. Charger
	// carry/pummel and a generic IsPinned() predicate are not interchangeable.
	if ( !victim->GetTongueOwner() && !victim->m_pounceAttacker.Get() && !victim->m_jockeyAttacker.Get() )
		return true;
	return HasUsedObject( me );
}

void SurvivorDislodgeVictim::OnObjectUsed( SurvivorBot *me )
{
	if ( m_shove )
	{
		me->GetBodyInterface()->AimHeadTowards( m_object.Get(), IBody::MANDATORY, 1.0f, &s_shoveWhenAimed, "Dislodging enemy from my friend!", true, 0.25f );
		CTerrorPlayer *victim = ToTerrorPlayer( m_object.Get() );
		if ( victim && ( victim->GetTeamNumber() == TEAM_SURVIVOR || victim->GetTeamNumber() == TEAM_L4D1_SURVIVOR ) )
			me->NoteFriendDislodgeAttempt( victim->GetSurvivorCharacter(), 10.0f );
	}
	else
	{
		me->GetBodyInterface()->AimHeadTowards( m_object.Get(), IBody::MANDATORY, 1.0f, &s_fireWhenAimed, "Trying to dislodge by attacking", true, 0.25f );
	}
	SurvivorUseObject::OnObjectUsed( me );
}

EventDesiredResult< SurvivorBot > SurvivorDislodgeVictim::OnContact( SurvivorBot *me, CBaseEntity *other, CGameTrace *result )
{
	if ( other && other->GetTeamNumber() == TEAM_INFECTED )
	{
		me->GetBodyInterface()->AimHeadTowards( other, IBody::IMPORTANT, 1.0f, m_shove ? static_cast< INextBotReply * >( &s_shoveWhenAimed ) : static_cast< INextBotReply * >( &s_fireWhenAimed ),
			m_shove ? "Shoving enemy out of my way" : "Trying to kill friend's attacker", true, 0.25f );
	}
	return TryContinue();
}

EventDesiredResult< SurvivorBot > SurvivorDislodgeVictim::OnInjured( SurvivorBot *me, const CTakeDamageInfo &info )
{
	return TryToSustain( RESULT_TRY );
}
