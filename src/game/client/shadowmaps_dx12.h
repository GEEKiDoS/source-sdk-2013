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

class IClientRenderable;
class ICollideable;
struct StaticPropLump_t;

// ConVars (defined in shadowmaps_dx12.cpp)
extern ConVar r_shadowmap_enable;		// archived, default 1; 0 retains all baked visibility and selected direct/highres lighting
extern ConVar r_shadowmap_max_realtime_lights; // archived, default 4; 0 = no budget limit within camera PVS; invalid = 4
										// positive fractions truncate with minimum 1; values >= INT_MAX saturate
										// one frame-global main player position/PVS, independent of angles/FOV;
										// active styled lights rank by 1/max(emitter distance,1), stable lightId ties, >25% hysteresis
extern ConVar r_shadowmap_realtime_fade_seconds; // archived, default .5 real seconds; negative/nonfinite/nonnumeric = .5; 0 = immediate
										// full handoff: all outgoing locals fade together over half, then incoming over half;
										// free slots ramp in over half (.25s by default); detach/COW preserves the resident cap
										// first admission and >=256u positional cuts use current top-N at weight 1 immediately
										// normal reversals ramp from current weight; weights/membership frozen across receivers
										// live cap reduction/enable0 forces excess weakest weights to zero immediately;
										// old physical replay/GPU targets retire without blocking logical slot admission
extern ConVar r_csm_distance;			// archived, default 4096, clamped [128, 32768]
extern ConVar r_shadowmap_filter;		// archived, default 0, clamped [0,1]: 0 PCF, 1 PCSS; snapshotted per view packet
extern ConVar r_shadowmap_spot_near;	// archived, default 4 Source units, clamped [.1,64]; snapshotted per receiver view
										// spot shadow cameras only (including six-face spots), never influence/attenuation;
										// effective near = min(setting,max(.1,far/2)); changes rebuild projection and static depth
extern ConVar r_shadowmap_debug;		// cheat, default 0: 0 normal, 1 cascade, 2 visibility, 3 local page/face, 4 caster bounds, 5/6 PCSS diagnostics
extern ConVar r_shadowmap_autoexec;	// cheat, default "": acceptance automation; ';'-separated cfg list, item i exec'd on the
										// (i+1)*60th playable main view of each map (no loading plaques/menu backgrounds; independent of `wait`)
