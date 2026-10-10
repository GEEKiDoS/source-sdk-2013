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
//          RejectUnsupportedLitShader, RegisterStaticPropReceiver,
//          RegisterModelMeshMetadata, GetStaticPropVisibilityStats,
//          GetStaticPropVisibilityDetails, RequestPbrOverride, PbrOverridePending,
//          CommitPbrOverride, PbrOverride and Begin/EndMaterialTransaction (main thread)
//          are synchronous and thread-safe.
//          Every queued method is called DIRECTLY by the caller from any
//          thread at the point of use: the implementation copies every array, acquires
//          the Retain leases of every named depth target immediately, and then either
//          executes on the recording owner (when the caller is it) or QueueRefCalls its
//          own refcounted packet through the current render context. Callers never wrap
//          these methods in a second queue (a late replay could not legally acquire
//          leases of targets destroyed in between). No caller pointer survives the call.
//
//          Lighting resource ABI 6 retains the ABI 5 light/view layouts, adds
//          immutable prop mesh directories at t1033, actual draw triangles at t1034,
//          and the 96-byte static-prop draw block at b1, all in space2.
//          Unknown or moved receivers explicitly use visibility 1.
//          v4 baked-direct world/prop receivers merge the positive-weight resident
//          t1028 tail (cSunIdentity.zw offset/count) with sparse style overflow.
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
#include "shaderapi/dx12staticpropvisibility.h"

// Same typedef as shaderapi/ishaderapi.h (kept here so tools that include this header need no renderer headers).
typedef intp ShaderAPITextureHandle_t;

#define SHADERAPIDX12_LIGHTING_INTERFACE_VERSION "ShaderAPIDX12Lighting_008"

//-----------------------------------------------------------------------------
// Fixed contract literals (HLSL twins are emitted by gencommon / declared in shadowmap_lighting.hlsli)
//-----------------------------------------------------------------------------
#define DX12_LIGHTING_SHADER_ABI				6

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
#define DX12_LIGHTING_VIEW_TABLE_COUNT			1029	// immutable per-view portion, t0..t1028
#define DX12_LIGHTING_T_SUN_VISIBILITY			1029	// per-draw R32_UINT: allocation identity in high 24 bits, R8 visibility in low 8
#define DX12_LIGHTING_T_VISIBILITY_FACES		1030	// StructuredBuffer<uint4>: entry range, high width/height
#define DX12_LIGHTING_T_VISIBILITY_ENTRIES		1031	// StructuredBuffer<uint4>: canonical light, encoding, byte offset, count
#define DX12_LIGHTING_T_VISIBILITY_PAYLOAD		1032	// ByteAddressBuffer: R8, prop RGBA16F direct/style overflow, world unbaked IDs
#define DX12_LIGHTING_T_PROP_MESHES				1033	// StructuredBuffer<uint4>: entries/count/vertices/baked-direct flags
#define DX12_LIGHTING_T_PROP_TRIANGLES			1034	// root StructuredBuffer<DX12StaticPropTriangleGpu>
#define DX12_LIGHTING_RESOURCE_TABLE_COUNT		1034	// t1034 is a separate per-draw root SRV
#define DX12_SHADOW_VISIBILITY_BAKED_AVAILABLE	0x1
#define DX12_LIGHTING_S_COMPARISON				0		// linear, LESS_EQUAL, clamp
#define DX12_LIGHTING_B_VIEW					0		// DX12LightingViewConstantsV1 (CBV)
#define DX12_LIGHTING_B_PROP_DRAW				1		// DX12StaticPropDrawConstants

