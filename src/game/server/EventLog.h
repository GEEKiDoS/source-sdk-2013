//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//

#if !defined EVENTLOG_H
#define EVENTLOG_H

#ifdef _WIN32
#pragma once
#endif

#include "GameEventListener.h"
#include <igamesystem.h>
#if defined( L4D2 )
class CBaseEntity;
#endif

class CEventLog : public CGameEventListener, public CBaseGameSystem
{
	
public:
	CEventLog();
	virtual ~CEventLog();
#if defined( L4D2 )
	virtual void GetEntityIdentity( CBaseEntity *pEntity, char *pBuffer, int nBufferSize ) = 0;
#endif

public: // IGameEventListener Interface
	
	virtual void FireGameEvent( IGameEvent * event );
	
public: // CBaseGameSystem overrides

	virtual bool Init();
	virtual void Shutdown();

protected:

	virtual bool PrintEvent( IGameEvent * event );
	virtual bool PrintGameEvent( IGameEvent * event );
	virtual bool PrintPlayerEvent( IGameEvent * event );
	virtual bool PrintTeamEvent( IGameEvent * event );
	virtual bool PrintOtherEvent( IGameEvent * event );
};

extern IGameSystem* GameLogSystem();

#endif // EVENTLOG_H
