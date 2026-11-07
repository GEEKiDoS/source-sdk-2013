//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_BOOMER_ACTIONS_H
#define L4D2_BOOMER_ACTIONS_H
#ifdef _WIN32
#pragma once
#endif
#include "l4d2/infected/special/boomer.h"
#include "NextBot/NextBotBehavior.h"
#include "l4d2/nav/terror_direct_chase_path.h"
#include "NextBot/Path/NextBotPathFollow.h"
class BoomerBehavior : public Action< Boomer >
{
public:
	BoomerBehavior() : m_lastStuckTime( 0.0f ) {}
	virtual const char *GetName() const OVERRIDE { return "BoomerBehavior"; }
	virtual Action< Boomer > *InitialContainedAction( Boomer *me ) OVERRIDE;
	virtual EventDesiredResult< Boomer > OnContact( Boomer *me, CBaseEntity *other, CGameTrace *trace ) OVERRIDE;
	virtual EventDesiredResult< Boomer > OnStuck( Boomer *me ) OVERRIDE;
private:
	float m_lastStuckTime;
};
class BoomerAssault : public Action< Boomer >
{
public:
	BoomerAssault();
	virtual const char *GetName() const OVERRIDE { return "BoomerAssault"; }
	virtual ActionResult< Boomer > OnStart( Boomer *me, Action< Boomer > *prior ) OVERRIDE;
	virtual ActionResult< Boomer > Update( Boomer *me, float interval ) OVERRIDE;
	virtual EventDesiredResult< Boomer > OnContact( Boomer *me, CBaseEntity *other, CGameTrace *trace ) OVERRIDE;
	virtual EventDesiredResult< Boomer > OnInjured( Boomer *me, const CTakeDamageInfo &info ) OVERRIDE { return TryToSustain(); }
	virtual EventDesiredResult< Boomer > OnShoved( Boomer *me, CBaseEntity *pusher ) OVERRIDE { return TryToSustain(); }
private:
	CHandle< CTerrorPlayer > m_victim;
	TerrorDirectChasePath m_path;
};
class BoomerAttack : public Action< Boomer >
{
public:
	BoomerAttack();
	virtual const char *GetName() const OVERRIDE { return "BoomerAttack"; }
	virtual ActionResult< Boomer > OnStart( Boomer *me, Action< Boomer > *prior ) OVERRIDE;
	virtual ActionResult< Boomer > Update( Boomer *me, float interval ) OVERRIDE;
	virtual EventDesiredResult< Boomer > OnContact( Boomer *me, CBaseEntity *other, CGameTrace *trace ) OVERRIDE;
	virtual EventDesiredResult< Boomer > OnInjured( Boomer *me, const CTakeDamageInfo &info ) OVERRIDE { return TryToSustain(); }
	virtual EventDesiredResult< Boomer > OnShoved( Boomer *me, CBaseEntity *pusher ) OVERRIDE { return TryToSustain(); }
	virtual EventDesiredResult< Boomer > OnCommandAttack( Boomer *me, CBaseEntity *victim ) OVERRIDE;
	virtual EventDesiredResult< Boomer > OnCommandAssault( Boomer *me ) OVERRIDE;
	virtual EventDesiredResult< Boomer > OnCommandApproach( Boomer *me, const Vector &goal, float range ) OVERRIDE;
private:
	CHandle< CTerrorPlayer > m_victim;
	TerrorDirectChasePath m_path;
	IntervalTimer m_exposedTimer;
	CountdownTimer m_coverSearchTimer;
};
class BoomerRetreatToCover : public Action< Boomer >
{
public:
	explicit BoomerRetreatToCover( CNavArea *cover );
	virtual const char *GetName() const OVERRIDE { return "BoomerRetreatToCover"; }
	virtual ActionResult< Boomer > OnStart( Boomer *me, Action< Boomer > *prior ) OVERRIDE;
	virtual ActionResult< Boomer > Update( Boomer *me, float interval ) OVERRIDE;
	virtual EventDesiredResult< Boomer > OnShoved( Boomer *me, CBaseEntity *pusher ) OVERRIDE { return TryDone( RESULT_CRITICAL ); }
	virtual EventDesiredResult< Boomer > OnInjured( Boomer *me, const CTakeDamageInfo &info ) OVERRIDE { return TryToSustain( RESULT_CRITICAL ); }
	virtual EventDesiredResult< Boomer > OnMoveToSuccess( Boomer *me, const Path *path ) OVERRIDE { return TryDone( RESULT_CRITICAL ); }
	virtual EventDesiredResult< Boomer > OnMoveToFailure( Boomer *me, const Path *path, MoveToFailureType reason ) OVERRIDE;
private:
	CNavArea *m_cover;
	PathFollower m_path;
};
class BoomerHide : public Action< Boomer >
{
public:
	virtual const char *GetName() const OVERRIDE { return "BoomerHide"; }
	virtual ActionResult< Boomer > OnStart( Boomer *me, Action< Boomer > *prior ) OVERRIDE;
	virtual ActionResult< Boomer > Update( Boomer *me, float interval ) OVERRIDE;
private:
	CountdownTimer m_hideTimer;
};
class BoomerSelfDestruct : public Action< Boomer >
{
public:
	virtual const char *GetName() const OVERRIDE { return "BoomerSelfDestruct"; }
	virtual ActionResult< Boomer > OnStart( Boomer *me, Action< Boomer > *prior ) OVERRIDE;
	// S4306C0 is genuinely Continue: the real CSelfDestruct ability owns
	// detonation. Native stores its difficulty timer but never reads it here.
	virtual ActionResult< Boomer > Update( Boomer *me, float interval ) OVERRIDE { return Continue(); }
private:
	CountdownTimer m_detonationTimer;
};
class BoomerVomitOnVictim : public Action< Boomer >
{
public:
	explicit BoomerVomitOnVictim( CTerrorPlayer *victim );
	virtual const char *GetName() const OVERRIDE { return "BoomerVomitOnVictim"; }
	virtual ActionResult< Boomer > OnStart( Boomer *me, Action< Boomer > *prior ) OVERRIDE;
	virtual ActionResult< Boomer > Update( Boomer *me, float interval ) OVERRIDE;
private:
	CHandle< CTerrorPlayer > m_victim;
	CountdownTimer m_reactionTimer;
	bool m_hasFired; // Native constructor deliberately leaves this to OnStart.
};
#endif
