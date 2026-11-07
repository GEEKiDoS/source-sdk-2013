//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Game-owned reader for the original L4D2 scene image.
//
//=============================================================================//

#ifndef TERROR_SCENE_FILE_CACHE_H
#define TERROR_SCENE_FILE_CACHE_H
#ifdef _WIN32
#pragma once
#endif

#include "tier1/utlbuffer.h"

struct SceneImageEntry_t;

// This is the L4D2 result, not the stock SceneFileCache002 result ABI.
struct TerrorSceneCachedData_t
{
	unsigned int	msecs;
	float			lastSpeakSecs;
	int				numSounds;
	int				sceneId;
};

class CTerrorSceneFileCache
{
public:
	bool Init();
	void Shutdown();

	size_t GetSceneBufferSize( const char *pFilename );
	bool GetSceneData( const char *pFilename, byte *pBuffer, size_t nBufferSize );
	bool GetSceneCachedData( const char *pFilename, TerrorSceneCachedData_t *pData );
	short GetSceneCachedSound( int iScene, int iSound );
	const char *GetSceneString( short stringId );
	void Reload();

private:
	int FindSceneInImage( const char *pFilename ) const;
	const SceneImageEntry_t *GetSceneEntry( int iScene ) const;

	CUtlBuffer m_SceneImage;
};

extern CTerrorSceneFileCache *scenefilecache;

#endif // TERROR_SCENE_FILE_CACHE_H