// PBR projected-light (flashlight / env_projectedtexture) resources: pixel-stage space 5, every root signature.
// One 17-descriptor table: t0 StructuredBuffer<PBRSpotGpu>, t1..t8 cookie Texture2D<float4>, t9..t16 depth Texture2D<float>.
// s0 linear clamp, s1 comparison (linear, LESS_EQUAL, clamp) are static samplers of the root signature.
#define DX12_PBR_MAX_PROJECTED_LIGHTS			8
#define DX12_PBR_REGISTER_SPACE					5
#define DX12_PBR_T_LIGHTS						0		// StructuredBuffer<PBRSpotGpu>, stride sizeof( DX12ProjectedLightDesc )
#define DX12_PBR_T_COOKIE_FIRST					1		// Texture2D<float4> g_ProjectedCookies[8]
#define DX12_PBR_T_DEPTH_FIRST					9		// Texture2D<float> g_ProjectedDepth[8]
#define DX12_PBR_TABLE_COUNT					17
#define DX12_PBR_S_LINEAR						0
#define DX12_PBR_S_COMPARISON					1
#define DX12_PBR_LIGHT_WORLD					1		// DX12ProjectedLightDesc::flags: lights world receivers
#define DX12_PBR_LIGHT_MODELS					2		// DX12ProjectedLightDesc::flags: lights model receivers
// Ambient probe resources: pixel-stage space 4, lighting and highres root signatures only.
#define DX12_PROBE_REGISTER_SPACE				4
#define DX12_PROBE_TABLE_COUNT					9		// t0..t8
#define DX12_PROBE_T_INDIRECTION				0		// Texture3D<uint>: brick slot or hlight::kMissing
#define DX12_PROBE_T_DC							1		// Texture3D<float3>: R11G11B10_FLOAT flat irradiance
#define DX12_PROBE_T_BANDS_FIRST				2		// Texture3D<float4> ProbeBands[6]: RGBA8_SNORM, texture 2c + h = channel c bands 1..4 / 5..8
#define DX12_PROBE_T_VALIDITY					8		// Texture3D<float>: R8_UNORM
#define DX12_PROBE_S_LINEAR						0		// static sampler s0 space4: linear, clamp
#define DX12_PROBE_B_CONSTANTS					0		// DX12ProbeConstantsV1 (root CBV)

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
#define DX12_SHADOW_VIEW_CSM_VALID				0x2		// f > n and at least one initialized cascade (all four unless DEFERRED)
#define DX12_SHADOW_VIEW_STATIC_SUN_VALID		0x4
#define DX12_SHADOW_VIEW_UNSHADOWED			0x8		// selected direct active; skip runtime depth sampling (baked world sun cap remains)
#define DX12_SHADOW_VIEW_DEFERRED				0x10	// budgeted warm-up: missing charts are wholly zero; direct lighting remains active

//-----------------------------------------------------------------------------
// GPU-visible record layouts retained from ABI 1. Row-major float4x4; HLSL mul(matrix, float4(world,1)).
//-----------------------------------------------------------------------------
struct RuntimeShadowLightGpu				// 608 bytes: 7 rows + 6 matrices + 6 rects + hybrid visibility row
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
	float	worldToClip[DX12_SHADOW_MAX_FACES][16];					// rendered (guard-expanded) projection * view, row-major; reused with the same cached pixels
	uint32	faces[DX12_SHADOW_MAX_FACES][4];						// page, slotX, slotY, slotSize; unused faces all zero
	float	realtimeWeight;				// [0,1]; zero never references an atlas; positive requires complete charts
	uint32	visibilityFlags;			// DX12_SHADOW_VISIBILITY_*; no unknown bits
	uint32	bakedLightIndex;			// canonical selected manifest ordinal, valid iff BAKED_AVAILABLE
	uint32	reserved0;					// zero
};

