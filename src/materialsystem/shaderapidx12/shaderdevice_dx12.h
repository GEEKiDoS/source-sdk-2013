//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Native D3D12 shader device, submission worker and device manager.
//
//=============================================================================//

#ifndef SHADERDEVICE_DX12_H
#define SHADERDEVICE_DX12_H
#pragma once

#include "shaderapi/IShaderDevice.h"
#include "materialsystem/imesh.h"
#include "materialsystem/shaderapidx12/resources_dx12.h"
#include "materialsystem/shaderapidx12/hardwareconfig_dx12.h"
#include "materialsystem/shaderapidx12/dxsupport_dx12.h"
#include "materialsystem/shaderapidx12/shader_translate_dx12.h"
#include "materialsystem/shaderapidx12/command_recorder_dx12.h"
#include "materialsystem/shaderapidx12/framegen_dx12.h"
#include "materialsystem/shaderapidx12/lighting_dx12.h"
#include "materialsystem/shaderapidx12/highres_lightmaps_dx12.h"
#include "mathlib/vector.h"
#include "mathlib/vector4d.h"
#include "tier0/threadtools.h"
#include "tier1/utlstring.h"
#include "tier1/utlvector.h"
#include <intrin.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <stdint.h>

class IShaderUtil;
class IFileSystem;

namespace shaderapidx12
{

struct ShaderRecordDX12
{
	uint64_t identity = 0;
	// Hot per-draw constant layout for the active variant (see DrawBuffers); invalidated with the variant.
	uint32_t constantCounts[3] = {};
	uint64_t constantLayoutVariant = 0;
	uint32_t inlineConstantMask = 0;
	bool constantLayoutValid = false;
	CUtlVector<unsigned char> legacyBytecode;
	CUtlVector<unsigned char> bytecode;
	bool stagePixel = false;
	// SM2 centroid declarations live in the VCS header, not the bytecode's DCL tokens.
	uint32_t centroidTexcoordMask = 0;
	ShaderTranslationResultDX12 translated;
	bool stageGeometry = false;
	uint64_t activeVariantKey = 0;
	bool activeVariantValid = false, inputSignatureReady = false;
	CUtlVector<ShaderInputElementDX12> inputSignature;
	uint32_t nativeConstantRegisters[3] = {};

	// Native records: reflected cbuffers (engine or bridge-written material blocks) and their combined ABI hash.
	struct NativeCBufferMemberDX12
	{
		CUtlString name;
		uint32_t offset = 0, byteSize = 0;
	};

	struct NativeCBufferBindingDX12
	{
		CUtlString name;
		uint32_t shaderRegister = 0, registerSpace = 0, byteSize = 0;
		uint64_t layoutHash = 0;
		CCopyableUtlVector<NativeCBufferMemberDX12> members;
	};

	CUtlVector<NativeCBufferBindingDX12> nativeCBuffers;
	uint64_t nativeAbiHash = 0;
	bool nativeReflectionReady = false;
	bool lightingAbi = false, sunVisibilityAbi = false, propVisibilityAbi = false;
	uint32_t lightmapSamplerMask = 0, nativeSamplerMask = 0;
	bool highresAbi = false, samplerRolesReady = false, nativeCasterTwin = false;
	bool nativeEarlyDepthTwin = false;
	// Hash of translated.outputLinkage for the active variant; cleared whenever the linkage or variant changes.
	uint64_t linkageHash = 0, linkageHashVariant = 0;
	bool linkageHashValid = false;

	struct DerivedConstants
	{
		uint64_t versions[3] = {};
		CUtlVector<Vector4D> floats;
		CUtlVector<IntVector4D> integers;
		CUtlVector<uint32_t> booleans;

		DerivedConstants() = default;
		DerivedConstants( const DerivedConstants & ) = delete;
		DerivedConstants &operator=( const DerivedConstants & ) = delete;

		DerivedConstants( DerivedConstants &&other ) noexcept { Swap( other ); }

		DerivedConstants &operator=( DerivedConstants &&other ) noexcept
		{
			Swap( other );
			return *this;
		}

