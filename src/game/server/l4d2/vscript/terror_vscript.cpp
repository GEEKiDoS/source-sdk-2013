//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "vscript_shared.h"
#include "filesystem.h"
#include "util_shared.h"
#include "tier1/utlbuffer.h"
#include "tier0/memdbgon.h"

bool g_bTerrorScriptFileMissing = false;
// S:221E10 ->20CF90. The encrypted file is encrypted source, NOT Squirrel
// bytecode. Decode in memory with the native ICE helper; never rewrite assets.
HSCRIPT VScriptCompileScript( const char *name, bool warnMissing )
{
	g_bTerrorScriptFileMissing = false;
	if ( !g_pScriptVM )
		return NULL;
	static const char *plainExtensions[] = { "", ".gm", ".nut", ".lua", ".py" };
	static const char *encryptedExtensions[] = { "", ".gmc", ".nuc", "", "" };
	const int language = g_pScriptVM->GetLanguage();
	const char *extension = V_strrchr( name, '.' );
	if ( extension && V_stricmp( extension, plainExtensions[language] ) &&
		V_stricmp( extension, encryptedExtensions[language] ) )
	{
		Msg( "Script file type does not match VM type\n" );
		return NULL;
	}
	char plainPath[MAX_PATH], encryptedPath[MAX_PATH];
	if ( V_IsAbsolutePath( name ) )
	{
		Q_strncpy( plainPath, name, sizeof( plainPath ) );
		Q_strncpy( encryptedPath, name, sizeof( encryptedPath ) );
	}
	else if ( extension )
	{
		Q_snprintf( plainPath, sizeof( plainPath ), "scripts/vscripts/%s", name );
		Q_strncpy( encryptedPath, plainPath, sizeof( encryptedPath ) );
	}
	else
	{
		Q_snprintf( plainPath, sizeof( plainPath ), "scripts/vscripts/%s%s", name, plainExtensions[language] );
		Q_snprintf( encryptedPath, sizeof( encryptedPath ), "scripts/vscripts/%s%s", name, encryptedExtensions[language] );
	}
	CUtlBuffer script;
	// S:221E10 only enables decoding after the fallback read succeeds.
	// Preserve the native explicit-.nuc first-read bug.
	bool encrypted = false;
	const char *path = plainPath;
	if ( !filesystem->ReadFile( plainPath, "GAME", script ) )
	{
		if ( !filesystem->ReadFile( encryptedPath, "GAME", script ) )
		{
			g_bTerrorScriptFileMissing = true;
			if ( warnMissing )
				DevMsg( "Script not found (%s) \n", name );
			return NULL;
		}
		encrypted = true;
		path = encryptedPath;
	}
	const int length = script.TellPut();
	if ( !length )
	{
		g_bTerrorScriptFileMissing = true;
		return NULL;
	}
	if ( encrypted )
		UTIL_DecodeICE( static_cast<unsigned char *>( script.Base() ), length, reinterpret_cast<const unsigned char *>( "SDhfi878" ) );
	// CUtlBuffer retains its native trailing NUL; ICE changes only complete8-byte
	// blocks, and preserves the unencrypted tail (including a source terminator).
	const char *source = static_cast<const char *>( script.Base() );
	if ( !source || !*source )
		return NULL;
	char filename[MAX_PATH];
	Q_strncpy( filename, path, sizeof( filename ) );
	V_FixSlashes( filename, '/' );
	HSCRIPT compiled = g_pScriptVM->CompileScript( source, filename );
	if ( !compiled )
		DevMsg( "FAILED to compile and execute script file named %s\n", name );
	return compiled;
}
