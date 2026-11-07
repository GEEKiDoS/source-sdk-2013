//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_CHARGER_ACTIONS_H
#define L4D2_CHARGER_ACTIONS_H
#ifdef _WIN32
#pragma once
#endif
#include "l4d2/infected/special/charger.h"
#include "NextBot/NextBotBehavior.h"
#include "NextBot/Path/NextBotPathFollow.h"
#include "l4d2/nav/terror_direct_chase_path.h"
class ChargerBehavior : public Action< Charger >
{
public:
	ChargerBehavior() : m_lastStuckTime( 0.0f ) {}
	virtual const char *GetName() const OVERRIDE { return "ChargerBehavior"; }
	virtual Action< Charger > *InitialContainedAction( Charger *me ) OVERRIDE;
	virtual EventDesiredResult< Charger > OnContact( Charger *me, CBaseEntity *other, CGameTrace *trace ) OVERRIDE;
	virtual EventDesiredResult< Charger > OnStuck( Charger *me ) OVERRIDE;
private:
	float m_lastStuckTime;
};
class ChargerAssault : public Action< Charger >
{
public:
	ChargerAssault();
	virtual const char *GetName() const OVERRIDE { return "ChargerAssault"; }
	virtual ActionResult< Charger > OnStart( Charger *me, Action< Charger > *prior ) OVERRIDE;
	virtual ActionResult< Charger > Update( Charger *me, float interval ) OVERRIDE;
	virtual ActionResult< Charger > OnSuspend( Charger *me, Action< Charger > *interrupt ) OVERRIDE { return Continue(); }
	virtual ActionResult< Charger > OnResume( Charger *me, Action< Charger > *interrupt ) OVERRIDE;
	virtual EventDesiredResult< Charger > OnContact( Charger *me, CBaseEntity *other, CGameTrace *trace ) OVERRIDE;
	virtual EventDesiredResult< Charger > OnInjured( Charger *me, const CTakeDamageInfo &info ) OVERRIDE { return TryToSustain(); }
	virtual EventDesiredResult< Charger > OnShoved( Charger *me, CBaseEntity *pusher ) OVERRIDE { return TryToSustain(); }
	virtual EventDesiredResult< Charger > OnStuck( Charger *me ) OVERRIDE { return TryToSustain(); }
private:
	CHandle< CTerrorPlayer > m_victim;
	TerrorDirectChasePath m_path;
	CountdownTimer m_unusedTimer;
	CountdownTimer m_attackTimer;
	CountdownTimer m_offMeshTimer;
	IntervalTimer m_startedTimer;
};
class ChargerAttack : public Action< Charger >
{
public:
	ChargerAttack();
	virtual const char *GetName() const OVERRIDE { return "ChargerAttack"; }
	virtual ActionResult< Charger > OnStart( Charger *me, Action< Charger > *prior ) OVERRIDE;
	virtual ActionResult< Charger > Update( Charger *me, float interval ) OVERRIDE;
	virtual ActionResult< Charger > OnSuspend( Charger *me, Action< Charger > *interrupt ) OVERRIDE { return Continue(); }
	virtual ActionResult< Charger > OnResume( Charger *me, Action< Charger > *interrupt ) OVERRIDE;
	virtual EventDesiredResult< Charger > OnContact( Charger *me, CBaseEntity *other, CGameTrace *trace ) OVERRIDE;
	virtual EventDesiredResult< Charger > OnInjured( Charger *me, const CTakeDamageInfo &info ) OVERRIDE { return TryToSustain(); }
	virtual EventDesiredResult< Charger > OnShoved( Charger *me, CBaseEntity *pusher ) OVERRIDE { return TryToSustain(); }
	virtual EventDesiredResult< Charger > OnStuck( Charger *me ) OVERRIDE { return TryToSustain(); }
	virtual EventDesiredResult< Charger > OnCommandAttack( Charger *me, CBaseEntity *victim ) OVERRIDE;
	virtual EventDesiredResult< Charger > OnCommandAssault( Charger *me ) OVERRIDE;
	virtual EventDesiredResult< Charger > OnCommandApproach( Charger *me, const Vector &goal, float range ) OVERRIDE;
private:
	CHandle< CTerrorPlayer > m_victim;
	TerrorDirectChasePath m_path;
	CountdownTimer m_unusedTimer;
	CountdownTimer m_attackTimer;
	CountdownTimer m_offMeshTimer;
	IntervalTimer m_startedTimer;
};
class ChargerReturnToNavMesh : public Action< Charger >
{
public:
	virtual const char *GetName() const OVERRIDE { return "ChargerReturnToNavMesh"; }
	virtual ActionResult< Charger > Update( Charger *me, float interval ) OVERRIDE;
};
class ChargerChargeAtVictim : public Action< Charger >
{
public:
	explicit ChargerChargeAtVictim( CTerrorPlayer *victim ) : m_victim( victim ) {}
	virtual const char *GetName() const OVERRIDE { return "ChargerChargeAtVictim"; }
	virtual ActionResult< Charger > OnStart( Charger *me, Action< Charger > *prior ) OVERRIDE;
	virtual ActionResult< Charger > Update( Charger *me, float interval ) OVERRIDE;
	virtual EventDesiredResult< Charger > OnContact( Charger *me, CBaseEntity *other, CGameTrace *trace ) OVERRIDE { return TryToSustain( RESULT_CRITICAL ); }
	virtual EventDesiredResult< Charger > OnInjured( Charger *me, const CTakeDamageInfo &info ) OVERRIDE { return TryToSustain( RESULT_CRITICAL ); }
	virtual EventDesiredResult< Charger > OnShoved( Charger *me, CBaseEntity *pusher ) OVERRIDE { return TryDone( RESULT_CRITICAL ); }
private:
	CHandle< CTerrorPlayer > m_victim;
	Vector m_unused;
	bool m_hasFired;
	CountdownTimer m_reactionTimer;
};
class ChargerEvade : public Action< Charger >
{
public:
	virtual const char *GetName() const OVERRIDE { return "ChargerEvade"; }
	virtual ActionResult< Charger > OnStart( Charger *me, Action< Charger > *prior ) OVERRIDE;
	virtual ActionResult< Charger > Update( Charger *me, float interval ) OVERRIDE;
	virtual EventDesiredResult< Charger > OnContact( Charger *me, CBaseEntity *other, CGameTrace *trace ) OVERRIDE { return TryToSustain(); }
	virtual EventDesiredResult< Charger > OnInjured( Charger *me, const CTakeDamageInfo &info ) OVERRIDE { return TryToSustain( RESULT_IMPORTANT ); }
	virtual EventDesiredResult< Charger > OnShoved( Charger *me, CBaseEntity *pusher ) OVERRIDE { return TryToSustain(); }
	virtual EventDesiredResult< Charger > OnCommandAttack( Charger *me, CBaseEntity *victim ) OVERRIDE;
	virtual EventDesiredResult< Charger > OnCommandApproach( Charger *me, const Vector &goal, float range ) OVERRIDE;
private:
	CNavArea *m_area;
	PathFollower m_path;
};
#endif // L4D2_CHARGER_ACTIONS_H
