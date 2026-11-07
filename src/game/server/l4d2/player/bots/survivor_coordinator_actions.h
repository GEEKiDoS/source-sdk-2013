//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef SURVIVOR_COORDINATOR_ACTIONS_H
#define SURVIVOR_COORDINATOR_ACTIONS_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/player/bots/survivor_intention.h"

class CTerrorCheckpoint;
class CBasePropDoor;
class CPropMountedGun;
class TerrorNavArea;

class SurvivorCheckpointLeaving : public Action< SurvivorBot >
{
public:
	virtual const char *GetName() const OVERRIDE { return "SurvivorCheckpointLeaving"; }
	virtual ActionResult< SurvivorBot > OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction ) OVERRIDE;
	virtual ActionResult< SurvivorBot > Update( SurvivorBot *me, float interval ) OVERRIDE;
	virtual void OnEnd( SurvivorBot *me, Action< SurvivorBot > *nextAction ) OVERRIDE;
	virtual Action< SurvivorBot > *InitialContainedAction( SurvivorBot *me ) OVERRIDE;
	virtual EventDesiredResult< SurvivorBot > OnContact( SurvivorBot *me, CBaseEntity *other, CGameTrace *trace ) OVERRIDE;
	virtual EventDesiredResult< SurvivorBot > OnPickUp( SurvivorBot *me, CBaseEntity *item, CBaseCombatCharacter *giver ) OVERRIDE;
private:
	bool m_hasPickedUpMedkit; // Original write-only pickup state.
	CountdownTimer m_wanderTimer;
	CountdownTimer m_initialWaitTimer;
	TerrorNavArea *m_wanderArea;
	Vector m_wanderGoal;
};

class SurvivorCheckpointReached : public Action< SurvivorBot >
{
public:
	virtual const char *GetName() const OVERRIDE { return "SurvivorCheckpointReached"; }
	virtual ActionResult< SurvivorBot > OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction ) OVERRIDE;
	virtual ActionResult< SurvivorBot > Update( SurvivorBot *me, float interval ) OVERRIDE;
	virtual void OnEnd( SurvivorBot *me, Action< SurvivorBot > *nextAction ) OVERRIDE;
	virtual Action< SurvivorBot > *InitialContainedAction( SurvivorBot *me ) OVERRIDE;
private:
	CTerrorCheckpoint *m_checkpoint;
	CountdownTimer m_selectionTimer;
	CountdownTimer m_closeDoorTimer;
};

class SurvivorCloseDoor : public Action< SurvivorBot >
{
public:
	explicit SurvivorCloseDoor( CBasePropDoor *door );
	virtual const char *GetName() const OVERRIDE { return "SurvivorCloseDoor"; }
	virtual ActionResult< SurvivorBot > OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction ) OVERRIDE;
	virtual ActionResult< SurvivorBot > Update( SurvivorBot *me, float interval ) OVERRIDE;
	virtual ActionResult< SurvivorBot > OnResume( SurvivorBot *me, Action< SurvivorBot > *interruptingAction ) OVERRIDE;
	virtual void OnEnd( SurvivorBot *me, Action< SurvivorBot > *nextAction ) OVERRIDE;
	virtual Action< SurvivorBot > *InitialContainedAction( SurvivorBot *me ) OVERRIDE;
private:
	CHandle< CBasePropDoor > m_door;
	CountdownTimer m_timeout;
};

class SurvivorDebugApproach : public Action< SurvivorBot >
{
public:
	explicit SurvivorDebugApproach( const Vector &position ) : m_position( position ) {}
	virtual const char *GetName() const OVERRIDE { return "SurvivorDebugApproach"; }
	virtual ActionResult< SurvivorBot > OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction ) OVERRIDE;
	virtual ActionResult< SurvivorBot > Update( SurvivorBot *me, float interval ) OVERRIDE;
	virtual void OnEnd( SurvivorBot *me, Action< SurvivorBot > *nextAction ) OVERRIDE;
private:
	Vector m_position;
	CountdownTimer m_waitTimer;
};

class SurvivorElevatorEnter : public Action< SurvivorBot >
{
public:
	virtual const char *GetName() const OVERRIDE { return "SurvivorElevatorEnter"; }
	virtual ActionResult< SurvivorBot > OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction ) OVERRIDE;
	virtual ActionResult< SurvivorBot > Update( SurvivorBot *me, float interval ) OVERRIDE;
	virtual Action< SurvivorBot > *InitialContainedAction( SurvivorBot *me ) OVERRIDE;
private:
	IntervalTimer m_buttonWaitTimer;
};

class SurvivorElevatorRide : public Action< SurvivorBot >
{
public:
	virtual const char *GetName() const OVERRIDE { return "SurvivorElevatorRide"; }
	virtual ActionResult< SurvivorBot > OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction ) OVERRIDE;
	virtual ActionResult< SurvivorBot > Update( SurvivorBot *me, float interval ) OVERRIDE;
	virtual void OnEnd( SurvivorBot *me, Action< SurvivorBot > *nextAction ) OVERRIDE;
	virtual Action< SurvivorBot > *InitialContainedAction( SurvivorBot *me ) OVERRIDE;
};

