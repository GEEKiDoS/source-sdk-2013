//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_TANK_ACTIONS_H
#define L4D2_TANK_ACTIONS_H
#ifdef _WIN32
#pragma once
#endif
#include "NextBot/NextBotBehavior.h"
#include "l4d2/nav/terror_direct_chase_path.h"
class Tank;
class CTerrorPlayer;

class TankBehavior : public Action< Tank >
{
public:
	virtual const char *GetName() const OVERRIDE { return "TankBehavior"; } // S:471E60
	virtual Action< Tank > *InitialContainedAction( Tank *me ) OVERRIDE;
	virtual ActionResult< Tank > Update( Tank *me, float interval ) OVERRIDE;
	virtual EventDesiredResult< Tank > OnContact( Tank *me, CBaseEntity *other, CGameTrace *trace ) OVERRIDE;
};
class TankIdle : public Action< Tank >
{
public:
	virtual const char *GetName() const OVERRIDE { return "TankIdle"; } // S:474C50
	virtual ActionResult< Tank > OnStart( Tank *me, Action< Tank > *prior ) OVERRIDE;
	virtual ActionResult< Tank > Update( Tank *me, float interval ) OVERRIDE;
	virtual EventDesiredResult< Tank > OnContact( Tank *me, CBaseEntity *other, CGameTrace *trace ) OVERRIDE;
	virtual EventDesiredResult< Tank > OnInjured( Tank *me, const CTakeDamageInfo &info ) OVERRIDE;
	virtual EventDesiredResult< Tank > OnShoved( Tank *me, CBaseEntity *pusher ) OVERRIDE;
	virtual EventDesiredResult< Tank > OnCommandApproach( Tank *me, const Vector &position, float range ) OVERRIDE;
};
class TankAttack : public Action< Tank >
{
public:
	TankAttack();
	virtual const char *GetName() const OVERRIDE { return "TankAttack"; } // S:474110
	virtual ActionResult< Tank > OnStart( Tank *me, Action< Tank > *prior ) OVERRIDE;
	virtual ActionResult< Tank > Update( Tank *me, float interval ) OVERRIDE;
	virtual EventDesiredResult< Tank > OnContact( Tank *me, CBaseEntity *other, CGameTrace *trace ) OVERRIDE;
	virtual EventDesiredResult< Tank > OnMoveToSuccess( Tank *me, const Path *path ) OVERRIDE;
	virtual EventDesiredResult< Tank > OnMoveToFailure( Tank *me, const Path *path, MoveToFailureType reason ) OVERRIDE;
	virtual EventDesiredResult< Tank > OnStuck( Tank *me ) OVERRIDE;
	virtual EventDesiredResult< Tank > OnUnStuck( Tank *me ) OVERRIDE;
	virtual EventDesiredResult< Tank > OnInjured( Tank *me, const CTakeDamageInfo &info ) OVERRIDE;
	virtual EventDesiredResult< Tank > OnShoved( Tank *me, CBaseEntity *pusher ) OVERRIDE;
	virtual EventDesiredResult< Tank > OnCommandAttack( Tank *me, CBaseEntity *victim ) OVERRIDE;
	virtual EventDesiredResult< Tank > OnCommandApproach( Tank *me, const Vector &position, float range ) OVERRIDE;
private:
	void ChooseThrowError(); // S:472A50
	void UpdateThrow( Tank *me ); // S:472D00
	CHandle< CTerrorPlayer > m_victim; // native52
	TerrorDirectChasePath m_path; //56
	bool m_attackFlag; //18500, constructorfalse (native retains this member).
	IntervalTimer m_attackStartTimer; //18504
	CountdownTimer m_attackTimer; //18512, ctor-invalid, no additional writes.
	CountdownTimer m_throwTimer; //18524
	IntervalTimer m_stuckTimer; //18536
	IntervalTimer m_stasisTimer; //18544
	float m_throwError; //18552; ctor does not initialize, OnStart does.
};

// S:473E40/473EE0/473F30. The target's generic BehaviorMoveTo checks path
// validity BEFORE Update and has immediate completion event overrides; retail
// Tank's instantiation checks AFTER Update and inherits the generic events.
class TankMoveTo : public Action< Tank >
{
public:
	explicit TankMoveTo( const Vector &position );
	virtual const char *GetName() const OVERRIDE { return "BehaviorMoveTo"; }
	virtual ActionResult< Tank > OnStart( Tank *me, Action< Tank > *prior ) OVERRIDE;
	virtual ActionResult< Tank > Update( Tank *me, float interval ) OVERRIDE;
private:
	Vector m_goal;
	PathFollower m_path;
};
#endif
