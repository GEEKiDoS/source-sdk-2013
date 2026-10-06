//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Client runtime shadow-map manager (plan steps 5, 6, 8): map
//          admission, retained private depth targets (local pages, cascade
//          atlas, static sun map), per-receiver-view scheduling of caster views
//          (sun cascades, narrow spots, cube faces), static/dynamic depth
//          caches, CPU 16x16 tile light lists and the nested lighting packets
//          handed to IShaderAPIDX12Lighting.
//
//          Implementation: game/client/shadowmaps_dx12.cpp + viewrender
//          integration (ShadowViews). Lifecycle calls below are made by the
//          main-owned glue in cdll_client_int.cpp; receiver-view calls by
//          viewrender.cpp.
//
//=============================================================================//
#ifndef SHADOWMAPS_DX12_H
#define SHADOWMAPS_DX12_H
#ifdef _WIN32
#pragma once
#endif

#include "tier1/convar.h"
#include "tier1/utlvector.h"
#include "mathlib/vector.h"
#include "view_shared.h"
#include "hlight_bsp.h"
#include "shaderapi/ishaderapidx12lighting.h"

// ConVars (defined in shadowmaps_dx12.cpp)
extern ConVar r_csm_distance;			// archived, default 4096, clamped [128, 32768]
extern ConVar r_shadowmap_filter;		// archived, default 0, clamped [0,1]: 0 PCF, 1 PCSS; snapshotted per view packet
extern ConVar r_shadowmap_debug;		// cheat, default 0: 0 normal, 1 cascade, 2 visibility, 3 local page/face, 4 caster bounds, 5/6 PCSS diagnostics
extern ConVar r_shadowmap_autoexec;	// cheat, default "": acceptance automation; ';'-separated cfg list, item i exec'd on the
										// (i+1)*60th playable main view of each map (no loading plaques/menu backgrounds; independent of `wait`)
// ConCommand r_shadowmap_stats: one-shot last-completed-view counters (selected/relevant lights, pages, casters,
// static/dynamic redraws, CSR entries, residency failures) and current-generation GetSunVisibilityStats fields
// (receiverFaces, mappedFaces, pages, unresolvedDraws; pages here means actual R8 companion pages).
// ConCommand r_shadowmap_report <path>: writes JSON for the fixture runner (--runtime-report) from actual renderer
// data of the last completed main view: {"schema":1,"counters":{...view-counter keys printed by r_shadowmap_stats...},
// "sunVisibility":{"receiverFaces":n,"mappedFaces":n,"pages":n,"unresolvedDraws":n} (actual current-generation backend
// snapshot when the report is requested), "casterValidation":[{"light":id,"testedCount":n,"reportedCount":n,
// "exhaustiveCount":n,"mismatchCount":n,"exceeds4096":bool}]
// (EnumerateShadowCasters vs EnumerateShadowCastersExhaustive per relevant light/cascade/static sun),
// "detailCases":[{"name":"fast"|"ordinary","fast":bool,"matched":bool,"count":n}] (from IDetailObjectSystem::GetShadowReport),
// "mapState":{"featureMap":bool,"runtimeActive":bool,"mode":n,"selectedLights":n,"sun":id,"error":"..."}}.
// ConCommand r_shadowmap_spawn_renderables <count> [model] [ox oy oz] [gridX gridY] [sx sy sz] (cheat):
// replaces the fixture with client-only, opaque C_BaseAnimating renderables (no network edicts). Defaults:
// models/props_junk/garbage_carboard001a.mdl, origin (-448,-448,8), grid (17,241), spacing (2,2,0).
// Positions are origin + ((i % gridX) * sx, (i / gridX) * sy, sz); rows continue until count is reached.
// Count 0 destroys the fixture; level/client shutdown also releases all surviving fixture handles.

//-----------------------------------------------------------------------------
// Map state (owned by the manager; read by detailobjectsystem/viewrender/report)
//-----------------------------------------------------------------------------
struct ShadowMapClientMapState
{
	uint32						mapGeneration;			// increments per admitted feature map; 0 = none
	uint64						nativeMapGeneration;	// exact backend bridge generation; 0 = ordinary
	bool						featureMap;				// validated rshd-v4 manifest (any runtimeModeMask bit)
	bool						runtimeActive;			// engine-selected highres mode admitted
	bool						failed;					// feature map rejected with an explicit error
	int							selectedMode;			// SHADOWMAP_MODE_* actually rendered (engine HDR selection)
	char						error[256];
	CUtlVector<uint8>			rshdBytes;				// owned copy of the sidecar payload (engine->LoadGameLump)
	hlight::ManifestView			manifest;				// selected-light metadata borrowing rshdBytes; no receiver geometry
	const ShadowMapLightDisk	*lights;				// selected mode's records (sun first when present)
	uint32						lightCount;
	int							sunLightIndex;			// -1 none
	Vector						worldMins, worldMaxs;	// world model bounds (modelinfo->GetModelBounds)
};

