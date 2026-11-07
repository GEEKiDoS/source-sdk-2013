//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_HUNTER_ACTIONS_H
#define L4D2_HUNTER_ACTIONS_H
#ifdef _WIN32
#pragma once
#endif
#include "l4d2/infected/special/hunter.h"
#include "NextBot/NextBotBehavior.h"
#include "l4d2/nav/terror_direct_chase_path.h"
#include "NextBot/Path/NextBotPathFollow.h"

class HunterBehavior : public Action< Hunter >
{
public:
	HunterBehavior() : m_lastStuckTime( 0.0f ) {}
	virtual const char *GetName() const OVERRIDE { return "HunterBehavior"; }
	virtual Action< Hunter > *InitialContainedAction( Hunter *me ) OVERRIDE;
	virtual EventDesiredResult< Hunter > OnContact( Hunter *me, CBaseEntity *other, CGameTrace *trace ) OVERRIDE;
	virtual EventDesiredResult< Hunter > OnStuck( Hunter *me ) OVERRIDE;
private:
	float m_lastStuckTime;
};
class HunterAssault : public Action< Hunter >
{
public:
	HunterAssault();
	virtual const char *GetName() const OVERRIDE { return "HunterAssault"; }
	virtual ActionResult< Hunter > OnStart( Hunter *me, Action< Hunter > *priorAction ) OVERRIDE;
	virtual ActionResult< Hunter > Update( Hunter *me, float interval ) OVERRIDE;
	virtual EventDesiredResult< Hunter > OnContact( Hunter *me, CBaseEntity *other, CGameTrace *trace ) OVERRIDE;
	virtual EventDesiredResult< Hunter > OnInjured( Hunter *me, const CTakeDamageInfo &info ) OVERRIDE;
	virtual EventDesiredResult< Hunter > OnShoved( Hunter *me, CBaseEntity *pusher ) OVERRIDE;
private:
	CHandle< CTerrorPlayer > m_victim;
	TerrorDirectChasePath m_path;
};
class HunterAttack : public Action< Hunter >
{
public:
	HunterAttack();
	virtual const char *GetName() const OVERRIDE { return "HunterAttack"; }
	virtual ActionResult< Hunter > OnStart( Hunter *me, Action< Hunter > *priorAction ) OVERRIDE;
	virtual ActionResult< Hunter > Update( Hunter *me, float interval ) OVERRIDE;
	virtual EventDesiredResult< Hunter > OnContact( Hunter *me, CBaseEntity *other, CGameTrace *trace ) OVERRIDE;
	virtual EventDesiredResult< Hunter > OnInjured( Hunter *me, const CTakeDamageInfo &info ) OVERRIDE;
	virtual EventDesiredResult< Hunter > OnShoved( Hunter *me, CBaseEntity *pusher ) OVERRIDE;
	virtual EventDesiredResult< Hunter > OnCommandAttack( Hunter *me, CBaseEntity *victim ) OVERRIDE;
	virtual EventDesiredResult< Hunter > OnCommandAssault( Hunter *me ) OVERRIDE;
	virtual EventDesiredResult< Hunter > OnCommandApproach( Hunter *me, const Vector &goal, float range ) OVERRIDE;
private:
	CHandle< CTerrorPlayer > m_victim;
	TerrorDirectChasePath m_path;
};
class HunterLeapToCover : public Action< Hunter >
{
public:
	HunterLeapToCover();
	virtual const char *GetName() const OVERRIDE { return "HunterLeapToCover"; }
	virtual ActionResult< Hunter > OnStart( Hunter *me, Action< Hunter > *priorAction ) OVERRIDE;
	virtual ActionResult< Hunter > Update( Hunter *me, float interval ) OVERRIDE;
	virtual EventDesiredResult< Hunter > OnContact( Hunter *me, CBaseEntity *other, CGameTrace *trace ) OVERRIDE { return TryToSustain( RESULT_CRITICAL ); }
	virtual EventDesiredResult< Hunter > OnInjured( Hunter *me, const CTakeDamageInfo &info ) OVERRIDE { return TryToSustain( RESULT_CRITICAL ); }
	virtual EventDesiredResult< Hunter > OnShoved( Hunter *me, CBaseEntity *pusher ) OVERRIDE { return TryDone( RESULT_CRITICAL ); }
private:
	CNavArea *m_cover;
	CountdownTimer m_aimTimer;
	bool m_hasLeftGround;
};
class HunterLungeAtVictim : public Action< Hunter >
{
public:
	explicit HunterLungeAtVictim( CTerrorPlayer *victim );
	virtual const char *GetName() const OVERRIDE { return "HunterLungeAtVictim"; }
	virtual ActionResult< Hunter > OnStart( Hunter *me, Action< Hunter > *priorAction ) OVERRIDE;
	virtual ActionResult< Hunter > Update( Hunter *me, float interval ) OVERRIDE;
	virtual EventDesiredResult< Hunter > OnContact( Hunter *me, CBaseEntity *other, CGameTrace *trace ) OVERRIDE { return TryToSustain( RESULT_CRITICAL ); }
	virtual EventDesiredResult< Hunter > OnInjured( Hunter *me, const CTakeDamageInfo &info ) OVERRIDE;
	virtual EventDesiredResult< Hunter > OnShoved( Hunter *me, CBaseEntity *pusher ) OVERRIDE;
private:
	CHandle< CTerrorPlayer > m_victim;
	Vector m_goal;
	bool m_hasFired;
	bool m_unused;
	CountdownTimer m_reactionTimer;
	CountdownTimer m_landingTimer;
};
#endif // L4D2_HUNTER_ACTIONS_H
