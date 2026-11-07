//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Vendor frame generation for the DX12 backend: DLSS-G through Streamline (manual hooking), FSR frame
//          generation through the FidelityFX frame-interpolation swap chain, and XeSS-FG (XeFG + XeLL). Every
//          provider interpolates at present time and takes over the swap chain, so the object is owned by the
//          device: it chooses the swap-chain factory, adopts/wraps the chain, runs the per-present protocol and
//          brackets resizes. Runtimes are resolved through LoadLibraryExW/GetProcAddress; a missing module only
//          makes that provider unavailable.
//
//          Threading: Select/AdoptSwapChain/ReleaseSwapChain/BeforeResize/AfterResize/SetFpsLimit/Shutdown run on
//          the recording owner at a GPU-idle boundary; PrepareFrame on the recording owner inside Present after
//          the submission worker drained; BeforePresent/AfterPresent on whichever thread presents; Marker on any
//          thread (serialised by a mutex, never touches D3D12); the recorded dispatch callback on the worker.
//
//=============================================================================//
#ifndef FRAMEGEN_DX12_H
#define FRAMEGEN_DX12_H
#pragma once

#include "tier0/platform.h"
#include "tier0/threadtools.h"
#include "tier1/utlstring.h"
#include <d3d12.h>
#include <dxgi1_6.h>
#include <windows.h>
#include <wrl/client.h>

struct MaterialAdapterInfo_t;

namespace shaderapidx12
{
class CCommandRecorderDX12;

enum class FrameGenKindDX12 : uint8_t
{
	None = 0,
	DLSSG = 1,
	FSR = 2,
	XeFG = 3
};
const char *FrameGenKindNameDX12( FrameGenKindDX12 kind );

// Written by the worker when the recorded dispatch replays; read by the recording owner once `serial` matches.
struct FrameGenReplayResultDX12
{
	CInterlockedIntT<uint64> serial;
	CInterlockedUInt code; // 0 success, otherwise the provider's failure code (never 0)
	CInterlockedIntT<uint64> frame{ ~0ull };
};

// Camera of the frame being dispatched. Matrices are row-major with row vectors (the transpose of the
// column-vector VMatrix), which is what Streamline and XeFG take.
struct FrameGenCameraDX12
{
	float view[16], proj[16];
	float viewToClip[16], clipToView[16], clipToPrevClip[16], prevClipToClip[16];
	float pos[3], right[3], up[3], fwd[3];
	float nearPlane = 0.f, farPlane = 0.f, fovY = 0.f, aspect = 1.f;
	bool inverted = false;
};

// One dispatch; trivially copyable, copied into the ExternalCommand payload. `*Before` are the tracked states at
// record time; the callback leaves depth in DEPTH_WRITE and motion in RENDER_TARGET on every path. The hudless
// copy (when valid) was encoded before this command and rests in NON_PIXEL_SHADER_RESOURCE.
struct FrameGenDispatchDX12
{
	ID3D12Resource *depth = nullptr, *motion = nullptr, *hudless = nullptr;
	D3D12_RESOURCE_STATES depthBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE, motionBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	uint32_t width = 0, height = 0;
	uint32_t frameId = 0;      // client frame id (latency markers, XeFG present id, Streamline frame token)
	uint64_t presentSerial = 0; // the serial PrepareFrame will use for the present that consumes this dispatch (FSR frameID)
	float jitter[2] = {}, motionScale[2] = {}, frameTimeMs = 16.6667f;
	FrameGenCameraDX12 camera;
	bool reset = false, hudlessValid = false;
	uint64_t frame = 0, serial = 0;
	FrameGenReplayResultDX12 *result = nullptr;
};

class CFrameGenDX12
{
public:
	CFrameGenDX12();
	~CFrameGenDX12();
	CFrameGenDX12( const CFrameGenDX12 & ) = delete;
	CFrameGenDX12 &operator=( const CFrameGenDX12 & ) = delete;

	// Decides availability per provider once per device (modules beside the renderer, adapter vendor, feature
	// probes) and logs it. Streamline itself is only initialised when DLSS-G is first selected.
	bool Initialize( ID3D12Device *pDevice, IDXGIFactory6 *pFactory, const MaterialAdapterInfo_t &adapter, const wchar_t *pszModuleDir, bool bVerbose );

	bool Initialized() const { return m_pDevice != nullptr; }

	// Mode 1 auto (DLSS-G, else FSR, else XeFG), 2 DLSS-G, 3 FSR, 4 XeFG; None when the mode's provider is unavailable.
	FrameGenKindDX12 Resolve( int nMode ) const;
	const char *UnavailableReason( FrameGenKindDX12 kind ) const;

	FrameGenKindDX12 Kind() const { return m_Kind; }

	uint32_t Multiplier() const { return m_nMultiplier; }

	bool Hudless() const { return m_bHudless; }

