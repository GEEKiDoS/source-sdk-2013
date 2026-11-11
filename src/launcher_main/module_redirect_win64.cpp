//========= Copyright Valve Corporation, All rights reserved. ============//

#if !defined( _WIN64 )
#error This translation unit is only valid for the Win64 launcher.
#endif

#include "module_redirect_win64.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include <intrin.h>

#include "../thirdparty/minhook/include/MinHook.h"

namespace
{
typedef HMODULE ( WINAPI *LoadLibraryA_t )( LPCSTR );
typedef HMODULE ( WINAPI *LoadLibraryW_t )( LPCWSTR );
typedef HMODULE ( WINAPI *LoadLibraryExA_t )( LPCSTR, HANDLE, DWORD );
typedef HMODULE ( WINAPI *LoadLibraryExW_t )( LPCWSTR, HANDLE, DWORD );

static LoadLibraryA_t s_LoadLibraryA = NULL;
static LoadLibraryW_t s_LoadLibraryW = NULL;
static LoadLibraryExA_t s_LoadLibraryExA = NULL;
static LoadLibraryExW_t s_LoadLibraryExW = NULL;

struct ModuleRedirect
{
	wchar_t baseName[64];			// lower case, without extension
	wchar_t replacement[MAX_PATH];
};
static ModuleRedirect s_Redirects[4];
static int s_nRedirects = 0;
static bool s_bHooksInstalled = false;
// Set while a load requested by a replacement module runs: the original loader's nested LoadLibrary calls on
// this thread must pass through too, so a replacement can load the module it replaces.
static thread_local bool s_bReplacementLoad = false;

static wchar_t Lower( wchar_t c )
{
	return ( c >= L'A' && c <= L'Z' ) ? (wchar_t)( c + ( L'a' - L'A' ) ) : c;
}

// The base name of pName (directories and a trailing ".dll" removed) equals pBaseName, case-insensitively.
static bool BaseNameMatches( const wchar_t *pName, const wchar_t *pBaseName )
{
	const wchar_t *pBase = pName;
	for ( const wchar_t *p = pName; *p; ++p )
	{
		if ( *p == L'\\' || *p == L'/' )
			pBase = p + 1;
	}
	size_t length = wcslen( pBase );
	if ( length > 4 && _wcsicmp( pBase + length - 4, L".dll" ) == 0 )
		length -= 4;
	if ( length != wcslen( pBaseName ) )
		return false;
	for ( size_t i = 0; i < length; ++i )
	{
		if ( Lower( pBase[i] ) != pBaseName[i] )
			return false;
	}
	return true;
}

static bool SamePath( const wchar_t *pA, const wchar_t *pB )
{
	wchar_t fullA[MAX_PATH], fullB[MAX_PATH];
	if ( !GetFullPathNameW( pA, MAX_PATH, fullA, NULL ) || !GetFullPathNameW( pB, MAX_PATH, fullB, NULL ) )
		return false;
	return _wcsicmp( fullA, fullB ) == 0;
}

// True when pCaller (a return address) lies in one of the loaded replacement modules.
static bool CalledFromReplacement( void *pCaller )
{
	HMODULE caller = NULL;
	if ( !GetModuleHandleExW( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		static_cast<LPCWSTR>( pCaller ), &caller ) )
		return false;
	wchar_t callerPath[MAX_PATH];
	if ( !GetModuleFileNameW( caller, callerPath, MAX_PATH ) )
		return false;
	for ( int i = 0; i < s_nRedirects; ++i )
	{
		if ( SamePath( callerPath, s_Redirects[i].replacement ) )
			return true;
	}
	return false;
}

static const wchar_t *FindReplacementW( LPCWSTR pName )
{
	if ( !pName || s_bReplacementLoad )
		return NULL;
	for ( int i = 0; i < s_nRedirects; ++i )
	{
		if ( BaseNameMatches( pName, s_Redirects[i].baseName ) )
			return s_Redirects[i].replacement;
	}
	return NULL;
}

static const wchar_t *FindReplacementA( LPCSTR pName )
{
	if ( !pName || s_bReplacementLoad )
		return NULL;
	wchar_t wide[MAX_PATH];
	if ( !MultiByteToWideChar( CP_ACP, 0, pName, -1, wide, MAX_PATH ) )
		return NULL;
	return FindReplacementW( wide );
}

static bool IsResourceLoad( DWORD flags )
{
	return ( flags & ( LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE | LOAD_LIBRARY_AS_IMAGE_RESOURCE ) ) != 0;
}

static HMODULE LoadReplacement( const wchar_t *pReplacement, HANDLE hFile, DWORD flags )
{
	HMODULE result = s_LoadLibraryExW( pReplacement, hFile, flags );
	const DWORD error = GetLastError();
	SetLastError( error );
	return result;
}

// Runs an original loader call; when the replacement module itself asked for the load, nested hook entries on this
// thread pass through as well.
template <typename Load>
static HMODULE PassThrough( void *pCaller, Load load )
{
	if ( s_bReplacementLoad || !CalledFromReplacement( pCaller ) )
		return load();
	s_bReplacementLoad = true;
	HMODULE result = load();
	const DWORD error = GetLastError();
	s_bReplacementLoad = false;
	SetLastError( error );
	return result;
}

static HMODULE WINAPI RedirectLoadLibraryA( LPCSTR pName )
{
	void *pCaller = _ReturnAddress();
	const wchar_t *pReplacement = CalledFromReplacement( pCaller ) ? NULL : FindReplacementA( pName );
	return pReplacement ? LoadReplacement( pReplacement, NULL, 0 ) : PassThrough( pCaller, [&] { return s_LoadLibraryA( pName ); } );
}

static HMODULE WINAPI RedirectLoadLibraryW( LPCWSTR pName )
{
	void *pCaller = _ReturnAddress();
	const wchar_t *pReplacement = CalledFromReplacement( pCaller ) ? NULL : FindReplacementW( pName );
	return pReplacement ? LoadReplacement( pReplacement, NULL, 0 ) : PassThrough( pCaller, [&] { return s_LoadLibraryW( pName ); } );
}

static HMODULE WINAPI RedirectLoadLibraryExA( LPCSTR pName, HANDLE hFile, DWORD flags )
{
	void *pCaller = _ReturnAddress();
	const wchar_t *pReplacement = IsResourceLoad( flags ) || CalledFromReplacement( pCaller ) ? NULL : FindReplacementA( pName );
	return pReplacement ? LoadReplacement( pReplacement, hFile, flags ) :
		PassThrough( pCaller, [&] { return s_LoadLibraryExA( pName, hFile, flags ); } );
}

static HMODULE WINAPI RedirectLoadLibraryExW( LPCWSTR pName, HANDLE hFile, DWORD flags )
{
	void *pCaller = _ReturnAddress();
	const wchar_t *pReplacement = IsResourceLoad( flags ) || CalledFromReplacement( pCaller ) ? NULL : FindReplacementW( pName );
	return pReplacement ? LoadReplacement( pReplacement, hFile, flags ) :
		PassThrough( pCaller, [&] { return s_LoadLibraryExW( pName, hFile, flags ); } );
}

static void SetError( wchar_t *pError, size_t errorChars, const wchar_t *pFormat, unsigned int status )
{
	if ( !pError || errorChars == 0 )
		return;
	_snwprintf_s( pError, errorChars, _TRUNCATE, pFormat, status );
}

static void SetTextError( wchar_t *pError, size_t errorChars, const wchar_t *pText )
{
	if ( !pError || errorChars == 0 )
		return;
	wcsncpy_s( pError, errorChars, pText, _TRUNCATE );
}

static void RemoveHooks( const void *pLoadLibraryA, bool bLoadLibraryA, const void *pLoadLibraryW, bool bLoadLibraryW,
	const void *pLoadLibraryExA, bool bLoadLibraryExA, const void *pLoadLibraryExW, bool bLoadLibraryExW )
{
	if ( bLoadLibraryA )
		MH_RemoveHook( const_cast<void *>( pLoadLibraryA ) );
	if ( bLoadLibraryW )
		MH_RemoveHook( const_cast<void *>( pLoadLibraryW ) );
	if ( bLoadLibraryExA )
		MH_RemoveHook( const_cast<void *>( pLoadLibraryExA ) );
	if ( bLoadLibraryExW )
		MH_RemoveHook( const_cast<void *>( pLoadLibraryExW ) );
}

// Keep the launcher's MinHook instance initialized across installation retries.
static bool s_bMinHookReady = false;

static bool InitializeMinHook( wchar_t *pError, size_t errorChars )
{
	if ( s_bMinHookReady )
		return true;
	MH_STATUS status = MH_Initialize();
	if ( status == MH_ERROR_ALREADY_INITIALIZED )
		status = MH_OK;
	s_bMinHookReady = status == MH_OK;
	if ( !s_bMinHookReady )
		SetError( pError, errorChars, L"MinHook initialization failed (status %u).", (unsigned int)status );
	return s_bMinHookReady;
}
}

