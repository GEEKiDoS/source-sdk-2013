//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Helpers shared by the upscaler and frame-generation providers for loading vendor runtimes that live
//          beside the renderer: path joining, full-path LoadLibrary, export resolution and file-version strings.
//
//=============================================================================//
#ifndef PROVIDER_MODULE_DX12_H
#define PROVIDER_MODULE_DX12_H
#pragma once

#include "tier1/strtools.h"
#include "tier1/utlvector.h"
#include <windows.h>

namespace shaderapidx12
{

//-----------------------------------------------------------------------------
// Purpose: Resolves one provider export; false when the module lacks it
//-----------------------------------------------------------------------------
template <class Fn>
inline bool ResolveExport( HMODULE hModule, const char *pszName, Fn &fn )
{
	fn = reinterpret_cast<Fn>( GetProcAddress( hModule, pszName ) );
	return fn != nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: Joins a directory (with trailing separator) and a file name; false when the result does not fit
//-----------------------------------------------------------------------------
inline bool BuildPath( wchar_t ( &szPath )[MAX_PATH], const wchar_t *pszDir, const wchar_t *pszName )
{
	return V_snwprintf( szPath, MAX_PATH, L"%ls%ls", pszDir, pszName ) < MAX_PATH;
}

//-----------------------------------------------------------------------------
// Purpose: Appends a backslash to a non-empty directory that lacks a trailing separator, if it fits
//-----------------------------------------------------------------------------
inline void AppendSeparator( wchar_t *pszDir, int nDirSize )
{
	const int nLength = V_wcslen( pszDir );
	if ( nLength && nLength + 1 < nDirSize && pszDir[nLength - 1] != L'\\' && pszDir[nLength - 1] != L'/' )
	{
		pszDir[nLength] = L'\\';
		pszDir[nLength + 1] = 0;
	}
}

inline bool FileExists( const wchar_t *pszDir, const wchar_t *pszName )
{
	wchar_t szPath[MAX_PATH];
	return BuildPath( szPath, pszDir, pszName ) && GetFileAttributesW( szPath ) != INVALID_FILE_ATTRIBUTES;
}

//-----------------------------------------------------------------------------
// Purpose: Loads `pszDir\pszName` by full path; nullptr when the file is missing or fails to load
//-----------------------------------------------------------------------------
inline HMODULE LoadProviderModule( const wchar_t *pszDir, const wchar_t *pszName )
{
	wchar_t szPath[MAX_PATH];
	if ( !BuildPath( szPath, pszDir, pszName ) || GetFileAttributesW( szPath ) == INVALID_FILE_ATTRIBUTES )
		return nullptr;
	return LoadLibraryExW( szPath, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH );
}

//-----------------------------------------------------------------------------
// Purpose: Writes the file version of `pszDir\pszName` as "a.b.c.d", or "unknown version"
//-----------------------------------------------------------------------------
inline void FileVersion( const wchar_t *pszDir, const wchar_t *pszName, char *pszVersion, int nVersionSize )
{
	V_strncpy( pszVersion, "unknown version", nVersionSize );
	wchar_t szPath[MAX_PATH];
	if ( !BuildPath( szPath, pszDir, pszName ) )
		return;
	DWORD hHandle = 0;
	const DWORD nSize = GetFileVersionInfoSizeW( szPath, &hHandle );
	if ( !nSize )
		return;
	CUtlVector<unsigned char> data;
	data.SetCount( static_cast<int>( nSize ) );
	VS_FIXEDFILEINFO *pInfo = nullptr;
	UINT nLength = 0;
	if ( !GetFileVersionInfoW( szPath, 0, nSize, data.Base() ) || !VerQueryValueW( data.Base(), L"\\", reinterpret_cast<void **>( &pInfo ), &nLength ) || !pInfo )
		return;
	V_snprintf( pszVersion, nVersionSize, "%u.%u.%u.%u", HIWORD( pInfo->dwFileVersionMS ), LOWORD( pInfo->dwFileVersionMS ), HIWORD( pInfo->dwFileVersionLS ), LOWORD( pInfo->dwFileVersionLS ) );
}

} // namespace shaderapidx12

#endif // PROVIDER_MODULE_DX12_H