// ConCommand r_shadowmap_stats: one-shot last-completed-view counters (selected/relevant lights, pages, casters,
// static/dynamic redraws, CSR entries, residency failures, depthRenders, deferredCharts, initializingCharts, reusedCharts, pageCopies)
// plus depthWork for the most recently prepared engine frame and current-generation GetSunVisibilityStats fields
// (receiverFaces, mappedFaces, pages, unresolvedDraws; pages here means actual R8 companion pages).
// ConCommand r_shadowmap_report <path>: writes JSON for the fixture runner (--runtime-report) from actual renderer
// data of the last completed main view: {"schema":1,"counters":{...view-counter keys printed by r_shadowmap_stats...},
// counters.realtimeLights/transitioningLocals count frame-global resident locals; positiveWeightLocals/bakedOnlyLocals
// count radiance-relevant packet locals. promotions/demotions count frame-global changes. Sun is always excluded.
// realtimeLightIds contains {"lightId":id,"score":n,"weight":n,"departing":bool}, frozen at that completed view.
// score is inverse main-camera emitter distance (0 when PVS/style/radiance-ineligible), not receiver contribution.
// "sunVisibility":{"receiverFaces":n,"mappedFaces":n,"pages":n,"unresolvedDraws":n} (actual current-generation backend
// snapshot when the report is requested), "casterValidation":[{"light":id,"testedCount":n,"reportedCount":n,
// "exhaustiveCount":n,"mismatchCount":n,"exceeds4096":bool}]
// (EnumerateShadowCasters vs EnumerateShadowCastersExhaustive per relevant light/cascade/static sun),
// "detailCases":[{"name":"fast"|"ordinary","fast":bool,"matched":bool,"count":n}] (from IDetailObjectSystem::GetShadowReport),
// "mapState":{"featureMap":bool,"runtimeActive":bool,"mode":n,"selectedLights":n,"sun":id,"error":"..."}}.
// "receiver":{"kind":n,"frame":n,"generation":n,"origin":[x,y,z],"angles":[p,y,r],"fov":n,"aspect":n,
// "near":n,"far":n,"viewport":[x,y,width,height],"orthographic":bool} records the completed camera, not command input.
// "depthWork":{"frame":n,"renders":n} counts actual depth scene calls across all receiver views of that frame.
// counters.pageCopies counts LOCAL full-page COW in that view; depthWork.pageCopies
// counts it across all receiver views of that engine frame (sun restores excluded).
// counters.crossFrameReusedCharts/crossFrameReusedSunCharts count committed LOCAL faces/receiver-owned cascades
// retained from an earlier frame. counters.uncertifiedCasters counts collected dynamic casters whose depth content
// cannot be proven rigid; these remain on the required overlay redraw path. Report schema stays 1.
// counters.rigidLayerRebuilds counts clean+certified-rigid intermediate rebuilds; counters.overlayCasterDraws
// counts actual dynamic caster submissions to per-frame work overlays (not rigid rebuilds or oriented details).
// counters.footprintRestorePixels counts restored work pixels (full + partial); fullRestores counts full work charts.
// Clean->rigid initialization and LOCAL page COW are excluded. Unchanged charts erase the previous overlay's
// projected render-bounds union (+2 texels), then draw every current overlay caster/card; no overlay is throttled.
// Clean/rigid rebuilds, projection/depth/slot/source/work-target changes (including exact COW), unknown bounds
// and unproven mesh extents force full restoration. Detail cards and rope meshes report actual emitted vertices.
// Rope replay owns a pooled, refcounted record and clip matrix; callback-local feedback ends when emission ends.
// Completed rope bounds merge with CPU caster/card bounds. Still-pending or unproven captures restore fully;
// no caller stack pointer or cache-matrix pointer survives into the material queue.
// counters.fullRestoreReasons is a nonexclusive object of full-restored chart counts: unknownPrev, rebuild, matrix,
// depth, slot, target, source, cow, rope, customDraw, nonfinite, wNonPositive, invalidBounds, unknownGeometry,
// queuedRope, boundsCoverChart. Matrix/depth counts distinguish exact cascade refits from footprint rejection.
// depthWork.rigidLayerRebuilds and depthWork.views count CPU-submitted work across ALL receiver kinds in
// depthWork.frame (not just the completed main view). views are aggregated by receiver kind.
// depthWork.rigidEvents identifies each intermediate rebuild/proof rejection by kind, light and face;
// action 0=rebuild, 1=direct-overlay bypass, 2=draw-proof rejection. reasonBits/keyFieldBits decode its masks.
// Sun and every relevant resident local complete required clean/overlay updates before receiver draws; baked-only
// locals keep radiance/CSR but acquire no slots, chart matrices, depth work or receiver target leases.
// Baked-direct world receivers additionally use the positive-weight local list: ascending packet-local GPU
// indices appended after the complete CSR prefix in t1028; cSunIdentity.zw hold tail offset/count.
// tileIndexCount includes both spans. Models/detail/moved/non-highres receivers retain the full CSR unchanged.
// There is no update-rate budget. Whole-influence rejection may skip invisible lights, never off-screen emitters
// whose influence reaches visible receivers. Cubes publish matching depth and all face cameras atomically.
// Existing same-frame LOCAL reuse retains its once-per-frame state boundary: identical projection/generations,
// pointer-set/classification, per-face notifications and oriented-detail tuple. Across frames LOCAL faces and
// receiver-owned sun cascades additionally require exact certified rigid content bytes and physical work identity.
// Standard unparented, unscaled, single-root STUDIOHDR_FLAGS_STATIC_PROP entities and standard brush entities
// with opaque proxy-free nondeforming materials are eligible; unknown/custom draws, animation, flex, ragdolls,
// ropes/particles, clip planes, parents, cutout textures and oriented detail overlays redraw on every new frame.
// Registration/removal/classification generations cannot be acknowledged by equal content keys.
// Notification/key snapshots preceding collection/draw must still be valid at commit; no update throttling.
// Depth-draw notifications remain dirty. Sun depth remains receiver-owned, never shared between receiver caches.
// LOCAL pages and each receiver-owned cascade atlas retain a third 4096-square D32 rigid intermediate
// (64 MiB payload each). Warm mixed charts restore rigid->work once and draw uncertain/changing casters/details.
// The rigid subset uses exact membership/content equality, clean/slot identity, each certified entity's
// globally unique registration stamp, and handle-resolved per-entity notification revision proof.
// Sequence/cycle/playback/pose/controller/hitbox clocks are excluded: the certified STATIC_PROP SetupBones
// path copies only the single render-root transform. Animated nonstatic skeletons remain uncertifiable.
// Changes to uncertain casters alone do not rebuild a valid rigid layer.
// During the existing chart snapshot copy, exact keys are merged with the last committed full caster set.
// New/changed certified casters, including moving brushes, stay in the per-frame overlay; a key changed at the
// previous commit is not admitted on a single coincident sample. Stable peers retain the rigid intermediate.
// Missing/changed certified proof before splitting uses the original complete overlay path; changes observed
// during a depth draw prevent retaining the rigid layer for the next update.
// Unstable clean/projection/depth or rejected rigid draw proof uses direct clean+all-dynamic overlays instead
// of rebuilding an intermediate every frame. Eligible stable subsets admit it again immediately; visible
// overlays are never deferred or throttled. Empty rigid subsets use clean directly.
// counters.rigidLayerBypasses/invalidRigidLayers distinguish this admission decision from draw-proof rejection.
// Rigids are not receiver-sampled: work-only COW preserves parent pixels; copy/draw leases protect all sources.
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
	bool						featureMap;				// validated rshd-v5 manifest (any runtimeModeMask bit)
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
// Feature maps require a validated rshd-v5 manifest matching native level flags;
// old enhanced maps and orphan flags are rejected with a rebake error. The exact
// captured native lighting lump selects HDR/LDR; face selection is independent.
// RequireMap validates the pak asset and effective native identities in the
// backend, and supplies the native generation for ABI6 route 1. An established
// PENDING restore can initialize selected-light state; actual views require READY.
// PrepareMap copies selected lights with every inverse receiver span empty.
// Camera PVS is loaded from standard BSP planes/nodes, leaf v0/v1 and visibility;
// absent visibility is all-visible, malformed available PVS/tree rejects admission.
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

