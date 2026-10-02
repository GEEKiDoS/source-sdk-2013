//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Native-resolution temporal anti-aliasing providers (DLAA, FSR native AA, XeSS AA) and the DLSS-NR neural
//          rendering post-process for the DX12 backend. Every provider export is resolved at runtime through
//          LoadLibraryExW/GetProcAddress; a missing module or export only makes that provider unavailable. Provider
//          objects are owned by the recording thread; the worker only runs the recorded dispatch callback, which names
//          stable objects that stay alive until a GPU-idle boundary.
//
//=============================================================================//
#ifndef UPSCALER_DX12_H
#define UPSCALER_DX12_H
#pragma once

#include "tier0/platform.h"
#include "tier0/threadtools.h"
#include "tier1/utlstring.h"
#include "tier1/utlvector.h"
#include <d3d12.h>
#include <windows.h>
#include <wrl/client.h>

struct MaterialAdapterInfo_t;

namespace shaderapidx12
{
class CCommandRecorderDX12;
struct NrConstantsDX12;
constexpr uint32_t kNrMaxLayersDX12 = 8;
constexpr uint32_t kNrMinWidth = 320, kNrMinHeight = 180;

enum class UpscalerKindDX12 : uint8_t
{
	None = 0,
	DLSS = 1,
	FSR = 2,
	XeSS = 3
};
const char *UpscalerKindNameDX12( UpscalerKindDX12 kind );

// Written by the worker when a recorded dispatch replays; read by the recording owner once `serial` matches.
struct UpscalerReplayResultDX12
{
	CInterlockedIntT<uint64> serial;
	CInterlockedUInt code;   // 0 success, otherwise the provider's failure code (never 0)
	CInterlockedUInt nrCode; // 0 success or no DLSS-NR layers requested, otherwise the NR failure code
	CInterlockedIntT<uint64> frame{ ~0ull };
};

struct UpscalerFeatureDescDX12
{
	UpscalerKindDX12 kind = UpscalerKindDX12::None;
	uint32_t width = 0, height = 0;
	bool depthInverted = false;

	bool operator==( const UpscalerFeatureDescDX12 &o ) const { return kind == o.kind && width == o.width && height == o.height && depthInverted == o.depthInverted; }

	bool operator!=( const UpscalerFeatureDescDX12 &o ) const { return !( *this == o ); }
};

// DLSS-NR model tuning. The model reads these once, while each layer's feature is created; a change rebuilds the chain.
struct DlssNrTuningDX12
{
	uint32_t preset = 0, style = 0;
	float intensity = 1.f, localStructure = 1.f, localTone = 1.f, skinStructure = -1.f;
	bool autoMask = true, uiCorrection = true;

	bool operator==( const DlssNrTuningDX12 &o ) const
	{
		return preset == o.preset && style == o.style && intensity == o.intensity && localStructure == o.localStructure && localTone == o.localTone &&
		    skinStructure == o.skinStructure && autoMask == o.autoMask && uiCorrection == o.uiCorrection;
	}

