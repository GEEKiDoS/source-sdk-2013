//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Consume the mounted L4D2 HUD schemes and encrypted fonts without
//          converted files or a replacement font/scheme in the mod directory.
//
//=============================================================================//

#include "cbase.h"
#include "l4d2/ui/terror_hud_resources.h"
#include "l4d2/ui/terror_memory_font.h"
#include "filesystem.h"
#include "igamesystem.h"
#include "tier1/exprevaluator.h"
#include "tier1/utlbuffer.h"
#include "vstdlib/IKeyValuesSystem.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace
{

// Original client 0x3E01D0: unrecognized names are resolved by the shared
// KeyValues symbol system. In particular, do not invent a resolution threshold
// for WIN32HIDEF or reinterpret X360 resource conditions as desktop conditions.
bool TerrorConditionalSymbol( const char *pName )
{
	if ( *pName == '$' )
		++pName;

	if ( !Q_stricmp( pName, "WIN32" ) )
		return true;
	if ( !Q_stricmp( pName, "OSX" ) || !Q_stricmp( pName, "POSIX" ) ||
		 !Q_stricmp( pName, "LINUX" ) || !Q_stricmp( pName, "X360" ) ||
		 !Q_stricmp( pName, "DEMO" ) )
		return false;

	return KeyValuesSystem()->GetKeyValuesExpressionSymbol( pName );
}

// Resolve only bracket tokens, leaving quoted values and // comments alone.
// The existing tier1 evaluator is the original parenthetic &&/||/! evaluator.
// True tokens disappear; false tokens become the native parser's false [0].
// Both operations shrink the buffer, preserving ordinary KeyValues ownership,
// duplicate-key ordering, and the original numeric/string data.
bool ResolveTerrorConditions( char *pText )
{
	char *pRead = pText;
	char *pWrite = pText;
	bool bQuoted = false;
	bool bComment = false;
	CExpressionEvaluator evaluator;

	while ( *pRead )
	{
		if ( bComment )
		{
			if ( *pRead == '\n' || *pRead == '\r' )
				bComment = false;
			*pWrite++ = *pRead++;
			continue;
		}

		if ( bQuoted )
		{
			if ( *pRead == '\\' && pRead[1] == '"' )
			{
				*pWrite++ = *pRead++;
				*pWrite++ = *pRead++;
				continue;
			}
			if ( *pRead == '"' )
				bQuoted = false;
			*pWrite++ = *pRead++;
			continue;
		}

		if ( *pRead == '/' && pRead[1] == '/' )
		{
			bComment = true;
			*pWrite++ = *pRead++;
			continue;
		}
		if ( *pRead == '"' )
		{
			bQuoted = true;
			*pWrite++ = *pRead++;
			continue;
		}

		if ( *pRead != '[' )
		{
			*pWrite++ = *pRead++;
			continue;
		}

		char *pEnd = Q_strstr( pRead, "]" );
		if ( !pEnd )
			return false;
		char chAfter = pEnd[1];
		pEnd[1] = '\0';
		bool bResult = false;
		bool bValid = evaluator.Evaluate( bResult, pRead, TerrorConditionalSymbol );
		pEnd[1] = chAfter;
		if ( !bValid )
			return false;

		pRead = pEnd + 1;
		if ( !bResult )
		{
			*pWrite++ = '[';
			*pWrite++ = '0';
			*pWrite++ = ']';
		}
	}

	*pWrite = '\0';
	return true;
}

// The count immediately before VFONT1 includes the count byte itself. Salt
// bytes affect only the initial key; each ciphertext byte supplies the next key.
// The selected original fonts all decode to SFNT checksum 0xB1B0AFBA with this
// layout. The decoded buffer is never exposed as a filesystem file.
bool DecodeTerrorFont( CUtlBuffer &data, int &nFontBytes )
{
	const int nBytes = data.TellPut();
	if ( nBytes < 7 )
		return false;

	unsigned char *pData = static_cast<unsigned char *>( data.Base() );
	if ( memcmp( pData + nBytes - 6, "VFONT1", 6 ) )
		return false;

	const int nSaltBytesWithCount = pData[nBytes - 7];
	if ( nSaltBytesWithCount < 1 || nSaltBytesWithCount > nBytes - 6 )
		return false;

	nFontBytes = nBytes - 6 - nSaltBytesWithCount;
	unsigned char key = 0xA7;
	for ( int i = nFontBytes; i < nBytes - 7; ++i )
		key ^= static_cast<unsigned char>( pData[i] + 0xA7 );
	for ( int i = 0; i < nFontBytes; ++i )
	{
		const unsigned char cipher = pData[i];
		pData[i] = cipher ^ key;
		key = static_cast<unsigned char>( cipher + 0xA7 );
	}
	return true;
}

class CTerrorHudResources : public CAutoGameSystem
{
public:
	CTerrorHudResources() : CAutoGameSystem( "TerrorHudResources" ),
		m_bBootstrapPrepared( false ), m_bPrepareAttempted( false ), m_bPrepared( false )
	{
	}
	~CTerrorHudResources()
	{
		// Also cover an early render-target initialization failure before DLL Init.
		Shutdown();
	}
	bool PrepareBootstrap( IFileSystem *pFileSystem );
	bool Prepare();
	virtual void Shutdown() OVERRIDE;

private:
	bool PrepareFile( IFileSystem *pFileSystem, const char *pFilename );
	bool RegisterFont( IFileSystem *pFileSystem, const char *pFilename );

	bool m_bBootstrapPrepared;
	bool m_bPrepareAttempted;
	bool m_bPrepared;
	CUtlVector<CMemoryFileBacking *> m_Files;
	TerrorMemoryFonts_t m_Fonts;
};

bool CTerrorHudResources::RegisterFont( IFileSystem *pFileSystem, const char *pFilename )
{
	CUtlBuffer data;
	if ( !pFileSystem->ReadFile( pFilename, "GAME", data ) )
	{
		Warning( "Unable to read original HUD font %s\n", pFilename );
		return false;
	}
	int nFontBytes = 0;
	if ( !DecodeTerrorFont( data, nFontBytes ) )
	{
		Warning( "Invalid original VFONT resource %s\n", pFilename );
		return false;
	}

	return RegisterTerrorMemoryFont( m_Fonts, pFilename, data.Base(), nFontBytes );
}

bool CTerrorHudResources::PrepareFile( IFileSystem *pFileSystem, const char *pFilename )
{
	FileHandle_t hSource = pFileSystem->Open( pFilename, "rb", "GAME" );
	if ( hSource == FILESYSTEM_INVALID_HANDLE )
	{
		Warning( "Unable to read original HUD resource %s\n", pFilename );
		return false;
	}

	const int nBytes = pFileSystem->Size( hSource );
	CUtlBuffer text;
	text.EnsureCapacity( nBytes + 1 );
	if ( pFileSystem->Read( text.Base(), nBytes, hSource ) != nBytes )
	{
		pFileSystem->Close( hSource );
		return false;
	}
	static_cast<char *>( text.Base() )[nBytes] = '\0';
	if ( !ResolveTerrorConditions( static_cast<char *>( text.Base() ) ) )
	{
		Warning( "Invalid condition in original HUD resource %s\n", pFilename );
		pFileSystem->Close( hSource );
		return false;
	}

	KeyValuesAD resource( "Resource" );
	if ( !resource->LoadFromBuffer( pFilename, static_cast<const char *>( text.Base() ), pFileSystem, "GAME" ) )
	{
		pFileSystem->Close( hSource );
		return false;
	}

	KeyValues *pFonts = resource->FindKey( "CustomFontFiles" );
	if ( pFonts )
	{
		for ( KeyValues *pFont = pFonts->GetFirstSubKey(); pFont; )
		{
			KeyValues *pNext = pFont->GetNextKey();
			const char *pPath = pFont->GetString();
			const char *pExtension = Q_GetFileExtension( pPath );
			if ( pExtension && !Q_stricmp( pExtension, "vfont" ) )
			{
				if ( !RegisterFont( pFileSystem, pPath ) )
				{
					pFileSystem->Close( hSource );
					return false;
				}
				// The target AddCustomFontFile accepts native TTF filenames, not
				// encrypted VFONT data. The real face is now private to this process.
				pFonts->RemoveSubKey( pFont );
				pFont->deleteThis();
			}
			pFont = pNext;
		}
	}

	CUtlBuffer resolved( 0, 0, CUtlBuffer::TEXT_BUFFER );
	resource->RecursiveSaveToFile( resolved, 0, false, true );
	const int nResolvedBytes = resolved.TellPut();
	resolved.PutChar( '\0' );

	CMemoryFileBacking *pFile = new CMemoryFileBacking( pFileSystem );
	// Target filesystem_stdio OpenEx RVA0xB850 canonicalizes relative keys to
	// lowercase backslashes before its byte-exact memory-file lookup0x2200.
	// RegisterMemoryFile0xD800 does not canonicalize the supplied key itself.
	char szMemoryName[MAX_PATH];
	Q_strncpy( szMemoryName, pFilename, sizeof( szMemoryName ) );
	Q_FixSlashes( szMemoryName, '\\' );
	Q_strlower( szMemoryName );
	pFile->m_pFileName = strdup( szMemoryName );
	pFile->m_nLength = nResolvedBytes;
	pFile->m_pData = static_cast<char *>( pFileSystem->AllocOptimalReadBuffer( hSource, nResolvedBytes + 1 ) );
	pFileSystem->Close( hSource );
	if ( !pFile->m_pData )
	{
		pFile->Release();
		return false;
	}
	memcpy( const_cast<char *>( pFile->m_pData ), resolved.Base(), nResolvedBytes + 1 );

	CMemoryFileBacking *pExisting = NULL;
	if ( !pFileSystem->RegisterMemoryFile( pFile, &pExisting ) )
	{
		// RegisterMemoryFile ref-bumps an existing registration on collision;
		// balance that registration without replacing someone else's resource.
		if ( pExisting )
			pFileSystem->UnregisterMemoryFile( pExisting );
		pFile->Release();
		Warning( "Original HUD resource already memory-registered: %s\n", pFilename );
		return false;
	}
	m_Files.AddToTail( pFile );
	KeyValuesSystem()->InvalidateCacheForFile( pFilename, "GAME" );
	KeyValuesSystem()->InvalidateCacheForFile( pFilename, NULL );
	// Schemes and panel resources use the native Open/Read path. Registration
	// alone is not proof that that consumer will see the prepared bytes.
	FileHandle_t hPrepared = pFileSystem->Open( pFilename, "rb", "GAME" );
	if ( hPrepared == FILESYSTEM_INVALID_HANDLE )
	{
		Warning( "Unable to reopen prepared original HUD resource %s\n", pFilename );
		return false;
	}
	CUtlBuffer check;
	check.EnsureCapacity( nResolvedBytes );
	const bool bPrepared = pFileSystem->Size( hPrepared ) == nResolvedBytes &&
		pFileSystem->Read( check.Base(), nResolvedBytes, hPrepared ) == nResolvedBytes &&
		!memcmp( check.Base(), resolved.Base(), nResolvedBytes );
	pFileSystem->Close( hPrepared );
	if ( !bPrepared )
	{
		Warning( "Native filesystem did not select prepared original HUD resource %s\n", pFilename );
		return false;
	}
	DevMsg( "Original HUD resource prepared and native Open verified: %s\n", pFilename );
	return true;
}

bool CTerrorHudResources::PrepareBootstrap( IFileSystem *pFileSystem )
{
	if ( m_bBootstrapPrepared )
	{
		return true;
	}

	if ( !InstallTerrorMemoryFontBridge( m_Fonts ) ||
		!PrepareFile( pFileSystem, "resource/sourcescheme.res" ) )
	{
		// This call has not returned to the first EngineVGUI scheme consumer.
		Shutdown();
		return false;
	}

	// Render-target resets reuse the early scheme until actual client shutdown.
	m_bBootstrapPrepared = true;
	return true;
}

bool CTerrorHudResources::Prepare()
{
	if ( m_bPrepareAttempted )
	{
		return m_bPrepared;
	}

	if ( !PrepareBootstrap( filesystem ) )
	{
		Warning( "Unable to prepare original HUD native memory-font consumer\n" );
		return false;
	}
	m_bPrepareAttempted = true;

	// Default scheme precedes ClientScheme, matching native scheme ownership.
	// Original panels use the same lifecycle-owned native-expression adapter.
	// Legacy cl_create_server loads are lazy and nonfatal in the original.
	// Its unshipped CreateServer.res and MissionPanel.res are not prerequisites.
	static const char *s_Files[] =
	{
		// SourceScheme and its four private fonts were prepared before EngineVGUI.
		"resource/clientscheme.res",
		"resource/l4d360uischeme.res",
		"scripts/hudlayout.res",
		"resource/ui/hud/localplayerpanel.res",
		"resource/ui/hud/teammatepanel.res",
		"resource/ui/transitionstatssurvivor.res",
		"resource/ui/transitionstatssurvivorhighlight.res",
		"resource/ui/tippanel.res",
		"resource/ui/zombieintropanel.res",
		"resource/ui/versusmoderesults.res",
		"resource/ui/personalstatsummary.res",
		"resource/chatscheme.res",
		"resource/ui/textwindow.res",
		"resource/ui/minimotd.res",
		"resource/ui/hud/buildablecostpanel.res",
		"resource/ui/locator.res",
		"resource/ui/pzendgamepanel.res",
		"resource/ui/takeoversurvivorbar.res",
		"resource/ui/radialmenu.res",
		"resource/ui/spectator.res",
		"resource/ui/spectatorsurvivor.res",
		"resource/ui/spectatorinfected.res",
		"resource/ui/fullscreenversusmodescoreboard.res",
		"resource/ui/fullscreenscavengemodescoreboard.res",
		"resource/ui/fullscreensurvivalmodescoreboard.res",
		"resource/ui/hud/abilitytimerhud.res",
		"resource/ui/hud/boomerhealth.res",
		"resource/ui/hud/chargerhealth.res",
		"resource/ui/hud/finalemeter.res",
		"resource/ui/hud/frustrationmeter.res",
		"resource/ui/hud/hudscavengetimer.res",
		"resource/ui/hud/hudscriptedmode.res",
		"resource/ui/hud/hudsurvivaltimer.res",
		"resource/ui/hud/hunterhealth.res",
		"resource/ui/hud/infectedvoip.res",
		"resource/ui/hud/jockeyhealth.res",
		"resource/ui/hud/smokerhealth.res",
		"resource/ui/hud/spitterhealth.res",
		"resource/ui/hud/targetid.res",
		"resource/ui/hud/zombieteamdisplayplayer.res",
		"resource/ui/hudghostpanel.res",
		"resource/ui/leavingareawarning.res",
		"resource/ui/readycountdown.res",
		"resource/ui/scavengemodeembeddedscoreboard.res",
		"resource/ui/scoreboard.res",
		"resource/ui/scoreboardinfectedplayer.res",
		"resource/ui/scoreboardsurvivor.res",
		"resource/ui/versusmodeembeddedscoreboard.res",
		"resource/ui/zombiepanel.res",
	};
	for ( int i = 0; i < ARRAYSIZE( s_Files ); ++i )
	{
		if ( !PrepareFile( filesystem, s_Files[i] ) )
		{
			// Keep already consumed resources valid until client shutdown.
			return false;
		}
	}

	m_bPrepared = true;
	// Keep bootstrap scheme borders alive for the engine's existing GameUI.
	// The viewport loads the prepared ClientScheme normally after this returns.
	return true;
}

void CTerrorHudResources::Shutdown()
{
	// Detach the callback before any client-owned registry or backing is released.
	ShutdownTerrorMemoryFonts( m_Fonts );
	for ( int i = m_Files.Count() - 1; i >= 0; --i )
	{
		m_Files[i]->m_pFS->UnregisterMemoryFile( m_Files[i] );
		m_Files[i]->Release();
	}
	m_Files.Purge();
	m_bPrepared = false;
	m_bBootstrapPrepared = false;
	m_bPrepareAttempted = false;
}

CTerrorHudResources g_TerrorHudResources;

} // namespace

bool PrepareTerrorHudBootstrap( IFileSystem *pFileSystem )
{
	return g_TerrorHudResources.PrepareBootstrap( pFileSystem );
}

bool PrepareTerrorHudResources()
{
	return g_TerrorHudResources.Prepare();
}

bool LoadTerrorUiResource( KeyValues *resource, const char *filename )
{
	CUtlBuffer text( 0, 0, CUtlBuffer::TEXT_BUFFER );
	if ( !filesystem->ReadFile( filename, "GAME", text ) )
		return false;
	text.PutChar( '\0' );
	return ResolveTerrorConditions( static_cast<char *>( text.Base() ) ) &&
		resource->LoadFromBuffer( filename, static_cast<char *>( text.Base() ), filesystem, "GAME" );
}
