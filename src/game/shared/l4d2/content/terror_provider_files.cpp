//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/content/terror_provider_files.h"
#include "filesystem.h"
#include "tier1/utlbuffer.h"
#ifndef CLIENT_DLL
#include "vscript_shared.h"
#endif
#include "tier0/memdbgon.h"

static bool TerrorIsWorkshopAddon( const char *name )
{
	char normalized[MAX_PATH];
	Q_strncpy( normalized, name, sizeof( normalized ) );
	V_FixSlashes( normalized, '\\' );
	return !Q_strnicmp( normalized, "workshop\\", 9 );
}

static bool TerrorHasAddon( const CUtlVector< CUtlString > &names, const char *name )
{
	FOR_EACH_VEC( names, i )
		if ( !Q_strcmp( names[i].String(), name ) )
			return true;
	return false;
}

static KeyValues *TerrorReadAddonList( const char *filename, bool dedicated )
{
	// E17E2B0 reads the physical MOD file, never a mounted GAME override.
	CUtlBuffer contents;
	if ( !filesystem->ReadFile( filename, NULL, contents ) )
		return NULL;
	contents.PutChar( 0 );
	KeyValues *list = new KeyValues( "" );
	if ( !list->LoadFromBuffer( filename, static_cast< const char * >( contents.Base() ) ) )
	{
		list->deleteThis();
		return NULL;
	}
	// E17B4F0 with no UGC: Clear, not remove or SetInt(0). The cleared node
	// remains an empty true subkey and is not visited by GetFirstValue.
	if ( !dedicated )
		for ( KeyValues *entry = list->GetFirstValue(); entry; entry = entry->GetNextValue() )
			if ( entry->GetInt() && TerrorIsWorkshopAddon( entry->GetName() ) )
				entry->Clear();
	return list;
}

void TerrorMountAddons()
{
	char modPaths[MAX_PATH];
	filesystem->GetSearchPath( "MOD", false, modPaths, sizeof( modPaths ) );
	const char *mod = V_strrchr( modPaths, ';' );
	mod = mod ? mod + 1 : modPaths; // E17D550 selects the last physical MOD path.
	char listPath[MAX_PATH], pattern[MAX_PATH];
	Q_snprintf( listPath, sizeof( listPath ), "%saddonlist.txt", mod );
	// E17CEB0 removes the old add-on mounts before rebuilding the list. Both
	// DLLs share the filesystem, so enumerate it rather than keeping a DLL-local
	// list which would miss paths installed by the other game module.
	const int length = filesystem->GetSearchPath( "GAME", true, NULL, 0 );
	CUtlVector< char > paths;
	paths.SetCount( length );
	filesystem->GetSearchPath( "GAME", true, paths.Base(), paths.Count() );
	char addons[MAX_PATH];
	Q_snprintf( addons, sizeof( addons ), "%saddons", mod );
	V_FixSlashes( addons, '\\' );
	for ( char *path = paths.Base(); *path; )
	{
		char *next = V_strchr( path, ';' );
		if ( next ) *next++ = 0;
		char normalized[MAX_PATH];
		Q_strncpy( normalized, path, sizeof( normalized ) );
		V_FixSlashes( normalized, '\\' );
		if ( V_stristr( normalized, addons ) )
			filesystem->RemoveSearchPath( path, "GAME" );
		if ( !next ) break;
		path = next;
	}
	bool dedicated = false;
#ifndef CLIENT_DLL
	dedicated = engine->IsDedicatedServer();
#endif
	KeyValues *list = TerrorReadAddonList( listPath, dedicated );
	if ( !list )
		list = new KeyValues( "AddonList" );

	CUtlVector< CUtlString > archives, directories;
	FileFindHandle_t find = FILESYSTEM_INVALID_FIND_HANDLE;
	Q_snprintf( pattern, sizeof( pattern ), "%saddons\\*.vpk", mod );
	for ( const char *name = filesystem->FindFirst( pattern, &find ); name; name = filesystem->FindNext( find ) )
	{
		char normalized[MAX_PATH];
		Q_strncpy( normalized, name, sizeof( normalized ) );
		V_FixSlashes( normalized, '\\' );
		archives.AddToTail( normalized );
	}
	filesystem->FindClose( find );
	Q_snprintf( pattern, sizeof( pattern ), "%saddons\\*.*", mod );
	for ( const char *name = filesystem->FindFirst( pattern, &find ); name; name = filesystem->FindNext( find ) )
	{
		if ( !filesystem->FindIsDirectory( find ) || *name == '.' || !Q_stricmp( name, "workshop" ) )
			continue;
		char archive[MAX_PATH];
		Q_snprintf( archive, sizeof( archive ), "%s.vpk", name );
		if ( TerrorHasAddon( archives, archive ) )
			continue; // A same-name VPK suppresses the loose add-on directory.
		char info[MAX_PATH];
		Q_snprintf( info, sizeof( info ), "%saddons\\%s\\addoninfo.txt", mod, name );
		FileFindHandle_t infoFind = FILESYSTEM_INVALID_FIND_HANDLE;
		if ( filesystem->FindFirst( info, &infoFind ) )
			directories.AddToTail( name );
		filesystem->FindClose( infoFind );
	}
	filesystem->FindClose( find );
	// Native dedicated/no-UGC fallback is physical files, not subscriptions.
	if ( dedicated )
	{
		Q_snprintf( pattern, sizeof( pattern ), "%saddons\\workshop\\*.vpk", mod );
		for ( const char *name = filesystem->FindFirst( pattern, &find ); name; name = filesystem->FindNext( find ) )
		{
			char relative[MAX_PATH];
			Q_snprintf( relative, sizeof( relative ), "workshop\\%s", name );
			archives.AddToTail( relative );
		}
		filesystem->FindClose( find );
	}
	// E17C210 appends directories before archives; existing list order survives.
	FOR_EACH_VEC( directories, i )
		if ( !list->FindKey( directories[i].String() ) )
			list->SetInt( directories[i].String(), 1 );
	FOR_EACH_VEC( archives, i )
		if ( !list->FindKey( archives[i].String() ) )
			list->SetInt( archives[i].String(), 1 );
	for ( KeyValues *entry = list->GetFirstSubKey(); entry; )
	{
		KeyValues *next = entry->GetNextKey();
		const char *name = entry->GetName();
		if ( !TerrorHasAddon( directories, name ) && !TerrorHasAddon( archives, name ) &&
			( dedicated || !TerrorIsWorkshopAddon( name ) ) )
		{
			list->RemoveSubKey( entry );
			entry->deleteThis();
		}
		entry = next;
	}
	if ( list->GetFirstSubKey() )
		list->SaveToFile( filesystem, listPath, NULL );
	else if ( filesystem->FileExists( listPath, NULL ) )
		filesystem->RemoveFile( listPath, NULL );
	list->deleteThis();

	// E17B640 re-reads what was actually saved. Failed writes are not bypassed.
	list = TerrorReadAddonList( listPath, dedicated );
	if ( !list )
		return;
	for ( KeyValues *entry = list->GetFirstValue(); entry; entry = entry->GetNextValue() )
	{
		if ( !entry->GetInt() )
			continue;
		char path[MAX_PATH];
		Q_snprintf( path, sizeof( path ), "%saddons\\%s", mod, entry->GetName() );
		// Main-approved engine/API adaptation: ordinary public mounting for
		// both .vpk archives and loose directories, preserving head insertion.
		filesystem->AddSearchPath( path, "GAME", PATH_ADD_TO_HEAD );
	}
	list->deleteThis();
}