		void Swap( DerivedConstants &other ) noexcept
		{
			if ( this == &other )
				return;
			for ( int i = 0; i < ARRAYSIZE( versions ); ++i )
				V_swap( versions[i], other.versions[i] );
			floats.Swap( other.floats );
			integers.Swap( other.integers );
			booleans.Swap( other.booleans );
		}
	} derived;

	struct Variant
	{
		uint64_t key = 0;
		ShaderTranslationResultDX12 result;
		CUtlVector<ShaderInputElementDX12> inputSignature;
		bool inputSignatureReady = false;
		DerivedConstants derived;
	};

	CUtlBlockVector<Variant> variants;

	// Generated raster-stage variants share this vertex shader's retirement identity.
	struct GeometryVariant
	{
		uint64_t vertexVariant = 0;
		uint32_t rasterKey = 0;
		ShaderTranslationResultDX12 result;
	};

	CUtlBlockVector<GeometryVariant> geometryVariants;

	D3D12_SHADER_BYTECODE Bytecode() const
	{
		const CUtlVector<unsigned char> &code = legacyBytecode.IsEmpty() ? bytecode : translated.bytecode;
		return { code.Base(), static_cast<SIZE_T>( code.Count() ) };
	}
};

class CShaderDeviceDX12 final : public IShaderDevice
{
public:
	CShaderDeviceDX12();
	~CShaderDeviceDX12();
	bool Initialize( void *hWnd, int nAdapter, const ShaderDeviceInfo_t &info, IDXGIAdapter1 *pSelectedAdapter );
	void ShutdownDevice();

	bool IsInitialized() const { return !m_bFailed && m_pDevice != nullptr && m_pQueue != nullptr; }

	ID3D12Device *NativeDevice() const { return m_pDevice.Get(); }
	CLightingDX12 &Lighting() { return m_Lighting; }
	CHighresLightmapsDX12 &Highres() { return m_Highres; }

	// Drains queued submissions so direct queue operations keep submission order.
	ID3D12CommandQueue *Queue()
	{
		DrainSubmissions();
		return m_pQueue.Get();
	}

	void FlushSubmissions();
	bool ConsumeGpuTime( double &flAverageMs, uint32_t &nFrames );

	// Exclusive GPU intervals; receiver rendering includes ordinary/baked receiver work, not just shadow kernels.
	enum GpuStageDX12 : uint32_t { GpuOther, GpuReceiverRendering, GpuShadowDepth, GpuDepthRectRestore, GpuStageCount };
	// View 0 is outside lighting scopes; 1..64 are BeginView ordinals, 65 is explicit overflow.
	static constexpr uint32_t kGpuReceiverMaxViews = 64;
	static constexpr uint32_t kGpuReceiverViewBuckets = kGpuReceiverMaxViews + 2;
	struct GpuReceiverViewStatsDX12
	{
		double ms[2] = {}, psInvocations[2] = {}, draws[2] = {}; // other, lit; per presented frame after consume
		int width = 0, height = 0;
		bool seen = false, mixedViewport = false;
	};
	struct GpuStageStatsDX12
	{
		double msPerPresentedFrame[GpuStageCount] = {};
		double psInvocationsPerPresentedFrame[GpuStageCount] = {};
		double cInvocationsPerPresentedFrame[GpuStageCount] = {};
		double cPrimitivesPerPresentedFrame[GpuStageCount] = {};
		uint32_t presentedFrames = 0, pendingSpans = 0;
		uint64_t completedSpans = 0, skippedSpans = 0, overflowSpans = 0, invalidSpans = 0, scopeErrors = 0, totalSkippedLists = 0;
		uint64_t pipelineCompletedSpans = 0, pipelineSkippedSpans = 0, pipelineOverflowSpans = 0, pipelineInvalidSpans = 0;
		GpuReceiverViewStatsDX12 receiverViews[kGpuReceiverViewBuckets] = {};
		uint64_t receiverViewOverflow = 0, receiverScopeErrors = 0, receiverSkippedDraws = 0;
		bool available = false, pipelineAvailable = false;
	};
	void BeginGpuStage( GpuStageDX12 stage );
	void EndGpuStage( GpuStageDX12 stage );
	void BeginGpuReceiverView( int width, int height );
	void EndGpuReceiverView();
	void GpuReceiverDraw( bool lit );
	// ConsumeGpuTime collects fence-completed timestamps and pipeline counters; consume this snapshot immediately afterward.
	bool ConsumeGpuStageStats( GpuStageStatsDX12 &stats );

