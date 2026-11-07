//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef SURVIVOR_BEHAVIOR_H
#define SURVIVOR_BEHAVIOR_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/player/bots/survivor_intention.h"
#include "l4d2/player/bots/survivor_legs_behavior.h"

class SurvivorBot;
class CTerrorCheckpoint;
class CBasePropDoor;

class SurvivorBehavior : public Action< SurvivorBot >
{
public:
	virtual const char *GetName() const OVERRIDE { return "SurvivorBehavior"; }
	virtual ActionResult< SurvivorBot > OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction ) OVERRIDE;
	virtual ActionResult< SurvivorBot > OnResume( SurvivorBot *me, Action< SurvivorBot > *interruptingAction ) OVERRIDE;
	virtual ActionResult< SurvivorBot > Update( SurvivorBot *me, float interval ) OVERRIDE;
	virtual Action< SurvivorBot > *InitialContainedAction( SurvivorBot *me ) OVERRIDE;
	virtual EventDesiredResult< SurvivorBot > OnContact( SurvivorBot *me, CBaseEntity *other, CGameTrace *result = NULL ) OVERRIDE;
	virtual EventDesiredResult< SurvivorBot > OnIgnite( SurvivorBot *me ) OVERRIDE;
	virtual EventDesiredResult< SurvivorBot > OnEnteredSpit( SurvivorBot *me ) OVERRIDE;
	virtual EventDesiredResult< SurvivorBot > OnInjured( SurvivorBot *me, const CTakeDamageInfo &info ) OVERRIDE;
	virtual EventDesiredResult< SurvivorBot > OnSight( SurvivorBot *me, CBaseEntity *subject ) OVERRIDE;
	virtual EventDesiredResult< SurvivorBot > OnSpokeConcept( SurvivorBot *me, CBaseCombatCharacter *who, AIConcept_t concept, AI_Response *response ) OVERRIDE;
	virtual EventDesiredResult< SurvivorBot > OnNavAreaChanged( SurvivorBot *me, CNavArea *enteredArea, CNavArea *leftArea ) OVERRIDE;
	virtual EventDesiredResult< SurvivorBot > OnPickUp( SurvivorBot *me, CBaseEntity *item, CBaseCombatCharacter *giver ) OVERRIDE;
	virtual EventDesiredResult< SurvivorBot > OnCommandApproach( SurvivorBot *me, const Vector &position, float range ) OVERRIDE;
	virtual EventDesiredResult< SurvivorBot > OnShoved( SurvivorBot *me, CBaseEntity *pusher ) OVERRIDE;
	virtual QueryResultType ShouldHurry( const INextBot *me ) const OVERRIDE;
	virtual QueryResultType IsHindrance( const INextBot *me, CBaseEntity *blocker ) const OVERRIDE;
	virtual Vector SelectTargetPoint( const INextBot *me, const CBaseCombatCharacter *subject ) const OVERRIDE;
	virtual CBaseEntity *SelectMoreDangerousThreat( const INextBot *me, const CBaseCombatCharacter *subject, CBaseEntity *first, CBaseEntity *second ) const OVERRIDE;

private:
	CountdownTimer m_selectionTimer;
	CTerrorCheckpoint *m_exitCheckpoint;
	CountdownTimer m_comeHereTimer;
	bool m_startedLeavingCheckpoint;
	CHandle< CBasePropDoor > m_door;
	CountdownTimer m_closeDoorTimer;
};

class L4D1SurvivorBehavior : public Action< SurvivorBot >
{
public:
	virtual const char *GetName() const OVERRIDE { return "L4D1SurvivorBehavior"; }
	virtual ActionResult< SurvivorBot > OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction ) OVERRIDE;
	virtual ActionResult< SurvivorBot > OnResume( SurvivorBot *me, Action< SurvivorBot > *interruptingAction ) OVERRIDE;
	virtual ActionResult< SurvivorBot > Update( SurvivorBot *me, float interval ) OVERRIDE;
	virtual Action< SurvivorBot > *InitialContainedAction( SurvivorBot *me ) OVERRIDE;
	virtual EventDesiredResult< SurvivorBot > OnSight( SurvivorBot *me, CBaseEntity *subject ) OVERRIDE;
	virtual QueryResultType IsHindrance( const INextBot *me, CBaseEntity *blocker ) const OVERRIDE;
	virtual Vector SelectTargetPoint( const INextBot *me, const CBaseCombatCharacter *subject ) const OVERRIDE;
	virtual CBaseEntity *SelectMoreDangerousThreat( const INextBot *me, const CBaseCombatCharacter *subject, CBaseEntity *first, CBaseEntity *second ) const OVERRIDE;
private:
	CountdownTimer m_selectionTimer;
};

#endif // SURVIVOR_BEHAVIOR_H