struct DX12LightingViewConstantsV1		// 672 bytes, cbuffer b0 space2 (see HLSL twin)
{
	uint32	cShadowView0[4];			// mapGeneration, viewGeneration, filterMode, debugMode
	uint32	cShadowView1[4];			// tileCountX, tileCountY, localLightCount, DX12_SHADOW_VIEW_* flags
	float	cShadowViewport[4];			// viewportX, viewportY, 1/viewportWidth, 1/viewportHeight (tile = floor((pixel-xy)/16))
	float	cSunRadiance[4];			// rgb linear irradiance * LightStyleValue(sun.style) applied once; w = tan(sunAngularRadius)
	float	cSunTravel[4];				// xyz light->receiver (receiverToLight = -xyz); w = CSM far distance f
	float	cSunBasisX[4];				// xyz stable light-space X; w = 0.9*f (static blend start)
	float	cSunBasisY[4];				// xyz stable light-space Y; w = local shadow-sampling radiance bound (0 = exact)
	float	cEyePosition[4];			// xyz; w = CSM near n
	float	cViewForward[4];			// xyz; receiverDistance = dot(positionWS - eye, xyz); w = 0
	float	cCascadeSplits[4];			// s1, s2, s3, s4 (= f)
	float	cCascadeBlend[4];			// w1, w2, w3 half blend widths at interior splits; w = 0
	float	cShadowDepthRecords[DX12_SHADOW_SUN_MAP_COUNT][4];	// near, far, worldUnitsPerTexel, 0 (CSM0..3, static sun 4)
	float	cSunWorldToClip[DX12_SHADOW_CSM_CASCADES][16];		// rendered projection * view per cascade
	float	cStaticSunWorldToClip[16];
	uint32	cCascadeRects[DX12_SHADOW_CSM_CASCADES][4];			// slotX, slotY, slotSize, 0 inside the cascade atlas
	uint32	cStaticSunRect[4];			// 0, 0, 4096, 0
	// Under DEFERRED each missing sun chart has zero rect, depth record and matrix.
	// Validity flags require initialized charts with retained target leases.
	uint32	cSunIdentity[4];			// sun lightId (0xFFFFFFFF none), sunStyle, resident GPU-index tail offset/count in t1028
};

struct DX12ProjectedLightDesc			// 128 bytes, byte-identical to HLSL PBRSpotGpu
{
	float	worldToTexture[16];			// row-major ClientShadow_t::m_WorldToShadow
	float	origin[3], farZ;
	float	color[3], constantAttn;		// color pre-scaled by the client (HDR / sRGB-blend factors applied)
	float	linearAttn, quadraticAttn, shadowAtten, filterTexels;	// filterTexels = 1 / depth texture width
	int32	cookieSlot, depthSlot;		// 0..DX12_PBR_MAX_PROJECTED_LIGHTS-1; depthSlot -1 = unshadowed
	uint32	flags;						// DX12_PBR_LIGHT_*
	uint32	reserved;
};

struct DX12ProjectedLightPacket
{
	uint32	count;						// 0..DX12_PBR_MAX_PROJECTED_LIGHTS
	const DX12ProjectedLightDesc *lights;
};

struct alignas( 16 ) DX12ProbeConstantsV1	// 64 bytes, cbuffer b0 space4, pixel stage (HLSL twin: pbr_probe.hlsli)
{
	float	cProbeOrigin[4];			// xyz grid origin; w = 1 / spacing
	uint32	cProbeBricks[4];			// xyz indirection brick dims; w = 1 enabled (0: every pixel takes the engine ambient cube)
	float	cProbeAtlas[4];				// xyz 1 / (atlasBricks * 5); w = normal bias = 0.25 * spacing
	uint32	cProbeAtlasBricks[4];		// xyz atlasBricks; w = 0
};

