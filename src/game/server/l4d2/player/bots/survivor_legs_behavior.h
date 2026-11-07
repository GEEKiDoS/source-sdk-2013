//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef SURVIVOR_LEGS_BEHAVIOR_H
#define SURVIVOR_LEGS_BEHAVIOR_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/player/bots/survivor_legs.h"

class TerrorNavArea;

class SurvivorLegsStayClose : public Action< SurvivorBot >
{
public:
	virtual const char *GetName() const OVERRIDE { return "SurvivorLegsStayClose"; }
	virtual ActionResult< SurvivorBot > OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction ) OVERRIDE;
	virtual ActionResult< SurvivorBot > Update( SurvivorBot *me, float interval ) OVERRIDE;
	virtual Action< SurvivorBot > *InitialContainedAction( SurvivorBot *me ) OVERRIDE;
	virtual EventDesiredResult< SurvivorBot > OnNavAreaChanged( SurvivorBot *me, CNavArea *enteredArea, CNavArea *leftArea ) OVERRIDE;
	virtual EventDesiredResult< SurvivorBot > OnCommandRetreat( SurvivorBot *me, CBaseEntity *threat, float range ) OVERRIDE;
	virtual EventDesiredResult< SurvivorBot > OnCommandPause( SurvivorBot *me, float duration ) OVERRIDE;
private:
	IntervalTimer m_checkpointEnteredTimer;
	CountdownTimer m_unusedTimer;
};

class SurvivorLegsMoveOn : public Action< SurvivorBot >
{
public:
	virtual const char *GetName() const OVERRIDE { return "SurvivorLegsMoveOn"; }
	virtual ActionResult< SurvivorBot > OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction ) OVERRIDE;
	virtual ActionResult< SurvivorBot > Update( SurvivorBot *me, float interval ) OVERRIDE;
	virtual ActionResult< SurvivorBot > OnResume( SurvivorBot *me, Action< SurvivorBot > *interruptingAction ) OVERRIDE;
	virtual EventDesiredResult< SurvivorBot > OnStuck( SurvivorBot *me ) OVERRIDE;
	virtual const Path *GetCurrentPath( const INextBot *me ) const OVERRIDE;
private:
	void DelayForTeam();
	void MoveWithTeam( SurvivorBot *me );
	CountdownTimer m_teamPauseCheckTimer;
	CountdownTimer m_unusedTimer;
	CountdownTimer m_battlefieldCheckTimer;
	bool m_unusedFlag;
	CountdownTimer m_leaderPauseTimer;
};

class SurvivorLegsBattleStations : public Action< SurvivorBot >
{
public:
	virtual const char *GetName() const OVERRIDE { return "SurvivorLegsBattleStations"; }
	virtual ActionResult< SurvivorBot > OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction ) OVERRIDE;
	virtual ActionResult< SurvivorBot > Update( SurvivorBot *me, float interval ) OVERRIDE;
	virtual ActionResult< SurvivorBot > OnResume( SurvivorBot *me, Action< SurvivorBot > *interruptingAction ) OVERRIDE;
	virtual EventDesiredResult< SurvivorBot > OnStuck( SurvivorBot *me ) OVERRIDE;
	virtual const Path *GetCurrentPath( const INextBot *me ) const OVERRIDE { return &m_path; }
private:
	TerrorNavArea *SelectBattleStation( SurvivorBot *me, TerrorNavArea *current );
	PathFollower m_path;
	TerrorNavArea *m_battleStation;
	CountdownTimer m_reevaluateTimer;
	CountdownTimer m_repathTimer;
	CountdownTimer m_selectionTimer;
};

class SurvivorLegsMeetRescueVehicleArrival : public Action< SurvivorBot >
{
public:
	virtual const char *GetName() const OVERRIDE { return "SurvivorLegsMeetRescueVehicleArrival"; }
	virtual ActionResult< SurvivorBot > OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction ) OVERRIDE;
	virtual ActionResult< SurvivorBot > Update( SurvivorBot *me, float interval ) OVERRIDE;
	virtual ActionResult< SurvivorBot > OnResume( SurvivorBot *me, Action< SurvivorBot > *interruptingAction ) OVERRIDE;
	virtual EventDesiredResult< SurvivorBot > OnStuck( SurvivorBot *me ) OVERRIDE;
	virtual const Path *GetCurrentPath( const INextBot *me ) const OVERRIDE { return &m_path; }
private:
	PathFollower m_path;
	TerrorNavArea *m_loadingArea;
};

class SurvivorLegsEnterRescueVehicle : public Action< SurvivorBot >
{
public:
	virtual const char *GetName() const OVERRIDE { return "SurvivorLegsEnterRescueVehicle"; }
	virtual ActionResult< SurvivorBot > OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction ) OVERRIDE;
	virtual ActionResult< SurvivorBot > Update( SurvivorBot *me, float interval ) OVERRIDE;
	virtual ActionResult< SurvivorBot > OnResume( SurvivorBot *me, Action< SurvivorBot > *interruptingAction ) OVERRIDE;
	virtual EventDesiredResult< SurvivorBot > OnStuck( SurvivorBot *me ) OVERRIDE;
	virtual const Path *GetCurrentPath( const INextBot *me ) const OVERRIDE { return &m_path; }
private:
	PathFollower m_path;
};

class L4D1SurvivorLegsBattleStations : public Action< SurvivorBot >
{
public:
	virtual const char *GetName() const OVERRIDE { return "L4D1SurvivorLegsBattleStations"; }
	virtual ActionResult< SurvivorBot > OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction ) OVERRIDE;
	virtual ActionResult< SurvivorBot > Update( SurvivorBot *me, float interval ) OVERRIDE;
	virtual ActionResult< SurvivorBot > OnResume( SurvivorBot *me, Action< SurvivorBot > *interruptingAction ) OVERRIDE;
	virtual EventDesiredResult< SurvivorBot > OnStuck( SurvivorBot *me ) OVERRIDE;
	virtual const Path *GetCurrentPath( const INextBot *me ) const OVERRIDE { return &m_path; }
private:
	TerrorNavArea *SelectBattleStation( SurvivorBot *me, TerrorNavArea *current );
	PathFollower m_path;
	TerrorNavArea *m_battleStation;
	CountdownTimer m_reevaluateTimer;
	CountdownTimer m_repathTimer;
	CountdownTimer m_selectionTimer;
};

bool IsSurvivorRescueVehicleArriving();
void CollectSurvivorBattleStations( TerrorNavArea *start, const Vector &origin, CUtlVector< TerrorNavArea * > *areas, float range = -1.0f );

#endif
