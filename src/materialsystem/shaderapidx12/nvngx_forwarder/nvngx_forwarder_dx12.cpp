//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DLSS-NR snippet forwarder for shaderapidx12.
//
//          nvngx_dlssnr.dll only accepts calls whose return address lies in a module whose path contains "nvngx.dll"
//          (the driver core is _nvngx.dll); any other caller gets FAIL_PlatformError before arguments are read. This
//          DLL is therefore built as "nvngx.dll_dlssnr_dx12.dll" and every call into the snippet originates here.
//          Each forwarder stores the snippet result in a volatile local before returning: a tail call would jump into
//          the snippet with the renderer's return address and fail the same check.
//
//=============================================================================//
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>

namespace
{
typedef int( __cdecl *PFN_InitExt )( unsigned long long, const wchar_t *, ID3D12Device *, int, const void * );
typedef int( __cdecl *PFN_Shutdown1 )( ID3D12Device * );
typedef int( __cdecl *PFN_CreateFeature )( ID3D12GraphicsCommandList *, int, void *, void ** );
typedef int( __cdecl *PFN_EvaluateFeature )( ID3D12GraphicsCommandList *, const void *, const void *, void * );
typedef int( __cdecl *PFN_ReleaseFeature )( void * );

HMODULE g_hSnippet = nullptr;
PFN_InitExt g_pfnInit = nullptr;
PFN_Shutdown1 g_pfnShutdown1 = nullptr;
PFN_CreateFeature g_pfnCreate = nullptr;
PFN_EvaluateFeature g_pfnEvaluate = nullptr;
PFN_ReleaseFeature g_pfnRelease = nullptr;

constexpr int kNotLoaded = static_cast<int>( 0xBAD00000u | 2u ); // NVSDK_NGX_Result_FAIL_PlatformError

template <class Fn>
bool Resolve( const char *pszName, Fn &fn )
{
	fn = reinterpret_cast<Fn>( GetProcAddress( g_hSnippet, pszName ) );
	return fn != nullptr;
}
} // namespace

//-----------------------------------------------------------------------------
// Purpose: Loads the snippet by full path and resolves its D3D12 exports; 1 on success (or already loaded)
//-----------------------------------------------------------------------------
extern "C" __declspec( dllexport ) int __cdecl NrFwd_Load( const wchar_t *pszSnippetPath )
{
	if ( g_hSnippet )
		return 1;
	g_hSnippet = LoadLibraryExW( pszSnippetPath, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH );
	if ( !g_hSnippet )
		return 0;
	if ( !Resolve( "NVSDK_NGX_D3D12_Init_Ext", g_pfnInit ) || !Resolve( "NVSDK_NGX_D3D12_Shutdown1", g_pfnShutdown1 ) ||
	    !Resolve( "NVSDK_NGX_D3D12_CreateFeature", g_pfnCreate ) || !Resolve( "NVSDK_NGX_D3D12_EvaluateFeature", g_pfnEvaluate ) ||
	    !Resolve( "NVSDK_NGX_D3D12_ReleaseFeature", g_pfnRelease ) )
	{
		FreeLibrary( g_hSnippet );
		g_hSnippet = nullptr;
		return 0;
	}
	return 1;
}

//-----------------------------------------------------------------------------
// Snippet entry points. Each returns kNotLoaded until NrFwd_Load succeeded.
//-----------------------------------------------------------------------------
extern "C" __declspec( dllexport ) int __cdecl NrFwd_Init( unsigned long long nApplicationId, const wchar_t *pszDataPath, ID3D12Device *pDevice, int nSdkVersion, const void *pFeatureInfo )
{
	if ( !g_pfnInit )
		return kNotLoaded;
	volatile int nResult = g_pfnInit( nApplicationId, pszDataPath, pDevice, nSdkVersion, pFeatureInfo );
	return nResult;
}

extern "C" __declspec( dllexport ) int __cdecl NrFwd_CreateFeature( ID3D12GraphicsCommandList *pList, int nFeature, void *pParameters, void **ppHandle )
{
	if ( !g_pfnCreate )
		return kNotLoaded;
	volatile int nResult = g_pfnCreate( pList, nFeature, pParameters, ppHandle );
	return nResult;
}

extern "C" __declspec( dllexport ) int __cdecl NrFwd_EvaluateFeature( ID3D12GraphicsCommandList *pList, const void *pHandle, const void *pParameters )
{
	if ( !g_pfnEvaluate )
		return kNotLoaded;
	volatile int nResult = g_pfnEvaluate( pList, pHandle, pParameters, nullptr );
	return nResult;
}

extern "C" __declspec( dllexport ) int __cdecl NrFwd_ReleaseFeature( void *pHandle )
{
	if ( !g_pfnRelease )
		return kNotLoaded;
	volatile int nResult = g_pfnRelease( pHandle );
	return nResult;
}

extern "C" __declspec( dllexport ) int __cdecl NrFwd_Shutdown( ID3D12Device *pDevice )
{
	if ( !g_pfnShutdown1 )
		return kNotLoaded;
	volatile int nResult = g_pfnShutdown1( pDevice );
	return nResult;
}

//-----------------------------------------------------------------------------
// Purpose: Unloads the snippet and forgets every resolved export
//-----------------------------------------------------------------------------
extern "C" __declspec( dllexport ) void __cdecl NrFwd_Unload()
{
	if ( g_hSnippet )
		FreeLibrary( g_hSnippet );
	g_hSnippet = nullptr;
	g_pfnInit = nullptr;
	g_pfnShutdown1 = nullptr;
	g_pfnCreate = nullptr;
	g_pfnEvaluate = nullptr;
	g_pfnRelease = nullptr;
}
