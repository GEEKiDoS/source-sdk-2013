//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Extension interface of shaderapidx12.dll for retained world-light
//          and shadow-map state: private depth targets, per-view light packets
//          with CPU tile lists, lightmap-visibility (R8) companion pages and
//          map admission status. Obtained from the renderer's factory:
//          Sys_GetFactory("shaderapidx12")(SHADERAPIDX12_LIGHTING_INTERFACE_VERSION, NULL).
//
//          Threading: ValidateMap, PrepareMap, GetStatus, GetSunVisibilityStats,
//          Create/Retain/DestroyShadowDepthTarget, Set/ReceiverFeatureGeneration
//          and RejectUnsupportedLitShader are synchronous and thread-safe.
//          Every queued method is called DIRECTLY by the caller from any
//          thread at the point of use: the implementation copies every array, acquires
//          the Retain leases of every named depth target immediately, and then either
//          executes on the recording owner (when the caller is it) or QueueRefCalls its
//          own refcounted packet through the current render context. Callers never wrap
//          these methods in a second queue (a late replay could not legally acquire
//          leases of targets destroyed in between). No caller pointer survives the call.
//
//          Lighting resource ABI 4 preserves the space2 constant-buffer layout;
//          explicit native-generation/highres-route state and the 64-style snapshot
//          are copied through map/view packets. Space3 replacement resources and
//          DX12HighresDrawConstants are independent of the abandoned sun carrier.
//
//===========================================================================//
#ifndef ISHADERAPIDX12LIGHTING_H
#define ISHADERAPIDX12LIGHTING_H
#ifdef _WIN32
#pragma once
#endif

#include "tier0/platform.h"
#include "tier1/interface.h"
#include "tier1/refcount.h"

#define SHADERAPIDX12_LIGHTING_INTERFACE_VERSION "ShaderAPIDX12Lighting_004"

//-----------------------------------------------------------------------------
// Fixed contract literals (HLSL twins are emitted by gencommon / declared in shadowmap_lighting.hlsli)
//-----------------------------------------------------------------------------
#define DX12_LIGHTING_SHADER_ABI				4

#define DX12_SHADOW_PCSS_MAX_TEXELS				16		// search/filter radius cap, texels
#define DX12_SHADOW_GUARD_TEXELS				18		// rendered guard on every edge of every slot/face/cascade
#define DX12_SHADOW_PCSS_BLOCKER_SAMPLES		16		// fixed blocker-search budget (sample 0 is the center)
#define DX12_SHADOW_PCSS_FILTER_SAMPLES			32		// fixed comparison budget
#define DX12_SHADOW_PCF_TAPS					4		// (-0.5,-0.5),(+0.5,-0.5),(-0.5,+0.5),(+0.5,+0.5)

#define DX12_SHADOW_LOCAL_PAGE_SIZE				4096	// one local page, texels per side
#define DX12_SHADOW_LOCAL_SLOT_SIZE				512		// S for locals
#define DX12_SHADOW_LOCAL_SLOT_USEFUL			476		// U = S - 2*18
#define DX12_SHADOW_LOCAL_SLOTS_PER_ROW			8
#define DX12_SHADOW_LOCAL_SLOTS_PER_PAGE		64
#define DX12_SHADOW_MAX_LOCAL_PAGES				1024	// t0..t1023 descriptor contract (space 2)

#define DX12_SHADOW_CSM_CASCADES				4
#define DX12_SHADOW_CSM_SLOT_SIZE				2048	// S for cascades
#define DX12_SHADOW_CSM_SLOT_USEFUL				2012
#define DX12_SHADOW_CSM_ATLAS_SIZE				4096	// 2x2 cascade slots
#define DX12_SHADOW_STATIC_SUN_SIZE				4096	// S for the whole-map static sun map
#define DX12_SHADOW_STATIC_SUN_USEFUL			4060
#define DX12_SHADOW_SUN_MAP_COUNT				5		// CSM0..3 + static sun (index 4)
#define DX12_SHADOW_MAX_FACES					6