	// Generated frames per rendered frame for the active kind (after provider clamping).
	uint32_t Generated() const { return m_nGenerated; }

	// Switches the active kind at a GPU-idle boundary with no swap chain alive (the device released every chain
	// through ReleaseSwapChain first). DLSS-G replaces `pQueue` with one created through the Streamline proxy
	// device; leaving DLSS-G replaces it with a native queue again. Returns false (Kind() == None) on failure;
	// LastError() says why.
	bool Select( FrameGenKindDX12 kind, uint32_t nMultiplier, bool bHudless, ID3D12CommandQueue *&pQueue );

	// Swap-chain format for the active kind: R8G8B8A8_UNORM for DLSS-G/XeFG, FP16 scRGB otherwise.
	DXGI_FORMAT PresentFormat() const;
	// Factory the device must create its swap chain on: the Streamline proxy for DLSS-G, else nullptr (native).
	IDXGIFactory6 *SwapChainFactory() const;
	// Takes over a freshly created chain that has no back-buffer references yet: FSR wraps it (the pointer is
	// replaced by the frame-interpolation chain), XeFG initialises from it (replaced by the XeFG proxy), DLSS-G
	// and None leave it alone.
	bool AdoptSwapChain( Microsoft::WRL::ComPtr<IDXGISwapChain3> &swap, ID3D12CommandQueue *pQueue, bool bInvertedDepth );
	// Reverse of AdoptSwapChain, called with every back-buffer reference already dropped.
	void ReleaseSwapChain( Microsoft::WRL::ComPtr<IDXGISwapChain3> &swap );
	// Brackets ResizeBuffers on the adopted chain (FSR disables interpolation and drops its context first).
	void BeforeResize( IDXGISwapChain3 *pSwap );
	bool AfterResize( uint32_t nWidth, uint32_t nHeight );

	// FSR creates its frame-generation context lazily (display size and depth convention come from the first
	// dispatch). NeedsContext says whether the next dispatch must first pass a GPU-idle boundary.
	bool NeedsContext( uint32_t nWidth, uint32_t nHeight, bool bInverted ) const;
	bool EnsureContext( uint32_t nWidth, uint32_t nHeight, bool bInverted );

	// Present-format copy of the scene without the HUD, written by the shader API's encode; rests NPSR.
	bool EnsureHudless( uint32_t nWidth, uint32_t nHeight );

	ID3D12Resource *HudlessTexture() const { return m_pHudless.Get(); }

	// Records the provider's per-frame work as one ExternalCommand (FSR prepare, XeFG tagging); DLSS-G tags and
	// constants are submitted here on the recording thread (thread-safe Streamline calls).
	bool RecordDispatch( CCommandRecorderDX12 &recorder, const FrameGenDispatchDX12 &dispatch );

	// Per-present protocol on the recording owner, inside Present after the worker drained and before the
	// back-buffer encode is recorded. `bActive` = a dispatch was recorded this frame; otherwise pass-through.
	// `nPresentSerial` increments by exactly one per present (FSR frame id, must match the dispatch's).
	void PrepareFrame( bool bActive, uint32_t nFrameId, uint64_t nPresentSerial, uint32_t nWidth, uint32_t nHeight, IDXGISwapChain3 *pSwap );
	// Around the DXGI present on the presenting thread.
	void BeforePresent( uint32_t nFrameId, bool bActive );
	void AfterPresent( uint32_t nFrameId );

	// Frames shown by presents since the last call (rendered + generated); 0 while inactive.
	uint32_t TakePresentedCount() { return static_cast<uint32_t>( ThreadInterlockedExchange( reinterpret_cast<int32 volatile *>( &m_nPresented ), 0 ) ); }

	// 0, or -6 once a provider reported a runtime error (cleared when read).
	int RuntimeStatus() { return ThreadInterlockedExchange( reinterpret_cast<int32 volatile *>( &m_nRuntimeError ), 0 ) ? -6 : 0; }

	// Latency markers (SHADERAPIDX12_MARKER_*) with the client frame id; Reflex/XeLL sleep precedes SIMULATION_START.
	void Marker( uint32_t nMarker, uint32_t nFrameId );
	// Reflex/XeLL frame limiter from fps_max (0 uncapped); recording owner, GPU idle.
	void SetFpsLimit( float flFps );
	// Standalone Reflex (SHADERAPIDX12_REFLEX_*). Initialises Streamline on first use; returns the
	// SHADERAPIDX12_REFLEX_STATUS_* result (LastError says why -1). Recording owner.
	int SetReflexMode( int nMode );

	const char *LastError() const { return m_LastError.Get(); }

	// Drops feature contexts/tagged resources after all adopted chains are released and the GPU is idle.
	// Keeps provider/queue support alive until the owning device drops its queue and calls Shutdown.
	void ReleaseFeatures();

	// Destroys every provider object; every adopted chain was released and the GPU is idle.
	void Shutdown();

private:
	struct SlApi;
	struct FfxFgApi;
	struct XefgApi;