bool AddModuleRedirect( const wchar_t *pModuleBaseName, const wchar_t *pReplacementDll, wchar_t *pError, size_t errorChars )
{
	if ( pError && errorChars )
		pError[0] = L'\0';
	if ( s_bHooksInstalled || s_nRedirects == _countof( s_Redirects ) )
	{
		SetTextError( pError, errorChars, L"No further module redirects can be registered." );
		return false;
	}
	if ( !pModuleBaseName || !pModuleBaseName[0] || !pReplacementDll || !pReplacementDll[0] )
	{
		SetTextError( pError, errorChars, L"A module redirect needs a module name and a replacement path." );
		return false;
	}
	ModuleRedirect &redirect = s_Redirects[s_nRedirects];
	if ( wcslen( pModuleBaseName ) >= _countof( redirect.baseName ) || wcslen( pReplacementDll ) >= _countof( redirect.replacement ) )
	{
		SetTextError( pError, errorChars, L"A module redirect path is too long." );
		return false;
	}
	size_t i = 0;
	for ( ; pModuleBaseName[i]; ++i )
		redirect.baseName[i] = Lower( pModuleBaseName[i] );
	redirect.baseName[i] = L'\0';
	wcsncpy_s( redirect.replacement, _countof( redirect.replacement ), pReplacementDll, _TRUNCATE );

	wchar_t loaded[MAX_PATH];
	_snwprintf_s( loaded, _countof( loaded ), _TRUNCATE, L"%s.dll", redirect.baseName );
	if ( GetModuleHandleW( loaded ) != NULL )
	{
		SetTextError( pError, errorChars, L"A redirected module is already loaded; refusing to replace it." );
		return false;
	}
	++s_nRedirects;
	return true;
}