#define DX12_SHADOW_TILE_PIXELS					16		// CPU light-list tile size in screen pixels
#define DX12_SHADOW_RASTER_DEPTH_BIAS			1		// raw DepthBias of every private shadow PSO (receiver-plane compares need almost none)
#define DX12_SHADOW_RASTER_SLOPE_BIAS			0.1f	// SlopeScaledDepthBias; 2.0 erased ~2 texels of wall/ceiling contact shadow (sun leak)
#define DX12_SHADOW_CUBE_NEAR					0.1f

// Pixel-stage resource space 2 (feature variants only; ordinary logicals never declare it)
#define DX12_LIGHTING_REGISTER_SPACE			2
#define DX12_LIGHTING_T_LOCAL_ATLAS_FIRST		0		// Texture2D<float> localAtlas[1024]
#define DX12_LIGHTING_T_CASCADE_ATLAS			1024	// Texture2D<float>
#define DX12_LIGHTING_T_STATIC_SUN				1025	// Texture2D<float>
#define DX12_LIGHTING_T_LIGHTS					1026	// StructuredBuffer<RuntimeShadowLightGpu>
#define DX12_LIGHTING_T_TILE_RANGES				1027	// StructuredBuffer<uint2> (offset,count)
#define DX12_LIGHTING_T_TILE_INDICES			1028	// StructuredBuffer<uint>
#define DX12_LIGHTING_VIEW_TABLE_COUNT			1029	// t0..t1028 in the immutable per-view table
#define DX12_LIGHTING_T_SUN_VISIBILITY			1029	// per-draw R32_UINT: allocation identity in high 24 bits, R8 visibility in low 8
#define DX12_LIGHTING_S_COMPARISON				0		// linear, LESS_EQUAL, clamp
#define DX12_LIGHTING_B_VIEW					0		// DX12LightingViewConstantsV1 (CBV)

// Filter / debug modes snapshotted into each view packet
#define DX12_SHADOW_FILTER_PCF					0
#define DX12_SHADOW_FILTER_PCSS					1
#define DX12_SHADOW_DEBUG_NONE					0
#define DX12_SHADOW_DEBUG_CASCADE				1		// cascade ownership
#define DX12_SHADOW_DEBUG_VISIBILITY			2		// evaluated sun visibility, including the baked scalar cap
#define DX12_SHADOW_DEBUG_LOCAL_PAGE_FACE		3
#define DX12_SHADOW_DEBUG_CASTER_BOUNDS			4		// client-side overlay; shader passes through
#define DX12_SHADOW_DEBUG_PCSS_BLOCKERS			5		// diagnostics.x (blockerCount/16)
#define DX12_SHADOW_DEBUG_PCSS_RADIUS			6		// diagnostics.y (filterRadiusTexels/16)

// RuntimeShadowLightGpu::type
#define DX12_SHADOW_LIGHT_SUN					0
#define DX12_SHADOW_LIGHT_POINT					1		// six faces
#define DX12_SHADOW_LIGHT_SPOT					2		// one face (narrow) or six (wide: stopdot2 <= cos 89deg)

// DX12LightingViewConstantsV1::cShadowView1.w flags
#define DX12_SHADOW_VIEW_HAS_SUN				0x1
#define DX12_SHADOW_VIEW_CSM_VALID				0x2		// f > n and cascades rendered
#define DX12_SHADOW_VIEW_STATIC_SUN_VALID		0x4