void TerrorFindProviderFiles( const char *pattern, CUtlVector< CUtlString > &files )
{
	CUtlVector< CUtlString > names;
	FileFindHandle_t find = FILESYSTEM_INVALID_FIND_HANDLE;
	for ( const char *name = filesystem->FindFirstEx( pattern, "GAME", &find ); name; name = filesystem->FindNext( find ) )
	{
		if ( !filesystem->FindIsDirectory( find ) )
			names.AddToTail( name );
	}
	filesystem->FindClose( find );

	// FindFirstEx supplies the engine's mounted-member order but folds duplicate
	// names. Re-expand those names against the real ordered GAME search paths.
	// Addon scripts and custom modes load each absolute payload. Missions load
	// once by relative filename; their attempted-file registry folds repeats.
	int length = filesystem->GetSearchPath( "GAME", true, NULL, 0 );
	CUtlVector< char > paths;
	paths.SetCount( length );
	filesystem->GetSearchPath( "GAME", true, paths.Base(), paths.Count() );
	char directory[MAX_PATH];
	Q_strncpy( directory, pattern, sizeof( directory ) );
	V_StripFilename( directory );
	for ( char *path = paths.Base(); *path; )
	{
		char *next = V_strchr( path, ';' );
		if ( next ) *next++ = 0;
		FOR_EACH_VEC( names, i )
		{
			char relative[MAX_PATH], absolute[MAX_PATH];
			V_ComposeFileName( directory, names[i].String(), relative, sizeof( relative ) );
			V_ComposeFileName( path, relative, absolute, sizeof( absolute ) );
			V_FixSlashes( absolute, '\\' );
			// Target CBaseFileSystem::Open explicitly recognizes mounted
			// absolute .vpk\, .bsp\ and .zip\ member paths (18000BCB0).
			// Do not replace this with the OS-only absolute FindFirst path.
			FileHandle_t file = filesystem->Open( absolute, "rb", NULL );
			if ( file != FILESYSTEM_INVALID_HANDLE )
			{
				filesystem->Close( file );
				files.AddToTail( absolute );
			}
		}
		if ( !next ) break;
		path = next;
	}
}

#ifndef CLIENT_DLL
void VScriptRunTerrorAddonScripts( const char *name, HSCRIPT scope, bool warnMissing )
{
	char pattern[MAX_PATH];
	Q_snprintf( pattern, sizeof( pattern ), "scripts/vscripts/%s_addon.nut", name );
	CUtlVector< CUtlString > files;
	TerrorFindProviderFiles( pattern, files );
	FOR_EACH_VEC( files, i )
	{
		Msg( ">>> Loading addon script %s\n", files[i].String() );
		VScriptRunScript( files[i].String(), scope, warnMissing );
	}
}
#endif