//-----------------------------------------------------------------------------
// Lifecycle (main-owned glue)
//-----------------------------------------------------------------------------
// Early client init: resolves SHADERAPIDX12_LIGHTING_INTERFACE_VERSION (NULL on non-DX12 renderers; feature maps
// then fail admission with SHADOWMAP_ERR_REQUIRES_DX12). Material release/restore callbacks are appended only
// after native feature-map admission, so they follow the engine and certified bridge's forward dispatch.
void ShadowMapsDX12_Init();
void ShadowMapsDX12_Shutdown();

// Admission before client game systems initialize. Ordinary maps stay untouched.
// Feature maps require a validated rshd-v4 manifest matching native level flags;
// v3 enhanced maps and orphan flags are rejected with a rebake error. The exact
// captured native lighting lump selects HDR/LDR; face selection is independent.
// RequireMap validates the pak asset and effective native identities in the
// backend, and supplies the native generation for ABI4 route 1. An established
// PENDING restore can initialize selected-light state; actual views require READY.
// PrepareMap copies selected lights with every inverse receiver span empty.
// Selected-light maps also require server transmission readiness before drawing.
// Failure latches an explicit error; admission glue/runtime rejection disconnects.
bool ShadowMapsDX12_LevelInitPreEntity( const char *pMapName, char *error, int errorBytes );
// Current map state (mapGeneration 0 / featureMap false on ordinary maps).
const ShadowMapClientMapState &ShadowMapsDX12_MapState();
// Pair around the COMPLETE CHLClient level shutdown, not device reset helpers.
// Begin disables receiver admission, closes scopes and drains native/client work
// before entity/leaf teardown. Late LevelShutdown still retires GPU resources.
void ShadowMapsDX12_BeginClientLevelShutdown();
void ShadowMapsDX12_EndClientLevelShutdown();
void ShadowMapsDX12_LevelShutdown();
// Resource callbacks only latch release/restore state. FRAME_START performs complete readmission
// after restore dispatch, draining old work before changing generations and refreshing snapshots.
// PENDING suspends geometry without reading/admitting; READY must retain the exact native generation/pair.
// This is not native map retirement or a mode callback, and provider-only transitions do not invoke it.
// Server readiness, fixture renderables and playable-view autoexec count survive a resource-only reset.
// Device/resource loss: rebuild complete state or latch the explicit error before another scene.
void ShadowMapsDX12_OnDeviceReset();

// True while the engine-selected high-resolution mode is admitted.
bool ShadowMapsDX12_Active();
// Latched admission/runtime error (empty when none).
const char *ShadowMapsDX12_LastError();
// False during the shutdown bracket, an incomplete resource restore, or after a runtime error; ordinary
// views otherwise remain drawable even though they do not create lighting packets.
bool ShadowMapsDX12_CanDrawReceiverViews();
// Server sign-on readiness (C_World::m_bShadowMapTransmitReady) observed by the manager.
void ShadowMapsDX12_SetServerTransmitReady( bool ready );

//-----------------------------------------------------------------------------
// Receiver views (viewrender.cpp). Every actual receiver view (main, monitor, reflection/refraction, intro,
// 3D sky, viewmodel) brackets its geometry with one nested packet. Depth/HUD/loading/postprocess never call.
//-----------------------------------------------------------------------------
enum ShadowMapReceiverViewKind_t
{
	SHADOWMAP_VIEW_MAIN = 0,
	SHADOWMAP_VIEW_MONITOR,
	SHADOWMAP_VIEW_REFLECTION,
	SHADOWMAP_VIEW_REFRACTION,
	SHADOWMAP_VIEW_INTRO,
	SHADOWMAP_VIEW_SKY3D,
	SHADOWMAP_VIEW_VIEWMODEL,
};

// Builds/refreshes caster depth for the view (nested shadow views are rendered here, before receiver geometry),
// snapshots r_shadowmap_filter/r_shadowmap_debug, uploads the view packet and begins the lighting scope.
// `viewSetup` is the receiver's actual CViewSetup (sky views pass the transformed sky camera; viewmodels their own
// projection). Returns false for ordinary/shutdown views or an explicit failure;
// CanDrawReceiverViews distinguishes ordinary rendering from rejected rendering.
bool ShadowMapsDX12_BeginReceiverView( const CViewSetup &viewSetup, ShadowMapReceiverViewKind_t kind );
void ShadowMapsDX12_EndReceiverView();

// Dynamic caster bookkeeping (clientleafsystem/renderable movement feeds this through RenderableChanged glue):
// invalidates every light/cascade overlapped by the previous or current bounds.
void ShadowMapsDX12_OnCasterMoved( const Vector &oldMins, const Vector &oldMaxs, const Vector &newMins, const Vector &newMaxs );
// Static geometry/material generation bump (model/material reload, static-prop changes): rebuilds static caches.
void ShadowMapsDX12_InvalidateStatic();

#endif // SHADOWMAPS_DX12_H