//-----------------------------------------------------------------------------
// GPU-visible record layouts retained from ABI 1. Row-major float4x4; HLSL mul(matrix, float4(world,1)).
//-----------------------------------------------------------------------------
struct RuntimeShadowLightGpu				// 592 bytes: 7 rows of 16 + 6 * 64 + 6 * 16
{
	uint32	lightId;					// index into the map's selected light list (ShadowMapLightDisk order of the active mode)
	uint32	type;						// DX12_SHADOW_LIGHT_*
	uint32	style;						// original style id (diagnostic; radiance already carries LightStyleValue once)
	uint32	faceCount;					// 1 or 6
	float	origin[3];
	float	attenuationRadius;			// dworldlight_t::radius (0 = unbounded)
	float	travelDirection[3];			// light -> receiver (spot axis); unused for point
	float	innerConeCos;				// stopdot
	float	radiance[3];				// linear irradiance units (baker /255 already applied) * LightStyleValue(style)
	float	outerConeCos;				// stopdot2
	float	constantAttn, linearAttn, quadraticAttn, exponent;
	float	fadeStart, fadeEnd, capDist, shadowSourceRadius;
	float	shadowNear, shadowFar, planeToTexel, tanRenderedHalfFov;	// planeToTexel = slotSize / (2 * tanRenderedHalfFov)
	float	worldToClip[DX12_SHADOW_MAX_FACES][16];					// rendered (guard-expanded) projection * view, row-major
	uint32	faces[DX12_SHADOW_MAX_FACES][4];						// page, slotX, slotY, slotSize; unused faces all zero
};

struct DX12LightingViewConstantsV1		// 672 bytes, cbuffer b0 space2 (see HLSL twin)
{
	uint32	cShadowView0[4];			// mapGeneration, viewGeneration, filterMode, debugMode
	uint32	cShadowView1[4];			// tileCountX, tileCountY, localLightCount, DX12_SHADOW_VIEW_* flags
	float	cShadowViewport[4];			// viewportX, viewportY, 1/viewportWidth, 1/viewportHeight (tile = floor((pixel-xy)/16))
	float	cSunRadiance[4];			// rgb linear irradiance * LightStyleValue(sun.style) applied once; w = tan(sunAngularRadius)
	float	cSunTravel[4];				// xyz light->receiver (receiverToLight = -xyz); w = CSM far distance f
	float	cSunBasisX[4];				// xyz stable light-space X; w = 0.9*f (static blend start)
	float	cSunBasisY[4];				// xyz stable light-space Y; w = 0
	float	cEyePosition[4];			// xyz; w = CSM near n
	float	cViewForward[4];			// xyz; receiverDistance = dot(positionWS - eye, xyz); w = 0
	float	cCascadeSplits[4];			// s1, s2, s3, s4 (= f)
	float	cCascadeBlend[4];			// w1, w2, w3 half blend widths at interior splits; w = 0
	float	cShadowDepthRecords[DX12_SHADOW_SUN_MAP_COUNT][4];	// near, far, worldUnitsPerTexel, 0 (CSM0..3, static sun 4)
	float	cSunWorldToClip[DX12_SHADOW_CSM_CASCADES][16];		// rendered projection * view per cascade
	float	cStaticSunWorldToClip[16];
	uint32	cCascadeRects[DX12_SHADOW_CSM_CASCADES][4];			// slotX, slotY, slotSize, 0 inside the cascade atlas
	uint32	cStaticSunRect[4];			// 0, 0, 4096, 0
	uint32	cSunIdentity[4];			// sun lightId (0xFFFFFFFF none), sunStyle, 0, 0
};

COMPILE_TIME_ASSERT( sizeof( RuntimeShadowLightGpu ) == 592 );
COMPILE_TIME_ASSERT( sizeof( DX12LightingViewConstantsV1 ) == 672 );

//-----------------------------------------------------------------------------
// Control-plane descriptors
//-----------------------------------------------------------------------------
typedef uint64 DX12ShadowTarget_t;		// private generation/index id; 0 invalid; never an engine texture
#define DX12_SHADOW_TARGET_INVALID		( (DX12ShadowTarget_t)0 )

enum DX12LightingStatus
{
	DX12_LIGHTING_STATUS_PENDING = 0,
	DX12_LIGHTING_STATUS_READY = 1,
	DX12_LIGHTING_STATUS_FAILED = 2,	// latched error text via GetStatus
};

