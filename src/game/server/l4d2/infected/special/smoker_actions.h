//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_SMOKER_ACTIONS_H
#define L4D2_SMOKER_ACTIONS_H
#ifdef _WIN32
#pragma once
#endif
#include "l4d2/infected/special/smoker.h"
#include "NextBot/NextBotBehavior.h"
#include "NextBot/Path/NextBotPathFollow.h"
#include "l4d2/nav/terror_direct_chase_path.h"

class SmokerPathCost : public IPathCost
{
public:
	SmokerPathCost( Smoker *actor, CTerrorPlayer *victim );
	virtual float operator()( CNavArea *area, CNavArea *from, const CNavLadder *ladder,
		const CFuncElevator *elevator, float length ) const OVERRIDE;
private:
	Smoker *m_actor;
	ILocomotion *m_locomotion;
	CTerrorPlayer *m_victim;
};
class SmokerBehavior : public Action< Smoker >
{
public:
	SmokerBehavior() : m_lastStuckTime( 0.0f ) {}
	virtual const char *GetName() const OVERRIDE { return "SmokerBehavior"; }
	virtual Action< Smoker > *InitialContainedAction( Smoker *me ) OVERRIDE;
	virtual EventDesiredResult< Smoker > OnContact( Smoker *me, CBaseEntity *other, CGameTrace *trace ) OVERRIDE;
	virtual EventDesiredResult< Smoker > OnStuck( Smoker *me ) OVERRIDE;
private:
	float m_lastStuckTime;
};
class SmokerAssault : public Action< Smoker >
{
public:
	SmokerAssault();
	virtual const char *GetName() const OVERRIDE { return "SmokerAssault"; }
	virtual ActionResult< Smoker > OnStart( Smoker *me, Action< Smoker > *prior ) OVERRIDE;
	virtual ActionResult< Smoker > Update( Smoker *me, float interval ) OVERRIDE;
	virtual EventDesiredResult< Smoker > OnContact( Smoker *me, CBaseEntity *other, CGameTrace *trace ) OVERRIDE;
	virtual EventDesiredResult< Smoker > OnInjured( Smoker *me, const CTakeDamageInfo &info ) OVERRIDE { return TryToSustain(); }
	virtual EventDesiredResult< Smoker > OnShoved( Smoker *me, CBaseEntity *pusher ) OVERRIDE { return TryToSustain(); }
private:
	CHandle< CTerrorPlayer > m_victim;
	TerrorDirectChasePath m_path;
};
class SmokerAttack : public Action< Smoker >
{
public:
	SmokerAttack();
	virtual const char *GetName() const OVERRIDE { return "SmokerAttack"; }
	virtual ActionResult< Smoker > OnStart( Smoker *me, Action< Smoker > *prior ) OVERRIDE;
	virtual ActionResult< Smoker > Update( Smoker *me, float interval ) OVERRIDE;
	virtual EventDesiredResult< Smoker > OnContact( Smoker *me, CBaseEntity *other, CGameTrace *trace ) OVERRIDE;
	virtual EventDesiredResult< Smoker > OnInjured( Smoker *me, const CTakeDamageInfo &info ) OVERRIDE { return TryToSustain(); }
	virtual EventDesiredResult< Smoker > OnShoved( Smoker *me, CBaseEntity *pusher ) OVERRIDE { return TryToSustain(); }
	virtual EventDesiredResult< Smoker > OnCommandAttack( Smoker *me, CBaseEntity *victim ) OVERRIDE;
	virtual EventDesiredResult< Smoker > OnCommandAssault( Smoker *me ) OVERRIDE;
	virtual EventDesiredResult< Smoker > OnCommandApproach( Smoker *me, const Vector &goal, float range ) OVERRIDE;
private:
	CHandle< CTerrorPlayer > m_victim;
	TerrorDirectChasePath m_path;
};
class SmokerRetreatToCover : public Action< Smoker >
{
public:
	virtual const char *GetName() const OVERRIDE { return "SmokerRetreatToCover"; }
	virtual ActionResult< Smoker > OnStart( Smoker *me, Action< Smoker > *prior ) OVERRIDE;
	virtual ActionResult< Smoker > Update( Smoker *me, float interval ) OVERRIDE;
	virtual EventDesiredResult< Smoker > OnMoveToSuccess( Smoker *me, const Path *path ) OVERRIDE { return TryDone( RESULT_CRITICAL, "Reached end of path" ); }
	virtual EventDesiredResult< Smoker > OnMoveToFailure( Smoker *me, const Path *path, MoveToFailureType reason ) OVERRIDE;
	virtual EventDesiredResult< Smoker > OnInjured( Smoker *me, const CTakeDamageInfo &info ) OVERRIDE { return TryToSustain( RESULT_CRITICAL ); }
	virtual EventDesiredResult< Smoker > OnShoved( Smoker *me, CBaseEntity *pusher ) OVERRIDE { return TryDone( RESULT_CRITICAL ); }
private:
	PathFollower m_path;
};
class SmokerMoveToAttackPosition : public Action< Smoker >
{
public:
	explicit SmokerMoveToAttackPosition( CTerrorPlayer *victim );
	virtual const char *GetName() const OVERRIDE { return "SmokerMoveToAttackPosition"; }
	virtual ActionResult< Smoker > OnStart( Smoker *me, Action< Smoker > *prior ) OVERRIDE;
	virtual ActionResult< Smoker > Update( Smoker *me, float interval ) OVERRIDE;
	virtual EventDesiredResult< Smoker > OnStuck( Smoker *me ) OVERRIDE;
private:
	void ComputePath( Smoker *me );
	CHandle< CTerrorPlayer > m_victim;
	PathFollower m_path;
	CountdownTimer m_repathTimer;
	CountdownTimer m_stuckTimer;
};
class SmokerTongueVictim : public Action< Smoker >
{
public:
	explicit SmokerTongueVictim( CTerrorPlayer *victim ) : m_victim( victim ) {}
	virtual const char *GetName() const OVERRIDE { return "SmokerTongueVictim"; }
	virtual ActionResult< Smoker > OnStart( Smoker *me, Action< Smoker > *prior ) OVERRIDE;
	virtual ActionResult< Smoker > Update( Smoker *me, float interval ) OVERRIDE;
private:
	CHandle< CTerrorPlayer > m_victim;
	CountdownTimer m_reactionTimer;
	bool m_hasFired;
};
#endif // L4D2_SMOKER_ACTIONS_H
