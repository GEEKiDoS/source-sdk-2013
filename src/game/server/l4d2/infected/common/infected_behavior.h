//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_INFECTED_BEHAVIOR_H
#define L4D2_INFECTED_BEHAVIOR_H
#ifdef _WIN32
#pragma once
#endif

#include "NextBot/NextBotBehavior.h"
#include "NextBot/NextBotBodyInterface.h"
#include "l4d2/nav/terror_direct_chase_path.h"
#include "NextBot/Path/NextBotPathFollow.h"
#include "takedamageinfo.h"

class Infected;
class TerrorNavArea;

class InfectedBehavior : public Action< Infected >
{
public:
	InfectedBehavior();
	virtual const char *GetName() const OVERRIDE { return "InfectedBehavior"; }
	virtual Action< Infected > *InitialContainedAction( Infected *me ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnKilled( Infected *me, const CTakeDamageInfo &info ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnInjured( Infected *me, const CTakeDamageInfo &info ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnShoved( Infected *me, CBaseEntity *pusher ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnCommandApproach( Infected *me, const Vector &goal, float range ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnCommandApproach( Infected *me, CBaseEntity *goal ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnIgnite( Infected *me ) OVERRIDE;
private:
	int m_shoveTolerance;
};

// Original intention has this outer action, with InfectedBehavior as its child.
class InfectedExecAction : public Action< Infected >
{
public:
	virtual const char *GetName() const OVERRIDE { return "InfectedExecAction"; }
	virtual Action< Infected > *InitialContainedAction( Infected *me ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnInjured( Infected *me, const CTakeDamageInfo &info ) OVERRIDE;
};

class InfectedWander : public Action< Infected >
{
public:
	explicit InfectedWander( Action< Infected > *initialChild = NULL );
	virtual const char *GetName() const OVERRIDE { return "InfectedWander"; }
	virtual ActionResult< Infected > OnStart( Infected *me, Action< Infected > *priorAction ) OVERRIDE;
	virtual ActionResult< Infected > Update( Infected *me, float interval ) OVERRIDE;
	virtual ActionResult< Infected > OnResume( Infected *me, Action< Infected > *interruptingAction ) OVERRIDE;
	virtual Action< Infected > *InitialContainedAction( Infected *me ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnContact( Infected *me, CBaseEntity *other, CGameTrace *trace ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnInjured( Infected *me, const CTakeDamageInfo &info ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnSound( Infected *me, CBaseEntity *source, const Vector &pos, KeyValues *keys ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnShoved( Infected *me, CBaseEntity *pusher ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnCommandAttack( Infected *me, CBaseEntity *victim ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnCommandString( Infected *me, const char *command ) OVERRIDE;
	void BecomeAlert( Infected *me, const Vector &pos );
private:
	Action< Infected > *m_initialChild;
	CountdownTimer m_voiceTimer;
	CountdownTimer m_alertTimer;
	Vector m_recognitionPosition;
	CountdownTimer m_wanderTimer;
	CountdownTimer m_brawlTimer; // Native104; no extra OnStart/OnResume reset.
};

class InfectedAlert : public Action< Infected >
{
public:
	enum Reason { SOUND, INJURY };
	InfectedAlert( const Vector &pos, Reason reason = SOUND );
	virtual const char *GetName() const OVERRIDE { return "InfectedAlert"; }
	virtual ActionResult< Infected > OnStart( Infected *me, Action< Infected > *priorAction ) OVERRIDE;
	virtual ActionResult< Infected > Update( Infected *me, float interval ) OVERRIDE;
	virtual ActionResult< Infected > OnResume( Infected *me, Action< Infected > *interruptingAction ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnContact( Infected *me, CBaseEntity *other, CGameTrace *trace ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnInjured( Infected *me, const CTakeDamageInfo &info ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnPostureChanged( Infected *me ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnAnimationActivityComplete( Infected *me, int activity ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnSound( Infected *me, CBaseEntity *source, const Vector &pos, KeyValues *keys ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnShoved( Infected *me, CBaseEntity *pusher ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnCommandAttack( Infected *me, CBaseEntity *victim ) OVERRIDE;
private:
	bool StartAlertActivity( Infected *me );
	Vector m_alertPosition;
	Reason m_reason;
	CountdownTimer m_calmTimer;
	CountdownTimer m_acquireTimer;
	CountdownTimer m_voiceTimer;
	CountdownTimer m_soundTimer;
	CountdownTimer m_reorientTimer;
};

class InfectedFlee : public Action< Infected >
{
public:
    virtual const char *GetName() const OVERRIDE { return "InfectedFlee"; }
    virtual ActionResult< Infected > OnStart( Infected *me, Action< Infected > *prior ) OVERRIDE;
    virtual ActionResult< Infected > Update( Infected *me, float interval ) OVERRIDE;
    virtual EventDesiredResult< Infected > OnContact( Infected *me, CBaseEntity *other, CGameTrace *trace ) OVERRIDE;
    virtual EventDesiredResult< Infected > OnStuck( Infected *me ) OVERRIDE;
    virtual EventDesiredResult< Infected > OnShoved( Infected *me, CBaseEntity *pusher ) OVERRIDE;
private:
    PathFollower m_path;
    TerrorNavArea *m_goal; // Native raw area pointer; not a duplicate route owner.
};
class InfectedShoved : public Action< Infected >
{
public:
	InfectedShoved( CBaseEntity *pusher, Action< Infected > *successor = NULL );
	virtual ~InfectedShoved();
	virtual const char *GetName() const OVERRIDE { return "InfectedShoved"; }
	virtual ActionResult< Infected > OnStart( Infected *me, Action< Infected > *priorAction ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnAnimationActivityComplete( Infected *me, int activity ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnContact( Infected *me, CBaseEntity *other, CGameTrace *trace ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnShoved( Infected *me, CBaseEntity *pusher ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnInjured( Infected *me, const CTakeDamageInfo &info ) OVERRIDE { return TryToSustain(); } // 40E8F0
private:
	bool StartShoveActivity( Infected *me );
	EHANDLE m_pusher;
	Vector m_shoveFrom;
	Action< Infected > *m_successor;
};

class InfectedAttack : public Action< Infected >
{
public:
	explicit InfectedAttack( CBaseEntity *victim );
	virtual const char *GetName() const OVERRIDE { return "InfectedAttack"; }
	virtual ActionResult< Infected > OnStart( Infected *me, Action< Infected > *priorAction ) OVERRIDE;
	virtual ActionResult< Infected > Update( Infected *me, float interval ) OVERRIDE;
	virtual ActionResult< Infected > OnSuspend( Infected *me, Action< Infected > *interruptingAction ) OVERRIDE;
	virtual Action< Infected > *InitialContainedAction( Infected *me ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnShoved( Infected *me, CBaseEntity *pusher ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnInjured( Infected *me, const CTakeDamageInfo &info ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnBlinded( Infected *me, CBaseEntity *blinder ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnSound( Infected *me, CBaseEntity *source, const Vector &pos, KeyValues *keys ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnLeaveGround( Infected *me, CBaseEntity *ground ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnLandOnGround( Infected *me, CBaseEntity *ground ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnContact( Infected *me, CBaseEntity *other, CGameTrace *trace ) OVERRIDE { return TryToSustain(); }
	CBaseEntity *GetVictim() const { return m_victim.Get(); }
	void SetVictim( CBaseEntity *victim ) { m_victim = victim; }
	void OnPunch( Infected *me, CBaseEntity *victim );
private:
	EHANDLE m_victim;
	bool m_playerVictim;
};

class InfectedChasePath : public TerrorDirectChasePath
{
public:
	explicit InfectedChasePath( SubjectChaseType chaseHow = LEAD_SUBJECT );
	virtual void ComputeAreaCrossing( INextBot *bot, const CNavArea *from, const Vector &fromPos, const CNavArea *to, NavDirType dir, Vector *crossPos ) const OVERRIDE;
};

class ChaseVictim : public Action< Infected >
{
public:
	explicit ChaseVictim( InfectedAttack *attack );
	virtual const char *GetName() const OVERRIDE { return "ChaseVictim"; }
	virtual ActionResult< Infected > OnStart( Infected *me, Action< Infected > *priorAction ) OVERRIDE;
	virtual ActionResult< Infected > Update( Infected *me, float interval ) OVERRIDE;
	virtual ActionResult< Infected > OnSuspend( Infected *me, Action< Infected > *interruptingAction ) OVERRIDE;
	virtual ActionResult< Infected > OnResume( Infected *me, Action< Infected > *interruptingAction ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnContact( Infected *me, CBaseEntity *other, CGameTrace *trace ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnAnimationEvent( Infected *me, animevent_t *event ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnCommandAttack( Infected *me, CBaseEntity *victim ) OVERRIDE;
private:
	InfectedAttack *m_attack; // containing Action owns us and outlives us
	InfectedChasePath m_path;
	CountdownTimer m_movingAttackTimer;
	CountdownTimer m_doorUseTimer;
	CountdownTimer m_voiceTimer;
};

class PunchVictim : public Action< Infected >
{
public:
	PunchVictim( InfectedAttack *attack, CBaseEntity *victim );
	virtual const char *GetName() const OVERRIDE { return "PunchVictim"; }
	virtual ActionResult< Infected > OnStart( Infected *me, Action< Infected > *priorAction ) OVERRIDE;
	virtual ActionResult< Infected > Update( Infected *me, float interval ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnAnimationEvent( Infected *me, animevent_t *event ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnContact( Infected *me, CBaseEntity *other, CGameTrace *trace ) OVERRIDE { return TryToSustain(); }
	virtual EventDesiredResult< Infected > OnCommandAttack( Infected *me, CBaseEntity *victim ) OVERRIDE;
private:
	InfectedAttack *m_attack;
	CountdownTimer m_deadVictimTimer;
	IntervalTimer m_started;
	CountdownTimer m_voiceTimer;
	EHANDLE m_victim;
};

class InfectedDying : public Action< Infected >
{
public:
	explicit InfectedDying( const CTakeDamageInfo &info );
	virtual const char *GetName() const OVERRIDE { return "InfectedDying"; }
	virtual ActionResult< Infected > OnStart( Infected *me, Action< Infected > *priorAction ) OVERRIDE;
	virtual ActionResult< Infected > Update( Infected *me, float interval ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnAnimationActivityComplete( Infected *me, int activity ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnAnimationEvent( Infected *me, animevent_t *event ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnContact( Infected *me, CBaseEntity *other, CGameTrace *trace ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnShoved( Infected *me, CBaseEntity *pusher ) OVERRIDE;
protected:
	virtual bool SelectDeathActivity( Infected *me );
	CTakeDamageInfo m_info;
	float m_yawDrift;
	int m_optionalEnds;
private:
	void ApplySceneDamage( Infected *me, const Vector &pos );
};

#endif // L4D2_INFECTED_BEHAVIOR_H
