//========= Copyright Valve Corporation, All rights reserved. ============//
// VRAD ReSTIR launcher. The DLL name is intentionally fixed; vrad.redirect
// is not supported by this tool.

#include "stdafx.h"
#include <direct.h>
#include "tier1/strtools.h"
#include "tier0/icommandline.h"

static char *GetLastErrorString()
{
	static char err[2048];
	LPVOID pMessage = NULL;
	FormatMessage( FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, GetLastError(), MAKELANGID( LANG_NEUTRAL, SUBLANG_DEFAULT ), (LPTSTR)&pMessage, 0, NULL );
	if ( pMessage )
	{
		Q_strncpy( err, (const char *)pMessage, sizeof( err ) );
		LocalFree( pMessage );
	}
	else
	{
		Q_strncpy( err, "unknown system error", sizeof( err ) );
	}
	err[ sizeof( err ) - 1 ] = 0;
	return err;
}

int main( int argc, char *argv[] )
{
	const char *pDLLName = "vrad_restir_dll.dll";
	CommandLine()->CreateCmdLine( argc, argv );

	// Retain the original launcher contract: -both is replaced in-place for
	// pass 0 and pass 1, and each pass loads/unloads the DLL independently.
	int both_arg = 0;
	for ( int arg = 1; arg < argc; ++arg )
	{
		if ( Q_stricmp( argv[arg], "-both" ) == 0 )
			both_arg = arg;
	}

	int returnValue = 0;
	for ( int mode = 0; mode < 2; ++mode )
	{
		if ( mode && !both_arg )
			continue;

		CSysModule *pModule = Sys_LoadModule( pDLLName );
		if ( !pModule )
		{
			printf( "vrad_restir_launcher error: can't load %s\n%s", pDLLName, GetLastErrorString() );
			return 1;
		}

		CreateInterfaceFn fn = Sys_GetFactory( pModule );
		if ( !fn )
		{
			printf( "vrad_restir_launcher error: can't get factory from %s\n", pDLLName );
			Sys_UnloadModule( pModule );
			return 2;
		}

		int retCode = 0;
		IVRadDLL *pDLL = (IVRadDLL *)fn( VRAD_INTERFACE_VERSION, &retCode );
		if ( !pDLL )
		{
			printf( "vrad_restir_launcher error: can't get IVRadDLL interface from %s\n", pDLLName );
			Sys_UnloadModule( pModule );
			return 3;
		}

		// argv strings are contiguous; "-both" is exactly 5 characters, so write only
		// that many (vrad_launcher.cpp:138 strcpy of a 4-character token is also safe).
		if ( both_arg )
			Q_strncpy( argv[both_arg], mode ? "-hdr" : "-ldr", 6 );
		returnValue = pDLL->main( argc, argv );
		Sys_UnloadModule( pModule );
	}
	return returnValue;
}