class SurvivorEscapeFlames : public Action< SurvivorBot >
{
public:
	virtual const char *GetName() const OVERRIDE { return "SurvivorEscapeFlames"; }
	virtual ActionResult< SurvivorBot > OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction ) OVERRIDE;
	virtual ActionResult< SurvivorBot > Update( SurvivorBot *me, float interval ) OVERRIDE;
	virtual void OnEnd( SurvivorBot *me, Action< SurvivorBot > *nextAction ) OVERRIDE;
	virtual Action< SurvivorBot > *InitialContainedAction( SurvivorBot *me ) OVERRIDE;
	virtual EventDesiredResult< SurvivorBot > OnIgnite( SurvivorBot *me ) OVERRIDE;
	virtual EventDesiredResult< SurvivorBot > OnInjured( SurvivorBot *me, const CTakeDamageInfo &info ) OVERRIDE;
private:
	IntervalTimer m_timeSinceFire;
	CountdownTimer m_repathTimer;
	TerrorNavArea *m_escapeArea;
};

class SurvivorEscapeSpit : public Action< SurvivorBot >
{
public:
	virtual const char *GetName() const OVERRIDE { return "SurvivorEscapeSpit"; }
	virtual ActionResult< SurvivorBot > OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction ) OVERRIDE;
	virtual ActionResult< SurvivorBot > Update( SurvivorBot *me, float interval ) OVERRIDE;
	virtual void OnEnd( SurvivorBot *me, Action< SurvivorBot > *nextAction ) OVERRIDE;
	virtual Action< SurvivorBot > *InitialContainedAction( SurvivorBot *me ) OVERRIDE;
	virtual EventDesiredResult< SurvivorBot > OnEnteredSpit( SurvivorBot *me ) OVERRIDE;
	virtual EventDesiredResult< SurvivorBot > OnInjured( SurvivorBot *me, const CTakeDamageInfo &info ) OVERRIDE;
private:
	IntervalTimer m_timeSinceSpit;
	CountdownTimer m_repathTimer;
	TerrorNavArea *m_escapeArea;
	bool m_nativeSpitFlag; // Native80 is cleared by start, spit and acid injury.
};

class SurvivorApproachMinigun : public Action< SurvivorBot >
{
public:
	explicit SurvivorApproachMinigun( CPropMountedGun *gun );
	virtual const char *GetName() const OVERRIDE { return "SurvivorApproachMinigun"; }
	virtual ActionResult< SurvivorBot > OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction ) OVERRIDE;
	virtual ActionResult< SurvivorBot > Update( SurvivorBot *me, float interval ) OVERRIDE;
	virtual ActionResult< SurvivorBot > OnSuspend( SurvivorBot *me, Action< SurvivorBot > *interruptingAction ) OVERRIDE;
	virtual void OnEnd( SurvivorBot *me, Action< SurvivorBot > *nextAction ) OVERRIDE;
	virtual EventDesiredResult< SurvivorBot > OnStuck( SurvivorBot *me ) OVERRIDE;
	virtual EventDesiredResult< SurvivorBot > OnInjured( SurvivorBot *me, const CTakeDamageInfo &info ) OVERRIDE;
private:
	bool ShouldGiveUp( SurvivorBot *me );
	const Vector &GetUsePosition();
	const Vector &GetAimPosition( SurvivorBot *me );
	CHandle< CPropMountedGun > m_gun;
	CountdownTimer m_usedTimer;
	CountdownTimer m_unusedTimer; // Native68 is constructor-only.
	CountdownTimer m_reachabilityTimer;
};

class L4D1SurvivorMinigunAttack : public Action< SurvivorBot >
{
public:
	virtual const char *GetName() const OVERRIDE { return "L4D1SurvivorMinigunAttack"; }
	virtual ActionResult< SurvivorBot > OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction ) OVERRIDE;
	virtual ActionResult< SurvivorBot > Update( SurvivorBot *me, float interval ) OVERRIDE;
	virtual void OnEnd( SurvivorBot *me, Action< SurvivorBot > *nextAction ) OVERRIDE;
	virtual EventDesiredResult< SurvivorBot > OnSound( SurvivorBot *me, CBaseEntity *source, const Vector &position, KeyValues *keys ) OVERRIDE;
private:
	CBaseCombatCharacter *SelectTarget( SurvivorBot *me );
	void AimAtTarget( SurvivorBot *me );
	bool FireWeapon( SurvivorBot *me );
	CountdownTimer m_unusedBurstTimer;
	CountdownTimer m_unusedShotTimer;
	CHandle< CBaseCombatCharacter > m_target;
	int m_targetFrame;
	CountdownTimer m_unusedFireTimer;
	bool m_unusedTrigger;
	CountdownTimer m_unusedCooldownTimer;
};

#endif // SURVIVOR_COORDINATOR_ACTIONS_H