	// Replays every recorded command before returning; required before rewriting or freeing CPU RTV/DSV descriptors
	// that recorded OMSetRenderTargets/Clear*View calls still name.
	void DrainRecording()
	{
		m_Recorder.Flush();
		DrainSubmissions();
	}

	// Waits until the worker has issued every queued operation (header-inline so callers outside the DLL can use it).
	void DrainSubmissions()
	{
		const uint32_t nHead = m_nSubmitHead;
		while ( m_nSubmitTail != nHead )
			m_SubmitDoneEvent.Wait();
	}

	uint64_t Submit( bool bWait );
	uint64_t SubmitFrameSync();
	bool WaitForFence( uint64_t nValue );
	// Owner-thread GPU-idle boundary: replays every recorded command (external callbacks included), executes the
	// current list and waits for its fence. DrainRecording alone only replays on the CPU.
	bool SubmitAndWaitForGpu();

	// Blocks until the worker retires one more queued operation; returns false when nothing is queued.
	bool WaitForSubmissionProgress()
	{
		if ( m_nSubmitTail == m_nSubmitHead )
			return false;
		m_SubmitDoneEvent.Wait();
		return true;
	}

	// -dx12debug/-dx12gpuvalidation: forwards stored corruption/error messages as "ShaderAPIDX12 debug layer" warnings.
	void ReportDebugMessages();

	// Commands are recorded and replayed onto the native list on the submission worker.
	CCommandRecorderDX12 *CommandList() { return !m_bFailed && m_bRecording && IsRecordingOwner() ? &m_Recorder : nullptr; }

	ID3D12Resource *CurrentBackBuffer() const;
	uint32_t CurrentBackBufferIndex() const;
	D3D12_CPU_DESCRIPTOR_HANDLE CurrentBackBufferRTV() const;
	ID3D12Resource *SceneColor() const;
	D3D12_CPU_DESCRIPTOR_HANDLE SceneRTV( bool bSRGB = false ) const;
	ID3D12Resource *SceneDepth() const;
	D3D12_CPU_DESCRIPTOR_HANDLE SceneDSV() const;
	D3D12_CPU_DESCRIPTOR_HANDLE SceneReadOnlyDSV() const;
	// Scene colour, swap chain and full-frame targets are linear scRGB FP16; FLOAT has no sRGB view, so both
	// SceneRTV(true) and SceneRTV(false) name identical FLOAT RTVs.
	static constexpr ImageFormat kSceneImageFormat = IMAGE_FORMAT_RGBA16161616F;

	DXGI_FORMAT SceneColorFormat( bool = false ) const { return DXGI_FORMAT_R16G16B16A16_FLOAT; }

	DXGI_FORMAT SceneDepthFormat() const { return DXGI_FORMAT_D24_UNORM_S8_UINT; }

	// Swap-chain format: FP16 scRGB, or R8G8B8A8_UNORM while a frame generator that rejects FP16 owns the chain.
	// Scene colour, GetBackBufferFormat() and every render target stay FP16; only the final encode changes.
	DXGI_FORMAT PresentFormat() const { return m_PresentFormat; }
	bool PresentFormatIsFp16() const { return m_nPresentFormat == static_cast<int>( DXGI_FORMAT_R16G16B16A16_FLOAT ); }
	int HdrDisplayStatus() const { return m_nHdrDisplayStatus; }

