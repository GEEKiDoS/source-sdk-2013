//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_TERROR_TRACE_FILTER_H
#define L4D2_TERROR_TRACE_FILTER_H
#ifdef _WIN32
#pragma once
#endif

#include "util_shared.h"

// S0F35A0/C0FF310: the original name is misleading: only grenades are rejected.
class CTraceFilterIgnoreWeapons : public CTraceFilterSkipTwoEntities
{
public:
	CTraceFilterIgnoreWeapons( const IHandleEntity *pass, const IHandleEntity *pass2, int group ) :
		CTraceFilterSkipTwoEntities( pass, pass2, group ) {}
	virtual bool ShouldHitEntity( IHandleEntity *handle, int mask ) OVERRIDE;
};

class CTraceFilterPlayerMovement : public CTraceFilterSkipTwoEntities
{
public:
	CTraceFilterPlayerMovement() : CTraceFilterSkipTwoEntities( NULL, NULL, COLLISION_GROUP_NONE ), m_isGhost( false ) {}
	virtual void SetPassEntity( const IHandleEntity *pass ) OVERRIDE;
	virtual bool ShouldHitEntity( IHandleEntity *handle, int mask ) OVERRIDE;
private:
	bool m_isGhost;
};

class CTraceFilterSimpleListNoGhosts : public CTraceFilterSimpleList
{
public:
	explicit CTraceFilterSimpleListNoGhosts( int group ) : CTraceFilterSimpleList( group ) {}
	virtual bool ShouldHitEntity( IHandleEntity *handle, int mask ) OVERRIDE;
};

#ifndef CLIENT_DLL
class CTraceFilterSimpleListNoInfectedOrGhosts : public CTraceFilterSimpleListNoGhosts
{
public:
	explicit CTraceFilterSimpleListNoInfectedOrGhosts( int group ) : CTraceFilterSimpleListNoGhosts( group ) {}
	virtual bool ShouldHitEntity( IHandleEntity *handle, int mask ) OVERRIDE;
};

class CTraceFilterSimpleNoInfectedTeamOrGhosts : public CTraceFilterSimpleListNoGhosts
{
public:
	explicit CTraceFilterSimpleNoInfectedTeamOrGhosts( int group ) : CTraceFilterSimpleListNoGhosts( group ) {}
	virtual bool ShouldHitEntity( IHandleEntity *handle, int mask ) OVERRIDE;
};

class ZombieBotCollisionTraceFilter : public CTraceFilterSimple
{
public:
	ZombieBotCollisionTraceFilter( const IHandleEntity *pass, int group ) : CTraceFilterSimple( pass, group ) {}
	virtual bool ShouldHitEntity( IHandleEntity *handle, int mask ) OVERRIDE;
};

class CTraceFilterIgnoreActors : public CTraceFilterSimple
{
public:
	CTraceFilterIgnoreActors( const IHandleEntity *pass, int group, bool hitTanks, int team ) :
		CTraceFilterSimple( pass, group ), m_hitTanks( hitTanks ), m_team( team ) {}
	virtual bool ShouldHitEntity( IHandleEntity *handle, int mask ) OVERRIDE;
private:
	bool m_hitTanks;
	int m_team;
};

#endif

#endif
