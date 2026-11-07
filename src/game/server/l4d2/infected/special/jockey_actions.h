//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_JOCKEY_ACTIONS_H
#define L4D2_JOCKEY_ACTIONS_H
#ifdef _WIN32
#pragma once
#endif
#include "l4d2/infected/special/jockey.h"
#include "NextBot/NextBotBehavior.h"
#include "NextBot/Path/NextBotChasePath.h"
#include "NextBot/Path/NextBotPathFollow.h"

class JockeyRidePathCost : public IPathCost
{
public:
	JockeyRidePathCost( Jockey *actor, bool riding );
	virtual float operator()( CNavArea *area, CNavArea *from, const CNavLadder *ladder,
		const CFuncElevator *elevator, float length ) const OVERRIDE;
private:
	Jockey *m_actor;
	ILocomotion *m_locomotion;
	bool m_riding;
};
class JockeyBehavior : public Action< Jockey >
{
public:
	JockeyBehavior() : m_lastStuckTime( 0.0f ) {}
	virtual const char *GetName() const OVERRIDE { return "JockeyBehavior"; }
	virtual Action< Jockey > *InitialContainedAction( Jockey *me ) OVERRIDE;
	virtual EventDesiredResult< Jockey > OnContact( Jockey *me, CBaseEntity *other, CGameTrace *trace ) OVERRIDE;
	virtual EventDesiredResult< Jockey > OnStuck( Jockey *me ) OVERRIDE;
private:
	float m_lastStuckTime;
};
class JockeyAssault : public Action< Jockey >
{
public:
	JockeyAssault();
	virtual const char *GetName() const OVERRIDE { return "JockeyAssault"; }
	virtual ActionResult< Jockey > OnStart( Jockey *me, Action< Jockey > *priorAction ) OVERRIDE;
	virtual ActionResult< Jockey > Update( Jockey *me, float interval ) OVERRIDE;
	virtual EventDesiredResult< Jockey > OnContact( Jockey *me, CBaseEntity *other, CGameTrace *trace ) OVERRIDE;
	virtual EventDesiredResult< Jockey > OnInjured( Jockey *me, const CTakeDamageInfo &info ) OVERRIDE { return TryToSustain( RESULT_IMPORTANT ); }
	virtual EventDesiredResult< Jockey > OnShoved( Jockey *me, CBaseEntity *pusher ) OVERRIDE;
private:
	bool CanLeapToVictim( Jockey *me );
	void UpdateRide( Jockey *me );
	int m_leapTestTick;
	bool m_canLeap;
	CHandle< CTerrorPlayer > m_victim;
	ChasePath m_path;
	PathFollower m_ridePath;
	CNavArea *m_rideGoal;
	CountdownTimer m_rideScanTimer;
};
class JockeyAttack : public Action< Jockey >
{
public:
	JockeyAttack();
	virtual const char *GetName() const OVERRIDE { return "JockeyAttack"; }
	virtual ActionResult< Jockey > OnStart( Jockey *me, Action< Jockey > *priorAction ) OVERRIDE;
	virtual ActionResult< Jockey > Update( Jockey *me, float interval ) OVERRIDE;
	virtual EventDesiredResult< Jockey > OnContact( Jockey *me, CBaseEntity *other, CGameTrace *trace ) OVERRIDE;
	virtual EventDesiredResult< Jockey > OnInjured( Jockey *me, const CTakeDamageInfo &info ) OVERRIDE { return TryToSustain( RESULT_IMPORTANT ); }
	virtual EventDesiredResult< Jockey > OnShoved( Jockey *me, CBaseEntity *pusher ) OVERRIDE;
	virtual EventDesiredResult< Jockey > OnCommandAttack( Jockey *me, CBaseEntity *victim ) OVERRIDE;
	virtual EventDesiredResult< Jockey > OnCommandAssault( Jockey *me ) OVERRIDE;
	virtual EventDesiredResult< Jockey > OnCommandApproach( Jockey *me, const Vector &goal, float range ) OVERRIDE;
private:
	bool CanLeapToVictim( Jockey *me );
	void UpdateRide( Jockey *me );
	int m_leapTestTick;
	bool m_canLeap;
	CHandle< CTerrorPlayer > m_victim;
	ChasePath m_path;
	PathFollower m_ridePath;
	CNavArea *m_rideGoal;
	CountdownTimer m_rideScanTimer;
};
class JockeyLeap : public Action< Jockey >
{
public:
	explicit JockeyLeap( CTerrorPlayer *victim );
	virtual const char *GetName() const OVERRIDE { return "JockeyLeap"; }
	virtual ActionResult< Jockey > OnStart( Jockey *me, Action< Jockey > *priorAction ) OVERRIDE;
	virtual ActionResult< Jockey > Update( Jockey *me, float interval ) OVERRIDE;
	virtual EventDesiredResult< Jockey > OnContact( Jockey *me, CBaseEntity *other, CGameTrace *trace ) OVERRIDE { return TryToSustain( RESULT_CRITICAL ); }
	virtual EventDesiredResult< Jockey > OnInjured( Jockey *me, const CTakeDamageInfo &info ) OVERRIDE;
	virtual EventDesiredResult< Jockey > OnShoved( Jockey *me, CBaseEntity *pusher ) OVERRIDE;
private:
	CHandle< CTerrorPlayer > m_victim;
	Vector m_goal;
	Vector m_unused;
	bool m_hasFired;
	CountdownTimer m_reactionTimer;
	CountdownTimer m_landingTimer;
};
#endif // L4D2_JOCKEY_ACTIONS_H
