//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/bots/survivor_attack.h"
#include "l4d2/player/bots/survivor_bot.h"
#include "l4d2/player/bots/survivor_body.h"
#include "l4d2/weapons/terror_weapon.h"
#include "tier0/memdbgon.h"

SurvivorDispatchEnemy::SurvivorDispatchEnemy( CBaseCombatCharacter *enemy ) : m_enemy( enemy )
{
	m_triggerDown = false;
	m_hasSelectedWeapon = false;
}

ActionResult< SurvivorBot > SurvivorDispatchEnemy::OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction )
{
	m_timeout.Start( 8.0f );
	m_triggerDown = false;
	m_hasSelectedWeapon = false;
	return Continue();
}

ActionResult< SurvivorBot > SurvivorDispatchEnemy::SelectDispatchWeapon( SurvivorBot *me )
{
	CBaseCombatWeapon *active = me->GetActiveWeapon();
	if ( !active )
		return Continue();
	CBaseCombatWeapon *primary = me->Weapon_GetSlot( 0 );
	CBaseCombatWeapon *secondary = me->Weapon_GetSlot( 1 );
	if ( primary == active )
	{
		if ( static_cast< CTerrorWeapon * >( active )->GetWeaponID() == TERROR_WEAPON_GRENADE_LAUNCHER )
		{
			if ( !secondary )
				return Done( "No suitable weapon" );
			me->Weapon_Switch( secondary, 0 );
		}
	}
	else if ( secondary != active )
	{
		if ( !primary || static_cast< CTerrorWeapon * >( primary )->GetWeaponID() == TERROR_WEAPON_GRENADE_LAUNCHER )
		{
			if ( !secondary )
				return Done( "We have no weapons with which to dispatch an enemy." );
			me->Weapon_Switch( secondary, 0 );
		}
		else
		{
			me->Weapon_Switch( primary, 0 );
		}
		return Continue();
	}
	m_hasSelectedWeapon = true;
	return Continue();
}

ActionResult< SurvivorBot > SurvivorDispatchEnemy::Dispatch( SurvivorBot *me )
{
	if ( m_timeout.GetRemainingTime() <= 0.0f )
		return Done( "Timeout" );
	CBaseCombatCharacter *enemy = m_enemy.Get();
	if ( !enemy || !enemy->IsAlive() )
		return Done( "Target is dispatched" );
	if ( !me->IsAlive() || me->IsIT() || me->IsDominated() )
		return Done( "I am dead, dying, or at the mercy of the infected" );
	if ( me->IsUseActionTarget() && !me->IsPerformingSelfUseAction() )
		return Done( "Someone is healing me or giving me ammo" );
	me->GetBodyInterface()->AimHeadTowards( enemy, IBody::CRITICAL, 1.5f, NULL, "Dispatching an enemy", true, 0.25f );
	// Unlike SurvivorAttack, this original action does not wait for aim,
	// visibility, a steady head, or a weapon-type-specific burst interval.
	if ( m_triggerDown )
		me->ReleaseFireButton();
	else
		me->PressFireButton();
	m_triggerDown = !m_triggerDown;
	return Continue();
}

ActionResult< SurvivorBot > SurvivorDispatchEnemy::Update( SurvivorBot *me, float interval )
{
	return m_hasSelectedWeapon ? Dispatch( me ) : SelectDispatchWeapon( me );
}
