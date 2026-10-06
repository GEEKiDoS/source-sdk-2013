//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DX12 native-AA upscaler providers (DLSS/DLAA, FSR, XeSS), the DLSS-NR layer chain and the
//          CShaderAPIDX12 integration that selects, dispatches and releases them.
//
//=============================================================================//
#include "upscaler_dx12.h"
#include "command_recorder_dx12.h"
#include "shaderapi_dx12.h"
#include "shaderdevice_dx12.h"
#include "upscaler_nr_shaders_dx12.h"
#include "provider_module_dx12.h"
#include "materialsystem/imaterialsystem.h"
#include "renderparm.h"
#include "tier0/dbg.h"
#include "tier0/icommandline.h"
#include "tier0/platform.h"
#include "tier1/convar.h"
#include "tier1/strtools.h"
#include <d3dcompiler.h>
#include <type_traits>
#include <xess/xess_d3d12.h>
// FidelityFX SDK v2 (split runtime): amd_fidelityfx_loader_dx12.dll dispatches to amd_fidelityfx_upscaler_dx12.dll.
#include <upscalers/include/ffx_upscale.h>
#include <api/include/dx12/ffx_api_dx12.h>
#include <nvsdk_ngx.h>

namespace shaderapidx12
{
static_assert( kNrMaxLayersDX12 == DX12_NR_MAX_LAYERS, "renderparm.h and the provider agree on the DLSS-NR layer limit" );

//-----------------------------------------------------------------------------
// Purpose: Console/log name of a provider kind
//-----------------------------------------------------------------------------
const char *UpscalerKindNameDX12( UpscalerKindDX12 kind )
{
	switch ( kind )
	{
	case UpscalerKindDX12::DLSS:
		return "DLSS/DLAA";
	case UpscalerKindDX12::FSR:
		return "FSR native AA";
	case UpscalerKindDX12::XeSS:
		return "XeSS AA";
	default:
		return "none";
	}
}

namespace
{
// Failure codes in UpscalerReplayResultDX12::code/nrCode; provider results are folded into the low 16 bits.
constexpr uint32_t kReplayForced = 0x10001u, kReplayNoContext = 0x10002u;
constexpr D3D12_RESOURCE_STATES kRead = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
constexpr D3D12_RESOURCE_STATES kNpsr = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
constexpr D3D12_RESOURCE_STATES kUav = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
// Descriptor block per compute pass: t0..t3 then u0..u1.
constexpr uint32_t kPassDescriptors = 6;

//-----------------------------------------------------------------------------
// Purpose: Radical inverse of `nIndex` in `nBase` (one Halton sequence coordinate)
//-----------------------------------------------------------------------------
float Halton( uint32_t nIndex, uint32_t nBase )
{
	float flFraction = 1.f, flResult = 0.f;
	for ( ; nIndex; nIndex /= nBase )
	{
		flFraction /= static_cast<float>( nBase );
		flResult += flFraction * static_cast<float>( nIndex % nBase );
	}
	return flResult;
}

//-----------------------------------------------------------------------------
// Purpose: Appends a whole-resource transition to `pBarriers` unless the states match
//-----------------------------------------------------------------------------
void AddTransition( ID3D12Resource *pResource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after, D3D12_RESOURCE_BARRIER *pBarriers, UINT &nCount )
{
	if ( !pResource || before == after )
		return;
	D3D12_RESOURCE_BARRIER &b = pBarriers[nCount++];
	b = {};
	b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	b.Transition.pResource = pResource;
	b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	b.Transition.StateBefore = before;
	b.Transition.StateAfter = after;
}

void Transition( ID3D12GraphicsCommandList *pList, ID3D12Resource *pResource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after )
{
	D3D12_RESOURCE_BARRIER barrier;
	UINT nCount = 0;
	AddTransition( pResource, before, after, &barrier, nCount );
	if ( nCount )
		pList->ResourceBarrier( 1, &barrier );
}

//-----------------------------------------------------------------------------
// Purpose: Committed single-mip UAV-capable 2D texture; null on failure
//-----------------------------------------------------------------------------
Microsoft::WRL::ComPtr<ID3D12Resource> CreateTexture( ID3D12Device *pDevice, uint32_t nWidth, uint32_t nHeight, DXGI_FORMAT format, D3D12_RESOURCE_STATES state )
{
	D3D12_HEAP_PROPERTIES heap{};
	heap.Type = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_RESOURCE_DESC desc{};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = nWidth;
	desc.Height = nHeight;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = format;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	Microsoft::WRL::ComPtr<ID3D12Resource> pResource;
	if ( FAILED( pDevice->CreateCommittedResource( &heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS( &pResource ) ) ) )
		pResource.Reset();
	return pResource;
}

} // namespace

// Recorded payload: the stable provider object plus the dispatch copied at record time.
struct UpscalerReplayPayloadDX12
{
	const CUpscalerDX12 *owner;
	UpscalerDispatchDX12 dispatch;
};

static_assert( std::is_trivially_copyable<UpscalerReplayPayloadDX12>::value, "external command payloads are copied bytewise" );

struct CUpscalerDX12::XeSSApi
{
	decltype( &xessGetVersion ) getVersion = nullptr;
	decltype( &xessD3D12CreateContext ) createContext = nullptr;
	decltype( &xessD3D12Init ) init = nullptr;
	decltype( &xessD3D12Execute ) execute = nullptr;
	decltype( &xessDestroyContext ) destroyContext = nullptr;
	decltype( &xessGetInputResolution ) getInputResolution = nullptr;
	decltype( &xessSetVelocityScale ) setVelocityScale = nullptr;
	decltype( &xessSetJitterScale ) setJitterScale = nullptr;      // optional
	decltype( &xessGetJitterScale ) getJitterScale = nullptr;      // optional
	decltype( &xessGetIntelXeFXVersion ) getXeFXVersion = nullptr; // optional
	decltype( &xessSetLoggingCallback ) setLogging = nullptr;      // optional
};

struct CUpscalerDX12::FfxApi
{
	HMODULE upscaler = nullptr; // effect DLL, loaded by full path so the loader's bare-name LoadLibraryA finds it
	PfnFfxCreateContext createContext = nullptr;
	PfnFfxDestroyContext destroyContext = nullptr;
	PfnFfxQuery query = nullptr;
	PfnFfxDispatch dispatch = nullptr;
	bool fsr4 = false; // the selected provider is FSR 4 (UNTESTED: no RDNA3/RDNA4 GPU was available)
};

// NGX driver core (_nvngx.dll). Exports differ from the static-library wrappers: Init_ProjectID takes the SDK version
// before the feature info.
struct CUpscalerDX12::NgxApi
{
	using InitProjectId = NVSDK_NGX_Result( NVSDK_CONV * )( const char *, NVSDK_NGX_EngineType, const char *, const wchar_t *, ID3D12Device *, NVSDK_NGX_Version, const NVSDK_NGX_FeatureCommonInfo * );
	using Shutdown1 = NVSDK_NGX_Result( NVSDK_CONV * )( ID3D12Device * );
	using Parameters = NVSDK_NGX_Result( NVSDK_CONV * )( NVSDK_NGX_Parameter ** );
	using DestroyParameters = NVSDK_NGX_Result( NVSDK_CONV * )( NVSDK_NGX_Parameter * );
	using CreateFeature = NVSDK_NGX_Result( NVSDK_CONV * )( ID3D12GraphicsCommandList *, NVSDK_NGX_Feature, NVSDK_NGX_Parameter *, NVSDK_NGX_Handle ** );
	using ReleaseFeature = NVSDK_NGX_Result( NVSDK_CONV * )( NVSDK_NGX_Handle * );
	using EvaluateFeature = NVSDK_NGX_Result( NVSDK_CONV * )( ID3D12GraphicsCommandList *, const NVSDK_NGX_Handle *, NVSDK_NGX_Parameter *, PFN_NVSDK_NGX_ProgressCallback );
	InitProjectId init = nullptr;
	Shutdown1 shutdown = nullptr;
	Parameters capabilities = nullptr, allocate = nullptr;
	DestroyParameters destroy = nullptr;
	CreateFeature create = nullptr;
	ReleaseFeature release = nullptr;
	EvaluateFeature evaluate = nullptr;
	bool initialized = false;
	NVSDK_NGX_Parameter *caps = nullptr;       // capability block: DLSS availability, and the DLSS-NR parameter block
	NVSDK_NGX_Parameter *dlssParams = nullptr; // DLSS create/evaluate block, owned here
	wchar_t dataPath[MAX_PATH] = {};           // renderer directory without the trailing separator
	const wchar_t *pathList[1] = {};           // NGX feature search path list; points at dataPath
};

// DLSS-NR snippet calls go through nvngx.dll_dlssnr_dx12.dll: nvngx_dlssnr.dll rejects any caller whose return address
// is outside a module path containing "nvngx.dll".
struct CUpscalerDX12::NrApi
{
	using Load = int( __cdecl * )( const wchar_t * );
	using Init = int( __cdecl * )( unsigned long long, const wchar_t *, ID3D12Device *, int, const void * );
	using Create = int( __cdecl * )( ID3D12GraphicsCommandList *, int, void *, void ** );
	using Evaluate = int( __cdecl * )( ID3D12GraphicsCommandList *, const void *, const void * );
	using Release = int( __cdecl * )( void * );
	using Shutdown = int( __cdecl * )( ID3D12Device * );
	using Unload = void( __cdecl * )();
	Load load = nullptr;
	Init init = nullptr;
	Create create = nullptr;
	Evaluate evaluate = nullptr;
	Release release = nullptr;
	Shutdown shutdown = nullptr;
	Unload unload = nullptr;
	bool initialized = false;
};

struct CUpscalerDX12::NrChain
{
	DlssNrTuningDX12 tuning;
	uint32_t layers = 0, width = 0, height = 0;
	ID3D12Resource *source = nullptr;
	// proxy: NON_PIXEL_SHADER_RESOURCE between replays; answer and out[]: UNORDERED_ACCESS.
	Microsoft::WRL::ComPtr<ID3D12Resource> proxy, answer, out[2];
	void *handles[kNrMaxLayersDX12] = {};
};

CUpscalerDX12::CUpscalerDX12() = default;

CUpscalerDX12::~CUpscalerDX12()
{
	Shutdown();
}

//-----------------------------------------------------------------------------
// Purpose: Loads every provider installed in `pszModuleDir` and logs what is available
//-----------------------------------------------------------------------------
bool CUpscalerDX12::Initialize( ID3D12Device *pDevice, const MaterialAdapterInfo_t &adapter, const wchar_t *pszModuleDir, bool bVerbose )
{
	Shutdown();
	if ( !pDevice )
		return false;
	m_pDevice = pDevice;
	m_bVerbose = bVerbose;
	m_nVendor = adapter.m_VendorID;
	m_nDeviceId = adapter.m_DeviceID;
	V_wcscpy_safe( m_szDir, pszModuleDir );
	AppendSeparator( m_szDir, MAX_PATH );
	LoadNgx( m_szDir );
	LoadFfx( m_szDir );
	LoadXeSS( m_szDir );
	ProbeNr( m_szDir );
	Msg( "ShaderAPIDX12 upscaler: adapter vendor 0x%04x device 0x%04x\n", m_nVendor, m_nDeviceId );
	const char *const pszNames[] = { "dlss", "fsr", "xess", "dlssnr" };
	const Provider *const pProviders[] = { &m_Dlss, &m_Fsr, &m_Xess, &m_Nr };
	for ( size_t i = 0; i < ARRAYSIZE( pProviders ); ++i )
	{
		if ( pProviders[i]->available )
			Msg( "ShaderAPIDX12 upscaler: %s available (%s)\n", pszNames[i], pProviders[i]->version.Get() );
		else
			Msg( "ShaderAPIDX12 upscaler: %s unavailable: %s\n", pszNames[i], pProviders[i]->reason.Get() );
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Maps an r_upscaler mode to an available provider
//-----------------------------------------------------------------------------
UpscalerKindDX12 CUpscalerDX12::Resolve( int nMode ) const
{
	switch ( nMode )
	{
	case 1:
		if ( m_Dlss.available )
			return UpscalerKindDX12::DLSS;
		if ( m_nVendor == 0x8086 && m_Xess.available )
			return UpscalerKindDX12::XeSS;
		if ( m_Fsr.available )
			return UpscalerKindDX12::FSR;
		if ( m_Xess.available )
			return UpscalerKindDX12::XeSS;
		return UpscalerKindDX12::None;
	case 2:
		return m_Dlss.available ? UpscalerKindDX12::DLSS : UpscalerKindDX12::None;
	case 3:
		return m_Fsr.available ? UpscalerKindDX12::FSR : UpscalerKindDX12::None;
	case 4:
		return m_Xess.available ? UpscalerKindDX12::XeSS : UpscalerKindDX12::None;
	default:
		return UpscalerKindDX12::None;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Loads libxess.dll and probes device support with a throwaway context
//-----------------------------------------------------------------------------
bool CUpscalerDX12::LoadXeSS( const wchar_t *pszDir )
{
	m_Xess.module = LoadProviderModule( pszDir, L"libxess.dll" );
	if ( !m_Xess.module )
	{
		m_Xess.reason = "libxess.dll not installed beside the renderer";
		return false;
	}
	XeSSApi *pApi = new XeSSApi;
	const auto fail = [&]( const char *pszReason )
	{
		delete pApi;
		FreeLibrary( m_Xess.module );
		m_Xess.module = nullptr;
		m_Xess.reason = pszReason;
		return false;
	};
	if ( !ResolveExport( m_Xess.module, "xessGetVersion", pApi->getVersion ) || !ResolveExport( m_Xess.module, "xessD3D12CreateContext", pApi->createContext ) ||
	    !ResolveExport( m_Xess.module, "xessD3D12Init", pApi->init ) || !ResolveExport( m_Xess.module, "xessD3D12Execute", pApi->execute ) ||
	    !ResolveExport( m_Xess.module, "xessDestroyContext", pApi->destroyContext ) || !ResolveExport( m_Xess.module, "xessGetInputResolution", pApi->getInputResolution ) ||
	    !ResolveExport( m_Xess.module, "xessSetVelocityScale", pApi->setVelocityScale ) )
		return fail( "libxess.dll lacks a required D3D12 export" );
	ResolveExport( m_Xess.module, "xessSetJitterScale", pApi->setJitterScale );
	ResolveExport( m_Xess.module, "xessGetJitterScale", pApi->getJitterScale );
	ResolveExport( m_Xess.module, "xessGetIntelXeFXVersion", pApi->getXeFXVersion );
	ResolveExport( m_Xess.module, "xessSetLoggingCallback", pApi->setLogging );
	xess_version_t version{};
	if ( pApi->getVersion( &version ) != XESS_RESULT_SUCCESS )
		return fail( "xessGetVersion failed" );
	char szText[96];
	V_snprintf( szText, sizeof( szText ), "XeSS %u.%u.%u", version.major, version.minor, version.patch );
	m_Xess.version = szText;
	// Context creation is the device capability decision (the DP4a path needs SM 6.4; the runtime decides).
	xess_context_handle_t hProbe = nullptr;
	const xess_result_t created = pApi->createContext( m_pDevice, &hProbe );
	if ( created != XESS_RESULT_SUCCESS || !hProbe )
	{
		V_snprintf( szText, sizeof( szText ), "xessD3D12CreateContext rejected this device (%d)", static_cast<int>( created ) );
		return fail( szText );
	}
	if ( pApi->getXeFXVersion )
	{
		xess_version_t xefx{};
		if ( pApi->getXeFXVersion( hProbe, &xefx ) == XESS_RESULT_SUCCESS && ( xefx.major || xefx.minor || xefx.patch ) )
		{
			V_snprintf( szText, sizeof( szText ), ", XeFX %u.%u.%u", xefx.major, xefx.minor, xefx.patch );
			m_Xess.version += szText;
		}
	}
	pApi->destroyContext( hProbe );
	m_pXessApi = pApi;
	m_Xess.available = true;
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Loads the FidelityFX loader and upscaler DLLs and selects the FSR provider for this adapter
//-----------------------------------------------------------------------------
bool CUpscalerDX12::LoadFfx( const wchar_t *pszDir )
{
	// The loader resolves effect DLLs by bare name; the effect DLL is loaded first by full path so that name matches
	// the already-loaded module instead of whatever the process search order would find.
	HMODULE hUpscaler = LoadProviderModule( pszDir, L"amd_fidelityfx_upscaler_dx12.dll" );
	if ( !hUpscaler )
	{
		m_Fsr.reason = "amd_fidelityfx_upscaler_dx12.dll not installed beside the renderer";
		return false;
	}
	m_Fsr.module = LoadProviderModule( pszDir, L"amd_fidelityfx_loader_dx12.dll" );
	if ( !m_Fsr.module )
	{
		FreeLibrary( hUpscaler );
		m_Fsr.reason = "amd_fidelityfx_loader_dx12.dll not installed beside the renderer";
		return false;
	}
	FfxApi *pApi = new FfxApi;
	pApi->upscaler = hUpscaler;
	const auto fail = [&]( const char *pszReason )
	{
		FreeLibrary( pApi->upscaler );
		delete pApi;
		FreeLibrary( m_Fsr.module );
		m_Fsr.module = nullptr;
		m_Fsr.reason = pszReason;
		return false;
	};
	if ( !ResolveExport( m_Fsr.module, "ffxCreateContext", pApi->createContext ) || !ResolveExport( m_Fsr.module, "ffxDestroyContext", pApi->destroyContext ) ||
	    !ResolveExport( m_Fsr.module, "ffxQuery", pApi->query ) || !ResolveExport( m_Fsr.module, "ffxDispatch", pApi->dispatch ) )
		return fail( "amd_fidelityfx_loader_dx12.dll lacks a required FFX API export" );
	uint64_t nCount = 0;
	ffxQueryDescGetVersions versions{};
	versions.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
	versions.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
	versions.device = m_pDevice;
	versions.outputCount = &nCount;
	if ( pApi->query( nullptr, &versions.header ) != FFX_API_RETURN_OK || !nCount )
		return fail( "no FFX upscale provider enumerated" );
	CUtlVector<uint64_t> ids;
	CUtlVector<const char *> names;
	ids.SetCount( static_cast<int>( nCount ) );
	ids.FillWithValue( 0 );
	names.SetCount( static_cast<int>( nCount ) );
	names.FillWithValue( nullptr );
	versions.versionIds = ids.Base();
	versions.versionNames = names.Base();
	if ( pApi->query( nullptr, &versions.header ) != FFX_API_RETURN_OK )
		return fail( "FFX version query failed" );
	// The list is newest first. FSR 4 is selected only on AMD adapters (it needs RDNA3/RDNA4); elsewhere the newest
	// FSR 3.x provider runs. The opaque id is never hard-coded.
	CUtlString all;
	int nSelected = -1;
	for ( int i = 0; i < names.Count(); ++i )
	{
		const char *pszName = names[i] ? names[i] : "?";
		all += i ? ", " : "";
		all += pszName;
		const char *pszSpace = V_strrchr( pszName, ' ' );
		const int nMajor = V_atoi( pszSpace ? pszSpace + 1 : pszName );
		if ( nSelected < 0 && ( nMajor == 3 || ( nMajor >= 4 && m_nVendor == 0x1002 ) ) )
			nSelected = i;
	}
	if ( nSelected < 0 )
	{
		CUtlString reason;
		reason.Format( "no usable FFX upscale provider among: %s", all.Get() );
		return fail( reason.Get() );
	}
	const char *pszChosen = names[nSelected] ? names[nSelected] : "?";
	const char *pszSpace = V_strrchr( pszChosen, ' ' );
	pApi->fsr4 = V_atoi( pszSpace ? pszSpace + 1 : pszChosen ) >= 4;
	m_Fsr.versionId = ids[nSelected];
	char szLoaderVersion[64];
	FileVersion( pszDir, L"amd_fidelityfx_loader_dx12.dll", szLoaderVersion, sizeof( szLoaderVersion ) );
	m_Fsr.version.Format( "%s%s; loader %s; enumerated %s", pszChosen, pApi->fsr4 ? " [FSR 4 path UNTESTED]" : "", szLoaderVersion, all.Get() );
	m_pFfxApi = pApi;
	m_Fsr.available = true;
	return true;
}

static void NVSDK_CONV NgxLog( const char *pszMessage, NVSDK_NGX_Logging_Level, NVSDK_NGX_Feature feature )
{
	Msg( "ShaderAPIDX12 upscaler: NGX[%d]: %s", static_cast<int>( feature ), pszMessage ? pszMessage : "\n" );
}

//-----------------------------------------------------------------------------
// Purpose: Loads the driver's NGX core, initialises it for this device and checks DLSS (nvngx_dlss.dll) support
//-----------------------------------------------------------------------------
bool CUpscalerDX12::LoadNgx( const wchar_t *pszDir )
{
	if ( m_nVendor != 0x10de )
	{
		m_Dlss.reason = m_NgxCore.reason = "not an NVIDIA adapter";
		return false;
	}
	if ( !FileExists( pszDir, L"nvngx_dlss.dll" ) )
	{
		m_Dlss.reason = "nvngx_dlss.dll not installed beside the renderer";
	}
	// The driver publishes the NGX core location; _nvngx.dll is the core, nvngx.dll the legacy name. One extra
	// character leaves room for the separator appended to a registry value that fills MAX_PATH.
	wchar_t szCoreDir[MAX_PATH + 1] = {};
	DWORD nBytes = MAX_PATH * sizeof( wchar_t );
	if ( RegGetValueW( HKEY_LOCAL_MACHINE, L"System\\CurrentControlSet\\Services\\nvlddmkm\\NGXCore", L"NGXPath", RRF_RT_REG_SZ, nullptr, szCoreDir, &nBytes ) == ERROR_SUCCESS )
	{
		const DWORD nAttributes = GetFileAttributesW( szCoreDir );
		if ( nAttributes != INVALID_FILE_ATTRIBUTES && !( nAttributes & FILE_ATTRIBUTE_DIRECTORY ) )
		{
			// The value names a file: keep its directory, including the last separator.
			wchar_t *pszSlash = wcsrchr( szCoreDir, L'\\' );
			wchar_t *pszForwardSlash = wcsrchr( szCoreDir, L'/' );
			if ( !pszSlash || ( pszForwardSlash && pszForwardSlash > pszSlash ) )
				pszSlash = pszForwardSlash;
			if ( pszSlash )
				pszSlash[1] = 0;
			else
				szCoreDir[0] = 0;
		}
		AppendSeparator( szCoreDir, MAX_PATH + 1 );
	}
	else
	{
		szCoreDir[0] = 0;
	}
	const wchar_t *const pszCoreNames[] = { L"_nvngx.dll", L"nvngx.dll" };
	for ( size_t i = 0; i < ARRAYSIZE( pszCoreNames ) && !m_NgxCore.module && szCoreDir[0]; ++i )
		m_NgxCore.module = LoadProviderModule( szCoreDir, pszCoreNames[i] );
	if ( !m_NgxCore.module )
	{
		m_Dlss.reason = m_NgxCore.reason = "NGX core (_nvngx.dll) not found through HKLM\\...\\nvlddmkm\\NGXCore\\NGXPath";
		return false;
	}
	NgxApi *pApi = new NgxApi;
	const auto fail = [&]( const char *pszReason )
	{
		if ( pApi->caps )
			pApi->destroy( pApi->caps );
		if ( pApi->initialized )
			pApi->shutdown( m_pDevice );
		delete pApi;
		FreeLibrary( m_NgxCore.module );
		m_NgxCore.module = nullptr;
		m_NgxCore.reason = pszReason;
		if ( m_Dlss.reason.IsEmpty() )
			m_Dlss.reason = pszReason;
		return false;
	};
	if ( !ResolveExport( m_NgxCore.module, "NVSDK_NGX_D3D12_Init_ProjectID", pApi->init ) || !ResolveExport( m_NgxCore.module, "NVSDK_NGX_D3D12_Shutdown1", pApi->shutdown ) ||
	    !ResolveExport( m_NgxCore.module, "NVSDK_NGX_D3D12_GetCapabilityParameters", pApi->capabilities ) || !ResolveExport( m_NgxCore.module, "NVSDK_NGX_D3D12_AllocateParameters", pApi->allocate ) ||
	    !ResolveExport( m_NgxCore.module, "NVSDK_NGX_D3D12_DestroyParameters", pApi->destroy ) || !ResolveExport( m_NgxCore.module, "NVSDK_NGX_D3D12_CreateFeature", pApi->create ) ||
	    !ResolveExport( m_NgxCore.module, "NVSDK_NGX_D3D12_ReleaseFeature", pApi->release ) || !ResolveExport( m_NgxCore.module, "NVSDK_NGX_D3D12_EvaluateFeature", pApi->evaluate ) )
		return fail( "NGX core lacks a required D3D12 export" );
	// Feature DLLs (nvngx_dlss.dll) are searched in PathListInfo; the renderer directory is the only entry. pathList
	// points into this heap block, which stays put until Shutdown.
	V_wcscpy_safe( pApi->dataPath, pszDir );
	const int nDataPathLength = V_wcslen( pApi->dataPath );
	if ( nDataPathLength )
		pApi->dataPath[nDataPathLength - 1] = 0;
	pApi->pathList[0] = pApi->dataPath;
	NVSDK_NGX_FeatureCommonInfo info{};
	info.PathListInfo.Path = pApi->pathList;
	info.PathListInfo.Length = 1;
	if ( m_bVerbose )
	{
		info.LoggingInfo.LoggingCallback = NgxLog;
		info.LoggingInfo.MinimumLoggingLevel = NVSDK_NGX_LOGGING_LEVEL_ON;
	}
	// No NVIDIA-issued application id: a custom-engine project id (any GUID) is the documented alternative.
	const NVSDK_NGX_Result init = pApi->init( "6c3d9c52-8c1e-4b2a-9f4e-1b5a0d7e2c41", NVSDK_NGX_ENGINE_TYPE_CUSTOM, "Source SDK 2013 DX12",
	    pApi->dataPath, m_pDevice, NVSDK_NGX_Version_API, &info );
	char szText[160];
	if ( NVSDK_NGX_FAILED( init ) )
	{
		V_snprintf( szText, sizeof( szText ), "NVSDK_NGX_D3D12_Init_ProjectID failed (0x%08x)", static_cast<unsigned>( init ) );
		return fail( szText );
	}
	pApi->initialized = true;
	if ( NVSDK_NGX_FAILED( pApi->capabilities( &pApi->caps ) ) || !pApi->caps )
		return fail( "NVSDK_NGX_D3D12_GetCapabilityParameters failed" );
	char szVersion[64];
	FileVersion( szCoreDir, L"_nvngx.dll", szVersion, sizeof( szVersion ) );
	m_NgxCore.available = true;
	m_NgxCore.version = szVersion;
	m_pNgxApi = pApi;
	if ( !m_Dlss.reason.IsEmpty() )
		return false;
	unsigned int nAvailable = 0, nNeedsDriver = 0, nMinMajor = 0, nMinMinor = 0;
	pApi->caps->Get( NVSDK_NGX_Parameter_SuperSampling_Available, &nAvailable );
	pApi->caps->Get( NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, &nNeedsDriver );
	pApi->caps->Get( NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMajor, &nMinMajor );
	pApi->caps->Get( NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMinor, &nMinMinor );
	if ( nNeedsDriver )
	{
		V_snprintf( szText, sizeof( szText ), "DLSS needs driver %u.%u or newer", nMinMajor, nMinMinor );
		m_Dlss.reason = szText;
		return false;
	}
	if ( !nAvailable )
	{
		m_Dlss.reason = "NGX reports SuperSampling unavailable on this adapter";
		return false;
	}
	if ( NVSDK_NGX_FAILED( pApi->allocate( &pApi->dlssParams ) ) || !pApi->dlssParams )
	{
		m_Dlss.reason = "NVSDK_NGX_D3D12_AllocateParameters failed";
		return false;
	}
	FileVersion( pszDir, L"nvngx_dlss.dll", szVersion, sizeof( szVersion ) );
	m_Dlss.version.Format( "nvngx_dlss.dll %s, NGX core %s", szVersion, m_NgxCore.version.Get() );
	m_Dlss.available = true;
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Decides DLSS-NR availability from the installed files; the snippet itself loads on first use
//-----------------------------------------------------------------------------
void CUpscalerDX12::ProbeNr( const wchar_t *pszDir )
{
	if ( !m_NgxCore.available )
	{
		m_Nr.reason.Format( "NGX core unavailable: %s", m_NgxCore.reason.Get() );
		return;
	}
	if ( !FileExists( pszDir, L"nvngx_dlssnr.dll" ) )
	{
		m_Nr.reason = "nvngx_dlssnr.dll not installed beside the renderer";
		return;
	}
	if ( !FileExists( pszDir, L"nvngx.dll_dlssnr_dx12.dll" ) )
	{
		m_Nr.reason = "nvngx.dll_dlssnr_dx12.dll (snippet forwarder) not built beside the renderer";
		return;
	}
	char szVersion[64];
	FileVersion( pszDir, L"nvngx_dlssnr.dll", szVersion, sizeof( szVersion ) );
	m_Nr.version.Format( "nvngx_dlssnr.dll %s", szVersion );
	m_Nr.available = true;
}

//-----------------------------------------------------------------------------
// Purpose: Loads and initialises the DLSS-NR snippet through the forwarder; a failure disables DLSS-NR for good
//-----------------------------------------------------------------------------
bool CUpscalerDX12::InitNrSnippet()
{
	if ( m_pNrApi && m_pNrApi->initialized )
		return true;
	if ( !m_Nr.available )
		return false;
	const auto disable = [&]( const char *pszReason )
	{
		Warning( "ShaderAPIDX12 upscaler: DLSS-NR unavailable: %s\n", pszReason );
		if ( m_pNrApi && m_pNrApi->unload )
			m_pNrApi->unload();
		delete m_pNrApi;
		m_pNrApi = nullptr;
		if ( m_Nr.module )
			FreeLibrary( m_Nr.module );
		m_Nr.module = nullptr;
		m_Nr.available = false;
		m_Nr.reason = pszReason;
		return false;
	};
	m_Nr.module = LoadProviderModule( m_szDir, L"nvngx.dll_dlssnr_dx12.dll" );
	if ( !m_Nr.module )
		return disable( "nvngx.dll_dlssnr_dx12.dll failed to load" );
	m_pNrApi = new NrApi;
	if ( !ResolveExport( m_Nr.module, "NrFwd_Load", m_pNrApi->load ) || !ResolveExport( m_Nr.module, "NrFwd_Init", m_pNrApi->init ) || !ResolveExport( m_Nr.module, "NrFwd_CreateFeature", m_pNrApi->create ) ||
	    !ResolveExport( m_Nr.module, "NrFwd_EvaluateFeature", m_pNrApi->evaluate ) || !ResolveExport( m_Nr.module, "NrFwd_ReleaseFeature", m_pNrApi->release ) ||
	    !ResolveExport( m_Nr.module, "NrFwd_Shutdown", m_pNrApi->shutdown ) || !ResolveExport( m_Nr.module, "NrFwd_Unload", m_pNrApi->unload ) )
		return disable( "snippet forwarder lacks a required export" );
	wchar_t szSnippetPath[MAX_PATH];
	if ( !BuildPath( szSnippetPath, m_szDir, L"nvngx_dlssnr.dll" ) || !m_pNrApi->load( szSnippetPath ) )
		return disable( "nvngx_dlssnr.dll failed to load or lacks a D3D12 export" );
	// The snippet is initialised the way the NGX core initialises it: SDK 0x15 and the core's capability block.
	// m_Nr.available implies ProbeNr saw the NGX core available, so m_pNgxApi and its capability block exist.
	const int nInit = m_pNrApi->init( 0x24480451ull, m_pNgxApi->dataPath, m_pDevice, 0x0000015, m_pNgxApi->caps );
	if ( NVSDK_NGX_FAILED( static_cast<NVSDK_NGX_Result>( nInit ) ) )
	{
		char szText[96];
		V_snprintf( szText, sizeof( szText ), "nvngx_dlssnr.dll Init_Ext failed (0x%08x)", static_cast<unsigned>( nInit ) );
		return disable( szText );
	}
	m_pNrApi->initialized = true;
	Msg( "ShaderAPIDX12 upscaler: DLSS-NR snippet initialised (%s)\n", m_Nr.version.Get() );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Lazily builds the compute root signature and the DLSS-NR / depth-clone pipelines
//-----------------------------------------------------------------------------
bool CUpscalerDX12::EnsureCompute()
{
	if ( m_pComputeRoot && m_pNrPso && m_pDepthPso )
		return true;
	D3D12_DESCRIPTOR_RANGE ranges[2]{};
	ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	ranges[0].NumDescriptors = 4;
	ranges[0].OffsetInDescriptorsFromTableStart = 0;
	ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
	ranges[1].NumDescriptors = 2;
	ranges[1].OffsetInDescriptorsFromTableStart = 0;
	D3D12_ROOT_PARAMETER parameters[3]{};
	parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	parameters[0].Constants.Num32BitValues = sizeof( NrConstantsDX12 ) / 4;
	parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	parameters[1].DescriptorTable.NumDescriptorRanges = 1;
	parameters[1].DescriptorTable.pDescriptorRanges = &ranges[0];
	parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	parameters[2].DescriptorTable.NumDescriptorRanges = 1;
	parameters[2].DescriptorTable.pDescriptorRanges = &ranges[1];
	D3D12_STATIC_SAMPLER_DESC sampler{};
	sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.MaxLOD = D3D12_FLOAT32_MAX;
	D3D12_ROOT_SIGNATURE_DESC desc{};
	desc.NumParameters = 3;
	desc.pParameters = parameters;
	desc.NumStaticSamplers = 1;
	desc.pStaticSamplers = &sampler;
	Microsoft::WRL::ComPtr<ID3DBlob> blob, error, mainCode, depthCode;
	HRESULT hr = D3D12SerializeRootSignature( &desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error );
	if ( SUCCEEDED( hr ) )
		hr = m_pDevice->CreateRootSignature( 0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS( &m_pComputeRoot ) );
	if ( FAILED( hr ) )
	{
		Warning( "ShaderAPIDX12 upscaler: compute root signature failed (0x%08x)\n", static_cast<unsigned>( hr ) );
		return false;
	}
	for ( int pass = 0; pass < 2; ++pass )
	{
		Microsoft::WRL::ComPtr<ID3DBlob> &code = pass ? depthCode : mainCode;
		error.Reset();
		hr = D3DCompile( kNrShaderSourceDX12, sizeof( kNrShaderSourceDX12 ) - 1, "upscaler_nr", nullptr, nullptr, pass ? "CSDepth" : "CSMain", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &error );
		if ( FAILED( hr ) )
		{
			Warning( "ShaderAPIDX12 upscaler: compute shader compile failed (0x%08x): %s\n", static_cast<unsigned>( hr ), error ? static_cast<const char *>( error->GetBufferPointer() ) : "" );
			m_pComputeRoot.Reset();
			return false;
		}
		D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};
		pso.pRootSignature = m_pComputeRoot.Get();
		pso.CS = { code->GetBufferPointer(), code->GetBufferSize() };
		hr = m_pDevice->CreateComputePipelineState( &pso, IID_PPV_ARGS( pass ? &m_pDepthPso : &m_pNrPso ) );
		if ( FAILED( hr ) )
		{
			Warning( "ShaderAPIDX12 upscaler: compute PSO failed (0x%08x)\n", static_cast<unsigned>( hr ) );
			m_pComputeRoot.Reset();
			m_pNrPso.Reset();
			return false;
		}
	}
	m_nDescriptorStride = m_pDevice->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: (Re)creates the R32F depth clone at the given size
//-----------------------------------------------------------------------------
bool CUpscalerDX12::EnsureDepthClone( uint32_t nWidth, uint32_t nHeight )
{
	if ( m_pDepthClone )
	{
		const D3D12_RESOURCE_DESC desc = m_pDepthClone->GetDesc();
		if ( desc.Width == nWidth && desc.Height == nHeight )
			return true;
		m_pDepthClone.Reset(); // only reached behind the GPU-idle boundary that precedes feature/chain creation
	}
	m_pDepthClone = CreateTexture( m_pDevice, nWidth, nHeight, DXGI_FORMAT_R32_FLOAT, kNpsr );
	if ( !m_pDepthClone )
		Warning( "ShaderAPIDX12 upscaler: depth clone %ux%u creation failed\n", nWidth, nHeight );
	return m_pDepthClone != nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: Opens the private creation list (allocator, fence and list are created on first use)
//-----------------------------------------------------------------------------
ID3D12GraphicsCommandList *CUpscalerDX12::BeginImmediate()
{
	if ( !m_pImmediateAllocator && FAILED( m_pDevice->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS( &m_pImmediateAllocator ) ) ) )
		return nullptr;
	if ( !m_pImmediateFence && FAILED( m_pDevice->CreateFence( 0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS( &m_pImmediateFence ) ) ) )
		return nullptr;
	if ( FAILED( m_pImmediateAllocator->Reset() ) )
		return nullptr;
	if ( !m_pImmediateList )
	{
		if ( FAILED( m_pDevice->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_pImmediateAllocator.Get(), nullptr, IID_PPV_ARGS( &m_pImmediateList ) ) ) )
			return nullptr;
	}
	else if ( FAILED( m_pImmediateList->Reset( m_pImmediateAllocator.Get(), nullptr ) ) )
		return nullptr;
	return m_pImmediateList.Get();
}

//-----------------------------------------------------------------------------
// Purpose: Closes the creation list, executes it on `pQueue` and blocks until the GPU finished it
//-----------------------------------------------------------------------------
bool CUpscalerDX12::EndImmediate( ID3D12CommandQueue *pQueue )
{
	if ( !pQueue || FAILED( m_pImmediateList->Close() ) )
		return false;
	ID3D12CommandList *pLists[] = { m_pImmediateList.Get() };
	pQueue->ExecuteCommandLists( 1, pLists );
	if ( FAILED( pQueue->Signal( m_pImmediateFence.Get(), ++m_nImmediateValue ) ) )
		return false;
	if ( m_pImmediateFence->GetCompletedValue() < m_nImmediateValue )
	{
		HANDLE hEvent = CreateEventW( nullptr, FALSE, FALSE, nullptr );
		if ( !hEvent )
			return false;
		const bool bWaited = SUCCEEDED( m_pImmediateFence->SetEventOnCompletion( m_nImmediateValue, hEvent ) ) && WaitForSingleObject( hEvent, INFINITE ) == WAIT_OBJECT_0;
		CloseHandle( hEvent );
		if ( !bWaited )
			return false;
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Creates the provider context for `desc`; a failed desc is not retried until it changes
//-----------------------------------------------------------------------------
bool CUpscalerDX12::PrepareFeature( const UpscalerFeatureDescDX12 &desc, ID3D12CommandQueue *pQueue )
{
	if ( m_State == State::Ready )
		return m_Feature == desc; // a different feature needs ReleaseFeature behind a GPU-idle boundary first
	if ( m_State == State::Failed && m_Feature == desc )
		return false;
	if ( !m_pDevice || desc.kind == UpscalerKindDX12::None || !desc.width || !desc.height )
		return false;
	m_Feature = desc;
	bool bCreated = false;
	if ( desc.kind == UpscalerKindDX12::XeSS )
		bCreated = CreateXeSS( desc );
	else if ( desc.kind == UpscalerKindDX12::FSR )
		bCreated = CreateFfx( desc );
	else if ( desc.kind == UpscalerKindDX12::DLSS )
		bCreated = CreateDlss( desc, pQueue );
	m_State = bCreated ? State::Ready : State::Failed;
	const Provider &provider = desc.kind == UpscalerKindDX12::XeSS ? m_Xess : desc.kind == UpscalerKindDX12::FSR ? m_Fsr :
	                                                                                                               m_Dlss;
	if ( bCreated )
		Msg( "ShaderAPIDX12 upscaler: %s ready at %ux%u (%s)\n", UpscalerKindNameDX12( desc.kind ), desc.width, desc.height, provider.version.Get() );
	else
		Warning( "ShaderAPIDX12 upscaler: %s feature creation failed at %ux%u\n", UpscalerKindNameDX12( desc.kind ), desc.width, desc.height );
	return bCreated;
}

static void XeSSLog( const char *pszMessage, xess_logging_level_t level )
{
	if ( level >= XESS_LOGGING_LEVEL_WARNING )
		Warning( "ShaderAPIDX12 upscaler: XeSS: %s\n", pszMessage ? pszMessage : "" );
}

static void FfxLog( uint32_t nType, const wchar_t *pszMessage )
{
	Warning( "ShaderAPIDX12 upscaler: FSR %s: %ls\n", nType == FFX_API_MESSAGE_TYPE_ERROR ? "error" : "warning", pszMessage ? pszMessage : L"" );
}

//-----------------------------------------------------------------------------
// Purpose: XeSS AA context at native resolution
//-----------------------------------------------------------------------------
bool CUpscalerDX12::CreateXeSS( const UpscalerFeatureDescDX12 &desc )
{
	if ( !m_pXessApi )
		return false;
	xess_context_handle_t hContext = nullptr;
	xess_result_t result = m_pXessApi->createContext( m_pDevice, &hContext );
	if ( result != XESS_RESULT_SUCCESS || !hContext )
	{
		Warning( "ShaderAPIDX12 upscaler: xessD3D12CreateContext failed (%d)\n", static_cast<int>( result ) );
		return false;
	}
	const auto fail = [&]( const char *pszWhat, int nCode )
	{
		Warning( "ShaderAPIDX12 upscaler: %s failed (%d)\n", pszWhat, nCode );
		m_pXessApi->destroyContext( hContext );
		return false;
	};
	if ( m_pXessApi->setLogging )
		m_pXessApi->setLogging( hContext, XESS_LOGGING_LEVEL_WARNING, XeSSLog );
	xess_d3d12_init_params_t init{};
	init.outputResolution = { desc.width, desc.height };
	init.qualitySetting = XESS_QUALITY_SETTING_AA;
	// Linear scRGB input (no LDR flag), output-resolution jitter-free motion vectors (no JITTERED_MV flag).
	init.initFlags = XESS_INIT_FLAG_HIGH_RES_MV | XESS_INIT_FLAG_ENABLE_AUTOEXPOSURE | ( desc.depthInverted ? XESS_INIT_FLAG_INVERTED_DEPTH : 0 );
	result = m_pXessApi->init( hContext, &init );
	if ( result != XESS_RESULT_SUCCESS )
		return fail( "xessD3D12Init", result );
	xess_2d_t output{ desc.width, desc.height }, input{};
	result = m_pXessApi->getInputResolution( hContext, &output, XESS_QUALITY_SETTING_AA, &input );
	if ( result != XESS_RESULT_SUCCESS )
		return fail( "xessGetInputResolution", result );
	if ( input.x != desc.width || input.y != desc.height )
		return fail( "XeSS AA native input resolution check", static_cast<int>( input.x ) );
	// _rt_MotionVectors holds current-minus-previous UV; XeSS wants previous-minus-current pixels.
	result = m_pXessApi->setVelocityScale( hContext, -static_cast<float>( desc.width ), -static_cast<float>( desc.height ) );
	if ( result != XESS_RESULT_SUCCESS )
		return fail( "xessSetVelocityScale", result );
	if ( m_pXessApi->setJitterScale )
	{
		result = m_pXessApi->setJitterScale( hContext, 1.f, 1.f );
		if ( result != XESS_RESULT_SUCCESS )
			return fail( "xessSetJitterScale", result );
		float flJitterX = 0, flJitterY = 0;
		if ( m_pXessApi->getJitterScale && m_pXessApi->getJitterScale( hContext, &flJitterX, &flJitterY ) == XESS_RESULT_SUCCESS )
			Msg( "ShaderAPIDX12 upscaler: XeSS effective jitter scale %.2f %.2f\n", flJitterX, flJitterY );
	}
	m_pXessContext = hContext;
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: FSR native-AA context on the provider chosen in LoadFfx, plus its jitter sequence
//-----------------------------------------------------------------------------
bool CUpscalerDX12::CreateFfx( const UpscalerFeatureDescDX12 &desc )
{
	if ( !m_pFfxApi )
		return false;
	// Chain: upscale -> DX12 backend -> API version (required by v2) -> provider override (the enumerated id).
	ffxOverrideVersion override{};
	override.header.type = FFX_API_DESC_TYPE_OVERRIDE_VERSION;
	override.versionId = m_Fsr.versionId;
	ffxCreateContextDescUpscaleVersion version{};
	version.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE_VERSION;
	version.version = FFX_UPSCALER_VERSION;
	version.header.pNext = &override.header;
	ffxCreateBackendDX12Desc backend{};
	backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
	backend.device = m_pDevice;
	backend.header.pNext = &version.header;
	ffxCreateContextDescUpscale create{};
	create.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
	create.header.pNext = &backend.header;
	create.flags = FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE | FFX_UPSCALE_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS | FFX_UPSCALE_ENABLE_AUTO_EXPOSURE |
	    ( desc.depthInverted ? FFX_UPSCALE_ENABLE_DEPTH_INVERTED : 0 ) | ( m_bVerbose ? FFX_UPSCALE_ENABLE_DEBUG_CHECKING : 0 );
	create.maxRenderSize = { desc.width, desc.height };
	create.maxUpscaleSize = { desc.width, desc.height };
	create.fpMessage = FfxLog;
	if ( m_pFfxApi->fsr4 )
		Warning( "ShaderAPIDX12 upscaler: creating an FSR 4 context; this path is UNTESTED (no RDNA3/RDNA4 GPU was available)\n" );
	ffxContext context = nullptr;
	const ffxReturnCode_t created = m_pFfxApi->createContext( &context, &create.header, nullptr );
	if ( created != FFX_API_RETURN_OK || !context )
	{
		Warning( "ShaderAPIDX12 upscaler: ffxCreateContext(upscale) failed (%u)\n", created );
		return false;
	}
	ffxQueryGetProviderVersion provider{};
	provider.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
	if ( m_pFfxApi->query( &context, &provider.header ) == FFX_API_RETURN_OK && provider.versionName )
		Msg( "ShaderAPIDX12 upscaler: FSR context provider %s\n", provider.versionName );
	// Native AA: render size == display size; the provider supplies its phase count and offsets.
	int32_t nPhases = 0;
	ffxQueryDescUpscaleGetJitterPhaseCount phaseQuery{};
	phaseQuery.header.type = FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTERPHASECOUNT;
	phaseQuery.renderWidth = desc.width;
	phaseQuery.displayWidth = desc.width;
	phaseQuery.pOutPhaseCount = &nPhases;
	if ( m_pFfxApi->query( &context, &phaseQuery.header ) != FFX_API_RETURN_OK || nPhases <= 0 || nPhases > 1024 )
	{
		Warning( "ShaderAPIDX12 upscaler: FSR jitter phase query failed\n" );
		m_pFfxApi->destroyContext( &context, nullptr );
		return false;
	}
	m_FfxJitter.SetCount( nPhases * 2 );
	m_FfxJitter.FillWithValue( 0.f );
	for ( int32_t i = 0; i < nPhases; ++i )
	{
		ffxQueryDescUpscaleGetJitterOffset offset{};
		offset.header.type = FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTEROFFSET;
		offset.index = i;
		offset.phaseCount = nPhases;
		offset.pOutX = &m_FfxJitter[i * 2];
		offset.pOutY = &m_FfxJitter[i * 2 + 1];
		if ( m_pFfxApi->query( &context, &offset.header ) != FFX_API_RETURN_OK )
		{
			Warning( "ShaderAPIDX12 upscaler: FSR jitter offset query failed\n" );
			m_FfxJitter.RemoveAll();
			m_pFfxApi->destroyContext( &context, nullptr );
			return false;
		}
	}
	if ( m_bVerbose )
		Msg( "ShaderAPIDX12 upscaler: FSR jitter phases %d\n", nPhases );
	m_pFfxContext = context;
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: DLAA feature, created on the private list and waited for
//-----------------------------------------------------------------------------
bool CUpscalerDX12::CreateDlss( const UpscalerFeatureDescDX12 &desc, ID3D12CommandQueue *pQueue )
{
	if ( !m_pNgxApi || !m_pNgxApi->dlssParams || !EnsureCompute() || !EnsureDepthClone( desc.width, desc.height ) )
		return false;
	NVSDK_NGX_Parameter *p = m_pNgxApi->dlssParams;
	p->Set( NVSDK_NGX_Parameter_CreationNodeMask, 1u );
	p->Set( NVSDK_NGX_Parameter_VisibilityNodeMask, 1u );
	p->Set( NVSDK_NGX_Parameter_Width, desc.width );
	p->Set( NVSDK_NGX_Parameter_Height, desc.height );
	p->Set( NVSDK_NGX_Parameter_OutWidth, desc.width );
	p->Set( NVSDK_NGX_Parameter_OutHeight, desc.height );
	p->Set( NVSDK_NGX_Parameter_PerfQualityValue, static_cast<int>( NVSDK_NGX_PerfQuality_Value_DLAA ) );
	p->Set( NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA, static_cast<unsigned int>( NVSDK_NGX_DLSS_Hint_Render_Preset_Default ) );
	p->Set( NVSDK_NGX_Parameter_DLSS_Enable_Output_Subrects, 0 );
	// Linear scRGB, full-resolution motion vectors without jitter, automatic exposure.
	const int nFlags = NVSDK_NGX_DLSS_Feature_Flags_IsHDR | NVSDK_NGX_DLSS_Feature_Flags_AutoExposure | ( desc.depthInverted ? NVSDK_NGX_DLSS_Feature_Flags_DepthInverted : 0 );
	p->Set( NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags, nFlags );
	ID3D12GraphicsCommandList *pList = BeginImmediate();
	if ( !pList )
		return false;
	NVSDK_NGX_Handle *pHandle = nullptr;
	const NVSDK_NGX_Result result = m_pNgxApi->create( pList, NVSDK_NGX_Feature_SuperSampling, p, &pHandle );
	// The creation list completes before any recorded frame can evaluate the feature.
	const bool bExecuted = EndImmediate( pQueue );
	if ( NVSDK_NGX_FAILED( result ) || !pHandle || !bExecuted )
	{
		Warning( "ShaderAPIDX12 upscaler: NVSDK_NGX_D3D12_CreateFeature(SuperSampling) failed (0x%08x)\n", static_cast<unsigned>( result ) );
		if ( pHandle )
			m_pNgxApi->release( pHandle );
		return false;
	}
	m_pDlssHandle = pHandle;
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Jitter offset for frame `nIndex` from the active provider's preferred sequence
//-----------------------------------------------------------------------------
bool CUpscalerDX12::ProviderJitter( uint64_t nIndex, float &flX, float &flY ) const
{
	if ( m_State != State::Ready )
		return false;
	if ( m_Feature.kind == UpscalerKindDX12::DLSS )
	{
		// DLAA converges better on a longer sequence than the backend's default eight phases.
		const uint32_t nPhase = static_cast<uint32_t>( nIndex % 32 ) + 1;
		flX = Halton( nPhase, 2 ) - .5f;
		flY = Halton( nPhase, 3 ) - .5f;
		return true;
	}
	if ( m_Feature.kind != UpscalerKindDX12::FSR || m_FfxJitter.Count() < 2 )
		return false;
	const int nPhase = static_cast<int>( nIndex % static_cast<uint64_t>( m_FfxJitter.Count() / 2 ) );
	flX = m_FfxJitter[nPhase * 2];
	flY = m_FfxJitter[nPhase * 2 + 1];
	return true;
}

//-----------------------------------------------------------------------------
// DLSS-NR chain accessors
//-----------------------------------------------------------------------------
bool CUpscalerDX12::NrReady( const DlssNrTuningDX12 &tuning, uint32_t nLayers, ID3D12Resource *pSource ) const
{
	return m_pNrChain && m_pNrChain->layers == nLayers && m_pNrChain->tuning == tuning && m_pNrChain->source == pSource;
}

uint32_t CUpscalerDX12::NrLayers() const
{
	return m_pNrChain ? m_pNrChain->layers : 0;
}

ID3D12Resource *CUpscalerDX12::NrResult() const
{
	return m_pNrChain ? m_pNrChain->out[( m_pNrChain->layers - 1 ) & 1].Get() : nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: Creates the chain textures and one DLSS-NR feature per layer on the private list
//-----------------------------------------------------------------------------
bool CUpscalerDX12::PrepareNr( const DlssNrTuningDX12 &tuning, uint32_t nLayers, ID3D12Resource *pSource, ID3D12CommandQueue *pQueue )
{
	if ( NrReady( tuning, nLayers, pSource ) )
		return true;
	// The only caller (PrepareUpscalerNr) passes the live upscaler output and 1..kNrMaxLayersDX12 layers; the bound
	// still guards the fixed handles[] array.
	if ( m_pNrChain || nLayers > kNrMaxLayersDX12 || !InitNrSnippet() || !EnsureCompute() )
		return false;
	const D3D12_RESOURCE_DESC sourceDesc = pSource->GetDesc();
	const uint32_t nWidth = static_cast<uint32_t>( sourceDesc.Width ), nHeight = sourceDesc.Height;
	// At 64x64 the model creates, then faults the GPU on its first evaluate (device removed). 320x180 is the smallest
	// size verified to run; every game mode is larger.
	if ( nWidth < kNrMinWidth || nHeight < kNrMinHeight )
	{
		Warning( "ShaderAPIDX12 upscaler: DLSS-NR needs at least %ux%u (scene is %ux%u)\n", kNrMinWidth, kNrMinHeight, nWidth, nHeight );
		return false;
	}
	if ( !EnsureDepthClone( nWidth, nHeight ) )
		return false;
	NrChain *pChain = new NrChain;
	pChain->tuning = tuning;
	pChain->layers = nLayers;
	pChain->width = nWidth;
	pChain->height = nHeight;
	pChain->source = pSource;
	// The model reads an sRGB-encoded display-referred proxy; it is stored in the scene's FP16 format as the fork does.
	pChain->proxy = CreateTexture( m_pDevice, nWidth, nHeight, sourceDesc.Format, kNpsr );
	pChain->answer = CreateTexture( m_pDevice, nWidth, nHeight, sourceDesc.Format, kUav );
	pChain->out[0] = CreateTexture( m_pDevice, nWidth, nHeight, sourceDesc.Format, kUav );
	if ( nLayers > 1 )
		pChain->out[1] = CreateTexture( m_pDevice, nWidth, nHeight, sourceDesc.Format, kUav );
	if ( !pChain->proxy || !pChain->answer || !pChain->out[0] || ( nLayers > 1 && !pChain->out[1] ) )
	{
		Warning( "ShaderAPIDX12 upscaler: DLSS-NR chain textures %ux%u failed\n", nWidth, nHeight );
		delete pChain;
		return false;
	}
	NVSDK_NGX_Parameter *p = m_pNgxApi->caps;
	ID3D12GraphicsCommandList *pList = BeginImmediate();
	if ( !pList )
	{
		delete pChain;
		return false;
	}
	// Tuning is read only at creation; the block outlives the features, so every value is written each time.
	int nResult = NVSDK_NGX_Result_Success;
	for ( uint32_t nLayer = 0; nLayer < nLayers && NVSDK_NGX_SUCCEED( static_cast<NVSDK_NGX_Result>( nResult ) ); ++nLayer )
	{
		p->Set( "DLSSNR.Enabled", 1u );
		p->Set( "DLSSNR.Width", nWidth );
		p->Set( "DLSSNR.Height", nHeight );
		p->Set( NVSDK_NGX_Parameter_CreationNodeMask, 1u );
		p->Set( NVSDK_NGX_Parameter_VisibilityNodeMask, 1u );
		p->Set( "DLSSNR.Hint.Render.Preset", tuning.preset );
		p->Set( "DLSSNR.Intensity", tuning.intensity );
		p->Set( "DLSSNR.Style", tuning.style );
		p->Set( "DLSSNR.LocalStructureStrength", tuning.localStructure );
		p->Set( "DLSSNR.LocalToneStrength", tuning.localTone );
		p->Set( "DLSSNR.SkinStructureStrength", tuning.skinStructure );
		p->Set( "DLSSNR.UseAutoMask", tuning.autoMask ? 1u : 0u );
		p->Set( "DLSSNR.UICorrection", tuning.uiCorrection ? 1u : 0u );
		void *pHandle = nullptr;
		nResult = m_pNrApi->create( pList, 18, p, &pHandle );
		if ( NVSDK_NGX_SUCCEED( static_cast<NVSDK_NGX_Result>( nResult ) ) && !pHandle )
			nResult = NVSDK_NGX_Result_Fail;
		pChain->handles[nLayer] = pHandle;
	}
	// Creation completes on its own list before any recorded frame evaluates a layer.
	const bool bExecuted = EndImmediate( pQueue );
	if ( NVSDK_NGX_FAILED( static_cast<NVSDK_NGX_Result>( nResult ) ) || !bExecuted )
	{
		Warning( "ShaderAPIDX12 upscaler: DLSS-NR feature creation failed (0x%08x)\n", static_cast<unsigned>( nResult ) );
		for ( void *pHandle : pChain->handles )
			if ( pHandle )
				m_pNrApi->release( pHandle );
		delete pChain;
		return false;
	}
	m_pNrChain = pChain;
	Msg( "ShaderAPIDX12 upscaler: DLSS-NR %u layer%s ready at %ux%u (preset %u, style %u, intensity %.2f)\n", nLayers, nLayers == 1 ? "" : "s", nWidth, nHeight, tuning.preset, tuning.style, tuning.intensity );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Releases every layer feature and the chain textures (GPU-idle precondition)
//-----------------------------------------------------------------------------
void CUpscalerDX12::ReleaseNr()
{
	if ( !m_pNrChain )
		return;
	// A chain only exists after InitNrSnippet succeeded, and m_pNrApi outlives it (Shutdown releases features first).
	for ( void *pHandle : m_pNrChain->handles )
		if ( pHandle )
			m_pNrApi->release( pHandle );
	delete m_pNrChain;
	m_pNrChain = nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: Writes the depth-clone and DLSS-NR pass descriptors into the dispatch's shader-visible block
//-----------------------------------------------------------------------------
void CUpscalerDX12::WriteDescriptors( const UpscalerDispatchDX12 &d ) const
{
	ID3D12Device *device = m_pDevice;
	const auto slot = [&]( uint32_t pass, uint32_t index )
	{
		D3D12_CPU_DESCRIPTOR_HANDLE h = d.cpu;
		h.ptr += static_cast<SIZE_T>( pass * kPassDescriptors + index ) * m_nDescriptorStride;
		return h;
	};
	const auto srv = [&]( uint32_t pass, uint32_t index, ID3D12Resource *resource, DXGI_FORMAT format )
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC v{};
		v.Format = format;
		v.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		v.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		v.Texture2D.MipLevels = 1;
		device->CreateShaderResourceView( resource, &v, slot( pass, index ) );
	};
	const auto uav = [&]( uint32_t pass, uint32_t index, ID3D12Resource *resource, DXGI_FORMAT format )
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC v{};
		v.Format = format;
		v.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		device->CreateUnorderedAccessView( resource, nullptr, &v, slot( pass, index ) );
	};
	const DXGI_FORMAT color = d.output->GetDesc().Format;
	const auto pass = [&]( uint32_t index, ID3D12Resource *t0, DXGI_FORMAT f0, ID3D12Resource *t1, ID3D12Resource *t2, ID3D12Resource *u0, ID3D12Resource *u1 )
	{
		srv( index, 0, t0, f0 );
		srv( index, 1, t1, color );
		srv( index, 2, t2, color );
		srv( index, 3, nullptr, color );
		uav( index, 4, u0, color );
		uav( index, 5, u1, DXGI_FORMAT_R32_FLOAT );
	};
	// Pass 0: scene depth -> typed R32F clone.
	pass( 0, NeedsDepthClone( d ) ? d.depth : nullptr, DXGI_FORMAT_R24_UNORM_X8_TYPELESS, nullptr, nullptr, nullptr, NeedsDepthClone( d ) ? m_pDepthClone.Get() : nullptr );
	for ( uint32_t layer = 0; layer < d.nrLayers; ++layer )
	{
		ID3D12Resource *input = layer ? m_pNrChain->out[( layer - 1 ) & 1].Get() : d.output;
		ID3D12Resource *output = m_pNrChain->out[layer & 1].Get();
		pass( 1 + layer * 2, input, color, nullptr, nullptr, m_pNrChain->proxy.Get(), nullptr );                 // encode
		pass( 2 + layer * 2, m_pNrChain->proxy.Get(), color, m_pNrChain->answer.Get(), input, output, nullptr ); // resolve
	}
}

//-----------------------------------------------------------------------------
// Purpose: Writes the dispatch's descriptors and records the replay callback as one external command
//-----------------------------------------------------------------------------
bool CUpscalerDX12::RecordDispatch( CCommandRecorderDX12 &recorder, const UpscalerDispatchDX12 &dispatch )
{
	if ( m_State != State::Ready )
		return false;
	if ( dispatch.nrLayers && ( !m_pNrChain || m_pNrChain->layers != dispatch.nrLayers || m_pNrChain->source != dispatch.output ) )
		return false;
	if ( NeedsDescriptors( dispatch ) )
	{
		if ( !dispatch.heap || !dispatch.cpu.ptr || !m_pComputeRoot || ( NeedsDepthClone( dispatch ) && !m_pDepthClone ) )
			return false;
		WriteDescriptors( dispatch );
	}
	const UpscalerReplayPayloadDX12 payload{ this, dispatch };
	recorder.ExternalCommand( &CUpscalerDX12::ReplayThunk, payload );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Binds the compute root/pipeline/tables for pass `nPass` and dispatches over the scene
//-----------------------------------------------------------------------------
void CUpscalerDX12::BindCompute( ID3D12GraphicsCommandList *pList, const UpscalerDispatchDX12 &d, ID3D12PipelineState *pPso, uint32_t nPass, const NrConstantsDX12 &constants ) const
{
	pList->SetComputeRootSignature( m_pComputeRoot.Get() );
	ID3D12DescriptorHeap *pHeap = d.heap;
	pList->SetDescriptorHeaps( 1, &pHeap );
	pList->SetPipelineState( pPso );
	D3D12_GPU_DESCRIPTOR_HANDLE srvs = d.gpu;
	srvs.ptr += static_cast<UINT64>( nPass * kPassDescriptors ) * m_nDescriptorStride;
	D3D12_GPU_DESCRIPTOR_HANDLE uavs = srvs;
	uavs.ptr += static_cast<UINT64>( 4 ) * m_nDescriptorStride;
	pList->SetComputeRoot32BitConstants( 0, sizeof( constants ) / 4, &constants, 0 );
	pList->SetComputeRootDescriptorTable( 1, srvs );
	pList->SetComputeRootDescriptorTable( 2, uavs );
	pList->Dispatch( ( d.width + 7 ) / 8, ( d.height + 7 ) / 8, 1 );
}

//-----------------------------------------------------------------------------
// Purpose: Worker-side replay: transitions, depth clone, provider evaluate, DLSS-NR chain, copy back, result publish
//-----------------------------------------------------------------------------
void CUpscalerDX12::ReplayThunk( ID3D12GraphicsCommandList *pList, ID3D12Device *, const void *pPayload ) noexcept
{
	UpscalerReplayPayloadDX12 payload;
	memcpy( &payload, pPayload, sizeof( payload ) );
	const UpscalerDispatchDX12 &d = payload.dispatch;
	const CUpscalerDX12 &owner = *payload.owner;
	// XeSS with high-resolution motion vectors takes no depth; FSR reads it through its R24 SRV mapping; DLSS and
	// DLSS-NR read the typed clone written from it here.
	const bool bClone = owner.NeedsDepthClone( d );
	const bool bDepthInput = owner.m_Feature.kind == UpscalerKindDX12::FSR || bClone;
	const D3D12_RESOURCE_STATES depthDuring = bDepthInput ? kRead : D3D12_RESOURCE_STATE_DEPTH_WRITE;
	D3D12_RESOURCE_BARRIER barriers[8];
	UINT nCount = 0;
	AddTransition( d.color, d.colorBefore, kRead, barriers, nCount );
	AddTransition( d.depth, d.depthBefore, depthDuring, barriers, nCount );
	AddTransition( d.motion, d.motionBefore, kRead, barriers, nCount );
	AddTransition( d.output, d.outputBefore, kUav, barriers, nCount );
	AddTransition( bClone ? owner.m_pDepthClone.Get() : nullptr, kNpsr, kUav, barriers, nCount );
	if ( nCount )
		pList->ResourceBarrier( nCount, barriers );
	if ( bClone )
	{
		NrConstantsDX12 constants{};
		constants.width = d.width;
		constants.height = d.height;
		owner.BindCompute( pList, d, owner.m_pDepthPso.Get(), 0, constants );
		Transition( pList, owner.m_pDepthClone.Get(), kUav, kNpsr );
	}
	const uint32_t nCode = d.forceFailure ? kReplayForced : owner.Execute( pList, d );
	uint32_t nNrCode = 0;
	ID3D12Resource *pResult = d.output;
	if ( !nCode && d.nrLayers )
	{
		nNrCode = owner.ExecuteNr( pList, d );
		if ( !nNrCode )
			pResult = owner.NrResult();
	}
	nCount = 0;
	if ( !nCode )
	{
		// The temporal-AA output, or the last DLSS-NR layer when the chain ran; both rest in UNORDERED_ACCESS.
		AddTransition( pResult, kUav, D3D12_RESOURCE_STATE_COPY_SOURCE, barriers, nCount );
		AddTransition( d.color, kRead, D3D12_RESOURCE_STATE_COPY_DEST, barriers, nCount );
		pList->ResourceBarrier( nCount, barriers );
		nCount = 0;
		pList->CopyResource( d.color, pResult );
		AddTransition( pResult, D3D12_RESOURCE_STATE_COPY_SOURCE, kUav, barriers, nCount );
		AddTransition( d.color, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET, barriers, nCount );
	}
	else
		AddTransition( d.color, kRead, D3D12_RESOURCE_STATE_RENDER_TARGET, barriers, nCount );
	AddTransition( d.depth, depthDuring, D3D12_RESOURCE_STATE_DEPTH_WRITE, barriers, nCount );
	AddTransition( d.motion, kRead, D3D12_RESOURCE_STATE_RENDER_TARGET, barriers, nCount );
	if ( nCount )
		pList->ResourceBarrier( nCount, barriers );
	// The owner reads `serial` before `code`/`nrCode`/`frame`; each store is an interlocked exchange (full barrier),
	// so `serial` is published last.
	d.result->code = nCode;
	d.result->nrCode = nNrCode;
	d.result->frame = d.frame;
	d.result->serial = d.serial;
}

//-----------------------------------------------------------------------------
// Purpose: Runs every DLSS-NR layer (encode, model, resolve) on the worker's list
//-----------------------------------------------------------------------------
uint32_t CUpscalerDX12::ExecuteNr( ID3D12GraphicsCommandList *pList, const UpscalerDispatchDX12 &d ) const noexcept
{
	// A chain implies InitNrSnippet succeeded: m_pNrApi and the NGX capability block outlive it.
	if ( !m_pNrChain )
		return kReplayNoContext;
	NVSDK_NGX_Parameter *p = m_pNgxApi->caps;
	const NrChain &chain = *m_pNrChain;
	// Layer N reads layer N-1's output (layer 0 reads the temporal-AA output). Every resource returns to its resting
	// state -- proxy NON_PIXEL_SHADER_RESOURCE, the rest UNORDERED_ACCESS -- before an evaluate failure returns.
	for ( uint32_t nLayer = 0; nLayer < chain.layers; ++nLayer )
	{
		ID3D12Resource *pInput = nLayer ? chain.out[( nLayer - 1 ) & 1].Get() : d.output;
		NrConstantsDX12 constants{};
		constants.width = d.width;
		constants.height = d.height;
		constants.whitePoint = d.nr.whitePoint;
		constants.transferStrength = d.nr.transferStrength;
		constants.colourStrength = d.nr.colourStrength;
		constants.maxRatio = d.nr.maxRatio;
		constants.reversibleMode = d.nr.reversibleMode;
		constants.debugView = d.nr.debugView;
		constants.applyModel = d.nr.applyModel ? 1u : 0u;
		constants.compareMode = d.nr.compareMode;
		constants.compareSplit = d.nr.compareSplit;
		constants.compareZoom = d.nr.compareZoom;
		constants.compareSwap = d.nr.compareSwap ? 1u : 0u;
		// Encode: input -> proxy.
		D3D12_RESOURCE_BARRIER barriers[3];
		UINT nCount = 0;
		AddTransition( pInput, kUav, kNpsr, barriers, nCount );
		AddTransition( chain.proxy.Get(), kNpsr, kUav, barriers, nCount );
		pList->ResourceBarrier( nCount, barriers );
		constants.mode = 0;
		BindCompute( pList, d, m_pNrPso.Get(), 1 + nLayer * 2, constants );
		Transition( pList, chain.proxy.Get(), kUav, kNpsr );
		// The model: proxy + depth clone + motion -> answer.
		p->Set( "DLSSNR.Color", chain.proxy.Get() );
		p->Set( "DLSSNR.Depth", m_pDepthClone.Get() );
		p->Set( "DLSSNR.MVec", d.motion );
		p->Set( "DLSSNR.Output", chain.answer.Get() );
		p->Set( "DLSSNR.Enabled", 1u );
		p->Set( "DLSSNR.Width", d.width );
		p->Set( "DLSSNR.Height", d.height );
		p->Set( "DLSSNR.DepthInverted", m_Feature.depthInverted ? 1u : 0u );
		p->Set( "DLSSNR.Reset", d.nrReset ? 1u : 0u );
		const char *const pszPrefixes[] = { "DLSSNR.Color", "DLSSNR.Output", "DLSSNR.Depth", "DLSSNR.MVec" };
		for ( size_t i = 0; i < ARRAYSIZE( pszPrefixes ); ++i )
		{
			char szName[64];
			V_snprintf( szName, sizeof( szName ), "%sSubrectBaseX", pszPrefixes[i] );
			p->Set( szName, 0u );
			V_snprintf( szName, sizeof( szName ), "%sSubrectBaseY", pszPrefixes[i] );
			p->Set( szName, 0u );
			V_snprintf( szName, sizeof( szName ), "%sSubrectWidth", pszPrefixes[i] );
			p->Set( szName, d.width );
			V_snprintf( szName, sizeof( szName ), "%sSubrectHeight", pszPrefixes[i] );
			p->Set( szName, d.height );
		}
		p->Set( "DLSSNR.MVecScaleX", d.motionScale[0] );
		p->Set( "DLSSNR.MVecScaleY", d.motionScale[1] );
		const DlssNrTuningDX12 &t = chain.tuning;
		p->Set( "DLSSNR.Intensity", t.intensity );
		p->Set( "DLSSNR.Style", t.style );
		p->Set( "DLSSNR.LocalStructureStrength", t.localStructure );
		p->Set( "DLSSNR.LocalToneStrength", t.localTone );
		p->Set( "DLSSNR.SkinStructureStrength", t.skinStructure );
		p->Set( "DLSSNR.UseAutoMask", t.autoMask ? 1u : 0u );
		const int nResult = m_pNrApi->evaluate( pList, chain.handles[nLayer], p );
		if ( NVSDK_NGX_FAILED( static_cast<NVSDK_NGX_Result>( nResult ) ) )
		{
			Transition( pList, pInput, kNpsr, kUav );
			return 0x40000u | ( static_cast<uint32_t>( nResult ) & 0xffffu );
		}
		// Resolve: proxy + answer + input -> output.
		Transition( pList, chain.answer.Get(), kUav, kNpsr );
		constants.mode = 1;
		BindCompute( pList, d, m_pNrPso.Get(), 2 + nLayer * 2, constants );
		nCount = 0;
		AddTransition( chain.answer.Get(), kNpsr, kUav, barriers, nCount );
		AddTransition( pInput, kNpsr, kUav, barriers, nCount );
		pList->ResourceBarrier( nCount, barriers );
	}
	return 0;
}

//-----------------------------------------------------------------------------
// Purpose: Evaluates the active temporal-AA provider; 0 on success, otherwise a folded failure code
//-----------------------------------------------------------------------------
uint32_t CUpscalerDX12::Execute( ID3D12GraphicsCommandList *pList, const UpscalerDispatchDX12 &d ) const noexcept
{
	switch ( m_Feature.kind )
	{
	case UpscalerKindDX12::XeSS:
	{
		if ( !m_pXessContext )
			return kReplayNoContext;
		xess_d3d12_execute_params_t params{};
		params.pColorTexture = d.color;
		params.pVelocityTexture = d.motion;
		params.pDepthTexture = nullptr;
		params.pOutputTexture = d.output;
		params.jitterOffsetX = d.jitter[0];
		params.jitterOffsetY = d.jitter[1];
		params.exposureScale = 1.f;
		params.resetHistory = d.reset ? 1u : 0u;
		params.inputWidth = d.width;
		params.inputHeight = d.height;
		const xess_result_t result = m_pXessApi->execute( static_cast<xess_context_handle_t>( m_pXessContext ), pList, &params );
		return result == XESS_RESULT_SUCCESS ? 0u : 0x20000u | ( static_cast<uint32_t>( -static_cast<int>( result ) ) & 0xffffu );
	}
	case UpscalerKindDX12::FSR:
	{
		if ( !m_pFfxContext )
			return kReplayNoContext;
		ffxDispatchDescUpscale dispatch{};
		dispatch.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
		dispatch.commandList = pList;
		dispatch.color = ffxApiGetResourceDX12( d.color, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ );
		dispatch.depth = ffxApiGetResourceDX12( d.depth, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ );
		dispatch.motionVectors = ffxApiGetResourceDX12( d.motion, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ );
		dispatch.output = ffxApiGetResourceDX12( d.output, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS );
		dispatch.jitterOffset = { d.jitter[0], d.jitter[1] };
		dispatch.motionVectorScale = { d.motionScale[0], d.motionScale[1] };
		dispatch.renderSize = { d.width, d.height };
		dispatch.upscaleSize = { d.width, d.height };
		dispatch.enableSharpening = false;
		dispatch.sharpness = 0.f;
		dispatch.frameTimeDelta = d.frameTimeMs;
		dispatch.preExposure = 1.f;
		dispatch.reset = d.reset;
		dispatch.cameraNear = d.cameraNear;
		dispatch.cameraFar = d.cameraFar;
		dispatch.cameraFovAngleVertical = d.fovY;
		dispatch.viewSpaceToMetersFactor = 0.0254f; // Source units are inches
		ffxContext context = m_pFfxContext;
		const ffxReturnCode_t result = m_pFfxApi->dispatch( &context, &dispatch.header );
		return result == FFX_API_RETURN_OK ? 0u : 0x30000u | ( result & 0xffffu );
	}
	case UpscalerKindDX12::DLSS:
	{
		// A DLSS handle implies CreateDlss found m_pNgxApi and its dlssParams block.
		if ( !m_pDlssHandle )
			return kReplayNoContext;
		NVSDK_NGX_Parameter *p = m_pNgxApi->dlssParams;
		p->Set( NVSDK_NGX_Parameter_Color, d.color );
		p->Set( NVSDK_NGX_Parameter_Output, d.output );
		p->Set( NVSDK_NGX_Parameter_Depth, m_pDepthClone.Get() );
		p->Set( NVSDK_NGX_Parameter_MotionVectors, d.motion );
		p->Set( NVSDK_NGX_Parameter_Jitter_Offset_X, d.jitter[0] );
		p->Set( NVSDK_NGX_Parameter_Jitter_Offset_Y, d.jitter[1] );
		p->Set( NVSDK_NGX_Parameter_MV_Scale_X, d.motionScale[0] );
		p->Set( NVSDK_NGX_Parameter_MV_Scale_Y, d.motionScale[1] );
		p->Set( NVSDK_NGX_Parameter_Reset, d.reset ? 1 : 0 );
		p->Set( NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, d.width );
		p->Set( NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, d.height );
		p->Set( NVSDK_NGX_Parameter_Sharpness, 0.f );
		p->Set( NVSDK_NGX_Parameter_DLSS_Pre_Exposure, 1.f );
		p->Set( NVSDK_NGX_Parameter_DLSS_Exposure_Scale, 1.f );
		p->Set( NVSDK_NGX_Parameter_FrameTimeDeltaInMsec, d.frameTimeMs );
		const NVSDK_NGX_Result result = m_pNgxApi->evaluate( pList, static_cast<const NVSDK_NGX_Handle *>( m_pDlssHandle ), p, nullptr );
		return NVSDK_NGX_SUCCEED( result ) ? 0u : 0x50000u | ( static_cast<uint32_t>( result ) & 0xffffu );
	}
	default:
		return kReplayNoContext;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Destroys the provider context and the DLSS-NR chain (GPU-idle precondition, see header)
//-----------------------------------------------------------------------------
void CUpscalerDX12::ReleaseFeature()
{
	ReleaseNr();
	// Each context is only created after its provider API was loaded, and the APIs outlive every context.
	if ( m_pXessContext )
		m_pXessApi->destroyContext( static_cast<xess_context_handle_t>( m_pXessContext ) );
	if ( m_pFfxContext )
	{
		ffxContext context = m_pFfxContext;
		m_pFfxApi->destroyContext( &context, nullptr );
	}
	if ( m_pDlssHandle )
		m_pNgxApi->release( static_cast<NVSDK_NGX_Handle *>( m_pDlssHandle ) );
	m_pXessContext = nullptr;
	m_pFfxContext = nullptr;
	m_pDlssHandle = nullptr;
	m_FfxJitter.RemoveAll();
	m_State = State::Disabled;
	m_Feature = {};
}

//-----------------------------------------------------------------------------
// Purpose: ReleaseFeature plus snippet/NGX shutdown and module unload
//-----------------------------------------------------------------------------
void CUpscalerDX12::Shutdown()
{
	ReleaseFeature();
	// Order: features -> DLSS-NR snippet -> NGX parameter blocks -> NGX core -> modules. The APIs only exist after
	// an Initialize that set m_pDevice, which is cleared last.
	if ( m_pNrApi )
	{
		if ( m_pNrApi->initialized )
			m_pNrApi->shutdown( m_pDevice );
		m_pNrApi->unload();
		delete m_pNrApi;
		m_pNrApi = nullptr;
	}
	if ( m_pNgxApi )
	{
		if ( m_pNgxApi->dlssParams )
			m_pNgxApi->destroy( m_pNgxApi->dlssParams );
		if ( m_pNgxApi->caps )
			m_pNgxApi->destroy( m_pNgxApi->caps );
		if ( m_pNgxApi->initialized )
			m_pNgxApi->shutdown( m_pDevice );
	}
	delete m_pNgxApi;
	m_pNgxApi = nullptr;
	delete m_pXessApi;
	m_pXessApi = nullptr;
	if ( m_pFfxApi )
		FreeLibrary( m_pFfxApi->upscaler );
	delete m_pFfxApi;
	m_pFfxApi = nullptr;
	Provider *const pProviders[] = { &m_Nr, &m_Dlss, &m_Fsr, &m_Xess, &m_NgxCore };
	for ( size_t i = 0; i < ARRAYSIZE( pProviders ); ++i )
	{
		if ( pProviders[i]->module )
			FreeLibrary( pProviders[i]->module );
		*pProviders[i] = Provider{};
	}
	m_pDepthClone.Reset();
	m_pImmediateList.Reset();
	m_pImmediateAllocator.Reset();
	m_pImmediateFence.Reset();
	m_nImmediateValue = 0;
	m_pNrPso.Reset();
	m_pDepthPso.Reset();
	m_pComputeRoot.Reset();
	m_pDevice = nullptr;
	m_nVendor = m_nDeviceId = 0;
	m_szDir[0] = 0;
}

// ---------------------------------------------------------------------------------------------------------------
// CShaderAPIDX12 integration: mode selection, jitter, dispatch and lifetime. All of it runs on the recording owner.
// ---------------------------------------------------------------------------------------------------------------
static ConVar upscaler_fault_replays( "upscaler_fault_replays", "0", FCVAR_CHEAT,
    "Test hook: the next N recorded upscaler dispatches skip the provider and report a replay failure" );

namespace
{
//-----------------------------------------------------------------------------
// Purpose: View-space distances where the projection maps depth to 0 and 1 (column-vector VMatrix, w row = row 3)
//-----------------------------------------------------------------------------
bool ProjectionDepthRange( const VMatrix &p, float &flNear, float &flFar, bool &bInverted, float &flFovY )
{
	if ( p[3][3] != 0.f || p[3][2] == 0.f || p[2][2] == 0.f || p[2][2] == p[3][2] || p[1][1] == 0.f )
		return false;
	const float flAtZero = fabsf( -p[2][3] / p[2][2] ), flAtOne = fabsf( ( p[3][3] - p[2][3] ) / ( p[2][2] - p[3][2] ) );
	bInverted = flAtZero > flAtOne;
	flNear = bInverted ? flAtOne : flAtZero;
	flFar = bInverted ? flAtZero : flAtOne;
	flFovY = 2.f * atanf( 1.f / fabsf( p[1][1] ) );
	return IsFinite( flNear ) && IsFinite( flFar ) && flNear > 0.f && flFar > flNear;
}

// Extracts an unsigned `nBits`-wide field at `nShift` from a packed render-parameter config.
uint32_t Field( int nConfig, int nShift, int nBits )
{
	return ( static_cast<uint32_t>( nConfig ) >> nShift ) & ( ( 1u << nBits ) - 1u );
}

// NaN/inf map to `flLo`; finite values are clamped to [flLo, flHi].
float ClampFinite( float flValue, float flLo, float flHi )
{
	return IsFinite( flValue ) ? Clamp( flValue, flLo, flHi ) : flLo;
}
} // namespace

//-----------------------------------------------------------------------------
// Purpose: Folds replayed dispatch results into the status/history state, in serial order; `bWait` blocks on
//          submission progress for results that have not replayed yet
//-----------------------------------------------------------------------------
void CShaderAPIDX12::ConsumeUpscalerReplays( bool bWait )
{
	while ( m_nUpscalerConsumedSerial < m_nUpscalerPendingSerial )
	{
		const uint64_t nSerial = m_nUpscalerConsumedSerial + 1;
		UpscalerReplayResultDX12 &slot = m_UpscalerReplay[nSerial % kUpscalerReplaySlots];
		if ( slot.serial() != nSerial )
		{
			if ( !bWait )
				return;
			// The dispatch chunk was flushed when recorded, so its replay is queued ahead of anything still pending.
			if ( m_pDevice && m_pDevice->WaitForSubmissionProgress() )
				continue;
			// Queue empty: the callback has either run (serial now visible through the tail's release) or never will.
			if ( slot.serial() != nSerial )
			{
				m_nUpscalerConsumedSerial = nSerial;
				m_bUpscalerHistoryGap = m_bUpscalerNrHistoryGap = true;
				m_RenderingInts[INT_RENDERPARM_DX12_UPSCALE_STATUS] = -6;
				Warning( "ShaderAPIDX12 upscaler: dispatch %llu never replayed; history reset\n", static_cast<unsigned long long>( nSerial ) );
				continue;
			}
		}
		m_nUpscalerConsumedSerial = nSerial;
		const uint32_t nCode = slot.code(), nNrCode = slot.nrCode();
		const uint64_t nFrame = slot.frame();
		if ( nNrCode )
		{
			m_bUpscalerNrHistoryGap = true;
			m_RenderingInts[INT_RENDERPARM_DX12_NR_STATUS] = -6;
			Warning( "ShaderAPIDX12 upscaler: DLSS-NR replay failed for frame %llu (code 0x%x); the temporal-AA result was shown\n", static_cast<unsigned long long>( nFrame ), nNrCode );
		}
		if ( !nCode )
		{
			m_nUpscalerLastSuccessFrame = nFrame;
			continue;
		}
		m_bUpscalerHistoryGap = m_bUpscalerNrHistoryGap = true;
		m_RenderingInts[INT_RENDERPARM_DX12_UPSCALE_STATUS] = -6;
		Warning( "ShaderAPIDX12 upscaler: %s replay failed for frame %llu (code 0x%x); history reset\n", UpscalerKindNameDX12( m_UpscalerKind ), static_cast<unsigned long long>( nFrame ), nCode );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Picks this frame's sub-pixel jitter (provider sequence, else Halton(2,3) over eight phases)
//-----------------------------------------------------------------------------
void CShaderAPIDX12::SampleUpscalerJitter()
{
	m_UpscalerJitter[0] = m_UpscalerJitter[1] = 0.f;
	// The first frame after enable/reset renders unjittered; jitter starts only once the previous frame's
	// dispatch replayed successfully, so no unfiltered jittered frame reaches the screen.
	if ( !m_bUpscalerViewEligible || m_bUpscalerHistoryGap || m_UpscalerKind == UpscalerKindDX12::None || !m_pDevice || m_pDevice->SceneSampleCount() != 1 ||
	    m_nUpscalerQueuedFrame == m_nFrameCounter || m_nUpscalerLastDispatchFrame == ~0ull || m_nUpscalerLastSuccessFrame != m_nUpscalerLastDispatchFrame )
		return;
	const uint64_t nIndex = m_nUpscalerJitterIndex++;
	float flX = 0.f, flY = 0.f;
	if ( !m_Upscaler.ProviderJitter( nIndex, flX, flY ) )
	{
		const uint32_t nPhase = static_cast<uint32_t>( nIndex % 8 ) + 1;
		flX = Halton( nPhase, 2 ) - .5f;
		flY = Halton( nPhase, 3 ) - .5f;
	}
	m_UpscalerJitter[0] = Clamp( flX, -.5f, .5f );
	m_UpscalerJitter[1] = Clamp( flY, -.5f, .5f );
}

//-----------------------------------------------------------------------------
// Purpose: Waits until no recorded upscaler callback can still run or reference provider objects
//-----------------------------------------------------------------------------
bool CShaderAPIDX12::WaitUpscalerGpuIdle()
{
	if ( !m_pDevice )
		return true;
	if ( m_pDevice->IsRecordingOwner() )
		return m_pDevice->SubmitAndWaitForGpu();
	// Teardown off the owner: every dispatch chunk was flushed when recorded; wait for its replay and the last submission.
	m_pDevice->DrainSubmissions();
	return m_pDevice->WaitForFence( m_pDevice->NextFenceValue() - 1 );
}

//-----------------------------------------------------------------------------
// Purpose: Releases the provider feature behind a GPU-idle boundary on the recording owner
//-----------------------------------------------------------------------------
void CShaderAPIDX12::ReleaseUpscalerFeature()
{
	// GPU-idle boundary on the recording owner: every recorded callback naming the feature has replayed and its GPU
	// work has completed before the provider context is destroyed.
	if ( !m_pDevice || !m_pDevice->IsRecordingOwner() || !WaitUpscalerGpuIdle() )
		return;
	m_Pipeline.InvalidateGraphicsBindings();
	ConsumeUpscalerReplays( false );
	m_Upscaler.ReleaseFeature();
	m_bUpscalerHistoryGap = m_bUpscalerNrHistoryGap = true;
}

//-----------------------------------------------------------------------------
// Purpose: Releases the DLSS-NR chain behind a GPU-idle boundary on the recording owner
//-----------------------------------------------------------------------------
void CShaderAPIDX12::ReleaseUpscalerNr()
{
	if ( !m_Upscaler.NrLayers() || !m_pDevice || !m_pDevice->IsRecordingOwner() || !WaitUpscalerGpuIdle() )
		return;
	m_Pipeline.InvalidateGraphicsBindings();
	ConsumeUpscalerReplays( false );
	m_Upscaler.ReleaseNr();
	m_bUpscalerNrHistoryGap = true;
}

//-----------------------------------------------------------------------------
// Purpose: Reads the DLSS-NR render parameters, (re)builds the chain when they changed and fills `compose`;
//          returns the layer count to dispatch (0: no DLSS-NR this frame)
//-----------------------------------------------------------------------------
uint32_t CShaderAPIDX12::PrepareUpscalerNr( DlssNrComposeDX12 &compose )
{
	const int nConfig = m_RenderingInts[INT_RENDERPARM_DX12_NR_CONFIG];
	int &nStatus = m_RenderingInts[INT_RENDERPARM_DX12_NR_STATUS];
	const uint32_t nLayers = Min<uint32_t>( Field( nConfig, DX12_NR_CONFIG_LAYERS_SHIFT, 4 ), DX12_NR_MAX_LAYERS );
	if ( !nLayers )
	{
		if ( m_Upscaler.NrLayers() )
			ReleaseUpscalerNr();
		m_bUpscalerNrFailed = false;
		nStatus = 0;
		return 0;
	}
	if ( !m_Upscaler.NrAvailable() )
	{
		if ( !m_bUpscalerNrUnavailableLogged )
			Warning( "ShaderAPIDX12 upscaler: DLSS-NR requested but unavailable: %s\n", m_Upscaler.NrReason().Get() );
		m_bUpscalerNrUnavailableLogged = true;
		nStatus = -1;
		return 0;
	}
	const float *f = m_RenderingFloats;
	DlssNrTuningDX12 tuning;
	tuning.preset = Min( Field( nConfig, DX12_NR_CONFIG_PRESET_SHIFT, 4 ), 3u );
	tuning.style = Min( Field( nConfig, DX12_NR_CONFIG_STYLE_SHIFT, 2 ), 2u );
	tuning.intensity = ClampFinite( f[FLOAT_RENDERPARM_DX12_NR_INTENSITY], 0.f, 2.f );
	tuning.localStructure = ClampFinite( f[FLOAT_RENDERPARM_DX12_NR_LOCAL_STRUCTURE], 0.f, 2.f );
	tuning.localTone = ClampFinite( f[FLOAT_RENDERPARM_DX12_NR_LOCAL_TONE], 0.f, 2.f );
	tuning.skinStructure = ClampFinite( f[FLOAT_RENDERPARM_DX12_NR_SKIN_STRUCTURE], -1.f, 2.f );
	tuning.autoMask = ( nConfig & DX12_NR_CONFIG_AUTO_MASK ) != 0;
	tuning.uiCorrection = ( nConfig & DX12_NR_CONFIG_UI_CORRECTION ) != 0;
	ID3D12Resource *pSource = m_pUpscalerOutput.Get();
	if ( !m_Upscaler.NrReady( tuning, nLayers, pSource ) )
	{
		// A failed configuration is not retried every frame; any change to it is.
		if ( m_bUpscalerNrFailed && m_UpscalerNrFailedTuning == tuning && m_nUpscalerNrFailedLayers == nLayers && m_pUpscalerNrFailedSource == pSource )
		{
			nStatus = -2;
			return 0;
		}
		ReleaseUpscalerNr();
		if ( !m_pDevice->CommandList() || !m_Upscaler.PrepareNr( tuning, nLayers, pSource, m_pDevice->Queue() ) )
		{
			m_bUpscalerNrFailed = true;
			m_UpscalerNrFailedTuning = tuning;
			m_nUpscalerNrFailedLayers = nLayers;
			m_pUpscalerNrFailedSource = pSource;
			nStatus = -2;
			return 0;
		}
		m_bUpscalerNrFailed = false;
		m_bUpscalerNrHistoryGap = true;
	}
	compose.whitePoint = ClampFinite( f[FLOAT_RENDERPARM_DX12_NR_WHITE_POINT], .01f, 64.f );
	compose.transferStrength = ClampFinite( f[FLOAT_RENDERPARM_DX12_NR_TRANSFER_STRENGTH], 0.f, 2.f );
	compose.colourStrength = ClampFinite( f[FLOAT_RENDERPARM_DX12_NR_COLOUR_STRENGTH], 0.f, 4.f );
	compose.maxRatio = ClampFinite( f[FLOAT_RENDERPARM_DX12_NR_MAX_RATIO], 1.f, 8.f );
	compose.compareSplit = ClampFinite( f[FLOAT_RENDERPARM_DX12_NR_COMPARE_SPLIT], 0.f, 1.f );
	compose.compareZoom = ClampFinite( f[FLOAT_RENDERPARM_DX12_NR_COMPARE_ZOOM], 1.f, 2.f );
	compose.reversibleMode = Min( Field( nConfig, DX12_NR_CONFIG_REVERSIBLE_SHIFT, 3 ), 4u );
	compose.debugView = Field( nConfig, DX12_NR_CONFIG_DEBUG_VIEW_SHIFT, 2 );
	compose.compareMode = Min( Field( nConfig, DX12_NR_CONFIG_COMPARE_SHIFT, 2 ), 2u );
	compose.applyModel = ( nConfig & DX12_NR_CONFIG_APPLY_MODEL ) != 0;
	compose.compareSwap = ( nConfig & DX12_NR_CONFIG_COMPARE_SWAP ) != 0;
	return nLayers;
}

//-----------------------------------------------------------------------------
// Purpose: (Re)creates the scene-sized UAV the providers write into
//-----------------------------------------------------------------------------
bool CShaderAPIDX12::EnsureUpscalerOutput( UINT nWidth, UINT nHeight )
{
	if ( m_pUpscalerOutput )
	{
		const D3D12_RESOURCE_DESC desc = m_pUpscalerOutput->GetDesc();
		if ( desc.Width == nWidth && desc.Height == nHeight )
			return true;
		// Only reached on resize: the feature naming the old output is released behind a GPU-idle boundary first.
		ReleaseUpscalerFeature();
		m_pUpscalerOutput.Reset();
	}
	D3D12_HEAP_PROPERTIES heap{};
	heap.Type = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_RESOURCE_DESC desc{};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = nWidth;
	desc.Height = nHeight;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = m_pDevice->SceneColorFormat();
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	const HRESULT hr = m_pDevice->NativeDevice()->CreateCommittedResource( &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS( &m_pUpscalerOutput ) );
	if ( FAILED( hr ) )
	{
		Warning( "ShaderAPIDX12 upscaler: output %ux%u creation failed (0x%08x)\n", nWidth, nHeight, static_cast<unsigned>( hr ) );
		m_pUpscalerOutput.Reset();
		return false;
	}
	m_UpscalerOutputState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	m_bUpscalerHistoryGap = m_bUpscalerNrHistoryGap = true;
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Arms (nMode 1-4) or disarms (0) the upscaler for the view being begun; loads providers on first use
//-----------------------------------------------------------------------------
void CShaderAPIDX12::SetUpscalerMode( int nMode )
{
	m_bUpscalerViewEligible = false;
	m_UpscalerJitter[0] = m_UpscalerJitter[1] = 0.f;
	if ( !m_pDevice || !m_pDevice->IsRecordingOwner() || !m_pDevice->CommandList() )
		return;
	if ( nMode < 0 || nMode > 4 )
		nMode = 0;
	// Jitter sampling needs the previous dispatch's result; mode 0 only disarms and never waits on the worker.
	ConsumeUpscalerReplays( nMode != 0 );
	m_nUpscalerMode = nMode;
	m_nUpscalerFrameToken = m_nFrameCounter;
	// Mode 0 disarms: ineligible views (monitors, intro, loading, model panels) submit it every frame, before or after
	// the main view. The feature is released by BeginFrame once a whole frame passes without a nonzero mode, and a
	// later ineligible view keeps the status the main view published this frame.
	if ( !nMode )
	{
		if ( m_nUpscalerEnabledFrame != m_nFrameCounter )
			m_RenderingInts[INT_RENDERPARM_DX12_UPSCALE_STATUS] = m_RenderingInts[INT_RENDERPARM_DX12_NR_STATUS] = 0;
		return;
	}
	m_nUpscalerEnabledFrame = m_nFrameCounter;
	if ( nMode != m_nUpscalerSelectedMode )
	{
		m_nUpscalerSelectedMode = nMode;
		m_bUpscalerHistoryGap = m_bUpscalerNrHistoryGap = true;
	}
	if ( !m_bUpscalerInitialized )
	{
		m_bUpscalerInitialized = true;
		MaterialAdapterInfo_t adapter{};
		if ( g_pShaderDeviceMgrDX12 )
			g_pShaderDeviceMgrDX12->GetAdapterInfo( m_pDevice->GetCurrentAdapter(), adapter );
		// Provider DLLs live beside this module.
		wchar_t szModuleDir[MAX_PATH] = {};
		HMODULE hSelf = nullptr;
		GetModuleHandleExW( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>( &UpscalerKindNameDX12 ), &hSelf );
		if ( hSelf && GetModuleFileNameW( hSelf, szModuleDir, MAX_PATH ) )
		{
			if ( wchar_t *pszSlash = wcsrchr( szModuleDir, L'\\' ) )
				pszSlash[1] = 0;
		}
		m_Upscaler.Initialize( m_pDevice->NativeDevice(), adapter, szModuleDir, CommandLine() && CommandLine()->CheckParm( "-dx12upscalerlog" ) );
	}
	const UpscalerKindDX12 kind = m_Upscaler.Resolve( nMode );
	if ( kind != m_UpscalerKind )
	{
		if ( m_Upscaler.Feature().kind != UpscalerKindDX12::None )
			ReleaseUpscalerFeature();
		m_UpscalerKind = kind;
		m_bUpscalerHistoryGap = m_bUpscalerNrHistoryGap = true;
		if ( kind != UpscalerKindDX12::None )
			Msg( "ShaderAPIDX12 upscaler: mode %d selects %s\n", nMode, UpscalerKindNameDX12( kind ) );
		else
			Warning( "ShaderAPIDX12 upscaler: mode %d has no available provider on this adapter\n", nMode );
	}
	// The eligible main view was begun this frame; SampleUpscalerJitter and DispatchUpscaler handle a missing provider.
	m_bUpscalerViewEligible = true;
	if ( kind == UpscalerKindDX12::None )
	{
		m_RenderingInts[INT_RENDERPARM_DX12_UPSCALE_STATUS] = -1;
		return;
	}
	// A frame of the eligible view without a dispatch breaks the history.
	if ( m_nUpscalerLastDispatchFrame == ~0ull || m_nUpscalerLastDispatchFrame + 1 < m_nFrameCounter )
		m_bUpscalerHistoryGap = m_bUpscalerNrHistoryGap = true;
	SampleUpscalerJitter();
}

//-----------------------------------------------------------------------------
// Purpose: Validates this frame's inputs and records one provider dispatch (plus DLSS-NR layers) over the scene
//-----------------------------------------------------------------------------
void CShaderAPIDX12::DispatchUpscaler( int nFlags )
{
	if ( !( nFlags & DX12_UPSCALE_DISPATCH_RUN ) || !m_nUpscalerMode )
		return;
	int &nStatus = m_RenderingInts[INT_RENDERPARM_DX12_UPSCALE_STATUS];
	int &nNrStatus = m_RenderingInts[INT_RENDERPARM_DX12_NR_STATUS];
	// Every rejection records no command bytes and breaks the history; requested DLSS-NR layers report -3.
	const auto reject = [&]( int nCode, const char *pszWhy )
	{
		if ( nStatus != nCode )
			Warning( "ShaderAPIDX12 upscaler: dispatch rejected (%d): %s\n", nCode, pszWhy );
		nStatus = nCode;
		m_bUpscalerHistoryGap = m_bUpscalerNrHistoryGap = true;
		m_UpscalerJitter[0] = m_UpscalerJitter[1] = 0.f;
		nNrStatus = Field( m_RenderingInts[INT_RENDERPARM_DX12_NR_CONFIG], DX12_NR_CONFIG_LAYERS_SHIFT, 4 ) ? -3 : 0;
	};
	if ( !m_bUpscalerViewEligible || m_nUpscalerFrameToken != m_nFrameCounter )
		return reject( -4, "no eligible main view was begun this frame" );
	if ( !m_pDevice || m_pDevice->SceneSampleCount() > 1 )
		return reject( -3, "MSAA is active" );
	if ( !m_pDevice->IsRecordingOwner() || !m_pDevice->CommandList() || m_RenderTargets[0] != SHADER_RENDERTARGET_BACKBUFFER || !m_pDevice->SceneColor() || !m_pDevice->SceneDepth() )
		return reject( -4, "render target 0 is not the scene or the caller does not own recording" );
	const UINT nWidth = static_cast<UINT>( m_pDevice->SceneWidth() ), nHeight = static_cast<UINT>( m_pDevice->SceneHeight() );
	TextureRecord *pMotion = m_nMotionResolvedFrame == m_nFrameCounter ? FindTexture( m_hMotionResolvedHandle ) : nullptr;
	if ( !pMotion || !( pMotion->flags & TEXTURE_CREATE_RENDERTARGET ) || pMotion->format != IMAGE_FORMAT_RGBA16161616F || pMotion->width != static_cast<int>( nWidth ) ||
	    pMotion->height != static_cast<int>( nHeight ) || !EnsureTextureResident( *pMotion ) || !pMotion->resource )
		return reject( -5, "no scene-sized motion-vector resolve this frame" );
	if ( m_UpscalerKind == UpscalerKindDX12::None )
		return reject( -1, "no provider for the selected mode" );
	if ( m_nUpscalerQueuedFrame == m_nFrameCounter )
		return; // one dispatch per frame
	float flDerivedNear = 0.f, flDerivedFar = 0.f, flDerivedFov = 0.f;
	bool bInverted = false;
	ProjectionDepthRange( m_Matrices[MATERIAL_PROJECTION], flDerivedNear, flDerivedFar, bInverted, flDerivedFov );
	const UpscalerFeatureDescDX12 desc{ m_UpscalerKind, nWidth, nHeight, bInverted };
	if ( m_Upscaler.Feature().kind != UpscalerKindDX12::None && m_Upscaler.Feature() != desc )
		ReleaseUpscalerFeature();
	if ( !EnsureUpscalerOutput( nWidth, nHeight ) )
		return reject( -2, "output allocation failed" );
	if ( !m_Upscaler.PrepareFeature( desc, m_pDevice->Queue() ) )
		return reject( -2, "provider feature creation failed" );
	DlssNrComposeDX12 compose;
	const uint32_t nNrLayers = PrepareUpscalerNr( compose );
	FlushBufferedPrimitives();
	CommitTransforms();
	CCommandRecorderDX12 *pList = m_pDevice->CommandList();
	if ( !pList )
		return reject( -4, "recording ended during feature preparation" );
	while ( m_nUpscalerPendingSerial + 1 - m_nUpscalerConsumedSerial > kUpscalerReplaySlots )
		ConsumeUpscalerReplays( true );
	const int nFaces = ( pMotion->flags & TEXTURE_CREATE_CUBEMAP ) ? 6 : 1, nSub = pMotion->currentCopy * nFaces * pMotion->mipLevels;
	const float *pCamera = m_RenderingFloats;
	UpscalerDispatchDX12 d;
	d.color = m_pDevice->SceneColor();
	d.colorBefore = m_pDevice->SceneColorState();
	d.depth = m_pDevice->SceneDepth();
	d.depthBefore = m_pDevice->SceneDepthState();
	d.motion = pMotion->resource.Get();
	d.motionBefore = pMotion->subresourceStates[nSub];
	d.output = m_pUpscalerOutput.Get();
	d.outputBefore = m_UpscalerOutputState;
	d.jitter[0] = m_UpscalerJitter[0];
	d.jitter[1] = m_UpscalerJitter[1];
	// _rt_MotionVectors.xy = current - previous in UV; providers take previous - current in pixels.
	d.motionScale[0] = -static_cast<float>( nWidth );
	d.motionScale[1] = -static_cast<float>( nHeight );
	d.fovY = pCamera[FLOAT_RENDERPARM_DX12_UPSCALE_FOV_Y] > 0.f ? pCamera[FLOAT_RENDERPARM_DX12_UPSCALE_FOV_Y] : flDerivedFov;
	d.cameraNear = pCamera[FLOAT_RENDERPARM_DX12_UPSCALE_NEAR] > 0.f ? pCamera[FLOAT_RENDERPARM_DX12_UPSCALE_NEAR] : flDerivedNear;
	d.cameraFar = pCamera[FLOAT_RENDERPARM_DX12_UPSCALE_FAR] > 0.f ? pCamera[FLOAT_RENDERPARM_DX12_UPSCALE_FAR] : flDerivedFar;
	const double flNow = Plat_FloatTime();
	// FFX validates frameTimeDelta >= 1 ms; above 1000 fps the provider sees 1 ms.
	d.frameTimeMs = m_flUpscalerLastDispatchTime > 0.0 ? static_cast<float>( Clamp( ( flNow - m_flUpscalerLastDispatchTime ) * 1000.0, 1.0, 100.0 ) ) : 16.6667f;
	d.width = nWidth;
	d.height = nHeight;
	d.reset = ( nFlags & DX12_UPSCALE_DISPATCH_RESET ) != 0 || m_bUpscalerHistoryGap;
	d.nrLayers = nNrLayers;
	d.nr = compose;
	// The model's history breaks with the temporal AA's, on chain rebuild and on a change of proxy curve.
	d.nrReset = d.reset || m_bUpscalerNrHistoryGap || compose.reversibleMode != m_nUpscalerNrLastReversible;
	if ( m_Upscaler.NeedsDescriptors( d ) )
	{
		const DescriptorRangeDX12 range = m_Pipeline.AllocateTransientResources( CUpscalerDX12::DescriptorCount( nNrLayers ), m_pDevice->NextFenceValue() );
		if ( range.count != CUpscalerDX12::DescriptorCount( nNrLayers ) )
			return reject( -2, "descriptor allocation failed" );
		d.heap = m_Pipeline.ResourceDescriptorHeap();
		d.cpu = range.cpu;
		d.gpu = range.gpu;
	}
	if ( upscaler_fault_replays.GetInt() > 0 )
	{
		d.forceFailure = true;
		upscaler_fault_replays.SetValue( upscaler_fault_replays.GetInt() - 1 );
	}
	const uint64_t nSerial = m_nUpscalerPendingSerial + 1;
	d.frame = m_nFrameCounter;
	d.serial = nSerial;
	d.result = &m_UpscalerReplay[nSerial % kUpscalerReplaySlots];
	if ( !m_Upscaler.RecordDispatch( *pList, d ) )
		return reject( -2, "provider feature not ready" );
	m_nUpscalerPendingSerial = nSerial;
	// Flushed now so a later wait for this result only waits for this chunk's replay.
	pList->Flush();
	m_Pipeline.InvalidateGraphicsBindings();
	ID3D12Resource *const pRetained[] = { d.color, d.depth, d.motion, d.output };
	for ( size_t i = 0; i < ARRAYSIZE( pRetained ); ++i )
		m_pDevice->RetainResource( pRetained[i] );
	// The callback leaves every resource in its normal state on success and failure alike.
	m_pDevice->SetSceneStatesAfterExternal( D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_DEPTH_WRITE );
	if ( pMotion->subresourceStates[nSub] != D3D12_RESOURCE_STATE_RENDER_TARGET )
	{
		pMotion->subresourceStates[nSub] = D3D12_RESOURCE_STATE_RENDER_TARGET;
		pMotion->sampledStateValid = false;
		++m_nTextureStateEpoch;
	}
	m_UpscalerOutputState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	m_nUpscalerQueuedFrame = m_nUpscalerLastDispatchFrame = m_nFrameCounter;
	m_flUpscalerLastDispatchTime = flNow;
	m_bUpscalerHistoryGap = false;
	if ( nNrLayers )
	{
		m_bUpscalerNrHistoryGap = false;
		m_nUpscalerNrLastReversible = compose.reversibleMode;
		nNrStatus = static_cast<int>( nNrLayers );
	}
	// The frame generator de-jitters with the same offset after this zeroing.
	m_UpscalerFrameJitter[0] = m_UpscalerJitter[0];
	m_UpscalerFrameJitter[1] = m_UpscalerJitter[1];
	m_UpscalerJitter[0] = m_UpscalerJitter[1] = 0.f;
	nStatus = 1 | ( static_cast<int>( m_UpscalerKind ) << 8 );
}

//-----------------------------------------------------------------------------
// Purpose: Drops the feature and output after a whole frame without a nonzero mode submission
//-----------------------------------------------------------------------------
void CShaderAPIDX12::ReleaseIdleUpscaler()
{
	// A full frame without any nonzero mode submission (r_upscaler 0, DX9-equivalent paths, menus): drop the
	// provider feature and output behind a GPU-idle boundary. Modules stay loaded until device-resource release.
	if ( m_nUpscalerEnabledFrame == ~0ull || m_nUpscalerEnabledFrame + 1 >= m_nFrameCounter )
		return;
	m_nUpscalerEnabledFrame = ~0ull;
	if ( m_Upscaler.Feature().kind != UpscalerKindDX12::None || m_pUpscalerOutput )
	{
		ReleaseUpscalerFeature();
		m_pUpscalerOutput.Reset();
		m_UpscalerOutputState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	}
	m_UpscalerKind = UpscalerKindDX12::None;
	m_nUpscalerSelectedMode = 0;
	m_bUpscalerHistoryGap = m_bUpscalerNrHistoryGap = true;
}

//-----------------------------------------------------------------------------
// Purpose: Unloads every provider and resets all upscaler state (device-resource release)
//-----------------------------------------------------------------------------
void CShaderAPIDX12::ReleaseUpscalerResources()
{
	if ( m_Upscaler.Initialized() || m_pUpscalerOutput || m_nUpscalerPendingSerial != m_nUpscalerConsumedSerial )
	{
		// No callback may run after the provider modules unload: replay everything and wait for the GPU.
		WaitUpscalerGpuIdle();
		ConsumeUpscalerReplays( false );
	}
	m_Upscaler.Shutdown();
	m_pUpscalerOutput.Reset();
	m_UpscalerOutputState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	m_bUpscalerInitialized = false;
	m_nUpscalerMode = m_nUpscalerSelectedMode = 0;
	m_UpscalerKind = UpscalerKindDX12::None;
	m_nUpscalerEnabledFrame = ~0ull;
	m_bUpscalerViewEligible = false;
	m_bUpscalerHistoryGap = m_bUpscalerNrHistoryGap = true;
	m_bUpscalerNrFailed = m_bUpscalerNrUnavailableLogged = false;
	m_pUpscalerNrFailedSource = nullptr;
	m_nUpscalerNrLastReversible = 0;
	m_nUpscalerFrameToken = m_nUpscalerQueuedFrame = m_nUpscalerLastDispatchFrame = m_nUpscalerLastSuccessFrame = ~0ull;
	m_nUpscalerPendingSerial = m_nUpscalerConsumedSerial = 0;
	m_nUpscalerJitterIndex = 0;
	m_flUpscalerLastDispatchTime = 0.0;
	m_UpscalerJitter[0] = m_UpscalerJitter[1] = 0.f;
	for ( UpscalerReplayResultDX12 &slot : m_UpscalerReplay )
	{
		slot.serial = 0;
		slot.code = 0;
		slot.nrCode = 0;
		slot.frame = ~0ull;
	}
	m_hMotionResolvedHandle = 0;
	m_nMotionResolvedFrame = ~0ull;
	m_RenderingInts[INT_RENDERPARM_DX12_UPSCALE_STATUS] = m_RenderingInts[INT_RENDERPARM_DX12_NR_STATUS] = 0;
}

//-----------------------------------------------------------------------------
// Frame generation (contract F1-F10 of the frame-generation plan). The device owns CFrameGenDX12 and applies kind
// switches at the tail of Present; the shader API resolves the request, gates the per-frame dispatch and
// publishes the IShaderAPIDX12 status.
//-----------------------------------------------------------------------------
bool CShaderAPIDX12::ProjectionIsInverted() const
{
	float flNear = 0.f, flFar = 0.f, flFov = 0.f;
	bool bInverted = false;
	return ProjectionDepthRange( m_Matrices[MATERIAL_PROJECTION], flNear, flFar, bInverted, flFov ) && bInverted;
}

namespace
{
// Row-major, row-vector layout (the transpose of a column-vector VMatrix), as Streamline and XeFG take it.
void TransposeTo( const VMatrix &m, float ( &out )[16] )
{
	for ( int row = 0; row < 4; ++row )
		for ( int col = 0; col < 4; ++col )
			out[row * 4 + col] = m[col][row];
}
} // namespace

//-----------------------------------------------------------------------------
// IShaderAPIDX12: the client thread only stores requests; BeginFrame applies them on the recording thread
//-----------------------------------------------------------------------------
void CShaderAPIDX12::SetFrameGeneration( int nMode, int nMultiplier, bool bHudless )
{
	if ( nMode < 0 || nMode > 4 )
		nMode = 0;
	m_nFrameGenSettingsRequest = nMode | ( Clamp( nMultiplier, 2, 4 ) << 8 ) | ( bHudless ? 1 << 16 : 0 );
}

void CShaderAPIDX12::SetReflexMode( int nMode )
{
	m_nReflexRequest = Clamp( nMode, 0, 2 );
}

void CShaderAPIDX12::SetFrameRateLimit( float flFps )
{
	const float flLimit = flFps > 0.f && flFps < 100000.f ? flFps : 0.f;
	int nBits;
	memcpy( &nBits, &flLimit, sizeof( nBits ) );
	m_nFpsLimitBits = nBits;
}

void CShaderAPIDX12::LatencyMarker( int nMarker, unsigned int nFrameId )
{
	// Straight from the client's simulation thread (F6): never touches recording-thread state.
	if ( m_pDevice && nMarker >= SHADERAPIDX12_MARKER_SIMULATION_START && nMarker <= SHADERAPIDX12_MARKER_RENDER_SUBMIT_END )
		m_pDevice->FrameGen().Marker( static_cast<uint32_t>( nMarker ), nFrameId & 0x0FFFFFFFu );
}

void CShaderAPIDX12::ApplyFrameGenSettings()
{
	const int nSettings = m_nFrameGenSettingsRequest;
	SetFrameGenMode( nSettings & 0xff, ( nSettings >> 8 ) & 0xff, ( nSettings & ( 1 << 16 ) ) != 0 );
	SetReflexRequest( m_nReflexRequest );
	const int nBits = m_nFpsLimitBits;
	float flLimit;
	memcpy( &flLimit, &nBits, sizeof( flLimit ) );
	if ( flLimit != m_flFrameGenFpsLimit )
	{
		m_flFrameGenFpsLimit = flLimit;
		m_pDevice->SetFrameGenFpsLimit( flLimit );
	}
}

// The eligible main view of this frame (INT_RENDERPARM_DX12_FRAMEGEN_VIEW, before its first 3D draw).
void CShaderAPIDX12::SetFrameGenView( int nEligible )
{
	if ( nEligible && ( m_nFrameGenRequest & 0xff ) )
		m_nFrameGenFrameToken = m_nFrameCounter;
}

// Loads the provider object once per device (modules beside the renderer, adapter probes).
void CShaderAPIDX12::EnsureFrameGenInitialized()
{
	CFrameGenDX12 &frameGen = m_pDevice->FrameGen();
	if ( frameGen.Initialized() )
		return;
	MaterialAdapterInfo_t adapter{};
	if ( g_pShaderDeviceMgrDX12 )
		g_pShaderDeviceMgrDX12->GetAdapterInfo( m_pDevice->GetCurrentAdapter(), adapter );
	wchar_t szModuleDir[MAX_PATH] = {};
	HMODULE hSelf = nullptr;
	GetModuleHandleExW( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>( &UpscalerKindNameDX12 ), &hSelf );
	if ( hSelf && GetModuleFileNameW( hSelf, szModuleDir, MAX_PATH ) )
	{
		if ( wchar_t *pszSlash = wcsrchr( szModuleDir, L'\\' ) )
			pszSlash[1] = 0;
	}
	frameGen.Initialize( m_pDevice->NativeDevice(), m_pDevice->Factory(), adapter, szModuleDir, CommandLine() && CommandLine()->CheckParm( "-dx12framegenlog" ) );
}

//-----------------------------------------------------------------------------
// Purpose: Standalone Reflex request; re-applied after a failure only when the mode changes
//-----------------------------------------------------------------------------
void CShaderAPIDX12::SetReflexRequest( int nMode )
{
	if ( nMode == m_nReflexApplied )
		return;
	m_nReflexApplied = nMode;
	if ( nMode )
		EnsureFrameGenInitialized();
	CFrameGenDX12 &frameGen = m_pDevice->FrameGen();
	const int nStatus = frameGen.Initialized() ? frameGen.SetReflexMode( nMode ) : 0;
	m_nReflexStatus = nStatus;
	if ( nStatus < 0 )
	{
		V_strncpy( m_szFrameGenError, frameGen.LastError(), sizeof( m_szFrameGenError ) );
		Warning( "ShaderAPIDX12 reflex: mode %d unavailable: %s\n", nMode, frameGen.LastError() );
	}
	else if ( nMode )
		Msg( "ShaderAPIDX12 reflex: %s\n", nMode == 2 ? "low latency + boost" : "low latency" );
}

//-----------------------------------------------------------------------------
// Purpose: Frame-generation settings (mode, multiplier, hudless), evaluated every BeginFrame
//-----------------------------------------------------------------------------
void CShaderAPIDX12::SetFrameGenMode( int nMode, int nMultiplier, bool bHudless )
{
	CInterlockedInt &nStatus = m_nFrameGenStatus;
	const int nRequest = nMode | ( nMultiplier << 8 ) | ( bHudless ? 1 << 16 : 0 );
	if ( nRequest != m_nFrameGenRequest )
	{
		m_nFrameGenRequest = nRequest;
		m_bFrameGenSelectFailed = false;
		m_bFrameGenWarned = false;
		m_bFrameGenHistoryGap = true;
	}
	if ( nMode )
		EnsureFrameGenInitialized();
	CFrameGenDX12 &frameGen = m_pDevice->FrameGen();
	FrameGenKindDX12 kind = nMode ? frameGen.Resolve( nMode ) : FrameGenKindDX12::None;
	if ( kind != FrameGenKindDX12::None && !m_pDevice->IsWindowed() )
	{
		if ( !m_bFrameGenWarned )
		{
			m_bFrameGenWarned = true;
			Warning( "ShaderAPIDX12 framegen: mode %d needs a windowed or borderless display mode; exclusive fullscreen runs without frame generation\n", nMode );
		}
		nStatus = -4;
		kind = FrameGenKindDX12::None;
	}
	else if ( kind != FrameGenKindDX12::None && m_pDevice->SceneSampleCount() > 1 )
	{
		// No provider chain for an MSAA scene (the dispatch would reject every frame anyway); the native FP16 chain stays.
		nStatus = -3;
		kind = FrameGenKindDX12::None;
	}
	else if ( nMode && kind == FrameGenKindDX12::None )
	{
		if ( !m_bFrameGenWarned )
		{
			m_bFrameGenWarned = true;
			Warning( "ShaderAPIDX12 framegen: mode %d has no available provider on this adapter (dlssg: %s; fsr: %s; xefg: %s)\n", nMode, frameGen.UnavailableReason( FrameGenKindDX12::DLSSG ),
			    frameGen.UnavailableReason( FrameGenKindDX12::FSR ), frameGen.UnavailableReason( FrameGenKindDX12::XeFG ) );
		}
		nStatus = -1;
	}
	if ( m_bFrameGenSelectFailed || m_pDevice->FrameGenSelectPending() )
		return;
	const bool bKindChange = kind != frameGen.Kind();
	const bool bOptionChange = kind != FrameGenKindDX12::None && ( static_cast<uint32_t>( nMultiplier ) != frameGen.Multiplier() || bHudless != frameGen.Hudless() );
	if ( bKindChange || bOptionChange )
	{
		// Applied at the tail of this frame's Present (F5); until then no dispatch is recorded.
		m_pDevice->RequestFrameGenSelect( kind, static_cast<uint32_t>( nMultiplier ), bHudless );
		m_bFrameGenHistoryGap = true;
		if ( kind != FrameGenKindDX12::None )
		{
			Msg( "ShaderAPIDX12 framegen: mode %d selects %s (x%d%s)\n", nMode, FrameGenKindNameDX12( kind ), nMultiplier, bHudless ? ", hudless" : "" );
			nStatus = 0;
		}
		else if ( nStatus > 0 )
			nStatus = 0;
	}
	else if ( !nMode )
		nStatus = 0;
}

//-----------------------------------------------------------------------------
// Purpose: Folds replayed dispatch results into the status, in serial order (see ConsumeUpscalerReplays)
//-----------------------------------------------------------------------------
void CShaderAPIDX12::ConsumeFrameGenReplays( bool bWait )
{
	while ( m_nFrameGenConsumedSerial < m_nFrameGenPendingSerial )
	{
		const uint64_t nSerial = m_nFrameGenConsumedSerial + 1;
		FrameGenReplayResultDX12 &slot = m_FrameGenReplay[nSerial % kFrameGenReplaySlots];
		if ( slot.serial() != nSerial )
		{
			if ( !bWait )
				return;
			if ( m_pDevice && m_pDevice->WaitForSubmissionProgress() )
				continue;
			if ( slot.serial() != nSerial )
			{
				m_nFrameGenConsumedSerial = nSerial;
				m_bFrameGenHistoryGap = true;
				m_nFrameGenStatus = -6;
				Warning( "ShaderAPIDX12 framegen: dispatch %llu never replayed; history reset\n", static_cast<unsigned long long>( nSerial ) );
				continue;
			}
		}
		m_nFrameGenConsumedSerial = nSerial;
		const uint32_t nCode = slot.code();
		if ( !nCode )
			continue;
		m_bFrameGenHistoryGap = true;
		m_nFrameGenStatus = -6;
		Warning( "ShaderAPIDX12 framegen: %s replay failed for frame %llu (code 0x%x); history reset\n", FrameGenKindNameDX12( m_FrameGenKind ), static_cast<unsigned long long>( slot.frame() ), nCode );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Validates this frame's inputs and records the provider's per-frame work (hudless encode + one
//          ExternalCommand); Present then runs the interpolation for this frame
//-----------------------------------------------------------------------------
void CShaderAPIDX12::DispatchFrameGen( int nFlags )
{
	if ( !( nFlags & DX12_FRAMEGEN_DISPATCH_RUN ) )
		return;
	CInterlockedInt &nStatus = m_nFrameGenStatus;
	const auto reject = [&]( int nCode, const char *pszWhy )
	{
		if ( nStatus != nCode )
			Warning( "ShaderAPIDX12 framegen: dispatch rejected (%d): %s\n", nCode, pszWhy );
		nStatus = nCode;
		m_bFrameGenHistoryGap = true;
	};
	if ( m_nFrameGenFrameToken != m_nFrameCounter )
		return reject( -4, "no eligible main view was begun this frame" );
	if ( !m_pDevice || m_pDevice->SceneSampleCount() > 1 )
		return reject( -3, "MSAA is active" );
	if ( !m_pDevice->IsRecordingOwner() || !m_pDevice->CommandList() || m_RenderTargets[0] != SHADER_RENDERTARGET_BACKBUFFER || !m_pDevice->SceneColor() || !m_pDevice->SceneDepth() )
		return reject( -4, "render target 0 is not the scene or the caller does not own recording" );
	if ( !m_pDevice->IsWindowed() )
		return reject( -4, "exclusive fullscreen" );
	const UINT nWidth = static_cast<UINT>( m_pDevice->SceneWidth() ), nHeight = static_cast<UINT>( m_pDevice->SceneHeight() );
	TextureRecord *pMotion = m_nMotionResolvedFrame == m_nFrameCounter ? FindTexture( m_hMotionResolvedHandle ) : nullptr;
	if ( !pMotion || !( pMotion->flags & TEXTURE_CREATE_RENDERTARGET ) || pMotion->format != IMAGE_FORMAT_RGBA16161616F || pMotion->width != static_cast<int>( nWidth ) ||
	    pMotion->height != static_cast<int>( nHeight ) || !EnsureTextureResident( *pMotion ) || !pMotion->resource )
		return reject( -5, "no scene-sized motion-vector resolve this frame" );
	if ( m_pDevice->FrameGenSelectPending() )
	{
		nStatus = 0; // the switch applies at this frame's Present; the first dispatch follows next frame
		return;
	}
	if ( m_FrameGenKind == FrameGenKindDX12::None )
		return reject( m_bFrameGenSelectFailed ? -2 : -1, m_bFrameGenSelectFailed ? "provider creation failed" : "no provider for the selected mode" );
	if ( m_nFrameGenQueuedFrame == m_nFrameCounter )
		return; // one dispatch per frame
	CFrameGenDX12 &frameGen = m_pDevice->FrameGen();
	// Camera of the main pass (captured at DX12_MOTION_PASS_BEGIN_MAIN, the pass the motion resolve came from).
	VMatrix view, proj;
	memcpy( view.Base(), m_MotionMainView, sizeof( m_MotionMainView ) );
	memcpy( proj.Base(), m_MotionMainProj, sizeof( m_MotionMainProj ) );
	// Depth range/FOV come from the projection; the client's camera floats take precedence below and cover
	// projections the derivation rejects.
	float flDerivedNear = 0.f, flDerivedFar = 0.f, flDerivedFov = 0.f;
	bool bInverted = false;
	ProjectionDepthRange( proj, flDerivedNear, flDerivedFar, bInverted, flDerivedFov );
	if ( frameGen.NeedsContext( nWidth, nHeight, bInverted ) )
	{
		// Context (re)creation needs no recorded callback in flight that could still name the old one.
		if ( !WaitUpscalerGpuIdle() || !frameGen.EnsureContext( nWidth, nHeight, bInverted ) )
			return reject( -2, frameGen.LastError() );
	}
	FlushBufferedPrimitives();
	CommitTransforms();
	CCommandRecorderDX12 *pList = m_pDevice->CommandList();
	if ( !pList )
		return reject( -4, "recording ended during context preparation" );
	while ( m_nFrameGenPendingSerial + 1 - m_nFrameGenConsumedSerial > kFrameGenReplaySlots )
		ConsumeFrameGenReplays( true );
	// Every provider reads depth in DEPTH_WRITE (the tagged/transitioned state) from here to the present.
	m_pDevice->TransitionSceneDepth( D3D12_RESOURCE_STATE_DEPTH_WRITE );
	FrameGenDispatchDX12 d;
	if ( frameGen.Hudless() )
	{
		if ( !frameGen.EnsureHudless( nWidth, nHeight ) )
			return reject( -2, frameGen.LastError() );
		D3D12_RESOURCE_STATES hudlessState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
		if ( !EncodeSceneTo( frameGen.HudlessTexture(), hudlessState ) )
			return reject( -2, "hudless encode failed" );
		if ( hudlessState != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE )
		{
			D3D12_RESOURCE_BARRIER barrier{};
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Transition.pResource = frameGen.HudlessTexture();
			barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			barrier.Transition.StateBefore = hudlessState;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
			pList->ResourceBarrier( 1, &barrier );
		}
		d.hudless = frameGen.HudlessTexture();
		d.hudlessValid = true;
	}
	// DLSS-G tags the motion vectors in RENDER_TARGET for the whole present, and every provider hands them back
	// in that state; a sampling pass (motion blur, post) may have left them in a shader-read state.
	const int nFaces = ( pMotion->flags & TEXTURE_CREATE_CUBEMAP ) ? 6 : 1, nSub = pMotion->currentCopy * nFaces * pMotion->mipLevels;
	if ( pMotion->subresourceStates[nSub] != D3D12_RESOURCE_STATE_RENDER_TARGET )
	{
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = pMotion->resource.Get();
		barrier.Transition.Subresource = nSub;
		barrier.Transition.StateBefore = pMotion->subresourceStates[nSub];
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		pList->ResourceBarrier( 1, &barrier );
		pMotion->subresourceStates[nSub] = D3D12_RESOURCE_STATE_RENDER_TARGET;
		pMotion->sampledStateValid = false;
		++m_nTextureStateEpoch;
	}
	const float *pCamera = m_RenderingFloats;
	d.depth = m_pDevice->SceneDepth();
	d.depthBefore = m_pDevice->SceneDepthState();
	d.motion = pMotion->resource.Get();
	d.motionBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	d.width = nWidth;
	d.height = nHeight;
	d.frameId = m_nFrameGenFrameIdFrame == m_nFrameCounter ? m_nFrameGenFrameId : m_pDevice->NextPresentId();
	d.presentSerial = m_pDevice->NextPresentSerial();
	d.jitter[0] = m_UpscalerFrameJitter[0];
	d.jitter[1] = m_UpscalerFrameJitter[1];
	// _rt_MotionVectors.xy = current - previous in UV; providers take previous - current in pixels.
	d.motionScale[0] = -static_cast<float>( nWidth );
	d.motionScale[1] = -static_cast<float>( nHeight );
	const double flNow = Plat_FloatTime();
	d.frameTimeMs = m_flFrameGenLastDispatchTime > 0.0 ? static_cast<float>( Clamp( ( flNow - m_flFrameGenLastDispatchTime ) * 1000.0, 1.0, 100.0 ) ) : 16.6667f;
	d.reset = ( nFlags & DX12_FRAMEGEN_DISPATCH_RESET ) != 0 || m_bFrameGenHistoryGap || !m_MotionPrevViewProjValid[0];
	FrameGenCameraDX12 &camera = d.camera;
	camera.nearPlane = pCamera[FLOAT_RENDERPARM_DX12_UPSCALE_NEAR] > 0.f ? pCamera[FLOAT_RENDERPARM_DX12_UPSCALE_NEAR] : flDerivedNear;
	camera.farPlane = pCamera[FLOAT_RENDERPARM_DX12_UPSCALE_FAR] > 0.f ? pCamera[FLOAT_RENDERPARM_DX12_UPSCALE_FAR] : flDerivedFar;
	camera.fovY = pCamera[FLOAT_RENDERPARM_DX12_UPSCALE_FOV_Y] > 0.f ? pCamera[FLOAT_RENDERPARM_DX12_UPSCALE_FOV_Y] : flDerivedFov;
	camera.aspect = static_cast<float>( nWidth ) / static_cast<float>( nHeight );
	camera.inverted = bInverted;
	if ( !( camera.nearPlane > 0.f ) || !( camera.farPlane > camera.nearPlane ) || !( camera.fovY > 0.f ) )
		return reject( -4, "the main pass has no perspective projection and no camera parameters were sent" );
	VMatrix cameraToWorld, invProj, curVP, invCurVP, prevVP, invPrevVP;
	if ( !MatrixInverseGeneral( view, cameraToWorld ) || !MatrixInverseGeneral( proj, invProj ) )
		return reject( -4, "the main pass view/projection is singular" );
	curVP = proj * view;
	if ( m_MotionPrevViewProjValid[0] )
		memcpy( prevVP.Base(), m_MotionPrevViewProj[0], sizeof( float ) * 16 );
	else
		prevVP = curVP;
	if ( !MatrixInverseGeneral( curVP, invCurVP ) || !MatrixInverseGeneral( prevVP, invPrevVP ) )
		return reject( -4, "the main pass view-projection is singular" );
	TransposeTo( view, camera.view );
	TransposeTo( proj, camera.proj );
	TransposeTo( proj, camera.viewToClip );
	TransposeTo( invProj, camera.clipToView );
	TransposeTo( prevVP * invCurVP, camera.clipToPrevClip );
	TransposeTo( curVP * invPrevVP, camera.prevClipToClip );
	// Source view space: +x right, +y up, -z forward (MatrixBuildPerspectiveX sets m[3][2] = -1).
	for ( int i = 0; i < 3; ++i )
	{
		camera.pos[i] = cameraToWorld[i][3];
		camera.right[i] = cameraToWorld[i][0];
		camera.up[i] = cameraToWorld[i][1];
		camera.fwd[i] = -cameraToWorld[i][2];
	}
	const uint64_t nSerial = m_nFrameGenPendingSerial + 1;
	d.frame = m_nFrameCounter;
	d.serial = nSerial;
	d.result = &m_FrameGenReplay[nSerial % kFrameGenReplaySlots];
	if ( !frameGen.RecordDispatch( *pList, d ) )
		return reject( -2, frameGen.LastError()[0] ? frameGen.LastError() : "provider rejected the frame" );
	m_nFrameGenPendingSerial = nSerial;
	pList->Flush();
	m_Pipeline.InvalidateGraphicsBindings();
	ID3D12Resource *const pRetained[] = { d.depth, d.motion, d.hudless };
	for ( size_t i = 0; i < ARRAYSIZE( pRetained ); ++i )
		m_pDevice->RetainResource( pRetained[i] );
	m_pDevice->SetSceneStatesAfterExternal( m_pDevice->SceneColorState(), D3D12_RESOURCE_STATE_DEPTH_WRITE );
	m_nFrameGenQueuedFrame = m_nFrameCounter;
	m_nFrameGenLatchedFrameId = d.frameId;
	m_flFrameGenLastDispatchTime = flNow;
	m_bFrameGenHistoryGap = false;
	nStatus = 1 | ( static_cast<int>( m_FrameGenKind ) << 8 ) | ( static_cast<int>( frameGen.Generated() ) << 16 );
}

//-----------------------------------------------------------------------------
// Purpose: Releases presentation/features without recreating chains during device-resource teardown
//-----------------------------------------------------------------------------
void CShaderAPIDX12::ReleaseFrameGenResources()
{
	if ( m_pDevice && m_pDevice->IsRecordingOwner() && ( m_pDevice->FrameGen().Kind() != FrameGenKindDX12::None || m_pDevice->FrameGenSelectPending() ) )
	{
		m_pDevice->ReleaseResources(); // drains work and releases every adopted chain, without creating a new one
		m_pDevice->FrameGen().ReleaseFeatures(); // tagged inputs may now be destroyed; queue/provider lifetime remains device-owned
	}
	if ( m_nFrameGenPendingSerial != m_nFrameGenConsumedSerial )
	{
		WaitUpscalerGpuIdle();
		ConsumeFrameGenReplays( false );
	}
	m_nFrameGenRequest = 0;
	m_FrameGenKind = FrameGenKindDX12::None;
	m_bFrameGenSelectFailed = m_bFrameGenWarned = false;
	m_bFrameGenHistoryGap = true;
	m_nFrameGenFrameToken = m_nFrameGenQueuedFrame = m_nFrameGenFrameIdFrame = ~0ull;
	m_nFrameGenFrameId = m_nFrameGenLatchedFrameId = 0;
	m_flFrameGenLastDispatchTime = 0.0;
	m_nFrameGenPendingSerial = m_nFrameGenConsumedSerial = 0;
	m_flFrameGenFpsLimit = 0.f;
	for ( FrameGenReplayResultDX12 &slot : m_FrameGenReplay )
	{
		slot.serial = 0;
		slot.code = 0;
		slot.frame = ~0ull;
	}
	m_nFrameGenStatus = 0;
	m_nFramesShown = 1;
	m_nReflexStatus = 0;
	m_nReflexApplied = -1; // re-applied once the device is back
}

} // namespace shaderapidx12
