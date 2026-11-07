//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef SURVIVOR_INTERACTION_H
#define SURVIVOR_INTERACTION_H
#ifdef _WIN32
#pragma once
#endif

#include "NextBot/NextBotBehavior.h"
#include "util_shared.h"

class SurvivorBot;
class CTerrorPlayer;
class CTerrorWeapon;
class CSurvivorRescue;

// Original SurvivorUseObject is a concrete action, also the base of the native
// collect, rescue, revive, give-pills, ambush and dislodge actions.
class SurvivorUseObject : public Action< SurvivorBot >
{
public:
	explicit SurvivorUseObject( CBaseEntity *object );
	SurvivorUseObject( CBaseEntity *object, const Vector &usePosition );
	virtual const char *GetName() const OVERRIDE { return "SurvivorUseObject"; }
	virtual ActionResult< SurvivorBot > OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction ) OVERRIDE;
	virtual ActionResult< SurvivorBot > Update( SurvivorBot *me, float interval ) OVERRIDE;
	virtual void OnEnd( SurvivorBot *me, Action< SurvivorBot > *nextAction ) OVERRIDE;
	virtual ActionResult< SurvivorBot > OnSuspend( SurvivorBot *me, Action< SurvivorBot > *interruptingAction ) OVERRIDE;
	virtual EventDesiredResult< SurvivorBot > OnStuck( SurvivorBot *me ) OVERRIDE;
	virtual EventDesiredResult< SurvivorBot > OnInjured( SurvivorBot *me, const CTakeDamageInfo &info ) OVERRIDE;

	virtual float GetUseRange( SurvivorBot *me, CBaseEntity *object ) const;
	virtual const Vector &GetUsePosition( SurvivorBot *me, CBaseEntity *object );
	virtual bool ShouldGiveUp( SurvivorBot *me );
	virtual void OnObjectUsed( SurvivorBot *me );
	virtual bool HasUsedObject( SurvivorBot *me ) const;
	virtual bool IsUseComplete( SurvivorBot *me ) const;

protected:
	CHandle< CBaseEntity > m_object;
	CountdownTimer m_usedTimer;
	CountdownTimer m_reachabilityTimer;
	Vector m_usePosition;
};

class SurvivorCollectObject : public SurvivorUseObject
{
public:
	explicit SurvivorCollectObject( CBaseEntity *object ) : SurvivorUseObject( object ) {}
	virtual const char *GetName() const OVERRIDE { return "SurvivorCollectObject"; }
	virtual Action< SurvivorBot > *InitialContainedAction( SurvivorBot *me ) OVERRIDE;
	virtual bool ShouldGiveUp( SurvivorBot *me ) OVERRIDE;
	virtual EventDesiredResult< SurvivorBot > OnMoveToFailure( SurvivorBot *me, const Path *path, MoveToFailureType reason ) OVERRIDE;
};

class SurvivorRescueBarricadedFriend : public SurvivorUseObject
{
public:
	explicit SurvivorRescueBarricadedFriend( CSurvivorRescue *rescue );
	virtual const char *GetName() const OVERRIDE { return "SurvivorRescueBarricadedFriend"; }
	virtual Action< SurvivorBot > *InitialContainedAction( SurvivorBot *me ) OVERRIDE;
	virtual float GetUseRange( SurvivorBot *me, CBaseEntity *object ) const OVERRIDE;
	virtual bool ShouldGiveUp( SurvivorBot *me ) OVERRIDE;
};

class SurvivorReviveFriend : public SurvivorUseObject
{
public:
	explicit SurvivorReviveFriend( CTerrorPlayer *patient );
	virtual const char *GetName() const OVERRIDE { return "SurvivorReviveFriend"; }
	virtual ActionResult< SurvivorBot > OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction ) OVERRIDE;
	virtual ActionResult< SurvivorBot > Update( SurvivorBot *me, float interval ) OVERRIDE;
	virtual Action< SurvivorBot > *InitialContainedAction( SurvivorBot *me ) OVERRIDE;
	virtual float GetUseRange( SurvivorBot *me, CBaseEntity *object ) const OVERRIDE;
	virtual const Vector &GetUsePosition( SurvivorBot *me, CBaseEntity *object ) OVERRIDE;
	virtual bool ShouldGiveUp( SurvivorBot *me ) OVERRIDE;
	virtual void OnObjectUsed( SurvivorBot *me ) OVERRIDE;
	virtual bool IsUseComplete( SurvivorBot *me ) const OVERRIDE;
	virtual EventDesiredResult< SurvivorBot > OnInjured( SurvivorBot *me, const CTakeDamageInfo &info ) OVERRIDE;
	virtual EventDesiredResult< SurvivorBot > OnSound( SurvivorBot *me, CBaseEntity *source, const Vector &pos, KeyValues *keys ) OVERRIDE;

private:
	Vector m_ledgeUsePosition;
};

