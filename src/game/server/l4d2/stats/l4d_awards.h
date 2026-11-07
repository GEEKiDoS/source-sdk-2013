//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D_AWARDS_H
#define L4D_AWARDS_H
#ifdef _WIN32
#pragma once
#endif

#include "GameEventListener.h"
#include "tier1/utllinkedlist.h"

class CTerrorPlayer;

// Native CAwardTemplate owns a user ID, not an expresser or an entity handle.
class CAwardTemplate : public CGameEventListener
{
public:
	explicit CAwardTemplate( int userID );
	virtual void Init() = 0;
	CTerrorPlayer *GetPlayer() const;
	virtual bool EarnAward( int award, CBaseEntity *subject = NULL );
private:
	int m_userID;
};

// RTTI S7B24C8, constructor S379070, event consumer S379310.
class CFriendlyFireAwardTemplate : public CAwardTemplate
{
public:
	explicit CFriendlyFireAwardTemplate( int userID );
	virtual void Init() OVERRIDE;
	void Reset();
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
private:
	struct Hit_t { int damage; float time; };
	void EmitFriendlyFire( CTerrorPlayer *victim, CTerrorPlayer *guilty, int damageType );
	unsigned int m_nLastAttackSequence;
	bool m_hasLastAttackSequence; // Skip only undefined native serial comparison before Reset/real writer.
	CUtlLinkedList< Hit_t > m_Hits[4];
	IntervalTimer m_RecklessTimer;
	IntervalTimer m_FriendlyFireTimer;
};

class CReviveFriendAwardTemplate : public CAwardTemplate
{
public:
	explicit CReviveFriendAwardTemplate( int userID ) : CAwardTemplate( userID ) {}
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
};

class CRescuerAwardTemplate : public CAwardTemplate
{
public:
	explicit CRescuerAwardTemplate( int userID ) : CAwardTemplate( userID ) {}
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
};

class CSharingAwardTemplate : public CAwardTemplate
{
public:
	explicit CSharingAwardTemplate( int userID ) : CAwardTemplate( userID ) {}
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
};

class CHelpingHandAwardTemplate : public CAwardTemplate
{
public:
	explicit CHelpingHandAwardTemplate( int userID ) : CAwardTemplate( userID ) {}
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
};

class CSaviorAwardTemplate : public CAwardTemplate
{
public:
	explicit CSaviorAwardTemplate( int userID ) : CAwardTemplate( userID ) {}
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
};

class CSurvivorKillAwardTemplate : public CAwardTemplate
{
public:
	explicit CSurvivorKillAwardTemplate( int userID ) : CAwardTemplate( userID ) {}
	virtual void Init() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
};

const char *GetAwardResponseName( int award );
int GetAwardResponseType( int award );

#endif // L4D_AWARDS_H
