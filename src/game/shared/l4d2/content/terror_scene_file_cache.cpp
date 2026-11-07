//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Game-owned reader for the original L4D2 scene image.
//
//=============================================================================//

#include "tier0/dbg.h"
#include "l4d2/content/terror_scene_file_cache.h"
#include "filesystem.h"
#include "scenefilecache/SceneImageFile.h"
#include "tier1/checksum_crc.h"
#include "tier1/lzmaDecoder.h"
#include "tier1/strtools.h"
#include "tier1/utlmemory.h"

#include "tier0/memdbgon.h"

extern IFileSystem *filesystem;

struct TerrorSceneImageSummary_t
{
	unsigned int msecs;
	unsigned int lastspeech_msecs;
	int numSounds;
	int soundStrings[1];
};

COMPILE_TIME_ASSERT( sizeof( SceneImageHeader_t ) == 20 );
COMPILE_TIME_ASSERT( sizeof( SceneImageEntry_t ) == 16 );
COMPILE_TIME_ASSERT( sizeof( TerrorSceneCachedData_t ) == 16 );

static CTerrorSceneFileCache s_TerrorSceneFileCache;
CTerrorSceneFileCache *scenefilecache = &s_TerrorSceneFileCache;

bool CTerrorSceneFileCache::Init()
{
	if ( m_SceneImage.TellMaxPut() == 0 )
	{
		if ( filesystem->ReadFile( "scenes/scenes.image", "GAME", m_SceneImage ) )
		{
			const SceneImageHeader_t *pHeader = static_cast<const SceneImageHeader_t *>( m_SceneImage.Base() );
			if ( pHeader->nId != SCENE_IMAGE_ID || pHeader->nVersion != 3 )
			{
				Error( "CSceneFileCache: Bad scene image file %s\n", "scenes/scenes.image" );
			}
		}
		else
		{
			m_SceneImage.Purge();
		}
	}

	return true;
}

void CTerrorSceneFileCache::Shutdown()
{
	m_SceneImage.Purge();
}

int CTerrorSceneFileCache::FindSceneInImage( const char *pFilename ) const
{
	const SceneImageHeader_t *pHeader = static_cast<const SceneImageHeader_t *>( m_SceneImage.Base() );
	if ( !pHeader )
		return -1;

	char szFilename[260];
	Q_strncpy( szFilename, pFilename, sizeof( szFilename ) );
	Q_strlower( szFilename );
	Q_FixSlashes( szFilename, '\\' );
	Q_SetExtension( szFilename, ".vcd", sizeof( szFilename ) );
	CRC32_t crcFilename = CRC32_ProcessSingleBuffer( szFilename, Q_strlen( szFilename ) );

	const byte *pImage = static_cast<const byte *>( m_SceneImage.Base() );
	const SceneImageEntry_t *pEntries = reinterpret_cast<const SceneImageEntry_t *>( pImage + pHeader->nSceneEntryOffset );
	int iFirst = 1;
	int iLast = pHeader->nNumScenes;
	while ( iFirst <= iLast )
	{
		int iMiddle = ( iFirst + iLast ) / 2;
		CRC32_t crcEntry = pEntries[iMiddle - 1].crcFilename;
		if ( crcFilename < crcEntry )
			iLast = iMiddle - 1;
		else if ( crcFilename > crcEntry )
			iFirst = iMiddle + 1;
		else
			return iMiddle - 1;
	}

	return -1;
}

const SceneImageEntry_t *CTerrorSceneFileCache::GetSceneEntry( int iScene ) const
{
	const SceneImageHeader_t *pHeader = static_cast<const SceneImageHeader_t *>( m_SceneImage.Base() );
	if ( !pHeader || iScene < 0 || iScene >= pHeader->nNumScenes )
		return NULL;

	const byte *pImage = static_cast<const byte *>( m_SceneImage.Base() );
	const SceneImageEntry_t *pEntries = reinterpret_cast<const SceneImageEntry_t *>( pImage + pHeader->nSceneEntryOffset );
	return &pEntries[iScene];
}

