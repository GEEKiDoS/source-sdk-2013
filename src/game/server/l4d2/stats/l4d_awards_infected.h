//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D_AWARDS_INFECTED_H
#define L4D_AWARDS_INFECTED_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/stats/l4d_awards.h"
#include "util_shared.h"

class CClawHitAwardTemplate : public CAwardTemplate
{
public:
	explicit CClawHitAwardTemplate( int userID ) : CAwardTemplate( userID ) {}
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
};

class CDoomsdayAwardTemplate : public CAwardTemplate
{
public:
	explicit CDoomsdayAwardTemplate( int userID ) : CAwardTemplate( userID ) {}
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
};

class CExplosionAwardTemplate : public CAwardTemplate
{
public:
	explicit CExplosionAwardTemplate( int userID );
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
private:
	float m_deathTime;
};

class CFallCauserAwardTemplate : public CAwardTemplate
{
public:
	explicit CFallCauserAwardTemplate( int userID ) : CAwardTemplate( userID ) {}
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
};

class CHitAndRunAwardTemplate : public CAwardTemplate
{
public:
	explicit CHitAndRunAwardTemplate( int userID );
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
	virtual bool EarnAward( int award, CBaseEntity *subject = NULL ) OVERRIDE;
private:
	int m_hasInteracted;
	int m_hitCount;
};

class CHitEverybodyAwardTemplate : public CAwardTemplate
{
public:
	explicit CHitEverybodyAwardTemplate( int userID );
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
private:
	bool m_hit[32];
	int m_hitCount;
};

class CHulkPhysicsAwardTemplate : public CAwardTemplate
{
public:
	explicit CHulkPhysicsAwardTemplate( int userID ) : CAwardTemplate( userID ) {}
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
};

class CHulkPunchAwardTemplate : public CAwardTemplate
{
public:
	explicit CHulkPunchAwardTemplate( int userID );
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
private:
	// Native requests 48 bytes: slot 32 accesses byte +48 outside the object.
	// Its physical heap neighbor/effect is unobserved. This owned storage is
	// containment, NOT native slot-32 parity or authorization to register it.
	bool m_incapacitated[ MAX_PLAYERS + 1 ];
};

class CHulkRockAwardTemplate : public CAwardTemplate
{
public:
	explicit CHulkRockAwardTemplate( int userID ) : CAwardTemplate( userID ) {}
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
};

class CMostHitsAwardTemplate : public CAwardTemplate
{
public:
	explicit CMostHitsAwardTemplate( int userID ) : CAwardTemplate( userID ) {}
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
};

class CPounceAwardTemplate : public CAwardTemplate
{
public:
	explicit CPounceAwardTemplate( int userID ) : CAwardTemplate( userID ) {}
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
};

class CRedecoratorAwardTemplate : public CAwardTemplate
{
public:
	explicit CRedecoratorAwardTemplate( int userID ) : CAwardTemplate( userID ) {}
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
};

class CTongueChokeAwardTemplate : public CAwardTemplate
{
public:
	explicit CTongueChokeAwardTemplate( int userID );
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
	virtual bool EarnAward( int award, CBaseEntity *subject = NULL ) OVERRIDE;
private:
	bool m_hasChoked;
	int m_chokeCount;
};

class CVomitHitAwardTemplate : public CAwardTemplate
{
public:
	explicit CVomitHitAwardTemplate( int userID );
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
	virtual bool EarnAward( int award, CBaseEntity *subject = NULL ) OVERRIDE;
private:
	int m_hitCount;
	CountdownTimer m_hitTimer;
};

// The native factory constructs Gotcha for both teams, NoSurvivors for infected.
class CGotchaAwardTemplate : public CAwardTemplate
{
public:
	explicit CGotchaAwardTemplate( int userID ) : CAwardTemplate( userID ) {}
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
};

class CNoSurvivorsAwardTemplate : public CAwardTemplate
{
public:
	explicit CNoSurvivorsAwardTemplate( int userID ) : CAwardTemplate( userID ) {}
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
};

#endif // L4D_AWARDS_INFECTED_H