// FRAME_START, after ShadowMapsDX12_OnDeviceReset: applies a pending mat_pbr_override request (stdshader_dx12 cvar callback ->
// IShaderAPIDX12Lighting::RequestPbrOverride). Inside a native material transaction it commits the mode and re-snapshots every
// precached material; without the transaction the request stays pending and is retried next frame. Level init commits the
// request itself right before its own material reload/refresh.
void ShadowMapsDX12_CommitPbrOverride();

// Projected-light (flashlight / env_projectedtexture) publication for the PBR shaders, used by CClientShadowMgr::PublishProjectedLights.
// Available only on the DX12 renderer. The packet is queued like a receiver view, after the dx12/pbr_lights material bound the
// cookie/depth textures; it stays current until the next packet (an empty packet clears it).
bool ShadowMapsDX12_ProjectedLightsAvailable();
void ShadowMapsDX12_PublishProjectedLights( const DX12ProjectedLightPacket &packet );

// True while the engine-selected high-resolution mode is admitted.
bool ShadowMapsDX12_Active();
// Active enhanced runtime with runtime shadowing enabled; false during shutdown/reset/failure.
// Legacy entity projections use this, not Active (flashlights remain independent).
bool ShadowMapsDX12_ShadowsEnabled();
// Latched admission/runtime error (empty when none).
const char *ShadowMapsDX12_LastError();
// Client-system validation after shadow admission (CHLClient admits before LevelInitPreEntityAllSystems).
// Latches the existing map failure, rejects receiver drawing and requests disconnect.
void ShadowMapsDX12_RejectMap( const char *reason );
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

// RenderView calls once before monitors/sky/child receivers. Main player position drives cached BSP PVS
// eligibility and nearest-emitter priority; angles/FOV/projection changes never reset local residency.
// Absolute (nonpaused) frame time advances ramps once; main/overlay/stereo repeats cannot alter this frame.
void ShadowMapsDX12_PrepareMainView( const CViewSetup &viewSetup );

// Builds/refreshes caster depth BEFORE receiver world/decal-list construction, uploads the immutable lighting
// packet and begins its scope. At r_shadowmap_enable 0 the scope still supplies selected direct/highres lighting.
// `viewSetup` is the receiver's actual CViewSetup (sky views pass the transformed sky camera; viewmodels their own
// projection). Returns false for ordinary/shutdown views or an explicit failure;
// CanDrawReceiverViews distinguishes ordinary rendering from rejected rendering.
bool ShadowMapsDX12_BeginReceiverView( const CViewSetup &viewSetup, ShadowMapReceiverViewKind_t kind );
void ShadowMapsDX12_EndReceiverView();

// Exact sprp ordinal/public renderable registration; no name or proximity lookup.
// Prepare publishes immutable hardware-mesh identity + authored pose once per
// cache generation. Backend resolves the actual draw without splitting batches.
void ShadowMapsDX12_RegisterStaticPropReceiver( uint32 ordinal, ICollideable *prop, const StaticPropLump_t &authored );
void ShadowMapsDX12_PrepareStaticPropReceiver( IClientRenderable *renderable );

// Dynamic caster bookkeeping (clientleafsystem/renderable movement feeds this through RenderableChanged glue):
// invalidates every light/cascade overlapped by the previous or current bounds.
void ShadowMapsDX12_OnCasterMoved( IClientRenderable *renderable, const Vector &oldMins, const Vector &oldMaxs, const Vector &newMins, const Vector &newMaxs );
// Registration/removal/classification cannot be acknowledged by an equal rigid content key.
void ShadowMapsDX12_InvalidateCasterRegistration( IClientRenderable *renderable = NULL );
// Static geometry/material generation bump (model/material reload, static-prop changes): rebuilds static caches.
void ShadowMapsDX12_InvalidateStatic();

#endif // SHADOWMAPS_DX12_H
