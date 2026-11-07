//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_SERVER_DEMO_SYSTEM_H
#define L4D2_SERVER_DEMO_SYSTEM_H
#ifdef _WIN32
#pragma once
#endif
class KeyValues;

// Native IServerDemoSystem vtable1063F8F8 / concrete1063F93C, six slots.
// This is game-owned, not an extension of the target engine tools interface.
class IServerDemoSystem
{
public:
	virtual ~IServerDemoSystem() {}
	virtual void Think() = 0;
	virtual void RecordMessage( KeyValues *message ) = 0;
	virtual void WriteServerDemoFile( int clientIndex, const char *filename ) = 0;
	virtual void Start( const char *mapName ) = 0;
	virtual void Stop() = 0;
};
extern bool g_bAllowServerDemoDump;
extern IServerDemoSystem *g_pServerDemoSystem;
#endif
