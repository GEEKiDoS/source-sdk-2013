//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Extension interface of shaderapidx12.dll for features the stock IShaderAPI has no words for: frame
//          generation (DLSS-G / FSR FG / XeSS-FG), NVIDIA Reflex and the latency markers they need.
//
//          Obtained from the renderer's factory: Sys_GetFactory( "shaderapidx12" )( SHADERAPIDX12_INTERFACE_VERSION, NULL ).
//          A renderer without it returns NULL; callers must treat that as "features unavailable".
//
//          Settings are plain stores: thread-safe, callable from cvar callbacks, taken over by the renderer at its
//          next frame boundary. Status queries are readable from any thread. Per-frame work that must stay ordered
//          with the draw stream (the eligible view, frame id and dispatch) still goes through the render context
//          (INT_RENDERPARM_DX12_FRAMEGEN_* in renderparm.h).
//
//===========================================================================//
#ifndef ISHADERAPIDX12_H
#define ISHADERAPIDX12_H
#ifdef _WIN32
#pragma once
#endif

#include "tier1/interface.h"

#define SHADERAPIDX12_INTERFACE_VERSION "ShaderAPIDX12_001"

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
};

#endif // ISHADERAPIDX12_H
