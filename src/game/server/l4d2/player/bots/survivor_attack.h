//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef SURVIVOR_ATTACK_H
#define SURVIVOR_ATTACK_H
#ifdef _WIN32
#pragma once
#endif

#include "NextBot/NextBotBehavior.h"
#include "l4d2/player/bots/survivor_interaction.h"
#include "l4d2/player/bots/survivor_intention.h"
#include "util_shared.h"

class SurvivorBot;
class CTerrorPlayer;

class SurvivorAttack : public Action< SurvivorBot >
{
public:
	SurvivorAttack();
	virtual const char *GetName() const OVERRIDE { return "SurvivorAttack"; }
	virtual ActionResult< SurvivorBot > OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction ) OVERRIDE;
	virtual ActionResult< SurvivorBot > Update( SurvivorBot *me, float interval ) OVERRIDE;
	virtual void OnEnd( SurvivorBot *me, Action< SurvivorBot > *nextAction ) OVERRIDE;
	virtual ActionResult< SurvivorBot > OnSuspend( SurvivorBot *me, Action< SurvivorBot > *interruptingAction ) OVERRIDE;
	virtual EventDesiredResult< SurvivorBot > OnInjured( SurvivorBot *me, const CTakeDamageInfo &info ) OVERRIDE;
	virtual EventDesiredResult< SurvivorBot > OnThreatChanged( SurvivorBot *me, CBaseCombatCharacter *threat ) OVERRIDE;
	virtual QueryResultType ShouldHurry( const INextBot *me ) const OVERRIDE;
	virtual QueryResultType ShouldAttack( const INextBot *me, CBaseCombatCharacter *threat ) const OVERRIDE;

private:
	CBaseCombatCharacter *SelectTarget( SurvivorBot *me );
	void AimAtTarget( SurvivorBot *me );
	void SelectWeapon( SurvivorBot *me );
	bool FireWeapon( SurvivorBot *me );
	void Sidestep( SurvivorBot *me );
	void StopApproaching( SurvivorBot *me );

	bool m_triggerDown;
	CHandle< CBaseCombatCharacter > m_target;
	int m_targetFrame;
	CountdownTimer m_sidestepTimer;
	bool m_sidestepLeft;
	int m_enemyInjuryCount;
	CountdownTimer m_enemyInjuryDecayTimer;
	CountdownTimer m_shoveTimer;
	bool m_isApproaching;
};

class SurvivorAmbushBoomer : public SurvivorUseObject
{
public:
	explicit SurvivorAmbushBoomer( CTerrorPlayer *boomer );
	virtual const char *GetName() const OVERRIDE { return "SurvivorAmbushBoomer"; }
	virtual bool ShouldGiveUp( SurvivorBot *me ) OVERRIDE;
	virtual void OnObjectUsed( SurvivorBot *me ) OVERRIDE;
	virtual QueryResultType ShouldHurry( const INextBot *me ) const OVERRIDE { return ANSWER_YES; }
	virtual QueryResultType ShouldAttack( const INextBot *me, CBaseCombatCharacter *threat ) const OVERRIDE { return ANSWER_NO; }

private:
	bool m_hasShoved;
};

class SurvivorDislodgeVictim : public SurvivorUseObject
{
public:
	SurvivorDislodgeVictim( CTerrorPlayer *victim, bool shove );
	virtual const char *GetName() const OVERRIDE { return "SurvivorDislodgeVictim"; }
	virtual ActionResult< SurvivorBot > OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction ) OVERRIDE;
	virtual Action< SurvivorBot > *InitialContainedAction( SurvivorBot *me ) OVERRIDE;
	virtual const Vector &GetUsePosition( SurvivorBot *me, CBaseEntity *object ) OVERRIDE;
	virtual bool ShouldGiveUp( SurvivorBot *me ) OVERRIDE;
	virtual void OnObjectUsed( SurvivorBot *me ) OVERRIDE;
	virtual EventDesiredResult< SurvivorBot > OnContact( SurvivorBot *me, CBaseEntity *other, CGameTrace *result = NULL ) OVERRIDE;
	virtual EventDesiredResult< SurvivorBot > OnInjured( SurvivorBot *me, const CTakeDamageInfo &info ) OVERRIDE;
	virtual QueryResultType ShouldHurry( const INextBot *me ) const OVERRIDE { return ANSWER_YES; }
	virtual QueryResultType ShouldAttack( const INextBot *me, CBaseCombatCharacter *threat ) const OVERRIDE { return ANSWER_NO; }

