//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_SPITTER_ACTIONS_H
#define L4D2_SPITTER_ACTIONS_H
#ifdef _WIN32
#pragma once
#endif
#include "l4d2/infected/special/spitter.h"
#include "NextBot/NextBotBehavior.h"
#include "l4d2/nav/terror_direct_chase_path.h"
#include "NextBot/Path/NextBotPathFollow.h"
class SpitterBehavior : public Action< Spitter >
{
public:
	SpitterBehavior() : m_lastStuckTime( 0.0f ) {}
	virtual const char *GetName() const OVERRIDE { return "SpitterBehavior"; }
	virtual Action< Spitter > *InitialContainedAction( Spitter *me ) OVERRIDE;
	virtual EventDesiredResult< Spitter > OnContact( Spitter *me, CBaseEntity *other, CGameTrace *trace ) OVERRIDE;
	virtual EventDesiredResult< Spitter > OnStuck( Spitter *me ) OVERRIDE;
private:
	float m_lastStuckTime;
};
class SpitterAssault : public Action< Spitter >
{
public:
	SpitterAssault();
	virtual const char *GetName() const OVERRIDE { return "SpitterAssault"; }
	virtual ActionResult< Spitter > OnStart( Spitter *me, Action< Spitter > *prior ) OVERRIDE;
	virtual ActionResult< Spitter > Update( Spitter *me, float interval ) OVERRIDE;
private:
	CHandle< CTerrorPlayer > m_victim;
	TerrorDirectChasePath m_path;
	bool m_isSpitting; // Native constructors omit this; OnStart owns initialization.
	CountdownTimer m_spitTimer;
};
class SpitterPrepareAmbush : public Action< Spitter >
{
public:
	SpitterPrepareAmbush() : m_ambushArea( NULL ) {} // failure count initialized by OnStart only
	virtual const char *GetName() const OVERRIDE { return "SpitterPrepareAmbush"; }
	virtual ActionResult< Spitter > OnStart( Spitter *me, Action< Spitter > *prior ) OVERRIDE;
	virtual ActionResult< Spitter > Update( Spitter *me, float interval ) OVERRIDE;
	virtual EventDesiredResult< Spitter > OnInjured( Spitter *me, const CTakeDamageInfo &info ) OVERRIDE;
	virtual EventDesiredResult< Spitter > OnCommandAssault( Spitter *me ) OVERRIDE;
private:
	CNavArea *m_ambushArea;
	PathFollower m_path;
	int m_failureCount;
};
class SpitterWaitInAmbush : public Action< Spitter >
{
public:
	SpitterWaitInAmbush() {} // Native constructor does not initialize area; OnStart does.
	virtual const char *GetName() const OVERRIDE { return "SpitterWaitInAmbush"; }
	virtual ActionResult< Spitter > OnStart( Spitter *me, Action< Spitter > *prior ) OVERRIDE;
	virtual ActionResult< Spitter > Update( Spitter *me, float interval ) OVERRIDE;
	virtual EventDesiredResult< Spitter > OnInjured( Spitter *me, const CTakeDamageInfo &info ) OVERRIDE;
	virtual EventDesiredResult< Spitter > OnCommandAssault( Spitter *me ) OVERRIDE;
private:
	CNavArea *m_ambushArea;
	CountdownTimer m_waitTimer;
};
class SpitterAttack : public Action< Spitter >
{
public:
	SpitterAttack();
	virtual const char *GetName() const OVERRIDE { return "SpitterAttack"; }
	virtual ActionResult< Spitter > OnStart( Spitter *me, Action< Spitter > *prior ) OVERRIDE;
	virtual ActionResult< Spitter > Update( Spitter *me, float interval ) OVERRIDE;
	virtual EventDesiredResult< Spitter > OnCommandAssault( Spitter *me ) OVERRIDE;
private:
	CHandle< CTerrorPlayer > m_victim;
	TerrorDirectChasePath m_path;
	bool m_isSpitting;
	CountdownTimer m_spitTimer;
};
#endif