	// Windowed gamma/TV-range coefficients for the presentation encode; false when the scene is shown unchanged.
	bool PresentGammaCoefficients( float ( &flOut )[4] ) const
	{
		if ( !m_bWindowed || ( !m_bGammaTV && m_flGamma == 2.2f ) )
			return false;
		flOut[0] = m_flGamma / 2.2f;
		flOut[1] = m_bGammaTV ? 2.2f / m_flGammaExponent : 1.f;
		flOut[2] = m_bGammaTV ? ( m_flGammaMax - m_flGammaMin ) / 255.f : 1.f;
		flOut[3] = m_bGammaTV ? m_flGammaMin / 255.f : 0.f;
		return true;
	}

	// Frame generation (framegen_dx12.h). The shader API requests a kind; the device applies it at the tail of the
	// next Present (the recorder is empty there) by recreating every view's swap chain through the provider.
	CFrameGenDX12 &FrameGen() { return m_FrameGen; }

	IDXGIFactory6 *Factory() const { return m_pFactory.Get(); }

	bool IsWindowed() const { return m_bWindowed; }

	void RequestFrameGenSelect( FrameGenKindDX12 kind, uint32_t nMultiplier, bool bHudless )
	{
		m_PendingSelect = { kind, nMultiplier, bHudless, true };
	}

	bool FrameGenSelectPending() const { return m_PendingSelect.valid; }

	// Applies a pending request now (recording owner; brings the GPU idle itself). Called from Present's tail
	// and from the shader API's device-resource release.
	void ApplyFrameGenSelect();

	bool TakeFrameGenSelectFailure()
	{
		const bool bFailed = m_bSelectFailed;
		m_bSelectFailed = false;
		return bFailed;
	}

	// Reflex/XeLL frame limiter (fps_max); recording owner.
	void SetFrameGenFpsLimit( float flFps );

	// Id of the next present without a client frame id, and the serial the next Present will carry.
	uint32_t NextPresentId() const { return m_nLastPresentId + 1; }

	uint64_t NextPresentSerial() const { return m_nPresentSerial + 1; }

	int SceneSampleQuality() const { return m_nSampleQuality; }

	int SceneSampleCount() const { return m_nSampleCount; }

	int SceneWidth() const { return m_nWidth; }

	int SceneHeight() const { return m_nHeight; }

	void TransitionSceneColor( D3D12_RESOURCE_STATES state );
	void TransitionSceneDepth( D3D12_RESOURCE_STATES state );

	D3D12_RESOURCE_STATES SceneColorState() const { return m_pCurrentView ? m_pCurrentView->sceneColorState : D3D12_RESOURCE_STATE_RENDER_TARGET; }

	D3D12_RESOURCE_STATES SceneDepthState() const { return m_pCurrentView ? m_pCurrentView->sceneDepthState : D3D12_RESOURCE_STATE_DEPTH_WRITE; }

	// Records the states an external command leaves the scene resources in (it issued its own barriers).
	void SetSceneStatesAfterExternal( D3D12_RESOURCE_STATES color, D3D12_RESOURCE_STATES depth )
	{
		if ( m_pCurrentView )
		{
			m_pCurrentView->sceneColorState = color;
			m_pCurrentView->sceneDepthState = depth;
		}
	}

	void RetainResource( ID3D12Resource *pResource );
	// Workers enqueue handles only; the recording owner drains at public API boundaries.
	void QueueTextureDeletion( uintptr_t hTexture );
	void TakeTextureDeletionRequests( CUtlVector<uintptr_t> &handles );

	// Relaxed hint; a request queued concurrently is taken by the next check.
	bool HasTextureDeletionRequests() const { return m_nPendingTextureDeletionCount != 0; }

	bool SupportsMSAA( int nCount, int nQuality = 0 ) const;
	bool SupportsMSAAFormat( DXGI_FORMAT format, int nCount, int nQuality ) const;
	bool ChangeMode( const ShaderDeviceInfo_t &info );

	// x64 TEB ClientId.UniqueThread (what GetCurrentThreadId returns), read inline on the per-draw path.
	bool IsRecordingOwner() const { return m_nOwnerThread == static_cast<unsigned>( __readgsdword( 0x48 ) ); }

	bool AcquireRecordingOwnership();
	void ReleaseRecordingOwnership();

	SignDxbcFnDX12 Signer() const { return m_pfnSigner; }

	uint64_t NextFenceValue() const { return m_nFenceValue + 1; }