private:
	bool m_shove;
};

class SurvivorDispatchEnemy : public Action< SurvivorBot >
{
public:
	explicit SurvivorDispatchEnemy( CBaseCombatCharacter *enemy );
	virtual const char *GetName() const OVERRIDE { return "SurvivorDispatchEnemy"; }
	virtual ActionResult< SurvivorBot > OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction ) OVERRIDE;
	virtual ActionResult< SurvivorBot > Update( SurvivorBot *me, float interval ) OVERRIDE;

private:
	ActionResult< SurvivorBot > SelectDispatchWeapon( SurvivorBot *me );
	ActionResult< SurvivorBot > Dispatch( SurvivorBot *me );
	CHandle< CBaseCombatCharacter > m_enemy;
	CountdownTimer m_timeout;
	bool m_triggerDown;
	bool m_hasSelectedWeapon;
};

class SurvivorEscapeLadderAmbush : public Action< SurvivorBot >
{
public:
	virtual const char *GetName() const OVERRIDE { return "SurvivorEscapeLadderAmbush"; }
	virtual ActionResult< SurvivorBot > OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction ) OVERRIDE;
	virtual ActionResult< SurvivorBot > Update( SurvivorBot *me, float interval ) OVERRIDE;
	virtual void OnEnd( SurvivorBot *me, Action< SurvivorBot > *nextAction ) OVERRIDE;
	virtual Action< SurvivorBot > *InitialContainedAction( SurvivorBot *me ) OVERRIDE;
};

class SurvivorLiberateBesiegedFriend : public Action< SurvivorBot >
{
public:
	SurvivorLiberateBesiegedFriend( CTerrorPlayer *friendPlayer, Action< SurvivorBot > *postLiberationAction );
	virtual const char *GetName() const OVERRIDE { return "SurvivorLiberateBesiegedFriend"; }
	virtual ActionResult< SurvivorBot > OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction ) OVERRIDE;
	virtual ActionResult< SurvivorBot > Update( SurvivorBot *me, float interval ) OVERRIDE;
	virtual void OnEnd( SurvivorBot *me, Action< SurvivorBot > *nextAction ) OVERRIDE;
	virtual Action< SurvivorBot > *InitialContainedAction( SurvivorBot *me ) OVERRIDE;
	virtual EventDesiredResult< SurvivorBot > OnInjured( SurvivorBot *me, const CTakeDamageInfo &info ) OVERRIDE;
	virtual EventDesiredResult< SurvivorBot > OnThreatChanged( SurvivorBot *me, CBaseCombatCharacter *threat ) OVERRIDE;
	virtual QueryResultType ShouldHurry( const INextBot *me ) const OVERRIDE { return ANSWER_YES; }
	virtual QueryResultType ShouldAttack( const INextBot *me, CBaseCombatCharacter *threat ) const OVERRIDE { return ANSWER_NO; }
	virtual CBaseEntity *SelectMoreDangerousThreat( const INextBot *me, const CBaseCombatCharacter *subject, CBaseEntity *first, CBaseEntity *second ) const OVERRIDE;

private:
	CHandle< CTerrorPlayer > m_friend;
	CountdownTimer m_shoveTimer;
	Action< SurvivorBot > *m_postLiberationAction;
	bool m_isApproaching;
};

#endif // SURVIVOR_ATTACK_H
