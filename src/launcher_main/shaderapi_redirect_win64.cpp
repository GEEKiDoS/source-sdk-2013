//========= Copyright Valve Corporation, All rights reserved. ============//

#if !defined( _WIN64 )
#error This translation unit is only valid for the Win64 launcher.
#endif

#include "shaderapi_redirect_win64.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

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
static wchar_t s_RendererDll[MAX_PATH];

static bool IsDx9NameA( LPCSTR pName )
{
	if ( !pName )
		return false;

	const char *pBase = pName;
	for ( const char *p = pName; *p; ++p )
	{
		if ( *p == '\\' || *p == '/' )
			pBase = p + 1;
	}

	static const char kName[] = "shaderapidx9";
	static const char kNameDll[] = "shaderapidx9.dll";
	const size_t length = strlen( pBase );
	if ( length != sizeof( kName ) - 1 && length != sizeof( kNameDll ) - 1 )
		return false;

	const char *pExpected = length == sizeof( kName ) - 1 ? kName : kNameDll;
	for ( size_t i = 0; i < length; ++i )
	{
		char actual = pBase[i];
		if ( actual >= 'A' && actual <= 'Z' )
			actual = (char)( actual + ( 'a' - 'A' ) );
		if ( actual != pExpected[i] )
			return false;
	}
	return true;
}

static bool IsDx9NameW( LPCWSTR pName )
{
	if ( !pName )
		return false;

	const wchar_t *pBase = pName;
	for ( const wchar_t *p = pName; *p; ++p )
	{
		if ( *p == L'\\' || *p == L'/' )
			pBase = p + 1;
	}

	static const wchar_t kName[] = L"shaderapidx9";
	static const wchar_t kNameDll[] = L"shaderapidx9.dll";
	const size_t length = wcslen( pBase );
	if ( length != _countof( kName ) - 1 && length != _countof( kNameDll ) - 1 )
		return false;

	const wchar_t *pExpected = length == _countof( kName ) - 1 ? kName : kNameDll;
	for ( size_t i = 0; i < length; ++i )
	{
		wchar_t actual = pBase[i];
		if ( actual >= L'A' && actual <= L'Z' )
			actual = (wchar_t)( actual + ( L'a' - L'A' ) );
		if ( actual != pExpected[i] )
			return false;
	}
	return true;
}

static bool IsResourceLoad( DWORD flags )
{
	return ( flags & ( LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE | LOAD_LIBRARY_AS_IMAGE_RESOURCE ) ) != 0;
}

static HMODULE WINAPI RedirectLoadLibraryA( LPCSTR pName )
{
	if ( !pName || !IsDx9NameA( pName ) )
		return s_LoadLibraryA( pName );

	HMODULE result = s_LoadLibraryExW( s_RendererDll, NULL, 0 );
	const DWORD error = GetLastError();
	SetLastError( error );
	return result;
}

static HMODULE WINAPI RedirectLoadLibraryW( LPCWSTR pName )
{
	if ( !pName || !IsDx9NameW( pName ) )
		return s_LoadLibraryW( pName );

	HMODULE result = s_LoadLibraryExW( s_RendererDll, NULL, 0 );
	const DWORD error = GetLastError();
	SetLastError( error );
	return result;
}

static HMODULE WINAPI RedirectLoadLibraryExA( LPCSTR pName, HANDLE hFile, DWORD flags )
{
	if ( !pName || IsResourceLoad( flags ) || !IsDx9NameA( pName ) )
		return s_LoadLibraryExA( pName, hFile, flags );

	HMODULE result = s_LoadLibraryExW( s_RendererDll, hFile, flags );
	const DWORD error = GetLastError();
	SetLastError( error );
	return result;
}

static HMODULE WINAPI RedirectLoadLibraryExW( LPCWSTR pName, HANDLE hFile, DWORD flags )
{
	if ( !pName || IsResourceLoad( flags ) || !IsDx9NameW( pName ) )
		return s_LoadLibraryExW( pName, hFile, flags );

	HMODULE result = s_LoadLibraryExW( s_RendererDll, hFile, flags );
	const DWORD error = GetLastError();
	SetLastError( error );
	return result;
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
}

bool InstallShaderApiDx12Redirect( const wchar_t *pRendererDll, wchar_t *pError, size_t errorChars )
{
	if ( pError && errorChars )
		pError[0] = L'\0';
	if ( !pRendererDll || !pRendererDll[0] )
	{
		SetTextError( pError, errorChars, L"The DX12 renderer path is empty." );
		return false;
	}
	if ( wcslen( pRendererDll ) >= _countof( s_RendererDll ) )
	{
		SetTextError( pError, errorChars, L"The DX12 renderer path is too long." );
		return false;
	}
	if ( GetModuleHandleW( L"shaderapidx9.dll" ) != NULL )
	{
		SetTextError( pError, errorChars, L"shaderapidx9.dll is already loaded; refusing to change renderer." );
		return false;
	}

	MH_STATUS status = MH_Initialize();
	if ( status != MH_OK )
	{
		SetError( pError, errorChars, L"MinHook initialization failed (status %u).", (unsigned int)status );
		return false;
	}

	HMODULE kernel32 = GetModuleHandleW( L"kernel32.dll" );
	void *pLoadLibraryA = kernel32 ? reinterpret_cast<void *>( GetProcAddress( kernel32, "LoadLibraryA" ) ) : NULL;
	void *pLoadLibraryW = kernel32 ? reinterpret_cast<void *>( GetProcAddress( kernel32, "LoadLibraryW" ) ) : NULL;
	void *pLoadLibraryExA = kernel32 ? reinterpret_cast<void *>( GetProcAddress( kernel32, "LoadLibraryExA" ) ) : NULL;
	void *pLoadLibraryExW = kernel32 ? reinterpret_cast<void *>( GetProcAddress( kernel32, "LoadLibraryExW" ) ) : NULL;
	if ( !pLoadLibraryA || !pLoadLibraryW || !pLoadLibraryExA || !pLoadLibraryExW )
	{
		SetTextError( pError, errorChars, L"Could not resolve the Kernel32 loader exports." );
		MH_Uninitialize();
		return false;
	}

	bool bLoadLibraryA = false;
	bool bLoadLibraryW = false;
	bool bLoadLibraryExA = false;
	bool bLoadLibraryExW = false;
	wcsncpy_s( s_RendererDll, _countof( s_RendererDll ), pRendererDll, _TRUNCATE );
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
		MH_Uninitialize();
		SetError( pError, errorChars, L"MinHook hook installation failed (status %u).", (unsigned int)status );
		return false;
	}

	return true;
}
