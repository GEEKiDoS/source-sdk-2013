//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Native DX12 device; display-gamma CPU curve follows shaderdevicedx8.cpp.
//
//=============================================================================//

#include "shaderdevice_dx12.h"
#include "shaderapi_dx12.h"
#include "hardwareconfig_dx12.h"
#include "resources_dx12.h"
#include "shader_translate_dx12.h"
#include "tracy_dx12.h"
#include "filesystem.h"
#include "icvar.h"
#include "shaderapi/ishaderutil.h"
#include "tier0/dbg.h"
#include "tier0/icommandline.h"
#include "tier0/platform.h"
#include "tier1/convar.h"
#include "tier1/strtools.h"
#include "tier1/tier1.h"
#include "tier1/utlstring.h"
#include "tier2/tier2.h"
#include <windows.h>
#include <d3dcompiler.h>
#include <d3d12sdklayers.h>
#include <math.h>

namespace shaderapidx12
{
//-----------------------------------------------------------------------------
// Purpose: Loads dxbcSigner.dll from the renderer's directory and resolves SignDxbc
//-----------------------------------------------------------------------------
static bool LoadDxbcSigner( HMODULE &hModule, SignDxbcFnDX12 &pfnSigner )
{
	char szPath[MAX_PATH]{};
	HMODULE hSelf = nullptr;
	GetModuleHandleExA( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>( &LoadDxbcSigner ), &hSelf );
	if ( !hSelf || !GetModuleFileNameA( hSelf, szPath, sizeof( szPath ) ) )
		return false;
	char *pszSlash = strrchr( szPath, '\\' );
	if ( !pszSlash )
		pszSlash = strrchr( szPath, '/' );
	if ( pszSlash )
		pszSlash[1] = 0;
	CUtlString full( szPath );
	full += "dxbcSigner.dll";
	hModule = LoadLibraryExA( full.Get(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH );
	if ( !hModule )
		return false;
	pfnSigner = reinterpret_cast<SignDxbcFnDX12>( GetProcAddress( hModule, "SignDxbc" ) );
	if ( !pfnSigner )
	{
		FreeLibrary( hModule );
		hModule = nullptr;
		return false;
	}
	return true;
}

CShaderDeviceMgrDX12 *g_pShaderDeviceMgrDX12 = nullptr;
CShaderDeviceDX12 *g_pShaderDeviceDX12 = nullptr;
CInterlockedInt g_bTracyZonesActiveDX12;

//-----------------------------------------------------------------------------
// Purpose: Recomputes g_bTracyZonesActiveDX12 (once per presented frame)
//-----------------------------------------------------------------------------
void RefreshTracyZonesDX12()
{
#ifdef TRACY_ENABLE
	// -dx12nozones keeps Tracy sampling/frames while disabling renderer zones, so sampled
	// captures measure the disconnected hot path rather than instrumentation cost.
	static const bool s_bZonesDisabled = CommandLine() && CommandLine()->CheckParm( "-dx12nozones" );
	g_bTracyZonesActiveDX12 = ( !s_bZonesDisabled && TracyIsStarted && tracy::GetProfiler().IsConnected() ) ? 1 : 0;
#endif
}

CShaderDeviceDX12::CShaderDeviceDX12() = default;

CShaderDeviceDX12::~CShaderDeviceDX12()
{
	ShutdownDevice();
	for ( int i = 0; i < ARRAYSIZE( m_pDynamicVertices ); ++i )
		delete m_pDynamicVertices[i];
	for ( int i = 0; i < ARRAYSIZE( m_pDynamicIndices ); ++i )
		delete m_pDynamicIndices[i];
}

//-----------------------------------------------------------------------------
// Purpose: Marks the device failed, logs DRED data and stops with a fatal error
//-----------------------------------------------------------------------------
void CShaderDeviceDX12::FailDevice( const char *pszOperation, HRESULT hr )
{
	if ( m_bFailed )
		return;
	m_bFailed = true;
	const HRESULT reason = m_pDevice ? m_pDevice->GetDeviceRemovedReason() : S_OK;
	Warning( "ShaderAPIDX12: %s failed (0x%08x), device removal reason 0x%08x\n", pszOperation, static_cast<unsigned>( hr ), static_cast<unsigned>( reason ) );
	Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedData> dred;
	if ( m_pDevice && SUCCEEDED( m_pDevice.As( &dred ) ) )
	{
		D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT breadcrumbs{};
		D3D12_DRED_PAGE_FAULT_OUTPUT fault{};
		if ( SUCCEEDED( dred->GetAutoBreadcrumbsOutput( &breadcrumbs ) ) && breadcrumbs.pHeadAutoBreadcrumbNode )
			Warning( "ShaderAPIDX12: DRED breadcrumbs available at %p\n", breadcrumbs.pHeadAutoBreadcrumbNode );
		if ( SUCCEEDED( dred->GetPageFaultAllocationOutput( &fault ) ) && fault.PageFaultVA )
			Warning( "ShaderAPIDX12: DRED page fault GPU VA 0x%llx\n", static_cast<unsigned long long>( fault.PageFaultVA ) );
	}
	Error( "ShaderAPIDX12: native D3D12 rendering stopped at %s (0x%08x), removal 0x%08x\n", pszOperation, static_cast<unsigned>( hr ), static_cast<unsigned>( reason ) );
}

//-----------------------------------------------------------------------------
// Purpose: Returns true when hr succeeded, otherwise fails the device
//-----------------------------------------------------------------------------
bool CShaderDeviceDX12::CheckDevice( const char *pszOperation, HRESULT hr )
{
	if ( SUCCEEDED( hr ) )
		return true;
	FailDevice( pszOperation, hr );
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Creates the device, queue, frame objects and the primary view on hWnd
//-----------------------------------------------------------------------------
bool CShaderDeviceDX12::Initialize( void *hWnd, int nAdapter, const ShaderDeviceInfo_t &info, IDXGIAdapter1 *pSelectedAdapter )
{
	ShutdownDevice();
	if ( !hWnd || !IsWindow( static_cast<HWND>( hWnd ) ) || !pSelectedAdapter )
	{
		Warning( "ShaderAPIDX12: valid HWND and adapter required\n" );
		return false;
	}
	const bool bDebug = CommandLine() && CommandLine()->CheckParm( "-dx12debug" );
	const bool bGpuValidation = CommandLine() && CommandLine()->CheckParm( "-dx12gpuvalidation" );
	if ( bDebug || bGpuValidation )
	{
		Microsoft::WRL::ComPtr<ID3D12Debug> controller;
		const HRESULT hr = D3D12GetDebugInterface( IID_PPV_ARGS( &controller ) );
		if ( FAILED( hr ) )
		{
			Warning( "ShaderAPIDX12: requested DX12 debug layer unavailable (0x%08x); install Graphics Tools\n", static_cast<unsigned>( hr ) );
			return false;
		}
		controller->EnableDebugLayer();
		if ( bGpuValidation )
		{
			Microsoft::WRL::ComPtr<ID3D12Debug1> validation;
			if ( FAILED( controller.As( &validation ) ) )
			{
				Warning( "ShaderAPIDX12: GPU validation interface unavailable\n" );
				return false;
			}
			validation->SetEnableGPUBasedValidation( TRUE );
		}
	}
	HRESULT hr = CreateDXGIFactory2( ( bDebug || bGpuValidation ) ? DXGI_CREATE_FACTORY_DEBUG : 0, IID_PPV_ARGS( &m_pFactory ) );
	if ( FAILED( hr ) )
	{
		Warning( "ShaderAPIDX12: CreateDXGIFactory2 failed (0x%08x)\n", static_cast<unsigned>( hr ) );
		ShutdownDevice();
		return false;
	}
	hr = D3D12CreateDevice( pSelectedAdapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS( &m_pDevice ) );
	if ( FAILED( hr ) )
	{
		Warning( "ShaderAPIDX12: D3D12CreateDevice failed (0x%08x)\n", static_cast<unsigned>( hr ) );
		ShutdownDevice();
		return false;
	}
	D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
	const int bindingTier = SUCCEEDED( m_pDevice->CheckFeatureSupport( D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof( options ) ) ) ? int( options.ResourceBindingTier ) : 0;
	if ( g_pHardwareConfigDX12 ) g_pHardwareConfigDX12->SetResourceBindingTier( bindingTier );
	if ( ( bDebug || bGpuValidation ) && SUCCEEDED( m_pDevice.As( &m_pInfoQueue ) ) )
		m_pInfoQueue->SetMuteDebugOutput( FALSE );
	if ( !LoadDxbcSigner( m_hSignerModule, m_pfnSigner ) )
	{
		Warning( "ShaderAPIDX12: required dxbcSigner.dll/SignDxbc missing beside renderer\n" );
		ShutdownDevice();
		return false;
	}
	char szRendererPath[MAX_PATH]{};
	HMODULE hRendererModule = nullptr;
	GetModuleHandleExA( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>( &LoadDxbcSigner ), &hRendererModule );
	if ( !hRendererModule || !GetModuleFileNameA( hRendererModule, szRendererPath, sizeof( szRendererPath ) ) )
	{
		Warning( "ShaderAPIDX12: unable to locate renderer module while checking stdshader_dx12.dll\n" );
		ShutdownDevice();
		return false;
	}
	char *pszRendererSlash = strrchr( szRendererPath, '\\' );
	if ( !pszRendererSlash )
		pszRendererSlash = strrchr( szRendererPath, '/' );
	if ( pszRendererSlash )
		pszRendererSlash[1] = 0;
	CUtlString nativeShaderDll( szRendererPath );
	nativeShaderDll += "stdshader_dx12.dll";
	if ( GetFileAttributesA( nativeShaderDll.Get() ) == INVALID_FILE_ATTRIBUTES )
	{
		Warning( "ShaderAPIDX12: required stdshader_dx12.dll missing beside renderer: %s\n", nativeShaderDll.Get() );
		ShutdownDevice();
		return false;
	}
	D3D12_COMMAND_QUEUE_DESC queueDesc{};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	hr = m_pDevice->CreateCommandQueue( &queueDesc, IID_PPV_ARGS( &m_pQueue ) );
	if ( FAILED( hr ) )
	{
		Warning( "ShaderAPIDX12: CreateCommandQueue failed (0x%08x)\n", static_cast<unsigned>( hr ) );
		ShutdownDevice();
		return false;
	}
	m_nAdapterIndex = nAdapter;
	m_pWindow = hWnd;
	m_nOwnerThread = GetCurrentThreadId();
	m_bWaitForVsync = info.m_bWaitForVSync;
	m_bWindowed = info.m_bWindowed;
	m_nBackBufferCount = clamp( info.m_nBackBufferCount, 1, 2 ) + 1;
	m_nSampleCount = MAX( 1, info.m_nAASamples );
	m_nSampleQuality = info.m_nAAQuality;
	if ( !SupportsMSAA( m_nSampleCount, m_nSampleQuality ) )
	{
		Warning( "ShaderAPIDX12: unsupported MSAA count %d quality %d\n", m_nSampleCount, m_nSampleQuality );
		ShutdownDevice();
		return false;
	}
	Microsoft::WRL::ComPtr<IDXGIFactory5> factory5;
	BOOL bSupported = FALSE;
	if ( SUCCEEDED( m_pFactory.As( &factory5 ) ) )
		factory5->CheckFeatureSupport( DXGI_FEATURE_PRESENT_ALLOW_TEARING, &bSupported, sizeof( bSupported ) );
	m_bAllowTearing = bSupported != FALSE;
	m_nRtvStride = m_pDevice->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_RTV );
	m_nDsvStride = m_pDevice->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_DSV );
	RECT rect{};
	GetClientRect( static_cast<HWND>( hWnd ), &rect );
	const int nWidth = info.m_DisplayMode.m_nWidth > 0 ? info.m_DisplayMode.m_nWidth : static_cast<int>( rect.right - rect.left );
	const int nHeight = info.m_DisplayMode.m_nHeight > 0 ? info.m_DisplayMode.m_nHeight : static_cast<int>( rect.bottom - rect.top );
	if ( !CreateFrameObjects() || !AddView( hWnd ) )
	{
		ShutdownDevice();
		return false;
	}
	if ( nWidth > 0 && nHeight > 0 && ( nWidth != m_nWidth || nHeight != m_nHeight ) && !ResizeView( *m_pCurrentView, nWidth, nHeight ) )
	{
		ShutdownDevice();
		return false;
	}
	m_nWidth = m_pCurrentView->width;
	m_nHeight = m_pCurrentView->height;
	g_pShaderDeviceDX12 = this;
	StartSubmitThread();
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: MSAA support queries for one format / for both scene formats
//-----------------------------------------------------------------------------
bool CShaderDeviceDX12::SupportsMSAAFormat( DXGI_FORMAT format, int nCount, int nQuality ) const
{
	if ( !m_pDevice || nCount < 1 || nQuality < 0 )
		return false;
	if ( nCount == 1 )
		return nQuality == 0;
	D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS levels{ format, static_cast<UINT>( nCount ), D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE, 0 };
	return SUCCEEDED( m_pDevice->CheckFeatureSupport( D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &levels, sizeof( levels ) ) ) && static_cast<UINT>( nQuality ) < levels.NumQualityLevels;
}

bool CShaderDeviceDX12::SupportsMSAA( int nCount, int nQuality ) const
{
	return SupportsMSAAFormat( SceneColorFormat(), nCount, nQuality ) && SupportsMSAAFormat( SceneDepthFormat(), nCount, nQuality );
}

//-----------------------------------------------------------------------------
// Purpose: Recorder chunk callbacks; pContext is the owning device
//-----------------------------------------------------------------------------
unsigned char *CShaderDeviceDX12::AcquireRecorderChunk( void *pContext )
{
	return static_cast<CShaderDeviceDX12 *>( pContext )->AcquireCommandChunk();
}

void CShaderDeviceDX12::FlushRecorderChunk( void *pContext, unsigned char *pChunk, size_t nBytes )
{
	static_cast<CShaderDeviceDX12 *>( pContext )->FlushCommandChunk( pChunk, nBytes );
}

//-----------------------------------------------------------------------------
// Purpose: Creates per-frame allocators/lists, the fence and its event
//-----------------------------------------------------------------------------
bool CShaderDeviceDX12::CreateFrameObjects()
{
	static const bool s_bShadowTiming = CommandLine() && CommandLine()->CheckParm( "-dx12stats" ) && CommandLine()->CheckParm( "-dx12shadowstats" );
	m_bShadowTimingEnabled = s_bShadowTiming;
	for ( int i = 0; i < ARRAYSIZE( m_Frames ); ++i )
		if ( FAILED( m_pDevice->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS( &m_Frames[i].allocator ) ) ) )
		{
			Warning( "ShaderAPIDX12: CreateCommandAllocator failed\n" );
			return false;
		}
	// One list per frame context: the submission worker may still be closing/executing the previous list.
	for ( int i = 0; i < ARRAYSIZE( m_Frames ); ++i )
	{
		HRESULT hr = m_pDevice->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_Frames[i].allocator.Get(), nullptr, IID_PPV_ARGS( &m_Frames[i].list ) );
		if ( FAILED( hr ) )
		{
			Warning( "ShaderAPIDX12: CreateCommandList failed (0x%08x)\n", static_cast<unsigned>( hr ) );
			return false;
		}
		if ( i != 0 && FAILED( m_Frames[i].list->Close() ) )
			return false;
	}
	m_nFrameIndex = 0;
	m_bRecording = true;
	m_Recorder.m_pfnAcquireChunk = AcquireRecorderChunk;
	m_Recorder.m_pfnFlushChunk = FlushRecorderChunk;
	m_Recorder.m_pChunkContext = this;
	HRESULT hr = m_pDevice->CreateFence( 0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS( &m_pFence ) );
	if ( FAILED( hr ) )
	{
		Warning( "ShaderAPIDX12: CreateFence failed (0x%08x)\n", static_cast<unsigned>( hr ) );
		return false;
	}
	m_hFenceEvent = CreateEventW( nullptr, FALSE, FALSE, nullptr );
	if ( !m_hFenceEvent )
		return false;
	// The first list is created open; it does not pass through BeginRecording.
	if ( m_bShadowTimingEnabled && EnsureGpuTiming() )
		GpuTimingAfterSubmit();
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Advances to the next frame context once its fence completed and resets its list
//-----------------------------------------------------------------------------
bool CShaderDeviceDX12::BeginRecording()
{
	ZoneNamedN( ___tracy_scoped_zone, "DX12 BeginRecording", DX12_ZONES_ACTIVE );
	m_nFrameIndex = ( m_nFrameIndex + 1 ) % ARRAYSIZE( m_Frames );
	FrameContext &frame = m_Frames[m_nFrameIndex];
	{
		ZoneNamedN( ___tracy_scoped_zone, "DX12 BeginRecording Fence Wait", DX12_ZONES_ACTIVE );
		if ( frame.fence && !WaitForFence( frame.fence ) )
			return false;
	}
	{
		ZoneNamedN( ___tracy_scoped_zone, "DX12 BeginRecording Retained Release", DX12_ZONES_ACTIVE );
		for ( int i = 0; i < frame.retained.Count(); ++i )
			frame.retained[i]->Release();
		frame.retained.RemoveAll();
		frame.fence = 0;
	}
	{
		// The allocator's previous list completed on the GPU (fence above); reset runs in submission order.
		SubmitOpDX12 op{};
		op.kind = SubmitOpDX12::Reset;
		op.list = frame.list.Get();
		op.allocator = frame.allocator.Get();
		EnqueueSubmission( op );
		if ( m_bFailed )
			return false;
	}
	m_bRecording = true;
	if ( m_bShadowTimingEnabled && !m_bGpuTimingStopping && EnsureGpuTiming() )
		GpuTimingAfterSubmit();
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Keeps resources and query heaps alive until the current recording's fence completes
//-----------------------------------------------------------------------------
void CShaderDeviceDX12::RetainResource( ID3D12Pageable *pResource )
{
	if ( pResource && m_bRecording && IsRecordingOwner() )
	{
		pResource->AddRef();
		m_Frames[m_nFrameIndex].retained.AddToTail( pResource );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Thread-safe texture deletion queue drained by the recording owner
//-----------------------------------------------------------------------------
void CShaderDeviceDX12::QueueTextureDeletion( uintptr_t hTexture )
{
	if ( !hTexture )
		return;
	AUTO_LOCK( m_TextureDeletionMutex );
	m_PendingTextureDeletions.AddToTail( hTexture );
	m_nPendingTextureDeletionCount = m_PendingTextureDeletions.Count();
}

void CShaderDeviceDX12::TakeTextureDeletionRequests( CUtlVector<uintptr_t> &handles )
{
	handles.RemoveAll();
	AUTO_LOCK( m_TextureDeletionMutex );
	handles.Swap( m_PendingTextureDeletions );
	m_nPendingTextureDeletionCount = 0;
}

//-----------------------------------------------------------------------------
// Purpose: Replays the recording, executes and signals the current list, then starts the next recording.
//          Returns the submitted fence value, or 0 on failure.
//-----------------------------------------------------------------------------
uint64_t CShaderDeviceDX12::Submit( bool bWait )
{
	ZoneNamedN( ___tracy_scoped_zone, "DX12 Submit", DX12_ZONES_ACTIVE );
	if ( !m_bRecording || !IsRecordingOwner() || m_bFailed )
	{
		Warning( "ShaderAPIDX12: Submit rejected (wrong owner or no recording)\n" );
		return 0;
	}
	const bool bTiming = GpuTimingBeforeSubmit();
	// A logical occlusion interval can cross any Submit (including descriptor-pressure waits),
	// but D3D12 requires each native Begin/End pair to stay on this list before it is closed.
	if ( g_pShaderAPIDX12 )
		g_pShaderAPIDX12->FinishOcclusionQueriesForSubmit();
	FrameContext &frame = m_Frames[m_nFrameIndex];
	const uint64_t nValue = m_nFenceValue + 1;
	// Recorded commands replay, then Close/ExecuteCommandLists/Signal run, in FIFO order (inline without a worker).
	m_Recorder.Flush();
	SubmitOpDX12 op{};
	op.kind = SubmitOpDX12::Execute;
	op.list = frame.list.Get();
	op.value = nValue;
	EnqueueSubmission( op );
	if ( m_bFailed )
		return 0;
	m_bRecording = false;
	m_nFenceValue = nValue;
	frame.fence = nValue;
	if ( m_pCurrentView )
		m_pCurrentView->lastFence = nValue;
	if ( bWait )
	{
		ZoneNamedN( ___tracy_scoped_zone, "DX12 Submit Wait", DX12_ZONES_ACTIVE );
		if ( !WaitForFence( nValue ) )
			return 0;
	}
	ReportDebugMessages();
	if ( !BeginRecording() )
		return 0;
	if ( bTiming && !m_bShadowTimingEnabled && !m_bGpuTimingStopping )
		GpuTimingAfterSubmit();
	if ( g_pShaderAPIDX12 )
		g_pShaderAPIDX12->ResumeOcclusionQueriesAfterSubmit();
	return nValue;
}

//-----------------------------------------------------------------------------
// Purpose: Frame-paced submit: waits for the previous frame's fence, then submits without waiting
//-----------------------------------------------------------------------------
uint64_t CShaderDeviceDX12::SubmitFrameSync()
{
	if ( !CommandList() )
		return 0;
	// Reference DX9 waits for the previous sync query, then issues this frame's query.
	// Current-frame completion remains mandatory in readback/resource lifecycle paths.
	{
		ZoneNamedN( ___tracy_scoped_zone, "DX12 PreviousFrameWait", DX12_ZONES_ACTIVE );
		if ( !WaitForFence( m_nFrameSyncFence ) )
			return 0;
	}
	const uint64_t nValue = Submit( false );
	if ( nValue )
		m_nFrameSyncFence = nValue;
	return nValue;
}

//-----------------------------------------------------------------------------
// Purpose: Forwards stored debug-layer corruption/error messages as warnings
//-----------------------------------------------------------------------------
void CShaderDeviceDX12::ReportDebugMessages()
{
	if ( !m_pInfoQueue )
		return;
	const UINT64 nCount = m_pInfoQueue->GetNumStoredMessages();
	CUtlVector<unsigned char> storage;
	for ( UINT64 i = 0; i < nCount; ++i )
	{
		SIZE_T nBytes = 0;
		if ( FAILED( m_pInfoQueue->GetMessage( i, nullptr, &nBytes ) ) || !nBytes )
			continue;
		storage.SetCount( static_cast<int>( nBytes ) );
		D3D12_MESSAGE *pMessage = reinterpret_cast<D3D12_MESSAGE *>( storage.Base() );
		if ( FAILED( m_pInfoQueue->GetMessage( i, pMessage, &nBytes ) ) )
			continue;
		if ( pMessage->Severity <= D3D12_MESSAGE_SEVERITY_ERROR )
			Warning( "ShaderAPIDX12 debug layer %s %d: %s\n", pMessage->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION ? "CORRUPTION" : "ERROR", static_cast<int>( pMessage->ID ), pMessage->pDescription );
	}
	m_pInfoQueue->ClearStoredMessages();
}

//-----------------------------------------------------------------------------
// Purpose: Owner-thread GPU-idle boundary (see header)
//-----------------------------------------------------------------------------
bool CShaderDeviceDX12::SubmitAndWaitForGpu()
{
	if ( m_bFailed || !IsRecordingOwner() )
		return false;
	if ( m_bRecording )
		return Submit( true ) != 0;
	FlushSubmissions();
	return WaitForFence( m_nFenceValue );
}

//-----------------------------------------------------------------------------
// Purpose: Issues queued submissions, then waits for nValue; removal or a 5 s GPU timeout fails the device
//-----------------------------------------------------------------------------
bool CShaderDeviceDX12::WaitForFence( uint64_t nValue )
{
	if ( !nValue || !m_pFence )
		return true;
	const uint64_t nInitial = m_pFence->GetCompletedValue();
	if ( nInitial == UINT64_MAX )
	{
		FailDevice( "fence device removal", m_pDevice->GetDeviceRemovedReason() );
		return false;
	}
	if ( nInitial >= nValue )
		return true;
	// CPU command replay (including GPU-validation instrumentation) must issue
	// the queue Signal before the GPU fence deadline begins.
	FlushSubmissions();
	if ( m_bFailed )
		return false;
	if ( !CheckDevice( "fence SetEventOnCompletion", m_pFence->SetEventOnCompletion( nValue, m_hFenceEvent ) ) )
		return false;
	while ( m_pFence->GetCompletedValue() < nValue )
	{
		const DWORD nResult = WaitForSingleObject( m_hFenceEvent, 5000 );
		if ( nResult == WAIT_OBJECT_0 )
			continue;
		const HRESULT reason = m_pDevice->GetDeviceRemovedReason();
		FailDevice( nResult == WAIT_TIMEOUT ? "fence wait timed out" : "fence wait failed", FAILED( reason ) ? reason : HRESULT_FROM_WIN32( ERROR_TIMEOUT ) );
		return false;
	}
	const uint64_t nCompleted = m_pFence->GetCompletedValue();
	if ( nCompleted == UINT64_MAX )
	{
		FailDevice( "fence device removal", m_pDevice->GetDeviceRemovedReason() );
		return false;
	}
	return m_pFence->GetCompletedValue() >= nValue;
}

//-----------------------------------------------------------------------------
// Scene target and back buffer accessors
//-----------------------------------------------------------------------------
ID3D12Resource *CShaderDeviceDX12::SceneColor() const
{
	return m_pCurrentView ? m_pCurrentView->sceneColor.Get() : nullptr;
}

ID3D12Resource *CShaderDeviceDX12::SceneDepth() const
{
	return m_pCurrentView ? m_pCurrentView->sceneDepth.Get() : nullptr;
}

D3D12_CPU_DESCRIPTOR_HANDLE CShaderDeviceDX12::SceneRTV( bool bSRGB ) const
{
	D3D12_CPU_DESCRIPTOR_HANDLE handle = m_pCurrentView && m_pCurrentView->sceneRTVHeap ? m_pCurrentView->sceneRTVStart : D3D12_CPU_DESCRIPTOR_HANDLE{};
	if ( bSRGB && handle.ptr )
		handle.ptr += m_nRtvStride;
	return handle;
}

D3D12_CPU_DESCRIPTOR_HANDLE CShaderDeviceDX12::SceneDSV() const
{
	return m_pCurrentView && m_pCurrentView->sceneDSVHeap ? m_pCurrentView->sceneDSVStart : D3D12_CPU_DESCRIPTOR_HANDLE{};
}

D3D12_CPU_DESCRIPTOR_HANDLE CShaderDeviceDX12::SceneReadOnlyDSV() const
{
	D3D12_CPU_DESCRIPTOR_HANDLE handle = SceneDSV();
	if ( handle.ptr )
		handle.ptr += m_nDsvStride;
	return handle;
}

uint32_t CShaderDeviceDX12::CurrentBackBufferIndex() const
{
	return m_pCurrentView && m_pCurrentView->swap ? m_pCurrentView->swap->GetCurrentBackBufferIndex() : 0;
}

ID3D12Resource *CShaderDeviceDX12::CurrentBackBuffer() const
{
	return m_pCurrentView && m_pCurrentView->swap ? m_pCurrentView->backBuffers[CurrentBackBufferIndex()] : nullptr;
}

D3D12_CPU_DESCRIPTOR_HANDLE CShaderDeviceDX12::CurrentBackBufferRTV() const
{
	D3D12_CPU_DESCRIPTOR_HANDLE handle = m_pCurrentView && m_pCurrentView->rtvHeap ? m_pCurrentView->rtvHeap->GetCPUDescriptorHandleForHeapStart() : D3D12_CPU_DESCRIPTOR_HANDLE{};
	handle.ptr += static_cast<SIZE_T>( CurrentBackBufferIndex() ) * m_nRtvStride;
	return handle;
}

//-----------------------------------------------------------------------------
// Purpose: Records a scene colour / depth state transition when the state changes
//-----------------------------------------------------------------------------
void CShaderDeviceDX12::TransitionSceneColor( D3D12_RESOURCE_STATES state )
{
	if ( !m_pCurrentView || !SceneColor() || !CommandList() || m_pCurrentView->sceneColorState == state )
		return;
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = SceneColor();
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = m_pCurrentView->sceneColorState;
	barrier.Transition.StateAfter = state;
	m_Recorder.ResourceBarrier( 1, &barrier );
	m_pCurrentView->sceneColorState = state;
}

void CShaderDeviceDX12::TransitionSceneDepth( D3D12_RESOURCE_STATES state )
{
	if ( !m_pCurrentView || !SceneDepth() || !CommandList() || m_pCurrentView->sceneDepthState == state )
		return;
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = SceneDepth();
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = m_pCurrentView->sceneDepthState;
	barrier.Transition.StateAfter = state;
	m_Recorder.ResourceBarrier( 1, &barrier );
	m_pCurrentView->sceneDepthState = state;
}

//-----------------------------------------------------------------------------
// Purpose: Creates the back-buffer RTVs and the scene colour/depth targets of a view
//-----------------------------------------------------------------------------
bool CShaderDeviceDX12::CreateViewTargets( View &view )
{
	D3D12_DESCRIPTOR_HEAP_DESC heap{};
	heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	heap.NumDescriptors = m_nBackBufferCount;
	if ( FAILED( m_pDevice->CreateDescriptorHeap( &heap, IID_PPV_ARGS( &view.rtvHeap ) ) ) )
		return false;
	heap.NumDescriptors = 2;
	if ( FAILED( m_pDevice->CreateDescriptorHeap( &heap, IID_PPV_ARGS( &view.sceneRTVHeap ) ) ) )
		return false;
	heap.NumDescriptors = 2;
	heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	if ( FAILED( m_pDevice->CreateDescriptorHeap( &heap, IID_PPV_ARGS( &view.sceneDSVHeap ) ) ) )
		return false;
	view.ReleaseBackBuffers();
	view.backBuffers.SetCount( m_nBackBufferCount );
	for ( int i = 0; i < view.backBuffers.Count(); ++i )
		view.backBuffers[i] = nullptr;
	D3D12_CPU_DESCRIPTOR_HANDLE handle = view.rtvHeap->GetCPUDescriptorHandleForHeapStart();
	for ( int i = 0; i < m_nBackBufferCount; ++i )
	{
		HRESULT hr = view.swap->GetBuffer( i, IID_PPV_ARGS( &view.backBuffers[i] ) );
		if ( FAILED( hr ) )
		{
			Warning( "ShaderAPIDX12: GetBuffer failed (0x%08x)\n", static_cast<unsigned>( hr ) );
			return false;
		}
		m_pDevice->CreateRenderTargetView( view.backBuffers[i], nullptr, handle );
		handle.ptr += m_nRtvStride;
	}
	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_RESOURCE_DESC resource{};
	resource.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resource.Width = view.width;
	resource.Height = view.height;
	resource.DepthOrArraySize = 1;
	resource.MipLevels = 1;
	resource.SampleDesc.Count = m_nSampleCount;
	resource.SampleDesc.Quality = m_nSampleQuality;
	resource.Format = SceneColorFormat();
	resource.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	D3D12_CLEAR_VALUE color{};
	color.Format = SceneColorFormat();
	color.Color[3] = 1;
	HRESULT hr = m_pDevice->CreateCommittedResource( &heapProperties, D3D12_HEAP_FLAG_NONE, &resource, D3D12_RESOURCE_STATE_RENDER_TARGET, &color, IID_PPV_ARGS( &view.sceneColor ) );
	if ( FAILED( hr ) )
	{
		Warning( "ShaderAPIDX12: Create scene color failed (0x%08x)\n", static_cast<unsigned>( hr ) );
		return false;
	}
	view.sceneColorState = D3D12_RESOURCE_STATE_RENDER_TARGET;
	D3D12_RENDER_TARGET_VIEW_DESC colorView{};
	colorView.ViewDimension = m_nSampleCount > 1 ? D3D12_RTV_DIMENSION_TEXTURE2DMS : D3D12_RTV_DIMENSION_TEXTURE2D;
	const D3D12_CPU_DESCRIPTOR_HANDLE sceneViewStart = view.sceneRTVHeap->GetCPUDescriptorHandleForHeapStart();
	view.sceneRTVStart = sceneViewStart;
	colorView.Format = SceneColorFormat();
	m_pDevice->CreateRenderTargetView( view.sceneColor.Get(), &colorView, sceneViewStart );
	D3D12_CPU_DESCRIPTOR_HANDLE gammaView{ sceneViewStart.ptr + m_nRtvStride };
	m_pDevice->CreateRenderTargetView( view.sceneColor.Get(), &colorView, gammaView );
	resource.Format = DXGI_FORMAT_R24G8_TYPELESS;
	resource.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
	D3D12_CLEAR_VALUE depth{};
	depth.Format = SceneDepthFormat();
	depth.DepthStencil.Depth = 1;
	hr = m_pDevice->CreateCommittedResource( &heapProperties, D3D12_HEAP_FLAG_NONE, &resource, D3D12_RESOURCE_STATE_DEPTH_WRITE, &depth, IID_PPV_ARGS( &view.sceneDepth ) );
	if ( FAILED( hr ) )
	{
		Warning( "ShaderAPIDX12: Create scene depth failed (0x%08x)\n", static_cast<unsigned>( hr ) );
		return false;
	}
	view.sceneDepthState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
	view.sceneDSVStart = view.sceneDSVHeap->GetCPUDescriptorHandleForHeapStart();
	D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
	dsv.Format = SceneDepthFormat();
	dsv.ViewDimension = m_nSampleCount > 1 ? D3D12_DSV_DIMENSION_TEXTURE2DMS : D3D12_DSV_DIMENSION_TEXTURE2D;
	m_pDevice->CreateDepthStencilView( view.sceneDepth.Get(), &dsv, view.sceneDSVStart );
	dsv.Flags = D3D12_DSV_FLAG_READ_ONLY_DEPTH | D3D12_DSV_FLAG_READ_ONLY_STENCIL;
	D3D12_CPU_DESCRIPTOR_HANDLE readOnly{ view.sceneDSVStart.ptr + m_nDsvStride };
	m_pDevice->CreateDepthStencilView( view.sceneDepth.Get(), &dsv, readOnly );
	return true;
}

//-----------------------------------------------------------------------------
void CShaderDeviceDX12::QueryDisplayHdr( View &view )
{
	m_nHdrDisplayStatus = 0;
	if ( !view.swap )
		return;
	Microsoft::WRL::ComPtr<IDXGIOutput> output;
	Microsoft::WRL::ComPtr<IDXGIOutput6> output6;
	if ( FAILED( view.swap->GetContainingOutput( &output ) ) || FAILED( output.As( &output6 ) ) )
		return;
	DXGI_OUTPUT_DESC1 desc{};
	if ( FAILED( output6->GetDesc1( &desc ) ) )
		return;
	const bool bHdr = desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
	const int nits = clamp( static_cast<int>( desc.MaxLuminance + 0.5f ), 80, 10000 );
	m_nHdrDisplayStatus = ( bHdr ? SHADERAPIDX12_HDR_DISPLAY_HDR : SHADERAPIDX12_HDR_DISPLAY_SDR ) | ( nits << 8 );
}

//-----------------------------------------------------------------------------
// Purpose: Creates the swap chain and targets of a view; a zero-sized view stays suspended
//-----------------------------------------------------------------------------
bool CShaderDeviceDX12::CreateView( View &view, HWND hWnd, int nWidth, int nHeight )
{
	view.hwnd = hWnd;
	view.width = nWidth;
	view.height = nHeight;
	if ( nWidth <= 0 || nHeight <= 0 )
	{
		view.suspended = true;
		return true;
	}
	view.suspended = false;
	view.occluded = false;
	DXGI_SWAP_CHAIN_DESC1 desc{};
	desc.Width = nWidth;
	desc.Height = nHeight;
	desc.Format = m_PresentFormat;
	desc.BufferCount = m_nBackBufferCount;
	desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	desc.SampleDesc.Count = 1;
	if ( m_bAllowTearing )
		desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
	// DLSS-G: the chain comes from the Streamline proxy factory so its Present is intercepted.
	IDXGIFactory6 *pFactory = m_FrameGen.SwapChainFactory() ? m_FrameGen.SwapChainFactory() : m_pFactory.Get();
	Microsoft::WRL::ComPtr<IDXGISwapChain1> swap;
	HRESULT hr = pFactory->CreateSwapChainForHwnd( m_pQueue.Get(), hWnd, &desc, nullptr, nullptr, &swap );
	if ( FAILED( hr ) )
	{
		Warning( "ShaderAPIDX12: CreateSwapChainForHwnd failed (0x%08x)\n", static_cast<unsigned>( hr ) );
		return false;
	}
	if ( FAILED( swap.As( &view.swap ) ) )
		return false;
	swap.Reset();
	// scRGB: linear Rec.709 primaries, values may exceed 1. Unsupported colour spaces keep the FP16 swap chain.
	// An 8-bit chain is plain sRGB and takes no explicit colour space.
	if ( m_PresentFormat == DXGI_FORMAT_R16G16B16A16_FLOAT )
	{
		UINT nColorSpaceSupport = 0;
		if ( SUCCEEDED( view.swap->CheckColorSpaceSupport( DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709, &nColorSpaceSupport ) ) && ( nColorSpaceSupport & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT ) )
		{
			const HRESULT hrColorSpace = view.swap->SetColorSpace1( DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709 );
			if ( FAILED( hrColorSpace ) )
				Warning( "ShaderAPIDX12: SetColorSpace1(scRGB) failed (0x%08x); presenting FP16 without an explicit colour space\n", static_cast<unsigned>( hrColorSpace ) );
		}
		else
			Warning( "ShaderAPIDX12: scRGB swap-chain colour space unsupported; presenting FP16 without an explicit colour space\n" );
	}
	m_pFactory->MakeWindowAssociation( hWnd, DXGI_MWA_NO_ALT_ENTER );
	// The provider takes the chain over while it is the only reference and before any back buffer is acquired
	// (FSR replaces it with its frame-interpolation chain, XeFG with its proxy). Frame generation never runs in
	// exclusive fullscreen (ChangeMode drops it first), so the fullscreen transition only ever sees a native chain.
	if ( m_FrameGen.Kind() != FrameGenKindDX12::None && !m_FrameGen.AdoptSwapChain( view.swap, m_pQueue.Get(), g_pShaderAPIDX12 && g_pShaderAPIDX12->ProjectionIsInverted() ) )
	{
		Warning( "ShaderAPIDX12: frame generation could not adopt the swap chain: %s\n", m_FrameGen.LastError() );
		return false;
	}
	if ( !m_bWindowed && !CheckDevice( "SetFullscreenState", view.swap->SetFullscreenState( TRUE, nullptr ) ) )
		return false;
	const bool bTargetsCreated = CreateViewTargets( view );
	if ( bTargetsCreated )
		QueryDisplayHdr( view );
	return bTargetsCreated;
}

//-----------------------------------------------------------------------------
// Purpose: Resizes a view's swap chain and recreates its targets (notifies mode-change listeners)
//-----------------------------------------------------------------------------
bool CShaderDeviceDX12::ResizeView( View &view, int nWidth, int nHeight )
{
	FlushSubmissions();
	if ( nWidth <= 0 || nHeight <= 0 )
	{
		view.suspended = true;
		return true;
	}
	if ( view.swap && !view.suspended && nWidth == view.width && nHeight == view.height )
		return true;
	if ( !m_bChangingMode && g_pShaderDeviceDX12 == this && view.swap && ( nWidth != view.width || nHeight != view.height ) )
	{
		m_bChangingMode = true;
		if ( g_pShaderDeviceMgrDX12 )
			g_pShaderDeviceMgrDX12->NotifyModeChange();
		m_bChangingMode = false;
	}
	if ( m_bRecording && !Submit( false ) )
		return false;
	if ( !WaitForFence( view.lastFence ) )
		return false;
	view.sceneColor.Reset();
	view.sceneDepth.Reset();
	view.ReleaseBackBuffers();
	view.rtvHeap.Reset();
	view.sceneRTVHeap.Reset();
	view.sceneDSVHeap.Reset();
	view.width = nWidth;
	view.height = nHeight;
	view.suspended = false;
	view.occluded = false;
	if ( !view.swap )
		return CreateView( view, view.hwnd, nWidth, nHeight );
	DXGI_SWAP_CHAIN_DESC1 old{};
	if ( FAILED( view.swap->GetDesc1( &old ) ) )
		return false;
	m_FrameGen.BeforeResize( view.swap.Get() );
	HRESULT hr = view.swap->ResizeBuffers( m_nBackBufferCount, nWidth, nHeight, m_PresentFormat, old.Flags );
	if ( !CheckDevice( "ResizeBuffers", hr ) )
		return false;
	if ( !CreateViewTargets( view ) )
		return false;
	QueryDisplayHdr( view );
	if ( !m_FrameGen.AfterResize( nWidth, nHeight ) )
		Warning( "ShaderAPIDX12: frame generation could not follow the resize to %dx%d: %s\n", nWidth, nHeight, m_FrameGen.LastError() );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Applies a new windowed/vsync/MSAA/back-buffer configuration to every view
//-----------------------------------------------------------------------------
bool CShaderDeviceDX12::ChangeMode( const ShaderDeviceInfo_t &info )
{
	if ( !IsRecordingOwner() )
		return false;
	FlushSubmissions();
	if ( !SupportsMSAA( MAX( 1, info.m_nAASamples ), info.m_nAAQuality ) )
		return false;
	RECT newRect{};
	if ( m_pCurrentView )
		GetClientRect( m_pCurrentView->hwnd, &newRect );
	const int nNewWidth = info.m_DisplayMode.m_nWidth > 0 ? info.m_DisplayMode.m_nWidth : newRect.right - newRect.left;
	const int nNewHeight = info.m_DisplayMode.m_nHeight > 0 ? info.m_DisplayMode.m_nHeight : newRect.bottom - newRect.top;
	const bool bDropFrameGen = !info.m_bWindowed && m_FrameGen.Kind() != FrameGenKindDX12::None;
	const bool bChangesResources = bDropFrameGen || info.m_bWindowed != m_bWindowed || MAX( 1, info.m_nAASamples ) != m_nSampleCount || info.m_nAAQuality != m_nSampleQuality || clamp( info.m_nBackBufferCount, 1, 2 ) + 1 != m_nBackBufferCount || ( m_pCurrentView && ( nNewWidth != m_pCurrentView->width || nNewHeight != m_pCurrentView->height ) );
	m_bChangingMode = true;

	struct ChangeScope
	{
		bool &m_bFlag;

		~ChangeScope() { m_bFlag = false; }
	} changeScope{ m_bChangingMode };

	if ( bChangesResources && g_pShaderDeviceMgrDX12 )
		g_pShaderDeviceMgrDX12->NotifyModeChange();
	if ( m_bRecording && !Submit( true ) )
		return false;
	for ( View *pView : m_Views )
		if ( !WaitForFence( pView->lastFence ) )
			return false;
	// Frame generation is windowed/borderless only: leave it (natively re-created chains) before going fullscreen.
	// A pending request from this frame is dropped with it; the shader API re-requests when windowed again.
	if ( bDropFrameGen )
	{
		m_PendingSelect.valid = false;
		SelectFrameGen( FrameGenKindDX12::None, 2, false );
		if ( !RecreateViews() )
			return false;
	}
	const bool bFullscreen = !info.m_bWindowed;
	for ( View *pView : m_Views )
		if ( pView->swap && m_bWindowed != info.m_bWindowed && !CheckDevice( "SetFullscreenState", pView->swap->SetFullscreenState( bFullscreen, nullptr ) ) )
			return false;
	m_bWindowed = info.m_bWindowed;
	m_bWaitForVsync = info.m_bWaitForVSync;
	const int nSamples = MAX( 1, info.m_nAASamples ), nQuality = info.m_nAAQuality;
	const int nBuffers = clamp( info.m_nBackBufferCount, 1, 2 ) + 1;
	if ( nSamples != m_nSampleCount || nQuality != m_nSampleQuality || nBuffers != m_nBackBufferCount )
	{
		m_nSampleCount = nSamples;
		m_nSampleQuality = nQuality;
		m_nBackBufferCount = nBuffers;
		if ( !RecreateViews() )
			return false;
	}
	if ( m_pCurrentView )
	{
		RECT rect{};
		GetClientRect( m_pCurrentView->hwnd, &rect );
		const int nWidth = info.m_DisplayMode.m_nWidth > 0 ? info.m_DisplayMode.m_nWidth : rect.right - rect.left;
		const int nHeight = info.m_DisplayMode.m_nHeight > 0 ? info.m_DisplayMode.m_nHeight : rect.bottom - rect.top;
		if ( !ResizeView( *m_pCurrentView, nWidth, nHeight ) )
			return false;
		m_nWidth = m_pCurrentView->width;
		m_nHeight = m_pCurrentView->height;
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Releases every view's targets and provider chain and creates them again (GPU idle)
//-----------------------------------------------------------------------------
bool CShaderDeviceDX12::RecreateViews()
{
	for ( View *pView : m_Views )
	{
		pView->sceneColor.Reset();
		pView->sceneDepth.Reset();
		pView->ReleaseBackBuffers();
		pView->rtvHeap.Reset();
		pView->sceneRTVHeap.Reset();
		pView->sceneDSVHeap.Reset();
		m_FrameGen.ReleaseSwapChain( pView->swap );
	}
	for ( View *pView : m_Views )
		if ( !CreateView( *pView, pView->hwnd, pView->width, pView->height ) )
			return false;
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Switches the provider kind; DLSS-G swaps the presenting queue for a Streamline proxy queue (and back)
//-----------------------------------------------------------------------------
bool CShaderDeviceDX12::SelectFrameGen( FrameGenKindDX12 kind, uint32_t nMultiplier, bool bHudless )
{
	ID3D12CommandQueue *pQueue = m_pQueue.Get();
	const bool bOk = m_FrameGen.Select( kind, nMultiplier, bHudless, pQueue );
	if ( pQueue != m_pQueue.Get() )
		m_pQueue.Attach( pQueue ); // the provider created it with one reference for us
	m_PresentFormat = m_FrameGen.PresentFormat();
	m_nPresentFormat = static_cast<int>( m_PresentFormat );
	return bOk;
}

//-----------------------------------------------------------------------------
// Purpose: Applies the pending kind switch (F5): worker drained, GPU idle, chains released, provider selected,
//          chains created again through the provider. Failure falls back to the native chain (status -2).
//-----------------------------------------------------------------------------
void CShaderDeviceDX12::ApplyFrameGenSelect()
{
	if ( !m_PendingSelect.valid || !IsRecordingOwner() || m_bFailed )
		return;
	const PendingSelectDX12 request = m_PendingSelect;
	m_PendingSelect.valid = false;
	FlushSubmissions();
	if ( m_bRecording && !Submit( true ) )
	{
		m_bSelectFailed = true;
		return;
	}
	for ( View *pView : m_Views )
		if ( !WaitForFence( pView->lastFence ) )
			return;
	// A provider switch only replaces our private presentation resources; it
	// does not change the window's display mode. The engine's mode callback
	// mutates the HWND and deadlocks here when Present runs on the material
	// worker while the window thread waits for that worker to finish.
	for ( View *pView : m_Views )
	{
		pView->sceneColor.Reset();
		pView->sceneDepth.Reset();
		pView->ReleaseBackBuffers();
		pView->rtvHeap.Reset();
		pView->sceneRTVHeap.Reset();
		pView->sceneDSVHeap.Reset();
		m_FrameGen.ReleaseSwapChain( pView->swap );
	}
	bool bOk = SelectFrameGen( request.kind, request.multiplier, request.hudless );
	bool bViews = true;
	for ( View *pView : m_Views )
		if ( !CreateView( *pView, pView->hwnd, pView->width, pView->height ) )
		{
			bViews = false;
			break;
		}
	if ( bOk && !bViews )
	{
		// Adoption failed: back to the native chain so the frame keeps presenting.
		bOk = false;
		for ( View *pView : m_Views )
		{
			pView->ReleaseBackBuffers();
			pView->rtvHeap.Reset();
			pView->sceneRTVHeap.Reset();
			pView->sceneDSVHeap.Reset();
			pView->sceneColor.Reset();
			pView->sceneDepth.Reset();
			m_FrameGen.ReleaseSwapChain( pView->swap );
		}
		SelectFrameGen( FrameGenKindDX12::None, 2, false );
		bViews = true;
		for ( View *pView : m_Views )
			if ( !CreateView( *pView, pView->hwnd, pView->width, pView->height ) )
				bViews = false;
	}
	if ( !bViews )
	{
		FailDevice( "frame generation swap chain", E_FAIL );
		return;
	}
	m_bSelectFailed = !bOk && request.kind != FrameGenKindDX12::None;
	if ( m_pCurrentView )
	{
		m_nWidth = m_pCurrentView->width;
		m_nHeight = m_pCurrentView->height;
	}
}

void CShaderDeviceDX12::SetFrameGenFpsLimit( float flFps )
{
	if ( !IsRecordingOwner() )
		return;
	FlushSubmissions();
	// XeLL's sleep mode must change with the GPU idle; Reflex tolerates a live queue.
	if ( m_FrameGen.Kind() == FrameGenKindDX12::XeFG && !SubmitAndWaitForGpu() )
		return;
	m_FrameGen.SetFpsLimit( flFps );
}

//-----------------------------------------------------------------------------
// Purpose: Replays recorded commands onto the native list; the device performs descriptor copies
//-----------------------------------------------------------------------------
void CCommandRecorderDX12::Replay( ID3D12GraphicsCommandList *list, ID3D12Device *device, const unsigned char *data, size_t size )
{
	ZoneNamedN( replayZone, "DX12 CommandReplay", DX12_ZONES_ACTIVE );
	size_t offset = 0;
	while ( offset < size )
	{
		Header header;
		memcpy( &header, data + offset, sizeof( header ) );
		const unsigned char *p = data + offset + sizeof( Header );
		offset += header.size;
		const auto get = [&]( auto &value )
		{
			memcpy( &value, p, sizeof( value ) );
			p += sizeof( value );
		};
		switch ( header.op )
		{
		case Op::OMSetRenderTargets:
		{
			UINT count, single, stored, hasDepth;
			get( count );
			get( single );
			get( stored );
			get( hasDepth );
			D3D12_CPU_DESCRIPTOR_HANDLE rtvs[8];
			memcpy( rtvs, p, stored * sizeof( D3D12_CPU_DESCRIPTOR_HANDLE ) );
			p += stored * sizeof( D3D12_CPU_DESCRIPTOR_HANDLE );
			D3D12_CPU_DESCRIPTOR_HANDLE dsv;
			get( dsv );
			list->OMSetRenderTargets( count, stored ? rtvs : nullptr, single, hasDepth ? &dsv : nullptr );
			break;
		}
		case Op::OMSetStencilRef:
		{
			UINT value;
			get( value );
			list->OMSetStencilRef( value );
			break;
		}
		case Op::RSSetViewports:
		{
			UINT count;
			get( count );
			D3D12_VIEWPORT viewports[16];
			memcpy( viewports, p, count * sizeof( D3D12_VIEWPORT ) );
			list->RSSetViewports( count, viewports );
			break;
		}
		case Op::RSSetScissorRects:
		{
			UINT count;
			get( count );
			D3D12_RECT rects[16];
			memcpy( rects, p, count * sizeof( D3D12_RECT ) );
			list->RSSetScissorRects( count, rects );
			break;
		}
		case Op::SetGraphicsRootSignature:
		{
			ID3D12RootSignature *root;
			get( root );
			list->SetGraphicsRootSignature( root );
			break;
		}
		case Op::SetGraphicsRootDescriptorTable:
		{
			UINT index;
			D3D12_GPU_DESCRIPTOR_HANDLE table;
			get( index );
			get( table );
			list->SetGraphicsRootDescriptorTable( index, table );
			break;
		}
		case Op::SetGraphicsRootConstantBufferView:
		{
			UINT index;
			D3D12_GPU_VIRTUAL_ADDRESS address;
			get( index );
			get( address );
			list->SetGraphicsRootConstantBufferView( index, address );
			break;
		}
		case Op::SetGraphicsRootUnorderedAccessView:
		{
			UINT index;
			D3D12_GPU_VIRTUAL_ADDRESS address;
			get( index );
			get( address );
			list->SetGraphicsRootUnorderedAccessView( index, address );
			break;
		}
		case Op::SetGraphicsRootShaderResourceView:
		{
			UINT index;
			D3D12_GPU_VIRTUAL_ADDRESS address;
			get( index );
			get( address );
			list->SetGraphicsRootShaderResourceView( index, address );
			break;
		}
		case Op::SetGraphicsRoot32BitConstants:
		{
			UINT index, count, first;
			get( index );
			get( count );
			get( first );
			UINT values[64];
			memcpy( values, p, ( count < 64 ? count : 64 ) * 4 );
			list->SetGraphicsRoot32BitConstants( index, count, values, first );
			break;
		}
		case Op::SetPipelineState:
		{
			ID3D12PipelineState *pso;
			get( pso );
			list->SetPipelineState( pso );
			break;
		}
		case Op::SetDescriptorHeaps:
		{
			UINT count;
			get( count );
			ID3D12DescriptorHeap *heaps[2] = {};
			memcpy( heaps, p, count * sizeof( void * ) );
			list->SetDescriptorHeaps( count, heaps );
			break;
		}
		case Op::IASetVertexBuffers:
		{
			UINT start, count, stored;
			get( start );
			get( count );
			get( stored );
			D3D12_VERTEX_BUFFER_VIEW views[D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
			memcpy( views, p, stored * sizeof( D3D12_VERTEX_BUFFER_VIEW ) );
			list->IASetVertexBuffers( start, count, stored ? views : nullptr );
			break;
		}
		case Op::IASetIndexBuffer:
		{
			UINT present;
			D3D12_INDEX_BUFFER_VIEW view;
			get( present );
			get( view );
			list->IASetIndexBuffer( present ? &view : nullptr );
			break;
		}
		case Op::IASetPrimitiveTopology:
		{
			D3D12_PRIMITIVE_TOPOLOGY topology;
			get( topology );
			list->IASetPrimitiveTopology( topology );
			break;
		}
		case Op::DrawInstanced:
		{
			UINT a, b, c, d;
			get( a );
			get( b );
			get( c );
			get( d );
			list->DrawInstanced( a, b, c, d );
			break;
		}
		case Op::DrawIndexedInstanced:
		{
			UINT a, b, c, e;
			INT d;
			get( a );
			get( b );
			get( c );
			get( d );
			get( e );
			list->DrawIndexedInstanced( a, b, c, d, e );
			break;
		}
		case Op::ResourceBarrier:
		{
			UINT count;
			get( count );
			D3D12_RESOURCE_BARRIER barriers[16];
			for ( UINT done = 0; done < count; )
			{
				const UINT n = ( count - done ) < 16 ? count - done : 16;
				memcpy( barriers, p + done * sizeof( D3D12_RESOURCE_BARRIER ), n * sizeof( D3D12_RESOURCE_BARRIER ) );
				list->ResourceBarrier( n, barriers );
				done += n;
			}
			break;
		}
		case Op::CopyBufferRegion:
		{
			ID3D12Resource *dst, *src;
			UINT64 dstOffset, srcOffset, bytes;
			get( dst );
			get( dstOffset );
			get( src );
			get( srcOffset );
			get( bytes );
			list->CopyBufferRegion( dst, dstOffset, src, srcOffset, bytes );
			break;
		}
		case Op::CopyTextureRegion:
		{
			D3D12_TEXTURE_COPY_LOCATION dst, src;
			UINT x, y, z, hasBox;
			D3D12_BOX box;
			get( dst );
			get( x );
			get( y );
			get( z );
			get( src );
			get( hasBox );
			get( box );
			list->CopyTextureRegion( &dst, x, y, z, &src, hasBox ? &box : nullptr );
			break;
		}
		case Op::CopyResource:
		{
			ID3D12Resource *dst, *src;
			get( dst );
			get( src );
			list->CopyResource( dst, src );
			break;
		}
		case Op::ResolveSubresource:
		{
			ID3D12Resource *dst, *src;
			UINT dstSub, srcSub;
			DXGI_FORMAT format;
			get( dst );
			get( dstSub );
			get( src );
			get( srcSub );
			get( format );
			list->ResolveSubresource( dst, dstSub, src, srcSub, format );
			break;
		}
		case Op::ClearRenderTargetView:
		{
			D3D12_CPU_DESCRIPTOR_HANDLE rtv;
			FLOAT color[4];
			UINT count, stored;
			get( rtv );
			get( color );
			get( count );
			get( stored );
			D3D12_RECT rects[16];
			memcpy( rects, p, ( stored < 16 ? stored : 16 ) * sizeof( D3D12_RECT ) );
			list->ClearRenderTargetView( rtv, color, count, stored ? rects : nullptr );
			break;
		}
		case Op::ClearDepthStencilView:
		{
			D3D12_CPU_DESCRIPTOR_HANDLE dsv;
			D3D12_CLEAR_FLAGS flags;
			FLOAT depth;
			UINT stencil, count, stored;
			get( dsv );
			get( flags );
			get( depth );
			get( stencil );
			get( count );
			get( stored );
			D3D12_RECT rects[16];
			memcpy( rects, p, ( stored < 16 ? stored : 16 ) * sizeof( D3D12_RECT ) );
			list->ClearDepthStencilView( dsv, flags, depth, static_cast<UINT8>( stencil ), count, stored ? rects : nullptr );
			break;
		}
		case Op::BeginQuery:
		{
			ID3D12QueryHeap *heap;
			D3D12_QUERY_TYPE type;
			UINT index;
			get( heap );
			get( type );
			get( index );
			list->BeginQuery( heap, type, index );
			break;
		}
		case Op::EndQuery:
		{
			ID3D12QueryHeap *heap;
			D3D12_QUERY_TYPE type;
			UINT index;
			get( heap );
			get( type );
			get( index );
			list->EndQuery( heap, type, index );
			break;
		}
		case Op::CopyDescriptorTable:
		{
			D3D12_CPU_DESCRIPTOR_HANDLE destination;
			UINT count, pad;
			get( destination );
			get( count );
			get( pad );
			D3D12_CPU_DESCRIPTOR_HANDLE sources[32];
			const UINT n = count < 32 ? count : 32;
			memcpy( sources, p, n * sizeof( D3D12_CPU_DESCRIPTOR_HANDLE ) );
			device->CopyDescriptors( 1, &destination, &n, n, sources, nullptr, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
			break;
		}
		case Op::ResolveQueryData:
		{
			ID3D12QueryHeap *heap;
			D3D12_QUERY_TYPE type;
			UINT start, count;
			ID3D12Resource *dst;
			UINT64 destOffset;
			get( heap );
			get( type );
			get( start );
			get( count );
			get( dst );
			get( destOffset );
			list->ResolveQueryData( heap, type, start, count, dst, destOffset );
			break;
		}
		case Op::ExternalCommand:
		{
			ExternalFnDX12 fn;
			uint32_t bytes, copy;
			get( fn );
			get( bytes );
			get( copy );
			const size_t fixed = sizeof( Header ) + sizeof( fn ) + 8;
			if ( header.size < fixed || bytes != copy || bytes > header.size - fixed || offset > size || !fn )
			{
				Warning( "ShaderAPIDX12: malformed external command (%u payload bytes in a %u-byte record)\n", bytes, header.size );
				break;
			}
			fn( list, device, p );
			break;
		}
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Leaves fullscreen and destroys every view
//-----------------------------------------------------------------------------
void CShaderDeviceDX12::ReleaseViews()
{
	FlushSubmissions();
	for ( View *pView : m_Views )
	{
		if ( pView->swap && !m_bWindowed )
			pView->swap->SetFullscreenState( FALSE, nullptr );
		pView->ReleaseBackBuffers();
		m_FrameGen.ReleaseSwapChain( pView->swap );
	}
	m_Views.PurgeAndDeleteElements();
	m_pCurrentView = nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: Executes one queued operation (on the worker, or inline without one)
//-----------------------------------------------------------------------------
HRESULT CShaderDeviceDX12::RunSubmission( const SubmitOpDX12 &op )
{
	HRESULT hr = S_OK;
	switch ( op.kind )
	{
	case SubmitOpDX12::Replay:
		CCommandRecorderDX12::Replay( op.list, m_pDevice.Get(), op.chunk, op.chunkBytes );
		{
			AUTO_LOCK( m_ChunkMutex );
			m_FreeChunks.AddToTail( op.chunk );
		}
		break;
	case SubmitOpDX12::Reset:
		hr = op.allocator->Reset();
		if ( SUCCEEDED( hr ) )
			hr = op.list->Reset( op.allocator, nullptr );
		break;
	case SubmitOpDX12::Execute:
	{
		ZoneNamedN( submitZone, "DX12 Submit Execute", DX12_ZONES_ACTIVE );
		hr = op.list->Close();
		if ( SUCCEEDED( hr ) )
		{
			ID3D12CommandList *pLists[] = { op.list };
			m_pQueue->ExecuteCommandLists( 1, pLists );
		}
		// Signal even after a failed Close so fence waits cannot hang; the error is reported at the next flush.
		const HRESULT hrSignal = m_pQueue->Signal( m_pFence.Get(), op.value );
		if ( SUCCEEDED( hr ) )
			hr = hrSignal;
		break;
	}
	case SubmitOpDX12::Present:
	{
		ZoneNamedN( nativePresent, "DX12 DXGI Present", DX12_ZONES_ACTIVE );
		m_FrameGen.BeforePresent( op.frameId, op.framegenActive );
		hr = op.swap->Present( op.interval, op.flags );
		m_FrameGen.AfterPresent( op.frameId );
		if ( hr == DXGI_STATUS_OCCLUDED )
		{
			op.view->occluded = true;
			hr = S_OK;
		}
		break;
	}
	}
	return hr;
}

//-----------------------------------------------------------------------------
// Purpose: Returns a free recording chunk, allocating up to kMaxCommandChunks (unbounded without a worker)
//-----------------------------------------------------------------------------
unsigned char *CShaderDeviceDX12::AcquireCommandChunk()
{
	for ( ;; )
	{
		{
			AUTO_LOCK( m_ChunkMutex );
			if ( m_FreeChunks.Count() )
			{
				unsigned char *pChunk = m_FreeChunks.Tail();
				m_FreeChunks.RemoveMultipleFromTail( 1 );
				return pChunk;
			}
			if ( m_AllChunks.Count() < kMaxCommandChunks || !m_hSubmitThread )
			{
				unsigned char *pChunk = static_cast<unsigned char *>( MemAlloc_AllocAligned( CCommandRecorderDX12::kChunkBytes, 64 ) );
				m_AllChunks.AddToTail( pChunk );
				return pChunk;
			}
		}
		// Every chunk is queued: wait for the worker to replay one.
		m_SubmitDoneEvent.Wait();
	}
}

//-----------------------------------------------------------------------------
// Purpose: Queues a filled chunk for replay onto the current frame's list
//-----------------------------------------------------------------------------
void CShaderDeviceDX12::FlushCommandChunk( unsigned char *pChunk, size_t nBytes )
{
	SubmitOpDX12 op{};
	op.kind = SubmitOpDX12::Replay;
	op.list = m_Frames[m_nFrameIndex].list.Get();
	op.chunk = pChunk;
	op.chunkBytes = nBytes;
	EnqueueSubmission( op );
}

void CShaderDeviceDX12::ReleaseCommandChunks()
{
	AUTO_LOCK( m_ChunkMutex );
	for ( unsigned char *pChunk : m_AllChunks )
		MemAlloc_FreeAligned( pChunk );
	m_AllChunks.RemoveAll();
	m_FreeChunks.RemoveAll();
}

//-----------------------------------------------------------------------------
// Purpose: Submission worker: runs queued operations in FIFO order until asked to exit
//-----------------------------------------------------------------------------
uintp CShaderDeviceDX12::SubmitThreadMain( void *pParam )
{
	CShaderDeviceDX12 &device = *static_cast<CShaderDeviceDX12 *>( pParam );
	for ( ;; )
	{
		device.m_SubmitWorkEvent.Wait();
		for ( ;; )
		{
			const uint32_t nTail = device.m_nSubmitTail;
			if ( nTail == device.m_nSubmitHead )
				break;
			const SubmitOpDX12 &op = device.m_SubmitOps[nTail % ARRAYSIZE( device.m_SubmitOps )];
			const HRESULT hr = device.RunSubmission( op );
			if ( FAILED( hr ) )
				device.m_nSubmitError.AssignIf( S_OK, hr );
			device.m_nSubmitTail = nTail + 1;
			device.m_SubmitDoneEvent.Set();
		}
		if ( device.m_bSubmitExit )
			break;
	}
	return 0;
}

//-----------------------------------------------------------------------------
// Purpose: Starts / stops the submission worker (-dx12syncsubmit keeps submission inline)
//-----------------------------------------------------------------------------
void CShaderDeviceDX12::StartSubmitThread()
{
	if ( m_hSubmitThread || ( CommandLine() && CommandLine()->CheckParm( "-dx12syncsubmit" ) ) )
		return;
	m_bSubmitExit = 0;
	m_nSubmitHead = 0;
	m_nSubmitTail = 0;
	m_nSubmitError = S_OK;
	m_hSubmitThread = CreateSimpleThread( &CShaderDeviceDX12::SubmitThreadMain, this );
}

void CShaderDeviceDX12::StopSubmitThread()
{
	if ( !m_hSubmitThread )
		return;
	FlushSubmissions();
	m_bSubmitExit = 1;
	m_SubmitWorkEvent.Set();
	ThreadJoin( m_hSubmitThread );
	ReleaseThreadHandle( m_hSubmitThread );
	m_hSubmitThread = nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: Queues an operation for the worker, blocking while the ring is full
//-----------------------------------------------------------------------------
void CShaderDeviceDX12::EnqueueSubmission( const SubmitOpDX12 &op )
{
	if ( !m_hSubmitThread )
	{
		CheckDevice( "submission", RunSubmission( op ) );
		return;
	}
	const uint32_t nHead = m_nSubmitHead;
	while ( nHead - m_nSubmitTail >= ARRAYSIZE( m_SubmitOps ) )
		m_SubmitDoneEvent.Wait();
	m_SubmitOps[nHead % ARRAYSIZE( m_SubmitOps )] = op;
	m_nSubmitHead = nHead + 1;
	m_SubmitWorkEvent.Set();
}

//-----------------------------------------------------------------------------
// Purpose: Waits for queued operations and reports the first queued failure
//-----------------------------------------------------------------------------
void CShaderDeviceDX12::FlushSubmissions()
{
	if ( !m_hSubmitThread )
		return;
	if ( m_nSubmitTail != m_nSubmitHead )
	{
		ZoneNamedN( presentWait, "DX12 SubmitThreadWait", DX12_ZONES_ACTIVE );
		DrainSubmissions();
	}
	const HRESULT hr = ThreadInterlockedExchange( reinterpret_cast<int32 volatile *>( &m_nSubmitError ), S_OK );
	if ( FAILED( hr ) )
		CheckDevice( "queued submission", hr );
}

//-----------------------------------------------------------------------------
// Purpose: Submits outstanding work, waits for the GPU and releases every device object
//-----------------------------------------------------------------------------
void CShaderDeviceDX12::ShutdownDevice()
{
	// The final Submit may reset a list, but must not record new queries referencing soon-to-be-released heaps.
	m_bGpuTimingStopping = true;
	if ( g_pShaderAPIDX12 )
		g_pShaderAPIDX12->StopOcclusionQueriesForShutdown();
	StopSubmitThread();
	if ( m_bRecording && IsRecordingOwner() && m_pFence && !m_bFailed )
		Submit( true );
	if ( m_nFenceValue && !m_bFailed )
		WaitForFence( m_nFenceValue );
	m_Highres.ReleaseDevice();
	m_Lighting.Shutdown();
	if ( g_pHardwareConfigDX12 ) g_pHardwareConfigDX12->SetResourceBindingTier( 0 );
	ReleaseViews();
	// The provider outlives its chains (released above) but not the queue it may have created: drop the queue,
	// then the provider (Streamline/FFX/XeFG contexts), before the remaining device objects.
	m_PendingSelect.valid = false;
	m_pQueue.Reset();
	m_FrameGen.Shutdown();
	m_PresentFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
	m_nPresentFormat = static_cast<int>( m_PresentFormat );
	m_nLastPresentId = 0;
	m_nPresentSerial = 0;
	m_bSelectFailed = false;
	for ( int i = 0; i < ARRAYSIZE( m_pDynamicVertices ); ++i )
		if ( m_pDynamicVertices[i] )
			m_pDynamicVertices[i]->NativeResourceRef().Reset();
	for ( int i = 0; i < ARRAYSIZE( m_pDynamicIndices ); ++i )
		if ( m_pDynamicIndices[i] )
			m_pDynamicIndices[i]->NativeResourceRef().Reset();
	if ( m_hFenceEvent )
	{
		CloseHandle( m_hFenceEvent );
		m_hFenceEvent = nullptr;
	}
	if ( m_pTimestampReadback )
		m_pTimestampReadback->Unmap( 0, nullptr );
	m_pTimestampReadback.Reset();
	m_pTimestampHeap.Reset();
	m_pTimestampData = nullptr;
	if ( m_pStagePipelineData )
		m_pStagePipelineReadback->Unmap( 0, nullptr );
	m_pStagePipelineReadback.Reset();
	m_pStagePipelineHeap.Reset();
	m_pStagePipelineData = nullptr;
	m_bTimestampBegun = false;
	m_bTimestampUnavailable = false;
	m_bGpuTimingStopping = false;
	m_bShadowTimingEnabled = m_bStageQueriesAvailable = m_bStageTimestampBegun = false;
	m_nTimestampFrequency = 0;
	m_nTimestampSlot = m_nStageTimestampSlot = 0;
	m_GpuStage = GpuOther;
	m_nGpuStageDepth = m_nGpuStageOverflowDepth = 0;
	m_GpuReceiverKey = {};
	m_nGpuReceiverViewOrdinal = m_nGpuReceiverDepth = m_nGpuReceiverOverflowDepth = m_nGpuReceiverSpanDraws = 0;
	memset( m_TimestampFences, 0, sizeof( m_TimestampFences ) );
	memset( m_StageTimestampFences, 0, sizeof( m_StageTimestampFences ) );
	memset( m_flGpuStageSumMs, 0, sizeof( m_flGpuStageSumMs ) );
	memset( m_nGpuStagePSInvocations, 0, sizeof( m_nGpuStagePSInvocations ) );
	memset( m_nGpuStageCInvocations, 0, sizeof( m_nGpuStageCInvocations ) );
	memset( m_nGpuStageCPrimitives, 0, sizeof( m_nGpuStageCPrimitives ) );
	m_GpuStageStats = {};
	m_flGpuTimeSumMs = 0.0;
	m_nGpuTimePresented = 0;
	m_Recorder.Flush();
	if ( unsigned char *pChunk = m_Recorder.TakeEmptyChunk() )
	{
		AUTO_LOCK( m_ChunkMutex );
		m_FreeChunks.AddToTail( pChunk );
	}
	ReleaseCommandChunks();
	for ( int nFrame = 0; nFrame < ARRAYSIZE( m_Frames ); ++nFrame )
	{
		FrameContext &frame = m_Frames[nFrame];
		frame.list.Reset();
		for ( int i = 0; i < frame.retained.Count(); ++i )
			frame.retained[i]->Release();
		frame.retained.RemoveAll();
		frame.allocator.Reset();
		frame.fence = 0;
	}
	ReportDebugMessages();
	m_pInfoQueue.Reset();
	m_pFence.Reset();
	m_pQueue.Reset();
	m_pDevice.Reset();
	m_pFactory.Reset();
	if ( m_hSignerModule )
	{
		FreeLibrary( m_hSignerModule );
		m_hSignerModule = nullptr;
	}
	m_pfnSigner = nullptr;
	m_nFenceValue = m_nFrameSyncFence = 0;
	m_nFrameIndex = 0;
	m_bRecording = false;
	m_bFailed = false;
	m_nOwnerThread = 0;
	m_pWindow = nullptr;
	m_nWidth = m_nHeight = 0;
	if ( g_pShaderDeviceDX12 == this )
		g_pShaderDeviceDX12 = nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: Releases / recreates swap chains and view targets (queued to the owner from other threads)
//-----------------------------------------------------------------------------
void CShaderDeviceDX12::ReleaseResources()
{
	if ( !IsRecordingOwner() )
	{
		if ( g_pShaderDeviceMgrDX12 && g_pShaderDeviceMgrDX12->HostShaderUtil() )
			g_pShaderDeviceMgrDX12->HostShaderUtil()->OnThreadEvent( SHADER_THREAD_RELEASE_RESOURCES );
		else
			Warning( "ShaderAPIDX12: cannot queue resource release without host shader utility\n" );
		return;
	}
	FlushSubmissions();
	if ( m_bRecording )
		Submit( true );
	for ( View *pView : m_Views )
	{
		if ( !WaitForFence( pView->lastFence ) )
			return;
		pView->sceneColor.Reset();
		pView->sceneDepth.Reset();
		pView->ReleaseBackBuffers();
		pView->rtvHeap.Reset();
		pView->sceneRTVHeap.Reset();
		pView->sceneDSVHeap.Reset();
		if ( pView->swap && !m_bWindowed )
			pView->swap->SetFullscreenState( FALSE, nullptr );
		m_FrameGen.ReleaseSwapChain( pView->swap );
	}
}

void CShaderDeviceDX12::ReacquireResources()
{
	if ( !IsRecordingOwner() )
	{
		if ( g_pShaderDeviceMgrDX12 && g_pShaderDeviceMgrDX12->HostShaderUtil() )
			g_pShaderDeviceMgrDX12->HostShaderUtil()->OnThreadEvent( SHADER_THREAD_ACQUIRE_RESOURCES );
		else
			Warning( "ShaderAPIDX12: cannot queue resource acquisition without host shader utility\n" );
		return;
	}
	for ( View *pView : m_Views )
		if ( !pView->swap )
		{
			RECT rect{};
			GetClientRect( pView->hwnd, &rect );
			if ( !CreateView( *pView, pView->hwnd, rect.right - rect.left, rect.bottom - rect.top ) )
			{
				FailDevice( "reacquire view", E_FAIL );
				return;
			}
		}
}

//-----------------------------------------------------------------------------
// Purpose: Logs the device's maximum supported feature level
//-----------------------------------------------------------------------------
void CShaderDeviceDX12::SpewDriverInfo() const
{
	if ( !m_pDevice )
		return;
	D3D12_FEATURE_DATA_FEATURE_LEVELS levels{};
	const D3D_FEATURE_LEVEL requested[] = { D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
	levels.NumFeatureLevels = ARRAYSIZE( requested );
	levels.pFeatureLevelsRequested = requested;
	Msg( "ShaderAPIDX12: native feature level 0x%x\n", SUCCEEDED( m_pDevice->CheckFeatureSupport( D3D12_FEATURE_FEATURE_LEVELS, &levels, sizeof( levels ) ) ) ? levels.MaxSupportedFeatureLevel : D3D_FEATURE_LEVEL_11_0 );
}

//-----------------------------------------------------------------------------
// Purpose: Creates optional fence-owned profiling queries without draining submissions or waiting for the GPU.
//-----------------------------------------------------------------------------
bool CShaderDeviceDX12::EnsureGpuTiming()
{
	static const bool s_bEnabled = CommandLine() && CommandLine()->CheckParm( "-dx12stats" );
	if ( !s_bEnabled || m_bTimestampUnavailable )
		return false;
	if ( m_pTimestampHeap )
		return true;
	if ( FAILED( m_pQueue->GetTimestampFrequency( &m_nTimestampFrequency ) ) || !m_nTimestampFrequency )
	{
		m_bTimestampUnavailable = true;
		return false;
	}
	// If the extended allocation fails, retain ordinary total GPU timing and report missing stage samples.
	for ( uint32_t attempt = 0; attempt < ( m_bShadowTimingEnabled ? 2u : 1u ); ++attempt )
	{
		const bool extended = m_bShadowTimingEnabled && attempt == 0;
		const uint32_t slots = kTimestampSlots + ( extended ? kStageTimestampSlots : 0 );
		D3D12_QUERY_HEAP_DESC heapDesc{};
		heapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
		heapDesc.Count = slots * 2;
		if ( FAILED( m_pDevice->CreateQueryHeap( &heapDesc, IID_PPV_ARGS( &m_pTimestampHeap ) ) ) )
			continue;
		D3D12_HEAP_PROPERTIES properties{};
		properties.Type = D3D12_HEAP_TYPE_READBACK;
		D3D12_RESOURCE_DESC desc{};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		desc.Width = slots * 2 * sizeof( uint64_t );
		desc.Height = 1;
		desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.SampleDesc.Count = 1;
		desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		if ( FAILED( m_pDevice->CreateCommittedResource( &properties, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS( &m_pTimestampReadback ) ) ) )
		{
			m_pTimestampHeap.Reset();
			continue;
		}
		void *mapped = nullptr;
		if ( FAILED( m_pTimestampReadback->Map( 0, nullptr, &mapped ) ) )
		{
			m_pTimestampHeap.Reset();
			m_pTimestampReadback.Reset();
			continue;
		}
		m_pTimestampData = static_cast<const uint64_t *>( mapped );
		m_bStageQueriesAvailable = extended;
		if ( extended )
			EnsureGpuPipelineStats();
		return true;
	}
	m_bTimestampUnavailable = true;
	return false;
}

void CShaderDeviceDX12::EnsureGpuPipelineStats()
{
	// A separate allocation failure must not disable either timestamp ring. Each pipeline slot shares the
	// corresponding exclusive timestamp span's category and fence, so it cannot be reused before GPU completion.
	D3D12_QUERY_HEAP_DESC heapDesc{};
	heapDesc.Type = D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS;
	heapDesc.Count = kStageTimestampSlots;
	if ( FAILED( m_pDevice->CreateQueryHeap( &heapDesc, IID_PPV_ARGS( &m_pStagePipelineHeap ) ) ) )
		return;
	D3D12_HEAP_PROPERTIES properties{};
	properties.Type = D3D12_HEAP_TYPE_READBACK;
	D3D12_RESOURCE_DESC desc{};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Width = kStageTimestampSlots * sizeof( D3D12_QUERY_DATA_PIPELINE_STATISTICS );
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	if ( FAILED( m_pDevice->CreateCommittedResource( &properties, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS( &m_pStagePipelineReadback ) ) ) )
	{
		m_pStagePipelineHeap.Reset();
		return;
	}
	void *mapped = nullptr;
	if ( FAILED( m_pStagePipelineReadback->Map( 0, nullptr, &mapped ) ) )
	{
		m_pStagePipelineReadback.Reset();
		m_pStagePipelineHeap.Reset();
		return;
	}
	m_pStagePipelineData = static_cast<const D3D12_QUERY_DATA_PIPELINE_STATISTICS *>( mapped );
}

void CShaderDeviceDX12::CollectGpuTiming()
{
	if ( !m_pTimestampData )
		return;
	const uint64_t completed = CompletedFenceValue();
	const double tickMs = 1000.0 / static_cast<double>( m_nTimestampFrequency );
	for ( uint32_t slot = 0; slot < kTimestampSlots; ++slot )
		if ( m_TimestampFences[slot] && m_TimestampFences[slot] <= completed )
		{
			const uint64_t begin = m_pTimestampData[slot * 2], end = m_pTimestampData[slot * 2 + 1];
			if ( end > begin )
				m_flGpuTimeSumMs += static_cast<double>( end - begin ) * tickMs;
			m_TimestampFences[slot] = 0;
		}
	if ( !m_bStageQueriesAvailable )
		return;
	for ( uint32_t slot = 0; slot < kStageTimestampSlots; ++slot )
		if ( m_StageTimestampFences[slot] && m_StageTimestampFences[slot] <= completed )
		{
			const uint32_t query = ( kTimestampSlots + slot ) * 2;
			const uint64_t begin = m_pTimestampData[query], end = m_pTimestampData[query + 1];
			if ( begin && end >= begin )
			{
				m_flGpuStageSumMs[m_StageTimestampCategories[slot]] += static_cast<double>( end - begin ) * tickMs;
				++m_GpuStageStats.completedSpans;
				const GpuStageDX12 category = m_StageTimestampCategories[slot];
				if ( category == GpuReceiverRendering || category == GpuOther )
				{
					const GpuReceiverKeyDX12 &key = m_StageReceiverKeys[slot];
					GpuReceiverViewStatsDX12 &receiver = m_GpuStageStats.receiverViews[key.view];
					// The initial open list predates view creation; do not let that empty 0x0 span hide a real viewport.
					if ( !receiver.seen || receiver.width <= 0 || receiver.height <= 0 )
					{
						receiver.width = key.width;
						receiver.height = key.height;
						receiver.seen = true;
					}
					else if ( key.width > 0 && key.height > 0 )
						receiver.mixedViewport |= receiver.width != key.width || receiver.height != key.height;
					receiver.ms[key.lit ? 1 : 0] += static_cast<double>( end - begin ) * tickMs;
					receiver.draws[key.lit ? 1 : 0] += m_StageReceiverDraws[slot];
					if ( m_pStagePipelineData )
						receiver.psInvocations[key.lit ? 1 : 0] += static_cast<double>( m_pStagePipelineData[slot].PSInvocations );
				}
				if ( m_pStagePipelineData )
				{
					const D3D12_QUERY_DATA_PIPELINE_STATISTICS &pipeline = m_pStagePipelineData[slot];
					const GpuStageDX12 stage = m_StageTimestampCategories[slot];
					m_nGpuStagePSInvocations[stage] += pipeline.PSInvocations;
					m_nGpuStageCInvocations[stage] += pipeline.CInvocations;
					m_nGpuStageCPrimitives[stage] += pipeline.CPrimitives;
					++m_GpuStageStats.pipelineCompletedSpans;
				}
			}
			else
			{
				++m_GpuStageStats.invalidSpans;
				if ( m_pStagePipelineData )
					++m_GpuStageStats.pipelineInvalidSpans;
			}
			m_StageTimestampFences[slot] = 0;
		}
}

//-----------------------------------------------------------------------------
// Purpose: Ends both timers on this list; neither interval crosses a submission/CPU queue gap.
//-----------------------------------------------------------------------------
bool CShaderDeviceDX12::GpuTimingBeforeSubmit()
{
	if ( !EnsureGpuTiming() )
	{
		if ( m_bShadowTimingEnabled )
		{
			++m_GpuStageStats.skippedSpans;
			++m_GpuStageStats.pipelineSkippedSpans;
			++m_GpuStageStats.totalSkippedLists;
		}
		return false;
	}
	CollectGpuTiming();
	EndGpuStageSpan();
	if ( m_bTimestampBegun )
	{
		const uint32_t slot = m_nTimestampSlot;
		m_Recorder.EndQuery( m_pTimestampHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slot * 2 + 1 );
		m_Recorder.ResolveQueryData( m_pTimestampHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slot * 2, 2, m_pTimestampReadback.Get(), slot * 2 * sizeof( uint64_t ) );
		m_TimestampFences[slot] = NextFenceValue();
		m_nTimestampSlot = ( slot + 1 ) % kTimestampSlots;
		m_bTimestampBegun = false;
	}
	return m_TimestampFences[m_nTimestampSlot] == 0;
}

void CShaderDeviceDX12::GpuTimingAfterSubmit()
{
	// These are the next list's first commands. Logical nested scopes survive list submission.
	if ( !m_TimestampFences[m_nTimestampSlot] )
	{
		m_Recorder.EndQuery( m_pTimestampHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, m_nTimestampSlot * 2 );
		m_bTimestampBegun = true;
	}
	else if ( m_bShadowTimingEnabled )
		++m_GpuStageStats.totalSkippedLists;
	BeginGpuStageSpan();
}

void CShaderDeviceDX12::BeginGpuStageSpan()
{
	if ( !m_bShadowTimingEnabled )
		return;
	if ( !m_bStageQueriesAvailable || m_StageTimestampFences[m_nStageTimestampSlot] )
	{
		++m_GpuStageStats.skippedSpans;
		++m_GpuStageStats.pipelineSkippedSpans;
		if ( m_bStageQueriesAvailable )
		{
			++m_GpuStageStats.overflowSpans;
			if ( m_pStagePipelineData )
				++m_GpuStageStats.pipelineOverflowSpans;
		}
		return;
	}
	const uint32_t query = ( kTimestampSlots + m_nStageTimestampSlot ) * 2;
	m_StageTimestampCategories[m_nStageTimestampSlot] = m_GpuStage;
	m_StageReceiverKeys[m_nStageTimestampSlot] = m_GpuReceiverKey;
	if ( !m_GpuReceiverKey.view )
	{
		m_StageReceiverKeys[m_nStageTimestampSlot].width = m_pCurrentView ? m_pCurrentView->width : m_nWidth;
		m_StageReceiverKeys[m_nStageTimestampSlot].height = m_pCurrentView ? m_pCurrentView->height : m_nHeight;
	}
	m_nGpuReceiverSpanDraws = 0;
	m_Recorder.EndQuery( m_pTimestampHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, query );
	if ( m_pStagePipelineData )
		m_Recorder.BeginQuery( m_pStagePipelineHeap.Get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS, m_nStageTimestampSlot );
	else
		++m_GpuStageStats.pipelineSkippedSpans;
	m_bStageTimestampBegun = true;
}

void CShaderDeviceDX12::EndGpuStageSpan()
{
	if ( !m_bStageTimestampBegun )
		return;
	const uint32_t slot = m_nStageTimestampSlot, query = ( kTimestampSlots + slot ) * 2;
	m_StageReceiverDraws[slot] = m_nGpuReceiverSpanDraws;
	if ( m_pStagePipelineData )
	{
		m_Recorder.EndQuery( m_pStagePipelineHeap.Get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS, slot );
		m_Recorder.ResolveQueryData( m_pStagePipelineHeap.Get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS, slot, 1, m_pStagePipelineReadback.Get(), slot * sizeof( D3D12_QUERY_DATA_PIPELINE_STATISTICS ) );
	}
	m_Recorder.EndQuery( m_pTimestampHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, query + 1 );
	m_Recorder.ResolveQueryData( m_pTimestampHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, query, 2, m_pTimestampReadback.Get(), query * sizeof( uint64_t ) );
	// Even a scope transition before submission reserves its queries for that list's future fence.
	m_StageTimestampFences[slot] = NextFenceValue();
	m_nStageTimestampSlot = ( slot + 1 ) % kStageTimestampSlots;
	m_bStageTimestampBegun = false;
}

void CShaderDeviceDX12::ChangeGpuStage( GpuStageDX12 stage )
{
	if ( m_GpuStage == stage )
		return;
	EndGpuStageSpan();
	m_GpuStage = stage;
	BeginGpuStageSpan();
}

void CShaderDeviceDX12::BeginGpuStage( GpuStageDX12 stage )
{
	if ( !m_bShadowTimingEnabled )
		return;
	if ( m_nGpuStageOverflowDepth || m_nGpuStageDepth == kGpuStageStackSize )
	{
		++m_nGpuStageOverflowDepth;
		++m_GpuStageStats.scopeErrors;
		return;
	}
	m_GpuStageStack[m_nGpuStageDepth++] = stage;
	ChangeGpuStage( stage );
}

void CShaderDeviceDX12::EndGpuStage( GpuStageDX12 stage )
{
	if ( !m_bShadowTimingEnabled )
		return;
	if ( m_nGpuStageOverflowDepth )
	{
		--m_nGpuStageOverflowDepth;
		return;
	}
	if ( !m_nGpuStageDepth || m_GpuStageStack[m_nGpuStageDepth - 1] != stage )
	{
		++m_GpuStageStats.scopeErrors;
		return;
	}
	--m_nGpuStageDepth;
	ChangeGpuStage( m_nGpuStageDepth ? m_GpuStageStack[m_nGpuStageDepth - 1] : GpuOther );
}

void CShaderDeviceDX12::BeginGpuReceiverView( int width, int height )
{
	if ( !m_bShadowTimingEnabled )
		return;
	if ( m_nGpuReceiverOverflowDepth || m_nGpuReceiverDepth == kGpuStageStackSize )
	{
		++m_nGpuReceiverOverflowDepth;
		++m_GpuStageStats.receiverScopeErrors;
		BeginGpuStage( GpuReceiverRendering );
		return;
	}
	EndGpuStageSpan();
	m_GpuReceiverStack[m_nGpuReceiverDepth++] = m_GpuReceiverKey;
	if ( m_nGpuReceiverViewOrdinal < kGpuReceiverMaxViews + 1 )
		++m_nGpuReceiverViewOrdinal;
	if ( m_nGpuReceiverViewOrdinal > kGpuReceiverMaxViews )
		++m_GpuStageStats.receiverViewOverflow;
	m_GpuReceiverKey = {};
	m_GpuReceiverKey.view = m_nGpuReceiverViewOrdinal;
	m_GpuReceiverKey.width = width;
	m_GpuReceiverKey.height = height;
	const bool sameStage = m_GpuStage == GpuReceiverRendering;
	BeginGpuStage( GpuReceiverRendering );
	if ( sameStage )
		BeginGpuStageSpan();
}

void CShaderDeviceDX12::EndGpuReceiverView()
{
	if ( !m_bShadowTimingEnabled )
		return;
	if ( m_nGpuReceiverOverflowDepth )
	{
		--m_nGpuReceiverOverflowDepth;
		EndGpuStage( GpuReceiverRendering );
		return;
	}
	if ( !m_nGpuReceiverDepth )
	{
		++m_GpuStageStats.receiverScopeErrors;
		return;
	}
	EndGpuStageSpan();
	m_GpuReceiverKey = m_GpuReceiverStack[--m_nGpuReceiverDepth];
	const bool sameStage = m_nGpuStageDepth > 1 && m_GpuStageStack[m_nGpuStageDepth - 2] == GpuReceiverRendering;
	EndGpuStage( GpuReceiverRendering );
	if ( sameStage )
		BeginGpuStageSpan();
}

void CShaderDeviceDX12::GpuReceiverDraw( bool lit )
{
	if ( !m_bShadowTimingEnabled || ( m_GpuStage != GpuReceiverRendering && m_GpuStage != GpuOther ) )
		return;
	// Consecutive draws with the same view/class share queries; classify only successfully emitted draws.
	if ( m_GpuReceiverKey.lit != lit )
	{
		EndGpuStageSpan();
		m_GpuReceiverKey.lit = lit;
		BeginGpuStageSpan();
	}
	if ( m_bStageTimestampBegun )
		++m_nGpuReceiverSpanDraws;
	else
		++m_GpuStageStats.receiverSkippedDraws;
}

bool CShaderDeviceDX12::ConsumeGpuStageStats( GpuStageStatsDX12 &stats )
{
	if ( !m_bShadowTimingEnabled )
		return false;
	stats = m_GpuStageStats;
	stats.available = m_bStageQueriesAvailable;
	stats.pipelineAvailable = m_pStagePipelineData != nullptr;
	for ( GpuReceiverViewStatsDX12 &receiver : stats.receiverViews )
		for ( uint32_t drawClass = 0; drawClass < 2; ++drawClass )
		{
			receiver.ms[drawClass] = stats.presentedFrames ? receiver.ms[drawClass] / stats.presentedFrames : 0.0;
			receiver.psInvocations[drawClass] = stats.presentedFrames ? receiver.psInvocations[drawClass] / stats.presentedFrames : 0.0;
			receiver.draws[drawClass] = stats.presentedFrames ? receiver.draws[drawClass] / stats.presentedFrames : 0.0;
		}
	for ( uint32_t stage = 0; stage < GpuStageCount; ++stage )
	{
		stats.msPerPresentedFrame[stage] = stats.presentedFrames ? m_flGpuStageSumMs[stage] / stats.presentedFrames : 0.0;
		stats.psInvocationsPerPresentedFrame[stage] = stats.presentedFrames ? static_cast<double>( m_nGpuStagePSInvocations[stage] ) / stats.presentedFrames : 0.0;
		stats.cInvocationsPerPresentedFrame[stage] = stats.presentedFrames ? static_cast<double>( m_nGpuStageCInvocations[stage] ) / stats.presentedFrames : 0.0;
		stats.cPrimitivesPerPresentedFrame[stage] = stats.presentedFrames ? static_cast<double>( m_nGpuStageCPrimitives[stage] ) / stats.presentedFrames : 0.0;
	}
	for ( uint32_t slot = 0; slot < kStageTimestampSlots; ++slot )
		if ( m_StageTimestampFences[slot] )
			++stats.pendingSpans;
	if ( m_bStageTimestampBegun )
		++stats.pendingSpans;
	memset( m_flGpuStageSumMs, 0, sizeof( m_flGpuStageSumMs ) );
	memset( m_nGpuStagePSInvocations, 0, sizeof( m_nGpuStagePSInvocations ) );
	memset( m_nGpuStageCInvocations, 0, sizeof( m_nGpuStageCInvocations ) );
	memset( m_nGpuStageCPrimitives, 0, sizeof( m_nGpuStageCPrimitives ) );
	m_GpuStageStats = {};
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Returns and resets the average GPU frame time accumulated since the last call
//-----------------------------------------------------------------------------
bool CShaderDeviceDX12::ConsumeGpuTime( double &flAverageMs, uint32_t &nFrames )
{
	// Optional stage windows collect here too, so a short capture need not wait for another submit.
	if ( m_bShadowTimingEnabled )
		CollectGpuTiming();
	nFrames = m_nGpuTimePresented;
	flAverageMs = nFrames ? m_flGpuTimeSumMs / nFrames : 0.0;
	m_flGpuTimeSumMs = 0.0;
	m_nGpuTimePresented = 0;
	return nFrames != 0;
}

//-----------------------------------------------------------------------------
// Purpose: Copies/resolves (or gamma-corrects) scene colour to the back buffer, submits and presents
//-----------------------------------------------------------------------------
void CShaderDeviceDX12::Present()
{
	ZoneNamedN( ___tracy_scoped_zone, "DX12 Present", DX12_ZONES_ACTIVE );
	if ( !IsRecordingOwner() || !m_pCurrentView || !CommandList() )
		return;
	m_Lighting.Reclaim();
	if ( m_Lighting.PresentationBlocked() )
	{
		Submit( false );
		return;
	}
	FlushSubmissions();
	View &view = *m_pCurrentView;
	RECT rect{};
	GetClientRect( view.hwnd, &rect );
	const int nWidth = rect.right - rect.left, nHeight = rect.bottom - rect.top;
	if ( nWidth <= 0 || nHeight <= 0 || IsIconic( view.hwnd ) )
	{
		view.suspended = true;
		Submit( false );
		ApplyFrameGenSelect();
		return;
	}
	if ( !ResizeView( view, nWidth, nHeight ) )
		return;
	m_nWidth = view.width;
	m_nHeight = view.height;
	if ( !view.sceneColor || !view.swap )
		return;
	if ( view.occluded )
	{
		const HRESULT hrTest = view.swap->Present( 0, DXGI_PRESENT_TEST );
		if ( hrTest == DXGI_STATUS_OCCLUDED )
		{
			Submit( false );
			ApplyFrameGenSelect();
			return;
		}
		if ( !CheckDevice( "occlusion test", hrTest ) )
			return;
		view.occluded = false;
	}
	// Frame generation (F10): the provider's per-present calls run here with the worker drained. A frame without a
	// dispatch presents in pass-through. Every provider reads scene depth in DEPTH_WRITE at present time.
	const bool bFrameGenActive = m_FrameGen.Kind() != FrameGenKindDX12::None && g_pShaderAPIDX12 && g_pShaderAPIDX12->FrameGenDispatchedThisFrame();
	const uint32_t nFrameId = bFrameGenActive ? g_pShaderAPIDX12->FrameGenFrameId() : m_nLastPresentId + 1;
	++m_nPresentSerial;
	if ( m_FrameGen.Kind() != FrameGenKindDX12::None )
	{
		m_FrameGen.PrepareFrame( bFrameGenActive, nFrameId, m_nPresentSerial, view.width, view.height, view.swap.Get() );
		if ( bFrameGenActive )
			TransitionSceneDepth( D3D12_RESOURCE_STATE_DEPTH_WRITE );
	}
	// Display HDR toggles arrive without a window message, so the output's colour space is polled every 60 presents.
	static uint32_t s_nHdrQueryFrame = 0;
	if ( ++s_nHdrQueryFrame >= 60 )
	{
		s_nHdrQueryFrame = 0;
		QueryDisplayHdr( view );
	}
	float flGamma[4];
	// The scene is drawn into the back buffer whenever it needs a gamma ramp, the HDR output scale or an 8-bit (sRGB)
	// encode; otherwise it is resolved or copied. MSAA never meets an 8-bit chain: frame generation (the only 8-bit
	// user) rejects MSAA.
	const float flPresentScale = g_pShaderAPIDX12 ? g_pShaderAPIDX12->PresentOutputScale() : 1.f;
	const bool bEncode = PresentGammaCoefficients( flGamma ) || flPresentScale != 1.f || m_PresentFormat != SceneColorFormat();
	ID3D12Resource *pBack = CurrentBackBuffer();
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = pBack;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	barrier.Transition.StateAfter = bEncode ? D3D12_RESOURCE_STATE_RENDER_TARGET : ( m_nSampleCount > 1 ? D3D12_RESOURCE_STATE_RESOLVE_DEST : D3D12_RESOURCE_STATE_COPY_DEST );
	m_Recorder.ResourceBarrier( 1, &barrier );
	if ( bEncode )
	{
		D3D12_RESOURCE_STATES backState = D3D12_RESOURCE_STATE_RENDER_TARGET;
		if ( !g_pShaderAPIDX12 || !g_pShaderAPIDX12->EncodeSceneTo( pBack, backState ) || backState != D3D12_RESOURCE_STATE_RENDER_TARGET )
		{
			FailDevice( "presentation encode", E_FAIL );
			return;
		}
	}
	else if ( m_nSampleCount > 1 )
	{
		TransitionSceneColor( D3D12_RESOURCE_STATE_RESOLVE_SOURCE );
		m_Recorder.ResolveSubresource( pBack, 0, view.sceneColor.Get(), 0, SceneColorFormat() );
	}
	else
	{
		TransitionSceneColor( D3D12_RESOURCE_STATE_COPY_SOURCE );
		m_Recorder.CopyResource( pBack, view.sceneColor.Get() );
	}
	V_swap( barrier.Transition.StateBefore, barrier.Transition.StateAfter );
	m_Recorder.ResourceBarrier( 1, &barrier );
	TransitionSceneColor( D3D12_RESOURCE_STATE_RENDER_TARGET );
	if ( !Submit( false ) )
		return;
	++m_nGpuTimePresented;
	if ( m_bShadowTimingEnabled )
	{
		++m_GpuStageStats.presentedFrames;
		m_nGpuReceiverViewOrdinal = 0;
	}
	const UINT nInterval = m_bWaitForVsync ? 1 : 0;
	const UINT nFlags = !m_bWaitForVsync && m_bAllowTearing && m_bWindowed ? DXGI_PRESENT_ALLOW_TEARING : 0;
	if ( m_hSubmitThread )
	{
		// Present runs on the submission worker after this frame's ExecuteCommandLists. Every later
		// swap-chain or queue access on this thread calls FlushPresent/FlushSubmissions first.
		SubmitOpDX12 op{};
		op.kind = SubmitOpDX12::Present;
		op.swap = view.swap.Get();
		op.view = &view;
		op.interval = nInterval;
		op.flags = nFlags;
		op.frameId = nFrameId;
		op.framegenActive = bFrameGenActive;
		EnqueueSubmission( op );
	}
	else
	{
		HRESULT hr;
		{
			ZoneNamedN( nativePresent, "DX12 DXGI Present", DX12_ZONES_ACTIVE );
			m_FrameGen.BeforePresent( nFrameId, bFrameGenActive );
			hr = view.swap->Present( nInterval, nFlags );
			m_FrameGen.AfterPresent( nFrameId );
		}
		if ( hr == DXGI_STATUS_OCCLUDED )
			view.occluded = true;
		else
			CheckDevice( "Present", hr );
	}
	m_nLastPresentId = nFrameId;
	// A kind switch requested this frame applies here: the frame's image is consumed and the recorder is empty (F5).
	ApplyFrameGenSelect();
#ifdef TRACY_ENABLE
	if ( TracyIsStarted )
	{
		FrameMark;
	}
#endif
	RefreshTracyZonesDX12();
}

//-----------------------------------------------------------------------------
// Purpose: Stores the gamma settings; exclusive fullscreen also programs the DXGI output ramp
//-----------------------------------------------------------------------------
void CShaderDeviceDX12::SetHardwareGammaRamp( float fGamma, float fGammaTVRangeMin, float fGammaTVRangeMax, float fGammaTVExponent, bool bTVEnabled )
{
	m_flGamma = fGamma;
	m_flGammaMin = fGammaTVRangeMin;
	m_flGammaMax = fGammaTVRangeMax;
	m_flGammaExponent = fGammaTVExponent;
	m_bGammaTV = bTVEnabled;
	// DXGI output gamma is defined only for exclusive fullscreen presentation.
	FlushSubmissions();
	if ( m_bWindowed || !m_pCurrentView || !m_pCurrentView->swap )
		return;
	Microsoft::WRL::ComPtr<IDXGIOutput> output;
	HRESULT hr = m_pCurrentView->swap->GetContainingOutput( &output );
	DXGI_GAMMA_CONTROL_CAPABILITIES caps{};
	if ( SUCCEEDED( hr ) )
		hr = output->GetGammaControlCapabilities( &caps );
	if ( FAILED( hr ) )
	{
		Warning( "ShaderAPIDX12: gamma capabilities failed (0x%08x)\n", static_cast<unsigned>( hr ) );
		return;
	}
	DXGI_GAMMA_CONTROL ramp{};
	ramp.Scale = { 1, 1, 1 };
	for ( UINT i = 0; i < caps.NumGammaControlPoints; ++i )
	{
		float flCorrection = powf( clamp( caps.ControlPointPositions[i], 0.0f, 1.0f ), fGamma / 2.2f );
		if ( bTVEnabled )
		{
			flCorrection = powf( flCorrection, 2.2f / fGammaTVExponent );
			flCorrection = flCorrection * ( fGammaTVRangeMax - fGammaTVRangeMin ) / 255.0f + fGammaTVRangeMin / 255.0f;
		}
		flCorrection = clamp( flCorrection, caps.MinConvertedValue, caps.MaxConvertedValue );
		ramp.GammaCurve[i] = { flCorrection, flCorrection, flCorrection };
	}
	hr = output->SetGammaControl( &ramp );
	if ( FAILED( hr ) )
		Warning( "ShaderAPIDX12: SetGammaControl failed (0x%08x)\n", static_cast<unsigned>( hr ) );
}

//-----------------------------------------------------------------------------
// Purpose: View management; the first view becomes current
//-----------------------------------------------------------------------------
bool CShaderDeviceDX12::AddView( void *hWnd )
{
	if ( !IsRecordingOwner() || !hWnd || !IsWindow( static_cast<HWND>( hWnd ) ) )
		return false;
	FlushSubmissions();
	for ( const View *pView : m_Views )
		if ( pView->hwnd == hWnd )
			return true;
	RECT rect{};
	GetClientRect( static_cast<HWND>( hWnd ), &rect );
	View *pView = new View;
	if ( !CreateView( *pView, static_cast<HWND>( hWnd ), rect.right - rect.left, rect.bottom - rect.top ) )
	{
		delete pView;
		return false;
	}
	if ( !m_pCurrentView )
	{
		m_pCurrentView = pView;
		m_nWidth = pView->width;
		m_nHeight = pView->height;
	}
	m_Views.AddToTail( pView );
	return true;
}

void CShaderDeviceDX12::RemoveView( void *hWnd )
{
	if ( !IsRecordingOwner() )
		return;
	FlushSubmissions();
	for ( int i = 0; i < m_Views.Count(); ++i )
		if ( m_Views[i]->hwnd == hWnd )
		{
			View *pView = m_Views[i];
			if ( m_bRecording )
				Submit( false );
			if ( !WaitForFence( pView->lastFence ) )
				return;
			if ( pView->swap && !m_bWindowed )
				pView->swap->SetFullscreenState( FALSE, nullptr );
			if ( m_pCurrentView == pView )
				m_pCurrentView = nullptr;
			delete pView;
			m_Views.Remove( i );
			if ( !m_pCurrentView && !m_Views.IsEmpty() )
				m_pCurrentView = m_Views[0];
			m_nWidth = m_pCurrentView ? m_pCurrentView->width : 0;
			m_nHeight = m_pCurrentView ? m_pCurrentView->height : 0;
			return;
		}
}

void CShaderDeviceDX12::SetView( void *hWnd )
{
	if ( !IsRecordingOwner() )
		return;
	FlushSubmissions();
	if ( !hWnd )
		hWnd = m_pWindow;
	bool bFound = false;
	for ( const View *pView : m_Views )
		if ( pView->hwnd == hWnd )
		{
			bFound = true;
			break;
		}
	if ( !bFound && hWnd && IsWindow( static_cast<HWND>( hWnd ) ) )
	{
		if ( !AddView( hWnd ) )
			return;
		bFound = true;
	}
	if ( !bFound )
	{
		Warning( "ShaderAPIDX12: SetView requires a valid HWND\n" );
		return;
	}
	for ( View *pView : m_Views )
		if ( pView->hwnd == hWnd )
		{
			if ( pView != m_pCurrentView && m_bRecording && !Submit( false ) )
				return;
			m_pCurrentView = pView;
			m_nWidth = pView->width;
			m_nHeight = pView->height;
			return;
		}
}

//-----------------------------------------------------------------------------
// Purpose: Claims / releases the single recording-owner thread
//-----------------------------------------------------------------------------
bool CShaderDeviceDX12::AcquireRecordingOwnership()
{
	const unsigned nThread = GetCurrentThreadId();
	// A failed claim can only observe our own id if this thread already owns recording.
	if ( m_nOwnerThread.AssignIf( 0, nThread ) || m_nOwnerThread == nThread )
		return true;
	Warning( "ShaderAPIDX12: recording owner still active on another thread\n" );
	return false;
}

void CShaderDeviceDX12::ReleaseRecordingOwnership()
{
	if ( !IsRecordingOwner() )
	{
		Warning( "ShaderAPIDX12: release recording owner from wrong thread\n" );
		return;
	}
	FlushSubmissions();
	if ( m_bRecording )
		Submit( true );
	m_nOwnerThread = 0;
}

//-----------------------------------------------------------------------------
// Purpose: Wraps DXBC directly; compiles HLSL source with D3DCompile
//-----------------------------------------------------------------------------
IShaderBuffer *CShaderDeviceDX12::CompileShader( const char *pProgram, size_t nBufLen, const char *pShaderVersion )
{
	if ( !pProgram || !nBufLen )
		return nullptr;
	if ( nBufLen >= 4 && memcmp( pProgram, "DXBC", 4 ) == 0 )
		return new CShaderBufferDX12( pProgram, nBufLen );
	if ( !pShaderVersion || !*pShaderVersion )
		return nullptr;
	Microsoft::WRL::ComPtr<ID3DBlob> code, errors;
	const bool bLegacy = V_strlen( pShaderVersion ) > 3 && pShaderVersion[3] < '4';
	const UINT nFlags = ( bLegacy ? D3DCOMPILE_ENABLE_BACKWARDS_COMPATIBILITY : D3DCOMPILE_ENABLE_STRICTNESS ) | ( ( g_pHardwareConfigDX12 && g_pHardwareConfigDX12->DisableShaderOptimizations() ) ? D3DCOMPILE_SKIP_OPTIMIZATION : 0 );
	HRESULT hr = D3DCompile( pProgram, nBufLen, "shaderapidx12", nullptr, nullptr, "main", pShaderVersion,
	    nFlags, 0, &code, &errors );
	if ( FAILED( hr ) )
	{
		if ( errors )
			Warning( "ShaderAPIDX12: shader compile failed: %s\n", static_cast<const char *>( errors->GetBufferPointer() ) );
		else
			Warning( "ShaderAPIDX12: shader compile failed (0x%08x)\n", static_cast<unsigned>( hr ) );
		return nullptr;
	}
	return new CShaderBufferDX12( code->GetBufferPointer(), code->GetBufferSize() );
}

//-----------------------------------------------------------------------------
// Purpose: Allocates a shader record holding a copy of the DXBC or legacy (VCS) bytecode
//-----------------------------------------------------------------------------
static ShaderRecordDX12 *CreateShaderRecord( IShaderBuffer *pShaderBuffer, bool bPixel )
{
	if ( !pShaderBuffer || !pShaderBuffer->GetBits() || pShaderBuffer->GetSize() < 4 )
		return nullptr;
	const unsigned char *pBits = static_cast<const unsigned char *>( pShaderBuffer->GetBits() );
	const int nBytes = static_cast<int>( pShaderBuffer->GetSize() );
	static CInterlockedIntT<uint64> s_nNextIdentity( 1 );
	ShaderRecordDX12 *pRecord = new ShaderRecordDX12;
	pRecord->identity = s_nNextIdentity.AtomicAdd( 1 );
	pRecord->stagePixel = bPixel;
	if ( memcmp( pBits, "DXBC", 4 ) == 0 )
		pRecord->bytecode.CopyArray( pBits, nBytes );
	else
		pRecord->legacyBytecode.CopyArray( pBits, nBytes );
	if ( pRecord->legacyBytecode.IsEmpty() )
	{
		CUtlString error;
		if ( !ValidateLightingShaderDX12( pRecord->Bytecode(), bPixel, &pRecord->lightingAbi, error, &pRecord->sunVisibilityAbi, &pRecord->propVisibilityAbi ) )
		{
			Warning( "Shadowmaps: required native shader unavailable: %s\n", error.Get() );
			delete pRecord;
			return nullptr;
		}
	}
	return pRecord;
}

//-----------------------------------------------------------------------------
// Purpose: Shader handles are ShaderRecordDX12 pointers; destruction retires dependent pipelines first
//-----------------------------------------------------------------------------
VertexShaderHandle_t CShaderDeviceDX12::CreateVertexShader( IShaderBuffer *pShaderBuffer )
{
	return reinterpret_cast<VertexShaderHandle_t>( CreateShaderRecord( pShaderBuffer, false ) );
}

void CShaderDeviceDX12::DestroyVertexShader( VertexShaderHandle_t hShader )
{
	ShaderRecordDX12 *pRecord = reinterpret_cast<ShaderRecordDX12 *>( hShader );
	if ( g_pShaderAPIDX12 )
		g_pShaderAPIDX12->RetireShaderPipelines( pRecord );
	delete pRecord;
}

GeometryShaderHandle_t CShaderDeviceDX12::CreateGeometryShader( IShaderBuffer *pShaderBuffer )
{
	ShaderRecordDX12 *pRecord = CreateShaderRecord( pShaderBuffer, false );
	if ( pRecord )
		pRecord->stageGeometry = true;
	return reinterpret_cast<GeometryShaderHandle_t>( pRecord );
}

void CShaderDeviceDX12::DestroyGeometryShader( GeometryShaderHandle_t hShader )
{
	ShaderRecordDX12 *pRecord = reinterpret_cast<ShaderRecordDX12 *>( hShader );
	if ( g_pShaderAPIDX12 )
		g_pShaderAPIDX12->RetireShaderPipelines( pRecord );
	delete pRecord;
}

PixelShaderHandle_t CShaderDeviceDX12::CreatePixelShader( IShaderBuffer *pShaderBuffer )
{
	return reinterpret_cast<PixelShaderHandle_t>( CreateShaderRecord( pShaderBuffer, true ) );
}

void CShaderDeviceDX12::DestroyPixelShader( PixelShaderHandle_t hShader )
{
	ShaderRecordDX12 *pRecord = reinterpret_cast<ShaderRecordDX12 *>( hShader );
	if ( g_pShaderAPIDX12 )
		g_pShaderAPIDX12->RetireShaderPipelines( pRecord );
	delete pRecord;
}

//-----------------------------------------------------------------------------
// Purpose: Draw callback of static meshes; renders through the shader API's material path
//-----------------------------------------------------------------------------
static void DrawStaticMesh( void *, CMeshDX12 *pMesh, int nFirst, int nCount )
{
	if ( g_pShaderAPIDX12 )
		g_pShaderAPIDX12->DrawMaterialMesh( pMesh, nFirst, nCount );
}

//-----------------------------------------------------------------------------
// Mesh and buffer creation
//-----------------------------------------------------------------------------
IMesh *CShaderDeviceDX12::CreateStaticMesh( VertexFormat_t vertexFormat, const char *, IMaterial * )
{
	return new CMeshDX12( vertexFormat, 0, false, DrawStaticMesh, nullptr );
}

void CShaderDeviceDX12::DestroyStaticMesh( IMesh *pMesh )
{
	delete pMesh;
}

IVertexBuffer *CShaderDeviceDX12::CreateVertexBuffer( ShaderBufferType_t type, VertexFormat_t fmt, int nVertexCount, const char * )
{
	return new CVertexBufferDX12( fmt, nVertexCount, IsDynamicBufferType( type ) );
}

void CShaderDeviceDX12::DestroyVertexBuffer( IVertexBuffer *pBuffer )
{
	delete pBuffer;
}

IIndexBuffer *CShaderDeviceDX12::CreateIndexBuffer( ShaderBufferType_t type, MaterialIndexFormat_t fmt, int nIndexCount, const char * )
{
	return new CIndexBufferDX12( fmt, nIndexCount, IsDynamicBufferType( type ) );
}

void CShaderDeviceDX12::DestroyIndexBuffer( IIndexBuffer *pBuffer )
{
	delete pBuffer;
}

//-----------------------------------------------------------------------------
// Purpose: Shared dynamic buffers, created on first use per stream/format and buffering mode
//-----------------------------------------------------------------------------
IVertexBuffer *CShaderDeviceDX12::GetDynamicVertexBuffer( int nStreamID, VertexFormat_t vertexFormat, bool bBuffered )
{
	if ( nStreamID < 0 || nStreamID >= 32 )
		return nullptr;
	CVertexBufferDX12 *&pBuffer = m_pDynamicVertices[nStreamID * 2 + ( bBuffered ? 1 : 0 )];
	if ( !pBuffer )
		pBuffer = new CVertexBufferDX12( vertexFormat, 65536, true );
	else if ( pBuffer->GetVertexFormat() != vertexFormat )
		pBuffer->BeginCastBuffer( vertexFormat );
	return pBuffer;
}

IIndexBuffer *CShaderDeviceDX12::GetDynamicIndexBuffer( MaterialIndexFormat_t fmt, bool bBuffered )
{
	if ( fmt != MATERIAL_INDEX_FORMAT_16BIT && fmt != MATERIAL_INDEX_FORMAT_32BIT )
		return nullptr;
	CIndexBufferDX12 *&pBuffer = m_pDynamicIndices[( fmt == MATERIAL_INDEX_FORMAT_32BIT ? 2 : 0 ) + ( bBuffered ? 1 : 0 )];
	if ( !pBuffer )
		pBuffer = new CIndexBufferDX12( fmt, 65536, true );
	return pBuffer;
}

// These callbacks tick a console front buffer during loading; Windows has no such path.
void CShaderDeviceDX12::EnableNonInteractiveMode( MaterialNonInteractiveMode_t, ShaderNonInteractiveInfo_t * ) {}

void CShaderDeviceDX12::RefreshFrontBufferNonInteractive() {}

//-----------------------------------------------------------------------------
// Purpose: Runs a host-queued device event on the recording owner
//-----------------------------------------------------------------------------
void CShaderDeviceDX12::HandleThreadEvent( uint32 threadEvent )
{
	if ( !IsRecordingOwner() )
	{
		Warning( "ShaderAPIDX12: queued device event executed on non-owner thread\n" );
		return;
	}
	switch ( threadEvent )
	{
	case SHADER_THREAD_RELEASE_RESOURCES:
	case SHADER_THREAD_OTHER_APP_START:
		ReleaseResources();
		break;
	case SHADER_THREAD_ACQUIRE_RESOURCES:
	case SHADER_THREAD_OTHER_APP_END:
		ReacquireResources();
		break;
	case SHADER_THREAD_EVICT_RESOURCES:
		if ( g_pShaderAPIDX12 )
			g_pShaderAPIDX12->EvictManagedResources();
		break;
	case SHADER_THREAD_RESET_RENDER_STATE:
		if ( g_pShaderAPIDX12 )
			g_pShaderAPIDX12->ResetRenderState();
		break;
	case SHADER_THREAD_DEVICE_LOST:
		FailDevice( "host device-lost event", m_pDevice ? m_pDevice->GetDeviceRemovedReason() : DXGI_ERROR_DEVICE_REMOVED );
		break;
	default:
		Warning( "ShaderAPIDX12: unknown thread event %u\n", threadEvent );
		break;
	}
}

void CShaderDeviceDX12::GetWindowSize( int &nWidth, int &nHeight ) const
{
	RECT rect{};
	if ( m_pCurrentView )
		GetClientRect( m_pCurrentView->hwnd, &rect );
	nWidth = rect.right - rect.left;
	nHeight = rect.bottom - rect.top;
}

//-----------------------------------------------------------------------------
// Purpose: UTF-8 name of the output showing the current view (or of the adapter's first output)
//-----------------------------------------------------------------------------
char *CShaderDeviceDX12::GetDisplayDeviceName()
{
	Microsoft::WRL::ComPtr<IDXGIOutput> output;
	if ( m_pCurrentView && m_pCurrentView->swap )
		m_pCurrentView->swap->GetContainingOutput( &output );
	if ( !output && g_pShaderDeviceMgrDX12 )
	{
		const int nIndex = m_nAdapterIndex >= 0 ? m_nAdapterIndex : 0;
		if ( nIndex < g_pShaderDeviceMgrDX12->Adapters().Count() )
			g_pShaderDeviceMgrDX12->Adapters()[nIndex]->EnumOutputs( 0, &output );
	}
	DXGI_OUTPUT_DESC desc{};
	m_szDisplayDeviceName[0] = 0;
	if ( output && SUCCEEDED( output->GetDesc( &desc ) ) )
		WideCharToMultiByte( CP_UTF8, 0, desc.DeviceName, -1, m_szDisplayDeviceName, sizeof( m_szDisplayDeviceName ), nullptr, nullptr );
	return m_szDisplayDeviceName;
}

//-----------------------------------------------------------------------------
// Purpose: Releases the reference each adapter entry holds and empties the list
//-----------------------------------------------------------------------------
static void ReleaseAdapters( CUtlVector<IDXGIAdapter1 *> &adapters )
{
	for ( IDXGIAdapter1 *pAdapter : adapters )
		pAdapter->Release();
	adapters.RemoveAll();
}

CShaderDeviceMgrDX12::CShaderDeviceMgrDX12() = default;

CShaderDeviceMgrDX12::~CShaderDeviceMgrDX12()
{
	Shutdown();
}

// Keep native HDR selection enabled before any map load. Source still chooses
// LDR automatically when the map has no HDR lighting; this does not force HDR
// resources onto an LDR-only map or enable HDR display output.
static void EnforceNativeLightingTier( IConVar *variable, const char *, float )
{
	if ( !V_strcmp( variable->GetName(), "mat_hdr_level" ) )
	{
		ConVarRef hdrLevel( variable );
		if ( hdrLevel.GetInt() < 2 )
			hdrLevel.SetValue( 2 );
	}
	else if ( !V_strcmp( variable->GetName(), "mat_dxlevel" ) )
	{
		ConVarRef dxLevel( variable );
		if ( dxLevel.GetInt() != kSourceDXLevel )
			dxLevel.SetValue( kSourceDXLevel );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Resolves the host filesystem/shader utility and connects tier1/tier2
//-----------------------------------------------------------------------------
bool CShaderDeviceMgrDX12::Connect( CreateInterfaceFn factory )
{
	if ( !factory )
		return false;
	m_pfnHostFactory = factory;
	m_pFilesystem = static_cast<IFileSystem *>( factory( FILESYSTEM_INTERFACE_VERSION, nullptr ) );
	m_pShaderUtil = static_cast<IShaderUtil *>( factory( SHADER_UTIL_INTERFACE_VERSION, nullptr ) );
	if ( !m_pFilesystem || !m_pShaderUtil )
	{
		Warning( "ShaderAPIDX12: required host filesystem/shader utility interface missing\n" );
		m_pfnHostFactory = nullptr;
		m_pFilesystem = nullptr;
		m_pShaderUtil = nullptr;
		return false;
	}
	ConnectTier1Libraries( &factory, 1 );
	ConnectTier2Libraries( &factory, 1 );
	// Registers this module's development commands (shader_precache) with the host cvar system. FCVAR_CHEAT gates
	// them behind sv_cheats; FCVAR_DEVELOPMENTONLY would make retail engines reject them outright (cmd.cpp:1036).
	if ( g_pCVar )
	{
		ConVar_Register( FCVAR_CHEAT );
		g_pCVar->InstallGlobalChangeCallback( EnforceNativeLightingTier );
		ConVarRef hdrLevel( "mat_hdr_level", true );
		if ( hdrLevel.IsValid() && hdrLevel.GetInt() < 2 )
			hdrLevel.SetValue( 2 );
		ConVarRef dxLevel( "mat_dxlevel", true );
		if ( dxLevel.IsValid() && dxLevel.GetInt() != kSourceDXLevel )
			dxLevel.SetValue( kSourceDXLevel );
	}
	MathLib_Init( 2.2f, 2.2f, 0.0f, 2 );
	m_DxSupport.Load( m_pFilesystem ); // Malformed profiles log and leave hardware-derived caps intact.
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Shuts down and disconnects the tier libraries connected in Connect
//-----------------------------------------------------------------------------
void CShaderDeviceMgrDX12::Disconnect()
{
	Shutdown();
	m_DxSupport.Clear();
	if ( m_pfnHostFactory )
	{
		if ( g_pCVar )
		{
			g_pCVar->RemoveGlobalChangeCallback( EnforceNativeLightingTier );
			ConVar_Unregister();
		}
		DisconnectTier2Libraries();
		DisconnectTier1Libraries();
	}
	m_pFilesystem = nullptr;
	m_pShaderUtil = nullptr;
	m_pfnHostFactory = nullptr;
}

void *CShaderDeviceMgrDX12::QueryInterface( const char *pszName )
{
	return pszName ? Sys_GetFactoryThis()( pszName, nullptr ) : nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: Enumerates hardware adapters (plus WARP with -dx12warp), their caps and display modes
//-----------------------------------------------------------------------------
InitReturnVal_t CShaderDeviceMgrDX12::Init()
{
	ReleaseAdapters( m_pAdapters );
	m_AdapterInfo.RemoveAll();
	m_AdapterCaps.RemoveAll();
	m_AdapterModes.RemoveAll();
	const bool bAllowWarp = CommandLine() && CommandLine()->CheckParm( "-dx12warp" );
	Microsoft::WRL::ComPtr<IDXGIFactory6> factory;
	HRESULT hr = CreateDXGIFactory2( 0, IID_PPV_ARGS( &factory ) );
	if ( FAILED( hr ) )
	{
		Warning( "ShaderAPIDX12: adapter factory failed (0x%08x)\n", static_cast<unsigned>( hr ) );
		return INIT_FAILED;
	}
	for ( UINT i = 0;; ++i )
	{
		Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
		const HRESULT hrEnum = factory->EnumAdapters1( i, &adapter );
		if ( hrEnum == DXGI_ERROR_NOT_FOUND )
			break;
		if ( FAILED( hrEnum ) )
		{
			Warning( "ShaderAPIDX12: adapter enumeration failed (0x%08x)\n", static_cast<unsigned>( hrEnum ) );
			return INIT_FAILED;
		}
		DXGI_ADAPTER_DESC1 desc{};
		if ( FAILED( adapter->GetDesc1( &desc ) ) || ( desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE ) )
			continue;
		m_pAdapters.AddToTail( adapter.Detach() );
		MaterialAdapterInfo_t info{};
		WideCharToMultiByte( CP_UTF8, 0, desc.Description, -1, info.m_pDriverName, sizeof( info.m_pDriverName ), nullptr, nullptr );
		info.m_VendorID = desc.VendorId;
		info.m_DeviceID = desc.DeviceId;
		info.m_SubSysID = desc.SubSysId;
		info.m_Revision = desc.Revision;
		DXSupportCapsDX12 caps{};
		caps.vendor = desc.VendorId;
		caps.device = desc.DeviceId;
		caps.memory = desc.DedicatedVideoMemory;
		info.m_nDXSupportLevel = kSourceDXLevel;
		info.m_nMaxDXSupportLevel = kSourceDXLevel;
		m_AdapterInfo.AddToTail( info );
		m_AdapterCaps.AddToTail( caps );
	}
	if ( bAllowWarp )
	{
		Microsoft::WRL::ComPtr<IDXGIAdapter> warpBase;
		if ( SUCCEEDED( factory->EnumWarpAdapter( IID_PPV_ARGS( &warpBase ) ) ) )
		{
			Microsoft::WRL::ComPtr<IDXGIAdapter1> warp;
			if ( SUCCEEDED( warpBase.As( &warp ) ) )
			{
				DXGI_ADAPTER_DESC1 desc{};
				if ( SUCCEEDED( warp->GetDesc1( &desc ) ) )
				{
					m_pAdapters.AddToTail( warp.Detach() );
					MaterialAdapterInfo_t info{};
					WideCharToMultiByte( CP_UTF8, 0, desc.Description, -1, info.m_pDriverName, sizeof( info.m_pDriverName ), nullptr, nullptr );
					info.m_VendorID = desc.VendorId;
					info.m_DeviceID = desc.DeviceId;
					info.m_SubSysID = desc.SubSysId;
					info.m_Revision = desc.Revision;
					DXSupportCapsDX12 caps{};
					caps.vendor = desc.VendorId;
					caps.device = desc.DeviceId;
					caps.memory = desc.DedicatedVideoMemory;
					info.m_nDXSupportLevel = kSourceDXLevel;
					info.m_nMaxDXSupportLevel = kSourceDXLevel;
					m_AdapterInfo.AddToTail( info );
					m_AdapterCaps.AddToTail( caps );
				}
			}
		}
	}
	m_AdapterModes.SetCount( m_pAdapters.Count() );
	for ( int nIndex = 0; nIndex < m_pAdapters.Count(); ++nIndex )
	{
		Microsoft::WRL::ComPtr<IDXGIOutput> output;
		for ( UINT nOutput = 0;; ++nOutput )
		{
			Microsoft::WRL::ComPtr<IDXGIOutput> candidate;
			if ( m_pAdapters[nIndex]->EnumOutputs( nOutput, &candidate ) == DXGI_ERROR_NOT_FOUND )
				break;
			if ( !candidate )
				continue;
			DXGI_OUTPUT_DESC desc{};
			if ( FAILED( candidate->GetDesc( &desc ) ) || !desc.AttachedToDesktop )
				continue;
			output = candidate;
			break;
		}
		if ( !output && FAILED( m_pAdapters[nIndex]->EnumOutputs( 0, &output ) ) )
		{
			DEVMODEW desktop{};
			desktop.dmSize = sizeof( desktop );
			ShaderDisplayMode_t mode;
			mode.m_Format = IMAGE_FORMAT_UNKNOWN;
			if ( EnumDisplaySettingsW( nullptr, ENUM_CURRENT_SETTINGS, &desktop ) )
			{
				mode.m_nWidth = desktop.dmPelsWidth;
				mode.m_nHeight = desktop.dmPelsHeight;
				mode.m_nRefreshRateNumerator = desktop.dmDisplayFrequency;
				mode.m_nRefreshRateDenominator = 1;
			}
			else
			{
				mode.m_nWidth = GetSystemMetrics( SM_CXSCREEN );
				mode.m_nHeight = GetSystemMetrics( SM_CYSCREEN );
				mode.m_nRefreshRateNumerator = mode.m_nRefreshRateDenominator = 0;
			}
			if ( mode.m_nWidth > 0 && mode.m_nHeight > 0 )
				m_AdapterModes[nIndex].AddToTail( mode );
			continue;
		}

		UINT nCount = 0;
		DXGI_FORMAT modeFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		HRESULT hrMode = output->GetDisplayModeList( modeFormat, 0, &nCount, nullptr );
		if ( FAILED( hrMode ) || nCount == 0 )
		{
			modeFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
			nCount = 0;
			hrMode = output->GetDisplayModeList( modeFormat, 0, &nCount, nullptr );
		}
		if ( FAILED( hrMode ) || nCount == 0 )
		{
			modeFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
			nCount = 0;
			hrMode = output->GetDisplayModeList( modeFormat, 0, &nCount, nullptr );
		}
		if ( FAILED( hrMode ) || nCount == 0 )
			continue;

		CUtlVector<DXGI_MODE_DESC> modes;
		modes.SetCount( static_cast<int>( nCount ) );
		if ( FAILED( output->GetDisplayModeList( modeFormat, 0, &nCount, modes.Base() ) ) )
			continue;
		CUtlVector<ShaderDisplayMode_t> &dest = m_AdapterModes[nIndex];
		dest.EnsureCapacity( static_cast<int>( nCount ) );
		for ( UINT i = 0; i < nCount; ++i )
		{
			ShaderDisplayMode_t mode;
			mode.m_nWidth = modes[i].Width;
			mode.m_nHeight = modes[i].Height;
			mode.m_Format = IMAGE_FORMAT_UNKNOWN;
			mode.m_nRefreshRateNumerator = modes[i].RefreshRate.Numerator;
			mode.m_nRefreshRateDenominator = modes[i].RefreshRate.Denominator;
			if ( mode.m_nWidth > 0 && mode.m_nHeight > 0 )
				dest.AddToTail( mode );
		}
		if ( m_AdapterModes[nIndex].IsEmpty() )
		{
			DEVMODEW desktop{};
			desktop.dmSize = sizeof( desktop );
			ShaderDisplayMode_t mode;
			if ( EnumDisplaySettingsW( nullptr, ENUM_CURRENT_SETTINGS, &desktop ) )
			{
				mode.m_nWidth = desktop.dmPelsWidth;
				mode.m_nHeight = desktop.dmPelsHeight;
				mode.m_nRefreshRateNumerator = desktop.dmDisplayFrequency;
				mode.m_nRefreshRateDenominator = 1;
			}
			else
			{
				mode.m_nWidth = GetSystemMetrics( SM_CXSCREEN );
				mode.m_nHeight = GetSystemMetrics( SM_CYSCREEN );
				mode.m_nRefreshRateNumerator = 0;
				mode.m_nRefreshRateDenominator = 0;
			}
			mode.m_Format = IMAGE_FORMAT_UNKNOWN;
			if ( mode.m_nWidth > 0 && mode.m_nHeight > 0 )
				m_AdapterModes[nIndex].AddToTail( mode );
		}
	}
	if ( m_pAdapters.IsEmpty() )
		return INIT_FAILED;
#ifdef TRACY_ENABLE
	if ( !TracyIsStarted )
		tracy::StartupProfiler();
#endif
	return INIT_OK;
}

//-----------------------------------------------------------------------------
// Purpose: Releases the device and adapter lists; stops the profiler
//-----------------------------------------------------------------------------
void CShaderDeviceMgrDX12::Shutdown()
{
	// Final app-system/module shutdown, unlike SetMode's device-only reset.
	// Remove callbacks/detours while the sink and old recording resources still exist.
	m_Device.Highres().Shutdown();
	if ( g_pShaderAPIDX12 )
		g_pShaderAPIDX12->ShutdownDeviceResources();
	m_Device.ShutdownDevice();
	ReleaseAdapters( m_pAdapters );
	m_AdapterInfo.RemoveAll();
	m_AdapterCaps.RemoveAll();
	m_AdapterModes.RemoveAll();
	m_nCurrentAdapter = -1;
#ifdef TRACY_ENABLE
	// Join profiler workers before FreeLibrary acquires the Windows loader lock.
	g_bTracyZonesActiveDX12 = 0;
	if ( TracyIsStarted )
		tracy::ShutdownProfiler();
#endif
}

//-----------------------------------------------------------------------------
// Adapter and display mode queries
//-----------------------------------------------------------------------------
void CShaderDeviceMgrDX12::GetAdapterInfo( int nAdapter, MaterialAdapterInfo_t &info ) const
{
	memset( &info, 0, sizeof( info ) );
	if ( nAdapter >= 0 && nAdapter < m_AdapterInfo.Count() )
		info = m_AdapterInfo[nAdapter];
}

bool CShaderDeviceMgrDX12::GetRecommendedConfigurationInfo( int nAdapter, int nDXLevel, KeyValues *pConfiguration )
{
	if ( nAdapter < 0 || nAdapter >= m_AdapterCaps.Count() || !pConfiguration )
		return false;
	return m_DxSupport.GetRecommendedConfigurationInfo( m_AdapterCaps[nAdapter], nDXLevel, pConfiguration );
}

int CShaderDeviceMgrDX12::GetModeCount( int nAdapter ) const
{
	const int nResolved = nAdapter >= 0 ? nAdapter : ( m_nCurrentAdapter >= 0 ? m_nCurrentAdapter : 0 );
	return nResolved < m_AdapterModes.Count() ? m_AdapterModes[nResolved].Count() : 0;
}

void CShaderDeviceMgrDX12::GetModeInfo( ShaderDisplayMode_t *pInfo, int nAdapter, int nMode ) const
{
	if ( !pInfo )
		return;
	*pInfo = ShaderDisplayMode_t();
	const int nResolved = nAdapter >= 0 ? nAdapter : ( m_nCurrentAdapter >= 0 ? m_nCurrentAdapter : 0 );
	if ( nMode >= 0 && nResolved < m_AdapterModes.Count() && nMode < m_AdapterModes[nResolved].Count() )
		*pInfo = m_AdapterModes[nResolved][nMode];
}

void CShaderDeviceMgrDX12::GetCurrentModeInfo( ShaderDisplayMode_t *pInfo, int nAdapter ) const
{
	if ( !pInfo )
		return;
	*pInfo = ShaderDisplayMode_t();
	if ( nAdapter < 0 || nAdapter >= m_pAdapters.Count() )
		return;
	Microsoft::WRL::ComPtr<IDXGIOutput> output;
	DXGI_OUTPUT_DESC desc{};
	if ( FAILED( m_pAdapters[nAdapter]->EnumOutputs( 0, &output ) ) || FAILED( output->GetDesc( &desc ) ) )
		return;
	DEVMODEW mode{};
	mode.dmSize = sizeof( mode );
	if ( !EnumDisplaySettingsW( desc.DeviceName, ENUM_CURRENT_SETTINGS, &mode ) )
		return;
	pInfo->m_nWidth = mode.dmPelsWidth;
	pInfo->m_nHeight = mode.dmPelsHeight;
	pInfo->m_Format = IMAGE_FORMAT_UNKNOWN;
	pInfo->m_nRefreshRateNumerator = mode.dmDisplayFrequency;
	pInfo->m_nRefreshRateDenominator = 1;
}

bool CShaderDeviceMgrDX12::SetAdapter( int nAdapter, int )
{
	if ( nAdapter < 0 || nAdapter >= m_pAdapters.Count() )
		return false;
	m_nCurrentAdapter = nAdapter;
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Creates the device on nAdapter and initializes the shader API's device resources
//-----------------------------------------------------------------------------
CreateInterfaceFn CShaderDeviceMgrDX12::SetMode( void *hWnd, int nAdapter, const ShaderDeviceInfo_t &mode )
{
	if ( !SetAdapter( nAdapter, 0 ) )
		return nullptr;
	DXSupportCapsDX12 caps = m_AdapterCaps[nAdapter];
	ShaderDeviceInfo_t nativeMode = mode;
	nativeMode.m_nDXLevel = kSourceDXLevel;
	if ( g_pShaderAPIDX12 )
		g_pShaderAPIDX12->ShutdownDeviceResources();
	if ( !m_Device.Initialize( hWnd, nAdapter, nativeMode, m_pAdapters[nAdapter] ) )
		return nullptr;
	m_DxSupport.ReadHardwareCaps( caps, kSourceDXLevel );
	MaterialAdapterInfo_t info{};
	GetAdapterInfo( nAdapter, info );
	if ( g_pHardwareConfigDX12 )
	{
		g_pHardwareConfigDX12->SetAdapter( info, caps.memory, mode.m_nAASamples > 1 );
		g_pHardwareConfigDX12->SetSupportCaps( caps.fastClipping, caps.centroidHack, caps.disableShaderOptimizations );
	}
	if ( !g_pShaderAPIDX12 || !g_pShaderAPIDX12->InitializeDeviceResources( &m_Device ) )
	{
		if ( g_pShaderAPIDX12 )
			g_pShaderAPIDX12->ShutdownDeviceResources();
		m_Device.ShutdownDevice();
		return nullptr;
	}
	m_Device.Highres().Initialize( &m_Device, g_pShaderAPIDX12 );
	Msg( "ShaderAPIDX12: native D3D12 initialized on %s, %dx%d, material DX level %d, %u samples\n", info.m_pDriverName, m_Device.SceneWidth(), m_Device.SceneHeight(), kSourceDXLevel, m_Device.SceneSampleCount() );
	return Sys_GetFactoryThis();
}

//-----------------------------------------------------------------------------
// Purpose: Mode-change listeners (unique, non-null)
//-----------------------------------------------------------------------------
void CShaderDeviceMgrDX12::AddModeChangeCallback( ShaderModeChangeCallbackFunc_t func )
{
	if ( func && m_Callbacks.Find( func ) == m_Callbacks.InvalidIndex() )
		m_Callbacks.AddToTail( func );
}

void CShaderDeviceMgrDX12::RemoveModeChangeCallback( ShaderModeChangeCallbackFunc_t func )
{
	m_Callbacks.FindAndRemove( func );
}

void CShaderDeviceMgrDX12::NotifyModeChange()
{
	for ( int i = 0; i < m_Callbacks.Count(); ++i )
		m_Callbacks[i]();
}

} // namespace shaderapidx12