	uint64_t CompletedFenceValue() const { return m_pFence ? m_pFence->GetCompletedValue() : 0; }

	void ReleaseResources() override;
	void ReacquireResources() override;

	ImageFormat GetBackBufferFormat() const override { return kSceneImageFormat; }

	void GetBackBufferDimensions( int &nWidth, int &nHeight ) const override
	{
		nWidth = m_nWidth;
		nHeight = m_nHeight;
	}

	int GetCurrentAdapter() const override { return m_nAdapterIndex; }

	bool IsUsingGraphics() const override { return IsInitialized(); }

	void SpewDriverInfo() const override;

	int StencilBufferBits() const override { return 8; }

	bool IsAAEnabled() const override { return m_nSampleCount > 1; }

	void Present() override;
	void GetWindowSize( int &nWidth, int &nHeight ) const override;
	void SetHardwareGammaRamp( float fGamma, float fGammaTVRangeMin, float fGammaTVRangeMax, float fGammaTVExponent, bool bTVEnabled ) override;
	bool AddView( void *hWnd ) override;
	void RemoveView( void *hWnd ) override;
	void SetView( void *hWnd ) override;
	IShaderBuffer *CompileShader( const char *pProgram, size_t nBufLen, const char *pShaderVersion ) override;
	VertexShaderHandle_t CreateVertexShader( IShaderBuffer *pShaderBuffer ) override;
	void DestroyVertexShader( VertexShaderHandle_t hShader ) override;
	GeometryShaderHandle_t CreateGeometryShader( IShaderBuffer *pShaderBuffer ) override;
	void DestroyGeometryShader( GeometryShaderHandle_t hShader ) override;
	PixelShaderHandle_t CreatePixelShader( IShaderBuffer *pShaderBuffer ) override;
	void DestroyPixelShader( PixelShaderHandle_t hShader ) override;
	IMesh *CreateStaticMesh( VertexFormat_t vertexFormat, const char *pTextureBudgetGroup, IMaterial *pMaterial = nullptr ) override;
	void DestroyStaticMesh( IMesh *pMesh ) override;
	IVertexBuffer *CreateVertexBuffer( ShaderBufferType_t type, VertexFormat_t fmt, int nVertexCount, const char *pBudgetGroup ) override;
	void DestroyVertexBuffer( IVertexBuffer *pBuffer ) override;
	IIndexBuffer *CreateIndexBuffer( ShaderBufferType_t type, MaterialIndexFormat_t fmt, int nIndexCount, const char *pBudgetGroup ) override;
	void DestroyIndexBuffer( IIndexBuffer *pBuffer ) override;
	IVertexBuffer *GetDynamicVertexBuffer( int nStreamID, VertexFormat_t vertexFormat, bool bBuffered = true ) override;
	IIndexBuffer *GetDynamicIndexBuffer( MaterialIndexFormat_t fmt, bool bBuffered = true ) override;
	void EnableNonInteractiveMode( MaterialNonInteractiveMode_t mode, ShaderNonInteractiveInfo_t *pInfo = nullptr ) override;
	void RefreshFrontBufferNonInteractive() override;
	void HandleThreadEvent( uint32 threadEvent ) override;
	char *GetDisplayDeviceName() override;

private:
	struct View
	{
		HWND hwnd = nullptr;
		Microsoft::WRL::ComPtr<IDXGISwapChain3> swap;
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap, sceneRTVHeap, sceneDSVHeap;
		// Heap starts cached at creation; SceneRTV/SceneDSV run per draw.
		D3D12_CPU_DESCRIPTOR_HANDLE sceneRTVStart{}, sceneDSVStart{};
		CUtlVector<ID3D12Resource *> backBuffers;
		Microsoft::WRL::ComPtr<ID3D12Resource> sceneColor, sceneDepth;
		D3D12_RESOURCE_STATES sceneColorState = D3D12_RESOURCE_STATE_RENDER_TARGET;
		D3D12_RESOURCE_STATES sceneDepthState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
		int width = 0, height = 0;
		uint64_t lastFence = 0;
		bool suspended = false, occluded = false;