bool InstallModuleRedirects( wchar_t *pError, size_t errorChars )
{
	if ( pError && errorChars )
		pError[0] = L'\0';
	if ( s_bHooksInstalled || s_nRedirects == 0 )
		return true;
	if ( !InitializeMinHook( pError, errorChars ) )
		return false;
	MH_STATUS status = MH_OK;

	HMODULE kernel32 = GetModuleHandleW( L"kernel32.dll" );
	void *pLoadLibraryA = kernel32 ? reinterpret_cast<void *>( GetProcAddress( kernel32, "LoadLibraryA" ) ) : NULL;
	void *pLoadLibraryW = kernel32 ? reinterpret_cast<void *>( GetProcAddress( kernel32, "LoadLibraryW" ) ) : NULL;
	void *pLoadLibraryExA = kernel32 ? reinterpret_cast<void *>( GetProcAddress( kernel32, "LoadLibraryExA" ) ) : NULL;
	void *pLoadLibraryExW = kernel32 ? reinterpret_cast<void *>( GetProcAddress( kernel32, "LoadLibraryExW" ) ) : NULL;
	if ( !pLoadLibraryA || !pLoadLibraryW || !pLoadLibraryExA || !pLoadLibraryExW )
	{
		SetTextError( pError, errorChars, L"Could not resolve the Kernel32 loader exports." );
		return false;
	}

	bool bLoadLibraryA = false;
	bool bLoadLibraryW = false;
	bool bLoadLibraryExA = false;
	bool bLoadLibraryExW = false;
	status = MH_CreateHook( pLoadLibraryA, reinterpret_cast<void *>( RedirectLoadLibraryA ), reinterpret_cast<void **>( &s_LoadLibraryA ) );
	bLoadLibraryA = status == MH_OK;
	if ( status == MH_OK )
	{
		status = MH_CreateHook( pLoadLibraryW, reinterpret_cast<void *>( RedirectLoadLibraryW ), reinterpret_cast<void **>( &s_LoadLibraryW ) );
		bLoadLibraryW = status == MH_OK;
	}
	if ( status == MH_OK )
	{
		status = MH_CreateHook( pLoadLibraryExA, reinterpret_cast<void *>( RedirectLoadLibraryExA ), reinterpret_cast<void **>( &s_LoadLibraryExA ) );
		bLoadLibraryExA = status == MH_OK;
	}
	if ( status == MH_OK )
	{
		status = MH_CreateHook( pLoadLibraryExW, reinterpret_cast<void *>( RedirectLoadLibraryExW ), reinterpret_cast<void **>( &s_LoadLibraryExW ) );
		bLoadLibraryExW = status == MH_OK;
	}
	if ( status == MH_OK )
	{
		status = MH_QueueEnableHook( pLoadLibraryA );
		if ( status == MH_OK ) status = MH_QueueEnableHook( pLoadLibraryW );
		if ( status == MH_OK ) status = MH_QueueEnableHook( pLoadLibraryExA );
		if ( status == MH_OK ) status = MH_QueueEnableHook( pLoadLibraryExW );
	}
	if ( status == MH_OK )
		status = MH_ApplyQueued();
	if ( status != MH_OK )
	{
		if ( bLoadLibraryA ) MH_DisableHook( pLoadLibraryA );
		if ( bLoadLibraryW ) MH_DisableHook( pLoadLibraryW );
		if ( bLoadLibraryExA ) MH_DisableHook( pLoadLibraryExA );
		if ( bLoadLibraryExW ) MH_DisableHook( pLoadLibraryExW );
		RemoveHooks( pLoadLibraryA, bLoadLibraryA, pLoadLibraryW, bLoadLibraryW, pLoadLibraryExA, bLoadLibraryExA, pLoadLibraryExW, bLoadLibraryExW );
		SetError( pError, errorChars, L"MinHook hook installation failed (status %u).", (unsigned int)status );
		return false;
	}
	s_bHooksInstalled = true;
	return true;
}
