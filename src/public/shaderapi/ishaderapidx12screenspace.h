//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Extension interface of shaderapidx12.dll for the screen-space effects that run inside the main opaque
//          bracket (contact shadows, screen-space reflections) and the final raymarched volumetric lighting pass.
//
//          Obtained from the renderer's factory: Sys_GetFactory( "shaderapidx12" )( SHADERAPIDX12_SCREENSPACE_INTERFACE_VERSION, NULL ).
//          A renderer without it returns NULL; callers must treat that as "effects unavailable".
//
//          Every method is an ordered marker: it flushes buffered draws and is queued on the material call queue
//          (replayed directly when there is none), so it executes in draw-stream order on the recording owner.
//          Arguments are copied; no caller pointer is retained.
//
//          Bracket order inside one opaque main-view pass:
//            BeginView -> [BeginContactDepth -> replayed depth geometry -> EndContactDepth] -> G-buffer pass ->
//            motion vectors -> [BeginSkybox -> ordinary sky draw -> EndSkybox] -> GTAO compute -> DispatchSSR -> EndView
//          Once per main camera view, around all of its opaque brackets: BeginVolumetrics ... DrawVolumetrics.
//
//===========================================================================//
#ifndef ISHADERAPIDX12SCREENSPACE_H
#define ISHADERAPIDX12SCREENSPACE_H
#ifdef _WIN32
#pragma once
#endif

#include "tier1/interface.h"

#define SHADERAPIDX12_SCREENSPACE_INTERFACE_VERSION "ShaderAPIDX12ScreenSpace_012"

// Byte-identical to HLSL cbuffer DX12ScreenSpaceView (b0 space6, native_src/screenspace_view.hlsli).
struct alignas( 16 ) DX12ScreenSpaceViewConstants	// 256 bytes
{
	float	worldToClip[16];		// row-major, clip = mul( worldToClip, float4( P, 1 ) ); backend-filled (native raster epilogue + jitter)
	float	clipToWorld[16];		// inverse of worldToClip; forward viewmodel contacts reuse this lane for their projection
	float	eyeLength[4];			// eye xyz; forward SSR ray length
	float	forwardThickness[4];	// camera forward xyz; forward SSR depth thickness
	float	viewport[4];			// scene-pixel x, y, width, height
	uint32	control[4];				// reserved; scene samples or forward AO size; forward flags; SSR capture
	float	depth[4];				// depth-clear value (backend), normal bias 0.25, first ray distance 0.5, edge fade 8 pixels
	float	contactParams[4];		// Bend surface thickness (relative depth), edge threshold, contrast, reserved
	float	contactDepth[4];		// depth projection and valid raw range for the current receiver layer
	uint32	contactControl[4];		// mask scope: 0 off, 1 world, 2 viewmodel; samples; trace pixels; debug
};

struct DX12SSRSettings				// 20 bytes
{
	uint32	quality;				// 0 low, 1 balanced, 2 high
	float	maxDistance;			// Source units
	float	thickness;				// Source units
	float	resolutionScale;		// trace scale per axis relative to the quality profile, [0.25, 1]
	uint32	forwardSteps;			// march steps of the forward reflections of translucents and viewmodels, [0, 32]; 0 off
};

struct DX12ScreenSpaceViewDesc
{
	DX12ScreenSpaceViewConstants constants;
	DX12SSRSettings ssr;
	float bounceScale;
	uint32 bounceDebug;
};

// Snapshot of the volumetric controls for one main camera view. 64 bytes; the first 64 bytes of HLSL cbuffer
// DX12Volumetric (b1 space6), which appends float4 outputScale.
struct alignas( 16 ) DX12VolumetricSettings
{
	float	medium[4];				// local/projected lights: density (scattering coefficient, 1/unit), strength, HG g, maxDistance
	float	sunMedium[4];			// sun: density, strength, HG g, maxDistance
	float	beamMedium[4];			// paired map spotlights: density, strength, 0, 0; shares medium.zw
	uint32	control[4];				// steps per light segment, sun enabled, master enabled, rotating noise frame (framecount & 63)
};

struct alignas( 16 ) DX12VolumetricConstants	// 80 bytes, HLSL cbuffer DX12Volumetric (b1 space6)
{
	DX12VolumetricSettings settings;
	float	outputScale[4];			// native LINEAR_LIGHT_SCALE (m_ToneScale.x), 0, 0, 0
};

static_assert( sizeof( DX12ScreenSpaceViewConstants ) == 256, "DX12ScreenSpaceViewConstants must match HLSL DX12ScreenSpaceView" );
static_assert( sizeof( DX12SSRSettings ) == 20, "DX12SSRSettings layout" );
static_assert( sizeof( DX12VolumetricSettings ) == 64, "DX12VolumetricSettings layout" );
static_assert( sizeof( DX12VolumetricConstants ) == 80, "DX12VolumetricConstants must match HLSL DX12Volumetric" );

abstract_class IShaderAPIDX12ScreenSpace
{
public:
	// Starts an opaque bracket of an eligible mono main view. Client supplies eye/forward, viewport, contact and
	// SSR settings; the backend fills the matrices, sample count and depth clear. An invalid camera disables the bracket.
	virtual void BeginView( const DX12ScreenSpaceViewDesc &view ) = 0;
	// Copy the current scene depth; if there was no opaque prepass, replay normal geometry into the private copy.
	virtual void BeginContactDepth( bool hasViewModelDepth ) = 0;
	virtual void EndContactDepth() = 0;
	// After the G-buffer pass and motion vectors: trace and add the signed SSR correction to the scene.
	virtual void DispatchSSR() = 0;
	// Ends the bracket: contacts off; captures the current main camera/lighting when a volume scope is active.
	virtual void EndView() = 0;
	// Once per main camera view, before its first opaque bracket.
	virtual void BeginVolumetrics( const DX12VolumetricSettings &settings ) = 0;
	// After the last bracket of that view: adds in-scattering over the completed scene depth, then ends the scope.
	virtual void DrawVolumetrics() = 0;
	virtual void DrawBounce() = 0;
	virtual void DrawViewModelBounce() = 0;
	// Replays the ordinary main-view opaque draws into scene depth, retaining their VS/LOD/alpha coverage.
	// Only opaque depth writers are admitted; the backend masks color and removes the PS only when coverage is fixed.
	// Returns whether to replay world/models as well as viewmodels. Single-submit
	// is only offered by an immediate caller that needs no depth until after color.
	virtual bool BeginDepthPrepass( bool allowSingleSubmit ) = 0;
	virtual void EndDepthPrepass( bool resolvedScene ) = 0;
	// Renders ordinary sky only through untouched main-view depth pixels, using reserved stencil bit 0x40.
	// End restores their far depth and removes the mask; main scene depth and stencil bit 0x80 stay intact.
	virtual void BeginSkybox() = 0;
	virtual void EndSkybox() = 0;
	// After the viewmodel projection and depth range are set, reuse its opaque prepass or replay it in private.
	// EndContactDepth makes the result readable; EndViewModelContacts closes the receiver scope.
	virtual void BeginViewModelContacts( bool reuseMainDepth ) = 0;
	virtual void EndViewModelContacts() = 0;
};

#endif // ISHADERAPIDX12SCREENSPACE_H
