//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Extension interface of shaderapidx12.dll for features the stock IShaderAPI has no words for: frame
//          generation (DLSS-G / FSR FG / XeSS-FG), NVIDIA Reflex, HDR output and generic compute dispatch.
//
//          Obtained from the renderer's factory: Sys_GetFactory( "shaderapidx12" )( SHADERAPIDX12_INTERFACE_VERSION, NULL ).
//          A renderer without it returns NULL; callers must treat that as "features unavailable".
//
//          Settings are plain stores: thread-safe, callable from cvar callbacks, taken over by the renderer at its
//          next frame boundary. Status queries are readable from any thread. Per-frame work that must stay ordered
//          with the draw stream (the eligible view, frame id and dispatch) still goes through the render context.
//
//===========================================================================//
#ifndef ISHADERAPIDX12_H
#define ISHADERAPIDX12_H
#ifdef _WIN32
#pragma once
#endif

#include "tier1/interface.h"
#include "shaderapi/ishaderapi.h"

// _004: frame generation, reflex, HDR output, adapter performance tier and runtime GPU timing.
// _005: GetVideoMemory.
#define SHADERAPIDX12_INTERFACE_VERSION "ShaderAPIDX12_005"

static constexpr int SHADERAPIDX12_GPU_TIMING_MAX_EVENTS = 16;
static constexpr int SHADERAPIDX12_GPU_TIMING_NAME_LENGTH = 64;

struct ShaderAPIDX12GpuTimingEvent_t
{
	char name[SHADERAPIDX12_GPU_TIMING_NAME_LENGTH];
	float ms;
	unsigned char depth;
	unsigned char reserved[3];
};

struct ShaderAPIDX12GpuTiming_t
{
	ShaderAPIDX12GpuTimingEvent_t events[SHADERAPIDX12_GPU_TIMING_MAX_EVENTS];
	unsigned int count;
	float gpuFrameMs;
	unsigned int sampleFrames;
	bool available;
};

// AdapterPerformanceTier(): coarse class of the rendering adapter, used to pick default quality on first run.
enum ShaderAPIDX12AdapterTier_t
{
	SHADERAPIDX12_ADAPTER_TIER_UNKNOWN = -1, // no device yet
	SHADERAPIDX12_ADAPTER_TIER_LOW = 0,      // integrated/UMA, software, < 6 GB, or no mesh shaders / DXR 1.1 (Pascal, GTX 16, RDNA1)
	SHADERAPIDX12_ADAPTER_TIER_HIGH = 1,
};

enum ShaderAPIDX12HdrDisplayType_t
{
	SHADERAPIDX12_HDR_DISPLAY_UNKNOWN = 0,
	SHADERAPIDX12_HDR_DISPLAY_SDR = 1,
	SHADERAPIDX12_HDR_DISPLAY_HDR = 2,
};

#define SHADERAPIDX12_HDR_DISPLAY_TYPE( status ) ( ( status ) & 0xff )
#define SHADERAPIDX12_HDR_DISPLAY_MAX_NITS( status ) ( ( ( status ) >> 8 ) & 0xffff )

enum ShaderAPIDX12ComputeResourceKind_t
{
	SHADERAPIDX12_COMPUTE_RESOURCE_NONE = 0,
	SHADERAPIDX12_COMPUTE_RESOURCE_TEXTURE,
	SHADERAPIDX12_COMPUTE_RESOURCE_STANDARD_TEXTURE,
	SHADERAPIDX12_COMPUTE_RESOURCE_SCENE_DEPTH,
	// The PBR G-buffer targets (R16G16_UNORM octahedral normal, R8G8B8A8_UNORM F0.rgb + roughness), scene-sized and
	// scene-multisampled (Texture2DMS when SceneSampleCount() > 1). Before the first G-buffer pass the view is null.
	SHADERAPIDX12_COMPUTE_RESOURCE_PBR_NORMALS,
	SHADERAPIDX12_COMPUTE_RESOURCE_PBR_SPECULAR,
	// Stencil plane of the scene depth (X24_TYPELESS_G8_UINT, plane 1). Texture2D<uint2> or Texture2DMS<uint2>, value in .y.
	SHADERAPIDX12_COMPUTE_RESOURCE_SCENE_STENCIL,
	// SSR payload G-buffer targets (scene-sized/multisampled like the PBR targets; R16G16B16A16_FLOAT, or
	// R11G11B10_FLOAT at r_color_precision 1): environment contribution E (rgb), sampled-scene reflection response
	// K (rgb; zero for a non-writer, alpha 1 for a fresh writer when the format has one) and direct GGX specular S (rgb).
	SHADERAPIDX12_COMPUTE_RESOURCE_PBR_ENVIRONMENT,
	SHADERAPIDX12_COMPUTE_RESOURCE_PBR_SSR_RESPONSE,
	SHADERAPIDX12_COMPUTE_RESOURCE_PBR_SSR_SPECULAR,
	// The direct-specular light distance D in .a: E's alpha at r_color_precision 0, else an R16_FLOAT target read
	// through a red-to-alpha view.
	SHADERAPIDX12_COMPUTE_RESOURCE_PBR_SSR_DISTANCE,
	// Private contact replay depth; Texture2D or Texture2DMS matching the scene.
	SHADERAPIDX12_COMPUTE_RESOURCE_SCREEN_CONTACT_DEPTH,
	SHADERAPIDX12_COMPUTE_RESOURCE_SCENE_COLOR,
	SHADERAPIDX12_COMPUTE_RESOURCE_TEXTURE_SRGB,
};