		~View() { ReleaseBackBuffers(); }

		void ReleaseBackBuffers()
		{
			for ( ID3D12Resource *pBuffer : backBuffers )
				if ( pBuffer )
					pBuffer->Release();
			backBuffers.RemoveAll();
		}
	};

	// -dx12stats list spans are fence-owned; -dx12shadowstats adds exclusive stage timestamps and pipeline counters.
	static constexpr uint32_t kTimestampSlots = 32;
	static constexpr uint32_t kStageTimestampSlots = 4096;
	static constexpr uint32_t kGpuStageStackSize = 64;
	bool EnsureGpuTiming();
	void EnsureGpuPipelineStats();
	void CollectGpuTiming();
	bool GpuTimingBeforeSubmit();
	void GpuTimingAfterSubmit();
	void BeginGpuStageSpan();
	void EndGpuStageSpan();
	void ChangeGpuStage( GpuStageDX12 stage );
	Microsoft::WRL::ComPtr<ID3D12QueryHeap> m_pTimestampHeap;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_pTimestampReadback;
	const uint64_t *m_pTimestampData = nullptr;
	uint64_t m_TimestampFences[kTimestampSlots] = {};
	uint32_t m_nTimestampSlot = 0;
	bool m_bTimestampBegun = false, m_bTimestampUnavailable = false, m_bGpuTimingStopping = false;
	uint64_t m_nTimestampFrequency = 0;
	double m_flGpuTimeSumMs = 0.0;
	uint32_t m_nGpuTimePresented = 0;

	bool m_bShadowTimingEnabled = false, m_bStageQueriesAvailable = false, m_bStageTimestampBegun = false;
	Microsoft::WRL::ComPtr<ID3D12QueryHeap> m_pStagePipelineHeap;
	Microsoft::WRL::ComPtr<ID3D12Resource> m_pStagePipelineReadback;
	const D3D12_QUERY_DATA_PIPELINE_STATISTICS *m_pStagePipelineData = nullptr;
	struct GpuReceiverKeyDX12
	{
		uint32_t view = 0;
		int width = 0, height = 0;
		bool lit = false;
	};
	GpuReceiverKeyDX12 m_GpuReceiverKey, m_GpuReceiverStack[kGpuStageStackSize] = {};
	GpuReceiverKeyDX12 m_StageReceiverKeys[kStageTimestampSlots] = {};
	uint32_t m_nGpuReceiverViewOrdinal = 0, m_nGpuReceiverDepth = 0, m_nGpuReceiverOverflowDepth = 0;
	uint32_t m_nGpuReceiverSpanDraws = 0, m_StageReceiverDraws[kStageTimestampSlots] = {};
	uint64_t m_StageTimestampFences[kStageTimestampSlots] = {};
	GpuStageDX12 m_StageTimestampCategories[kStageTimestampSlots] = {};
	uint32_t m_nStageTimestampSlot = 0;
	GpuStageDX12 m_GpuStage = GpuOther, m_GpuStageStack[kGpuStageStackSize] = {};
	uint32_t m_nGpuStageDepth = 0, m_nGpuStageOverflowDepth = 0;
	double m_flGpuStageSumMs[GpuStageCount] = {};
	uint64_t m_nGpuStagePSInvocations[GpuStageCount] = {};
	uint64_t m_nGpuStageCInvocations[GpuStageCount] = {};
	uint64_t m_nGpuStageCPrimitives[GpuStageCount] = {};
	GpuStageStatsDX12 m_GpuStageStats;

	// Submission worker: Close/ExecuteCommandLists/Signal and Present run there in FIFO order. The recording
	// owner calls FlushSubmissions before any other queue or swap-chain access.
	struct SubmitOpDX12
	{
		enum Kind
		{
			Execute,
			Present,
			Replay,
			Reset
		} kind = Execute;

		ID3D12GraphicsCommandList *list = nullptr;
		ID3D12CommandAllocator *allocator = nullptr;
		uint64_t value = 0;
		IDXGISwapChain3 *swap = nullptr;
		View *view = nullptr;
		UINT interval = 0, flags = 0;
		unsigned char *chunk = nullptr;
		size_t chunkBytes = 0;
		// Present only: frame id for the provider's present markers, and whether this present interpolates.
		uint32_t frameId = 0;
		bool framegenActive = false;
	};

