//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D_AWARDS_FAULTS_H
#define L4D_AWARDS_FAULTS_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/stats/l4d_awards.h"
#include "util_shared.h"

// These are the twelve original CAwardTemplate subclasses, not one shared
// counter. Event subscriptions and reset boundaries belong to each template.
// Native ctor/Init/event: S375FF0 / S375B80 / S376160.
class CAbandonmentAwardTemplate : public CAwardTemplate
{
public:
	explicit CAbandonmentAwardTemplate( int userID );
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
private:
	struct AbandonedPlayer_t { int userID; float time; };
	CUtlLinkedList< AbandonedPlayer_t > m_AbandonedPlayers;
};

// Native ctor/Init/event: S3766F0 / S376650 / S376790.
class CAbusiveBehaviorAwardTemplate : public CAwardTemplate
{
public:
	explicit CAbusiveBehaviorAwardTemplate( int userID );
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
private:
	IntervalTimer m_ShoveTimers[32];
};

// Native ctor/Init/event: S378120 / S378000 / S378240.
class CEarlyMourningAwardTemplate : public CAwardTemplate
{
public:
	explicit CEarlyMourningAwardTemplate( int userID );
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
private:
	float m_flLastEnemyDeath;
};

// Native ctor/Init/event: S378560 / S378460 / S378750; update S3785F0.
class CForgetfulAwardTemplate : public CAwardTemplate
{
public:
	explicit CForgetfulAwardTemplate( int userID );
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
private:
	void UpdateHealthItemTimer();
	// S378750 writes the interval's raw timestamp to zero on map_transition.
	// Keep that distinct from both Start() and the invalid -1 sentinel.
	float m_flHealthItemStartTime;
	bool m_bForgotHealthItem;
};

// Native ctor/Init/event: S37A020 / S379F70 / S37A090; health class S379FB0.
class CGreedAwardTemplate : public CAwardTemplate
{
public:
	explicit CGreedAwardTemplate( int userID );
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
private:
	// The original constructor deliberately does not initialize this byte.
	bool m_bGreedyHeal;
};

// Native ctor/Init/event: S37BA30 / S37B9B0 / S37BB10.
class CHinderingEscapeAwardTemplate : public CAwardTemplate
{
public:
	explicit CHinderingEscapeAwardTemplate( int userID );
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
private:
	IntervalTimer m_BlockedTimers[32];
};

// Native ctor/Init/event: S37D1D0 / S37D160 / S37D2B0.
class CMurderAwardTemplate : public CAwardTemplate
{
public:
	explicit CMurderAwardTemplate( int userID );
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
private:
	IntervalTimer m_HurtTimers[32];
};

// Native ctor/Init/event: S37CBA0 / S37CB30 / S37CC80.
class CKnockdownAwardTemplate : public CAwardTemplate
{
public:
	explicit CKnockdownAwardTemplate( int userID );
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
private:
	IntervalTimer m_HurtTimers[32];
};

// Native ctor/Init/event: S3770A0 / S376E00 / S376E50.
class CBoomerBlunderAwardTemplate : public CAwardTemplate
{
public:
	explicit CBoomerBlunderAwardTemplate( int userID );
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
};

// Native ctor/Init/event: S37A3F0 / S37A230 / S37A270.
class CGrenadeBlunderAwardTemplate : public CAwardTemplate
{
public:
	explicit CGrenadeBlunderAwardTemplate( int userID );
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
};

// Native ctor/Init/event: S37EF10 / S37EDE0 / S37EF80; scan S37EE80.
class CSecurityFailureAwardTemplate : public CAwardTemplate
{
public:
	explicit CSecurityFailureAwardTemplate( int userID );
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
private:
	bool m_bOpenedCheckpoint;
};

// Native ctor/Init/event: S380F90 / S380F70 / S380EA0.
class CWitchHarassmentAwardTemplate : public CAwardTemplate
{
public:
	explicit CWitchHarassmentAwardTemplate( int userID );
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
};

#endif // L4D_AWARDS_FAULTS_H
