//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/bots/survivor_bot.h"
#include "l4d2/weapons/terror_weapon.h"
#include "tier0/memdbgon.h"

// Native S38F820 starts this timer before attempting the inherited switch,
// including rejected switches and attempts to select the already active weapon.
bool SurvivorBot::Weapon_Switch( CBaseCombatWeapon *weapon, int viewModelIndex )
{
	m_weaponSwitchAttemptTimer.Start();
	return BaseClass::Weapon_Switch( weapon, viewModelIndex );
}

bool SurvivorBot::IsSafeForCombatAction() const
{
	if ( GetTimeSinceEnemyInjury() < 1.0f && !IsRespondingToSurvivorDistress() )
		return false;
	const SurvivorKnownEntity *known = GetIntentionInterface()->GetPrimaryKnownThreat();
	if ( known && known->GetEntity() && GetIntentionInterface()->IsImmediateThreat( const_cast< SurvivorBot * >( this ), known->GetEntity() ) )
		return false;
	return CanPerformBackpackAction();
}

void SurvivorBot::ReloadCombatWeapon()
{
	if ( IsUsingWeaponType( TERROR_WEAPONTYPE_SHOTGUN ) && GetTimeSinceWeaponFired() > 1.0f )
	{
		CBaseCombatWeapon *active = GetActiveWeapon();
		if ( !active || !active->m_bInReload )
			PressReloadButton();
	}
	if ( IsSafeForCombatAction() && GetTimeSinceWeaponFired() > 5.0f &&
		( !m_weaponSwitchAttemptTimer.HasStarted() || m_weaponSwitchAttemptTimer.GetElapsedTime() > 5.0f ) )
	{
		CBaseCombatWeapon *active = GetActiveWeapon();
		if ( !active || !active->m_bInReload )
			PressReloadButton();
	}
}

void SurvivorBot::LookAround()
{
	if ( m_lookAroundTimer.GetRemainingTime() > 0.0f )
		return;
	m_lookAroundTimer.Start( RandomFloat( 3.0f, 5.0f ) );
	SurvivorBody *body = GetBodyInterface();
	SurvivorVision *vision = GetVisionInterface();
	if ( RandomFloat( 0.0f, 100.0f ) < 33.0f )
	{
		CTerrorPlayer *friendPlayer = GetTeamSituation().GetRandomRecentlySeenFriend();
		if ( friendPlayer )
		{
			body->AimHeadTowards( friendPlayer, IBody::BORING, m_lookAroundTimer.GetRemainingTime(), NULL, "Looking around at friend", true, 0.25f );
			return;
		}
	}
	Vector forward = body->GetViewVector();
	forward.z = 0.0f;
	VectorNormalize( forward );
	Vector side( -forward.y, forward.x, 0.0f );
	VectorNormalize( side );
	float sign = RandomInt( 0, 100 ) >= 50 ? 1.0f : -1.0f;
	float vertical = RandomFloat( -0.5f, 0.5f );
	float lateral = RandomFloat( 0.5f, 2.0f ) * sign;
	Vector direction = side * lateral + forward;
	direction.z += vertical;
	VectorNormalize( direction );
	Vector position = EyePosition() + direction * 300.0f;
	if ( vision->IsLineOfSightClear( position ) )
		body->AimHeadTowards( position, IBody::BORING, m_lookAroundTimer.GetRemainingTime(), NULL, "Looking around at random spot", true, 0.25f );
}

void SurvivorBot::NoteFriendDislodgeAttempt( SurvivorCharacterType character, float duration )
{
	if ( static_cast< unsigned int >( character ) <= 7 )
		m_friendActionTimer[ static_cast< int >( character ) ].Start( duration );
}
