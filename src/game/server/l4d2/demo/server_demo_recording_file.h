//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_SERVER_DEMO_RECORDING_FILE_H
#define L4D2_SERVER_DEMO_RECORDING_FILE_H
#ifdef _WIN32
#pragma once
#endif

#include "tier1/utlstring.h"
#include "tier1/utlvector.h"

class KeyValues;
struct ServerDemoCircularBuffer_t;

// Native CServerDemoFile (1CF150/1CF180), not an engine-interface wrapper.
// The original unbound g_pServerDemoSystem root is deliberately NOT activated
// by this owner. Engine-side WriteServerDemoFile is a separate, unsupported
// interface boundary; this class neither fabricates it nor writes fake files.
class CServerDemoFile
{
public:
	CServerDemoFile();
	~CServerDemoFile();
	bool Init( const char *mapName, float startTime ); // S:1CDF40
	int FindOrAddModelName( const char *modelName ); // S:1CF010
	void RecordMessage( KeyValues *message, float time ); // S:1CEED0

private:
	void WriteBaseEntity( KeyValues *message ); // S:1CE5B0/1CDF90
	void WriteBaseAnimating( KeyValues *message ); // S:1CEDE0/1CE8D0
	void WriteBaseAnimatingOverlay( KeyValues *message ); // S:1CEE50/1CEBC0
	void WriteFixedRecord( KeyValues *message, const char *key, unsigned int size );
	ServerDemoCircularBuffer_t *m_pBuffer;
	char m_szMapName[256];
	CUtlVector< CUtlString > m_ModelNames;
	float m_flStartTime;
};

#endif // L4D2_SERVER_DEMO_RECORDING_FILE_H
