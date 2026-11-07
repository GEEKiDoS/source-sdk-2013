//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_WITCH_BEHAVIOR_H
#define L4D2_WITCH_BEHAVIOR_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/infected/common/infected_behavior.h"

class Witch;
class CTerrorPlayer;

class WitchExecAction : public Action< Infected >
{
public:
	virtual const char *GetName() const OVERRIDE { return "WitchExecAction"; }
	virtual Action< Infected > *InitialContainedAction( Infected *me ) OVERRIDE;
	virtual ActionResult< Infected > Update( Infected *me, float interval ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnInjured( Infected *me, const CTakeDamageInfo &info ) OVERRIDE;
};

class WitchBehavior : public Action< Infected >
{
public:
	virtual const char *GetName() const OVERRIDE { return "WitchBehavior"; }
	virtual Action< Infected > *InitialContainedAction( Infected *me ) OVERRIDE;
	virtual ActionResult< Infected > Update( Infected *me, float interval ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnIgnite( Infected *me ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnInjured( Infected *me, const CTakeDamageInfo &info ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnKilled( Infected *me, const CTakeDamageInfo &info ) OVERRIDE;
};

class WitchAngry : public Action< Infected >
{
public:
	explicit WitchAngry( float growlDelay = 0.0f );
	virtual const char *GetName() const OVERRIDE { return "WitchAngry"; }
	virtual ActionResult< Infected > OnStart( Infected *me, Action< Infected > *prior ) OVERRIDE;
	virtual ActionResult< Infected > Update( Infected *me, float interval ) OVERRIDE;
	virtual void OnEnd( Infected *me, Action< Infected > *next ) OVERRIDE;
	virtual ActionResult< Infected > OnSuspend( Infected *me, Action< Infected > *interrupt ) OVERRIDE;
	virtual ActionResult< Infected > OnResume( Infected *me, Action< Infected > *interrupt ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnContact( Infected *me, CBaseEntity *other, CGameTrace *trace ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnInjured( Infected *me, const CTakeDamageInfo &info ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnShoved( Infected *me, CBaseEntity *pusher ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnBlinded( Infected *me, CBaseEntity *blinder ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnHitByVomitJar( Infected *me, CBaseEntity *attacker ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnCommandAttack( Infected *me, CBaseEntity *victim ) OVERRIDE;
private:
	void UpdateGrowl( Witch *me );
	CountdownTimer m_initialAngerTimer;
	CountdownTimer m_growlTimer;
	float m_previousRage;
};

class WitchIdle : public Action< Infected >
{
public:
	WitchIdle();
	virtual const char *GetName() const OVERRIDE { return "WitchIdle"; }
	virtual ActionResult< Infected > OnStart( Infected *me, Action< Infected > *prior ) OVERRIDE;
	virtual ActionResult< Infected > Update( Infected *me, float interval ) OVERRIDE;
	virtual void OnEnd( Infected *me, Action< Infected > *next ) OVERRIDE;
	virtual ActionResult< Infected > OnResume( Infected *me, Action< Infected > *interrupt ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnContact( Infected *me, CBaseEntity *other, CGameTrace *trace ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnInjured( Infected *me, const CTakeDamageInfo &info ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnSound( Infected *me, CBaseEntity *source, const Vector &pos, KeyValues *keys ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnShoved( Infected *me, CBaseEntity *pusher ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnBlinded( Infected *me, CBaseEntity *blinder ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnHitByVomitJar( Infected *me, CBaseEntity *attacker ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnCommandAttack( Infected *me, CBaseEntity *victim ) OVERRIDE;
private:
	CountdownTimer m_despairTimer;
	CountdownTimer m_heardFireTimer;
	EHANDLE m_heardFireSource;
};

class WitchAttack : public Action< Infected >
{
public:
	explicit WitchAttack( CBaseEntity *victim );
	virtual const char *GetName() const OVERRIDE { return "WitchAttack"; }
	virtual ActionResult< Infected > OnStart( Infected *me, Action< Infected > *prior ) OVERRIDE;
	virtual ActionResult< Infected > Update( Infected *me, float interval ) OVERRIDE;
	virtual void OnEnd( Infected *me, Action< Infected > *next ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnAnimationEvent( Infected *me, animevent_t *event ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnContact( Infected *me, CBaseEntity *other, CGameTrace *trace ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnMoveToSuccess( Infected *me, const Path *path ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnMoveToFailure( Infected *me, const Path *path, MoveToFailureType reason ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnInjured( Infected *me, const CTakeDamageInfo &info ) OVERRIDE { return TryToSustain(); }
	virtual EventDesiredResult< Infected > OnShoved( Infected *me, CBaseEntity *pusher ) OVERRIDE { return TryToSustain(); }
	virtual EventDesiredResult< Infected > OnSound( Infected *me, CBaseEntity *source, const Vector &pos, KeyValues *keys ) OVERRIDE { return TryToSustain(); }
	virtual EventDesiredResult< Infected > OnCommandAttack( Infected *me, CBaseEntity *victim ) OVERRIDE { return TryToSustain(); }
private:
	CBaseEntity *GetVictim() const;
	void SetVictim( CBaseEntity *victim );
	EHANDLE m_victim;
	int m_victimCharacter;
	InfectedChasePath m_path;
	CountdownTimer m_rageSoundTimer;
	IntervalTimer m_started;
	CUtlVector< EHANDLE > m_shovedPlayers;
	IntervalTimer m_victimChangeTimer;
};

class WitchKillIncapVictim : public Action< Infected >
{
public:
	explicit WitchKillIncapVictim( CBaseEntity *victim );
	virtual const char *GetName() const OVERRIDE { return "WitchKillIncapVictim"; }
	virtual ActionResult< Infected > OnStart( Infected *me, Action< Infected > *prior ) OVERRIDE;
	virtual ActionResult< Infected > Update( Infected *me, float interval ) OVERRIDE;
	virtual void OnEnd( Infected *me, Action< Infected > *next ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnAnimationEvent( Infected *me, animevent_t *event ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnInjured( Infected *me, const CTakeDamageInfo &info ) OVERRIDE { return TryToSustain(); }
	virtual EventDesiredResult< Infected > OnShoved( Infected *me, CBaseEntity *pusher ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnCommandAttack( Infected *me, CBaseEntity *victim ) OVERRIDE { return TryToSustain(); }
private:
	EHANDLE m_victim;
	CountdownTimer m_voiceTimer;
	IntervalTimer m_started;
};

class WitchRetreat : public Action< Infected >
{
public:
	virtual const char *GetName() const OVERRIDE { return "WitchRetreat"; }
	virtual ActionResult< Infected > OnStart( Infected *me, Action< Infected > *prior ) OVERRIDE;
	virtual ActionResult< Infected > Update( Infected *me, float interval ) OVERRIDE;
	virtual void OnEnd( Infected *me, Action< Infected > *next ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnAnimationEvent( Infected *me, animevent_t *event ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnContact( Infected *me, CBaseEntity *other, CGameTrace *trace ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnMoveToSuccess( Infected *me, const Path *path ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnMoveToFailure( Infected *me, const Path *path, MoveToFailureType reason ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnInjured( Infected *me, const CTakeDamageInfo &info ) OVERRIDE { return TryToSustain(); }
	virtual EventDesiredResult< Infected > OnShoved( Infected *me, CBaseEntity *pusher ) OVERRIDE { return TryToSustain(); }
private:
	bool ComputePath( Infected *me );
	PathFollower m_path;
	CountdownTimer m_repathTimer;
	CountdownTimer m_voiceTimer;
	IntervalTimer m_started;
	IntervalTimer m_lastVisible;
	CountdownTimer m_unusedTimer;
	CUtlVector< EHANDLE > m_shovedPlayers;
};

class WitchWander : public Action< Infected >
{
public:
	WitchWander();
	virtual const char *GetName() const OVERRIDE { return "WitchWander"; }
	virtual ActionResult< Infected > OnStart( Infected *me, Action< Infected > *prior ) OVERRIDE;
	virtual ActionResult< Infected > Update( Infected *me, float interval ) OVERRIDE;
	virtual void OnEnd( Infected *me, Action< Infected > *next ) OVERRIDE;
	virtual ActionResult< Infected > OnSuspend( Infected *me, Action< Infected > *interrupt ) OVERRIDE;
	virtual ActionResult< Infected > OnResume( Infected *me, Action< Infected > *interrupt ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnAnimationEvent( Infected *me, animevent_t *event ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnContact( Infected *me, CBaseEntity *other, CGameTrace *trace ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnMoveToSuccess( Infected *me, const Path *path ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnMoveToFailure( Infected *me, const Path *path, MoveToFailureType reason ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnSound( Infected *me, CBaseEntity *source, const Vector &pos, KeyValues *keys ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnInjured( Infected *me, const CTakeDamageInfo &info ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnShoved( Infected *me, CBaseEntity *pusher ) OVERRIDE { return TryToSustain(); }
	virtual EventDesiredResult< Infected > OnHitByVomitJar( Infected *me, CBaseEntity *attacker ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnCommandAttack( Infected *me, CBaseEntity *victim ) OVERRIDE;
private:
	bool ComputePath( Infected *me );
	CountdownTimer m_voiceTimer;
	PathFollower m_path;
	Vector m_lastOrigin;
	IntervalTimer m_musicTimer;
	CountdownTimer m_moveTimer;
	int m_failedPaths;
	CountdownTimer m_attentionTimer;
	EHANDLE m_pendingVictim;
};

class WitchBurn : public Action< Infected >
{
public:
	explicit WitchBurn( CBaseEntity *arsonist );
	virtual const char *GetName() const OVERRIDE { return "WitchBurn"; }
	virtual ActionResult< Infected > OnStart( Infected *me, Action< Infected > *prior ) OVERRIDE;
	virtual ActionResult< Infected > Update( Infected *me, float interval ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnContact( Infected *me, CBaseEntity *other, CGameTrace *trace ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnInjured( Infected *me, const CTakeDamageInfo &info ) OVERRIDE { return TryToSustain(); }
	virtual EventDesiredResult< Infected > OnShoved( Infected *me, CBaseEntity *pusher ) OVERRIDE;
	virtual EventDesiredResult< Infected > OnBlinded( Infected *me, CBaseEntity *blinder ) OVERRIDE { return TryToSustain(); }
	virtual EventDesiredResult< Infected > OnHitByVomitJar( Infected *me, CBaseEntity *attacker ) OVERRIDE;
private:
	float m_yawDrift;
	EHANDLE m_arsonist;
};

class WitchDying : public InfectedDying
{
public:
	explicit WitchDying( const CTakeDamageInfo &info );
	virtual const char *GetName() const OVERRIDE { return "WitchDying"; }
	virtual ActionResult< Infected > OnStart( Infected *me, Action< Infected > *prior ) OVERRIDE;
protected:
	virtual bool SelectDeathActivity( Infected *me ) OVERRIDE;
private:
	int m_attackerUserID;
};

#endif // L4D2_WITCH_BEHAVIOR_H
