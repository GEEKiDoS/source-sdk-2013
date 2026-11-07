//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef SURVIVOR_INTENTION_H
#define SURVIVOR_INTENTION_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/player/terrorplayer.h"
#include "NextBot/NextBotIntentionInterface.h"
#include "NextBot/NextBotBehavior.h"
#include "tier1/utlvector.h"

class SurvivorBot;
class CTerrorPlayer;
class CNavArea;

// Retail survivor knowledge is not CKnownEntity. In particular, refreshing a
// heard position does NOT restart its hearing timer (S39FD40).
class SurvivorKnownEntity
{
public:
	explicit SurvivorKnownEntity( CBaseCombatCharacter *entity );
	CBaseCombatCharacter *GetEntity() const;
	const Vector &GetLastKnownPosition() const { return m_lastPosition; }
	CNavArea *GetLastKnownArea() const { return m_lastArea; }
	bool IsVisibleNow() const { return m_visible; }
	float GetTimeSinceLastSeen() const { return m_lastSeen.GetElapsedTime(); }
	float GetTimeSinceLastHeard() const { return m_lastHeard.GetElapsedTime(); }
	float GetTimeWhenLastSeen() const { return m_lastSeen.GetStartTime(); }
	bool WasEverSeen() const { return m_lastSeen.GetStartTime() > 0.0f; }
	int GetActorID() const { return m_actorID; }
	void MarkSeen();
	void UpdateHeardPosition();

private:
	friend class SurvivorIntention;
	CHandle< CBaseCombatCharacter > m_entity;
	Vector m_lastPosition;
	CNavArea *m_lastArea;
	IntervalTimer m_lastSeen;
	IntervalTimer m_lastHeard;
	int m_actorID;
	bool m_visible;
};

class SurvivorIntention : public IIntention
{
public:
	explicit SurvivorIntention( SurvivorBot *bot );
	virtual ~SurvivorIntention();
	virtual void Reset() OVERRIDE;
	virtual void Update() OVERRIDE;
	virtual INextBotEventResponder *FirstContainedResponder() const OVERRIDE;
	virtual INextBotEventResponder *NextContainedResponder( INextBotEventResponder *current ) const OVERRIDE;
	virtual QueryResultType ShouldHurry( const INextBot *me ) const OVERRIDE;
	virtual QueryResultType IsPositionAllowed( const INextBot *me, const Vector &position ) const OVERRIDE;
	virtual void OnInjured( const CTakeDamageInfo &info ) OVERRIDE;
	virtual void OnSound( CBaseEntity *source, const Vector &position, KeyValues *keys ) OVERRIDE;

	const CUtlVector< SurvivorKnownEntity > &GetKnownThreats() const { return m_knownThreats; }
	const SurvivorKnownEntity *GetPrimaryKnownThreat() const;
	SurvivorKnownEntity *FindKnownThreat( CBaseCombatCharacter *entity );
	void AddKnownThreat( CBaseCombatCharacter *entity );
	bool IsThreatDangerous( CTerrorPlayer *subject, CBaseEntity *threat ) const;
	bool IsImmediateThreat( CTerrorPlayer *subject, CBaseEntity *threat ) const;
	void PushLegsAction( Action< SurvivorBot > *action );
	void PopLegsAction();
	void ChangeLegsAction( Action< SurvivorBot > *action );
	void ResetLegsAction( Action< SurvivorBot > *action );

private:
	void ClearLegsActions();
	void UpdateKnownThreats();
	SurvivorBot *m_actor;
	Behavior< SurvivorBot > *m_behavior;
	Behavior< SurvivorBot > *m_legsBehavior[16];
	int m_currentLegsBehavior;
	CUtlVector< SurvivorKnownEntity > m_knownThreats;
	struct PendingSound
	{
		CHandle< CBaseCombatCharacter > entity;
		float recognizeTime;
	};
	CUtlVector< PendingSound > m_pendingSounds;
};

extern ConVar sb_hold_position;
extern ConVar sb_l4d1_survivor_behavior;
extern ConVar sb_threat_medium_range;
extern ConVar sb_threat_far_range;
extern ConVar sb_threat_very_far_range;

#endif // SURVIVOR_INTENTION_H