	bool operator!=( const DlssNrTuningDX12 &o ) const { return !( *this == o ); }
};

// DLSS-NR composition of the model's answer back onto each layer's input; read per dispatch.
struct DlssNrComposeDX12
{
	float whitePoint = 1.f, transferStrength = 1.f, colourStrength = 1.f, maxRatio = 2.f, compareSplit = .5f, compareZoom = 1.f;
	uint32_t reversibleMode = 0, debugView = 0, compareMode = 0;
	bool applyModel = true, compareSwap = false;
};

// One dispatch. `*Before` are the tracked states at record time; the callback leaves colour/motion in
// RENDER_TARGET, depth in DEPTH_WRITE and output in UNORDERED_ACCESS on every path.
struct UpscalerDispatchDX12
{
	ID3D12Resource *color = nullptr, *depth = nullptr, *motion = nullptr, *output = nullptr;
	D3D12_RESOURCE_STATES colorBefore = D3D12_RESOURCE_STATE_RENDER_TARGET, depthBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
	D3D12_RESOURCE_STATES motionBefore = D3D12_RESOURCE_STATE_RENDER_TARGET, outputBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	float jitter[2] = {}, motionScale[2] = {};
	float cameraNear = 0, cameraFar = 0, fovY = 0, frameTimeMs = 0;
	uint32_t width = 0, height = 0;
	uint32_t nrLayers = 0; // DLSS-NR layers chained after the temporal AA; each feeds the next
	DlssNrComposeDX12 nr;
	bool reset = false, nrReset = false;
	bool forceFailure = false; // test hook: skip the provider call and report a replay failure
	uint64_t frame = 0, serial = 0;
	UpscalerReplayResultDX12 *result = nullptr;
	// Shader-visible descriptor block for the depth clone and DLSS-NR passes (DescriptorCount(nrLayers) entries),
	// written at record time and retired with the recording fence.
	ID3D12DescriptorHeap *heap = nullptr;
	D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
	D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
};

class CUpscalerDX12
{
public:
	CUpscalerDX12();
	~CUpscalerDX12();
	CUpscalerDX12( const CUpscalerDX12 & ) = delete;
	CUpscalerDX12 &operator=( const CUpscalerDX12 & ) = delete;

	// Loads every installed provider module beside the renderer and logs vendor/device and runtime versions.
	bool Initialize( ID3D12Device *pDevice, const MaterialAdapterInfo_t &adapter, const wchar_t *pszModuleDir, bool bVerbose );

	bool Initialized() const { return m_pDevice != nullptr; }

	// Mode 1 auto, 2 DLSS/DLAA, 3 FSR native AA, 4 XeSS AA; None when the mode's provider is unavailable.
	UpscalerKindDX12 Resolve( int nMode ) const;
	// Creates the provider context for `desc` when it differs from the current one. DLSS records its creation on a
	// private list executed on `pQueue` and waited for, so no recorded frame ever creates and evaluates together.
	// The caller destroys any previous feature through ReleaseFeature first.
	bool PrepareFeature( const UpscalerFeatureDescDX12 &desc, ID3D12CommandQueue *pQueue );

	const UpscalerFeatureDescDX12 &Feature() const { return m_Feature; }

	// Provider jitter for the current feature; false keeps the backend's Halton(2,3) eight-phase sequence.
	bool ProviderJitter( uint64_t nIndex, float &flX, float &flY ) const;
	// Records the whole provider envelope (temporal AA plus any DLSS-NR layers) as one ExternalCommand.
	bool RecordDispatch( CCommandRecorderDX12 &recorder, const UpscalerDispatchDX12 &dispatch );
	// Destroys the current feature and the DLSS-NR chain. Only after SubmitAndWaitForGpu: no recorded callback may
	// still name them.
	void ReleaseFeature();
	// ReleaseFeature plus runtime shutdown and module unload; the same GPU-idle precondition applies.
	void Shutdown();

	// DLSS-NR: availability is decided at Initialize from the installed files; the snippet itself loads on first use.
	bool NrAvailable() const { return m_Nr.available; }

	const CUtlString &NrReason() const { return m_Nr.reason; }

	bool NrReady( const DlssNrTuningDX12 &tuning, uint32_t nLayers, ID3D12Resource *pSource ) const;
	uint32_t NrLayers() const;
	// Builds `nLayers` features over `pSource` (the temporal-AA output, which every chain starts from). Any existing
	// chain must have been released through ReleaseNr behind a GPU-idle boundary first.
	bool PrepareNr( const DlssNrTuningDX12 &tuning, uint32_t nLayers, ID3D12Resource *pSource, ID3D12CommandQueue *pQueue );
	void ReleaseNr();

	// The dispatch needs a descriptor block when DLSS or DLSS-NR reads the depth clone.
	bool NeedsDescriptors( const UpscalerDispatchDX12 &d ) const { return NeedsDepthClone( d ); }

	static uint32_t DescriptorCount( uint32_t nNrLayers ) { return 6u * ( 1u + 2u * nNrLayers ); }

private:
	enum class State : uint8_t
	{
		Disabled,
		Ready,
		Failed
	};
	struct XeSSApi;
	struct FfxApi;
	struct NgxApi;
	struct NrApi;
	struct NrChain;