	HRESULT RunSubmission( const SubmitOpDX12 &op );
	unsigned char *AcquireCommandChunk();
	void FlushCommandChunk( unsigned char *pChunk, size_t nBytes );
	static unsigned char *AcquireRecorderChunk( void *pContext );
	static void FlushRecorderChunk( void *pContext, unsigned char *pChunk, size_t nBytes );
	void ReleaseCommandChunks();
	static constexpr int kMaxCommandChunks = 32;
	CCommandRecorderDX12 m_Recorder;
	CThreadFastMutex m_ChunkMutex;
	CUtlVector<unsigned char *> m_FreeChunks, m_AllChunks;
	static uintp SubmitThreadMain( void *pParam );
	void StartSubmitThread();
	void StopSubmitThread();
	void EnqueueSubmission( const SubmitOpDX12 &op );
	ThreadHandle_t m_hSubmitThread = nullptr;
	CThreadEvent m_SubmitWorkEvent, m_SubmitDoneEvent;
	CInterlockedInt m_bSubmitExit;
	CInterlockedUInt m_nSubmitHead, m_nSubmitTail;
	CInterlockedInt m_nSubmitError; // HRESULT of the first failed queued operation
	SubmitOpDX12 m_SubmitOps[16];
	bool CreateView( View &view, HWND hWnd, int nWidth, int nHeight );
	void QueryDisplayHdr( View &view );
	bool ResizeView( View &view, int nWidth, int nHeight );
	bool CreateViewTargets( View &view );
	bool CreateFrameObjects();
	bool BeginRecording();
	void FailDevice( const char *pszOperation, HRESULT hr );
	bool CheckDevice( const char *pszOperation, HRESULT hr );
	void ReleaseViews();
	// Releases every view's targets and provider chain and creates them again (mode/kind changes; GPU idle).
	bool RecreateViews();
	// Switches the frame-generation kind on the provider, adopting its queue and present format (GPU idle, no
	// swap chain alive). Returns the provider's result.
	bool SelectFrameGen( FrameGenKindDX12 kind, uint32_t nMultiplier, bool bHudless );
	Microsoft::WRL::ComPtr<ID3D12Device> m_pDevice;
	Microsoft::WRL::ComPtr<IDXGIFactory6> m_pFactory;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_pQueue;
	Microsoft::WRL::ComPtr<ID3D12InfoQueue> m_pInfoQueue;

	struct FrameContext
	{
		Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
		Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> list;
		uint64_t fence = 0;
		CUtlVector<ID3D12Resource *> retained;
	};

	FrameContext m_Frames[3];
	uint32_t m_nFrameIndex = 0;
	bool m_bRecording = false, m_bFailed = false;
	bool m_bChangingMode = false;
	CInterlockedUInt m_nOwnerThread; // thread id
	CThreadFastMutex m_TextureDeletionMutex;
	CUtlVector<uintptr_t> m_PendingTextureDeletions;
	CInterlockedInt m_nPendingTextureDeletionCount;
	Microsoft::WRL::ComPtr<ID3D12Fence> m_pFence;
	CUtlVector<View *> m_Views;
	// Owned; created on first use and deleted in the destructor.
	CVertexBufferDX12 *m_pDynamicVertices[64] = {};
	CIndexBufferDX12 *m_pDynamicIndices[4] = {};
	View *m_pCurrentView = nullptr;
	HANDLE m_hFenceEvent = nullptr;
	uint64_t m_nFenceValue = 0, m_nFrameSyncFence = 0;
	HMODULE m_hSignerModule = nullptr;
	SignDxbcFnDX12 m_pfnSigner = nullptr;
	UINT m_nRtvStride = 0;
	UINT m_nDsvStride = 0;
	int m_nAdapterIndex = -1;
	int m_nWidth = 0, m_nHeight = 0;
	int m_nSampleCount = 1, m_nSampleQuality = 0;
	int m_nBackBufferCount = 2;
	void *m_pWindow = nullptr;
	char m_szDisplayDeviceName[128] = {};
	float m_flGamma = 2.2f, m_flGammaMin = 0.0f, m_flGammaMax = 255.0f, m_flGammaExponent = 2.2f;
	bool m_bGammaTV = false;
	bool m_bWaitForVsync = true, m_bWindowed = true;
	bool m_bAllowTearing = false;
	DXGI_FORMAT m_PresentFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
	CInterlockedInt m_nPresentFormat = static_cast<int>( DXGI_FORMAT_R16G16B16A16_FLOAT );
	CInterlockedInt m_nHdrDisplayStatus;
	// Frame generation (F5): pending kind switch applied at Present's tail, present ids/serials for the providers.
	CFrameGenDX12 m_FrameGen;
	CLightingDX12 m_Lighting;
	CHighresLightmapsDX12 m_Highres;