struct ShaderAPIDX12ComputeResource_t
{
	int m_nKind;
	ShaderAPITextureHandle_t m_hTexture;
	int m_nStandardTexture;
	int m_nMip;
	int m_nMipCount;
};

#define SHADERAPIDX12_COMPUTE_MAX_SRVS      16
#define SHADERAPIDX12_COMPUTE_MAX_UAVS      8
// Must match the nativeshaderpack_dx12 compute cbuffer contract; raising it does not change the dispatch layout.
#define SHADERAPIDX12_COMPUTE_MAX_CONSTANTS 512

struct ShaderAPIDX12ComputeDispatch_t
{
	const char *m_pShaderName;
	int m_nStaticIndex;
	int m_nDynamicIndex;
	ShaderAPIDX12ComputeResource_t m_Srv[SHADERAPIDX12_COMPUTE_MAX_SRVS];
	ShaderAPIDX12ComputeResource_t m_Uav[SHADERAPIDX12_COMPUTE_MAX_UAVS];
	const void *m_pConstants;
	int m_nConstantBytes;
	int m_nGroupsX, m_nGroupsY, m_nGroupsZ;
};

// _002: SHADERAPIDX12_COMPUTE_MAX_SRVS 8 -> 16 (ShaderAPIDX12ComputeDispatch_t layout), PBR_SSR_SPECULAR resource.
// _003: SetAmbientOcclusionTerm.
// _004: SetAmbientOcclusionTerm bViewModel.
#define SHADERAPIDX12_COMPUTE_INTERFACE_VERSION "ShaderAPIDX12Compute_004"

// SetFrameGeneration modes.
enum ShaderAPIDX12FrameGenMode_t
{
	SHADERAPIDX12_FRAMEGEN_OFF = 0,
	SHADERAPIDX12_FRAMEGEN_AUTO,    // DLSS-G on RTX, else FSR frame generation, else XeSS-FG
	SHADERAPIDX12_FRAMEGEN_DLSSG,
	SHADERAPIDX12_FRAMEGEN_FSR,
	SHADERAPIDX12_FRAMEGEN_XEFG,
};

// FrameGenerationStatus(): 0 off; active = SHADERAPIDX12_FRAMEGEN_STATUS_ACTIVE | (kind << 8) | (generated << 16)
// with kind 1 DLSS-G, 2 FSR, 3 XeSS-FG; negative codes are fail-closed (the frame presents in pass-through).
enum ShaderAPIDX12FrameGenStatus_t
{
	SHADERAPIDX12_FRAMEGEN_STATUS_OFF = 0,
	SHADERAPIDX12_FRAMEGEN_STATUS_ACTIVE = 1,
	SHADERAPIDX12_FRAMEGEN_STATUS_NO_PROVIDER = -1,     // no provider for the mode on this adapter / runtime
	SHADERAPIDX12_FRAMEGEN_STATUS_CREATE_FAILED = -2,   // provider or swap chain creation failed
	SHADERAPIDX12_FRAMEGEN_STATUS_MSAA = -3,            // multisampled scene
	SHADERAPIDX12_FRAMEGEN_STATUS_WRONG_TARGET = -4,    // dispatch outside the back buffer / exclusive fullscreen / no eligible view
	SHADERAPIDX12_FRAMEGEN_STATUS_NO_MOTION = -5,       // no scene-sized motion-vector resolve this frame
	SHADERAPIDX12_FRAMEGEN_STATUS_RUNTIME_ERROR = -6,   // provider reported a runtime error; history reset
};
#define SHADERAPIDX12_FRAMEGEN_STATUS_KIND( status )      ( ( ( status ) >> 8 ) & 0xff )
#define SHADERAPIDX12_FRAMEGEN_STATUS_GENERATED( status ) ( ( ( status ) >> 16 ) & 0xff )

// SetReflexMode modes and ReflexStatus() values.
enum ShaderAPIDX12ReflexMode_t
{
	SHADERAPIDX12_REFLEX_OFF = 0,
	SHADERAPIDX12_REFLEX_LOW_LATENCY,
	SHADERAPIDX12_REFLEX_LOW_LATENCY_BOOST,
};
enum ShaderAPIDX12ReflexStatus_t
{
	SHADERAPIDX12_REFLEX_STATUS_OFF = 0,
	SHADERAPIDX12_REFLEX_STATUS_ACTIVE = 1,
	SHADERAPIDX12_REFLEX_STATUS_UNAVAILABLE = -1,       // not an RTX adapter, Streamline missing / unsigned, or Reflex unsupported
};

