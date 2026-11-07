//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_SURVIVOR_LEGS_H
#define L4D2_SURVIVOR_LEGS_H
#ifdef _WIN32
#pragma once
#endif

#include "NextBot/NextBotBehavior.h"
#include "NextBot/Path/NextBotPathFollow.h"
#include "l4d2/nav/terror_direct_chase_path.h"
#include "l4d2/player/bots/survivor_intention.h"

class SurvivorBot;

float GetSurvivorRushingRange();
int CountHumansNearRescueVehicle();

class SurvivorLegsApproach : public Action< SurvivorBot >
{
public:
	explicit SurvivorLegsApproach( CBaseEntity *goal );
	explicit SurvivorLegsApproach( const Vector &goal );
	virtual const char *GetName() const OVERRIDE { return "SurvivorLegsApproach"; }
	virtual ActionResult< SurvivorBot > OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction ) OVERRIDE;
	virtual ActionResult< SurvivorBot > Update( SurvivorBot *me, float interval ) OVERRIDE;
	virtual ActionResult< SurvivorBot > OnResume( SurvivorBot *me, Action< SurvivorBot > *interruptingAction ) OVERRIDE;
	virtual EventDesiredResult< SurvivorBot > OnStuck( SurvivorBot *me ) OVERRIDE;
	virtual const Path *GetCurrentPath( const INextBot *me ) const OVERRIDE { return &m_path; }
private:
	void ComputePathToPosition( SurvivorBot *me );
	EHANDLE m_goalEntity;
	Vector m_goalPosition;
	bool m_isEntityGoal;
	TerrorDirectChasePath m_chasePath;
	PathFollower m_path;
	CountdownTimer m_computeTimer;
	CountdownTimer m_invalidateTimer;
};

class SurvivorLegsWait : public Action< SurvivorBot >
{
public:
	explicit SurvivorLegsWait( float duration = 0.0f );
	virtual const char *GetName() const OVERRIDE { return "SurvivorLegsWait"; }
	virtual ActionResult< SurvivorBot > OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction ) OVERRIDE;
	virtual ActionResult< SurvivorBot > Update( SurvivorBot *me, float interval ) OVERRIDE;
	virtual ActionResult< SurvivorBot > OnSuspend( SurvivorBot *me, Action< SurvivorBot > *interruptingAction ) OVERRIDE;
	virtual EventDesiredResult< SurvivorBot > OnCommandPause( SurvivorBot *me, float duration ) OVERRIDE;
	virtual EventDesiredResult< SurvivorBot > OnCommandResume( SurvivorBot *me ) OVERRIDE;
private:
	float m_duration;
	CountdownTimer m_waitTimer;
};

class SurvivorLegsRegroup : public Action< SurvivorBot >
{
public:
	explicit SurvivorLegsRegroup( CTerrorPlayer *friendPlayer = NULL );
	virtual const char *GetName() const OVERRIDE { return "SurvivorLegsRegroup"; }
	virtual ActionResult< SurvivorBot > OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction ) OVERRIDE;
	virtual ActionResult< SurvivorBot > Update( SurvivorBot *me, float interval ) OVERRIDE;
	virtual EventDesiredResult< SurvivorBot > OnStuck( SurvivorBot *me ) OVERRIDE;
	virtual const Path *GetCurrentPath( const INextBot *me ) const OVERRIDE { return &m_path; }
private:
	CHandle< CTerrorPlayer > m_friend;
	ChasePath m_path;
	IntervalTimer m_reuniteTimer;
};

class SurvivorLegsCoverOrphan : public Action< SurvivorBot >
{
public:
	explicit SurvivorLegsCoverOrphan( CTerrorPlayer *friendPlayer );
	virtual const char *GetName() const OVERRIDE { return "SurvivorLegsCoverOrphan"; }
	virtual ActionResult< SurvivorBot > OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction ) OVERRIDE;
	virtual ActionResult< SurvivorBot > Update( SurvivorBot *me, float interval ) OVERRIDE;
	virtual EventDesiredResult< SurvivorBot > OnMoveToFailure( SurvivorBot *me, const Path *path, MoveToFailureType reason ) OVERRIDE;
	virtual EventDesiredResult< SurvivorBot > OnStuck( SurvivorBot *me ) OVERRIDE;
	virtual const Path *GetCurrentPath( const INextBot *me ) const OVERRIDE { return &m_path; }
private:
	CHandle< CTerrorPlayer > m_friend;
	ChasePath m_path;
	IntervalTimer m_reuniteTimer;
};

class SurvivorLegsCoverFriendInCombat : public Action< SurvivorBot >
{
public:
	explicit SurvivorLegsCoverFriendInCombat( CTerrorPlayer *friendPlayer );
	virtual const char *GetName() const OVERRIDE { return "SurvivorLegsCoverFriendInCombat"; }
	virtual ActionResult< SurvivorBot > OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction ) OVERRIDE;
	virtual ActionResult< SurvivorBot > Update( SurvivorBot *me, float interval ) OVERRIDE;
	virtual EventDesiredResult< SurvivorBot > OnStuck( SurvivorBot *me ) OVERRIDE;
	virtual const Path *GetCurrentPath( const INextBot *me ) const OVERRIDE { return &m_path; }
private:
	CHandle< CTerrorPlayer > m_friend;
	ChasePath m_path;
	IntervalTimer m_reuniteTimer;
};

class SurvivorLegsApproachElevator : public Action< SurvivorBot >
{
public:
	virtual const char *GetName() const OVERRIDE { return "SurvivorLegsApproachElevator"; }
	virtual ActionResult< SurvivorBot > Update( SurvivorBot *me, float interval ) OVERRIDE;
	virtual EventDesiredResult< SurvivorBot > OnStuck( SurvivorBot *me ) OVERRIDE;
	virtual const Path *GetCurrentPath( const INextBot *me ) const OVERRIDE { return &m_path; }
private:
	ChasePath m_path;
};

class SurvivorLegsRetreat : public Action< SurvivorBot >
{
public:
	explicit SurvivorLegsRetreat( CBaseEntity *threat );
	virtual const char *GetName() const OVERRIDE { return "SurvivorLegsRetreat"; }
	virtual ActionResult< SurvivorBot > OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction ) OVERRIDE;
	virtual ActionResult< SurvivorBot > Update( SurvivorBot *me, float interval ) OVERRIDE;
	virtual EventDesiredResult< SurvivorBot > OnCommandRetreat( SurvivorBot *me, CBaseEntity *threat, float range ) OVERRIDE;
	virtual QueryResultType ShouldHurry( const INextBot *me ) const OVERRIDE { return ANSWER_YES; }
	virtual QueryResultType ShouldRetreat( const INextBot *me ) const OVERRIDE { return ANSWER_NO; }
	virtual const Path *GetCurrentPath( const INextBot *me ) const OVERRIDE { return &m_path; }
private:
	void ComputeRetreatPath( SurvivorBot *me );
	PathFollower m_path;
	Vector m_lastThreatPosition;
	EHANDLE m_threat;
	IntervalTimer m_lastThreatVisibleTimer;
};

#endif