class SurvivorHealSelf : public Action< SurvivorBot >
{
public:
	virtual const char *GetName() const OVERRIDE { return "SurvivorHealSelf"; }
	virtual ActionResult< SurvivorBot > OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction ) OVERRIDE;
	virtual ActionResult< SurvivorBot > Update( SurvivorBot *me, float interval ) OVERRIDE;
	virtual void OnEnd( SurvivorBot *me, Action< SurvivorBot > *nextAction ) OVERRIDE;
	virtual EventDesiredResult< SurvivorBot > OnInjured( SurvivorBot *me, const CTakeDamageInfo &info ) OVERRIDE;
	virtual EventDesiredResult< SurvivorBot > OnShoved( SurvivorBot *me, CBaseEntity *pusher ) OVERRIDE;

private:
	CountdownTimer m_waitTimer;
};

class SurvivorTakePills : public Action< SurvivorBot >
{
public:
	virtual const char *GetName() const OVERRIDE { return "SurvivorTakePills"; }
	virtual ActionResult< SurvivorBot > OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction ) OVERRIDE;
	virtual ActionResult< SurvivorBot > Update( SurvivorBot *me, float interval ) OVERRIDE;
	virtual void OnEnd( SurvivorBot *me, Action< SurvivorBot > *nextAction ) OVERRIDE;

private:
	CountdownTimer m_waitTimer;
};

class SurvivorHealFriend : public Action< SurvivorBot >
{
public:
	explicit SurvivorHealFriend( CTerrorPlayer *patient ) : m_patient( patient ) {}
	virtual const char *GetName() const OVERRIDE { return "SurvivorHealFriend"; }
	virtual ActionResult< SurvivorBot > OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction ) OVERRIDE;
	virtual ActionResult< SurvivorBot > Update( SurvivorBot *me, float interval ) OVERRIDE;
	virtual void OnEnd( SurvivorBot *me, Action< SurvivorBot > *nextAction ) OVERRIDE;
	virtual EventDesiredResult< SurvivorBot > OnStuck( SurvivorBot *me ) OVERRIDE;
	virtual EventDesiredResult< SurvivorBot > OnInjured( SurvivorBot *me, const CTakeDamageInfo &info ) OVERRIDE;

private:
	bool IsPatientInUseTrace( SurvivorBot *me ) const;
	CHandle< CTerrorPlayer > m_patient;
	bool m_isApproaching;
};

class SurvivorGivePillsToFriend : public SurvivorUseObject
{
public:
	explicit SurvivorGivePillsToFriend( CTerrorPlayer *patient );
	virtual const char *GetName() const OVERRIDE { return "SurvivorGivePillsToFriend"; }
	virtual ActionResult< SurvivorBot > Update( SurvivorBot *me, float interval ) OVERRIDE;
	virtual void OnEnd( SurvivorBot *me, Action< SurvivorBot > *nextAction ) OVERRIDE;
	virtual void OnObjectUsed( SurvivorBot *me ) OVERRIDE;
};

class SurvivorGiveItem : public Action< SurvivorBot >
{
public:
	explicit SurvivorGiveItem( CTerrorPlayer *recipient );
	virtual const char *GetName() const OVERRIDE { return "SurvivorGiveItem"; }
	virtual ActionResult< SurvivorBot > OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction ) OVERRIDE;
	virtual ActionResult< SurvivorBot > Update( SurvivorBot *me, float interval ) OVERRIDE;
	virtual void OnEnd( SurvivorBot *me, Action< SurvivorBot > *nextAction ) OVERRIDE;

private:
	void ThrowItem( SurvivorBot *me );
	CHandle< CTerrorPlayer > m_recipient;
	CHandle< CTerrorWeapon > m_item;
	CountdownTimer m_waitTimer;
	int m_weaponID;
};

class SurvivorAttractForItemGive : public Action< SurvivorBot >
{
public:
	virtual const char *GetName() const OVERRIDE { return "SurvivorAttractForItemGive"; }
	virtual ActionResult< SurvivorBot > OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction ) OVERRIDE;
	virtual ActionResult< SurvivorBot > Update( SurvivorBot *me, float interval ) OVERRIDE;

private:
	CountdownTimer m_waitTimer;
	CountdownTimer m_speechTimer;
	CHandle< CTerrorPlayer > m_recipient;
};

class SurvivorAttractAndGiveItem : public Action< SurvivorBot >
{
public:
	virtual const char *GetName() const OVERRIDE { return "SurvivorAttractAndGiveItem"; }
	virtual Action< SurvivorBot > *InitialContainedAction( SurvivorBot *me ) OVERRIDE;
	virtual ActionResult< SurvivorBot > OnStart( SurvivorBot *me, Action< SurvivorBot > *priorAction ) OVERRIDE;
	virtual ActionResult< SurvivorBot > Update( SurvivorBot *me, float interval ) OVERRIDE;
	virtual void OnEnd( SurvivorBot *me, Action< SurvivorBot > *nextAction ) OVERRIDE;
};

#endif // SURVIVOR_INTERACTION_H
