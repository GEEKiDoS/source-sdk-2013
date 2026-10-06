//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DX12 frame-generation providers (DLSS-G via Streamline, FSR frame generation via the FidelityFX
//          frame-interpolation swap chain, XeSS-FG via XeFG + XeLL). See framegen_dx12.h for the threading
//          contract. The device owns one instance and drives it from Select/CreateView/ResizeView/Present.
//
//=============================================================================//
#include "framegen_dx12.h"
#include "command_recorder_dx12.h"
#include "provider_module_dx12.h"
#include "materialsystem/imaterialsystem.h"
#include "tier0/dbg.h"
#include "tier1/strtools.h"
#include "tier0/icommandline.h"
#include <stdarg.h>
#include <initializer_list>
#include <type_traits>
// Streamline 2.11.1 headers against the 2.13.0 runtime: SL structures are versioned, older headers are the safe side.
#include <sl.h>
#include <sl_dlss_g.h>
#include <sl_reflex.h>
#include <sl_pcl.h>
// FidelityFX SDK 2.3 frame generation: amd_fidelityfx_loader_dx12.dll dispatches to amd_fidelityfx_framegeneration_dx12.dll.
#include <framegeneration/include/ffx_framegeneration.h>
#include <framegeneration/include/dx12/ffx_api_framegeneration_dx12.h>
// XeSS-FG 1.3 (libxess_fg.dll) and XeLL 1.3 (libxell.dll).
#include <xess_fg/xefg_swapchain_d3d12.h>
#include <xell/xell_d3d12.h>