COMPILE_TIME_ASSERT( sizeof( RuntimeShadowLightGpu ) == 608 );
COMPILE_TIME_ASSERT( sizeof( DX12LightingViewConstantsV1 ) == 672 );
COMPILE_TIME_ASSERT( sizeof( DX12ProjectedLightDesc ) == 128 );
COMPILE_TIME_ASSERT( sizeof( DX12ProbeConstantsV1 ) == 64 );

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
// Cumulative actual lit-model draws since admission; snapshots never print per draw.
struct DX12StaticPropVisibilityStats
{
	uint32 mapGeneration, registeredProps;
	uint64 mappedDraws, unmatchedDraws, movedDraws, ambiguousDraws, topologyCacheBuilds;
	uint64 modelDraws, authoredModelDraws, registeredReceiverDraws, registeredReceiverFallbackDraws;
	uint64 reasonCounts[DX12_PROP_VISIBILITY_REASON_COUNT];
	uint64 detailOverflowDraws;
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
	uint32	highresRoute;				// 1: v5 selected-light map with required v3 baked direct/visibility; no inverse receiver spans
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
	const uint32 *tileRanges;			// 2 * tileCount entries: offset, count into the CSR prefix of tileIndices
	uint32	tileCount;					// tileCountX * tileCountY
	const uint32 *tileIndices;			// CSR prefix, then ascending GPU indices for every light with realtimeWeight > 0
	uint32	tileIndexCount;				// includes resident tail; cSunIdentity.zw describes it (zero/zero allowed on ordinary views)
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
	// Queued exact static-prop scope. Copies the entire mesh array before returning.
	// No name, position, vertex-color or pooled-color-offset identity inference.
	virtual void BeginStaticPropReceiver( const DX12StaticPropReceiver &receiver ) = 0;
	virtual void EndStaticPropReceiver() = 0;
	// Synchronous copied registration; 0 meshes unregisters the renderable token.
	// Draw selection uses exact hardware mesh and exact authored rigid matrix,
	// never material/color streams. Ambiguous coincident instances have no domain.
	virtual void RegisterStaticPropReceiver( uint64 renderableToken, const DX12StaticPropReceiver &receiver ) = 0;
	virtual void GetStaticPropVisibilityStats( uint32 mapGeneration, DX12StaticPropVisibilityStats &stats ) = 0;
	// Synchronous copied model/LOD/mesh labels; never receiver identity.
	virtual void RegisterModelMeshMetadata( const DX12ModelMeshMetadata &metadata ) = 0;
	// Synchronous bounded snapshot; labels borrowed during callback only.
	// Unknown/nonstatic models and unmatched authored-model poses are not proof
	// of a static-prop failure. REGISTERED_RECEIVER identifies proven scope/pose.
	virtual void GetStaticPropVisibilityDetails( uint32 mapGeneration, IDX12StaticPropVisibilityDetailsSink &sink ) = 0;

	// PBR adapter mode. Requested by the stdshader_dx12 cvar callback; committed by the client at FRAME_START
	// inside a material transaction, immediately before refreshing loaded materials.
	virtual void RequestPbrOverride( bool enabled ) = 0;		// relaxed atomic store
	virtual bool PbrOverridePending() = 0;						// requested != committed
	virtual bool CommitPbrOverride() = 0;						// committed = requested; returns true when it changed
	virtual bool PbrOverride() = 0;								// committed value read by material snapshots/draws
	// Material-thread quiescence for refreshing loaded materials on any map (no native domain needed).
	virtual bool BeginMaterialTransaction() = 0;				// main thread; nests
	virtual void EndMaterialTransaction() = 0;
	// Projected (flashlight / env_projectedtexture) lights for PBR shaders.
	// Called by the DX12_PBRLights material on the recording owner immediately before BeginProjectedLights.
	// Slots >= count (and any invalid handle) bind a null view.
	virtual void SetProjectedLightTextures( const ShaderAPITextureHandle_t *cookies, const ShaderAPITextureHandle_t *depths, int count ) = 0;
	// Queued like BeginView: copies the records now; the descriptor table is built lazily by the first PBR draw of each
	// recording batch. The packet stays current until the next one. count 0 is valid.
	virtual void BeginProjectedLights( const DX12ProjectedLightPacket &packet ) = 0;
};

#endif // ISHADERAPIDX12LIGHTING_H
