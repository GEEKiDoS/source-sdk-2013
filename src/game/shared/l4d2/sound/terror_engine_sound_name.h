//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_ENGINE_SOUND_NAME_H
#define TERROR_ENGINE_SOUND_NAME_H
#ifdef _WIN32
#pragma once
#endif

#include "engine/IEngineSound.h"
#include "sharedInterface.h"

#if defined( L4D2 )
#include "soundchars.h"
#include "tier1/utlmemory.h"
#if !defined( CLIENT_DLL )
#include "networkstringtable_gamedll.h"
#endif
#endif

// Keep native names in the game (especially '$' caption policy). Adapt only the
// sample passed synchronously to the pinned SDK engine, including stop/precache
// and duration queries so they use the same name as playback. The temporary must
// live through the engine call; never retain Get() beyond this object's lifetime.
// Other games pass the exact original pointer without scanning or copying.
class CTerrorEngineSoundName
{
public:
	explicit CTerrorEngineSoundName( const char *pSample ) : m_pName( pSample )
	{
#if defined( L4D2 )
		if ( !m_pName )
			return;

		// '(' is native directional stereo (not SDK spatial stereo ')'). '$'
		// controls game-owned subtitles. Neither has an equivalent SDK marker.
		// Main admits ordinary playback rather than silence; directional-stereo
		// mixing/attenuation is not reproduced. Do not replace '(' with ')'/'@'.
		while ( IsUnsupportedMarker( *m_pName ) )
			++m_pName;

		// Native engine101DD630 hashes case-folded, slash-normalized names.
		// The SDK sound dictionary folds case but distinguishes separators.
		// Canonicalize every engine boundary, not only precache, so emission,
		// stops and duration queries refer to the same sample-table entry.
		int nRemoved = 0;
		bool bChanged = false;
		bool bPrefix = true;
		const char *pEnd = m_pName;
		for ( ; *pEnd; ++pEnd )
		{
			bPrefix = bPrefix && IsSoundChar( *pEnd );
			if ( bPrefix && IsUnsupportedMarker( *pEnd ) )
				++nRemoved;
			else if ( *pEnd == '\\' || ( *pEnd >= 'A' && *pEnd <= 'Z' ) )
				bChanged = true;
		}
		if ( !nRemoved && !bChanged )
			return;

		// Size the normalized output: long marker chains can still fit on the
		// stack, and genuinely long paths allocate their final storage once.
		const int nOutputBytes = static_cast< int >( pEnd - m_pName ) - nRemoved + 1;
		char *pAdapted = m_buffer;
		if ( nOutputBytes > static_cast< int >( sizeof( m_buffer ) ) )
		{
			m_overflow.EnsureCapacity( nOutputBytes );
			pAdapted = m_overflow.Base();
		}
		char *pOutput = pAdapted;
		bPrefix = true;
		for ( const char *p = m_pName; p != pEnd; ++p )
		{
			const char c = *p;
			bPrefix = bPrefix && IsSoundChar( c );
			if ( bPrefix && IsUnsupportedMarker( c ) )
				continue;
			*pOutput++ = c == '\\' ? '/' : ( c >= 'A' && c <= 'Z' ) ? static_cast< char >( c + ( 'a' - 'A' ) ) : c;
		}
		*pOutput = '\0';
		m_pName = pAdapted;
#endif
	}

	const char *Get() const { return m_pName; }

	bool IsPrecached() const
	{
#if defined( L4D2 ) && !defined( CLIENT_DLL )
		if ( TestSoundChar( m_pName, CHAR_SENTENCE ) )
			return true;

		// SDK engine1800185C0 tests SV_SoundIndex against -1, but
		// engine180143630 maps a missing sound to 0. Query the real table.
		INetworkStringTable *pTable = networkstringtable->FindTable( "soundprecache" );
		return pTable && pTable->FindStringIndex( m_pName ) != INVALID_STRING_INDEX;
#else
		return enginesound->IsSoundPrecached( m_pName );
#endif
	}

private:
	CTerrorEngineSoundName( const CTerrorEngineSoundName & );
	CTerrorEngineSoundName &operator=( const CTerrorEngineSoundName & );

#if defined( L4D2 )
	static bool IsUnsupportedMarker( char c ) { return c == '(' || c == '$'; }
	char m_buffer[256];
	CUtlMemory< char > m_overflow;
#endif
	const char *m_pName;
};

#endif // TERROR_ENGINE_SOUND_NAME_H
