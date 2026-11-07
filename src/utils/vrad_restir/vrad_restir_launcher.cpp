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

	// The DLL owns both-mode execution: it can also discover converted inputs after loading the BSP.
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

	const int returnValue = pDLL->main( argc, argv );
	Sys_UnloadModule( pModule );
	return returnValue;
}
