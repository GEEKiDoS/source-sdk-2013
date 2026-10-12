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
//            motion vectors -> DispatchSSR -> EndView
//          Once per main camera view, around all of its opaque brackets: BeginVolumetrics ... DrawVolumetrics.
//
//===========================================================================//
#ifndef ISHADERAPIDX12SCREENSPACE_H
#define ISHADERAPIDX12SCREENSPACE_H
#ifdef _WIN32
#pragma once
#endif

#include "tier1/interface.h"

#define SHADERAPIDX12_SCREENSPACE_INTERFACE_VERSION "ShaderAPIDX12ScreenSpace_001"

// Byte-identical to HLSL cbuffer DX12ScreenSpaceView (b0 space6, native_src/screenspace_view.hlsli).
struct alignas( 16 ) DX12ScreenSpaceViewConstants	// 208 bytes
{
	float	worldToClip[16];		// row-major, clip = mul( worldToClip, float4( P, 1 ) ); backend-filled (native raster epilogue + jitter)
	float	clipToWorld[16];		// inverse of worldToClip; backend-filled
	float	eyeLength[4];			// eye xyz, contact length (Source units)
	float	forwardThickness[4];	// unit camera forward xyz, contact thickness (Source units)
	float	viewport[4];			// scene-pixel x, y, width, height
	uint32	control[4];				// contact enabled, scene sample count, contact steps, SSR capture enabled; backend-authoritative x/y/w
	float	depth[4];				// depth-clear value (backend), normal bias 0.25, first ray distance 0.5, edge fade 8 pixels
};

struct DX12SSRSettings				// 16 bytes
{
	uint32	quality;				// 0 low, 1 balanced, 2 high
	float	maxDistance;			// Source units
	float	thickness;				// Source units
	uint32	reserved;				// 0
};

struct DX12ScreenSpaceViewDesc
{
	DX12ScreenSpaceViewConstants constants;
	DX12SSRSettings ssr;
};

// Snapshot of the volumetric controls for one main camera view. 48 bytes; the first 48 bytes of HLSL cbuffer
// DX12Volumetric (b1 space6), which appends float4 outputScale.
struct alignas( 16 ) DX12VolumetricSettings
{
	float	medium[4];				// local/projected lights: density (scattering coefficient, 1/unit), strength, HG g, maxDistance
	float	sunMedium[4];			// sun: density, strength, HG g, maxDistance
	uint32	control[4];				// steps per light segment, sun enabled, master enabled, noise index (0 static dither; framecount & 63 under a temporal upscaler)
};

struct alignas( 16 ) DX12VolumetricConstants	// 64 bytes, HLSL cbuffer DX12Volumetric (b1 space6)
{
	DX12VolumetricSettings settings;
	float	outputScale[4];			// native LINEAR_LIGHT_SCALE (m_ToneScale.x), 0, 0, 0
};

static_assert( sizeof( DX12ScreenSpaceViewConstants ) == 208, "DX12ScreenSpaceViewConstants must match HLSL DX12ScreenSpaceView" );
static_assert( sizeof( DX12SSRSettings ) == 16, "DX12SSRSettings layout" );
static_assert( sizeof( DX12VolumetricSettings ) == 48, "DX12VolumetricSettings layout" );
static_assert( sizeof( DX12VolumetricConstants ) == 64, "DX12VolumetricConstants must match HLSL DX12Volumetric" );

abstract_class IShaderAPIDX12ScreenSpace
{
public:
	// Starts an opaque bracket of an eligible mono main view. Client supplies eye/forward, contact length/thickness,
	// viewport, control.x contact enable, control.z contact steps, control.w SSR capture and the SSR settings; the
	// backend fills both matrices, the actual sample count and depth-clear value. An ineligible/zero-sized view or a
	// failed inversion disables every effect of the bracket.
	virtual void BeginView( const DX12ScreenSpaceViewDesc &view ) = 0;
	// Private normal-geometry depth replay for contact rays; draws between the two markers write only private depth.
	virtual void BeginContactDepth() = 0;
	virtual void EndContactDepth() = 0;
	// After the G-buffer pass and motion vectors: trace and add the signed SSR correction to the scene.
	virtual void DispatchSSR() = 0;
	// Ends the bracket: contacts off; captures the current main camera/lighting when a volume scope is active.
	virtual void EndView() = 0;
	// Once per main camera view, before its first opaque bracket.
	virtual void BeginVolumetrics( const DX12VolumetricSettings &settings ) = 0;
	// After the last bracket of that view: adds in-scattering over the completed scene depth, then ends the scope.
	virtual void DrawVolumetrics() = 0;
};

#endif // ISHADERAPIDX12SCREENSPACE_H