	struct Provider
	{
		bool available = false;
		CUtlString version, reason;
	};

	static void ReplayThunk( ID3D12GraphicsCommandList *pList, ID3D12Device *pDevice, const void *pPayload ) noexcept;
	uint32_t Execute( ID3D12GraphicsCommandList *pList, const FrameGenDispatchDX12 &d ) const noexcept;
	void Fail( const char *pszFormat, ... );

	void LoadFfx();
	void LoadXefg();
	void LoadStreamline();
	bool EnsureStreamline();
	bool SelectDlssg( ID3D12CommandQueue *&pQueue );
	bool SelectXefg();
	void ReleaseDlssg( ID3D12CommandQueue *&pQueue );
	void ReleaseXefg();
	bool CreateXefgContext();
	void DestroyXefgContext();
	void DestroyFfxContexts();
	bool CreateFfxFgContext( uint32_t nWidth, uint32_t nHeight, bool bInverted );
	bool RecreateNativeQueue( ID3D12CommandQueue *&pQueue );
	void ApplyReflex();
	void ApplyXellSleepMode();
	bool DlssgSetOptions( bool bOn, uint32_t nWidth, uint32_t nHeight );
	bool SubmitDlssgFrame( const FrameGenDispatchDX12 &d );
	void SlMarker( uint32_t nMarker, uint32_t nFrameId );

	ID3D12Device *m_pDevice = nullptr;
	IDXGIFactory6 *m_pFactory = nullptr;
	bool m_bVerbose = false;
	unsigned m_nVendor = 0, m_nDeviceId = 0;
	LUID m_AdapterLuid{};
	wchar_t m_szDir[MAX_PATH] = {}; // renderer directory with a trailing separator
	Provider m_Dlssg, m_Fsr, m_Xefg;
	SlApi *m_pSl = nullptr;
	FfxFgApi *m_pFfx = nullptr;
	XefgApi *m_pXefg = nullptr;
	FrameGenKindDX12 m_Kind = FrameGenKindDX12::None;
	uint32_t m_nMultiplier = 2, m_nGenerated = 1;
	bool m_bHudless = true;
	bool m_bInverted = false; // depth convention of the last dispatch / adopted chain
	float m_flFpsLimit = 0.f;
	CUtlString m_LastError;
	CThreadFastMutex m_MarkerMutex;
	CInterlockedUInt m_nPresented;
	CInterlockedInt m_nRuntimeError;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_pHudless;
	uint32_t m_nHudlessWidth = 0, m_nHudlessHeight = 0;
	DXGI_FORMAT m_HudlessFormat = DXGI_FORMAT_UNKNOWN;

	// Streamline (DLSS-G)
	Microsoft::WRL::ComPtr<ID3D12Device> m_pSlDevice;
	Microsoft::WRL::ComPtr<IDXGIFactory6> m_pSlFactory;
	bool m_bSlInitialized = false, m_bDlssgLoaded = false, m_bQueueIsProxy = false;
	int m_nReflexMode = 0; // SHADERAPIDX12_REFLEX_*; DLSS-G forces at least low latency, XeFG hands latency to XeLL
	// Reflex sleep / PCL markers run for DLSS-G and for standalone Reflex, never next to XeLL.
	bool ReflexMarkers() const { return m_Kind != FrameGenKindDX12::XeFG && ( m_Kind == FrameGenKindDX12::DLSSG || m_nReflexMode != 0 ); }
	bool m_bDlssgOptionsValid = false, m_bDlssgOn = false, m_bDlssgStatusLogged = false;
	uint32_t m_nDlssgWidth = 0, m_nDlssgHeight = 0, m_nDlssgGenerated = 0, m_nDlssgBackBuffers = 0, m_nDlssgMax = 0;
	IDXGISwapChain3 *m_pPresentSwap = nullptr; // chain of the present in flight (set by PrepareFrame, read on the presenting thread)
	UINT m_nDlssgLastPresentCount = 0;
	uint32_t m_nDlssgLogCounter = 0;
	bool m_bDlssgPresentCountValid = false;

	// FFX frame generation
	void *m_pFfxSwapChainContext = nullptr; // ffxContext of the frame-interpolation swap chain
	void *m_pFfxFgContext = nullptr;        // ffxContext of the frame-generation effect
	uint32_t m_nFfxWidth = 0, m_nFfxHeight = 0;
	bool m_bFfxInverted = false, m_bFfxEnabled = false;

	// XeFG + XeLL
	void *m_hXefg = nullptr;
	void *m_hXell = nullptr;
	bool m_bXefgEnabled = false, m_bXefgWarned = false;
	uint32_t m_nXefgBackBufferWidth = 0, m_nXefgBackBufferHeight = 0;
};

} // namespace shaderapidx12

#endif // FRAMEGEN_DX12_H