	struct Provider
	{
		HMODULE module = nullptr;
		bool available = false;
		CUtlString version, reason;
		uint64_t versionId = 0; // FFX: the provider id returned by the version query
	};

	static void ReplayThunk( ID3D12GraphicsCommandList *pList, ID3D12Device *pDevice, const void *pPayload ) noexcept;
	uint32_t Execute( ID3D12GraphicsCommandList *pList, const UpscalerDispatchDX12 &d ) const noexcept;
	// Runs the DLSS-NR chain over d.output; on success the final layer is in NrResult (UNORDERED_ACCESS).
	uint32_t ExecuteNr( ID3D12GraphicsCommandList *pList, const UpscalerDispatchDX12 &d ) const noexcept;
	ID3D12Resource *NrResult() const;
	bool EnsureCompute();
	void WriteDescriptors( const UpscalerDispatchDX12 &d ) const;
	void BindCompute( ID3D12GraphicsCommandList *pList, const UpscalerDispatchDX12 &d, ID3D12PipelineState *pPso, uint32_t nPass, const NrConstantsDX12 &constants ) const;
	bool LoadFfx( const wchar_t *pszDir );

	// DLSS and DLSS-NR read a typed R24_UNORM_X8 clone of the R24G8_TYPELESS scene depth, refreshed in every replay.
	bool NeedsDepthClone( const UpscalerDispatchDX12 &d ) const { return m_Feature.kind == UpscalerKindDX12::DLSS || d.nrLayers; }

	bool EnsureDepthClone( uint32_t nWidth, uint32_t nHeight );
	bool LoadXeSS( const wchar_t *pszDir );
	bool LoadNgx( const wchar_t *pszDir );
	void ProbeNr( const wchar_t *pszDir );
	bool InitNrSnippet();
	bool CreateXeSS( const UpscalerFeatureDescDX12 &desc );
	bool CreateFfx( const UpscalerFeatureDescDX12 &desc );
	bool CreateDlss( const UpscalerFeatureDescDX12 &desc, ID3D12CommandQueue *pQueue );
	// Private one-shot list for feature creation; EndImmediate executes it on `pQueue` and waits for completion.
	ID3D12GraphicsCommandList *BeginImmediate();
	bool EndImmediate( ID3D12CommandQueue *pQueue );

	ID3D12Device *m_pDevice = nullptr;
	bool m_bVerbose = false;
	unsigned m_nVendor = 0, m_nDeviceId = 0;
	wchar_t m_szDir[MAX_PATH] = {}; // renderer directory with a trailing separator
	Provider m_Dlss, m_Fsr, m_Xess, m_NgxCore, m_Nr;
	XeSSApi *m_pXessApi = nullptr;
	FfxApi *m_pFfxApi = nullptr;
	NgxApi *m_pNgxApi = nullptr;
	NrApi *m_pNrApi = nullptr;
	NrChain *m_pNrChain = nullptr;
	State m_State = State::Disabled;
	UpscalerFeatureDescDX12 m_Feature{};
	void *m_pXessContext = nullptr;
	void *m_pFfxContext = nullptr;
	void *m_pDlssHandle = nullptr;
	CUtlVector<float> m_FfxJitter;                        // FSR jitter offsets, x/y interleaved per phase
	Microsoft::WRL::ComPtr<ID3D12Resource> m_pDepthClone; // R32_FLOAT, NON_PIXEL_SHADER_RESOURCE between replays
	Microsoft::WRL::ComPtr<ID3D12RootSignature> m_pComputeRoot;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pNrPso, m_pDepthPso;
	UINT m_nDescriptorStride = 0;
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_pImmediateAllocator;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_pImmediateList;
	Microsoft::WRL::ComPtr<ID3D12Fence> m_pImmediateFence;
	uint64_t m_nImmediateValue = 0;
};

} // namespace shaderapidx12

#endif // UPSCALER_DX12_H