	struct PendingSelectDX12
	{
		FrameGenKindDX12 kind = FrameGenKindDX12::None;
		uint32_t multiplier = 2;
		bool hudless = true;
		bool valid = false;
	} m_PendingSelect;

	bool m_bSelectFailed = false;
	uint32_t m_nLastPresentId = 0;
	uint64_t m_nPresentSerial = 0;
};

class CShaderDeviceMgrDX12 final : public IShaderDeviceMgr
{
public:
	CShaderDeviceMgrDX12();
	~CShaderDeviceMgrDX12();
	bool Connect( CreateInterfaceFn factory ) override;
	void Disconnect() override;
	void *QueryInterface( const char *pszName ) override;
	InitReturnVal_t Init() override;
	void Shutdown() override;

	int GetAdapterCount() const override { return m_pAdapters.Count(); }

	void GetAdapterInfo( int nAdapter, MaterialAdapterInfo_t &info ) const override;
	bool GetRecommendedConfigurationInfo( int nAdapter, int nDXLevel, KeyValues *pConfiguration ) override;
	int GetModeCount( int nAdapter ) const override;
	void GetModeInfo( ShaderDisplayMode_t *pInfo, int nAdapter, int nMode ) const override;
	void GetCurrentModeInfo( ShaderDisplayMode_t *pInfo, int nAdapter ) const override;
	bool SetAdapter( int nAdapter, int nFlags ) override;
	CreateInterfaceFn SetMode( void *hWnd, int nAdapter, const ShaderDeviceInfo_t &mode ) override;
	void AddModeChangeCallback( ShaderModeChangeCallbackFunc_t func ) override;
	void RemoveModeChangeCallback( ShaderModeChangeCallbackFunc_t func ) override;
	void NotifyModeChange();

	CShaderDeviceDX12 *Device() { return &m_Device; }

	const CUtlVector<IDXGIAdapter1 *> &Adapters() const { return m_pAdapters; }

	IShaderUtil *HostShaderUtil() const { return m_pShaderUtil; }

	IFileSystem *HostFileSystem() const { return m_pFilesystem; }

private:
	CUtlVector<IDXGIAdapter1 *> m_pAdapters; // each holds one reference, released by ReleaseAdapters
	CUtlVector<MaterialAdapterInfo_t> m_AdapterInfo;
	CUtlVector<DXSupportCapsDX12> m_AdapterCaps;
	CUtlVector<CUtlVector<ShaderDisplayMode_t>> m_AdapterModes;
	CUtlVector<ShaderModeChangeCallbackFunc_t> m_Callbacks;
	CDXSupportDX12 m_DxSupport;
	CShaderDeviceDX12 m_Device;
	CreateInterfaceFn m_pfnHostFactory = nullptr;
	IFileSystem *m_pFilesystem = nullptr;
	IShaderUtil *m_pShaderUtil = nullptr;
	int m_nCurrentAdapter = -1;
};

extern CShaderDeviceMgrDX12 *g_pShaderDeviceMgrDX12;
extern CShaderDeviceDX12 *g_pShaderDeviceDX12;

} // namespace shaderapidx12

#endif // SHADERDEVICE_DX12_H