size_t CTerrorSceneFileCache::GetSceneBufferSize( const char *pFilename )
{
	const SceneImageEntry_t *pEntry = GetSceneEntry( FindSceneInImage( pFilename ) );
	if ( !pEntry )
		return 0;

	byte *pData = static_cast<byte *>( m_SceneImage.Base() ) + pEntry->nDataOffset;
	if ( CLZMA::IsCompressed( pData ) )
		return CLZMA::GetActualSize( pData );

	return pEntry->nDataLength;
}

bool CTerrorSceneFileCache::GetSceneData( const char *pFilename, byte *pBuffer, size_t nBufferSize )
{
	const SceneImageEntry_t *pEntry = GetSceneEntry( FindSceneInImage( pFilename ) );
	if ( !pEntry )
	{
		if ( pBuffer )
			*pBuffer = 0;
		return false;
	}

	if ( !pBuffer )
		return true;

	// Payload offsets are not necessarily dword aligned in the original image.
	byte *pData = static_cast<byte *>( m_SceneImage.Base() ) + pEntry->nDataOffset;
	if ( CLZMA::IsCompressed( pData ) )
	{
		unsigned int nActualSize = CLZMA::GetActualSize( pData );
		if ( nActualSize > nBufferSize )
		{
			CUtlMemory<byte> uncompressed( 0, nActualSize );
			CLZMA::Uncompress( pData, uncompressed.Base() );
			Q_memcpy( pBuffer, uncompressed.Base(), nBufferSize );
		}
		else
		{
			CLZMA::Uncompress( pData, pBuffer );
		}
	}
	else
	{
		size_t nCopySize = MIN( nBufferSize, static_cast<size_t>( pEntry->nDataLength ) );
		Q_memcpy( pBuffer, pData, nCopySize );
	}

	// The original reports an existing entry even when its decoder fails.
	return true;
}

bool CTerrorSceneFileCache::GetSceneCachedData( const char *pFilename, TerrorSceneCachedData_t *pData )
{
	int iScene = FindSceneInImage( pFilename );
	const SceneImageEntry_t *pEntry = GetSceneEntry( iScene );
	if ( !pEntry )
	{
		pData->sceneId = -1;
		pData->msecs = 0;
		pData->lastSpeakSecs = 0.0f;
		pData->numSounds = 0;
		return false;
	}

	const byte *pImage = static_cast<const byte *>( m_SceneImage.Base() );
	const TerrorSceneImageSummary_t *pSummary = reinterpret_cast<const TerrorSceneImageSummary_t *>( pImage + pEntry->nSceneSummaryOffset );
	pData->sceneId = iScene;
	pData->msecs = pSummary->msecs;
	pData->numSounds = pSummary->numSounds;
	// Original x87 FMUL reads the float32 0.001f constant, not a double 0.001.
	pData->lastSpeakSecs = static_cast<float>( static_cast<double>( pSummary->lastspeech_msecs ) * static_cast<double>( 0.001f ) );
	return true;
}

short CTerrorSceneFileCache::GetSceneCachedSound( int iScene, int iSound )
{
	const SceneImageEntry_t *pEntry = GetSceneEntry( iScene );
	if ( !pEntry )
		return -1;

	const byte *pImage = static_cast<const byte *>( m_SceneImage.Base() );
	const TerrorSceneImageSummary_t *pSummary = reinterpret_cast<const TerrorSceneImageSummary_t *>( pImage + pEntry->nSceneSummaryOffset );
	if ( iSound < 0 || iSound >= pSummary->numSounds )
		return -1;

	return static_cast<short>( pSummary->soundStrings[iSound] );
}

const char *CTerrorSceneFileCache::GetSceneString( short stringId )
{
	const SceneImageHeader_t *pHeader = static_cast<const SceneImageHeader_t *>( m_SceneImage.Base() );
	if ( !pHeader || stringId < 0 || stringId >= pHeader->nNumStrings )
		return NULL;

	const byte *pImage = static_cast<const byte *>( m_SceneImage.Base() );
	const unsigned int *pStringOffsets = reinterpret_cast<const unsigned int *>( pImage + sizeof( SceneImageHeader_t ) );
	return reinterpret_cast<const char *>( pImage + pStringOffsets[stringId] );
}

void CTerrorSceneFileCache::Reload()
{
	Shutdown();
	Init();
}