struct DX12LightingSelectedLight		// immutable copy of a selected worldlight (map lifetime)
{
	uint32	lightId;					// index into the map's selected list (ShadowMapLightDisk order)
	uint32	type;						// DX12_SHADOW_LIGHT_*
	int32	style;
	float	origin[3];
	float	direction[3];				// travel direction (sun/spot)
	float	radiance[3];				// unstyled linear irradiance
	float	constantAttn, linearAttn, quadraticAttn, exponent;
	float	innerConeCos, outerConeCos;
	float	attenuationRadius;
	float	startFade, endFade, capDist;
	float	shadowSourceRadius;			// locals (world units)
	float	shadowSunAngularRadius;		// sun (degrees)
};

struct ShadowMapReceiverFaceDisk;
struct ShadowMapReceiverTriangleDisk;

struct DX12LightingSunVisibilityStats
{
	uint32	receiverFaces;
	uint32	mappedFaces;
	uint32	pages;
	uint32	unresolvedDraws;
};

// Original, undeformed BSP quad in grid order (0,0), (1,0), (1,1), (0,1).
// Sparse, sorted by receiver-face index. Needed to reproduce Source's
// unsigned-area displacement overlay interpolation; never inferred from RGB.
struct DX12LightingReceiverQuad
{
	uint32 faceIndex;
	uint32 gridSize;
	float position[4][3];
};

struct DX12LightingMapDesc
{
	uint32	mapGeneration;				// client map load counter; UnloadMap retires it
	uint32	mode;						// SHADOWMAP_MODE_LDR / HDR
	uint32	shaderAbi;					// must be DX12_LIGHTING_SHADER_ABI
	uint32	highresRoute;				// 1: v4 selected-light map, no inverse receiver spans
	uint64	nativeMapGeneration;			// exact bridge generation supplied by client admission
	uint32	selectedLightCount;
	const DX12LightingSelectedLight *selectedLights;
	int32	sunLightIndex;				// index into selectedLights of the sun, -1 none
	float	worldMins[3];
	float	worldMaxs[3];
	uint32	receiverFaceCount;
	const ShadowMapReceiverFaceDisk *receiverFaces;
	uint32	receiverTriangleCount;
	const ShadowMapReceiverTriangleDisk *receiverTriangles;
	uint32	sunVisibilityCount;
	const unsigned char *sunVisibility;	// scalar R8 samples; all spans are copied by PrepareMap
	uint32 receiverQuadCount;
	const DX12LightingReceiverQuad *receiverQuads;
};

struct DX12LightingViewPacket
{
	uint32	mapGeneration;
	uint32	viewGeneration;				// unique per BeginView; packets for nested views nest in order
	uint64	nativeMapGeneration;			// exact native bridge generation; never inferred from mapGeneration
	uint32	highresRoute;				// 1: explicit highres replacement, 0: ordinary/old receiver route
	float	styles[64];					// immutable public LightStyleValue snapshot, indexed by authored style
	int		viewportX, viewportY, viewportWidth, viewportHeight;
	DX12LightingViewConstantsV1 constants;
	DX12ShadowTarget_t cascadeAtlasTarget;	// 0 when CSM invalid
	DX12ShadowTarget_t staticSunTarget;		// 0 when no sun
	const DX12ShadowTarget_t *localTargets;	// page index (RuntimeShadowLightGpu::faces[].page) -> target, each 4096x4096
	uint32	localTargetCount;
	const RuntimeShadowLightGpu *lights;	// the view's relevant local lights (tile indices address this array)
	uint32	lightCount;
	const uint32 *tileRanges;			// 2 * tileCount entries: offset, count into tileIndices
	uint32	tileCount;					// tileCountX * tileCountY
	const uint32 *tileIndices;
	uint32	tileIndexCount;
};