namespace shaderapidx12
{
bool VerifyStreamlineSignatureDX12( const wchar_t *pszFullPath ); // framegen_sl_dx12.cpp

//-----------------------------------------------------------------------------
// Purpose: Console/log name of a provider kind
//-----------------------------------------------------------------------------
const char *FrameGenKindNameDX12( FrameGenKindDX12 kind )
{
	switch ( kind )
	{
	case FrameGenKindDX12::DLSSG:
		return "DLSS-G";
	case FrameGenKindDX12::FSR:
		return "FSR frame generation";
	case FrameGenKindDX12::XeFG:
		return "XeSS-FG";
	default:
		return "none";
	}
}

namespace
{
constexpr uint32_t kFrameIdMask = 0x0FFFFFFFu;
constexpr float kViewSpaceToMeters = 0.0254f; // Source units are inches
constexpr D3D12_RESOURCE_STATES kNpsr = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
constexpr D3D12_RESOURCE_STATES kRead = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
// Replay failure codes (provider results are folded into the low 16 bits).
constexpr uint32_t kReplayNoContext = 0x10002u;

struct FrameGenReplayPayloadDX12
{
	const CFrameGenDX12 *owner;
	FrameGenDispatchDX12 dispatch;
};

static_assert( std::is_trivially_copyable<FrameGenReplayPayloadDX12>::value, "external command payloads are copied bytewise" );

void Transition( ID3D12GraphicsCommandList *pList, ID3D12Resource *pResource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after )
{
	if ( !pResource || before == after )
		return;
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = pResource;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = before;
	barrier.Transition.StateAfter = after;
	pList->ResourceBarrier( 1, &barrier );
}

// Frame ids are 28-bit and never 0 (XeFG rejects present id 0).
uint32_t ClampFrameId( uint32_t nId )
{
	nId &= kFrameIdMask;
	return nId ? nId : 1u;
}

void SlLog( sl::LogType type, const char *pszMessage )
{
	const char *pszText = pszMessage ? pszMessage : "";
	const int nLength = V_strlen( pszText );
	const char *pszNewline = nLength && pszText[nLength - 1] == '\n' ? "" : "\n";
	if ( type == sl::LogType::eError )
		Warning( "ShaderAPIDX12 framegen: SL error: %s%s", pszText, pszNewline );
	else if ( type == sl::LogType::eWarn )
		Warning( "ShaderAPIDX12 framegen: SL: %s%s", pszText, pszNewline );
	else
		Msg( "ShaderAPIDX12 framegen: SL: %s%s", pszText, pszNewline );
}

void DlssgError( const sl::APIError &error )
{
	Warning( "ShaderAPIDX12 framegen: DLSS-G API error 0x%08x\n", static_cast<unsigned>( error.hres ) );
}

void XefgLog( const char *pszMessage, xefg_swapchain_logging_level_t level, void * )
{
	if ( level >= XEFG_SWAPCHAIN_LOGGING_LEVEL_WARNING )
		Warning( "ShaderAPIDX12 framegen: XeFG: %s\n", pszMessage ? pszMessage : "" );
	else
		Msg( "ShaderAPIDX12 framegen: XeFG: %s\n", pszMessage ? pszMessage : "" );
}

void XellLog( const char *pszMessage, xell_logging_level_t level )
{
	if ( level >= XELL_LOGGING_LEVEL_WARNING )
		Warning( "ShaderAPIDX12 framegen: XeLL: %s\n", pszMessage ? pszMessage : "" );
	else
		Msg( "ShaderAPIDX12 framegen: XeLL: %s\n", pszMessage ? pszMessage : "" );
}

// Picks a provider from an FFX GET_VERSIONS list (newest first): the first entry, logging every name.
bool SelectFfxVersion( PfnFfxQuery query, ID3D12Device *pDevice, uint64_t nCreateDescType, uint64_t &nVersionId, CUtlString &names )
{
	uint64_t nCount = 0;
	ffxQueryDescGetVersions versions{};
	versions.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
	versions.createDescType = nCreateDescType;
	versions.device = pDevice;
	versions.outputCount = &nCount;
	if ( query( nullptr, &versions.header ) != FFX_API_RETURN_OK || !nCount )
		return false;
	CUtlVector<uint64_t> ids;
	CUtlVector<const char *> versionNames;
	ids.SetCount( static_cast<int>( nCount ) );
	ids.FillWithValue( 0 );
	versionNames.SetCount( static_cast<int>( nCount ) );
	versionNames.FillWithValue( nullptr );
	versions.versionIds = ids.Base();
	versions.versionNames = versionNames.Base();
	if ( query( nullptr, &versions.header ) != FFX_API_RETURN_OK )
		return false;
	names.Clear();
	for ( int i = 0; i < versionNames.Count(); ++i )
	{
		names += i ? ", " : "";
		names += versionNames[i] ? versionNames[i] : "?";
	}
	nVersionId = ids[0];
	return true;
}

void SetFloat4x4( sl::float4x4 &m, const float ( &rows )[16] )
{
	for ( uint32_t i = 0; i < 4; ++i )
		m.setRow( i, sl::float4( rows[i * 4 + 0], rows[i * 4 + 1], rows[i * 4 + 2], rows[i * 4 + 3] ) );
}
} // namespace

// Streamline exports (sl.interposer.dll) and the feature functions resolved through slGetFeatureFunction.
struct CFrameGenDX12::SlApi
{
	HMODULE module = nullptr; // never freed: Streamline must stay resident while any proxy object exists
	PFun_slInit *init = nullptr;
	PFun_slShutdown *shutdown = nullptr;
	PFun_slIsFeatureSupported *isFeatureSupported = nullptr;
	PFun_slSetFeatureLoaded *setFeatureLoaded = nullptr;
	PFun_slSetTagForFrame *setTagForFrame = nullptr;
	PFun_slSetConstants *setConstants = nullptr;
	PFun_slGetFeatureRequirements *getFeatureRequirements = nullptr;
	PFun_slUpgradeInterface *upgradeInterface = nullptr;
	PFun_slGetNativeInterface *getNativeInterface = nullptr;
	PFun_slGetFeatureFunction *getFeatureFunction = nullptr;
	PFun_slGetNewFrameToken *getNewFrameToken = nullptr;
	PFun_slSetD3DDevice *setD3DDevice = nullptr;
	PFun_slFreeResources *freeResources = nullptr;
	PFun_slDLSSGSetOptions *dlssgSetOptions = nullptr;
	PFun_slDLSSGGetState *dlssgGetState = nullptr;
	PFun_slReflexSetOptions *reflexSetOptions = nullptr;
	PFun_slReflexSleep *reflexSleep = nullptr;
	PFun_slReflexGetState *reflexGetState = nullptr;
	PFun_slPCLSetMarker *pclSetMarker = nullptr;
	wchar_t pluginPath[MAX_PATH] = {};
	const wchar_t *pluginPaths[1] = {};
	bool reflexReported = false;
};

struct CFrameGenDX12::FfxFgApi
{
	HMODULE loader = nullptr, effect = nullptr; // effect loaded first by full path so the loader's bare-name load binds it
	PfnFfxCreateContext createContext = nullptr;
	PfnFfxDestroyContext destroyContext = nullptr;
	PfnFfxConfigure configure = nullptr;
	PfnFfxQuery query = nullptr;
	PfnFfxDispatch dispatch = nullptr;
	uint64_t fgVersionId = 0, swapChainVersionId = 0;
};

struct CFrameGenDX12::XefgApi
{
	HMODULE xell = nullptr, xefg = nullptr;
	decltype( &xefgSwapChainGetVersion ) getVersion = nullptr;
	decltype( &xefgSwapChainD3D12CreateContext ) createContext = nullptr;
	decltype( &xefgSwapChainD3D12GetProperties ) getProperties = nullptr;
	decltype( &xefgSwapChainD3D12InitFromSwapChain ) initFromSwapChain = nullptr;
	decltype( &xefgSwapChainD3D12GetSwapChainPtr ) getSwapChainPtr = nullptr;
	decltype( &xefgSwapChainD3D12TagFrameResource ) tagFrameResource = nullptr;
	decltype( &xefgSwapChainTagFrameConstants ) tagFrameConstants = nullptr;
	decltype( &xefgSwapChainSetEnabled ) setEnabled = nullptr;
	decltype( &xefgSwapChainSetPresentId ) setPresentId = nullptr;
	decltype( &xefgSwapChainGetLastPresentStatus ) getLastPresentStatus = nullptr;
	decltype( &xefgSwapChainSetLoggingCallback ) setLoggingCallback = nullptr;
	decltype( &xefgSwapChainDestroy ) destroy = nullptr;
	decltype( &xefgSwapChainSetLatencyReduction ) setLatencyReduction = nullptr;
	decltype( &xefgSwapChainSetNumInterpolatedFrames ) setNumInterpolatedFrames = nullptr;
	decltype( &xefgSwapChainSetUiCompositionState ) setUiCompositionState = nullptr;
	decltype( &xellD3D12CreateContext ) xellCreateContext = nullptr;
	decltype( &xellDestroyContext ) xellDestroy = nullptr;
	decltype( &xellSetSleepMode ) xellSetSleepMode = nullptr;
	decltype( &xellSleep ) xellSleep = nullptr;
	decltype( &xellAddMarkerData ) xellAddMarkerData = nullptr;
	decltype( &xellSetLoggingCallback ) xellSetLoggingCallback = nullptr;
	decltype( &xellGetVersion ) xellGetVersion = nullptr;
};

CFrameGenDX12::CFrameGenDX12() = default;

CFrameGenDX12::~CFrameGenDX12()
{
	Shutdown();
}

void CFrameGenDX12::Fail( const char *pszFormat, ... )
{
	char szMessage[512];
	va_list args;
	va_start( args, pszFormat );
	V_vsnprintf( szMessage, sizeof( szMessage ), pszFormat, args );
	va_end( args );
	m_LastError = szMessage;
	Warning( "ShaderAPIDX12 framegen: %s\n", szMessage );
}

//-----------------------------------------------------------------------------
// Purpose: Decides provider availability for this device and logs it
//-----------------------------------------------------------------------------
bool CFrameGenDX12::Initialize( ID3D12Device *pDevice, IDXGIFactory6 *pFactory, const MaterialAdapterInfo_t &adapter, const wchar_t *pszModuleDir, bool bVerbose )
{
	Shutdown();
	if ( !pDevice || !pFactory )
		return false;
	m_pDevice = pDevice;
	m_pFactory = pFactory;
	m_bVerbose = bVerbose;
	m_nVendor = adapter.m_VendorID;
	m_nDeviceId = adapter.m_DeviceID;
	m_AdapterLuid = pDevice->GetAdapterLuid();
	V_wcscpy_safe( m_szDir, pszModuleDir );
	AppendSeparator( m_szDir, MAX_PATH );
	LoadStreamline();
	LoadFfx();
	LoadXefg();
	Msg( "ShaderAPIDX12 framegen: adapter vendor 0x%04x device 0x%04x\n", m_nVendor, m_nDeviceId );
	const char *const pszNames[] = { "dlssg", "fsr", "xefg" };
	const Provider *const pProviders[] = { &m_Dlssg, &m_Fsr, &m_Xefg };
	for ( size_t i = 0; i < ARRAYSIZE( pProviders ); ++i )
	{
		if ( pProviders[i]->available )
			Msg( "ShaderAPIDX12 framegen: %s available (%s)\n", pszNames[i], pProviders[i]->version.Get() );
		else
			Msg( "ShaderAPIDX12 framegen: %s unavailable: %s\n", pszNames[i], pProviders[i]->reason.Get() );
	}
	return true;
}

FrameGenKindDX12 CFrameGenDX12::Resolve( int nMode ) const
{
	switch ( nMode )
	{
	case 1:
		if ( m_Dlssg.available )
			return FrameGenKindDX12::DLSSG;
		if ( m_Fsr.available )
			return FrameGenKindDX12::FSR;
		return m_Xefg.available ? FrameGenKindDX12::XeFG : FrameGenKindDX12::None;
	case 2:
		return m_Dlssg.available ? FrameGenKindDX12::DLSSG : FrameGenKindDX12::None;
	case 3:
		return m_Fsr.available ? FrameGenKindDX12::FSR : FrameGenKindDX12::None;
	case 4:
		return m_Xefg.available ? FrameGenKindDX12::XeFG : FrameGenKindDX12::None;
	default:
		return FrameGenKindDX12::None;
	}
}

const char *CFrameGenDX12::UnavailableReason( FrameGenKindDX12 kind ) const
{
	switch ( kind )
	{
	case FrameGenKindDX12::DLSSG:
		return m_Dlssg.available ? "available" : m_Dlssg.reason.Get();
	case FrameGenKindDX12::FSR:
		return m_Fsr.available ? "available" : m_Fsr.reason.Get();
	case FrameGenKindDX12::XeFG:
		return m_Xefg.available ? "available" : m_Xefg.reason.Get();
	default:
		return "no provider";
	}
}

DXGI_FORMAT CFrameGenDX12::PresentFormat() const
{
	return m_Kind == FrameGenKindDX12::DLSSG || m_Kind == FrameGenKindDX12::XeFG ? DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_R16G16B16A16_FLOAT;
}

IDXGIFactory6 *CFrameGenDX12::SwapChainFactory() const
{
	return m_Kind == FrameGenKindDX12::DLSSG ? m_pSlFactory.Get() : nullptr;
}

//-----------------------------------------------------------------------------
// Streamline (DLSS-G)
//-----------------------------------------------------------------------------
void CFrameGenDX12::LoadStreamline()
{
	if ( m_nVendor != 0x10de )
	{
		m_Dlssg.reason = "DLSS-G needs an NVIDIA RTX adapter";
		return;
	}
	wchar_t szPath[MAX_PATH];
	if ( !BuildPath( szPath, m_szDir, L"sl.interposer.dll" ) || GetFileAttributesW( szPath ) == INVALID_FILE_ATTRIBUTES )
	{
		m_Dlssg.reason = "sl.interposer.dll not installed beside the renderer";
		return;
	}
	for ( const wchar_t *pszName : { L"sl.common.dll", L"sl.dlss_g.dll", L"sl.reflex.dll", L"sl.pcl.dll", L"nvngx_dlssg.dll" } )
	{
		if ( !FileExists( m_szDir, pszName ) )
		{
			m_Dlssg.reason.Format( "%ls not installed beside the renderer", pszName );
			return;
		}
	}
	static const bool s_bUnsigned = CommandLine() && CommandLine()->CheckParm( "-dx12slunsigned" );
	if ( s_bUnsigned )
		Warning( "ShaderAPIDX12 framegen: -dx12slunsigned: loading sl.interposer.dll WITHOUT verifying its signature (development only)\n" );
	else if ( !VerifyStreamlineSignatureDX12( szPath ) )
	{
		m_Dlssg.reason = "sl.interposer.dll signature verification failed";
		return;
	}
	HMODULE hModule = LoadLibraryExW( szPath, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH );
	if ( !hModule )
	{
		m_Dlssg.reason.Format( "sl.interposer.dll failed to load (error %u)", static_cast<unsigned>( GetLastError() ) );
		return;
	}
	SlApi *pApi = new SlApi;
	pApi->module = hModule;
	if ( !ResolveExport( hModule, "slInit", pApi->init ) || !ResolveExport( hModule, "slShutdown", pApi->shutdown ) || !ResolveExport( hModule, "slIsFeatureSupported", pApi->isFeatureSupported ) ||
	    !ResolveExport( hModule, "slSetFeatureLoaded", pApi->setFeatureLoaded ) || !ResolveExport( hModule, "slSetTagForFrame", pApi->setTagForFrame ) || !ResolveExport( hModule, "slSetConstants", pApi->setConstants ) ||
	    !ResolveExport( hModule, "slGetFeatureRequirements", pApi->getFeatureRequirements ) || !ResolveExport( hModule, "slUpgradeInterface", pApi->upgradeInterface ) ||
	    !ResolveExport( hModule, "slGetNativeInterface", pApi->getNativeInterface ) || !ResolveExport( hModule, "slGetFeatureFunction", pApi->getFeatureFunction ) ||
	    !ResolveExport( hModule, "slGetNewFrameToken", pApi->getNewFrameToken ) || !ResolveExport( hModule, "slSetD3DDevice", pApi->setD3DDevice ) || !ResolveExport( hModule, "slFreeResources", pApi->freeResources ) )
	{
		delete pApi;
		m_Dlssg.reason = "sl.interposer.dll lacks a required export";
		return;
	}
	// The interposer stays loaded: its proxies and plugins are not unloadable mid-process.
	V_wcscpy_safe( pApi->pluginPath, m_szDir );
	const int nLength = V_wcslen( pApi->pluginPath );
	if ( nLength > 1 && ( pApi->pluginPath[nLength - 1] == L'\\' || pApi->pluginPath[nLength - 1] == L'/' ) )
		pApi->pluginPath[nLength - 1] = 0;
	pApi->pluginPaths[0] = pApi->pluginPath;
	m_pSl = pApi;
	char szVersion[64], szDlssgVersion[64];
	FileVersion( m_szDir, L"sl.interposer.dll", szVersion, sizeof( szVersion ) );
	FileVersion( m_szDir, L"nvngx_dlssg.dll", szDlssgVersion, sizeof( szDlssgVersion ) );
	// slInit is deferred to the first DLSS-G selection (F9); the feature probe runs there and may still demote it.
	m_Dlssg.version.Format( "Streamline %s, DLSS-G %s, signed; feature support checked on first use", szVersion, szDlssgVersion );
	m_Dlssg.available = true;
}

//-----------------------------------------------------------------------------
// Purpose: slInit once per device (manual hooking), proxies for device and factory, Reflex/DLSS-G support probes
//-----------------------------------------------------------------------------
bool CFrameGenDX12::EnsureStreamline()
{
	if ( m_bSlInitialized )
		return true;
	if ( !m_pSl )
		return false;
	static const bool s_bLog = CommandLine() && CommandLine()->CheckParm( "-dx12framegenlog" );
	const sl::Feature features[] = { sl::kFeatureDLSS_G, sl::kFeatureReflex, sl::kFeaturePCL };
	sl::Preferences pref{};
	pref.showConsole = false;
	pref.logLevel = s_bLog || m_bVerbose ? sl::LogLevel::eVerbose : sl::LogLevel::eDefault;
	pref.pathsToPlugins = m_pSl->pluginPaths;
	pref.numPathsToPlugins = 1;
	pref.pathToLogsAndData = s_bLog ? m_pSl->pluginPath : nullptr;
	pref.logMessageCallback = SlLog;
	pref.flags = static_cast<sl::PreferenceFlags>( static_cast<uint64_t>( sl::PreferenceFlags::eUseManualHooking ) | static_cast<uint64_t>( sl::PreferenceFlags::eDisableCLStateTracking ) |
	    static_cast<uint64_t>( sl::PreferenceFlags::eUseFrameBasedResourceTagging ) );
	pref.featuresToLoad = features;
	pref.numFeaturesToLoad = ARRAYSIZE( features );
	pref.engine = sl::EngineType::eCustom;
	pref.engineVersion = "Source SDK 2013 DX12";
	pref.projectId = "6c3d9c52-8c1e-4b2a-9f4e-1b5a0d7e2c41";
	pref.renderAPI = sl::RenderAPI::eD3D12;
	sl::Result result = m_pSl->init( pref, sl::kSDKVersion );
	if ( result != sl::Result::eOk )
	{
		Fail( "slInit failed (sl::Result %d)", static_cast<int>( result ) );
		m_Dlssg.available = false;
		m_Dlssg.reason = m_LastError;
		return false;
	}
	m_bSlInitialized = true;
	const auto fail = [&]( const char *pszWhat, sl::Result r )
	{
		Fail( "%s (sl::Result %d)", pszWhat, static_cast<int>( r ) );
		m_Dlssg.available = false;
		m_Dlssg.reason = m_LastError;
		m_pSlFactory.Reset();
		m_pSlDevice.Reset();
		m_pSl->shutdown();
		m_bSlInitialized = false;
		return false;
	};
	result = m_pSl->setD3DDevice( m_pDevice );
	if ( result != sl::Result::eOk )
		return fail( "slSetD3DDevice failed", result );
	sl::AdapterInfo adapterInfo{};
	adapterInfo.deviceLUID = reinterpret_cast<uint8_t *>( &m_AdapterLuid );
	adapterInfo.deviceLUIDSizeInBytes = sizeof( m_AdapterLuid );
	// Reflex is mandatory (standalone and under DLSS-G); DLSS-G itself only demotes that provider.
	result = m_pSl->isFeatureSupported( sl::kFeatureReflex, adapterInfo );
	if ( result != sl::Result::eOk )
		return fail( "Reflex is not supported on this adapter", result );
	result = m_pSl->isFeatureSupported( sl::kFeatureDLSS_G, adapterInfo );
	if ( result != sl::Result::eOk )
	{
		m_Dlssg.available = false;
		m_Dlssg.reason.Format( "DLSS-G is not supported on this adapter/OS (hardware-accelerated GPU scheduling off?) (sl::Result %d)", static_cast<int>( result ) );
		Warning( "ShaderAPIDX12 framegen: %s\n", m_Dlssg.reason.Get() );
	}
	m_pSlDevice = m_pDevice;
	result = m_pSl->upgradeInterface( reinterpret_cast<void **>( m_pSlDevice.GetAddressOf() ) );
	if ( result != sl::Result::eOk )
		return fail( "slUpgradeInterface(device) failed", result );
	m_pSlFactory = m_pFactory;
	result = m_pSl->upgradeInterface( reinterpret_cast<void **>( m_pSlFactory.GetAddressOf() ) );
	if ( result != sl::Result::eOk )
		return fail( "slUpgradeInterface(factory) failed", result );
	void *pFunction = nullptr;
	const struct
	{
		sl::Feature feature;
		const char *name;
		void **slot;
	} functions[] = {
		{ sl::kFeatureDLSS_G, "slDLSSGSetOptions", reinterpret_cast<void **>( &m_pSl->dlssgSetOptions ) },
		{ sl::kFeatureDLSS_G, "slDLSSGGetState", reinterpret_cast<void **>( &m_pSl->dlssgGetState ) },
		{ sl::kFeatureReflex, "slReflexSetOptions", reinterpret_cast<void **>( &m_pSl->reflexSetOptions ) },
		{ sl::kFeatureReflex, "slReflexSleep", reinterpret_cast<void **>( &m_pSl->reflexSleep ) },
		{ sl::kFeatureReflex, "slReflexGetState", reinterpret_cast<void **>( &m_pSl->reflexGetState ) },
		{ sl::kFeaturePCL, "slPCLSetMarker", reinterpret_cast<void **>( &m_pSl->pclSetMarker ) },
	};
	for ( const auto &fn : functions )
	{
		if ( fn.feature == sl::kFeatureDLSS_G && !m_Dlssg.available )
			continue;
		pFunction = nullptr;
		result = m_pSl->getFeatureFunction( fn.feature, fn.name, pFunction );
		if ( result != sl::Result::eOk || !pFunction )
		{
			char szWhat[128];
			V_snprintf( szWhat, sizeof( szWhat ), "slGetFeatureFunction(%s) failed", fn.name );
			return fail( szWhat, result );
		}
		*fn.slot = pFunction;
	}
	m_bDlssgLoaded = m_Dlssg.available; // featuresToLoad loaded it at init when the adapter supports it
	Msg( "ShaderAPIDX12 framegen: Streamline initialised (manual hooking); Reflex and PCL loaded, DLSS-G %s\n", m_Dlssg.available ? "loaded" : m_Dlssg.reason.Get() );
	return true;
}

bool CFrameGenDX12::RecreateNativeQueue( ID3D12CommandQueue *&pQueue )
{
	D3D12_COMMAND_QUEUE_DESC desc{};
	desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	ID3D12CommandQueue *pNew = nullptr;
	const HRESULT hr = m_pDevice->CreateCommandQueue( &desc, IID_PPV_ARGS( &pNew ) );
	if ( FAILED( hr ) )
	{
		Fail( "CreateCommandQueue (native) failed (0x%08x)", static_cast<unsigned>( hr ) );
		return false;
	}
	pQueue = pNew;
	m_bQueueIsProxy = false;
	return true;
}

void CFrameGenDX12::ApplyReflex()
{
	if ( !m_bSlInitialized || !m_pSl->reflexSetOptions )
		return;
	sl::ReflexOptions options{};
	// XeFG hands latency to XeLL (one sleeper per frame); DLSS-G needs at least low latency; otherwise the user's mode.
	if ( m_Kind != FrameGenKindDX12::XeFG )
	{
		if ( m_nReflexMode == 2 )
			options.mode = sl::ReflexMode::eLowLatencyWithBoost;
		else if ( m_nReflexMode == 1 || m_Kind == FrameGenKindDX12::DLSSG )
			options.mode = sl::ReflexMode::eLowLatency;
	}
	// With DLSS-G on, Reflex's limiter caps the displayed rate (measured: frameLimitUs = 1/fps_max halves the
	// rendered rate to fps_max/2). fps_max caps rendered frames, so the displayed cap is fps_max x frames shown.
	const float flDisplayLimit = m_Kind == FrameGenKindDX12::DLSSG ? m_flFpsLimit * float( 1 + m_nGenerated ) : m_flFpsLimit;
	if ( options.mode != sl::ReflexMode::eOff )
		options.frameLimitUs = flDisplayLimit > 0.f ? static_cast<uint32_t>( 1000000.f / flDisplayLimit ) : 0u;
	const sl::Result result = m_pSl->reflexSetOptions( options );
	if ( result != sl::Result::eOk )
		Warning( "ShaderAPIDX12 framegen: slReflexSetOptions failed (sl::Result %d)\n", static_cast<int>( result ) );
}

//-----------------------------------------------------------------------------
// Purpose: Standalone Reflex: Streamline comes up on first use, the mode is re-applied at every kind switch
//-----------------------------------------------------------------------------
int CFrameGenDX12::SetReflexMode( int nMode )
{
	AUTO_LOCK( m_MarkerMutex );
	m_nReflexMode = MAX( 0, MIN( nMode, 2 ) );
	if ( !m_nReflexMode )
	{
		ApplyReflex(); // back to DLSS-G's own requirement or off
		return 0;
	}
	if ( !m_pSl )
	{
		m_LastError = m_Dlssg.reason; // vendor / module / signature reason from LoadStreamline
		return -1;
	}
	if ( !EnsureStreamline() )
		return -1; // m_LastError set by Fail
	ApplyReflex();
	return 1;
}

bool CFrameGenDX12::SelectDlssg( ID3D12CommandQueue *&pQueue )
{
	if ( !EnsureStreamline() )
		return false;
	if ( !m_Dlssg.available )
	{
		Fail( "%s", m_Dlssg.reason.Get() );
		return false;
	}
	if ( !m_bDlssgLoaded )
	{
		const sl::Result result = m_pSl->setFeatureLoaded( sl::kFeatureDLSS_G, true );
		if ( result != sl::Result::eOk )
		{
			Fail( "slSetFeatureLoaded(DLSS-G, true) failed (sl::Result %d)", static_cast<int>( result ) );
			return false;
		}
		m_bDlssgLoaded = true;
	}
	// The presenting queue must come from the proxy device so Streamline tracks it.
	D3D12_COMMAND_QUEUE_DESC desc{};
	desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	ID3D12CommandQueue *pNew = nullptr;
	const HRESULT hr = m_pSlDevice->CreateCommandQueue( &desc, IID_PPV_ARGS( &pNew ) );
	if ( FAILED( hr ) )
	{
		Fail( "CreateCommandQueue through the Streamline proxy failed (0x%08x)", static_cast<unsigned>( hr ) );
		return false;
	}
	pQueue = pNew;
	m_bQueueIsProxy = true;
	m_Kind = FrameGenKindDX12::DLSSG;
	// numFramesToGenerateMax is learned from slDLSSGGetState on the presenting thread (AfterPresent); until then the
	// request is the multiplier and the plugin clamps it itself.
	m_nGenerated = m_nDlssgMax ? MIN( m_nMultiplier - 1, m_nDlssgMax ) : m_nMultiplier - 1;
	m_bDlssgOptionsValid = false;
	m_bDlssgStatusLogged = false;
	ApplyReflex();
	Msg( "ShaderAPIDX12 framegen: DLSS-G selected, %u generated frame(s) per rendered frame requested\n", m_nGenerated );
	return true;
}

void CFrameGenDX12::ReleaseDlssg( ID3D12CommandQueue *&pQueue )
{
	if ( m_bSlInitialized )
	{
		if ( m_bDlssgOptionsValid )
			DlssgSetOptions( false, m_nDlssgWidth, m_nDlssgHeight );
		m_Kind = FrameGenKindDX12::None;
		ApplyReflex();
		m_pSl->freeResources( sl::kFeatureDLSS_G, sl::ViewportHandle( 0u ) );
		if ( m_bDlssgLoaded )
		{
			const sl::Result result = m_pSl->setFeatureLoaded( sl::kFeatureDLSS_G, false );
			if ( result != sl::Result::eOk )
				Warning( "ShaderAPIDX12 framegen: slSetFeatureLoaded(DLSS-G, false) failed (sl::Result %d)\n", static_cast<int>( result ) );
			m_bDlssgLoaded = false;
		}
	}
	m_bDlssgOptionsValid = false;
	if ( m_bQueueIsProxy )
		RecreateNativeQueue( pQueue );
}

bool CFrameGenDX12::DlssgSetOptions( bool bOn, uint32_t nWidth, uint32_t nHeight )
{
	sl::DLSSGOptions options{};
	options.mode = bOn ? sl::DLSSGMode::eOn : sl::DLSSGMode::eOff;
	options.numFramesToGenerate = MAX( 1u, m_nGenerated );
	options.flags = sl::DLSSGFlags::eRetainResourcesWhenOff;
	options.numBackBuffers = m_nDlssgBackBuffers;
	options.mvecDepthWidth = options.colorWidth = nWidth;
	options.mvecDepthHeight = options.colorHeight = nHeight;
	options.colorBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	options.mvecBufferFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
	options.depthBufferFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	options.hudLessBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	options.onErrorCallback = DlssgError;
	const sl::Result result = m_pSl->dlssgSetOptions( sl::ViewportHandle( 0u ), options );
	if ( result != sl::Result::eOk )
	{
		Warning( "ShaderAPIDX12 framegen: slDLSSGSetOptions(%s) failed (sl::Result %d)\n", bOn ? "on" : "off", static_cast<int>( result ) );
		m_nRuntimeError = 1;
		return false;
	}
	m_bDlssgOptionsValid = true;
	m_bDlssgOn = bOn;
	m_nDlssgWidth = nWidth;
	m_nDlssgHeight = nHeight;
	m_nDlssgGenerated = m_nGenerated;
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: DLSS-G per-frame inputs (thread-safe Streamline calls on the recording thread)
//-----------------------------------------------------------------------------
bool CFrameGenDX12::SubmitDlssgFrame( const FrameGenDispatchDX12 &d )
{
	sl::FrameToken *pToken = nullptr;
	const uint32_t nId = ClampFrameId( d.frameId );
	if ( m_pSl->getNewFrameToken( pToken, &nId ) != sl::Result::eOk || !pToken )
		return false;
	const sl::ViewportHandle viewport( 0u );
	sl::Constants constants{};
	SetFloat4x4( constants.cameraViewToClip, d.camera.viewToClip );
	SetFloat4x4( constants.clipToCameraView, d.camera.clipToView );
	static const float s_Identity[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
	SetFloat4x4( constants.clipToLensClip, s_Identity );
	SetFloat4x4( constants.clipToPrevClip, d.camera.clipToPrevClip );
	SetFloat4x4( constants.prevClipToClip, d.camera.prevClipToClip );
	constants.jitterOffset = sl::float2( d.jitter[0], d.jitter[1] );
	// _rt_MotionVectors is in UV units (current - previous); Streamline normalises mvec * mvecScale, and its
	// pixel-space scale for NGX is mvecScale * size, i.e. the (-W, -H) the upscaler uses.
	constants.mvecScale = sl::float2( -1.f, -1.f );
	constants.cameraPinholeOffset = sl::float2( 0.f, 0.f );
	constants.cameraPos = sl::float3( d.camera.pos[0], d.camera.pos[1], d.camera.pos[2] );
	constants.cameraUp = sl::float3( d.camera.up[0], d.camera.up[1], d.camera.up[2] );
	constants.cameraRight = sl::float3( d.camera.right[0], d.camera.right[1], d.camera.right[2] );
	constants.cameraFwd = sl::float3( d.camera.fwd[0], d.camera.fwd[1], d.camera.fwd[2] );
	constants.cameraNear = d.camera.nearPlane;
	constants.cameraFar = d.camera.farPlane;
	constants.cameraFOV = d.camera.fovY;
	constants.cameraAspectRatio = d.camera.aspect;
	constants.motionVectorsInvalidValue = 0.f;
	constants.depthInverted = d.camera.inverted ? sl::Boolean::eTrue : sl::Boolean::eFalse;
	constants.cameraMotionIncluded = sl::Boolean::eTrue;
	constants.motionVectors3D = sl::Boolean::eFalse;
	constants.reset = d.reset ? sl::Boolean::eTrue : sl::Boolean::eFalse;
	constants.orthographicProjection = sl::Boolean::eFalse;
	constants.motionVectorsDilated = sl::Boolean::eFalse;
	constants.motionVectorsJittered = sl::Boolean::eFalse;
	sl::Result result = m_pSl->setConstants( constants, *pToken, viewport );
	if ( result != sl::Result::eOk )
	{
		Warning( "ShaderAPIDX12 framegen: slSetConstants failed (sl::Result %d)\n", static_cast<int>( result ) );
		return false;
	}
	sl::Extent extent{};
	extent.width = d.width;
	extent.height = d.height;
	sl::Resource depth( sl::ResourceType::eTex2d, d.depth, D3D12_RESOURCE_STATE_DEPTH_WRITE );
	sl::Resource motion( sl::ResourceType::eTex2d, d.motion, D3D12_RESOURCE_STATE_RENDER_TARGET );
	sl::Resource hudless( sl::ResourceType::eTex2d, d.hudless, kNpsr );
	sl::ResourceTag tags[3] = {
		sl::ResourceTag( &depth, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent, &extent ),
		sl::ResourceTag( &motion, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilPresent, &extent ),
		sl::ResourceTag( &hudless, sl::kBufferTypeHUDLessColor, sl::ResourceLifecycle::eValidUntilPresent, &extent ),
	};
	result = m_pSl->setTagForFrame( *pToken, viewport, tags, d.hudlessValid && d.hudless ? 3u : 2u, nullptr );
	if ( result != sl::Result::eOk )
	{
		Warning( "ShaderAPIDX12 framegen: slSetTagForFrame failed (sl::Result %d)\n", static_cast<int>( result ) );
		return false;
	}
	return true;
}

void CFrameGenDX12::SlMarker( uint32_t nMarker, uint32_t nFrameId )
{
	if ( !m_bSlInitialized || !m_pSl->pclSetMarker || !m_pSl->reflexSleep )
		return;
	sl::FrameToken *pToken = nullptr;
	const uint32_t nId = ClampFrameId( nFrameId );
	if ( m_pSl->getNewFrameToken( pToken, &nId ) != sl::Result::eOk || !pToken )
		return;
	switch ( nMarker )
	{
	case 1: // simulation start: Reflex sleeps here, then the marker
		m_pSl->reflexSleep( *pToken );
		m_pSl->pclSetMarker( sl::PCLMarker::eSimulationStart, *pToken );
		break;
	case 2:
		m_pSl->pclSetMarker( sl::PCLMarker::eSimulationEnd, *pToken );
		break;
	case 3:
		m_pSl->pclSetMarker( sl::PCLMarker::eRenderSubmitStart, *pToken );
		break;
	case 4:
		m_pSl->pclSetMarker( sl::PCLMarker::eRenderSubmitEnd, *pToken );
		break;
	case 5:
		m_pSl->pclSetMarker( sl::PCLMarker::ePresentStart, *pToken );
		break;
	case 6:
		m_pSl->pclSetMarker( sl::PCLMarker::ePresentEnd, *pToken );
		break;
	default:
		break;
	}
}

//-----------------------------------------------------------------------------
// FidelityFX frame generation
//-----------------------------------------------------------------------------
void CFrameGenDX12::LoadFfx()
{
	HMODULE hEffect = LoadProviderModule( m_szDir, L"amd_fidelityfx_framegeneration_dx12.dll" );
	if ( !hEffect )
	{
		m_Fsr.reason = "amd_fidelityfx_framegeneration_dx12.dll not installed beside the renderer";
		return;
	}
	HMODULE hLoader = LoadProviderModule( m_szDir, L"amd_fidelityfx_loader_dx12.dll" );
	if ( !hLoader )
	{
		FreeLibrary( hEffect );
		m_Fsr.reason = "amd_fidelityfx_loader_dx12.dll not installed beside the renderer";
		return;
	}
	FfxFgApi *pApi = new FfxFgApi;
	pApi->effect = hEffect;
	pApi->loader = hLoader;
	const auto fail = [&]( const char *pszReason )
	{
		FreeLibrary( pApi->loader );
		FreeLibrary( pApi->effect );
		delete pApi;
		m_Fsr.reason = pszReason;
	};
	if ( !ResolveExport( hLoader, "ffxCreateContext", pApi->createContext ) || !ResolveExport( hLoader, "ffxDestroyContext", pApi->destroyContext ) || !ResolveExport( hLoader, "ffxConfigure", pApi->configure ) ||
	    !ResolveExport( hLoader, "ffxQuery", pApi->query ) || !ResolveExport( hLoader, "ffxDispatch", pApi->dispatch ) )
		return fail( "amd_fidelityfx_loader_dx12.dll lacks a required FFX API export" );
	CUtlString fgNames, swapNames;
	if ( !SelectFfxVersion( pApi->query, m_pDevice, FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION, pApi->fgVersionId, fgNames ) )
		return fail( "no FFX frame-generation provider enumerated" );
	if ( !SelectFfxVersion( pApi->query, m_pDevice, FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_WRAP_DX12, pApi->swapChainVersionId, swapNames ) )
		return fail( "no FFX frame-interpolation swap-chain provider enumerated" );
	char szVersion[64];
	FileVersion( m_szDir, L"amd_fidelityfx_framegeneration_dx12.dll", szVersion, sizeof( szVersion ) );
	m_Fsr.version.Format( "%s; swap chain %s; runtime %s", fgNames.Get(), swapNames.Get(), szVersion );
	m_pFfx = pApi;
	m_Fsr.available = true;
}

void CFrameGenDX12::DestroyFfxContexts()
{
	if ( !m_pFfx )
		return;
	if ( m_pFfxFgContext )
	{
		m_pFfx->destroyContext( &m_pFfxFgContext, nullptr );
		m_pFfxFgContext = nullptr;
	}
	m_nFfxWidth = m_nFfxHeight = 0;
	m_bFfxEnabled = false;
}

bool CFrameGenDX12::CreateFfxFgContext( uint32_t nWidth, uint32_t nHeight, bool bInverted )
{
	if ( !m_pFfx )
		return false;
	ffxOverrideVersion override{};
	override.header.type = FFX_API_DESC_TYPE_OVERRIDE_VERSION;
	override.versionId = m_pFfx->fgVersionId;
	ffxCreateContextDescFrameGenerationHudless hudless{};
	hudless.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION_HUDLESS;
	hudless.hudlessBackBufferFormat = FFX_API_SURFACE_FORMAT_R16G16B16A16_FLOAT;
	hudless.header.pNext = &override.header;
	ffxCreateContextDescFrameGenerationVersion version{};
	version.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION_VERSION;
	version.version = FFX_FRAMEGENERATION_VERSION;
	version.header.pNext = m_bHudless ? &hudless.header : &override.header;
	ffxCreateBackendDX12Desc backend{};
	backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
	backend.device = m_pDevice;
	backend.header.pNext = &version.header;
	ffxCreateContextDescFrameGeneration create{};
	create.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
	create.header.pNext = &backend.header;
	create.flags = FFX_FRAMEGENERATION_ENABLE_HIGH_DYNAMIC_RANGE | FFX_FRAMEGENERATION_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS | ( bInverted ? FFX_FRAMEGENERATION_ENABLE_DEPTH_INVERTED : 0 ) |
	    ( m_bVerbose ? FFX_FRAMEGENERATION_ENABLE_DEBUG_CHECKING : 0 );
	create.displaySize = { nWidth, nHeight };
	create.maxRenderSize = { nWidth, nHeight };
	create.backBufferFormat = FFX_API_SURFACE_FORMAT_R16G16B16A16_FLOAT;
	ffxContext context = nullptr;
	const ffxReturnCode_t created = m_pFfx->createContext( &context, &create.header, nullptr );
	if ( created != FFX_API_RETURN_OK || !context )
	{
		Fail( "ffxCreateContext(framegeneration) failed (%u) at %ux%u", created, nWidth, nHeight );
		return false;
	}
	m_pFfxFgContext = context;
	m_nFfxWidth = nWidth;
	m_nFfxHeight = nHeight;
	m_bFfxInverted = bInverted;
	m_bFfxEnabled = false;
	if ( m_bVerbose )
		Msg( "ShaderAPIDX12 framegen: FSR frame-generation context %ux%u inverted=%d\n", nWidth, nHeight, bInverted ? 1 : 0 );
	return true;
}

bool CFrameGenDX12::NeedsContext( uint32_t nWidth, uint32_t nHeight, bool bInverted ) const
{
	if ( m_Kind != FrameGenKindDX12::FSR )
		return false;
	return !m_pFfxFgContext || m_nFfxWidth != nWidth || m_nFfxHeight != nHeight || m_bFfxInverted != bInverted;
}

bool CFrameGenDX12::EnsureContext( uint32_t nWidth, uint32_t nHeight, bool bInverted )
{
	m_bInverted = bInverted;
	if ( !NeedsContext( nWidth, nHeight, bInverted ) )
		return true;
	DestroyFfxContexts();
	return CreateFfxFgContext( nWidth, nHeight, bInverted );
}

//-----------------------------------------------------------------------------
// XeSS-FG + XeLL
//-----------------------------------------------------------------------------
void CFrameGenDX12::LoadXefg()
{
	D3D12_FEATURE_DATA_SHADER_MODEL model{ D3D_SHADER_MODEL_6_4 };
	if ( FAILED( m_pDevice->CheckFeatureSupport( D3D12_FEATURE_SHADER_MODEL, &model, sizeof( model ) ) ) || model.HighestShaderModel < D3D_SHADER_MODEL_6_4 )
	{
		m_Xefg.reason = "XeSS-FG needs shader model 6.4";
		return;
	}
	HMODULE hXell = LoadProviderModule( m_szDir, L"libxell.dll" );
	if ( !hXell )
	{
		m_Xefg.reason = "libxell.dll not installed beside the renderer";
		return;
	}
	HMODULE hXefg = LoadProviderModule( m_szDir, L"libxess_fg.dll" );
	if ( !hXefg )
	{
		FreeLibrary( hXell );
		m_Xefg.reason = "libxess_fg.dll not installed beside the renderer";
		return;
	}
	XefgApi *pApi = new XefgApi;
	pApi->xell = hXell;
	pApi->xefg = hXefg;
	const auto fail = [&]( const char *pszReason )
	{
		FreeLibrary( pApi->xefg );
		FreeLibrary( pApi->xell );
		delete pApi;
		m_Xefg.reason = pszReason;
	};
	if ( !ResolveExport( hXefg, "xefgSwapChainGetVersion", pApi->getVersion ) || !ResolveExport( hXefg, "xefgSwapChainD3D12CreateContext", pApi->createContext ) ||
	    !ResolveExport( hXefg, "xefgSwapChainD3D12GetProperties", pApi->getProperties ) || !ResolveExport( hXefg, "xefgSwapChainD3D12InitFromSwapChain", pApi->initFromSwapChain ) ||
	    !ResolveExport( hXefg, "xefgSwapChainD3D12GetSwapChainPtr", pApi->getSwapChainPtr ) || !ResolveExport( hXefg, "xefgSwapChainD3D12TagFrameResource", pApi->tagFrameResource ) ||
	    !ResolveExport( hXefg, "xefgSwapChainTagFrameConstants", pApi->tagFrameConstants ) || !ResolveExport( hXefg, "xefgSwapChainSetEnabled", pApi->setEnabled ) ||
	    !ResolveExport( hXefg, "xefgSwapChainSetPresentId", pApi->setPresentId ) || !ResolveExport( hXefg, "xefgSwapChainGetLastPresentStatus", pApi->getLastPresentStatus ) ||
	    !ResolveExport( hXefg, "xefgSwapChainSetLoggingCallback", pApi->setLoggingCallback ) || !ResolveExport( hXefg, "xefgSwapChainDestroy", pApi->destroy ) ||
	    !ResolveExport( hXefg, "xefgSwapChainSetLatencyReduction", pApi->setLatencyReduction ) || !ResolveExport( hXefg, "xefgSwapChainSetNumInterpolatedFrames", pApi->setNumInterpolatedFrames ) ||
	    !ResolveExport( hXefg, "xefgSwapChainSetUiCompositionState", pApi->setUiCompositionState ) )
		return fail( "libxess_fg.dll lacks a required export" );
	if ( !ResolveExport( hXell, "xellD3D12CreateContext", pApi->xellCreateContext ) || !ResolveExport( hXell, "xellDestroyContext", pApi->xellDestroy ) || !ResolveExport( hXell, "xellSetSleepMode", pApi->xellSetSleepMode ) ||
	    !ResolveExport( hXell, "xellSleep", pApi->xellSleep ) || !ResolveExport( hXell, "xellAddMarkerData", pApi->xellAddMarkerData ) || !ResolveExport( hXell, "xellSetLoggingCallback", pApi->xellSetLoggingCallback ) ||
	    !ResolveExport( hXell, "xellGetVersion", pApi->xellGetVersion ) )
		return fail( "libxell.dll lacks a required export" );
	// Probe both contexts: XeLL is mandatory for XeFG.
	xell_context_handle_t hXellContext = nullptr;
	xell_result_t xellResult = pApi->xellCreateContext( m_pDevice, &hXellContext );
	if ( xellResult != XELL_RESULT_SUCCESS || !hXellContext )
	{
		char szReason[128];
		V_snprintf( szReason, sizeof( szReason ), "xellD3D12CreateContext failed (%d)", static_cast<int>( xellResult ) );
		return fail( szReason );
	}
	pApi->xellDestroy( hXellContext );
	xefg_swapchain_handle_t hProbe = nullptr;
	const xefg_swapchain_result_t probe = pApi->createContext( m_pDevice, &hProbe );
	if ( probe != XEFG_SWAPCHAIN_RESULT_SUCCESS || !hProbe )
	{
		char szReason[128];
		V_snprintf( szReason, sizeof( szReason ), "xefgSwapChainD3D12CreateContext failed (%d)", static_cast<int>( probe ) );
		return fail( szReason );
	}
	pApi->destroy( hProbe );
	xefg_swapchain_version_t version{};
	xell_version_t xellVersion{};
	pApi->getVersion( &version );
	pApi->xellGetVersion( &xellVersion );
	m_Xefg.version.Format( "XeFG %u.%u.%u, XeLL %u.%u.%u%s", version.major, version.minor, version.patch, xellVersion.major, xellVersion.minor, xellVersion.patch,
	    m_nVendor == 0x8086 ? "" : " (non-Intel adapter: one generated frame)" );
	m_pXefg = pApi;
	m_Xefg.available = true;
}

void CFrameGenDX12::ApplyXellSleepMode()
{
	if ( !m_hXell )
		return;
	xell_sleep_params_t params{};
	params.minimumIntervalUs = m_flFpsLimit > 0.f ? static_cast<uint32_t>( 1000000.f / m_flFpsLimit ) : 0u;
	params.bLowLatencyMode = 1;
	const xell_result_t result = m_pXefg->xellSetSleepMode( static_cast<xell_context_handle_t>( m_hXell ), &params );
	if ( result != XELL_RESULT_SUCCESS )
		Warning( "ShaderAPIDX12 framegen: xellSetSleepMode failed (%d)\n", static_cast<int>( result ) );
}

bool CFrameGenDX12::SelectXefg()
{
	xell_context_handle_t hXell = nullptr;
	const xell_result_t xellResult = m_pXefg->xellCreateContext( m_pDevice, &hXell );
	if ( xellResult != XELL_RESULT_SUCCESS || !hXell )
	{
		Fail( "xellD3D12CreateContext failed (%d)", static_cast<int>( xellResult ) );
		return false;
	}
	m_hXell = hXell;
	if ( m_bVerbose )
		m_pXefg->xellSetLoggingCallback( hXell, XELL_LOGGING_LEVEL_DEBUG, XellLog );
	ApplyXellSleepMode();
	m_Kind = FrameGenKindDX12::XeFG;
	m_nGenerated = 1; // clamped to the swap chain's maxSupportedInterpolations at adoption
	m_bXefgEnabled = false;
	m_bXefgWarned = false;
	return true;
}

bool CFrameGenDX12::CreateXefgContext()
{
	xefg_swapchain_handle_t hXefg = nullptr;
	const xefg_swapchain_result_t result = m_pXefg->createContext( m_pDevice, &hXefg );
	if ( result != XEFG_SWAPCHAIN_RESULT_SUCCESS || !hXefg )
	{
		Fail( "xefgSwapChainD3D12CreateContext failed (%d)", static_cast<int>( result ) );
		return false;
	}
	m_hXefg = hXefg;
	if ( m_bVerbose )
		m_pXefg->setLoggingCallback( hXefg, XEFG_SWAPCHAIN_LOGGING_LEVEL_DEBUG, XefgLog, this );
	const xefg_swapchain_result_t latency = m_pXefg->setLatencyReduction( hXefg, m_hXell );
	if ( latency != XEFG_SWAPCHAIN_RESULT_SUCCESS )
		Warning( "ShaderAPIDX12 framegen: xefgSwapChainSetLatencyReduction failed (%d)\n", static_cast<int>( latency ) );
	return true;
}

void CFrameGenDX12::DestroyXefgContext()
{
	if ( m_hXefg )
	{
		const xefg_swapchain_result_t result = m_pXefg->destroy( static_cast<xefg_swapchain_handle_t>( m_hXefg ) );
		if ( result != XEFG_SWAPCHAIN_RESULT_SUCCESS )
			Warning( "ShaderAPIDX12 framegen: xefgSwapChainDestroy failed (%d)\n", static_cast<int>( result ) );
		m_hXefg = nullptr;
	}
	m_bXefgEnabled = false;
}

void CFrameGenDX12::ReleaseXefg()
{
	DestroyXefgContext();
	if ( m_hXell )
	{
		m_pXefg->xellDestroy( static_cast<xell_context_handle_t>( m_hXell ) );
		m_hXell = nullptr;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Kind switch at a GPU-idle boundary with no swap chain alive
//-----------------------------------------------------------------------------
bool CFrameGenDX12::Select( FrameGenKindDX12 kind, uint32_t nMultiplier, bool bHudless, ID3D12CommandQueue *&pQueue )
{
	AUTO_LOCK( m_MarkerMutex );
	m_LastError.Clear();
	// Tear down the previous kind (adopted chains were already released by the device).
	switch ( m_Kind )
	{
	case FrameGenKindDX12::DLSSG:
		ReleaseDlssg( pQueue );
		break;
	case FrameGenKindDX12::FSR:
		DestroyFfxContexts();
		break;
	case FrameGenKindDX12::XeFG:
		ReleaseXefg();
		break;
	default:
		break;
	}
	m_pHudless.Reset();
	m_Kind = FrameGenKindDX12::None;
	m_nMultiplier = clamp( nMultiplier, 2u, 4u );
	m_bHudless = bHudless;
	m_nGenerated = 1;
	m_nPresented = 0;
	m_nRuntimeError = 0;
	bool bOk = true;
	switch ( kind )
	{
	case FrameGenKindDX12::DLSSG:
		bOk = m_Dlssg.available && SelectDlssg( pQueue );
		if ( !bOk && m_Kind == FrameGenKindDX12::DLSSG )
			ReleaseDlssg( pQueue );
		break;
	case FrameGenKindDX12::FSR:
		bOk = m_Fsr.available;
		if ( bOk )
		{
			m_Kind = FrameGenKindDX12::FSR; // contexts follow in AdoptSwapChain / EnsureContext
			Msg( "ShaderAPIDX12 framegen: FSR frame generation selected (1 generated frame per rendered frame)\n" );
		}
		break;
	case FrameGenKindDX12::XeFG:
		bOk = m_Xefg.available && SelectXefg();
		if ( !bOk )
			ReleaseXefg();
		break;
	default:
		break;
	}
	if ( !bOk )
	{
		m_Kind = FrameGenKindDX12::None;
		if ( m_LastError.IsEmpty() )
			Fail( "%s is not available: %s", FrameGenKindNameDX12( kind ), UnavailableReason( kind ) );
	}
	return bOk;
}

//-----------------------------------------------------------------------------
// Purpose: Takes over a fresh swap chain (no back-buffer references yet)
//-----------------------------------------------------------------------------
bool CFrameGenDX12::AdoptSwapChain( Microsoft::WRL::ComPtr<IDXGISwapChain3> &swap, ID3D12CommandQueue *pQueue, bool bInvertedDepth )
{
	if ( !swap )
		return false;
	DXGI_SWAP_CHAIN_DESC1 desc{};
	swap->GetDesc1( &desc );
	m_bInverted = bInvertedDepth;
	switch ( m_Kind )
	{
	case FrameGenKindDX12::DLSSG:
		m_nDlssgBackBuffers = desc.BufferCount;
		m_bDlssgOptionsValid = false;
		return true;
	case FrameGenKindDX12::FSR:
	{
		Microsoft::WRL::ComPtr<IDXGISwapChain4> chain4;
		if ( FAILED( swap.As( &chain4 ) ) )
		{
			Fail( "IDXGISwapChain4 unavailable for the FSR frame-interpolation wrap" );
			return false;
		}
		swap.Reset();
		// The wrap releases the application chain (it must be the only reference so the window frees it) and
		// creates the frame-interpolation chain for the same HWND/desc on its own present queue.
		IDXGISwapChain4 *pRaw = chain4.Detach();
		ffxOverrideVersion override{};
		override.header.type = FFX_API_DESC_TYPE_OVERRIDE_VERSION;
		override.versionId = m_pFfx->swapChainVersionId;
		ffxCreateContextDescFrameGenerationSwapChainVersionDX12 version{};
		version.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_VERSION_DX12;
		version.version = FFX_FRAMEGENERATION_SWAPCHAIN_DX12_VERSION;
		version.header.pNext = &override.header;
		ffxCreateContextDescFrameGenerationSwapChainWrapDX12 wrap{};
		wrap.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_WRAP_DX12;
		wrap.header.pNext = &version.header;
		wrap.swapchain = &pRaw;
		wrap.gameQueue = pQueue;
		ffxContext context = nullptr;
		const ffxReturnCode_t created = m_pFfx->createContext( &context, &wrap.header, nullptr );
		if ( created != FFX_API_RETURN_OK || !context || !pRaw )
		{
			// The application chain was released inside the wrap; nothing is left to free.
			Fail( "ffxCreateContext(frame-interpolation swap chain) failed (%u)", created );
			return false;
		}
		m_pFfxSwapChainContext = context;
		swap.Attach( pRaw );
		// The wrapper derives its transfer function from this call (scRGB -> FP16 linear).
		swap->SetColorSpace1( DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709 );
		return true;
	}
	case FrameGenKindDX12::XeFG:
	{
		if ( !CreateXefgContext() )
			return false;
		xefg_swapchain_d3d12_init_params_t params{};
		params.pApplicationSwapChain = swap.Detach(); // refcount 1; XeFG releases it
		params.initFlags = bInvertedDepth ? XEFG_SWAPCHAIN_INIT_FLAG_INVERTED_DEPTH : XEFG_SWAPCHAIN_INIT_FLAG_NONE;
		params.maxInterpolatedFrames = XEFG_SWAPCHAIN_USE_MAX_SUPPORTED_INTERPOLATED_FRAMES;
		params.creationNodeMask = 1;
		params.visibleNodeMask = 1;
		params.uiMode = XEFG_SWAPCHAIN_UI_MODE_AUTO;
		xefg_swapchain_handle_t hXefg = static_cast<xefg_swapchain_handle_t>( m_hXefg );
		xefg_swapchain_result_t result = m_pXefg->initFromSwapChain( hXefg, pQueue, &params );
		if ( result < XEFG_SWAPCHAIN_RESULT_SUCCESS )
		{
			Fail( "xefgSwapChainD3D12InitFromSwapChain failed (%d)", static_cast<int>( result ) );
			DestroyXefgContext();
			return false;
		}
		result = m_pXefg->getSwapChainPtr( hXefg, IID_PPV_ARGS( swap.ReleaseAndGetAddressOf() ) );
		if ( result < XEFG_SWAPCHAIN_RESULT_SUCCESS || !swap )
		{
			Fail( "xefgSwapChainD3D12GetSwapChainPtr failed (%d)", static_cast<int>( result ) );
			DestroyXefgContext();
			return false;
		}
		xefg_swapchain_properties_t properties{};
		uint32_t nMax = 1;
		if ( m_pXefg->getProperties( hXefg, nullptr, 0, 0, DXGI_FORMAT_UNKNOWN, &properties ) >= XEFG_SWAPCHAIN_RESULT_SUCCESS && properties.maxSupportedInterpolations )
			nMax = properties.maxSupportedInterpolations;
		m_nGenerated = MIN( m_nMultiplier - 1, nMax );
		result = m_pXefg->setNumInterpolatedFrames( hXefg, m_nGenerated );
		if ( result < XEFG_SWAPCHAIN_RESULT_SUCCESS )
			Warning( "ShaderAPIDX12 framegen: xefgSwapChainSetNumInterpolatedFrames(%u) failed (%d)\n", m_nGenerated, static_cast<int>( result ) );
		m_pXefg->setUiCompositionState( hXefg, m_bHudless ? XEFG_SWAPCHAIN_UI_COMPOSITION_STATE_ENABLED : XEFG_SWAPCHAIN_UI_COMPOSITION_STATE_DISABLED );
		m_bXefgEnabled = false; // the proxy starts disabled; BeforePresent enables it per frame
		m_nXefgBackBufferWidth = desc.Width;
		m_nXefgBackBufferHeight = desc.Height;
		Msg( "ShaderAPIDX12 framegen: XeSS-FG selected, %u generated frame(s) per rendered frame (max %u)\n", m_nGenerated, nMax );
		return true;
	}
	default:
		return true;
	}
}

void CFrameGenDX12::ReleaseSwapChain( Microsoft::WRL::ComPtr<IDXGISwapChain3> &swap )
{
	switch ( m_Kind )
	{
	case FrameGenKindDX12::FSR:
		if ( m_pFfx && m_pFfxFgContext && m_bFfxEnabled && swap )
		{
			ffxConfigureDescFrameGeneration config{};
			config.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
			config.swapChain = swap.Get();
			config.frameGenerationEnabled = false;
			config.generationRect = { 0, 0, static_cast<int32_t>( m_nFfxWidth ), static_cast<int32_t>( m_nFfxHeight ) };
			m_pFfx->configure( &m_pFfxFgContext, &config.header );
			m_bFfxEnabled = false;
		}
		DestroyFfxContexts();
		swap.Reset();
		if ( m_pFfxSwapChainContext )
		{
			m_pFfx->destroyContext( &m_pFfxSwapChainContext, nullptr );
			m_pFfxSwapChainContext = nullptr;
		}
		break;
	case FrameGenKindDX12::XeFG:
		swap.Reset();
		DestroyXefgContext();
		break;
	default:
		swap.Reset();
		break;
	}
	m_pHudless.Reset();
	m_pPresentSwap = nullptr;
	m_bDlssgPresentCountValid = false;
}

void CFrameGenDX12::BeforeResize( IDXGISwapChain3 *pSwap )
{
	switch ( m_Kind )
	{
	case FrameGenKindDX12::DLSSG:
		// Streamline hooks ResizeBuffers itself; the next PrepareFrame re-sends the options for the new size (a
		// second slDLSSGSetOptions in the same frame is flagged as redundant by the plugin).
		m_bDlssgOptionsValid = false;
		break;
	case FrameGenKindDX12::FSR:
		// Interpolation off while the context still exists (the wrapper keeps calling the configured callback
		// otherwise); the context is recreated at the new display size by AfterResize.
		if ( m_pFfxFgContext && m_bFfxEnabled && pSwap )
		{
			ffxConfigureDescFrameGeneration config{};
			config.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
			config.swapChain = pSwap;
			config.frameGenerationEnabled = false;
			config.generationRect = { 0, 0, static_cast<int32_t>( m_nFfxWidth ), static_cast<int32_t>( m_nFfxHeight ) };
			m_pFfx->configure( &m_pFfxFgContext, &config.header );
			m_bFfxEnabled = false;
		}
		DestroyFfxContexts();
		break;
	default:
		break;
	}
	m_pHudless.Reset();
}

bool CFrameGenDX12::AfterResize( uint32_t nWidth, uint32_t nHeight )
{
	m_bDlssgPresentCountValid = false;
	switch ( m_Kind )
	{
	case FrameGenKindDX12::FSR:
		return CreateFfxFgContext( nWidth, nHeight, m_bInverted );
	case FrameGenKindDX12::XeFG:
		m_nXefgBackBufferWidth = nWidth;
		m_nXefgBackBufferHeight = nHeight;
		return true;
	default:
		return true;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Present-format HUD-less scene copy, created only when missing (replaced at idle boundaries)
//-----------------------------------------------------------------------------
bool CFrameGenDX12::EnsureHudless( uint32_t nWidth, uint32_t nHeight )
{
	const DXGI_FORMAT format = PresentFormat();
	if ( m_pHudless && m_nHudlessWidth == nWidth && m_nHudlessHeight == nHeight && m_HudlessFormat == format )
		return true;
	m_pHudless.Reset();
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
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	const HRESULT hr = m_pDevice->CreateCommittedResource( &heap, D3D12_HEAP_FLAG_NONE, &desc, kNpsr, nullptr, IID_PPV_ARGS( &m_pHudless ) );
	if ( FAILED( hr ) )
	{
		Fail( "hudless texture creation failed (0x%08x)", static_cast<unsigned>( hr ) );
		return false;
	}
	m_pHudless->SetName( L"ShaderAPIDX12 framegen hudless" );
	m_nHudlessWidth = nWidth;
	m_nHudlessHeight = nHeight;
	m_HudlessFormat = format;
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Records the per-frame provider work as one ExternalCommand
//-----------------------------------------------------------------------------
bool CFrameGenDX12::RecordDispatch( CCommandRecorderDX12 &recorder, const FrameGenDispatchDX12 &dispatch )
{
	switch ( m_Kind )
	{
	case FrameGenKindDX12::DLSSG:
		if ( !SubmitDlssgFrame( dispatch ) )
			return false;
		break;
	case FrameGenKindDX12::FSR:
		if ( !m_pFfxFgContext || m_nFfxWidth != dispatch.width || m_nFfxHeight != dispatch.height || m_bFfxInverted != dispatch.camera.inverted )
			return false;
		break;
	case FrameGenKindDX12::XeFG:
		if ( !m_hXefg )
			return false;
		break;
	default:
		return false;
	}
	const FrameGenReplayPayloadDX12 payload{ this, dispatch };
	recorder.ExternalCommand( &CFrameGenDX12::ReplayThunk, payload );
	return true;
}

void CFrameGenDX12::ReplayThunk( ID3D12GraphicsCommandList *pList, ID3D12Device *, const void *pPayload ) noexcept
{
	FrameGenReplayPayloadDX12 payload;
	memcpy( &payload, pPayload, sizeof( payload ) );
	const FrameGenDispatchDX12 &d = payload.dispatch;
	const uint32_t nCode = payload.owner->Execute( pList, d );
	// `serial` is published last (each store is an interlocked exchange).
	d.result->code = nCode;
	d.result->frame = d.frame;
	d.result->serial = d.serial;
}

uint32_t CFrameGenDX12::Execute( ID3D12GraphicsCommandList *pList, const FrameGenDispatchDX12 &d ) const noexcept
{
	switch ( m_Kind )
	{
	case FrameGenKindDX12::FSR:
	{
		if ( !m_pFfxFgContext )
			return kReplayNoContext;
		Transition( pList, d.depth, d.depthBefore, kRead );
		Transition( pList, d.motion, d.motionBefore, kRead );
		ffxDispatchDescFrameGenerationPrepareV2 prepare{};
		prepare.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_V2;
		prepare.frameID = d.presentSerial;
		prepare.flags = 0;
		prepare.commandList = pList;
		prepare.renderSize = { d.width, d.height };
		prepare.jitterOffset = { d.jitter[0], d.jitter[1] };
		prepare.motionVectorScale = { d.motionScale[0], d.motionScale[1] };
		prepare.frameTimeDelta = d.frameTimeMs;
		prepare.reset = d.reset;
		prepare.cameraNear = d.camera.nearPlane;
		prepare.cameraFar = d.camera.farPlane;
		prepare.cameraFovAngleVertical = d.camera.fovY;
		prepare.viewSpaceToMetersFactor = kViewSpaceToMeters;
		prepare.depth = ffxApiGetResourceDX12( d.depth, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ );
		prepare.motionVectors = ffxApiGetResourceDX12( d.motion, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ );
		memcpy( prepare.cameraPosition, d.camera.pos, sizeof( prepare.cameraPosition ) );
		memcpy( prepare.cameraUp, d.camera.up, sizeof( prepare.cameraUp ) );
		memcpy( prepare.cameraRight, d.camera.right, sizeof( prepare.cameraRight ) );
		memcpy( prepare.cameraForward, d.camera.fwd, sizeof( prepare.cameraForward ) );
		ffxContext context = m_pFfxFgContext;
		const ffxReturnCode_t rc = m_pFfx->dispatch( &context, &prepare.header );
		Transition( pList, d.depth, kRead, D3D12_RESOURCE_STATE_DEPTH_WRITE );
		Transition( pList, d.motion, kRead, D3D12_RESOURCE_STATE_RENDER_TARGET );
		return rc == FFX_API_RETURN_OK ? 0u : ( 0x20000u | ( rc & 0xffffu ) );
	}
	case FrameGenKindDX12::XeFG:
	{
		if ( !m_hXefg )
			return kReplayNoContext;
		xefg_swapchain_handle_t hXefg = static_cast<xefg_swapchain_handle_t>( m_hXefg );
		const uint32_t nId = ClampFrameId( d.frameId );
		Transition( pList, d.depth, d.depthBefore, D3D12_RESOURCE_STATE_COPY_SOURCE );
		Transition( pList, d.motion, d.motionBefore, D3D12_RESOURCE_STATE_COPY_SOURCE );
		xefg_swapchain_d3d12_resource_data_t resource{};
		resource.validity = XEFG_SWAPCHAIN_RV_ONLY_NOW;
		resource.resourceBase = { 0, 0 };
		resource.resourceSize = { d.width, d.height };
		resource.incomingState = D3D12_RESOURCE_STATE_COPY_SOURCE;
		resource.type = XEFG_SWAPCHAIN_RES_DEPTH;
		resource.pResource = d.depth;
		xefg_swapchain_result_t worst = m_pXefg->tagFrameResource( hXefg, pList, nId, &resource );
		resource.type = XEFG_SWAPCHAIN_RES_MOTION_VECTOR;
		resource.pResource = d.motion;
		xefg_swapchain_result_t result = m_pXefg->tagFrameResource( hXefg, pList, nId, &resource );
		worst = MIN( worst, result );
		if ( d.hudlessValid && d.hudless )
		{
			resource.type = XEFG_SWAPCHAIN_RES_HUDLESS_COLOR;
			resource.validity = XEFG_SWAPCHAIN_RV_UNTIL_NEXT_PRESENT;
			resource.incomingState = kNpsr;
			resource.pResource = d.hudless;
			result = m_pXefg->tagFrameResource( hXefg, pList, nId, &resource );
			worst = MIN( worst, result );
		}
		xefg_swapchain_frame_constant_data_t constants{};
		memcpy( constants.viewMatrix, d.camera.view, sizeof( constants.viewMatrix ) );
		memcpy( constants.projectionMatrix, d.camera.proj, sizeof( constants.projectionMatrix ) );
		constants.jitterOffsetX = d.jitter[0];
		constants.jitterOffsetY = d.jitter[1];
		constants.motionVectorScaleX = d.motionScale[0];
		constants.motionVectorScaleY = d.motionScale[1];
		constants.resetHistory = d.reset ? 1u : 0u;
		constants.frameRenderTime = d.frameTimeMs;
		result = m_pXefg->tagFrameConstants( hXefg, nId, &constants );
		worst = MIN( worst, result );
		Transition( pList, d.depth, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE );
		Transition( pList, d.motion, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET );
		return worst >= XEFG_SWAPCHAIN_RESULT_SUCCESS ? 0u : ( 0x30000u | ( static_cast<uint32_t>( -static_cast<int>( worst ) ) & 0xffffu ) );
	}
	default:
		return 0; // DLSS-G: tags and constants went through Streamline at record time
	}
}

//-----------------------------------------------------------------------------
// Purpose: Per-present protocol on the recording owner (worker drained, Present op not yet enqueued)
//-----------------------------------------------------------------------------
void CFrameGenDX12::PrepareFrame( bool bActive, uint32_t nFrameId, uint64_t nPresentSerial, uint32_t nWidth, uint32_t nHeight, IDXGISwapChain3 *pSwap )
{
	m_pPresentSwap = pSwap; // the chain the Present op that follows will use (worker drained; released only at idle)
	switch ( m_Kind )
	{
	case FrameGenKindDX12::DLSSG:
	{
		if ( !m_bSlInitialized || !m_pSl->dlssgSetOptions )
			return;
		if ( m_nDlssgMax && m_nGenerated > m_nDlssgMax )
		{
			m_nGenerated = m_nDlssgMax;
			ApplyReflex(); // the displayed-rate limit follows the frames actually shown
		}
		if ( !m_bDlssgOptionsValid || m_bDlssgOn != bActive || m_nDlssgWidth != nWidth || m_nDlssgHeight != nHeight || m_nDlssgGenerated != m_nGenerated )
			DlssgSetOptions( bActive, nWidth, nHeight );
		break;
	}
	case FrameGenKindDX12::FSR:
	{
		if ( !m_pFfx || !pSwap )
		{
			m_nPresented += 1;
			return;
		}
		// The frame-interpolation chain presents on its own pacing thread, so DXGI's present count lags
		// unpredictably; the count reported is what this configure asks the chain to show.
		const bool bInterpolate = bActive && m_pFfxFgContext;
		m_nPresented += bInterpolate ? 1u + m_nGenerated : 1u;
		if ( !m_pFfxFgContext )
			return;
		ffxConfigureDescFrameGeneration config{};
		config.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
		config.swapChain = pSwap;
		config.presentCallback = nullptr;
		config.presentCallbackUserContext = nullptr;
		config.frameGenerationCallback = []( ffxDispatchDescFrameGeneration *pParams, void *pUserContext ) -> ffxReturnCode_t
		{
			const CFrameGenDX12 *pSelf = static_cast<const CFrameGenDX12 *>( pUserContext );
			ffxContext context = pSelf->m_pFfxFgContext;
			return context ? pSelf->m_pFfx->dispatch( &context, &pParams->header ) : FFX_API_RETURN_ERROR;
		};
		config.frameGenerationCallbackUserContext = this;
		config.frameGenerationEnabled = bActive;
		config.allowAsyncWorkloads = false;
		config.HUDLessColor = bActive && m_bHudless && m_pHudless ? ffxApiGetResourceDX12( m_pHudless.Get(), FFX_API_RESOURCE_STATE_COMPUTE_READ ) : FfxApiResource{};
		config.flags = 0;
		config.onlyPresentGenerated = false;
		config.generationRect = { 0, 0, static_cast<int32_t>( nWidth ), static_cast<int32_t>( nHeight ) };
		config.frameID = nPresentSerial;
		const ffxReturnCode_t rc = m_pFfx->configure( &m_pFfxFgContext, &config.header );
		if ( rc != FFX_API_RETURN_OK )
		{
			m_nRuntimeError = 1;
			Warning( "ShaderAPIDX12 framegen: ffxConfigure(framegeneration) failed (%u)\n", rc );
		}
		m_bFfxEnabled = bActive;
		break;
	}
	default:
		break;
	}
}

void CFrameGenDX12::BeforePresent( uint32_t nFrameId, bool bActive )
{
	if ( ReflexMarkers() )
	{
		AUTO_LOCK( m_MarkerMutex );
		SlMarker( 5, nFrameId );
	}
	switch ( m_Kind )
	{
	case FrameGenKindDX12::XeFG:
	{
		if ( !m_hXefg )
			return;
		xefg_swapchain_handle_t hXefg = static_cast<xefg_swapchain_handle_t>( m_hXefg );
		if ( m_bXefgEnabled != bActive )
		{
			const xefg_swapchain_result_t result = m_pXefg->setEnabled( hXefg, bActive ? 1u : 0u );
			if ( result < XEFG_SWAPCHAIN_RESULT_SUCCESS )
				Warning( "ShaderAPIDX12 framegen: xefgSwapChainSetEnabled(%d) failed (%d)\n", bActive ? 1 : 0, static_cast<int>( result ) );
			m_bXefgEnabled = bActive;
		}
		const uint32_t nId = ClampFrameId( nFrameId );
		m_pXefg->setPresentId( hXefg, nId );
		AUTO_LOCK( m_MarkerMutex );
		if ( m_hXell )
			m_pXefg->xellAddMarkerData( static_cast<xell_context_handle_t>( m_hXell ), nId, XELL_PRESENT_START );
		break;
	}
	default:
		break;
	}
}

void CFrameGenDX12::AfterPresent( uint32_t nFrameId )
{
	if ( ReflexMarkers() )
	{
		AUTO_LOCK( m_MarkerMutex );
		SlMarker( 6, nFrameId );
		if ( m_pSl->reflexGetState && !m_pSl->reflexReported )
		{
			sl::ReflexState reflex{};
			if ( m_pSl->reflexGetState( reflex ) == sl::Result::eOk && reflex.latencyReportAvailable )
			{
				m_pSl->reflexReported = true;
				Msg( "ShaderAPIDX12 framegen: Reflex latency report available; low latency %s\n", reflex.lowLatencyAvailable ? "available" : "unavailable" );
			}
		}
	}
	switch ( m_Kind )
	{
	case FrameGenKindDX12::DLSSG:
	{
		// slDLSSGGetState belongs on the presenting thread; the recording owner only touches Streamline with the
		// worker drained, so the two never overlap.
		if ( !m_bSlInitialized || !m_pSl->dlssgGetState )
			return;
		sl::DLSSGState state{};
		if ( m_pSl->dlssgGetState( sl::ViewportHandle( 0u ), state, nullptr ) != sl::Result::eOk )
			return;
		m_nPresented += state.numFramesActuallyPresented ? state.numFramesActuallyPresented : 1u;
		if ( m_bVerbose && m_pPresentSwap )
		{
			// Cross-check against DXGI's own count on the chain (the proxy forwards it to the native chain).
			UINT nCount = 0;
			if ( SUCCEEDED( m_pPresentSwap->GetLastPresentCount( &nCount ) ) )
			{
				if ( m_bDlssgPresentCountValid && ( ++m_nDlssgLogCounter % 30 ) == 0 )
					Msg( "ShaderAPIDX12 framegen: DLSS-G present %u: numFramesActuallyPresented %u, DXGI presents since last %u\n", nFrameId, state.numFramesActuallyPresented, nCount - m_nDlssgLastPresentCount );
				m_nDlssgLastPresentCount = nCount;
				m_bDlssgPresentCountValid = true;
			}
		}
		if ( state.status != sl::DLSSGStatus::eOk )
		{
			m_nRuntimeError = 1;
			if ( !m_bDlssgStatusLogged )
			{
				m_bDlssgStatusLogged = true;
				const uint32_t s = static_cast<uint32_t>( state.status );
				Warning( "ShaderAPIDX12 framegen: DLSS-G status 0x%x:%s%s%s%s%s\n", s, s & 1 ? " resolution too low" : "", s & 2 ? " Reflex not detected at runtime" : "", s & 4 ? " HDR format not supported" : "",
				    s & 8 ? " common constants invalid" : "", s & 16 ? " GetCurrentBackBufferIndex not called" : "" );
			}
		}
		else if ( m_bDlssgStatusLogged )
		{
			m_bDlssgStatusLogged = false;
			Msg( "ShaderAPIDX12 framegen: DLSS-G status ok again\n" );
		}
		if ( state.minWidthOrHeight && ( m_nDlssgWidth < state.minWidthOrHeight || m_nDlssgHeight < state.minWidthOrHeight ) )
			m_nRuntimeError = 1;
		if ( state.numFramesToGenerateMax )
			m_nDlssgMax = state.numFramesToGenerateMax;
		break;
	}
	case FrameGenKindDX12::XeFG:
	{
		if ( !m_hXefg )
			return;
		const uint32_t nId = ClampFrameId( nFrameId );
		{
			AUTO_LOCK( m_MarkerMutex );
			if ( m_hXell )
				m_pXefg->xellAddMarkerData( static_cast<xell_context_handle_t>( m_hXell ), nId, XELL_PRESENT_END );
		}
		xefg_swapchain_present_status_t status{};
		if ( m_pXefg->getLastPresentStatus( static_cast<xefg_swapchain_handle_t>( m_hXefg ), &status ) >= XEFG_SWAPCHAIN_RESULT_SUCCESS )
		{
			m_nPresented += status.framesPresented ? status.framesPresented : 1u;
			if ( status.frameGenResult < XEFG_SWAPCHAIN_RESULT_SUCCESS )
			{
				m_nRuntimeError = 1;
				if ( !m_bXefgWarned )
				{
					m_bXefgWarned = true;
					Warning( "ShaderAPIDX12 framegen: XeSS-FG present reported %d\n", static_cast<int>( status.frameGenResult ) );
				}
			}
			else if ( status.frameGenResult > XEFG_SWAPCHAIN_RESULT_SUCCESS && m_bVerbose )
				Msg( "ShaderAPIDX12 framegen: XeSS-FG present warning %d\n", static_cast<int>( status.frameGenResult ) );
		}
		break;
	}
	default:
		break;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Latency markers from any thread (the client's simulation thread, the presenting thread)
//-----------------------------------------------------------------------------
void CFrameGenDX12::Marker( uint32_t nMarker, uint32_t nFrameId )
{
	if ( nMarker < 1 || nMarker > 4 )
		return;
	AUTO_LOCK( m_MarkerMutex );
	if ( ReflexMarkers() )
	{
		SlMarker( nMarker, nFrameId );
		return;
	}
	switch ( m_Kind )
	{
	case FrameGenKindDX12::XeFG:
	{
		if ( !m_hXell )
			return;
		xell_context_handle_t hXell = static_cast<xell_context_handle_t>( m_hXell );
		const uint32_t nId = ClampFrameId( nFrameId );
		static const xell_latency_marker_type_t s_Markers[] = { XELL_SIMULATION_START, XELL_SIMULATION_END, XELL_RENDERSUBMIT_START, XELL_RENDERSUBMIT_END };
		if ( nMarker == 1 )
			m_pXefg->xellSleep( hXell, nId );
		m_pXefg->xellAddMarkerData( hXell, nId, s_Markers[nMarker - 1] );
		break;
	}
	default:
		break;
	}
}

void CFrameGenDX12::SetFpsLimit( float flFps )
{
	m_flFpsLimit = flFps > 0.f && flFps < 100000.f ? flFps : 0.f;
	AUTO_LOCK( m_MarkerMutex );
	ApplyReflex();
	ApplyXellSleepMode();
}

//-----------------------------------------------------------------------------
// Purpose: Releases tagged resources before the shader API destroys its input textures
//-----------------------------------------------------------------------------
void CFrameGenDX12::ReleaseFeatures()
{
	AUTO_LOCK( m_MarkerMutex );
	DestroyFfxContexts();
	if ( m_pFfxSwapChainContext )
	{
		m_pFfx->destroyContext( &m_pFfxSwapChainContext, nullptr );
		m_pFfxSwapChainContext = nullptr;
	}
	ReleaseXefg();
	if ( m_bSlInitialized )
	{
		if ( m_bDlssgOptionsValid && m_bDlssgOn )
			DlssgSetOptions( false, m_nDlssgWidth, m_nDlssgHeight );
		m_Kind = FrameGenKindDX12::None;
		ApplyReflex();
		if ( m_bDlssgLoaded )
		{
			m_pSl->freeResources( sl::kFeatureDLSS_G, sl::ViewportHandle( 0u ) );
			m_pSl->setFeatureLoaded( sl::kFeatureDLSS_G, false );
			m_bDlssgLoaded = false;
		}
	}
	m_Kind = FrameGenKindDX12::None;
	m_pHudless.Reset();
	m_bDlssgOptionsValid = m_bDlssgOn = false;
}

//-----------------------------------------------------------------------------
// Purpose: Destroys every provider object (adopted chains and owning queue released, GPU idle)
//-----------------------------------------------------------------------------
void CFrameGenDX12::Shutdown()
{
	ReleaseFeatures();
	{
		AUTO_LOCK( m_MarkerMutex );
		if ( m_bSlInitialized )
		{
			m_pSlFactory.Reset();
			m_pSlDevice.Reset();
			m_pSl->shutdown();
			m_bSlInitialized = false;
		}
	}
	m_bQueueIsProxy = false;
	if ( m_pXefg )
	{
		FreeLibrary( m_pXefg->xefg );
		FreeLibrary( m_pXefg->xell );
		delete m_pXefg;
		m_pXefg = nullptr;
	}
	if ( m_pFfx )
	{
		FreeLibrary( m_pFfx->loader );
		FreeLibrary( m_pFfx->effect );
		delete m_pFfx;
		m_pFfx = nullptr;
	}
	// sl.interposer.dll stays resident (see SlApi); the table is simply dropped.
	delete m_pSl;
	m_pSl = nullptr;
	m_Dlssg = Provider{};
	m_Fsr = Provider{};
	m_Xefg = Provider{};
	m_pDevice = nullptr;
	m_pFactory = nullptr;
	m_nGenerated = 1;
	m_nPresented = 0;
	m_nRuntimeError = 0;
	m_nReflexMode = 0;
	m_LastError.Clear();
}

} // namespace shaderapidx12