// LatencyMarker stages, in frame order. The sleep (Reflex or XeLL) happens inside SIMULATION_START, so the client
// issues it at the start of its frame, before input is sampled; the renderer adds the present markers itself.
enum ShaderAPIDX12LatencyMarker_t
{
	SHADERAPIDX12_MARKER_SIMULATION_START = 1,
	SHADERAPIDX12_MARKER_SIMULATION_END = 2,
	SHADERAPIDX12_MARKER_RENDER_SUBMIT_START = 3,
	SHADERAPIDX12_MARKER_RENDER_SUBMIT_END = 4,
};

abstract_class IShaderAPIDX12
{
public:
	// Frame generation: nMode as ShaderAPIDX12FrameGenMode_t, nMultiplier frames shown per rendered frame (2..4;
	// FSR is always 2, DLSS-G/XeSS-FG clamp to the GPU's maximum), bHudless hands the providers a HUD-less copy of
	// the scene. Mode 0 releases the provider and restores the renderer's own swap chain. Frame generation needs a
	// windowed or borderless mode and a single-sample scene.
	virtual void SetFrameGeneration( int nMode, int nMultiplier, bool bHudless ) = 0;
	// NVIDIA Reflex, independent of frame generation (DLSS-G forces at least low latency while it runs; XeSS-FG
	// hands latency reduction to Intel XeLL instead).
	virtual void SetReflexMode( int nMode ) = 0;
	// Frame-rate cap the game enforces itself (fps_max; 0 uncapped), so the Reflex / XeLL limiter agrees with it.
	virtual void SetFrameRateLimit( float flFps ) = 0;

	// Latency marker for the client frame nFrameId (28 bits used); call from the simulation thread, never through
	// the render context. A no-op unless Reflex or a latency-aware frame generator is running.
	virtual void LatencyMarker( int nMarker, unsigned int nFrameId ) = 0;

	// Backend-owned status, see ShaderAPIDX12FrameGenStatus_t.
	virtual int FrameGenerationStatus() = 0;
	// Frames the last present put on screen: 1 without frame generation, 2..4 with it (0 before the first present).
	virtual int FramesShown() = 0;
	// See ShaderAPIDX12ReflexStatus_t.
	virtual int ReflexStatus() = 0;
	// Why the last SetReflexMode / frame-generation request did not take; "" when it did.
	virtual const char *LastError() = 0;
	// HDR display output. Present() applies flUiNits / 80 to an FP16 scRGB swap chain when enabled.
	virtual void SetHdrOutput( bool bEnable, float flUiNits ) = 0;
	// Bits 0-7: display type; bits 8-23: display MaxLuminance in nits.
	virtual int HdrDisplayStatus() = 0;
	// True when the swap chain is FP16 scRGB.
	virtual bool HdrOutputCapable() = 0;
	// Enables the optional timestamp/readback collection used by cl_showfps 3. When false, no timing queries are recorded.
	virtual void SetGpuTimingEnabled( bool bEnabled ) = 0;
	// Copies the latest approximately half-second inclusive PIX-event snapshot. The caller owns the fixed-size output.
	virtual bool GetGpuTiming( ShaderAPIDX12GpuTiming_t &timing ) = 0;
	// See ShaderAPIDX12AdapterTier_t.
	virtual int AdapterPerformanceTier() = 0;
	// This process's use of the adapter's local video memory and the OS budget for it (DXGI QueryVideoMemoryInfo),
	// and the adapter's dedicated video memory, in bytes. False when unavailable. Callable from any thread.
	virtual bool GetVideoMemory( uint64 &nUsage, uint64 &nBudget, uint64 &nDedicated ) = 0;

};
abstract_class IShaderAPIDX12Compute
{
public:
	virtual bool Dispatch( const ShaderAPIDX12ComputeDispatch_t &dispatch ) = 0;
	virtual int SceneSampleCount() = 0;
	// The main view's ambient-occlusion term for this frame, in call order with Dispatch: viewport-local texels
	// [0, nWidth) x [0, nHeight) of hTerm cover the scene viewport; bPacked when the stored value is ao / 1.5 (the
	// GTAO main pass without a denoise pass); bViewModel when it was computed with the first-person viewmodels' depth
	// (their pixels then carry their own term, which the viewmodel draws apply). SSR darkens reflected hits with it;
	// the last call of a frame wins.
	virtual void SetAmbientOcclusionTerm( ShaderAPITextureHandle_t hTerm, int nWidth, int nHeight, bool bPacked, bool bViewModel ) = 0;
};

#endif // ISHADERAPIDX12_H