//-----------------------------------------------------------------------------
// Interface
//-----------------------------------------------------------------------------
abstract_class IShaderAPIDX12Lighting
{
public:
	// Synchronous. Checks ABI, ResourceBindingTier >= 2, required native shaders and record sanity; no GPU work.
	virtual bool ValidateMap( const DX12LightingMapDesc &map, char *error, int errorBytes ) = 0;
	// Synchronous at map admission: uploads the light buffer / seeds null-filled tables on a private queue with one
	// bounded fence wait; GetStatus(mapGeneration, 0) is Ready/Failed when this returns.
	virtual void PrepareMap( const DX12LightingMapDesc &map ) = 0;
	// Synchronous. viewGeneration 0 queries map preparation; otherwise Pending until that view's EndView has replayed,
	// then Ready or Failed (latched error). Terminal results survive UnloadMap (delayed polls never regress to Pending).
	virtual DX12LightingStatus GetStatus( uint32 mapGeneration, uint32 viewGeneration, char *error, int errorBytes ) = 0;
	// Queued. Begins a nested lighting scope; validates every target generation/dimension/rectangle and tile state on replay.
	// A failed view suppresses its receiver draws and latches the error.
	virtual void BeginView( const DX12LightingViewPacket &view ) = 0;
	// Synchronous control-thread allocation of typeless R32 storage + DSV/SRV descriptors. 0 on failure.
	virtual DX12ShadowTarget_t CreateShadowDepthTarget( const char *name, int width, int height ) = 0;
	// Synchronous. Returns an AddRef'd lease (or NULL when the target is gone/destroying). Packets acquire before enqueue, release after replay.
	virtual IRefCounted *RetainShadowDepthTarget( DX12ShadowTarget_t target ) = 0;
	// Synchronous. Prevents new leases; storage retires only after every lease and GPU use completed.
	virtual void DestroyShadowDepthTarget( DX12ShadowTarget_t target ) = 0;
	// Queued. Saves backend state, binds zero color targets + the private DSV, sets viewport/scissor to the rect, clears only it when asked.
	virtual void BeginShadowPass( DX12ShadowTarget_t target, int x, int y, int width, int height, bool clear ) = 0;
	virtual void EndShadowPass() = 0;
	// Queued. Depth-only fullscreen-triangle raster restore (point-sampled source, SV_Depth, depth test ALWAYS). src != dst.
	virtual void CopyShadowDepthRect( DX12ShadowTarget_t dst, DX12ShadowTarget_t src, int dstX, int dstY, int srcX, int srcY, int width, int height ) = 0;
	// Queued. Restores the parent packet (or none).
	virtual void EndView() = 0;
	// Queued. Releases every map-scoped resource (light buffers, cached tables) of the generation.
	virtual void UnloadMap( uint32 mapGeneration ) = 0;
	// Synchronous plain store/load. The client sets the converted map generation (0 = off) at admission BEFORE
	// participating material snapshots are refreshed / world meshes allocated, and clears it at unload. The native
	// material DLL selects <base>_shadowmap_* variants for lit passes only while this is nonzero.
	virtual void SetReceiverFeatureGeneration( uint32 mapGeneration ) = 0;
	virtual uint32 ReceiverFeatureGeneration() = 0;
	// Synchronous. Called by the native material DLL when a converted map draws a lit material whose resolved path has
	// no current lighting-ABI variant (e.g. a DX8 fallback); latches SHADOWMAP_ERR_SHADER_UNAVAILABLE (": <shaderName>") for
	// the active map generation so GetStatus reports Failed, receiver draws are suppressed and presentation is blocked.
	virtual void RejectUnsupportedLitShader( const char *shaderName ) = 0;
	// Synchronous snapshot of actual per-generation receiver mapping; zero for an unknown generation.
	virtual void GetSunVisibilityStats( uint32 mapGeneration, DX12LightingSunVisibilityStats &stats ) = 0;
};

#endif // ISHADERAPIDX12LIGHTING_H
