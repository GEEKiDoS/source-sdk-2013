//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "c_baseanimating.h"
#include "c_breakableprop.h"
#include "c_physicsprop.h"
#include "c_props.h"
#include "c_basedoor.h"
#include <typeinfo>
#include "shadowmaps_dx12.h"
#include "shaderapi/ishaderapidx12highres.h"
#include "shadowmap_scene.h"
#include "viewrender.h"
#include "view.h"
#include "detailobjectsystem.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/imaterialvar.h"
#include "materialsystem/imaterialsystemhardwareconfig.h"
#include "tier1/utlbuffer.h"
#include "tier1/smartptr.h"
#include "engine/ivdebugoverlay.h"
#include "filesystem.h"
#include "studio.h"
#include "model_types.h"
#include "worldsize.h"
#include "tier0/threadtools.h"
#include "datacache/imdlcache.h"
#include "engine/ICollideable.h"
#include "gamebspfile.h"
#include "optimize.h"
#include "tier1/checksum_crc.h"
#include "tier1/utlmap.h"
#include "shaderapi/dx12staticpropvisibility.h"
#include "tier0/vprof.h"
#include "tier0/memdbgon.h"


ConVar r_shadowmap_enable( "r_shadowmap_enable", "1", FCVAR_ARCHIVE, "Enable runtime shadow maps; 0 retains selected direct lighting and all baked visibility", true, 0, true, 1 );
ConVar r_shadowmap_max_realtime_lights( "r_shadowmap_max_realtime_lights", "4", FCVAR_ARCHIVE, "Frame-global chart-resident local-light cap; 0 is all realtime, negative/nonfinite values use 4; sun excluded" );
ConVar r_shadowmap_realtime_fade_seconds( "r_shadowmap_realtime_fade_seconds", "0.5", FCVAR_ARCHIVE, "Real-time seconds per hybrid shadow ramp; 0 switches immediately, negative/nonfinite values use 0.5" );
ConVar r_csm_distance( "r_csm_distance", "4096", FCVAR_ARCHIVE, "Sun cascade receiver distance", true, 128, true, 32768 );
ConVar r_shadowmap_filter( "r_shadowmap_filter", "0", FCVAR_ARCHIVE, "0: four-tap PCF, 1: bounded contact-hardening PCSS", true, 0, true, 1 );
ConVar r_shadowmap_skip_radiance( "r_shadowmap_skip_radiance", "0.0009765625", FCVAR_ARCHIVE, "Skip local shadow sampling below this total unshadowed direct RGB contribution; retain all light energy, 0 samples exactly", true, 0.0f, false, 0.0f );
ConVar r_shadowmap_spot_near( "r_shadowmap_spot_near", "4", FCVAR_ARCHIVE, "Spotlight shadow-camera near distance in Source units (including six-face spots); does not change illumination", true, 0.1f, true, 64.0f );
ConVar r_shadowmap_autoexec( "r_shadowmap_autoexec", "", FCVAR_CHEAT, "Acceptance automation: cfg exec'd once per map after the first completed main receiver view" );
ConVar r_shadowmap_debug( "r_shadowmap_debug", "0", FCVAR_CHEAT, "0 normal, 1 cascades, 2 visibility, 3 pages/faces, 4 caster bounds, 5 blockers, 6 radius; debug builds also check projection math" );

namespace
{
struct ShadowFullRestoreReasons_t
{
	uint32 unknownPrev, rebuild, matrix, depth, slot, target, source, cow;
	uint32 rope, customDraw, nonfinite, wNonPositive, invalidBounds, unknownGeometry, queuedRope, boundsCoverChart;
};
struct ShadowStats_t
{
	uint32 selectedLights, relevantLights, pages, casters, staticRedraws, dynamicRedraws, depthCopies, csrEntries, residencyFailures;
	uint32 depthRenders, deferredCharts, initializingCharts, reusedCharts, pageCopies;
	uint32 crossFrameReusedCharts, crossFrameReusedSunCharts, uncertifiedCasters;
	uint32 rigidLayerRebuilds, overlayCasterDraws;
	uint32 rigidLayerBypasses, invalidRigidLayers;
	uint32 realtimeLights, positiveWeightLocals, bakedOnlyLocals, transitioningLocals, promotions, demotions;
	uint64 footprintRestorePixels; // Actual work restores, including full charts; excludes rigid builds/COW.
	uint32 fullRestores;
	ShadowFullRestoreReasons_t fullRestoreReasons;
};
struct ShadowVolumeReport_t
{
	int light;
	ShadowCasterVolume_t volume;
};
struct ShadowTileRect_t { int x0, y0, x1, y1; };
struct ShadowLightRank_t
{
	int relevantIndex;
	uint32 lightId;
	float score, weight;
	bool resident, desired;
};
struct ShadowChartCaster_t
{
	IClientRenderable *renderable;
	EHANDLE entity;
	bool immutable, staticProp;
	bool certified, stableAtCommit, layerRigid;
	int casterIndex, contentFirst, contentBytes;
	uint64 notificationRevision, registrationRevision;
};
enum ShadowRigidChangeFlags_t
{
	RIGID_NO_LAYER=1, RIGID_CLEAN_CHANGED=2, RIGID_PROJECTION_CHANGED=4, RIGID_DEPTH_CHANGED=8,
	RIGID_SLOT_CHANGED=16, RIGID_TARGET_CHANGED=32, RIGID_MEMBERSHIP_CHANGED=64,
	RIGID_REGISTRATION_CHANGED=128, RIGID_ENTITY_CHANGED=256, RIGID_MATERIAL_CHANGED=512,
	RIGID_DRAW_NOTIFICATION=1024, RIGID_PROOF_INVALIDATED=2048, RIGID_NO_CASTERS=4096
};
struct ShadowRigidChange_t
{
	ShadowRigidChange_t() : flags(0), keyFields(0) {}
	uint32 flags, keyFields;
	EHANDLE entity;
};
struct ShadowRigidEvent_t
{
	ShadowRigidChange_t change;
	int kind, light, face, action; // 0 rebuild, 1 direct-overlay bypass, 2 draw-proof rejection.
};
struct ShadowFrameViewStats_t
{
	uint32 views, depthRenders, rigidLayerRebuilds, overlayCasterDraws, rigidLayerBypasses, invalidRigidLayers;
};
struct ShadowCache_t
{
	ShadowCache_t() : staticGeneration(0), detailGeneration(0), slotGeneration(0), hadDynamic(false), dirty(true),
		valid(false), pending(false), pendingClean(false), pendingReady(false), pendingDynamic(false), pendingBillboards(false),
		pendingStaticGeneration(0), pendingDetailGeneration(0), pendingSlotGeneration(0),
		notifications(0), pendingNotifications(0), committedNotifications(0), committedFrame(-1),
		workTarget(0), pendingWorkTarget(0), hadBillboards(false),
		registrationGeneration(0), pendingRegistrationGeneration(0), pendingContentGeneration(0), reusableContent(false),
		rigidValid(false), pendingRigidReady(false), pendingRigidInputsInvalidated(false), rigidInvalidReason(0), rigidTarget(0), pendingRigidTarget(0),
		pendingRigidCasters(false), pendingOverlayCasters(false), footprintKnown(false), footprintCowInvalidated(false),
		footprintReasons(0), pendingFootprintReasons(0),
		footprintWorkTarget(0), footprintSource(0), pendingFootprintWorkTarget(0), pendingFootprintSource(0)
	{
		memset(matrix,0,sizeof(matrix)); memset(depth,0,sizeof(depth));
		memset(pendingMatrix,0,sizeof(pendingMatrix)); memset(pendingDepth,0,sizeof(pendingDepth));
	}
	uint32 staticGeneration, detailGeneration, slotGeneration;
	float matrix[16], depth[4];
	bool hadDynamic, dirty, valid, pending, pendingClean, pendingReady, pendingDynamic, pendingBillboards;
	uint32 pendingStaticGeneration, pendingDetailGeneration, pendingSlotGeneration;
	float pendingMatrix[16], pendingDepth[4];
	CViewSetup view, pendingView;
	ShadowCasterVolume_t pendingVolume;
	// Reuse names one committed chart, never an earlier receiver's pixels.
	// Sun caches remain receiver-owned, including their physical work target.
	uint64 notifications, pendingNotifications, committedNotifications;
	int committedFrame;
	DX12ShadowTarget_t workTarget, pendingWorkTarget;
	bool hadBillboards;
	ShadowMapDetailOrientation_t detail, pendingDetail;
	CUtlVector<ShadowChartCaster_t> dynamicCasters, pendingDynamicCasters;
	CUtlVector<unsigned char> content, pendingContent;
	uint64 registrationGeneration, pendingRegistrationGeneration;
	uint64 pendingContentGeneration;
	bool reusableContent;
	bool rigidValid, pendingRigidReady, pendingRigidInputsInvalidated;
	uint32 rigidInvalidReason;
	DX12ShadowTarget_t rigidTarget, pendingRigidTarget;
	bool pendingRigidCasters, pendingOverlayCasters;
	bool footprintKnown, footprintCowInvalidated;
	uint32 footprintReasons, pendingFootprintReasons;
	ShadowTileRect_t footprint, pendingFootprint, footprintChart, pendingFootprintChart;
	// Deliberately NOT migrated by exact atlas COW: its next overlay must restore fully.
	DX12ShadowTarget_t footprintWorkTarget, footprintSource, pendingFootprintWorkTarget, pendingFootprintSource;
	CUtlVector<CSmartPtr<CShadowMapRopeFootprint> > ropeFootprintPool;
	CSmartPtr<CShadowMapRopeFootprint> ropeFootprint, pendingRopeFootprint;
};
struct LocalLight_t
{
	LocalLight_t() : projectionNear(0), projectionFar(0), faceCount(0), relevant(false), shadowRelevant(false),
		bakedLightIndex(0), realtimeWeight(0), score(0), resident(false), desired(false)
	{
		memset(faceVolumes,0,sizeof(faceVolumes));
		for ( int i=0;i<6;++i ) { page[i]=-1; slot[i]=-1; }
	}
	dworldlight_t world;
	DX12LightingSelectedLight selected;
	ShadowMapInfluenceVolume_t influence;
	ShadowCasterVolume_t faceVolumes[6];
	float projectionNear, projectionFar; // Shadow-camera cache key, independent of the conservative influence near.
	int faceCount, page[6], slot[6];
	ShadowCache_t cache[6];
	bool relevant, shadowRelevant;
	uint32 bakedLightIndex;
	float realtimeWeight, score;
	bool resident, desired; // Frame-global; weight-zero retirement may still own leased slots.
};
struct LocalPage_t
{
	LocalPage_t() : work(0), clean(0), rigid(0), generation(1), ownershipChanged(false) { for ( int i=0;i<64;++i ) owners[i]=-1; }
	DX12ShadowTarget_t work, clean, rigid;
	int owners[64];
	uint32 generation;
	bool ownershipChanged; // Detached rects need work COW before reuse if an old packet still leases the page.
};
struct SparePage_t { int page; DX12ShadowTarget_t target; };

IShaderAPIDX12Lighting *g_Lighting = NULL;
IShaderAPIDX12HighresLightmaps *g_Highres = NULL;

struct StaticPropReceiverPose
{
	uint32 ordinal, publishedGeneration;
	StaticPropLump_t authored;
};
struct StaticPropModelVisibility
{
	const studiohwdata_t *hardware;
	const studiohdr_t *header;
	const studioloddata_t *hardwareLODs;
	int rootLOD, lodCount, studioMeshCount;
	uint32 checksum, generation;
	bool valid;
	CUtlVector<DX12StaticPropMeshIdentity> meshes;
};
CUtlMap<IClientRenderable *, StaticPropReceiverPose, int> g_StaticPropReceivers( DefLessFunc(IClientRenderable *) );
CUtlMap<const model_t *, StaticPropModelVisibility *, int> g_StaticPropModels( DefLessFunc(const model_t *) );
uint32 g_StaticPropModelGeneration = 1;
bool g_StaticPropRegistrationComplete = false;
int g_StaticPropRegistrationRootLOD = -2147483647;

void ClearStaticPropModelVisibility()
{
	FOR_EACH_MAP( g_StaticPropModels, i ) delete g_StaticPropModels[i];
	g_StaticPropModels.Purge();
	g_StaticPropRegistrationComplete = false;
	FOR_EACH_MAP( g_StaticPropReceivers, prop ) g_StaticPropReceivers[prop].publishedGeneration = 0;
	if ( g_Lighting ) { DX12ModelMeshMetadata empty = {}; g_Lighting->RegisterModelMeshMetadata(empty); }
	if ( !++g_StaticPropModelGeneration ) ++g_StaticPropModelGeneration;
}

// VTX is only the exact referenced model's serialized topology, never a receiver
// identity key. Checking it against live hardware preserves canonical ordinals
// when the engine omits LODs above r_rootlod and repacks original vertex offsets.
template<class T>
const T *StaticPropVtxArray( const CUtlBuffer &bytes, const void *owner, int offset, int count )
{
	if ( offset < 0 || count < 0 ) return NULL;
	const uint8 *base = static_cast<const uint8 *>(bytes.Base());
	const uint64 start = static_cast<const uint8 *>(owner) - base;
	const uint64 at = start + uint32(offset), length = uint64(count) * sizeof(T);
	if ( at > uint64(bytes.TellPut()) || length > uint64(bytes.TellPut()) - at ) return NULL;
	return reinterpret_cast<const T *>(base + at);
}

void PublishModelMeshMetadata( const model_t *model, const studiohdr_t *hdr, const studiohwdata_t *hw )
{
	bool authoredStatic = false;
	FOR_EACH_MAP( g_StaticPropReceivers, prop )
		if ( g_StaticPropReceivers.Key(prop)->GetModel() == model ) { authoredStatic = true; break; }
	if ( !hw->m_pLODs ) return;
	for ( int body = 0; body < hdr->numbodyparts; ++body )
	{
		const mstudiobodyparts_t *bp = hdr->pBodypart(body);
		for ( int subModel = 0; subModel < bp->nummodels; ++subModel )
		{
			const mstudiomodel_t *sm = bp->pModel(subModel);
			for ( int lod = hw->m_RootLOD; lod < hw->m_NumLODs; ++lod )
			{
				if ( !hw->m_pLODs[lod].m_pMeshData ) continue;
				for ( int mesh = 0; mesh < sm->nummeshes; ++mesh )
				{
					const int meshID = sm->pMesh(mesh)->meshid;
					if ( meshID < 0 || meshID >= hw->m_NumStudioMeshes ) continue;
					const studiomeshdata_t &live = hw->m_pLODs[lod].m_pMeshData[meshID];
					for ( int group = 0; group < live.m_NumGroup; ++group )
					{
						if ( !live.m_pMeshGroup[group].m_pMesh ) continue;
						DX12ModelMeshMetadata metadata = {};
						metadata.meshToken = uint64(reinterpret_cast<uintptr_t>(live.m_pMeshGroup[group].m_pMesh));
						metadata.modelName = modelinfo->GetModelName(model); metadata.modelChecksum = hdr->checksum;
						metadata.bodyPart = body; metadata.subModel = subModel; metadata.lod = lod;
						metadata.studioMesh = mesh; metadata.stripGroup = group; metadata.staticPropModel = authoredStatic;
						g_Lighting->RegisterModelMeshMetadata(metadata);
					}
				}
			}
		}
	}
}

bool BuildStaticPropModelVisibility( const model_t *model, const studiohdr_t *hdr,
	const studiohwdata_t *hw, StaticPropModelVisibility &out )
{
	if ( !hdr || !hw || !(hdr->flags & STUDIOHDR_FLAGS_STATIC_PROP) ||
		hdr->numbones != 1 || hdr->numflexdesc || hw->m_NumLODs <= 0 ||
		hw->m_NumLODs > MAX_NUM_LODS || !hw->m_pLODs ) return false;
	char path[MAX_PATH];
	V_StripExtension( modelinfo->GetModelName(model), path, sizeof(path) );
	V_strncat( path, ".dx90.vtx", sizeof(path) );
	CUtlBuffer bytes;
	if ( !filesystem->ReadFile(path, "GAME", bytes) || bytes.TellPut() < int(sizeof(OptimizedModel::FileHeader_t)) ) return false;
	const OptimizedModel::FileHeader_t *vtx = static_cast<const OptimizedModel::FileHeader_t *>(bytes.Base());
	if ( vtx->version != OPTIMIZED_MODEL_FILE_VERSION || vtx->checkSum != hdr->checksum ||
		vtx->numLODs != hw->m_NumLODs || vtx->numBodyParts != hdr->numbodyparts ) return false;
	const OptimizedModel::BodyPartHeader_t *bodies =
		StaticPropVtxArray<OptimizedModel::BodyPartHeader_t>(bytes, vtx, vtx->bodyPartOffset, vtx->numBodyParts);
	if ( !bodies ) return false;
	uint32 ordinal = 0;
	uint64 originalBase = 0;
	for ( int body = 0; body < hdr->numbodyparts; ++body )
	{
		const mstudiobodyparts_t *bp = hdr->pBodypart(body);
		if ( bodies[body].numModels != bp->nummodels ) return false;
		const OptimizedModel::ModelHeader_t *models =
			StaticPropVtxArray<OptimizedModel::ModelHeader_t>(bytes, &bodies[body], bodies[body].modelOffset, bodies[body].numModels);
		if ( !models ) return false;
		for ( int mid = 0; mid < bp->nummodels; ++mid )
		{
			const mstudiomodel_t *sm = bp->pModel(mid);
			if ( models[mid].numLODs != vtx->numLODs ) return false;
			const OptimizedModel::ModelLODHeader_t *lods =
				StaticPropVtxArray<OptimizedModel::ModelLODHeader_t>(bytes, &models[mid], models[mid].lodOffset, models[mid].numLODs);
			if ( !lods ) return false;
			uint64 originalModelCount = 0;
			for ( int mesh = 0; mesh < sm->nummeshes; ++mesh )
			{
				const int count = sm->pMesh(mesh)->vertexdata.numLODVertexes[0];
				if ( count < 0 ) return false;
				originalModelCount += uint32(count);
			}
			for ( int lod = 0; lod < vtx->numLODs; ++lod )
			{
				if ( lods[lod].numMeshes != sm->nummeshes ) return false;
				const OptimizedModel::MeshHeader_t *meshes =
					StaticPropVtxArray<OptimizedModel::MeshHeader_t>(bytes, &lods[lod], lods[lod].meshOffset, lods[lod].numMeshes);
				if ( !meshes ) return false;
				uint64 originalMeshOffset = 0;
				for ( int mesh = 0; mesh < sm->nummeshes; ++mesh )
				{
					const mstudiomesh_t *mdlMesh = sm->pMesh(mesh);
					const OptimizedModel::StripGroupHeader_t *groups =
						StaticPropVtxArray<OptimizedModel::StripGroupHeader_t>(bytes, &meshes[mesh],
							meshes[mesh].stripGroupHeaderOffset, meshes[mesh].numStripGroups);
					if ( !groups || mdlMesh->meshid < 0 || mdlMesh->meshid >= hw->m_NumStudioMeshes ) return false;
					const studiomeshdata_t *live = hw->m_pLODs[lod].m_pMeshData ?
						&hw->m_pLODs[lod].m_pMeshData[mdlMesh->meshid] : NULL;
					if ( live && live->m_NumGroup != meshes[mesh].numStripGroups ) return false;
					for ( int group = 0; group < meshes[mesh].numStripGroups; ++group, ++ordinal )
					{
						const OptimizedModel::Vertex_t *vertices =
							StaticPropVtxArray<OptimizedModel::Vertex_t>(bytes, &groups[group], groups[group].vertOffset, groups[group].numVerts);
						if ( !vertices || ordinal == 0xffffffffu ) return false;
						if ( !live ) continue;
						const studiomeshgroup_t &actual = live->m_pMeshGroup[group];
						if ( actual.m_NumVertices != groups[group].numVerts ||
							(actual.m_NumVertices && !actual.m_pGroupIndexToMeshIndex) ) return false;
						if ( !actual.m_pMesh || !actual.m_NumVertices ) continue;
						CRC32_t crc; CRC32_Init(&crc);
						for ( int vi = 0; vi < actual.m_NumVertices; ++vi )
						{
							const uint32 sourceVertex = actual.m_pGroupIndexToMeshIndex[vi];
							const uint64 source = originalBase + originalMeshOffset + sourceVertex;
							if ( sourceVertex != vertices[vi].origMeshVertID ||
								sourceVertex >= uint32(mdlMesh->vertexdata.numLODVertexes[0]) || source > 0xffffffffu ) return false;
							const uint32 index = uint32(source);
							CRC32_ProcessBuffer(&crc, &index, sizeof(index));
						}
						CRC32_Final(&crc);
						DX12StaticPropMeshIdentity identity = {};
						identity.meshToken = uint64(reinterpret_cast<uintptr_t>(actual.m_pMesh));
						identity.meshOrdinal = ordinal; identity.lod = lod;
						identity.vertexCount = actual.m_NumVertices; identity.vertexOrderCRC32 = crc;
						out.meshes.AddToTail(identity);
					}
					originalMeshOffset += uint32(mdlMesh->vertexdata.numLODVertexes[0]);
				}
			}
			originalBase += originalModelCount;
			if ( originalBase > 0xffffffffu ) return false;
		}
	}
	return true;
}
uint32 g_MapGeneration = 0, g_ViewGeneration = 0, g_StaticGeneration = 1;
uint64 g_CasterRegistrationGeneration = 1;
uint64 g_CasterContentGeneration = 1;
bool g_Admitted = false, g_TransmitReady = false, g_MaterialFeature = false;
bool g_ClientLevelShutdown = false;
bool g_DeviceCallbacksRegistered = false;
bool g_DeviceResetPending = false, g_DeviceRestoreComplete = false;
bool g_ResourceResetInProgress = false;
bool g_RuntimeShadowsWereEnabled = true;
char g_Error[256] = "";
CUtlVector<DX12LightingSelectedLight> g_Selected;
CUtlVector<LocalLight_t> g_Locals;
CUtlVector<LocalPage_t> g_Pages;
CUtlVector<SparePage_t> g_PageSpares;
int g_Sun = -1;
uint32 g_SelectionFrame=0xffffffffu, g_FramePromotions=0, g_FrameDemotions=0;
bool g_FrameShadowsEnabled=true;
int g_DepartingLocal=-1;
bool g_HaveSelectionView=false;
CViewSetup g_LastSelectionView;
CUtlVector<ShadowLightRank_t> g_LightRanks;
ShadowMapClientMapState g_State;
uint32 g_MapCounter = 0;
char g_MapName[MAX_PATH] = "";

uint32 g_DepthFrame = 0xffffffffu, g_FrameDepthRenders = 0;
// LOCAL atlas COW only, not the four 2048-square sun cascade restores.
uint32 g_PageCopyFrame = 0xffffffffu, g_FramePageCopies = 0;
uint32 FramePageCopies() { return g_PageCopyFrame==g_DepthFrame ? g_FramePageCopies : 0; }
uint32 g_FrameRigidLayerRebuilds=0;
ShadowFrameViewStats_t g_FrameViewStats[SHADOWMAP_VIEW_VIEWMODEL+1]={};
CUtlVector<ShadowRigidEvent_t> g_FrameRigidEvents;
CUtlVector<EHANDLE> g_SpawnedRenderables;

class CShadowMapFixtureRenderable : public C_BaseAnimating
{
public:
	CShadowMapFixtureRenderable()
	{
		EnableDynamicModels();
		SetPlaybackRate( 0 );
	}
};

void DestroySpawnedRenderables()
{
	if ( !g_SpawnedRenderables.Count() )
		return;

	int destroyed = 0;
	for ( int i = 0; i < g_SpawnedRenderables.Count(); ++i )
	{
		C_BaseEntity *entity = g_SpawnedRenderables[i].Get();
		if ( entity )
		{
			entity->Release();
			++destroyed;
		}
	}
	g_SpawnedRenderables.Purge();
	Msg( "ShadowMapSpawnRenderables: destroyed=%d\n", destroyed );
}

CUtlVector<DX12LightingReceiverQuad> g_ReceiverQuads;

void ResetMapState()
{
	g_State.mapGeneration=0; g_State.featureMap=false; g_State.runtimeActive=false; g_State.failed=false;
	g_State.nativeMapGeneration=0;
	g_State.selectedMode=SHADOWMAP_MODE_LDR; g_State.error[0]='\0';
	// Invalidate every borrowed sidecar view before reusing its backing bytes.
	g_State.lights=NULL; g_State.lightCount=0; g_State.sunLightIndex=-1;
	memset(&g_State.manifest,0,sizeof(g_State.manifest)); g_State.rshdBytes.RemoveAll();
	g_ReceiverQuads.RemoveAll();
	g_State.worldMins.Init(); g_State.worldMaxs.Init();
}

class CShadowViewData;
CUtlVector<CShadowViewData *> g_ViewPool, g_ViewStack, g_PendingViews;
int g_OrdinaryAutoexecFrames = 0;
CShadowViewData *g_LastMain = NULL;
ShadowStats_t g_LastStats = {};

bool Fail( const char *error, char *dst = NULL, int bytes = 0 )
{
	V_strncpy( g_Error, error && *error ? error : SHADOWMAP_ERR_RESIDENCY, sizeof(g_Error) );
	g_Admitted = false;
	g_State.failed=g_State.featureMap; g_State.runtimeActive=false;
	V_strncpy(g_State.error,g_Error,sizeof(g_State.error));
	if ( dst && bytes > 0 ) V_strncpy( dst, g_Error, bytes );
	Warning( "%s\n", g_Error );
	// Admission failures are disconnected by CHLClient's caller. Runtime
	// failures reject receiver geometry immediately and request safe disconnect.
	if ( !dst && !g_ClientLevelShutdown ) engine->ClientCmd_Unrestricted("disconnect\n");
	return false;
}

void ResolveLightingInterface()
{
	CreateInterfaceFn factory=Sys_GetFactory("shaderapidx12");
	g_Lighting=factory?static_cast<IShaderAPIDX12Lighting *>(factory(SHADERAPIDX12_LIGHTING_INTERFACE_VERSION,NULL)):NULL;
	g_Highres=factory?static_cast<IShaderAPIDX12HighresLightmaps *>(factory(SHADERAPIDX12_HIGHRES_INTERFACE_VERSION,NULL)):NULL;
}

void ShadowMapReleaseFunc()
{
	if ( !ThreadInMainThread() ) Error("Shadowmaps: material release callback left the main thread\n");
	if ( g_ClientLevelShutdown || !g_State.runtimeActive || g_Error[0] ) return;
	// Do not touch targets or enqueue scopes while the material system owns its
	// release transaction. Its native callback has already invalidated the atlas.
	g_DeviceResetPending=true; g_DeviceRestoreComplete=false;
}

void ShadowMapRestoreFunc( int )
{
	if ( !ThreadInMainThread() ) Error("Shadowmaps: material restore callback left the main thread\n");
	if ( g_ClientLevelShutdown || !g_State.runtimeActive || g_Error[0] ) return;
	// Registration follows native domain admission, hence the certified forward
	// dispatcher reaches us after both engine and native bridge restoration.
	// Only latch here: ReloadMaterials/readmission must not reenter that lock.
	g_DeviceResetPending=true; g_DeviceRestoreComplete=true;
}

void RegisterDeviceCallbacks()
{
	if ( g_DeviceCallbacksRegistered ) return;
	materials->AddReleaseFunc(ShadowMapReleaseFunc);
	materials->AddRestoreFunc(ShadowMapRestoreFunc);
	g_DeviceCallbacksRegistered=true;
}


struct CShadowBspFile
{
	CShadowBspFile( const char *path ) : handle(g_pFullFileSystem->Open(path,"rb","GAME")), bytes(0)
	{ if ( handle!=FILESYSTEM_INVALID_HANDLE ) bytes=g_pFullFileSystem->Size(handle); }
	~CShadowBspFile() { if ( handle!=FILESYSTEM_INVALID_HANDLE ) g_pFullFileSystem->Close(handle); }
	FileHandle_t handle;
	uint32 bytes;
};

bool ReadBspBytes( void *context, uint32 offset, uint32 bytes, void *out )
{
	CShadowBspFile &file=*static_cast<CShadowBspFile *>(context);
	if ( file.handle==FILESYSTEM_INVALID_HANDLE || offset>INT_MAX || bytes>INT_MAX || (uint64)offset+bytes>file.bytes ) return false;
	g_pFullFileSystem->Seek(file.handle,(int)offset,FILESYSTEM_SEEK_HEAD);
	return g_pFullFileSystem->Read(out,(int)bytes,file.handle)==(int)bytes;
}

template<class T>
bool ReadReceiverBspRecord( CShadowBspFile &file, const ShadowMapBspLumpInfo &lump, uint32 index, T &out )
{
	const uint64 offset=uint64(lump.fileofs)+uint64(index)*sizeof(T);
	return !lump.uncompressedSize && lump.filelen%sizeof(T)==0 && index<lump.filelen/sizeof(T) &&
		offset<=0xFFFFFFFFu && ReadBspBytes(&file,uint32(offset),uint32(sizeof(T)),&out);
}

bool ReadReceiverQuads( CShadowBspFile &file, const ShadowMapBspLumpInfo *lumps, const ShadowMapModeView &mode )
{
	for ( uint32 i=0;i<mode.receiverFaceCount;++i )
	{
		const ShadowMapReceiverFaceDisk &receiver=mode.receiverFaces[i];
		if ( receiver.modelIndex || !(receiver.flags&SHADOWMAP_RECEIVER_DISPLACEMENT) ) continue;
		dface_t face; ddispinfo_t disp;
		if ( !ReadReceiverBspRecord(file,lumps[g_State.selectedMode==SHADOWMAP_MODE_HDR?LUMP_FACES_HDR:LUMP_FACES],receiver.dfaceIndex,face) ||
			face.dispinfo<0 || face.numedges!=4 || face.firstedge<0 ||
			!ReadReceiverBspRecord(file,lumps[LUMP_DISPINFO],face.dispinfo,disp) || disp.power<2 || disp.power>4 ) return false;
		Vector points[4];
		int start=-1; float closest=999999999.0f;
		for ( int v=0;v<4;++v )
		{
			int surfedge; dedge_t edge; dvertex_t vertex;
			if ( !ReadReceiverBspRecord(file,lumps[LUMP_SURFEDGES],uint32(face.firstedge)+v,surfedge) || surfedge==INT_MIN ||
				!ReadReceiverBspRecord(file,lumps[LUMP_EDGES],surfedge<0?-surfedge:surfedge,edge) ||
				!ReadReceiverBspRecord(file,lumps[LUMP_VERTEXES],edge.v[surfedge<0?1:0],vertex) ) return false;
			points[v]=vertex.point;
			// Same authored start-corner selection as CCoreDispSurface; this
			// does not identify a receiver or project onto nearby geometry.
			const float distance=(disp.startPosition-points[v]).LengthSqr();
			if ( distance<closest ) { closest=distance; start=v; }
		}
		if ( start<0 ) return false;
		DX12LightingReceiverQuad quad={};
		quad.faceIndex=i; quad.gridSize=(1u<<disp.power)+1;
		for ( int v=0;v<4;++v ) for ( int a=0;a<3;++a )
			quad.position[v][a]=points[(start+4-v)%4][a];
		g_ReceiverQuads.AddToTail(quad);
	}
	return true;
}

bool ReadMapMetadata( const char *mapName, char *error, int errorBytes )
{
	int size=engine->GameLumpSize(GAMELUMP_RESTIR_SHADOWMAPS);
	g_State.featureMap=size!=0;
	char name[MAX_PATH],path[MAX_PATH];
	V_strncpy(name,mapName && *mapName ? mapName : engine->GetLevelName(),sizeof(name));
	V_FixSlashes(name,'/'); V_StripExtension(name,name,sizeof(name));
	if ( !V_strnicmp(name,"maps/",5) ) V_snprintf(path,sizeof(path),"%s.bsp",name);
	else V_snprintf(path,sizeof(path),"maps/%s.bsp",name);
	if ( !g_pFullFileSystem ) return Fail(SHADOWMAP_ERR_INVALID_METADATA,error,errorBytes);
	CShadowBspFile file(path);
	ShadowMapBspLumpInfo lumps[HEADER_LUMPS];
	uint32 offset,bytes,version,flags; bool compressed;
	if ( !ShadowMap_ReadBspDirectory(ReadBspBytes,&file,lumps,offset,bytes,version,compressed,flags) ) return Fail(SHADOWMAP_ERR_INVALID_METADATA,error,errorBytes);
	if ( size==0 )
	{
		// A stripped BSP cannot fall back to ordinary receiver shaders when a
		// hand edit removed its sidecar. Inspect pak names, not lighting data.
		if ( flags & (LVLFLAGS_RUNTIME_SHADOWMAP_DIRECT_NONHDR|LVLFLAGS_RUNTIME_SHADOWMAP_DIRECT_HDR) )
		{
			g_State.featureMap=true;
			return Fail(SHADOWMAP_ERR_INVALID_METADATA ": orphan level flag",error,errorBytes);
		}
		bool orphanAsset=false;
		if ( !hlight::FindPakAsset(ReadBspBytes,&file,lumps[LUMP_PAKFILE],orphanAsset) || orphanAsset )
		{
			g_State.featureMap=true;
			return Fail(SHADOWMAP_ERR_INVALID_METADATA ": unreadable pak directory or orphan high-resolution asset",error,errorBytes);
		}
		return true;
	}
	if ( size<0 || compressed || bytes!=(uint32)size || version!=(uint32)engine->GameLumpVersion(GAMELUMP_RESTIR_SHADOWMAPS) ||
		(uint64)offset+bytes>file.bytes ) return Fail(SHADOWMAP_ERR_INVALID_METADATA,error,errorBytes);
	g_State.rshdBytes.SetCount(size);
	if ( !engine->LoadGameLump(GAMELUMP_RESTIR_SHADOWMAPS,g_State.rshdBytes.Base(),size) ) return Fail(SHADOWMAP_ERR_INVALID_METADATA,error,errorBytes);
	if ( version!=hlight::kManifestVersion )
		return Fail(SHADOWMAP_ERR_INVALID_METADATA ": legacy enhanced map; rebake with high-resolution lightmaps",error,errorBytes);
	char reason[256]={0};
	if ( !hlight::ValidateManifest(g_State.rshdBytes.Base(),(uint32)size,flags,g_State.manifest,reason,sizeof(reason)) )
		return Fail(reason,error,errorBytes);
	int modelIndex=modelinfo->GetModelIndex(path);
	const model_t *world=modelIndex>=0 ? modelinfo->GetModel(modelIndex) : NULL;
	if ( !world || modelinfo->GetModelType(world)!=mod_brush ) return Fail(SHADOWMAP_ERR_INVALID_METADATA,error,errorBytes);
	modelinfo->GetModelBounds(world,g_State.worldMins,g_State.worldMaxs);
	return true;
}
void DestroyTarget( DX12ShadowTarget_t &target )
{
	if ( target && g_Lighting ) g_Lighting->DestroyShadowDepthTarget( target );
	target = 0;
}
void CopyMatrix( const VMatrix &matrix, float out[16] ) { memcpy( out, &matrix[0][0], 16*sizeof(float) ); }
inline void CopyVector3( const Vector &source, float *destination )
{
	destination[0]=source.x; destination[1]=source.y; destination[2]=source.z;
}
Vector BoxCorner( const Vector &mins, const Vector &maxs, int index )
{
	return Vector( index&1 ? maxs.x : mins.x, index&2 ? maxs.y : mins.y, index&4 ? maxs.z : mins.z );
}
void AddBounds( Vector &mins, Vector &maxs, const Vector &lo, const Vector &hi )
{
	VectorMin( mins,lo,mins ); VectorMax( maxs,hi,maxs );
}
Vector Unproject( const VMatrix &inverse, float x, float y, float z )
{
	float w = inverse[3][0]*x+inverse[3][1]*y+inverse[3][2]*z+inverse[3][3];
	return Vector( (inverse[0][0]*x+inverse[0][1]*y+inverse[0][2]*z+inverse[0][3])/w,
		(inverse[1][0]*x+inverse[1][1]*y+inverse[1][2]*z+inverse[1][3])/w,
		(inverse[2][0]*x+inverse[2][1]*y+inverse[2][2]*z+inverse[2][3])/w );
}
bool ExtractVolume( const VMatrix &clip, ShadowCasterVolume_t &volume, const VMatrix *clipToWorld = NULL, Vector *corners = NULL )
{
	volume.m_nPlaneCount = 6;
	for ( int p=0;p<6;++p )
	{
		int axis=p/2; float sign=(p&1)?-1.0f:1.0f;
		float row[4];
		for ( int j=0;j<4;++j )
		{
			row[j] = p==4 ? clip[2][j] : clip[3][j]+sign*clip[axis][j];
			if ( !ShadowMap_IsFiniteFloat(row[j]) ) return Fail(SHADOWMAP_ERR_INVALID_METADATA);
		}
		Vector n(row[0],row[1],row[2]); float length=VectorNormalize(n);
		if ( !(length>0) || !ShadowMap_IsFiniteFloat(length) ) return Fail(SHADOWMAP_ERR_INVALID_METADATA);
		volume.m_Planes[p].m_Normal=n; volume.m_Planes[p].m_Dist=-row[3]/length;
		if ( !volume.m_Planes[p].m_Normal.IsValid() || !ShadowMap_IsFiniteFloat(volume.m_Planes[p].m_Dist) ) return Fail(SHADOWMAP_ERR_INVALID_METADATA);
	}
	VMatrix inverse;
	if ( !clipToWorld )
	{
		if ( !MatrixInverseGeneral(clip,inverse) ) return Fail(SHADOWMAP_ERR_INVALID_METADATA);
		clipToWorld = &inverse;
	}
	volume.m_vecMins.Init(FLT_MAX,FLT_MAX,FLT_MAX); volume.m_vecMaxs.Init(-FLT_MAX,-FLT_MAX,-FLT_MAX);
	for ( int i=0;i<8;++i )
	{
		Vector v=Unproject(*clipToWorld,i&1?1:-1,i&2?1:-1,i&4?1:0);
		if ( !v.IsValid() ) return Fail(SHADOWMAP_ERR_INVALID_METADATA);
		if ( corners ) corners[i]=v;
		AddBounds(volume.m_vecMins,volume.m_vecMaxs,v,v);
	}
	return true;
}
ShadowCasterVolume_t CasterVolume( const ShadowMapInfluenceVolume_t &v )
{
	ShadowCasterVolume_t out;
	out.m_vecMins=v.mins; out.m_vecMaxs=v.maxs; out.m_nPlaneCount=v.planeCount;
	memcpy(out.m_Planes,v.planes,sizeof(out.m_Planes)); return out;
}
bool BoxInVolume( const ShadowCasterVolume_t &volume, const Vector &mins, const Vector &maxs )
{
	for ( int a=0;a<3;++a ) if ( mins[a]>volume.m_vecMaxs[a] || maxs[a]<volume.m_vecMins[a] ) return false;
	for ( int p=0;p<volume.m_nPlaneCount;++p )
	{
		const Vector &n=volume.m_Planes[p].m_Normal;
		Vector support(n.x>=0?maxs.x:mins.x,n.y>=0?maxs.y:mins.y,n.z>=0?maxs.z:mins.z);
		if ( DotProduct(n,support)<volume.m_Planes[p].m_Dist ) return false;
	}
	return true;
}

// Backend calls copy arrays/acquire replay and GPU leases immediately. CPU pool
// entries remain immutable until GetStatus says their EndView replay completed.
// Poll only on the main thread: NEVER enqueue a client-DLL callback/functor.
class CShadowViewData : public CRefCounted<>
{
public:
	CShadowViewData() : active(false), lastUsedFrame(0xffffffffu), hasDetails(false), shadowsEnabled(true), drawCandidateFirst(-1), drawCandidateCount(0), cascadeClean(0), cascadeWork(0), cascadeRigid(0), staticSunClean(0), staticSun(0)
	{ memset(&packet,0,sizeof(packet)); }
	~CShadowViewData()
	{
		ReleaseLeases(); DestroyTarget(cascadeClean); DestroyTarget(cascadeWork); DestroyTarget(cascadeRigid);
		DestroyTarget(staticSunClean); DestroyTarget(staticSun);
	}
	bool Idle() const { return !active && GetRefCount()==1; }
	bool Lease( DX12ShadowTarget_t target )
	{
		VPROF_BUDGET( "ShadowMapsDX12::LeaseTarget", "Shadowmaps" );
		if ( !target ) return true;
		for ( int i=0;i<leasedTargets.Count();++i ) if ( leasedTargets[i]==target ) return true;
		IRefCounted *lease=g_Lighting->RetainShadowDepthTarget(target);
		if ( !lease ) return false;
		leasedTargets.AddToTail(target); leases.AddToTail(lease); return true;
	}
	void ReleaseLeases()
	{
		VPROF_BUDGET( "ShadowMapsDX12::ReleaseTargetLeases", "Shadowmaps" );
		for ( int i=0;i<leases.Count();++i ) leases[i]->Release();
		leases.RemoveAll(); leasedTargets.RemoveAll();
	}
	void Prepare()
	{
		active=true; ReleaseLeases();
		memset(&packet,0,sizeof(packet)); memset(&stats,0,sizeof(stats));
		// Freeze one finite setting for every spotlight face in this receiver packet.
		casterKeys.RemoveAll(); casterContent.RemoveAll();
		spotShadowNear=r_shadowmap_spot_near.GetFloat();
		spotShadowNear=ShadowMap_IsFiniteFloat(spotShadowNear) ? clamp(spotShadowNear,0.1f,64.0f) : 4.0f;
		realtimeLocals.RemoveAll(); lightRanks.RemoveAll();
		lights.RemoveAll(); targets.RemoveAll(); ranges.RemoveAll(); indices.RemoveAll(); rects.RemoveAll(); casters.RemoveAll(); volumes.RemoveAll(); relevant.RemoveAll();
		casterRanges.RemoveAll(); casterIndices.RemoveAll(); drawCandidateFirst=-1; drawCandidateCount=0;
	}
	bool active;
	ShadowMapReceiverViewKind_t kind;
	uint32 lastUsedFrame;
	CViewSetup receiver;
	ShadowMapDetailOrientation_t detail;
	bool hasDetails;
	bool shadowsEnabled;
	Vector detailMins,detailMaxs;
	float spotShadowNear;
	DX12LightingViewPacket packet;
	ShadowStats_t stats;
	CUtlVector<RuntimeShadowLightGpu> lights;
	CUtlVector<DX12ShadowTarget_t> targets, leasedTargets;
	CUtlVector<IRefCounted *> leases;
	CUtlVector<uint32> ranges, indices, tileRunIndices;
	CUtlVector<ShadowTileRect_t> rects;
	CUtlVector<ShadowMapSceneCaster_t> casters;
	CUtlVector<ShadowChartCaster_t> localFaceCasters;
	CUtlVector<ShadowChartCaster_t> casterKeys;
	CUtlVector<unsigned char> casterContent;
	uint64 casterContentGeneration;
	CUtlVector<ShadowVolumeReport_t> volumes;
	CUtlVector<int> relevant;
	CUtlVector<unsigned char> realtimeLocals; // Indexed exactly like relevant/lights; frozen before depth work.
	CUtlVector<ShadowLightRank_t> lightRanks;
	CUtlVector<uint32> casterRanges;
	CUtlVector<int> casterIndices;
	int drawCandidateFirst, drawCandidateCount;
	DX12ShadowTarget_t cascadeClean, cascadeWork, cascadeRigid, staticSunClean, staticSun;
	ShadowCache_t cascades[4];
	ShadowCasterVolume_t cascadeVolumes[4];
	ShadowCache_t staticSunCache;
	IDetailObjectSystem::ShadowReport_t detailReport;
};

// Fixed-size convex domain: the actual main frustum intersected with map bounds.
// Active-set projections cover face interiors, edge interiors and vertices;
// clamping a frustum AABB would rank points which are not receivers.
struct ShadowRankingDomain_t
{
	VPlane planes[12];
	Vector vertices[220]; // C(12,3), including harmless duplicate vertices.
	int planeCount, vertexCount;
	bool Contains( const Vector &point ) const
	{
		for ( int p=0;p<planeCount;++p )
			if ( DotProduct(point,planes[p].m_Normal)<planes[p].m_Dist-0.01f ) return false;
		return point.IsValid();
	}
	void Build( const ShadowCasterVolume_t &volume )
	{
		planeCount=volume.m_nPlaneCount; vertexCount=0;
		memcpy(planes,volume.m_Planes,planeCount*sizeof(VPlane));
		for ( int a=0;a<3;++a ) for ( int side=0;side<2;++side )
		{
			VPlane &plane=planes[planeCount++];
			plane.m_Normal.Init(); plane.m_Normal[a]=side ? -1.0f : 1.0f;
			plane.m_Dist=side ? -g_State.worldMaxs[a] : g_State.worldMins[a];
		}
		for ( int p=0;p<planeCount;++p ) for ( int q=p+1;q<planeCount;++q ) for ( int r=q+1;r<planeCount;++r )
		{
			const Vector cross=CrossProduct(planes[q].m_Normal,planes[r].m_Normal);
			const float determinant=DotProduct(planes[p].m_Normal,cross);
			if ( fabsf(determinant)<1e-7f ) continue;
			const Vector point=(cross*planes[p].m_Dist+
				CrossProduct(planes[r].m_Normal,planes[p].m_Normal)*planes[q].m_Dist+
				CrossProduct(planes[p].m_Normal,planes[q].m_Normal)*planes[r].m_Dist)/determinant;
			if ( Contains(point) ) vertices[vertexCount++]=point;
		}
	}
	Vector Nearest( const Vector &origin, float &farthestDistance ) const
	{
		Vector nearest=vertices[0]; float best=FLT_MAX, farthest=0;
		for ( int v=0;v<vertexCount;++v )
		{
			const float d=(vertices[v]-origin).LengthSqr();
			farthest=MAX(farthest,d);
			if ( d<best ) { best=d; nearest=vertices[v]; }
		}
		farthestDistance=sqrtf(farthest);
		if ( Contains(origin) ) return origin;
		for ( int p=0;p<planeCount;++p )
		{
			const Vector &n=planes[p].m_Normal;
			const float nn=n.LengthSqr(), d=planes[p].m_Dist-DotProduct(origin,n);
			Vector point=origin+n*(d/nn);
			if ( Contains(point) && (point-origin).LengthSqr()<best ) { best=(point-origin).LengthSqr(); nearest=point; }
			for ( int q=p+1;q<planeCount;++q )
			{
				const Vector &m=planes[q].m_Normal;
				const float mm=m.LengthSqr(), nm=DotProduct(n,m), determinant=nn*mm-nm*nm;
				if ( determinant<1e-7f ) continue;
				const float e=planes[q].m_Dist-DotProduct(origin,m);
				point=origin+n*((d*mm-e*nm)/determinant)+m*((e*nn-d*nm)/determinant);
				if ( Contains(point) && (point-origin).LengthSqr()<best ) { best=(point-origin).LengthSqr(); nearest=point; }
			}
		}
		return nearest;
	}
};

float HybridNonnegativeSetting( const char *text, float fallback )
{
	char *end=NULL;
	const float value=(float)strtod(text,&end);
	return end!=text && end && !*end && ShadowMap_IsFiniteFloat(value) && value>=0 ? value : fallback;
}

float LocalMinimumDenominator( const DX12LightingSelectedLight &light, float distance, float farthestDistance )
{
	const float lower=light.capDist>0 ? MIN(MAX(distance,1.0f),light.capDist) : MAX(distance,1.0f);
	float upper=MAX(farthestDistance,1.0f);
	if ( light.attenuationRadius>0 ) upper=MIN(upper,MAX(light.attenuationRadius,1.0f));
	if ( light.endFade>light.startFade ) upper=MIN(upper,MAX(light.endFade,1.0f));
	if ( light.capDist>0 ) upper=MIN(upper,light.capDist);
	float denominator=light.constantAttn+lower*light.linearAttn+lower*lower*light.quadraticAttn;
	const float endDenominator=light.constantAttn+upper*light.linearAttn+upper*upper*light.quadraticAttn;
	denominator=MIN(denominator,endDenominator);
	if ( light.quadraticAttn>0 )
	{
		const float stationary=clamp(-light.linearAttn/(2*light.quadraticAttn),lower,upper);
		denominator=MIN(denominator,light.constantAttn+stationary*light.linearAttn+stationary*stationary*light.quadraticAttn);
	}
	return denominator;
}

float BestReceiverConeDot( const ShadowRankingDomain_t &domain, const Vector &origin, const Vector &direction )
{
	// First test whether the exact central spot ray reaches the clipped domain.
	float nearDistance=0, farDistance=FLT_MAX;
	for ( int p=0;p<domain.planeCount;++p )
	{
		const VPlane &plane=domain.planes[p];
		const float slope=DotProduct(direction,plane.m_Normal), offset=plane.m_Dist-DotProduct(origin,plane.m_Normal);
		if ( fabsf(slope)<1e-7f ) { if ( offset>0 ) { farDistance=-1; break; } }
		else if ( slope>0 ) nearDistance=MAX(nearDistance,offset/slope);
		else farDistance=MIN(farDistance,offset/slope);
	}
	if ( farDistance>0 && farDistance>=nearDistance ) return 1;
	float best=-1;
	// If the ray misses, the angular maximum lies on the convex domain's
	// silhouette edges. All vertex pairs include those edges; extra chords are
	// valid receivers too. Optimize cosine analytically along each segment.
	for ( int v=0;v<domain.vertexCount;++v )
	{
		const Vector a=domain.vertices[v]-origin;
		const float aa=a.LengthSqr(), da=DotProduct(direction,a);
		if ( aa>0 ) best=MAX(best,da/sqrtf(aa));
		for ( int w=v+1;w<domain.vertexCount;++w )
		{
			const Vector edge=domain.vertices[w]-domain.vertices[v];
			const float ee=edge.LengthSqr(), ae=DotProduct(a,edge), de=DotProduct(direction,edge);
			const float divisor=de*ae-da*ee;
			if ( fabsf(divisor)<1e-12f ) continue;
			const float t=(da*ae-de*aa)/divisor;
			if ( t<=0 || t>=1 ) continue;
			const Vector point=a+edge*t;
			if ( point.LengthSqr()>0 ) best=MAX(best,DotProduct(direction,point)/point.Length());
		}
	}
	return clamp(best,-1.0f,1.0f);
}

float LocalContributionScore( const LocalLight_t &local, const ShadowRankingDomain_t &domain )
{
	const DX12LightingSelectedLight &light=local.selected;
	const float radiance=MAX(light.radiance[0],MAX(light.radiance[1],light.radiance[2]))*engine->LightStyleValue(light.style);
	if ( !(radiance>0) || !domain.vertexCount ) return 0;
	const Vector origin(light.origin[0],light.origin[1],light.origin[2]);
	float farthestDistance;
	const Vector offset=domain.Nearest(origin,farthestDistance)-origin;
	const float distance=offset.Length();
	const bool hardFade=light.endFade>light.startFade;
	if ( (light.attenuationRadius>0 && distance>light.attenuationRadius) || (hardFade && distance>light.endFade) ) return 0;
	// Estimate the supremum, not the shader's isolated zero at the emitter.
	// Arbitrarily close receivers use the exact one-unit attenuation limit.
	const float clampedDistance=MAX(distance,1.0f);
	const float denominator=LocalMinimumDenominator(light,distance,farthestDistance);
	float falloff=denominator>0 ? 1.0f/denominator : 0;
	if ( light.type==DX12_SHADOW_LIGHT_SPOT )
	{
		const Vector direction(light.direction[0],light.direction[1],light.direction[2]);
		const float coneDot=distance<1 ? BestReceiverConeDot(domain,origin,direction) : DotProduct(offset,direction)/distance;
		if ( coneDot<=light.outerConeCos ) return 0;
		float cone=1;
		if ( coneDot<=light.innerConeCos )
		{
			cone=clamp((coneDot-light.outerConeCos)/(light.innerConeCos-light.outerConeCos),0.0f,1.0f);
			if ( light.exponent!=0 && light.exponent!=1 ) cone=powf(cone,light.exponent);
		}
		falloff*=coneDot*cone;
	}
	if ( hardFade )
	{
		const float t=1.0f-clamp((clampedDistance-light.startFade)/(light.endFade-light.startFade),0.0f,1.0f);
		falloff*=t*t*t*(t*(t*6.0f-15.0f)+10.0f);
	}
	const float score=radiance*falloff;
	return ShadowMap_IsFiniteFloat(score) ? MAX(score,0.0f) : 0;
}

int LightRankCompare( const ShadowLightRank_t *a, const ShadowLightRank_t *b )
{
	if ( a->score!=b->score ) return a->score>b->score ? -1 : 1;
	return a->lightId<b->lightId ? -1 : a->lightId>b->lightId ? 1 : 0;
}

void ReleaseBakedSlots();
bool LocalHasSlots( const LocalLight_t &local )
{
	for ( int f=0;f<local.faceCount;++f ) if ( local.page[f]>=0 ) return true;
	return false;
}

bool HybridCameraCut( const CViewSetup &previous, const CViewSetup &current )
{
	Vector previousForward,currentForward;
	AngleVectors(previous.angles,&previousForward); AngleVectors(current.angles,&currentForward);
	return (current.origin-previous.origin).LengthSqr()>=256.0f*256.0f ||
		DotProduct(previousForward,currentForward)<=0.5f || fabsf(current.fov-previous.fov)>=15.0f ||
		current.m_bOrtho!=previous.m_bOrtho;
}

bool UpdateRealtimeSelection( const CViewSetup &setup )
{
	if ( g_SelectionFrame==(uint32)gpGlobals->framecount ) return true;
	VMatrix worldToView,projection,clip,worldToPixels;
	render->GetMatricesForView(setup,&worldToView,&projection,&clip,&worldToPixels);
	ShadowCasterVolume_t volume;
	if ( !ExtractVolume(clip,volume) ) return false;
	ShadowRankingDomain_t domain; domain.Build(volume);
	g_SelectionFrame=(uint32)gpGlobals->framecount;
	g_FramePromotions=g_FrameDemotions=0;
	g_FrameShadowsEnabled=ShadowMapsDX12_ShadowsEnabled();
	const float setting=HybridNonnegativeSetting(r_shadowmap_max_realtime_lights.GetString(),4.0f);
	const int cap=setting==0 ? 0 : setting>=INT_MAX ? INT_MAX : MAX((int)setting,1);
	const int limit=g_FrameShadowsEnabled ? (cap ? MIN(cap,g_Locals.Count()) : g_Locals.Count()) : 0;
	const float fadeSeconds=HybridNonnegativeSetting(r_shadowmap_realtime_fade_seconds.GetString(),0.5f);
	const float realDelta=gpGlobals->absoluteframetime;
	const float step=fadeSeconds>0 ? (ShadowMap_IsFiniteFloat(realDelta) ? MAX(realDelta,0.0f)/fadeSeconds : 0) : 1.0f;
	g_LightRanks.SetCount(g_Locals.Count());
	for ( int i=0;i<g_Locals.Count();++i )
	{
		LocalLight_t &local=g_Locals[i];
		const bool relevant=local.world.radius<=0 || (BoxInVolume(volume,local.influence.mins,local.influence.maxs) &&
			ShadowMapScene_VolumeIntersectsBox(local.influence,volume.m_vecMins,volume.m_vecMaxs));
		const bool occluded=relevant && local.world.radius>0 && !setup.m_bOrtho && engine->IsOccluded(local.influence.mins,local.influence.maxs);
		local.score=relevant && !occluded ? LocalContributionScore(local,domain) : 0;
		ShadowLightRank_t &rank=g_LightRanks[i];
		rank.relevantIndex=i; rank.lightId=local.selected.lightId; rank.score=local.score;
		rank.weight=local.realtimeWeight; rank.resident=local.resident; rank.desired=false;
		local.desired=false;
	}
	g_LightRanks.Sort(LightRankCompare);
	const bool instantSelection=!g_HaveSelectionView || HybridCameraCut(g_LastSelectionView,setup);
	int selected=0;
	for ( int r=0;!instantSelection && r<g_LightRanks.Count() && selected<limit;++r )
	{
		LocalLight_t &local=g_Locals[g_LightRanks[r].relevantIndex];
		if ( local.resident && (local.score>0 || !cap) ) { local.desired=true; ++selected; }
	}
	for ( int r=0;r<g_LightRanks.Count() && selected<limit;++r )
	{
		LocalLight_t &local=g_Locals[g_LightRanks[r].relevantIndex];
		if ( !local.desired && (local.score>0 || !cap) ) { local.desired=true; ++selected; }
	}
	for ( int r=0;!instantSelection && r<g_LightRanks.Count();++r )
	{
		LocalLight_t &challenger=g_Locals[g_LightRanks[r].relevantIndex];
		if ( challenger.desired || challenger.score<=0 ) continue;
		int weakest=-1;
		for ( int w=g_LightRanks.Count()-1;w>=0;--w )
			if ( g_Locals[g_LightRanks[w].relevantIndex].desired ) { weakest=g_LightRanks[w].relevantIndex; break; }
		if ( weakest<0 || !(challenger.score>g_Locals[weakest].score*1.25f) ) break;
		g_Locals[weakest].desired=false; challenger.desired=true;
	}
	// The first real scored view and camera cuts have no temporal relationship
	// to old pixels. Admit current top-N fully instead of fading stale cameras.
	if ( instantSelection )
	{
		g_DepartingLocal=-1;
		for ( int i=0;i<g_Locals.Count();++i )
		{
			LocalLight_t &local=g_Locals[i];
			if ( local.resident && !local.desired ) { local.resident=false; ++g_FrameDemotions; }
			local.realtimeWeight=local.resident ? 1.0f : 0.0f;
		}
	}
	int residents=0;
	for ( int i=0;i<g_Locals.Count();++i ) if ( g_Locals[i].resident ) ++residents;
	// A live budget reduction/disable must obey the new cap immediately.
	// Camera cuts above also switch immediately; replay leases retire separately.
	for ( int r=g_LightRanks.Count()-1;r>=0 && residents>limit;--r )
	{
		LocalLight_t &local=g_Locals[g_LightRanks[r].relevantIndex];
		if ( !local.resident ) continue;
		local.resident=false; local.realtimeWeight=0; --residents; ++g_FrameDemotions;
	}
	int departing=g_DepartingLocal;
	if ( departing>=0 && (!g_Locals[departing].resident || g_Locals[departing].desired) ) departing=-1;
	for ( int r=g_LightRanks.Count()-1;r>=0 && departing<0;--r )
	{
		LocalLight_t &local=g_Locals[g_LightRanks[r].relevantIndex];
		if ( local.resident && !local.desired ) { departing=g_LightRanks[r].relevantIndex; break; }
	}
	g_DepartingLocal=departing;
	for ( int i=0;i<g_Locals.Count();++i )
	{
		LocalLight_t &local=g_Locals[i];
		if ( !local.resident ) continue;
		local.realtimeWeight=!cap ? 1.0f : i==departing ? MAX(0.0f,local.realtimeWeight-step) : MIN(1.0f,local.realtimeWeight+step);
		if ( i==departing && local.realtimeWeight==0 ) { local.resident=false; --residents; ++g_FrameDemotions; g_DepartingLocal=-1; }
	}
	ReleaseBakedSlots();
	// Detached logical slots no longer occupy the budget. Leased old work
	// pixels are protected by ownershipChanged COW, never a whole-page idle wait.
	int occupied=residents;
	for ( int r=0;r<g_LightRanks.Count() && occupied<limit;++r )
	{
		LocalLight_t &local=g_Locals[g_LightRanks[r].relevantIndex];
		if ( !local.desired || local.resident ) continue;
		local.resident=true; local.realtimeWeight=!instantSelection && cap && fadeSeconds>0 ? 0 : 1;
		++occupied; ++g_FramePromotions;
	}
	for ( int i=0;i<g_Locals.Count();++i ) g_Locals[i].shadowRelevant=g_Locals[i].resident;
	if ( g_HaveSelectionView || (g_FrameShadowsEnabled && selected>0) )
	{
		g_LastSelectionView=setup; g_HaveSelectionView=true;
	}
	return true;
}

void SelectRealtimeLocals( CShadowViewData &data )
{
	data.realtimeLocals.SetCount(data.relevant.Count());
	data.lightRanks.SetCount(g_LightRanks.Count());
	data.stats.promotions=g_FramePromotions; data.stats.demotions=g_FrameDemotions;
	for ( int r=0;r<g_LightRanks.Count();++r )
	{
		data.lightRanks[r]=g_LightRanks[r];
		const LocalLight_t &local=g_Locals[g_LightRanks[r].relevantIndex];
		data.lightRanks[r].weight=local.realtimeWeight;
		data.lightRanks[r].resident=local.resident; data.lightRanks[r].desired=local.desired;
		if ( local.resident ) ++data.stats.realtimeLights;
		if ( local.resident && (local.realtimeWeight<1 || !local.desired) ) ++data.stats.transitioningLocals;
	}
	for ( int i=0;i<data.relevant.Count();++i )
	{
		const LocalLight_t &local=g_Locals[data.relevant[i]];
		data.realtimeLocals[i]=local.resident;
		if ( local.realtimeWeight>0 ) ++data.stats.positiveWeightLocals;
		else ++data.stats.bakedOnlyLocals;
	}
}

bool SameReceiverGeometry( const CViewSetup &a, const CViewSetup &b )
{
	if ( a.origin!=b.origin || a.angles!=b.angles || a.fov!=b.fov || a.zNear!=b.zNear || a.zFar!=b.zFar ||
		a.width!=b.width || a.height!=b.height || a.m_flAspectRatio!=b.m_flAspectRatio || a.m_eStereoEye!=b.m_eStereoEye ||
		a.m_bOrtho!=b.m_bOrtho || a.m_bOffCenter!=b.m_bOffCenter || a.m_bViewToProjectionOverride!=b.m_bViewToProjectionOverride ) return false;
	if ( a.m_bOrtho && (a.m_OrthoLeft!=b.m_OrthoLeft || a.m_OrthoRight!=b.m_OrthoRight || a.m_OrthoTop!=b.m_OrthoTop || a.m_OrthoBottom!=b.m_OrthoBottom) ) return false;
	if ( a.m_bOffCenter && (a.m_flOffCenterLeft!=b.m_flOffCenterLeft || a.m_flOffCenterRight!=b.m_flOffCenterRight || a.m_flOffCenterTop!=b.m_flOffCenterTop || a.m_flOffCenterBottom!=b.m_flOffCenterBottom) ) return false;
	return !a.m_bViewToProjectionOverride || !memcmp(&a.m_ViewToProjection[0][0],&b.m_ViewToProjection[0][0],16*sizeof(float));
}

CShadowViewData *AcquireView( const CViewSetup &setup, ShadowMapReceiverViewKind_t kind )
{
	VPROF_BUDGET( "ShadowMapsDX12::AcquireView", "Shadowmaps" );
	CShadowViewData *candidate=NULL;
	uint32 frame=(uint32)gpGlobals->framecount;
	// Preserve per-camera caches even when several monitor/water views execute
	// sequentially. Moving cameras reuse a same-kind entry unused in this frame.
	for ( int i=0;i<g_ViewPool.Count();++i )
	{
		CShadowViewData *data=g_ViewPool[i];
		if ( !data->Idle() || data->kind!=kind ) continue;
		if ( SameReceiverGeometry(data->receiver,setup) ) { candidate=data; break; }
		if ( !candidate && data->lastUsedFrame!=frame ) candidate=data;
	}
	if ( !candidate ) { candidate=new CShadowViewData; g_ViewPool.AddToTail(candidate); }
	candidate->lastUsedFrame=frame; candidate->Prepare(); return candidate;
}

bool PollCompletedViews()
{
	VPROF_BUDGET( "ShadowMapsDX12::PollCompletedViews", "Shadowmaps" );
	// Device recreation erased old lighting statuses. Do not interpret them as
	// failed views, release their CPU leases, or readmit before native restoration.
	if ( g_DeviceResetPending ) return !g_Error[0];
	if ( !g_Lighting ) return !g_Error[0];
	char error[256]={0};
	if ( g_Admitted && g_Highres && !g_ClientLevelShutdown && !g_Error[0] )
	{
		VPROF_BUDGET( "ShadowMapsDX12::PollCompletedViews.HighresStatus", "Shadowmaps" );
		DX12HighresMapStatus native={};
		g_Highres->GetStatus(native,error,sizeof(error));
		if ( native.state==DX12_HIGHRES_REJECTED || native.nativeMapGeneration!=g_State.nativeMapGeneration )
			Fail(error[0]?error:"Highres lightmaps: active native generation changed");
	}
	{
		VPROF_BUDGET( "ShadowMapsDX12::PollCompletedViews.MapStatus", "Shadowmaps" );
		if ( g_MapGeneration && !g_Error[0] && g_Lighting->GetStatus(g_MapGeneration,0,error,sizeof(error))==DX12_LIGHTING_STATUS_FAILED ) Fail(error);
	}
	int pending=0;
	{
		VPROF_BUDGET( "ShadowMapsDX12::PollCompletedViews.PendingViews", "Shadowmaps" );
		for ( int i=0;i<g_PendingViews.Count();++i )
		{
			CShadowViewData *data=g_PendingViews[i];
			DX12LightingStatus status=g_Lighting->GetStatus(data->packet.mapGeneration,data->packet.viewGeneration,error,sizeof(error));
			if ( status==DX12_LIGHTING_STATUS_PENDING )
			{
				if ( pending!=i ) g_PendingViews[pending]=data;
				++pending; continue;
			}
			if ( data->packet.mapGeneration==g_MapGeneration )
			{
				if ( status==DX12_LIGHTING_STATUS_FAILED && !g_Error[0] ) Fail(error);
				if ( status==DX12_LIGHTING_STATUS_READY )
				{
					g_LastStats=data->stats;
					if ( data->kind==SHADOWMAP_VIEW_MAIN )
					{
						data->AddRef();
						if ( g_LastMain ) g_LastMain->Release();
						g_LastMain=data;
					}
				}
			}
			data->ReleaseLeases(); data->Release();
		}
		g_PendingViews.SetCount(pending);
	}
	return !g_Error[0];
}

void CloseReceiverScopes()
{
	// Close in stack order before draining. Do not restore parent detail/leaf
	// orientation: no new receiver work may enter once teardown starts.
	while ( g_ViewStack.Count() )
	{
		CShadowViewData *data=g_ViewStack.Tail();
		g_ViewStack.Remove(g_ViewStack.Count()-1);
		g_Lighting->EndView(); data->active=false;
		data->AddRef(); g_PendingViews.AddToTail(data);
	}
}

bool AbortView( CShadowViewData &data )
{
	// Backend packets already own every target they recorded; no delayed client
	// callback is needed to unwind a failed CPU view.
	data.ReleaseLeases();
	data.active=false;
	return false;
}
void CopyDepthRect( CShadowViewData &data, DX12ShadowTarget_t dst, DX12ShadowTarget_t src, int x, int y, int size )
{
	VPROF_BUDGET( "ShadowMapsDX12::CopyDepthRect", "Shadowmaps" );
	if ( !data.Lease(dst) || !data.Lease(src) ) { Fail(SHADOWMAP_ERR_RESIDENCY); return; }
	g_Lighting->CopyShadowDepthRect(dst,src,x,y,x,y,size,size);
	++data.stats.depthCopies;
}

// Exact bytes, not a hash: no collision can retain an obsolete silhouette.
// Scratch and committed byte arrays retain capacity; collection is the only
// model/material visit, irrespective of the number of intersecting charts.
struct ShadowRigidEntityKey_t
{
	const model_t *model;
	matrix3x4_t renderToWorld;
	Vector renderOrigin, absOrigin;
	QAngle renderAngles, absAngles;
	float modulation[3];
	int entitySerial, renderHandle, skin, body, mode, fx, blend, effects, ready;
	int drawEntities, drawOtherModels, drawBrushModels, lod, rootLod, physicsLighting;
	color32 color;
};
struct ShadowRigidMaterialKey_t
{
	IMaterial *material;
	ITexture *texture;
	int alphaTest, translucent, noCull, noDraw, frame, shader;
	float alphaReference;
};
template<class T> void AppendRigidKey( CUtlVector<unsigned char> &bytes, const T &key )
{
	int first=bytes.AddMultipleToTail(sizeof(key));
	memcpy(bytes.Base()+first,&key,sizeof(key));
}
bool BuildRigidCasterContent( CShadowViewData &data, const ShadowMapSceneCaster_t &caster )
{
	IClientRenderable *renderable=caster.renderable;
	IClientUnknown *unknown=renderable->GetIClientUnknown();
	C_BaseEntity *entity=unknown ? unknown->GetBaseEntity() : NULL;
	const model_t *model=renderable->GetModel();
	// Engine static props not admitted to clean depth, custom renderables,
	// ropes, particles and details have no certified standard entity draw.
	if ( caster.staticProp || !entity || !model || renderable!=entity->GetClientRenderable() ||
		renderable->GetRenderClipPlane() || entity->GetMoveParent() || entity->IsFollowingEntity() ||
		entity->m_nRenderFX!=kRenderFxNone || entity->GetRenderMode()!=kRenderNormal || entity->HasBBoxVisualization() ) return false;
	// Handle resolution makes post-draw proof safe across arbitrary removal.
	EHANDLE entityHandle(entity);
	if ( entityHandle.Get()!=entity ) return false;
	static ConVarRef collisionWireframe("vcollide_wireframe"), materialWireframe("mat_wireframe"), renderBoxes("r_drawrenderboxes");
	if ( collisionWireframe.GetBool() || materialWireframe.GetInt() || renderBoxes.GetInt() ) return false;
	const std::type_info &type=typeid(*entity);
	int modelType=modelinfo->GetModelType(model);
	C_BaseAnimating *animating=NULL;
	if ( modelType==mod_studio )
	{
		// Exact runtime types, never an inherited opt-in: derived flex/ragdoll,
		// custom DrawModel/SetupBones/OnInternalDrawModel implementations fail.
		if ( type!=typeid(C_BaseAnimating) && type!=typeid(C_BreakableProp) &&
			type!=typeid(C_DynamicProp) && type!=typeid(C_PhysicsProp) &&
			type!=typeid(CShadowMapFixtureRenderable) ) return false;
		animating=static_cast<C_BaseAnimating *>(entity);
		CStudioHdr *drawStudio=animating->GetModelPtr();
		if ( !drawStudio || !drawStudio->SequencesAvailable() || animating->IsDynamicModelLoading() ) return false;
		studiohdr_t *studio=modelinfo->GetStudiomodel(model);
		// The static-prop SetupBones branch writes only the rigid root transform;
		// frozen cycles alone do NOT prove IK/transitions/layers/jiggle immutable.
		if ( !studio || !(studio->flags&STUDIOHDR_FLAGS_STATIC_PROP) || studio->numbones!=1 ||
			studio->numflexdesc || studio->numflexcontrollers || studio->numflexrules ||
			animating->IsRagdoll() || animating->IsAboutToRagdoll() || animating->GetModelScale()!=1.0f ) return false;
		// This one audited OnInternalDrawModel override changes lighting only,
		// except its sleep transition can reject the draw; never certify it then.
		if ( type==typeid(C_PhysicsProp) && !static_cast<C_PhysicsProp *>(entity)->CanReuseRigidShadowDepth() ) return false;
	}
	else if ( modelType!=mod_brush || (type!=typeid(C_BaseEntity) && type!=typeid(C_BaseDoor)) )
		return false;

	ShadowRigidEntityKey_t key;
	memset(&key,0,sizeof(key));
	key.model=model; memcpy(&key.renderToWorld,&renderable->RenderableToWorldTransform(),sizeof(key.renderToWorld));
	key.renderOrigin=renderable->GetRenderOrigin(); key.renderAngles=renderable->GetRenderAngles();
	key.absOrigin=entity->GetAbsOrigin(); key.absAngles=entity->GetAbsAngles();
	renderable->GetColorModulation(key.modulation);
	key.entitySerial=entity->GetRefEHandle().ToInt(); key.renderHandle=renderable->RenderHandle();
	key.skin=renderable->GetSkin(); key.body=renderable->GetBody();
	key.mode=entity->GetRenderMode(); key.fx=entity->m_nRenderFX; key.blend=renderable->GetFxBlend();
	key.color=entity->GetRenderColor(); key.effects=entity->GetEffects(); key.ready=entity->m_bReadyToDraw;
	static ConVarRef drawEntities("r_drawentities"), drawOtherModels("r_drawothermodels"), drawBrushModels("r_drawbrushmodels");
	static ConVarRef lod("r_lod"), rootLod("r_rootlod"), physicsLighting("r_PhysPropStaticLighting");
	key.drawEntities=drawEntities.GetInt(); key.drawOtherModels=drawOtherModels.GetInt(); key.drawBrushModels=drawBrushModels.GetInt();
	key.lod=lod.GetInt(); key.rootLod=rootLod.GetInt(); key.physicsLighting=physicsLighting.GetInt();
	// STATIC_PROP SetupBones copies only the render root (c_baseanimating.cpp).
	// Sequence/cycle/pose/controllers/hitbox state cannot deform this geometry.
	// Including those unrelated clocks would spuriously invalidate rigid depth.
	AppendRigidKey(data.casterContent,key);
	static CUtlVector<IMaterial *> modelMaterials;
	int count=modelinfo->GetModelMaterialCount(model);
	modelMaterials.SetCount(count);
	if ( count ) modelinfo->GetModelMaterials(model,count,modelMaterials.Base());
	for ( int i=0;i<count;++i )
	{
		IMaterial *material=modelMaterials[i];
		if ( !material || material->HasProxy() ) return false;
		bool found=false;
		IMaterialVar *sway=material->FindVar("$treesway",&found,false);
		if ( found && sway->GetIntValue()!=0 ) return false;
		ShadowRigidMaterialKey_t m;
		memset(&m,0,sizeof(m)); m.material=material;
		const char *shader=material->GetShaderName();
		if ( !V_stricmp(shader,"VertexLitGeneric") ) m.shader=1;
		else if ( !V_stricmp(shader,"LightmappedGeneric") ) m.shader=2;
		else if ( !V_stricmp(shader,"UnlitGeneric") ) m.shader=3;
		else return false; // Unversioned custom/deforming shader geometry.
		m.alphaTest=material->IsAlphaTested(); m.translucent=material->IsTranslucent();
		m.noCull=material->IsTwoSided(); m.noDraw=material->GetMaterialVarFlag(MATERIAL_VAR_NO_DRAW);
		if ( m.alphaTest )
		{
			IMaterialVar *texture=material->FindVar("$basetexture",&found,false);
			if ( found && texture->IsTexture() ) m.texture=texture->GetTextureValue();
			IMaterialVar *frame=material->FindVar("$frame",&found,false);
			if ( found ) m.frame=frame->GetIntValue();
			IMaterialVar *reference=material->FindVar("$AlphaTestReference",&found,false);
			if ( found ) m.alphaReference=reference->GetFloatValue();
		}
		AppendRigidKey(data.casterContent,m);
		// Texture pixels have no public mutation revision (Download/SwapContents).
		// Even equal texture/frame/reference cannot certify a cutout silhouette.
		if ( m.alphaTest || m.translucent ) return false;
	}
	return true;
}
void CollectRigidCasterContent( CShadowViewData &data, ShadowMapSceneCaster_t &caster )
{
	ShadowChartCaster_t key;
	key.renderable=caster.renderable; key.immutable=caster.immutable; key.staticProp=caster.staticProp;
	key.casterIndex=data.casters.Count();
	key.contentFirst=data.casterContent.Count(); key.contentBytes=0;
	key.certified=!caster.immutable && BuildRigidCasterContent(data,caster);
	key.stableAtCommit=false; key.layerRigid=false; caster.rigid=false;
	C_BaseEntity *entity=key.certified ? caster.renderable->GetIClientUnknown()->GetBaseEntity() : NULL;
	key.entity=entity;
	key.notificationRevision=entity ? entity->GetShadowDepthRevision() : 0;
	key.registrationRevision=entity ? entity->GetShadowDepthRegistrationRevision() : 0;
	if ( key.certified ) key.contentBytes=data.casterContent.Count()-key.contentFirst;
	else
	{
		data.casterContent.SetCount(key.contentFirst);
		if ( !caster.immutable ) ++data.stats.uncertifiedCasters;
	}
	data.casterKeys.AddToTail(key);
}

class CCollectCasters : public IShadowCasterSink
{
public:
	CCollectCasters( CShadowViewData &data ) : m_Data(data)
	{ m_Data.casterContentGeneration=g_CasterContentGeneration; }
	virtual void Add( IClientRenderable *renderable, bool isStaticProp )
	{
		ShadowMapSceneCaster_t caster;
		caster.renderable=renderable; caster.staticProp=isStaticProp;
		renderable->GetRenderBoundsWorldspace(caster.mins,caster.maxs);
		// Only engine static props with immutable geometry AND opacity can enter
		// clean depth. Brush entities, skinned/morphed models and proxies always move.
		const model_t *model=renderable->GetModel();
		studiohdr_t *studio=model && modelinfo->GetModelType(model)==mod_studio ? modelinfo->GetStudiomodel(model) : NULL;
		caster.immutable=isStaticProp && studio && (studio->flags&STUDIOHDR_FLAGS_STATIC_PROP) && studio->numflexdesc==0 && !modelinfo->ModelHasMaterialProxy(model);
		if ( caster.immutable )
		{
			int count=modelinfo->GetModelMaterialCount(model);
			m_Materials.SetCount(count); modelinfo->GetModelMaterials(model,count,m_Materials.Base());
			for ( int i=0;i<count;++i )
			{
				IMaterial *material=m_Materials[i];
				bool found=false;
				IMaterialVar *sway=material->FindVar("$treesway",&found,false);
				if ( material->HasProxy() || (found && sway->GetIntValue()!=0) ) { caster.immutable=false; break; }
			}
		}
		CollectRigidCasterContent(m_Data,caster);
		m_Data.casters.AddToTail(caster);
	}
private:
	CShadowViewData &m_Data;
	// Shared by every caster query; no material array allocation per prop.
	static CUtlVector<IMaterial *> m_Materials;
};
CUtlVector<IMaterial *> CCollectCasters::m_Materials;

struct ShadowCasterSnapshot_t
{
	ShadowCasterSnapshot_t() : frame(-1), mapGeneration(0), staticGeneration(0), uncertifiedCasters(0), registrationGeneration(0), contentGeneration(0) {}
	int frame;
	uint32 mapGeneration, staticGeneration, uncertifiedCasters;
	uint64 registrationGeneration, contentGeneration;
	ShadowCasterVolume_t query;
	CUtlVector<ShadowMapSceneCaster_t> casters;
	CUtlVector<ShadowChartCaster_t> keys;
	CUtlVector<unsigned char> content;
};
ShadowCasterSnapshot_t g_CasterSnapshot;

void CollectReceiverCasters( CShadowViewData &data, const ShadowCasterVolume_t &query )
{
	ShadowCasterSnapshot_t &snapshot=g_CasterSnapshot;
	static ConVarRef validateCasters("r_shadowmap_validate_casters");
	bool reuse=snapshot.frame==gpGlobals->framecount && snapshot.mapGeneration==g_MapGeneration &&
		snapshot.staticGeneration==g_StaticGeneration && snapshot.registrationGeneration==g_CasterRegistrationGeneration &&
		snapshot.contentGeneration==g_CasterContentGeneration &&
		snapshot.query.m_vecMins==query.m_vecMins && snapshot.query.m_vecMaxs==query.m_vecMaxs &&
		snapshot.query.m_nPlaneCount==query.m_nPlaneCount && !validateCasters.GetBool();
	for ( int p=0;reuse && p<query.m_nPlaneCount;++p )
		reuse=snapshot.query.m_Planes[p].m_Normal==query.m_Planes[p].m_Normal &&
			snapshot.query.m_Planes[p].m_Dist==query.m_Planes[p].m_Dist;
	if ( reuse )
	{
		VPROF_BUDGET( "ShadowMapsDX12::ReuseFrameCasters", "Shadowmaps" );
		data.casters.CopyArray(snapshot.casters.Base(),snapshot.casters.Count());
		data.casterKeys.CopyArray(snapshot.keys.Base(),snapshot.keys.Count());
		data.casterContent.CopyArray(snapshot.content.Base(),snapshot.content.Count());
		data.casterContentGeneration=snapshot.contentGeneration;
		data.stats.uncertifiedCasters=snapshot.uncertifiedCasters;
		return;
	}
	snapshot.frame=-1;
	const uint64 registrationGeneration=g_CasterRegistrationGeneration;
	const uint32 staticGeneration=g_StaticGeneration;
	CCollectCasters sink(data); ClientLeafSystem()->EnumerateShadowCasters(query,sink);
	// Bounds callbacks can invalidate the proof during enumeration. Never
	// publish a partial snapshot under the newer generation.
	if ( data.casterContentGeneration!=g_CasterContentGeneration ||
		registrationGeneration!=g_CasterRegistrationGeneration || staticGeneration!=g_StaticGeneration ) return;
	// Source's chart state is already shared within one frame. Freeze this
	// same-frame input tuple too, but always reread eligibility/materials on
	// the next frame. Copy before any chart assigns receiver-specific rigid flags.
	snapshot.query=query; snapshot.mapGeneration=g_MapGeneration; snapshot.staticGeneration=staticGeneration;
	snapshot.registrationGeneration=registrationGeneration; snapshot.contentGeneration=g_CasterContentGeneration;
	snapshot.uncertifiedCasters=data.stats.uncertifiedCasters;
	snapshot.casters.CopyArray(data.casters.Base(),data.casters.Count());
	snapshot.keys.CopyArray(data.casterKeys.Base(),data.casterKeys.Count());
	snapshot.content.CopyArray(data.casterContent.Base(),data.casterContent.Count());
	snapshot.frame=gpGlobals->framecount;
}

bool DistributeCasters( CShadowViewData &data )
{
	VPROF_BUDGET( "ShadowMapsDX12::DistributeCasters", "Shadowmaps" );
	data.casterRanges.SetCount(data.relevant.Count()*2);
	// A plane-free influence containing the aggregate caster AABB accepts
	// every caster using exactly the original per-axis intersection predicate.
	Vector casterMins(FLT_MAX,FLT_MAX,FLT_MAX), casterMaxs(-FLT_MAX,-FLT_MAX,-FLT_MAX);
	bool validBounds=true;
	for ( int c=0;c<data.casters.Count();++c )
	{
		const ShadowMapSceneCaster_t &caster=data.casters[c];
		validBounds=validBounds && caster.mins.IsValid() && caster.maxs.IsValid();
		for ( int a=0;a<3 && validBounds;++a ) validBounds=caster.mins[a]<=caster.maxs[a];
		AddBounds(casterMins,casterMaxs,caster.mins,caster.maxs);
	}
	int completeFirst=-1;
	for ( int l=0;l<data.relevant.Count();++l )
	{
		if ( !data.realtimeLocals[l] )
		{
			data.casterRanges[2*l]=data.casterIndices.Count(); data.casterRanges[2*l+1]=0;
			continue;
		}
		const ShadowMapInfluenceVolume_t &volume=g_Locals[data.relevant[l]].influence;
		bool complete=validBounds && volume.planeCount==0;
		for ( int a=0;a<3 && complete;++a )
			complete=casterMins[a]>=volume.mins[a] && casterMaxs[a]<=volume.maxs[a];
		if ( complete )
		{
			int count=data.casters.Count(), first=data.casterIndices.Count();
			if ( count>INT_MAX-first ) return Fail(SHADOWMAP_ERR_RESIDENCY);
			data.casterIndices.AddMultipleToTail(count);
			if ( count )
			{
				if ( completeFirst>=0 )
					memcpy(data.casterIndices.Base()+first,data.casterIndices.Base()+completeFirst,count*sizeof(int));
				else
				{
					for ( int c=0;c<count;++c ) data.casterIndices[first+c]=c;
					completeFirst=first;
				}
			}
			data.casterRanges[2*l]=first; data.casterRanges[2*l+1]=count;
			continue;
		}
		uint32 first=data.casterIndices.Count();
		// Cube candidates are gathered once here, then classified into expanded
		// face frusta by HasDynamic and the scene draw, not re-enumerated six times.
		for ( int c=0;c<data.casters.Count();++c )
		{
			const ShadowMapSceneCaster_t &caster=data.casters[c];
			if ( !ShadowMapScene_VolumeIntersectsBox(volume,caster.mins,caster.maxs) ) continue;
			if ( data.casterIndices.Count()==INT_MAX ) return Fail(SHADOWMAP_ERR_RESIDENCY);
			data.casterIndices.AddToTail(c);
		}
		data.casterRanges[2*l]=first;
		data.casterRanges[2*l+1]=data.casterIndices.Count()-first;
	}
	return true;
}

// Target shared-reference counts are a conservative idle test: registry + this
// temporary lease is two; packet, parent-view and GPU leases make it larger.
bool TargetIdle( DX12ShadowTarget_t target )
{
	if ( !target ) return true;
	IRefCounted *lease=g_Lighting->RetainShadowDepthTarget(target);
	if ( !lease ) return false;
	int references=lease->AddRef(); lease->Release(); lease->Release();
	return references==3;
}
bool PageTargetsIdle( int index )
{
	if ( !TargetIdle(g_Pages[index].work) || !TargetIdle(g_Pages[index].clean) || !TargetIdle(g_Pages[index].rigid) ) return false;
	for ( int i=0;i<g_PageSpares.Count();++i ) if ( g_PageSpares[i].page==index && !TargetIdle(g_PageSpares[i].target) ) return false;
	return true;
}
bool PageInactive( const LocalPage_t &page )
{
	for ( int s=0;s<64;++s ) if ( page.owners[s]>=0 && g_Locals[page.owners[s]].shadowRelevant ) return false;
	return true;
}
void EvictPage( int index )
{
	LocalPage_t &page=g_Pages[index];
	for ( int s=0;s<64;++s )
	{
		int owner=page.owners[s];
		if ( owner>=0 )
		{
			LocalLight_t &light=g_Locals[owner];
			// A cube transaction can span pages. Discard every pending step,
			// retaining completed work on the surviving pages but invalidating
			// clean keys that an abandoned replacement may have overwritten.
			for ( int f=0;f<light.faceCount;++f )
			{
				light.cache[f].pending=false; light.cache[f].staticGeneration=0; light.cache[f].dirty=true;
				light.cache[f].rigidValid=false;
			}
			for ( int f=0;f<light.faceCount;++f ) if ( light.page[f]==index )
			{
				light.page[f]=-1; light.slot[f]=-1;
				// Eviction invalidates pixels, not the immutable light projection.
				ShadowCache_t &cache=light.cache[f];
				cache.valid=false;
				cache.rigidValid=false;
				cache.staticGeneration=0; cache.detailGeneration=0; cache.slotGeneration=0;
				cache.hadDynamic=false; cache.dirty=true;
			}
		}
		page.owners[s]=-1;
	}
	DestroyTarget(page.work); DestroyTarget(page.clean); DestroyTarget(page.rigid); ++page.generation;
	for ( int i=g_PageSpares.Count()-1;i>=0;--i ) if ( g_PageSpares[i].page==index )
	{
		DestroyTarget(g_PageSpares[i].target); g_PageSpares.FastRemove(i);
	}
}
void DetachLocalFace( LocalPage_t &page, LocalLight_t &local, int face, int owner )
{
	Assert(page.owners[local.slot[face]]==owner);
	page.owners[local.slot[face]]=-1; page.ownershipChanged=true;
	local.page[face]=local.slot[face]=-1; local.cache[face]=ShadowCache_t();
}

void ReleaseBakedSlots()
{
	// Frame-boundary metadata detachment does not touch leased pixels. Waiting
	// for a whole shared page to become idle can starve forever while another
	// resident keeps rendering that page. Reuse is protected by work COW below.
	for ( int l=0;l<g_Locals.Count();++l )
	{
		LocalLight_t &local=g_Locals[l];
		if ( local.resident || !LocalHasSlots(local) ) continue;
		local.projectionNear=local.projectionFar=0;
		for ( int f=0;f<local.faceCount;++f )
		{
			if ( local.page[f]>=0 ) DetachLocalFace(g_Pages[local.page[f]],local,f,l);
			else local.cache[f]=ShadowCache_t();
		}
	}
	for ( int p=0;p<g_Pages.Count();++p )
	{
		bool empty=true;
		for ( int s=0;s<64;++s ) if ( g_Pages[p].owners[s]>=0 ) { empty=false; break; }
		if ( empty && g_Pages[p].work && PageTargetsIdle(p) ) EvictPage(p);
	}
}
bool CreatePage( int index )
{
	LocalPage_t &page=g_Pages[index];
	if ( page.work && page.clean && page.rigid ) return true;
	char name[80]; V_snprintf(name,sizeof(name),"shadow_local_%u_%d_work",g_MapGeneration,index);
	page.work=g_Lighting->CreateShadowDepthTarget(name,4096,4096);
	V_snprintf(name,sizeof(name),"shadow_local_%u_%d_static",g_MapGeneration,index);
	page.clean=g_Lighting->CreateShadowDepthTarget(name,4096,4096);
	V_snprintf(name,sizeof(name),"shadow_local_%u_%d_rigid",g_MapGeneration,index);
	page.rigid=g_Lighting->CreateShadowDepthTarget(name,4096,4096);
	if ( page.work && page.clean && page.rigid ) return true;
	DestroyTarget(page.work); DestroyTarget(page.clean); DestroyTarget(page.rigid); return false;
}
bool AllocateSlot( int owner, int face )
{
	VPROF_BUDGET( "ShadowMapsDX12::AllocateSlot", "Shadowmaps" );
	LocalLight_t &light=g_Locals[owner];
	if ( light.page[face]>=0 ) return true;
	for ( int p=0;p<g_Pages.Count();++p )
	{
		for ( int s=0;s<64;++s ) if ( g_Pages[p].owners[s]<0 )
		{
			if ( !CreatePage(p) ) break;
			g_Pages[p].owners[s]=owner; light.page[face]=p; light.slot[face]=s;
			return true;
		}
	}
	// Reclaim only a complete inactive page with no outstanding replay/GPU lease.
	for ( int p=0;p<g_Pages.Count();++p ) if ( PageInactive(g_Pages[p]) && PageTargetsIdle(p) )
	{
		EvictPage(p);
		if ( !CreatePage(p) ) continue;
		g_Pages[p].owners[0]=owner; light.page[face]=p; light.slot[face]=0; return true;
	}
	if ( g_Pages.Count()>=DX12_SHADOW_MAX_LOCAL_PAGES ) return false;
	int p=g_Pages.AddToTail();
	if ( !CreatePage(p) ) { g_Pages.Remove(p); return false; }
	g_Pages[p].owners[0]=owner; light.page[face]=p; light.slot[face]=0; return true;
}
bool ParentSamples( DX12ShadowTarget_t target )
{
	for ( int v=0;v<g_ViewStack.Count();++v )
	{
		const CShadowViewData &parent=*g_ViewStack[v];
		for ( int p=0;p<parent.targets.Count();++p ) if ( parent.targets[p]==target ) return true;
	}
	return false;
}
bool WritablePage( CShadowViewData &data, int pageIndex )
{
	VPROF_BUDGET( "ShadowMapsDX12::WritablePage", "Shadowmaps" );
	LocalPage_t &page=g_Pages[pageIndex];
	const bool parentSamples=ParentSamples(page.work);
	const bool ownershipCopy=page.ownershipChanged && !TargetIdle(page.work);
	if ( !parentSamples && !ownershipCopy ) { page.ownershipChanged=false; return true; }
	DX12ShadowTarget_t previous=page.work;
	DX12ShadowTarget_t next=0;
	for ( int i=0;i<g_PageSpares.Count();++i ) if ( g_PageSpares[i].page==pageIndex && TargetIdle(g_PageSpares[i].target) )
	{
		next=g_PageSpares[i].target; g_PageSpares.FastRemove(i); break;
	}
	if ( !next )
	{
		char name[80]; V_snprintf(name,sizeof(name),"shadow_local_%u_%d_nested_%u",g_MapGeneration,pageIndex,data.packet.viewGeneration);
		next=g_Lighting->CreateShadowDepthTarget(name,4096,4096);
		if ( !next )
		{
			for ( int p=0;p<g_Pages.Count();++p ) if ( p!=pageIndex && PageInactive(g_Pages[p]) && PageTargetsIdle(p) ) EvictPage(p);
			next=g_Lighting->CreateShadowDepthTarget(name,4096,4096);
		}
	}
	if ( !next ) return false;
	CopyDepthRect(data,next,previous,0,0,4096);
	if ( g_Error[0] ) { DestroyTarget(next); return false; }
	++data.stats.pageCopies;
	if ( g_PageCopyFrame!=(uint32)gpGlobals->framecount )
	{
		g_PageCopyFrame=(uint32)gpGlobals->framecount;
		g_FramePageCopies=0;
	}
	++g_FramePageCopies;
	page.work=next; page.ownershipChanged=false;
	// This exact full-page copy preserves every chart, including a pending
	// earlier cube face. Transfer identities before any subsequent chart write.
	// Clean/rigid intermediates are not sampled by receiver packets. Their
	// ordered depth/copy commands retain separate leases, so only work needs
	// COW; each chart's rigidTarget identity deliberately remains page.rigid.
	for ( int s=0;s<64;++s ) if ( page.owners[s]>=0 )
	{
		LocalLight_t &local=g_Locals[page.owners[s]];
		for ( int f=0;f<local.faceCount;++f ) if ( local.page[f]==pageIndex && local.slot[f]==s )
		{
			ShadowCache_t &cache=local.cache[f];
			// Force a full next overlay even if spare targets later cycle A->B->A.
			// Also invalidate an already-rendered pending cube face before commit.
			cache.footprintKnown=false; cache.pendingFootprintWorkTarget=0; cache.footprintCowInvalidated=true;
			if ( cache.workTarget==previous ) cache.workTarget=next;
			if ( cache.pendingWorkTarget==previous ) cache.pendingWorkTarget=next;
		}
	}
	SparePage_t spare; spare.page=pageIndex; spare.target=previous; g_PageSpares.AddToTail(spare);
	return true;
}

void SetupLightView( CViewSetup &setup, const Vector &origin, const Vector &forward, const Vector &up,
	const VMatrix &projection, float nearDepth, float farDepth, int size, bool ortho, float halfExtent, float tanHalfFov )
{
	setup=CViewSetup(); setup.origin=origin; VectorAngles(forward,up,setup.angles);
	setup.x=setup.y=setup.m_nUnscaledX=setup.m_nUnscaledY=0;
	setup.width=setup.height=setup.m_nUnscaledWidth=setup.m_nUnscaledHeight=size;
	setup.zNear=nearDepth; setup.zFar=farDepth; setup.zNearViewmodel=nearDepth; setup.zFarViewmodel=farDepth;
	setup.fov=setup.fovViewmodel=RAD2DEG(2*atanf(tanHalfFov)); setup.m_flAspectRatio=1;
	setup.m_bOrtho=ortho; setup.m_OrthoLeft=-halfExtent; setup.m_OrthoRight=halfExtent;
	setup.m_OrthoTop=-halfExtent; setup.m_OrthoBottom=halfExtent;
	setup.m_bDoBloomAndToneMapping=false;
	setup.m_bViewToProjectionOverride=true; setup.m_ViewToProjection=projection;
	// The engine camera convention is right/up/-forward, unlike the shared
	// +forward chart. Convert ONLY this engine projection; GPU records stay P*V.
	for ( int row=0;row<4;++row ) setup.m_ViewToProjection[row][2]=-projection[row][2];
}
int ChartCasterCompare( const ShadowChartCaster_t *a, const ShadowChartCaster_t *b )
{
	uintp x=(uintp)a->renderable,y=(uintp)b->renderable;
	if ( x!=y ) return x<y ? -1 : 1;
	if ( a->immutable!=b->immutable ) return a->immutable ? 1 : -1;
	return a->staticProp==b->staticProp ? 0 : a->staticProp ? 1 : -1;
}
void CollectLocalFaceCasters( CShadowViewData &data, const ShadowCasterVolume_t &volume )
{
	VPROF_BUDGET( "ShadowMapsDX12::CollectLocalFaceCasters", "Shadowmaps" );
	// Retain scratch capacity across faces/views; canonical order compares the
	// exact set, independent of leaf enumeration order, without hashes/copies.
	data.localFaceCasters.RemoveAll();
	int count=data.drawCandidateFirst>=0 ? data.drawCandidateCount : data.casters.Count();
	bool ordered=true;
	for ( int i=0;i<count;++i )
	{
		int index=data.drawCandidateFirst>=0 ? data.casterIndices[data.drawCandidateFirst+i] : i;
		const ShadowMapSceneCaster_t &caster=data.casters[index];
		if ( caster.immutable || !BoxInVolume(volume,caster.mins,caster.maxs) ) continue;
		const ShadowChartCaster_t &entry=data.casterKeys[index];
		if ( data.localFaceCasters.Count() && ChartCasterCompare(&data.localFaceCasters.Tail(),&entry)>0 ) ordered=false;
		data.localFaceCasters.AddToTail(entry);
	}
	if ( !ordered )
	{
		VPROF_BUDGET( "ShadowMapsDX12::CollectLocalFaceCasters.Sort", "Shadowmaps" );
		data.localFaceCasters.Sort(ChartCasterCompare);
	}
}
bool SameDetailOrientation( const ShadowMapDetailOrientation_t &a, const ShadowMapDetailOrientation_t &b )
{
	return a.m_nGeneration==b.m_nGeneration && a.m_vecViewOrigin==b.m_vecViewOrigin &&
		a.m_vecViewForward==b.m_vecViewForward && a.m_vecViewRight==b.m_vecViewRight && a.m_vecViewUp==b.m_vecViewUp;
}
bool ReuseCommittedChart( const CShadowViewData &data, const ShadowCache_t &cache, DX12ShadowTarget_t work,
	uint32 slotGeneration, bool billboards, uint32 detailGeneration )
{
	VPROF_BUDGET( "ShadowMapsDX12::ReuseCommittedChart", "Shadowmaps" );
	if ( !cache.valid || cache.pending || cache.committedFrame<0 || cache.committedFrame==gpGlobals->framecount || !cache.reusableContent ||
		cache.workTarget!=work || cache.staticGeneration!=g_StaticGeneration ||
		cache.slotGeneration!=slotGeneration || cache.registrationGeneration!=g_CasterRegistrationGeneration ||
		cache.detailGeneration!=detailGeneration || cache.hadBillboards!=billboards ||
		(billboards && !SameDetailOrientation(cache.detail,data.detail)) ||
		cache.dynamicCasters.Count()!=data.localFaceCasters.Count() ) return false;
	// Oriented detail geometry/materials have no rigid content certification.
	if ( billboards ) return false;
	for ( int i=0;i<cache.dynamicCasters.Count();++i )
	{
		const ShadowChartCaster_t &old=cache.dynamicCasters[i], &now=data.localFaceCasters[i];
		if ( ChartCasterCompare(&old,&now) || !old.certified || !now.certified ||
			old.contentBytes!=now.contentBytes ||
			memcmp(cache.content.Base()+old.contentFirst,data.casterContent.Base()+now.contentFirst,now.contentBytes) ) return false;
	}
	return true;
}
bool ReuseLocalChart( const CShadowViewData &data, const ShadowCache_t &cache, const LocalPage_t &page,
	float nearDepth, float farDepth, bool billboards, uint32 detailGeneration )
{
	VPROF_BUDGET( "ShadowMapsDX12::ReuseLocalChart", "Shadowmaps" );
	// Keep the established Source once-per-frame state boundary unchanged.
	// Certification is an additional shortcut only on a later frame.
	if ( cache.valid && !cache.pending && cache.committedFrame==gpGlobals->framecount &&
		cache.workTarget==page.work && cache.view.zNear==nearDepth && cache.view.zFar==farDepth &&
		cache.staticGeneration==g_StaticGeneration && cache.slotGeneration==page.generation &&
		cache.detailGeneration==detailGeneration && cache.committedNotifications==cache.notifications &&
		cache.hadBillboards==billboards && (!billboards || SameDetailOrientation(cache.detail,data.detail)) &&
		cache.dynamicCasters.Count()==data.localFaceCasters.Count() )
	{
		for ( int i=0;i<cache.dynamicCasters.Count();++i )
			if ( ChartCasterCompare(&cache.dynamicCasters[i],&data.localFaceCasters[i]) ) return false;
		return true;
	}
	return cache.view.zNear==nearDepth && cache.view.zFar==farDepth &&
		ReuseCommittedChart(data,cache,page.work,page.generation,billboards,detailGeneration);
}
void CaptureChartContent( CShadowViewData &data, ShadowCache_t &cache, DX12ShadowTarget_t work )
{
	cache.pendingDetail=data.detail; cache.pendingWorkTarget=work;
	cache.pendingContentGeneration=data.casterContentGeneration;
	cache.pendingDynamicCasters.RemoveAll(); cache.pendingContent.RemoveAll();
	cache.pendingRigidCasters=false; cache.pendingOverlayCasters=false;
	int oldIndex=0;
	for ( int i=0;i<data.localFaceCasters.Count();++i )
	{
		ShadowChartCaster_t key=data.localFaceCasters[i];
		int source=key.contentFirst;
		while ( oldIndex<cache.dynamicCasters.Count() && ChartCasterCompare(&cache.dynamicCasters[oldIndex],&key)<0 ) ++oldIndex;
		bool sameCaster=oldIndex<cache.dynamicCasters.Count() && !ChartCasterCompare(&cache.dynamicCasters[oldIndex],&key);
		key.stableAtCommit=false; key.layerRigid=false;
		if ( sameCaster )
		{
			const ShadowChartCaster_t &old=cache.dynamicCasters[oldIndex++];
			key.stableAtCommit=key.certified && old.certified && key.registrationRevision==old.registrationRevision &&
				key.contentBytes==old.contentBytes &&
				!memcmp(cache.content.Base()+old.contentFirst,data.casterContent.Base()+source,key.contentBytes);
			// A key that changed at the previous commit is still an overlay
			// input, even if this one sample happens to coincide (tick cadence).
			key.layerRigid=key.stableAtCommit && old.stableAtCommit;
		}
		data.casters[key.casterIndex].rigid=key.layerRigid;
		if ( key.layerRigid ) cache.pendingRigidCasters=true;
		else cache.pendingOverlayCasters=true;
		key.contentFirst=cache.pendingContent.Count();
		if ( key.contentBytes )
		{
			int first=cache.pendingContent.AddMultipleToTail(key.contentBytes);
			memcpy(cache.pendingContent.Base()+first,data.casterContent.Base()+source,key.contentBytes);
		}
		cache.pendingDynamicCasters.AddToTail(key);
	}
}
void AddReportVolume( CShadowViewData &data, int id, const ShadowCasterVolume_t &volume )
{
	VPROF_BUDGET( "ShadowMapsDX12::AddReportVolume", "Shadowmaps" );
	ShadowVolumeReport_t report; report.light=id; report.volume=volume; data.volumes.AddToTail(report);
}
bool RenderDepth( CShadowViewData &data, DX12ShadowTarget_t target, int x, int y, int size,
	const CViewSetup &lightView, const ShadowCasterVolume_t &volume, bool clear, bool world, bool immutable, bool dynamic, bool detail,
	bool orientedDetail = false, ShadowMapDynamicLayer_t dynamicLayer = SHADOWMAP_DYNAMIC_ALL,
	ShadowMapDepthFootprint_t *footprint = NULL, CShadowMapRopeFootprint *ropeFootprint = NULL )
{
	VPROF_BUDGET( "ShadowMapsDX12::RenderDepth", "Shadowmaps" );
	if ( !data.Lease(target) ) return Fail(SHADOWMAP_ERR_RESIDENCY);
	++g_FrameDepthRenders;
	++data.stats.depthRenders;
	ShadowMapDepthScene_t scene;
	scene.lightView=lightView; scene.receiverView=data.receiver; scene.volume=volume; scene.detail=data.detail;
	scene.casters=&data.casters; scene.lighting=g_Lighting; scene.target=target;
	scene.candidateIndices=data.drawCandidateFirst>=0 ? &data.casterIndices : NULL;
	scene.candidateFirst=data.drawCandidateFirst; scene.candidateCount=data.drawCandidateCount;
	scene.x=x; scene.y=y; scene.size=size; scene.clear=clear;
	scene.drawWorld=world; scene.drawStatic=immutable; scene.drawDynamic=dynamic; scene.drawDetail=detail; scene.orientedDetail=orientedDetail;
	scene.dynamicLayer=dynamicLayer;
	scene.overlayCasterDraws=dynamic ? &data.stats.overlayCasterDraws : NULL;
	if ( dynamicLayer==SHADOWMAP_DYNAMIC_RIGID ) scene.overlayCasterDraws=NULL;
	scene.overlayFootprint=footprint;
	scene.overlayRopeFootprint=ropeFootprint;
	{
		VPROF_BUDGET( "ShadowMapsDX12::RenderDepth.DrawScene", "Shadowmaps" );
		ViewRender_DrawShadowMapScene(scene);
	}
	if ( world || immutable || (detail && !orientedDetail) ) ++data.stats.staticRedraws;
	if ( dynamic || (detail && orientedDetail) ) ++data.stats.dynamicRedraws;
	return true;
}

void StartChart( ShadowCache_t &cache, const float matrix[16], const float depth[4], const CViewSetup &setup,
	const ShadowCasterVolume_t &volume, bool rebuild, bool dynamic, bool billboards, uint32 detailGeneration, uint32 slotGeneration )
{
	cache.pending=true; cache.pendingReady=false; cache.pendingClean=!rebuild;
	cache.pendingRopeFootprint=NULL;
	cache.pendingDynamic=dynamic; cache.pendingBillboards=billboards;
	cache.pendingStaticGeneration=g_StaticGeneration; cache.pendingDetailGeneration=detailGeneration; cache.pendingSlotGeneration=slotGeneration;
	// Capture before any clean/overlay draw, including callbacks from DrawModel.
	cache.pendingNotifications=cache.notifications;
	cache.pendingWorkTarget=0;
	cache.pendingRigidReady=false; cache.pendingRigidInputsInvalidated=false; cache.pendingRigidTarget=0;
	cache.pendingRigidCasters=false; cache.pendingOverlayCasters=false;
	cache.pendingRegistrationGeneration=g_CasterRegistrationGeneration;
	memcpy(cache.pendingMatrix,matrix,sizeof(cache.pendingMatrix));
	if ( depth ) memcpy(cache.pendingDepth,depth,sizeof(cache.pendingDepth));
	cache.pendingView=setup; cache.pendingVolume=volume;
}

// Only retained-layer casters participate in this proof. An actor's draw-time
// notification must not invalidate an unrelated sleeping prop's rigid layer.
// Handle resolution and per-entity registration stamps protect removal/re-add
// without invalidating unchanged rigids when an uncertain actor is registered.
bool PendingRigidInputsUnchanged( const ShadowCache_t &cache, ShadowRigidChange_t *change = NULL )
{
	uint32 failure=cache.pendingRigidInputsInvalidated ? RIGID_PROOF_INVALIDATED :
		(cache.pendingStaticGeneration!=g_StaticGeneration ? RIGID_CLEAN_CHANGED : 0);
	for ( int i=0;i<cache.pendingDynamicCasters.Count() && !failure;++i )
	{
		const ShadowChartCaster_t &key=cache.pendingDynamicCasters[i];
		if ( !key.layerRigid ) continue;
		C_BaseEntity *entity=key.entity.Get();
		if ( !entity ) failure=RIGID_MEMBERSHIP_CHANGED;
		else if ( entity->GetShadowDepthRegistrationRevision()!=key.registrationRevision ) failure=RIGID_REGISTRATION_CHANGED;
		else if ( entity->GetShadowDepthRevision()!=key.notificationRevision ) failure=RIGID_DRAW_NOTIFICATION;
		if ( failure && change ) change->entity=key.entity;
	}
	if ( change ) change->flags|=failure;
	return failure==0;
}
uint32 RigidEntityChangedFields( const unsigned char *old, const unsigned char *now )
{
	uint32 fields=0;
	// Only executed after an actual key mismatch, never on the warm path.
	if ( memcmp(old+offsetof(ShadowRigidEntityKey_t,renderToWorld),now+offsetof(ShadowRigidEntityKey_t,renderToWorld),
		offsetof(ShadowRigidEntityKey_t,modulation)-offsetof(ShadowRigidEntityKey_t,renderToWorld)) ) fields|=1;
	if ( memcmp(old,now,sizeof(void *)) ||
		memcmp(old+offsetof(ShadowRigidEntityKey_t,entitySerial),now+offsetof(ShadowRigidEntityKey_t,entitySerial),4*sizeof(int)) ) fields|=4;
	if ( memcmp(old+offsetof(ShadowRigidEntityKey_t,modulation),now+offsetof(ShadowRigidEntityKey_t,modulation),3*sizeof(float)) ||
		memcmp(old+offsetof(ShadowRigidEntityKey_t,mode),now+offsetof(ShadowRigidEntityKey_t,mode),5*sizeof(int)) ||
		memcmp(old+offsetof(ShadowRigidEntityKey_t,color),now+offsetof(ShadowRigidEntityKey_t,color),sizeof(color32)) ) fields|=8;
	if ( memcmp(old+offsetof(ShadowRigidEntityKey_t,drawEntities),now+offsetof(ShadowRigidEntityKey_t,drawEntities),6*sizeof(int)) ) fields|=16;
	return fields;
}
bool RigidLayerMatches( const ShadowCache_t &cache, DX12ShadowTarget_t rigid, ShadowRigidChange_t &change )
{
	if ( !cache.rigidValid ) change.flags|=cache.rigidInvalidReason ? cache.rigidInvalidReason : RIGID_NO_LAYER;
	if ( cache.rigidTarget!=rigid ) change.flags|=RIGID_TARGET_CHANGED;
	if ( cache.staticGeneration!=cache.pendingStaticGeneration ) change.flags|=RIGID_CLEAN_CHANGED;
	if ( cache.slotGeneration!=cache.pendingSlotGeneration ) change.flags|=RIGID_SLOT_CHANGED;
	if ( memcmp(cache.matrix,cache.pendingMatrix,sizeof(cache.matrix)) ) change.flags|=RIGID_PROJECTION_CHANGED;
	if ( memcmp(cache.depth,cache.pendingDepth,sizeof(cache.depth)) ) change.flags|=RIGID_DEPTH_CHANGED;
	// A changing clean chart or rejected draw proof already requires the full
	// overlay; do not scan unchanged rigid keys merely to diagnose another reason.
	if ( change.flags & ~(RIGID_NO_LAYER|RIGID_TARGET_CHANGED) ) return false;
	int oldIndex=0, newIndex=0;
	for ( ;; )
	{
		while ( oldIndex<cache.dynamicCasters.Count() && !cache.dynamicCasters[oldIndex].layerRigid ) ++oldIndex;
		while ( newIndex<cache.pendingDynamicCasters.Count() && !cache.pendingDynamicCasters[newIndex].layerRigid ) ++newIndex;
		bool oldEnd=oldIndex==cache.dynamicCasters.Count(), newEnd=newIndex==cache.pendingDynamicCasters.Count();
		if ( oldEnd || newEnd )
		{
			if ( oldEnd!=newEnd )
			{
				change.flags|=RIGID_MEMBERSHIP_CHANGED;
				change.entity=oldEnd ? cache.pendingDynamicCasters[newIndex].entity : cache.dynamicCasters[oldIndex].entity;
			}
			return change.flags==0;
		}
		const ShadowChartCaster_t &old=cache.dynamicCasters[oldIndex++], &now=cache.pendingDynamicCasters[newIndex++];
		if ( ChartCasterCompare(&old,&now) || old.contentBytes!=now.contentBytes )
		{
			change.flags|=RIGID_MEMBERSHIP_CHANGED; change.entity=now.entity; return false;
		}
		if ( old.registrationRevision!=now.registrationRevision )
		{
			change.flags|=RIGID_REGISTRATION_CHANGED; change.entity=now.entity; return false;
		}
		const unsigned char *oldBytes=cache.content.Base()+old.contentFirst, *newBytes=cache.pendingContent.Base()+now.contentFirst;
		if ( memcmp(oldBytes,newBytes,now.contentBytes) )
		{
			if ( memcmp(oldBytes,newBytes,sizeof(ShadowRigidEntityKey_t)) )
			{
				change.flags|=RIGID_ENTITY_CHANGED; change.keyFields=RigidEntityChangedFields(oldBytes,newBytes);
			}
			if ( memcmp(oldBytes+sizeof(ShadowRigidEntityKey_t),newBytes+sizeof(ShadowRigidEntityKey_t),
				now.contentBytes-sizeof(ShadowRigidEntityKey_t)) ) change.flags|=RIGID_MATERIAL_CHANGED;
			change.entity=now.entity; return false;
		}
	}
}
void RecordRigidEvent( const CShadowViewData &data, int light, int face, int action, const ShadowRigidChange_t &change )
{
	ShadowRigidEvent_t event;
	event.kind=data.kind; event.light=light; event.face=face; event.action=action; event.change=change;
	g_FrameRigidEvents.AddToTail(event);
}
CShadowMapRopeFootprint *AcquireRopeFootprint( ShadowCache_t &cache, int size )
{
	for ( int i=0;i<cache.ropeFootprintPool.Count();++i )
	{
		CShadowMapRopeFootprint *record=cache.ropeFootprintPool[i].GetObject();
		if ( record->CanReset() ) { record->Reset(cache.pendingMatrix,size); return record; }
	}
	CShadowMapRopeFootprint *record=new CShadowMapRopeFootprint;
	cache.ropeFootprintPool[cache.ropeFootprintPool.AddToTail()]=record;
	record->Release(); // Transfer the initial reference to the sharing pool entry.
	record->Reset(cache.pendingMatrix,size);
	return record;
}

bool RenderChart( CShadowViewData &data, ShadowCache_t &cache, DX12ShadowTarget_t clean, DX12ShadowTarget_t work,
	int x, int y, int size, DX12ShadowTarget_t rigid = 0, int chartLight = -5, int chartFace = 0 )
{
	VPROF_BUDGET( "ShadowMapsDX12::RenderChart", "Shadowmaps" );
	if ( !cache.pending || cache.pendingReady ) return true;
	bool baseRebuilt=!cache.pendingClean;
	// Required work is synchronous; no throttling or deferred visible updates.
	if ( !cache.pendingClean )
	{
		VPROF_BUDGET( "ShadowMapsDX12::RenderChart.CleanDepth", "Shadowmaps" );
		cache.rigidValid=false;
		if ( !RenderDepth(data,clean,x,y,size,cache.pendingView,cache.pendingVolume,true,true,true,false,
			DetailObjectSystem()->HasShadowCasters(cache.pendingVolume,false)) ) return false;
		cache.pendingClean=true;
	}
	DX12ShadowTarget_t source=clean;
	ShadowMapDynamicLayer_t overlayLayer=SHADOWMAP_DYNAMIC_ALL;
	bool overlayDynamic=cache.pendingDynamic;
	cache.pendingRigidTarget=rigid;
	ShadowRigidChange_t change;
	bool inputsUnchanged=rigid && PendingRigidInputsUnchanged(cache,&change);
	bool matches=inputsUnchanged && RigidLayerMatches(cache,rigid,change);
	// Do not build an extra layer for a currently changing chart. The normal
	// complete overlay commits current keys while stable peers qualify for the
	// intermediate. All changing inputs still draw in the current overlay.
	uint32 warmingFlags=RIGID_NO_LAYER|RIGID_TARGET_CHANGED;
	if ( !cache.rigidValid ) warmingFlags|=RIGID_MEMBERSHIP_CHANGED;
	bool stableInputs=cache.valid && !(change.flags & ~warmingFlags);
	if ( inputsUnchanged && cache.pendingRigidCasters && (matches || stableInputs) )
	{
		if ( !matches )
		{
			VPROF_BUDGET( "ShadowMapsDX12::RenderChart.RigidDepth", "Shadowmaps" );
			baseRebuilt=true;
			CopyDepthRect(data,rigid,clean,x,y,size);
			if ( g_Error[0] ) return false;
			if ( cache.pendingRigidCasters && !RenderDepth(data,rigid,x,y,size,cache.pendingView,cache.pendingVolume,
				false,false,false,true,false,false,SHADOWMAP_DYNAMIC_RIGID) ) return false;
			++data.stats.rigidLayerRebuilds;
			++g_FrameRigidLayerRebuilds;
			RecordRigidEvent(data,chartLight,chartFace,0,change);
		}
		// Private PSOs force D32 <=/write, fixed bias, no color or stencil:
		// min(clean,rigid,uncertain) is identical to the original draw union.
		// The persistent intermediate is clean + certified rigid casters only.
		// Receiver packets never sample it; their independently protected work
		// chart is restored before the ordinary dynamic/detail overlay.
		source=rigid; overlayLayer=SHADOWMAP_DYNAMIC_OVERLAY;
		overlayDynamic=cache.pendingOverlayCasters;
		cache.pendingRigidReady=true;
	}
	else if ( rigid )
	{
		if ( !cache.pendingRigidCasters ) change.flags|=RIGID_NO_CASTERS;
		++data.stats.rigidLayerBypasses;
		RecordRigidEvent(data,chartLight,chartFace,1,change);
	}
	// Work outside the previous overlay is already identical to this unchanged
	// clean/rigid base. Restore ONLY old pixels, then draw the whole new overlay.
	const bool ropePending=cache.ropeFootprint.GetObject() && !cache.ropeFootprint->IsComplete();
	ShadowTileRect_t previous={0,0,size,size};
	if ( cache.footprintKnown ) previous=cache.footprint;
	uint32 previousReasons=cache.footprintReasons;
	if ( cache.footprintKnown && cache.ropeFootprint.GetObject() && !ropePending )
	{
		const ShadowMapDepthFootprint_t &rope=cache.ropeFootprint->GetFootprint();
		previous.x0=MIN(previous.x0,rope.x0); previous.y0=MIN(previous.y0,rope.y0);
		previous.x1=MAX(previous.x1,rope.x1); previous.y1=MAX(previous.y1,rope.y1);
		previousReasons|=rope.reasons;
	}
	if ( ropePending ) previousReasons|=SHADOWMAP_FOOTPRINT_ROPE|SHADOWMAP_FOOTPRINT_QUEUED_ROPE;
	const bool unknownPrev=!cache.valid || !cache.footprintKnown || ropePending;
	const bool changedMatrix=memcmp(cache.matrix,cache.pendingMatrix,sizeof(cache.matrix))!=0;
	const bool changedDepth=memcmp(cache.depth,cache.pendingDepth,sizeof(cache.depth))!=0;
	const bool changedSlot=cache.slotGeneration!=cache.pendingSlotGeneration || (cache.footprintKnown &&
		(cache.footprintChart.x0!=x || cache.footprintChart.y0!=y || cache.footprintChart.x1!=x+size || cache.footprintChart.y1!=y+size));
	const bool changedTarget=cache.footprintWorkTarget!=work, changedSource=cache.footprintSource!=source;
	const bool rebuilt=baseRebuilt || cache.staticGeneration!=cache.pendingStaticGeneration;
	const bool partial=!(unknownPrev || rebuilt || changedMatrix || changedDepth || changedSlot || changedTarget || changedSource);
	ShadowTileRect_t restore={0,0,size,size};
	if ( partial ) restore=previous;
	const int width=restore.x1-restore.x0, height=restore.y1-restore.y0;
	if ( width>0 && height>0 )
	{
		if ( !data.Lease(work) || !data.Lease(source) ) return Fail(SHADOWMAP_ERR_RESIDENCY);
		g_Lighting->CopyShadowDepthRect(work,source,x+restore.x0,y+restore.y0,x+restore.x0,y+restore.y0,width,height);
		++data.stats.depthCopies;
		data.stats.footprintRestorePixels+=uint64(width)*height;
		if ( width==size && height==size )
		{
			++data.stats.fullRestores;
			ShadowFullRestoreReasons_t &r=data.stats.fullRestoreReasons;
			r.unknownPrev+=unknownPrev; r.rebuild+=rebuilt; r.matrix+=changedMatrix; r.depth+=changedDepth;
			r.slot+=changedSlot; r.target+=changedTarget; r.source+=changedSource; r.cow+=cache.footprintCowInvalidated;
			const uint32 why=previousReasons;
			r.rope+=(why&SHADOWMAP_FOOTPRINT_ROPE)!=0; r.customDraw+=(why&SHADOWMAP_FOOTPRINT_CUSTOM_DRAW)!=0;
			r.nonfinite+=(why&SHADOWMAP_FOOTPRINT_NONFINITE)!=0; r.wNonPositive+=(why&SHADOWMAP_FOOTPRINT_W_NONPOSITIVE)!=0;
			r.invalidBounds+=(why&SHADOWMAP_FOOTPRINT_INVALID_BOUNDS)!=0; r.unknownGeometry+=(why&SHADOWMAP_FOOTPRINT_UNKNOWN)!=0;
			r.queuedRope+=(why&SHADOWMAP_FOOTPRINT_QUEUED_ROPE)!=0;
			r.boundsCoverChart+=partial && !why;
		}
	}
	if ( g_Error[0] ) return false;
	ShadowMapDepthFootprint_t footprint;
	footprint.Reset(cache.pendingMatrix,size);
	if ( overlayDynamic ) cache.pendingRopeFootprint=AcquireRopeFootprint(cache,size);
	if ( overlayDynamic || cache.pendingBillboards )
	{
		VPROF_BUDGET( "ShadowMapsDX12::RenderChart.OverlayDepth", "Shadowmaps" );
		if ( !RenderDepth(data,work,x,y,size,cache.pendingView,cache.pendingVolume,false,false,false,
			overlayDynamic,cache.pendingBillboards,true,overlayLayer,&footprint,cache.pendingRopeFootprint.GetObject()) ) return false;
	}
	if ( g_Error[0] ) return false;
	cache.pendingFootprint.x0=footprint.x0; cache.pendingFootprint.y0=footprint.y0;
	cache.pendingFootprint.x1=footprint.x1; cache.pendingFootprint.y1=footprint.y1;
	cache.pendingFootprintReasons=footprint.reasons;
	cache.pendingFootprintChart.x0=x; cache.pendingFootprintChart.y0=y;
	cache.pendingFootprintChart.x1=x+size; cache.pendingFootprintChart.y1=y+size;
	cache.pendingFootprintWorkTarget=work; cache.pendingFootprintSource=source;
	cache.pendingReady=true; return true;
}

void CommitChart( CShadowViewData &data, ShadowCache_t &cache, int chartLight = -5, int chartFace = 0 )
{
	Assert(cache.pending && cache.pendingReady);
	memcpy(cache.matrix,cache.pendingMatrix,sizeof(cache.matrix)); memcpy(cache.depth,cache.pendingDepth,sizeof(cache.depth));
	cache.view=cache.pendingView;
	cache.staticGeneration=cache.pendingStaticGeneration; cache.slotGeneration=cache.pendingSlotGeneration;
	cache.hadDynamic=cache.pendingDynamic; cache.detailGeneration=cache.pendingDetailGeneration;
	cache.dirty=cache.pendingNotifications!=cache.notifications;
	cache.committedNotifications=cache.pendingNotifications;
	cache.valid=true; cache.pending=false;
	cache.footprint=cache.pendingFootprint; cache.footprintChart=cache.pendingFootprintChart;
	cache.footprintWorkTarget=cache.pendingFootprintWorkTarget; cache.footprintSource=cache.pendingFootprintSource;
	cache.footprintKnown=cache.footprintWorkTarget!=0;
	cache.footprintReasons=cache.pendingFootprintReasons;
	cache.ropeFootprint=cache.pendingRopeFootprint;
	cache.pendingRopeFootprint=NULL;
	if ( cache.footprintKnown ) cache.footprintCowInvalidated=false;
	if ( cache.pendingWorkTarget )
	{
		cache.rigidTarget=cache.pendingRigidTarget;
		ShadowRigidChange_t change;
		bool unchanged=PendingRigidInputsUnchanged(cache,&change);
		cache.rigidValid=cache.pendingRigidReady && unchanged;
		cache.rigidInvalidReason=unchanged ? 0 : change.flags;
		if ( cache.pendingRigidTarget && !unchanged )
		{
			if ( cache.pendingRigidReady ) ++data.stats.invalidRigidLayers;
			RecordRigidEvent(data,chartLight,chartFace,2,change);
		}
		cache.workTarget=cache.pendingWorkTarget;
		cache.registrationGeneration=cache.pendingRegistrationGeneration;
		cache.committedFrame=cache.dirty ? -1 : gpGlobals->framecount;
		cache.reusableContent=!cache.dirty && cache.pendingContentGeneration==g_CasterContentGeneration &&
			cache.pendingRegistrationGeneration==g_CasterRegistrationGeneration && cache.pendingStaticGeneration==g_StaticGeneration;
		cache.hadBillboards=cache.pendingBillboards;
		cache.detail=cache.pendingDetail;
		cache.dynamicCasters.Swap(cache.pendingDynamicCasters);
		cache.content.Swap(cache.pendingContent);
	}
}

bool BuildLocal( CShadowViewData &data, int index, int relevantIndex )
{
	VPROF_BUDGET( "ShadowMapsDX12::BuildLocal", "Shadowmaps" );
	LocalLight_t &local=g_Locals[index];
	if ( data.realtimeLocals[relevantIndex] )
	{
		data.drawCandidateFirst=data.casterRanges[relevantIndex*2]; data.drawCandidateCount=data.casterRanges[relevantIndex*2+1];
	}
	RuntimeShadowLightGpu &gpu=data.lights[data.lights.AddToTail()]; memset(&gpu,0,sizeof(gpu));
	const DX12LightingSelectedLight &light=local.selected;
	gpu.lightId=light.lightId; gpu.type=light.type; gpu.style=light.style; gpu.faceCount=local.faceCount;
	gpu.realtimeWeight=local.realtimeWeight;
	gpu.visibilityFlags=DX12_SHADOW_VISIBILITY_BAKED_AVAILABLE;
	gpu.bakedLightIndex=local.bakedLightIndex;
	memcpy(gpu.origin,light.origin,sizeof(gpu.origin)); memcpy(gpu.travelDirection,light.direction,sizeof(gpu.travelDirection));
	float style=data.packet.styles[light.style];
	for ( int c=0;c<3;++c ) gpu.radiance[c]=light.radiance[c]*style;
	gpu.attenuationRadius=light.attenuationRadius; gpu.innerConeCos=light.innerConeCos; gpu.outerConeCos=light.outerConeCos;
	gpu.constantAttn=light.constantAttn; gpu.linearAttn=light.linearAttn; gpu.quadraticAttn=light.quadraticAttn; gpu.exponent=light.exponent;
	gpu.fadeStart=light.startFade; gpu.fadeEnd=light.endFade; gpu.capDist=light.capDist; gpu.shadowSourceRadius=light.shadowSourceRadius;
	gpu.shadowFar=local.influence.zFar;
	if ( !ShadowMap_IsFiniteFloat(gpu.shadowFar) || gpu.shadowFar<=local.influence.zNear )
		return Fail(SHADOWMAP_ERR_INVALID_METADATA);
	// Camera coverage only: CSR, receiver relevance and server transmission keep
	// the shared .1-unit influence near. Points retain their original cube near.
	// Tiny finite-radius spots retain .1; otherwise cap at half far so n < f.
	gpu.shadowNear=local.world.type==emit_spotlight ? MIN(data.spotShadowNear,MAX(local.influence.zNear,gpu.shadowFar*0.5f)) : local.influence.zNear;
	gpu.tanRenderedHalfFov=local.influence.tanRenderedHalfFov;
	if ( !ShadowMap_IsFiniteFloat(gpu.shadowNear) || gpu.shadowNear<=0 || gpu.shadowNear>=gpu.shadowFar ||
		!ShadowMap_IsFiniteFloat(gpu.tanRenderedHalfFov) || gpu.tanRenderedHalfFov<=0 )
		return Fail(SHADOWMAP_ERR_INVALID_METADATA);
	gpu.planeToTexel=512/(2*gpu.tanRenderedHalfFov);
	// Keep valid identity/radiometry/projection ranges, but zero faces/matrices
	// and no target references when runtime depth sampling is bypassed.
	if ( !data.realtimeLocals[relevantIndex] ) return true;
	AddReportVolume(data,(int)light.lightId,CasterVolume(local.influence));
	const bool projectionChanged=local.projectionNear!=gpu.shadowNear || local.projectionFar!=gpu.shadowFar;
	for ( int f=0;f<local.faceCount;++f )
	{
		ShadowCache_t &cache=local.cache[f];
		int pageIndex=local.page[f], slot=local.slot[f], x=(slot%8)*512, y=(slot/8)*512;
		LocalPage_t &page=g_Pages[pageIndex];
		if ( projectionChanged || !cache.valid )
		{
			VPROF_BUDGET( "ShadowMapsDX12::BuildLocal.NewProjection", "Shadowmaps" );
			VMatrix view,projection,clip; float tanHalfFov;
			ShadowMapScene_LocalFaceMatrices(local.world,f,gpu.shadowNear,gpu.shadowFar,512,476,view,projection,clip,tanHalfFov);
			float matrix[16]; CopyMatrix(clip,matrix);
			ShadowCasterVolume_t volume; if ( !ExtractVolume(clip,volume) ) return false;
			bool billboards=DetailObjectSystem()->HasShadowCasters(volume,true);
			CollectLocalFaceCasters(data,volume);
			bool dynamic=data.localFaceCasters.Count()!=0;
			uint32 detailGeneration=billboards ? data.detail.m_nGeneration : 0;
			Vector forward,up,right;
			if ( local.faceCount==6 ) ShadowMapScene_CubeFaceBasis(f,forward,up,right);
			else { forward=local.world.normal; VectorNormalize(forward); ShadowMapScene_SunBasis(forward,right,up); }
			CViewSetup setup; SetupLightView(setup,local.world.origin,forward,up,projection,gpu.shadowNear,gpu.shadowFar,512,false,0,tanHalfFov);
			StartChart(cache,matrix,NULL,setup,volume,true,dynamic,billboards,detailGeneration,page.generation);
		}
		else
		{
			VPROF_BUDGET( "ShadowMapsDX12::BuildLocal.ReuseChecks", "Shadowmaps" );
			const ShadowCasterVolume_t &volume=local.faceVolumes[f];
			bool billboards=DetailObjectSystem()->HasShadowCasters(volume,true);
			CollectLocalFaceCasters(data,volume);
			bool dynamic=data.localFaceCasters.Count()!=0;
			uint32 detailGeneration=billboards ? data.detail.m_nGeneration : 0;
			bool rebuild=cache.staticGeneration!=g_StaticGeneration || cache.slotGeneration!=page.generation;
			if ( ReuseLocalChart(data,cache,page,gpu.shadowNear,gpu.shadowFar,billboards,detailGeneration) )
			{
				if ( cache.committedFrame==gpGlobals->framecount ) ++data.stats.reusedCharts;
				else ++data.stats.crossFrameReusedCharts;
			}
			else
				StartChart(cache,cache.matrix,NULL,cache.view,volume,rebuild,dynamic,billboards,detailGeneration,page.generation);
		}
		// Protect any parent receiver before the first write, not after rendering.
		if ( cache.pending )
		{
			if ( !WritablePage(data,pageIndex) ) return Fail(SHADOWMAP_ERR_RESIDENCY);
			CaptureChartContent(data,cache,page.work);
		}
		if ( !RenderChart(data,cache,page.clean,page.work,x,y,512,page.rigid,gpu.lightId,f) ) return false;
	}
	bool pending=false;
	for ( int f=0;f<local.faceCount;++f ) if ( local.cache[f].pending )
	{
		pending=true;
		Assert(local.cache[f].pendingReady);
	}
	if ( pending )
	{
		// Publish an entire cube transaction together: near/far and every face
		// matrix always describe exactly the working pixels that are sampled.
		for ( int f=0;f<local.faceCount;++f ) if ( local.cache[f].pending )
		{
			local.faceVolumes[f]=local.cache[f].pendingVolume;
			CommitChart(data,local.cache[f],gpu.lightId,f);
		}
		local.projectionNear=gpu.shadowNear; local.projectionFar=gpu.shadowFar;
	}
	bool valid=local.projectionNear>0;
	for ( int f=0;f<local.faceCount;++f )
	{
		if ( !local.cache[f].valid || local.cache[f].slotGeneration!=g_Pages[local.page[f]].generation ) valid=false;
		if ( local.cache[f].pending ) ++data.stats.deferredCharts;
	}
	if ( !valid )
	{
		data.stats.initializingCharts+=local.faceCount;
		return Fail("Shadowmaps: visible local chart not initialized after required update");
	}
	// Build promotions synchronously at zero weight; expose no chart or target
	// references until a later frame's ramp samples the complete transaction.
	if ( gpu.realtimeWeight==0 ) return true;
	gpu.shadowNear=local.projectionNear; gpu.shadowFar=local.projectionFar;
	for ( int f=0;f<local.faceCount;++f )
	{
		memcpy(gpu.worldToClip[f],local.cache[f].matrix,sizeof(local.cache[f].matrix));
		int slot=local.slot[f];
		gpu.faces[f][0]=local.page[f]; gpu.faces[f][1]=(slot%8)*512; gpu.faces[f][2]=(slot/8)*512; gpu.faces[f][3]=512;
	}
	return true;
}

void ReceiverCorners( const CViewSetup &setup, const VMatrix &clip, const Vector &forward, float nearDepth, float farDepth, Vector corners[8] )
{
	VMatrix inverse; MatrixInverseGeneral(clip,inverse);
	for ( int i=0;i<4;++i )
	{
		Vector a=Unproject(inverse,i&1?1:-1,i&2?1:-1,0), b=Unproject(inverse,i&1?1:-1,i&2?1:-1,1);
		float da=DotProduct(a-setup.origin,forward), db=DotProduct(b-setup.origin,forward);
		Vector step=(b-a)/(db-da);
		corners[i]=a+step*(nearDepth-da); corners[i+4]=a+step*(farDepth-da);
	}
}
Vector SnapSunCenter( const Vector &center, float nominalRadius, const Vector &basisX, const Vector &basisY, int useful )
{
	float units=2*nominalRadius/useful, x=DotProduct(center,basisX), y=DotProduct(center,basisY);
	return center+basisX*(floorf(x/units+0.5f)*units-x)+basisY*(floorf(y/units+0.5f)*units-y);
}

bool SunBoxOverlapsSquare( const ShadowMapSceneCaster_t &caster, const Vector &center, float halfExtent, const Vector &basisX, const Vector &basisY )
{
	Vector offset=(caster.mins+caster.maxs)*0.5f-center, extent=(caster.maxs-caster.mins)*0.5f;
	float xRadius=fabsf(basisX.x)*extent.x+fabsf(basisX.y)*extent.y+fabsf(basisX.z)*extent.z;
	float yRadius=fabsf(basisY.x)*extent.x+fabsf(basisY.y)*extent.y+fabsf(basisY.z)*extent.z;
	return fabsf(DotProduct(offset,basisX))<=halfExtent+xRadius+0.01f && fabsf(DotProduct(offset,basisY))<=halfExtent+yRadius+0.01f;
}

void SunProjection( const Vector &boundsMins, const Vector &boundsMaxs, const Vector &snapped, float nominalRadius,
	const Vector &travel, const Vector &basisX, const Vector &basisY, int S, int U,
	VMatrix &clip, VMatrix &clipToWorld, CViewSetup &setup, float depth[4] )
{
	float units=2*nominalRadius/U;
	float minimum=FLT_MAX, maximum=-FLT_MAX;
	for ( int i=0;i<8;++i ) { float d=DotProduct(BoxCorner(boundsMins,boundsMaxs,i),travel); minimum=MIN(minimum,d); maximum=MAX(maximum,d); }
	Vector origin=snapped+travel*(minimum-1-DotProduct(snapped,travel));
	depth[0]=0.1f; depth[1]=MAX(1.1f,maximum-minimum+2); depth[2]=units; depth[3]=0;
	float rendered=ShadowMapScene_RenderedOrthoHalfExtent(nominalRadius,S,U);
	VMatrix view,projection;
	ShadowMapScene_BuildViewMatrix(origin,travel,basisY,view);
	ShadowMapScene_BuildOrtho(rendered,rendered,depth[0],depth[1],projection); MatrixMultiply(projection,view,clip);
	// Invert the known orthographic camera analytically. The general inverse's
	// absolute 1e-5 pivot cutoff rejects valid wide cutscene cascades.
	view.InverseTR( clipToWorld );
	for ( int row = 0; row < 3; ++row )
	{
		clipToWorld[row][3] += clipToWorld[row][2] * depth[0];
		clipToWorld[row][0] *= rendered;
		clipToWorld[row][1] *= rendered;
		clipToWorld[row][2] *= depth[1] - depth[0];
	}
	SetupLightView(setup,origin,travel,basisY,projection,depth[0],depth[1],S,true,rendered,1);
}
bool CreateSunTarget( DX12ShadowTarget_t &target, const char *name )
{
	if ( !target ) target=g_Lighting->CreateShadowDepthTarget(name,4096,4096);
	return target!=0;
}
bool BuildSun( CShadowViewData &data, const VMatrix &receiverClip, const Vector &forward )
{
	VPROF_BUDGET( "ShadowMapsDX12::BuildSun", "Shadowmaps" );
	data.drawCandidateFirst=-1; data.drawCandidateCount=0;
	if ( g_Sun<0 ) return true;
	const ShadowMapClientMapState &map=ShadowMapsDX12_MapState();
	const DX12LightingSelectedLight &sun=g_Selected[g_Sun];
	Vector travel(sun.direction[0],sun.direction[1],sun.direction[2]), basisX,basisY; VectorNormalize(travel); ShadowMapScene_SunBasis(travel,basisX,basisY);
	DX12LightingViewConstantsV1 &constants=data.packet.constants;
	constants.cShadowView1[3]|=DX12_SHADOW_VIEW_HAS_SUN;
	float style=data.packet.styles[sun.style];
	for ( int i=0;i<3;++i ) constants.cSunRadiance[i]=sun.radiance[i]*style;
	constants.cSunRadiance[3]=tanf(DEG2RAD(sun.shadowSunAngularRadius));
	CopyVector3(travel,constants.cSunTravel); CopyVector3(basisX,constants.cSunBasisX); CopyVector3(basisY,constants.cSunBasisY);
	constants.cSunIdentity[0]=sun.lightId; constants.cSunIdentity[1]=sun.style;
	if ( !data.shadowsEnabled ) return true;
	Vector staticMins=map.worldMins,staticMaxs=map.worldMaxs;
	for ( int i=0;i<data.casters.Count();++i )
	{
		const ShadowMapSceneCaster_t &caster=data.casters[i];
		if ( caster.immutable ) AddBounds(staticMins,staticMaxs,caster.mins,caster.maxs);
	}
	if ( data.hasDetails ) AddBounds(staticMins,staticMaxs,data.detailMins,data.detailMaxs);
	Vector center=(staticMins+staticMaxs)*0.5f;
	float radius=1;
	for ( int i=0;i<8;++i )
	{
		Vector v=BoxCorner(staticMins,staticMaxs,i)-center;
		radius=MAX(radius,MAX(fabsf(DotProduct(v,basisX)),fabsf(DotProduct(v,basisY))));
	}
	// Include half-texel snapping slack in the nominal square itself.
	radius+=2*radius/DX12_SHADOW_STATIC_SUN_USEFUL;
	VMatrix staticClip, staticClipToWorld; CViewSetup staticView; float staticDepth[4],staticMatrix[16];
	SunProjection(staticMins,staticMaxs,SnapSunCenter(center,radius,basisX,basisY,4060),radius,travel,basisX,basisY,4096,4060,staticClip,staticClipToWorld,staticView,staticDepth);
	CopyMatrix(staticClip,staticMatrix);
	ShadowCasterVolume_t staticVolume; if ( !ExtractVolume(staticClip,staticVolume,&staticClipToWorld) ) return false;
	AddReportVolume(data,-5,staticVolume);
	ShadowCache_t &staticCache=data.staticSunCache;
	bool staticBillboards=DetailObjectSystem()->HasShadowCasters(staticVolume,true);
	uint32 staticDetailGeneration=staticBillboards ? data.detail.m_nGeneration : 0;
	bool staticRebuild=!staticCache.valid || staticCache.staticGeneration!=g_StaticGeneration || memcmp(staticCache.matrix,staticMatrix,sizeof(staticMatrix));
	if ( !CreateSunTarget(data.staticSun,"shadow_static_sun") || !CreateSunTarget(data.staticSunClean,"shadow_static_sun_clean") )
		return Fail(SHADOWMAP_ERR_RESIDENCY);
	if ( staticRebuild || staticCache.detailGeneration!=staticDetailGeneration )
		StartChart(staticCache,staticMatrix,staticDepth,staticView,staticVolume,staticRebuild,false,staticBillboards,staticDetailGeneration,0);
	if ( !RenderChart(data,staticCache,data.staticSunClean,data.staticSun,0,0,4096) ) return false;
	if ( staticCache.pending && staticCache.pendingReady )
	{
		CommitChart(data,staticCache);
	}
	if ( staticCache.pending ) ++data.stats.deferredCharts;
	if ( staticCache.valid )
	{
		memcpy(constants.cStaticSunWorldToClip,staticCache.matrix,sizeof(staticCache.matrix));
		memcpy(constants.cShadowDepthRecords[4],staticCache.depth,sizeof(staticCache.depth));
		constants.cStaticSunRect[2]=4096;
		if ( !data.Lease(data.staticSun) ) return Fail(SHADOWMAP_ERR_RESIDENCY);
		data.packet.staticSunTarget=data.staticSun; constants.cShadowView1[3]|=DX12_SHADOW_VIEW_STATIC_SUN_VALID;
	}
	else ++data.stats.initializingCharts;
	float splits[5],blend[3],farDepth=MIN(data.receiver.zFar,r_csm_distance.GetFloat());
	constants.cSunTravel[3]=farDepth; constants.cSunBasisX[3]=0.9f*farDepth;
	if ( !ShadowMapScene_CascadeSplits(data.receiver.zNear,farDepth,splits,blend) )
	{
		for ( int i=0;i<4;++i )
		{
			ShadowCache_t &cache=data.cascades[i];
			cache.pending=false; cache.staticGeneration=0; cache.dirty=true;
		}
		return true;
	}
	for ( int i=0;i<4;++i ) constants.cCascadeSplits[i]=splits[i+1];
	for ( int i=0;i<3;++i ) constants.cCascadeBlend[i]=blend[i];
	if ( !CreateSunTarget(data.cascadeClean,"shadow_cascade_static") || !CreateSunTarget(data.cascadeWork,"shadow_cascade_work") ||
		!CreateSunTarget(data.cascadeRigid,"shadow_cascade_rigid") )
		return Fail(SHADOWMAP_ERR_RESIDENCY);
	bool anyCascade=false;
	for ( int i=0;i<4;++i )
	{
		VPROF_BUDGET( "ShadowMapsDX12::BuildSun.Cascade", "Shadowmaps" );
		float nearSlice=splits[i]-(i?blend[i-1]:0), farSlice=splits[i+1]+(i<3?blend[i]:0);
		Vector corners[8]; ReceiverCorners(data.receiver,receiverClip,forward,nearSlice,farSlice,corners);
		Vector sphereCenter(0,0,0); for ( int c=0;c<8;++c ) sphereCenter+=corners[c]; sphereCenter*=1.0f/8;
		float sphereRadius=1; for ( int c=0;c<8;++c ) sphereRadius=MAX(sphereRadius,(corners[c]-sphereCenter).Length());
		sphereRadius=ceilf(sphereRadius*16)/16; sphereRadius+=2*sphereRadius/2012;
		Vector snapped=SnapSunCenter(sphereCenter,sphereRadius,basisX,basisY,2012);
		float halfExtent=ShadowMapScene_RenderedOrthoHalfExtent(sphereRadius,2048,2012);
		Vector depthMins=staticMins,depthMaxs=staticMaxs;
		// Extend upstream through world and RELEVANT dynamic bounds. An actor
		// outside this XY square must not perturb the other cascades' caches.
		for ( int c=0;c<data.casters.Count();++c )
		{
			const ShadowMapSceneCaster_t &caster=data.casters[c];
			if ( !caster.immutable && SunBoxOverlapsSquare(caster,snapped,halfExtent,basisX,basisY) ) AddBounds(depthMins,depthMaxs,caster.mins,caster.maxs);
		}
		for ( int c=0;c<8;++c ) AddBounds(depthMins,depthMaxs,corners[c],corners[c]);
		VMatrix clip, clipToWorld; CViewSetup lightView; float depth[4],matrix[16];
		SunProjection(depthMins,depthMaxs,snapped,sphereRadius,travel,basisX,basisY,2048,2012,clip,clipToWorld,lightView,depth);
		CopyMatrix(clip,matrix);
		int x=(i&1)*2048,y=(i/2)*2048;
		ShadowCasterVolume_t volume; if ( !ExtractVolume(clip,volume,&clipToWorld) ) return false;
		AddReportVolume(data,-1-i,volume);
		ShadowCache_t &cache=data.cascades[i];
		CollectLocalFaceCasters(data,volume);
		bool billboards=DetailObjectSystem()->HasShadowCasters(volume,true), dynamic=data.localFaceCasters.Count()!=0;
		uint32 detailGeneration=billboards ? data.detail.m_nGeneration : 0;
		bool rebuild=!cache.valid || cache.staticGeneration!=g_StaticGeneration ||
			memcmp(cache.matrix,matrix,sizeof(matrix)) || memcmp(cache.depth,depth,sizeof(depth));
		if ( !rebuild && ReuseCommittedChart(data,cache,data.cascadeWork,0,billboards,detailGeneration) )
		{
			if ( cache.committedFrame!=gpGlobals->framecount ) ++data.stats.crossFrameReusedSunCharts;
		}
		else
		{
			StartChart(cache,matrix,depth,lightView,volume,rebuild,dynamic,billboards,detailGeneration,0);
			CaptureChartContent(data,cache,data.cascadeWork);
		}
		if ( !RenderChart(data,cache,data.cascadeClean,data.cascadeWork,x,y,2048,data.cascadeRigid,-1-i) ) return false;
		if ( cache.pending && cache.pendingReady )
		{
			data.cascadeVolumes[i]=cache.pendingVolume;
			CommitChart(data,cache,-1-i);
		}
		if ( cache.pending ) ++data.stats.deferredCharts;
		if ( cache.valid )
		{
			memcpy(constants.cSunWorldToClip[i],cache.matrix,sizeof(cache.matrix));
			memcpy(constants.cShadowDepthRecords[i],cache.depth,sizeof(cache.depth));
			constants.cCascadeRects[i][0]=x; constants.cCascadeRects[i][1]=y; constants.cCascadeRects[i][2]=2048;
			anyCascade=true;
		}
		else ++data.stats.initializingCharts;
	}
	if ( anyCascade )
	{
		if ( !data.Lease(data.cascadeWork) ) return Fail(SHADOWMAP_ERR_RESIDENCY);
		data.packet.cascadeAtlasTarget=data.cascadeWork; constants.cShadowView1[3]|=DX12_SHADOW_VIEW_CSM_VALID;
	}
	return true;
}

ShadowTileRect_t ProjectInfluence( const LocalLight_t &local, const CShadowViewData &data, const VMatrix &clip, const Vector &forward, int tilesX, int tilesY )
{
	ShadowTileRect_t rect={0,0,tilesX,tilesY};
	if ( local.world.radius<=0 ) return rect;
	Vector corners[8];
	if ( local.influence.cube ) for ( int i=0;i<8;++i ) corners[i]=BoxCorner(local.influence.mins,local.influence.maxs,i);
	else
	{
		Vector f=local.world.normal,r,u; VectorNormalize(f); ShadowMapScene_SunBasis(f,r,u);
		for ( int i=0;i<8;++i )
		{
			float d=i&4?local.influence.zFar:local.influence.zNear;
			corners[i]=local.world.origin+(f+(r*(i&1?1:-1)+u*(i&2?1:-1))*local.influence.tanRenderedHalfFov)*d;
		}
	}
	float minX=FLT_MAX,minY=FLT_MAX,maxX=-FLT_MAX,maxY=-FLT_MAX;
	for ( int i=0;i<8;++i )
	{
		// Crossing the receiver near plane is deliberately FULL viewport coverage.
		if ( DotProduct(corners[i]-data.receiver.origin,forward)<=data.receiver.zNear ) return rect;
		float w=clip[3][0]*corners[i].x+clip[3][1]*corners[i].y+clip[3][2]*corners[i].z+clip[3][3];
		if ( w<=0 ) return rect;
		float x=(clip[0][0]*corners[i].x+clip[0][1]*corners[i].y+clip[0][2]*corners[i].z+clip[0][3])/w;
		float y=(clip[1][0]*corners[i].x+clip[1][1]*corners[i].y+clip[1][2]*corners[i].z+clip[1][3])/w;
		x=(0.5f*x+0.5f)*data.receiver.width; y=(0.5f-0.5f*y)*data.receiver.height;
		minX=MIN(minX,x); maxX=MAX(maxX,x); minY=MIN(minY,y); maxY=MAX(maxY,y);
	}
	// Round outward by a pixel for raster/jitter boundaries, and clamp FLOAT
	// coordinates before conversion: a very large projected box must cover the
	// viewport, not overflow an integer and silently produce an empty CSR rect.
	minX=clamp(minX-1,0.0f,(float)data.receiver.width); minY=clamp(minY-1,0.0f,(float)data.receiver.height);
	maxX=clamp(maxX+1,0.0f,(float)data.receiver.width); maxY=clamp(maxY+1,0.0f,(float)data.receiver.height);
	rect.x0=(int)floorf(minX/16); rect.y0=(int)floorf(minY/16);
	rect.x1=MIN((int)floorf(maxX/16)+1,tilesX); rect.y1=MIN((int)floorf(maxY/16)+1,tilesY);
	return rect;
}
bool BuildTiles( CShadowViewData &data, const VMatrix &clip, const Vector &forward )
{
	VPROF_BUDGET( "ShadowMapsDX12::BuildTiles", "Shadowmaps" );
	int x=(data.receiver.width+15)/16,y=(data.receiver.height+15)/16;
	uint64 tileCount=(uint64)x*y;
	if ( !x || !y || tileCount>INT_MAX/2 ) return Fail(SHADOWMAP_ERR_RESIDENCY);
	data.ranges.SetCount((int)tileCount*2);
	data.rects.SetCount(data.relevant.Count());
	data.tileRunIndices.SetCount(data.relevant.Count());
	{
		VPROF_BUDGET( "ShadowMapsDX12::BuildTiles.CountEntries", "Shadowmaps" );
		for ( int l=0;l<data.relevant.Count();++l )
		{
			ShadowTileRect_t rect=ProjectInfluence(g_Locals[data.relevant[l]],data,clip,forward,x,y); data.rects[l]=rect;
		}
	}
	data.indices.RemoveAll();
	{
		VPROF_BUDGET( "ShadowMapsDX12::BuildTiles.FillIndices", "Shadowmaps" );
		// A tile range is an offset/count, not an exclusive allocation. Share
		// the exact ascending list across each rectangle-edge cell instead of
		// copying it once per pixel tile (most locals cover the whole view).
		for ( int row=0;row<y; )
		{
			int nextRow=y;
			for ( int l=0;l<data.rects.Count();++l )
			{
				const ShadowTileRect_t &rect=data.rects[l];
				if ( rect.y0>row ) nextRow=MIN(nextRow,rect.y0);
				if ( rect.y1>row ) nextRow=MIN(nextRow,rect.y1);
			}
			for ( int column=0;column<x; )
			{
				int next=x, count=0;
				for ( int l=0;l<data.rects.Count();++l )
				{
					const ShadowTileRect_t &rect=data.rects[l];
					if ( row<rect.y0 || row>=rect.y1 ) continue;
					if ( rect.x0>column ) next=MIN(next,rect.x0);
					if ( rect.x1>column ) next=MIN(next,rect.x1);
					if ( column>=rect.x0 && column<rect.x1 ) data.tileRunIndices[count++]=l;
				}
				const uint32 first=data.indices.Count();
				if ( count>INT_MAX-data.indices.Count() ) return Fail(SHADOWMAP_ERR_RESIDENCY);
				if ( count )
				{
					int at=data.indices.AddMultipleToTail(count);
					memcpy(data.indices.Base()+at,data.tileRunIndices.Base(),count*sizeof(uint32));
				}
				for ( int bandRow=row;bandRow<nextRow;++bandRow )
					for ( int tile=column;tile<next;++tile )
					{
						data.ranges[2*(bandRow*x+tile)]=first;
						data.ranges[2*(bandRow*x+tile)+1]=count;
					}
				column=next;
			}
			row=nextRow;
		}
	}
	data.packet.constants.cShadowView1[0]=x; data.packet.constants.cShadowView1[1]=y;
	data.packet.tileCount=(uint32)tileCount; data.packet.tileRanges=data.ranges.Base();
	data.packet.tileIndices=data.indices.Base(); data.packet.tileIndexCount=data.indices.Count();
	data.stats.csrEntries=data.indices.Count(); return true;
}

#ifdef _DEBUG
Vector ProjectionUvDepth( const VMatrix &clip, const Vector &point )
{
	float w=clip[3][0]*point.x+clip[3][1]*point.y+clip[3][2]*point.z+clip[3][3];
	float x=clip[0][0]*point.x+clip[0][1]*point.y+clip[0][2]*point.z+clip[0][3];
	float y=clip[1][0]*point.x+clip[1][1]*point.y+clip[1][2]*point.z+clip[1][3];
	float z=clip[2][0]*point.x+clip[2][1]*point.y+clip[2][2]*point.z+clip[2][3];
	return Vector(0.5f+0.5f*x/w,0.5f-0.5f*y/w,z/w);
}

void HybridSelectionSelfCheck()
{
	const Vector savedMins=g_State.worldMins, savedMaxs=g_State.worldMaxs;
	g_State.worldMins.Init(0,0,0); g_State.worldMaxs.Init(1,1,1);
	ShadowCasterVolume_t volume={}; volume.m_nPlaneCount=1;
	volume.m_Planes[0].m_Normal=Vector(1,1,0); volume.m_Planes[0].m_Dist=1;
	ShadowRankingDomain_t domain; domain.Build(volume);
	Assert(domain.vertexCount>0);
	float farthest;
	Assert((domain.Nearest(Vector(-1,-1,0.5f),farthest)-Vector(0.5f,0.5f,0.5f)).Length()<0.0001f);
	Assert((domain.Nearest(Vector(-1,0,2),farthest)-Vector(0,1,1)).Length()<0.0001f);
	Assert(domain.Nearest(Vector(0.75f,0.75f,0.5f),farthest)==Vector(0.75f,0.75f,0.5f));
	Assert(fabsf(BestReceiverConeDot(domain,Vector(0.75f,0.75f,0.5f),Vector(1,0,0))-1)<0.0001f);
	Assert(fabsf(BestReceiverConeDot(domain,Vector(0.5f,0.5f,0.5f),Vector(-0.70710678f,-0.70710678f,0)))<0.0001f);
	CViewSetup beforeCut,afterCut; beforeCut.origin=afterCut.origin=vec3_origin;
	beforeCut.angles=afterCut.angles=vec3_angle; beforeCut.fov=afterCut.fov=90;
	beforeCut.m_bOrtho=afterCut.m_bOrtho=false;
	Assert(!HybridCameraCut(beforeCut,afterCut));
	afterCut.origin.x=255; Assert(!HybridCameraCut(beforeCut,afterCut));
	afterCut.origin.x=256; Assert(HybridCameraCut(beforeCut,afterCut));
	afterCut.origin=vec3_origin; afterCut.angles.y=61; Assert(HybridCameraCut(beforeCut,afterCut));
	afterCut.angles=vec3_angle; afterCut.fov=105; Assert(HybridCameraCut(beforeCut,afterCut));
	// Retire a face from a shared page without checking whole-page idleness or
	// mutating any target: another resident and leased old pixels must survive.
	LocalPage_t page; LocalLight_t retiring;
	page.work=42; page.clean=43; page.rigid=44; page.owners[3]=2; page.owners[4]=1;
	retiring.page[0]=0; retiring.slot[0]=3; retiring.cache[0].valid=retiring.cache[0].rigidValid=true;
	DetachLocalFace(page,retiring,0,2);
	Assert(page.owners[3]==-1 && page.owners[4]==1 && page.ownershipChanged);
	Assert(page.work==42 && page.clean==43 && page.rigid==44);
	Assert(retiring.page[0]==-1 && retiring.slot[0]==-1 && !retiring.cache[0].valid && !retiring.cache[0].rigidValid);
	g_State.worldMins=savedMins; g_State.worldMaxs=savedMaxs;
	DX12LightingSelectedLight light={}; light.constantAttn=10; light.linearAttn=-4; light.quadraticAttn=1;
	Assert(fabsf(LocalMinimumDenominator(light,1,4)-6)<0.0001f);
	light.capDist=1.5f;
	Assert(fabsf(LocalMinimumDenominator(light,0,4)-6.25f)<0.0001f);
	light.capDist=0; light.attenuationRadius=1.25f;
	Assert(fabsf(LocalMinimumDenominator(light,1,4)-6.5625f)<0.0001f);
	Assert(HybridNonnegativeSetting("0",4)==0);
	Assert(HybridNonnegativeSetting("-1",4)==4);
	Assert(HybridNonnegativeSetting("nan",4)==4);
	Assert(HybridNonnegativeSetting("junk",4)==4);
	Assert(HybridNonnegativeSetting("4junk",4)==4);
	ShadowLightRank_t a={}, b={}; a.score=b.score=1; a.lightId=3; b.lightId=4;
	Assert(LightRankCompare(&a,&b)<0 && LightRankCompare(&b,&a)>0);
	Msg("Shadowmaps: hybrid clipped-domain/cone/attenuation/settings/tie/camera-cut/retirement assertions completed\n");
}

void ProjectionSelfCheck()
{
	HybridSelectionSelfCheck();
	dworldlight_t light; memset(&light,0,sizeof(light)); light.type=emit_point;
	const float core=18.0f/512.0f;
	for ( int face=0;face<6;++face )
	{
		Vector f,u,r; ShadowMapScene_CubeFaceBasis(face,f,u,r);
		VMatrix view,projection,clip; float t;
		ShadowMapScene_LocalFaceMatrices(light,face,0.1f,100,512,476,view,projection,clip,t);
		Vector center=ProjectionUvDepth(clip,f*10);
		Assert(fabsf(center.x-0.5f)<0.00001f && fabsf(center.y-0.5f)<0.00001f);
		Assert(fabsf(ProjectionUvDepth(clip,f*0.1f).z)<0.00001f);
		Assert(fabsf(ProjectionUvDepth(clip,f*100).z-1)<0.00001f);
		Assert(ShadowMapScene_SelectCubeFace(f)==face);
		for ( int axis=0;axis<2;++axis ) for ( int sign=-1;sign<=1;sign+=2 )
		{
			Vector basis=axis?u:r;
			Vector corePoint=ProjectionUvDepth(clip,(f+basis*sign)*10);
			float edge=axis?corePoint.y:corePoint.x;
			float expected=(axis?-sign:sign)<0?core:1-core;
			AssertMsg(fabsf(edge-expected)<0.00001f,"cube logical core fits useful square");
			Vector guardPoint=ProjectionUvDepth(clip,(f+basis*(sign*t))*10);
			edge=axis?guardPoint.y:guardPoint.x;
			expected=(axis?-sign:sign)<0?0:1;
			AssertMsg(fabsf(edge-expected)<0.00001f,"cube rendered guard edge");
			Vector seam=f+basis*(sign*1.001f);
			int neighbor=ShadowMapScene_SelectCubeFace(seam);
			Assert(neighbor!=face);
			VMatrix neighborView,neighborProjection,neighborClip; float neighborTan;
			ShadowMapScene_LocalFaceMatrices(light,neighbor,0.1f,100,512,476,neighborView,neighborProjection,neighborClip,neighborTan);
			Vector uv=ProjectionUvDepth(neighborClip,seam*10),nf,nu,nr;
			ShadowMapScene_CubeFaceBasis(neighbor,nf,nu,nr);
			Vector recovered=nf+nr*((uv.x-0.5f)*2*neighborTan)+nu*((0.5f-uv.y)*2*neighborTan);
			VectorNormalize(recovered); VectorNormalize(seam);
			AssertMsg(DotProduct(recovered,seam)>0.99999f,"neighbor chart seam ray round trip");
			Assert(uv.x>=core-0.001f && uv.x<=1-core+0.001f && uv.y>=core-0.001f && uv.y<=1-core+0.001f);
		}
	}
	Assert(ShadowMapScene_SelectCubeFace(Vector(1,1,1))==0);
	Assert(ShadowMapScene_SelectCubeFace(Vector(0,1,1))==2);
	Vector travel(0.70710678f,0,-0.70710678f),x,y;
	ShadowMapScene_SunBasis(travel,x,y);
	for ( int map=0;map<2;++map )
	{
		int S=map?4096:2048,U=map?4060:2012;
		float rendered=ShadowMapScene_RenderedOrthoHalfExtent(64,S,U);
		VMatrix view,projection,clip;
		ShadowMapScene_BuildViewMatrix(vec3_origin,travel,y,view);
		ShadowMapScene_BuildOrtho(rendered,rendered,0.1f,100,projection); MatrixMultiply(projection,view,clip);
		Vector center=ProjectionUvDepth(clip,travel*10);
		Assert(fabsf(center.x-0.5f)<0.00001f && fabsf(center.y-0.5f)<0.00001f);
		Assert(fabsf(ProjectionUvDepth(clip,travel*0.1f).z)<0.00001f && fabsf(ProjectionUvDepth(clip,travel*100).z-1)<0.00001f);
		for ( int axis=0;axis<2;++axis ) for ( int sign=-1;sign<=1;sign+=2 )
		{
			Vector basis=axis?y:x;
			Vector corePoint=ProjectionUvDepth(clip,travel*10+basis*(sign*64));
			Vector guardPoint=ProjectionUvDepth(clip,travel*10+basis*(sign*rendered));
			float expected=(axis?-sign:sign)<0?18.0f/S:1-18.0f/S;
			Assert(fabsf((axis?corePoint.y:corePoint.x)-expected)<0.00001f);
			expected=(axis?-sign:sign)<0?0:1;
			Assert(fabsf((axis?guardPoint.y:guardPoint.x)-expected)<0.00001f);
		}
	}
	float splits[5],blend[3];
	Assert(ShadowMapScene_CascadeSplits(1,4096,splits,blend));
	for ( int i=1;i<4;++i ) Assert(fabsf(blend[i-1]-0.05f*MIN(splits[i]-splits[i-1],splits[i+1]-splits[i]))<0.00001f);
	Assert(!ShadowMapScene_CascadeSplits(1,1,splits,blend));
	Msg("Shadowmaps: numerical projection assertions completed\n");
}
#endif


}

void ShadowMapsDX12_Init()
{
	ResolveLightingInterface(); ResetMapState();
}
void ShadowMapsDX12_Shutdown()
{
	if ( g_DeviceCallbacksRegistered )
	{
		materials->RemoveRestoreFunc(ShadowMapRestoreFunc);
		materials->RemoveReleaseFunc(ShadowMapReleaseFunc);
		g_DeviceCallbacksRegistered=false;
	}
	ShadowMapsDX12_LevelShutdown();
	for ( int i=0;i<g_ViewPool.Count();++i ) g_ViewPool[i]->Release();
	g_ViewPool.Purge(); g_ViewStack.Purge();
	g_Lighting=NULL; g_Highres=NULL;
}
bool ShadowMapsDX12_LevelInitPreEntity( const char *mapName, char *error, int bytes )
{
	if ( g_ClientLevelShutdown ) return Fail("Highres lightmaps: admission attempted during client level shutdown",error,bytes);
	if ( g_MapGeneration || g_State.featureMap ) ShadowMapsDX12_LevelShutdown();
	ResetMapState(); g_Error[0]='\0'; g_Admitted=false;
	if ( !g_ResourceResetInProgress ) g_OrdinaryAutoexecFrames=0;
	if ( error && bytes>0 ) error[0]='\0';
	V_strncpy(g_MapName,mapName && *mapName ? mapName : engine->GetLevelName(),sizeof(g_MapName));
	if ( !ReadMapMetadata(g_MapName,error,bytes) ) return false;
	if ( !g_State.featureMap )
	{
		if ( g_Lighting ) g_Lighting->SetReceiverFeatureGeneration(0);
		if ( g_MaterialFeature ) materials->ReloadMaterials();
		g_MaterialFeature=false;
		return true;
	}
	if ( !g_Lighting || !g_Highres ) ResolveLightingInterface();
	if ( !g_Lighting || !g_Highres ) return Fail(SHADOWMAP_ERR_REQUIRES_DX12,error,bytes);
	DX12HighresMapStatus native={}; char nativeError[256]={0};
	g_Highres->GetStatus(native,nativeError,sizeof(nativeError));
	if ( native.state==DX12_HIGHRES_REJECTED ) return Fail(nativeError,error,bytes);
	if ( !native.nativeMapGeneration || !hlight::ValidPair(native.faceLump,native.lightingLump) )
	{
		// RequireMap can provide the exact compatibility/reload diagnosis, but
		// never invent a native pair or attach the replacement after map load.
		g_Highres->RequireMap(g_MapName,native,nativeError,sizeof(nativeError));
		return Fail(nativeError[0]?nativeError:"Highres lightmaps: native startup mode unavailable; clean map reload required",error,bytes);
	}
	g_State.selectedMode=native.lightingLump==LUMP_LIGHTING_HDR ? SHADOWMAP_MODE_HDR : SHADOWMAP_MODE_LDR;
	const hlight::ManifestModeView &mode=g_State.manifest.mode[g_State.selectedMode];
	const bool activeMode=mode.runtime;
	const uint64 nativeGeneration=native.nativeMapGeneration;
	const uint32 faceLump=native.faceLump, lightingLump=native.lightingLump;
	if ( activeMode && (mode.record->faceLump!=faceLump || mode.record->lightingLump!=lightingLump) )
		return Fail("Highres lightmaps: manifest/native face and lighting pair mismatch",error,bytes);
	if ( !g_Highres->RequireMap(g_MapName,native,nativeError,sizeof(nativeError)) )
		return Fail(nativeError,error,bytes);
	if ( native.nativeMapGeneration!=nativeGeneration || native.faceLump!=faceLump || native.lightingLump!=lightingLump )
		return Fail("Highres lightmaps: native admission generation or mode changed",error,bytes);
	if ( !activeMode )
	{
		// This independently captured native lighting mode has no replacement.
		if ( native.state!=DX12_HIGHRES_ORDINARY )
			return Fail("Highres lightmaps: client/native rendering mode mismatch",error,bytes);
		g_Lighting->SetReceiverFeatureGeneration(0);
		if ( g_MaterialFeature ) materials->ReloadMaterials();
		g_MaterialFeature=false; return true;
	}
	if ( (native.state!=DX12_HIGHRES_READY && native.state!=DX12_HIGHRES_PENDING) || !native.nativeMapGeneration )
		return Fail("Highres lightmaps: required native map generation unavailable",error,bytes);
	// PublishDomain registers the bridge callbacks before entering the original
	// native LevelInit, which in turn calls client admission. Append only now,
	// never at client Init (which precedes that first native registration).
	RegisterDeviceCallbacks();
	g_State.nativeMapGeneration=native.nativeMapGeneration;
	g_State.lights=mode.lights; g_State.lightCount=mode.lightCount; g_State.sunLightIndex=mode.sunLightIndex;
	static ConVarRef portalsOpenAll("r_portalsopenall",true);
	if ( !portalsOpenAll.IsValid() ) return Fail(SHADOWMAP_ERR_CASTER_TRAVERSAL,error,bytes);
	g_Selected.RemoveAll(); g_Locals.RemoveAll(); g_Sun=g_State.sunLightIndex;
	for ( uint32 i=0;i<g_State.lightCount;++i )
	{
		const ShadowMapLightDisk &record=g_State.lights[i];
		DX12LightingSelectedLight selected;
		if ( !ShadowMapScene_SelectedLightFromWorldLight(record,i,selected) ) return Fail(SHADOWMAP_ERR_INVALID_METADATA,error,bytes);
		g_Selected.AddToTail(selected);
		if ( selected.type!=DX12_SHADOW_LIGHT_SUN )
		{
			LocalLight_t &local=g_Locals[g_Locals.AddToTail()];
			local.world=record.light; local.selected=selected; local.bakedLightIndex=i;
			ShadowMapScene_LocalInfluence(local.world,g_State.worldMins,g_State.worldMaxs,local.influence);
			local.faceCount=local.influence.cube?6:1;
		}
	}
	if ( !++g_MapCounter ) ++g_MapCounter;
	g_MapGeneration=g_MapCounter; g_State.mapGeneration=g_MapGeneration;
	// PrepareMap copies the selected-light span synchronously. Route 1 uses only
	// explicit native metadata; the zero-initialized inverse spans stay empty.
	DX12LightingMapDesc map={};
	map.mapGeneration=g_MapGeneration; map.mode=g_State.selectedMode; map.shaderAbi=DX12_LIGHTING_SHADER_ABI;
	map.highresRoute=1; map.nativeMapGeneration=g_State.nativeMapGeneration;
	map.selectedLightCount=g_Selected.Count(); map.selectedLights=g_Selected.Base(); map.sunLightIndex=g_State.sunLightIndex;
	CopyVector3(g_State.worldMins,map.worldMins); CopyVector3(g_State.worldMaxs,map.worldMaxs);
	char reason[256]={0};
	if ( !g_Lighting->ValidateMap(map,reason,sizeof(reason)) ) return Fail(reason,error,bytes);
	g_Lighting->SetReceiverFeatureGeneration(g_MapGeneration);
	if ( g_ResourceResetInProgress )
	{
		// The feature/layout is unchanged. Refresh snapshot contexts, not VMT
		// variables or native geometry: ReloadMaterials would release/restore again.
		for ( MaterialHandle_t h=materials->FirstMaterial(); h!=materials->InvalidMaterial(); h=materials->NextMaterial(h) )
		{
			IMaterial *material=materials->GetMaterial(h);
			if ( material && material->IsPrecached() ) material->RefreshPreservingMaterialVars();
		}
	}
	else if ( !g_MaterialFeature ) materials->ReloadMaterials();
	g_MaterialFeature=true;
	g_Lighting->PrepareMap(map);
	if ( g_Lighting->GetStatus(g_MapGeneration,0,reason,sizeof(reason))!=DX12_LIGHTING_STATUS_READY ) return Fail(reason[0]?reason:SHADOWMAP_ERR_RESIDENCY,error,bytes);
	// Sign-on DT_World can arrive after PreEntity. Check its capability before
	// the FIRST receiver draw, not prematurely while the map is still loading.
	g_State.runtimeActive=true; g_Admitted=true;
	return true;
}

const ShadowMapClientMapState &ShadowMapsDX12_MapState() { return g_State; }
void ShadowMapsDX12_BeginClientLevelShutdown()
{
	if ( g_ClientLevelShutdown ) return;
	g_ClientLevelShutdown=true; g_Admitted=false; g_TransmitReady=false;
	CloseReceiverScopes();
	// The backend disables native capture and synchronously drains material
	// jobs with all existing client references/depth resources still intact.
	if ( g_Highres ) g_Highres->BeginClientLevelShutdown();
	PollCompletedViews();
	ViewRender_ClearShadowMapScenes();
	for ( int i=0;i<g_ViewPool.Count();++i )
	{
		CShadowViewData *data=g_ViewPool[i];
		data->active=false;
		data->casters.RemoveAll(); data->casterRanges.RemoveAll(); data->casterIndices.RemoveAll();
		data->drawCandidateFirst=-1; data->drawCandidateCount=0;
	}
	// Release fixture-owned renderables while their leaf/entity owners exist.
	DestroySpawnedRenderables();
}
void ShadowMapsDX12_EndClientLevelShutdown()
{
	if ( !g_ClientLevelShutdown ) return;
	if ( g_Highres ) g_Highres->EndClientLevelShutdown();
	g_ClientLevelShutdown=false;
}
void ShadowMapsDX12_LevelShutdown()
{
	g_Admitted=false; CloseReceiverScopes();
	g_CasterSnapshot.frame=-1;
	g_CasterSnapshot.casters.RemoveAll(); g_CasterSnapshot.keys.RemoveAll(); g_CasterSnapshot.content.RemoveAll();
	if ( !g_ResourceResetInProgress ) DestroySpawnedRenderables();
	if ( !g_ResourceResetInProgress ) g_StaticPropReceivers.Purge();
	g_TransmitReady=false;
	if ( g_Lighting ) g_Lighting->SetReceiverFeatureGeneration(0);
	for ( int p=0;p<g_Pages.Count();++p ) { DestroyTarget(g_Pages[p].work); DestroyTarget(g_Pages[p].clean); DestroyTarget(g_Pages[p].rigid); }
	for ( int i=0;i<g_PageSpares.Count();++i ) DestroyTarget(g_PageSpares[i].target);
	g_PageSpares.RemoveAll();
	g_Pages.RemoveAll(); g_Locals.RemoveAll(); g_Selected.RemoveAll(); g_Sun=-1;
	g_SelectionFrame=0xffffffffu; g_FramePromotions=g_FrameDemotions=0; g_DepartingLocal=-1; g_LightRanks.RemoveAll();
	g_HaveSelectionView=false;
	if ( g_LastMain ) { g_LastMain->Release(); g_LastMain=NULL; }
	memset(&g_LastStats,0,sizeof(g_LastStats));
	g_FrameRigidLayerRebuilds=0; memset(g_FrameViewStats,0,sizeof(g_FrameViewStats)); g_FrameRigidEvents.RemoveAll();
	for ( int i=0;i<g_ViewPool.Count();++i )
	{
		CShadowViewData *data=g_ViewPool[i];
		DestroyTarget(data->cascadeClean); DestroyTarget(data->cascadeWork); DestroyTarget(data->cascadeRigid);
		DestroyTarget(data->staticSunClean); DestroyTarget(data->staticSun);
		data->ReleaseLeases(); data->active=false; data->staticSunCache=ShadowCache_t();
		for ( int c=0;c<4;++c ) data->cascades[c]=ShadowCache_t();
	}
	for ( int i=0;i<g_PendingViews.Count();++i ) g_PendingViews[i]->Release();
	g_PendingViews.RemoveAll(); g_ViewStack.RemoveAll();
	ViewRender_ClearShadowMapScenes();
	// CPU queues drain at the early CHLClient boundary; backend leases/fences
	// separately retire destroyed GPU storage (also safe for resource reset).
	if ( g_Lighting && g_MapGeneration ) g_Lighting->UnloadMap(g_MapGeneration);
	g_MapGeneration=0; ShadowMapsDX12_InvalidateStatic(); g_Error[0]='\0'; ResetMapState();
	g_MapName[0]='\0'; g_DeviceResetPending=false; g_DeviceRestoreComplete=false;
}
void ShadowMapsDX12_OnDeviceReset()
{
	if ( !g_DeviceResetPending || !g_DeviceRestoreComplete ) return;
	if ( !ThreadInMainThread() ) Error("Shadowmaps: device readmission left the main thread\n");
	if ( g_ClientLevelShutdown || !g_State.runtimeActive || !g_MapName[0] || g_Error[0] ) return;
	if ( !g_Highres ) { Fail(SHADOWMAP_ERR_REQUIRES_DX12); return; }
	DX12HighresMapStatus native={}; char reason[256]={0};
	g_Highres->GetStatus(native,reason,sizeof(reason));
	// A resource-release callback retains the native domain/dynamic CPU data but
	// leaves its GPU atlas PENDING. Never read the BSP or admit targets then.
	if ( native.state==DX12_HIGHRES_PENDING ) return;
	const hlight::ManifestModeDisk &mode=*g_State.manifest.mode[g_State.selectedMode].record;
	if ( native.state!=DX12_HIGHRES_READY || native.nativeMapGeneration!=g_State.nativeMapGeneration ||
		native.faceLump!=mode.faceLump || native.lightingLump!=mode.lightingLump )
	{
		Fail(reason[0]?reason:"Highres lightmaps: device restore requires READY exact native generation/mode");
		return;
	}
	const bool transmit=g_TransmitReady;
	char mapName[MAX_PATH]; V_strncpy(mapName,g_MapName,sizeof(mapName));
	// FRAME_START precedes all view construction. Drain old draws before
	// changing their generation or reloading snapshots; retain the native domain.
	if ( !g_Highres->BeginClientResourceReadmission() )
	{
		Fail("Highres lightmaps: cannot quiesce resource readmission");
		return;
	}
	g_ResourceResetInProgress=true;
	ShadowMapsDX12_LevelShutdown();
	const bool admitted=ShadowMapsDX12_LevelInitPreEntity(mapName,reason,sizeof(reason));
	g_ResourceResetInProgress=false;
	g_TransmitReady=transmit;
	g_Highres->EndClientResourceReadmission();
	if ( !admitted ) engine->ClientCmd_Unrestricted("disconnect\n");
}
bool ShadowMapsDX12_Active() { return !g_DeviceResetPending && g_Admitted && !g_Error[0] && g_State.runtimeActive; }

void ShadowMapsDX12_RegisterStaticPropReceiver( uint32 ordinal, ICollideable *prop, const StaticPropLump_t &authored )
{
	IClientUnknown *unknown = prop ? prop->GetIClientUnknown() : NULL;
	IClientRenderable *renderable = unknown ? unknown->GetClientRenderable() : NULL;
	if ( !renderable || renderable->GetIClientUnknown() != unknown ) return;
	StaticPropReceiverPose record = {};
	record.ordinal = ordinal; record.authored = authored;
	g_StaticPropRegistrationComplete = false;
	const int found = g_StaticPropReceivers.Find(renderable);
	if ( found == g_StaticPropReceivers.InvalidIndex() ) g_StaticPropReceivers.Insert(renderable,record);
	else g_StaticPropReceivers[found] = record;
}

void ShadowMapsDX12_PrepareStaticPropReceiver( IClientRenderable *renderable )
{
	if ( !ShadowMapsDX12_Active() || !g_Lighting || !renderable ) return;
	const int propIndex = g_StaticPropReceivers.Find(renderable);
	const model_t *model = renderable->GetModel();
	if ( !model || modelinfo->GetModelType(model) != mod_studio ) return;
	MDLCACHE_CRITICAL_SECTION();
	const MDLHandle_t handle = modelinfo->GetCacheHandle(model);
	const studiohdr_t *hdr = handle != MDLHANDLE_INVALID ? mdlcache->GetStudioHdr(handle) : NULL;
	const studiohwdata_t *hw = handle != MDLHANDLE_INVALID ? mdlcache->GetHardwareData(handle) : NULL;
	if ( !hdr || !hw || !hw->m_pLODs || hw->m_NumLODs <= 0 ||
		hw->m_RootLOD < 0 || hw->m_RootLOD >= hw->m_NumLODs ) return;
	int cacheIndex = g_StaticPropModels.Find(model);
	StaticPropModelVisibility *cached = cacheIndex != g_StaticPropModels.InvalidIndex() ? g_StaticPropModels[cacheIndex] : NULL;
	if ( !cached || cached->header != hdr || cached->hardware != hw || cached->checksum != uint32(hdr->checksum) ||
		cached->hardwareLODs != hw->m_pLODs || cached->rootLOD != hw->m_RootLOD ||
		cached->lodCount != hw->m_NumLODs || cached->studioMeshCount != hw->m_NumStudioMeshes )
	{
		if ( cached ) { delete cached; g_StaticPropModels.RemoveAt(cacheIndex); }
		cached = new StaticPropModelVisibility;
		cached->header = hdr; cached->hardware = hw; cached->checksum = hdr->checksum;
		cached->hardwareLODs = hw->m_pLODs; cached->rootLOD = hw->m_RootLOD;
		cached->lodCount = hw->m_NumLODs; cached->studioMeshCount = hw->m_NumStudioMeshes;
		if ( !++g_StaticPropModelGeneration ) ++g_StaticPropModelGeneration;
		cached->generation = g_StaticPropModelGeneration;
		cached->valid = BuildStaticPropModelVisibility(model,hdr,hw,*cached);
		if ( !cached->valid ) cached->meshes.Purge();
		g_StaticPropModels.Insert(model,cached);
		PublishModelMeshMetadata(model,hdr,hw);
	}
	if ( propIndex == g_StaticPropReceivers.InvalidIndex() ) return;
	StaticPropReceiverPose &prop = g_StaticPropReceivers[propIndex];
	if ( prop.publishedGeneration == cached->generation ) return;
	DX12StaticPropReceiver receiver = {};
	receiver.staticPropOrdinal = prop.ordinal; receiver.modelChecksum = cached->checksum;
	receiver.poseIdentity = DX12StaticPropPoseIdentity(prop.authored.m_Origin.Base(),prop.authored.m_Angles.Base(),
		prop.authored.m_LightingOrigin.Base(),prop.authored.m_Flags);
	matrix3x4_t modelToWorld;
	AngleMatrix(prop.authored.m_Angles,prop.authored.m_Origin,modelToWorld);
	memcpy(receiver.modelToWorld,modelToWorld.Base(),sizeof(receiver.modelToWorld));
	receiver.meshCount = cached->meshes.Count(); receiver.meshes = cached->meshes.Base();
	receiver.modelName = modelinfo->GetModelName(model); receiver.skin = prop.authored.m_Skin;
	g_Lighting->RegisterStaticPropReceiver(uint64(reinterpret_cast<uintptr_t>(renderable)),receiver);
	prop.publishedGeneration = cached->generation;
}
bool ShadowMapsDX12_ShadowsEnabled() { return !g_ClientLevelShutdown && ShadowMapsDX12_Active() && r_shadowmap_enable.GetBool(); }
const char *ShadowMapsDX12_LastError() { PollCompletedViews(); return g_Error; }
void ShadowMapsDX12_RejectMap( const char *reason ) { Fail(reason); }
bool ShadowMapsDX12_CanDrawReceiverViews()
{
	return !g_ClientLevelShutdown && !g_DeviceResetPending && !ShadowMapsDX12_LastError()[0];
}
void ShadowMapsDX12_SetServerTransmitReady( bool ready ) { g_TransmitReady=ready; }

// Lit draws outside a receiver scope get the backend's neutral packet (no
// selected sun/local irradiance). No additional client scopes are needed there.
// Acceptance automation: r_shadowmap_autoexec holds a ';'-separated cfg list; item i is exec'd on the
// (i+1)*60th playable main view of the map; loading plaques and menu backgrounds do not advance it.
void AutoexecTick( ShadowMapReceiverViewKind_t kind )
{
	const char *list=r_shadowmap_autoexec.GetString();
	if ( kind!=SHADOWMAP_VIEW_MAIN || !list[0] || !engine->IsInGame() ||
		engine->IsDrawingLoadingImage() || engine->IsLevelMainMenuBackground() ) return;
	++g_OrdinaryAutoexecFrames;
	if ( g_OrdinaryAutoexecFrames%60 ) return;
	int index=g_OrdinaryAutoexecFrames/60-1;
	const char *p=list;
	for ( int i=0;i<index && p;++i ) { p=strchr(p,';'); if ( p ) ++p; }
	if ( !p || !*p ) return;
	const char *end=strchr(p,';'); int len=end?int(end-p):int(strlen(p));
	char cmd[MAX_PATH+8]; V_snprintf(cmd,sizeof(cmd),"exec %.*s\n",len,p);
	engine->ClientCmd_Unrestricted(cmd);
}
void ShadowMapsDX12_PrepareMainView( const CViewSetup &setup )
{
	if ( g_ClientLevelShutdown || g_DeviceResetPending || !ShadowMapsDX12_Active() ||
		g_SelectionFrame==(uint32)gpGlobals->framecount ) return;
	if ( !PollCompletedViews() ) return;
	if ( setup.width<=0 || setup.height<=0 ) { Fail(SHADOWMAP_ERR_INVALID_METADATA); return; }
	VPlane frustum[6];
	render->Push3DView(setup,0,NULL,frustum);
	UpdateRealtimeSelection(setup);
	render->PopView(frustum);
}

bool ShadowMapsDX12_BeginReceiverView( const CViewSetup &setup, ShadowMapReceiverViewKind_t kind )
{
	VPROF_BUDGET( "ShadowMapsDX12_BeginReceiverView", "Shadowmaps" );
	if ( g_ClientLevelShutdown || g_DeviceResetPending || !PollCompletedViews() ) return false;
	AutoexecTick(kind);
	if ( !ShadowMapsDX12_Active() ) return false;
	if ( g_DepthFrame!=(uint32)gpGlobals->framecount )
	{
		g_DepthFrame=(uint32)gpGlobals->framecount;
		g_FrameDepthRenders=0;
		g_FrameRigidLayerRebuilds=0; memset(g_FrameViewStats,0,sizeof(g_FrameViewStats)); g_FrameRigidEvents.RemoveAll();
	}
	DX12HighresMapStatus native={}; char nativeError[256]={0};
	if ( !g_Highres ) return Fail(SHADOWMAP_ERR_REQUIRES_DX12);
	{
		VPROF_BUDGET( "ShadowMapsDX12::ReceiverHighresStatus", "Shadowmaps" );
		g_Highres->GetStatus(native,nativeError,sizeof(nativeError));
	}
	const hlight::ManifestModeDisk &mode=*g_State.manifest.mode[g_State.selectedMode].record;
	if ( native.state!=DX12_HIGHRES_READY || native.nativeMapGeneration!=g_State.nativeMapGeneration ||
		native.faceLump!=mode.faceLump || native.lightingLump!=mode.lightingLump )
		return Fail(nativeError[0]?nativeError:"Highres lightmaps: receiver requires READY exact native generation/mode");
	if ( !g_TransmitReady ) return Fail(SHADOWMAP_ERR_TRANSMIT_UNAVAILABLE);
	if ( setup.width<=0 || setup.height<=0 ) return Fail(SHADOWMAP_ERR_INVALID_METADATA);
	CShadowViewData *data=AcquireView(setup,kind); data->receiver=setup; data->kind=kind;
	if ( g_SelectionFrame!=(uint32)gpGlobals->framecount ) { Fail("Shadowmaps: main-view hybrid selection was not prepared before receiver views"); return AbortView(*data); }
	data->shadowsEnabled=g_FrameShadowsEnabled;
	// Do not mutate targets retained by in-flight packets. Reenable invalidates
	// CPU cache identities only; the usual leases/copy-on-write protect pixels.
	if ( data->shadowsEnabled && !g_RuntimeShadowsWereEnabled ) ShadowMapsDX12_InvalidateStatic();
	g_RuntimeShadowsWereEnabled=data->shadowsEnabled;
	// Admit every authored prop, independent of PVS/occlusion and draw order.
	if ( kind == SHADOWMAP_VIEW_MAIN )
	{
		static ConVarRef rootLOD("r_rootlod");
		if ( g_StaticPropRegistrationRootLOD != rootLOD.GetInt() )
		{
			ClearStaticPropModelVisibility();
			g_StaticPropRegistrationRootLOD = rootLOD.GetInt();
		}
		if ( !g_StaticPropRegistrationComplete )
		{
			g_StaticPropRegistrationComplete = true;
			FOR_EACH_MAP( g_StaticPropReceivers, prop )
			{
				ShadowMapsDX12_PrepareStaticPropReceiver(g_StaticPropReceivers.Key(prop));
				if ( !g_StaticPropReceivers[prop].publishedGeneration ) g_StaticPropRegistrationComplete = false;
			}
		}
	}
	if ( !++g_ViewGeneration ) ++g_ViewGeneration;
	data->packet.mapGeneration=g_MapGeneration; data->packet.viewGeneration=g_ViewGeneration;
	data->packet.highresRoute=1; data->packet.nativeMapGeneration=native.nativeMapGeneration;
	for ( uint32 i=0;i<hlight::kLightstyleCount;++i )
		data->packet.styles[i]=engine->LightStyleValue(i);
	data->stats.selectedLights=g_Selected.Count();
	DX12LightingViewConstantsV1 &constants=data->packet.constants;
	constants.cShadowView0[0]=g_MapGeneration; constants.cShadowView0[1]=g_ViewGeneration;
	constants.cShadowView0[2]=r_shadowmap_filter.GetInt(); constants.cShadowView0[3]=r_shadowmap_debug.GetInt();
	float skipRadiance=r_shadowmap_skip_radiance.GetFloat();
	constants.cSunBasisY[3]=ShadowMap_IsFiniteFloat(skipRadiance) ? MAX(skipRadiance,0.0f) : 0.0f;
	constants.cShadowViewport[0]=(float)setup.x; constants.cShadowViewport[1]=(float)setup.y;
	constants.cShadowViewport[2]=1.0f/setup.width; constants.cShadowViewport[3]=1.0f/setup.height;
	constants.cSunIdentity[0]=0xffffffffu;
	if ( !data->shadowsEnabled ) constants.cShadowView1[3]|=DX12_SHADOW_VIEW_UNSHADOWED;
	Vector forward,right,up; AngleVectors(setup.angles,&forward,&right,&up);
	CopyVector3(setup.origin,constants.cEyePosition); constants.cEyePosition[3]=setup.zNear;
	CopyVector3(forward,constants.cViewForward);
	// Fixed detail enters clean depth; only intersecting orientation-dependent
	// detail is overlaid into working depth using this immutable receiver tuple.
	data->detail=DetailObjectSystem()->SnapshotShadowOrientation(setup.origin,forward,right,up);
#ifdef _DEBUG
	static int checkedMode=0;
	if ( r_shadowmap_debug.GetInt()!=checkedMode ) { checkedMode=r_shadowmap_debug.GetInt(); if ( checkedMode>0 ) ProjectionSelfCheck(); }
#endif
	// The engine writes all four outputs unconditionally (NULL crashes inside its matrix concat).
	VMatrix worldToView,projection,clip,worldToPixels;
	{
		VPROF_BUDGET( "ShadowMapsDX12::ReceiverMatrices", "Shadowmaps" );
		render->GetMatricesForView(setup,&worldToView,&projection,&clip,&worldToPixels);
	}
	ShadowCasterVolume_t receiverVolume; if ( !ExtractVolume(clip,receiverVolume) ) return AbortView(*data);
	const ShadowMapClientMapState &map=ShadowMapsDX12_MapState();
	{
		VPROF_BUDGET( "ShadowMapsDX12::SelectRelevantLights", "Shadowmaps" );
		for ( int i=0;i<g_Locals.Count();++i )
		{
			LocalLight_t &light=g_Locals[i];
			light.relevant=light.world.radius<=0 || (BoxInVolume(receiverVolume,light.influence.mins,light.influence.maxs) && ShadowMapScene_VolumeIntersectsBox(light.influence,receiverVolume.m_vecMins,receiverVolume.m_vecMaxs));
			// Query the complete finite influence, not the emitter origin: a hidden
			// light can still illuminate a visible doorway/window. The receiver's
			// pushed engine view owns occlusion here, before any shadow traversal.
			// Viewmodel projection is not world visibility; keep its lights.
			if ( light.relevant && light.world.radius>0 && kind!=SHADOWMAP_VIEW_VIEWMODEL &&
				!setup.m_bOrtho && engine->IsOccluded(light.influence.mins,light.influence.maxs) )
				light.relevant=false;
			if ( !light.relevant ) continue;
			data->relevant.AddToTail(i);
		}
	}
	// Radiance relevance stays per receiver; chart membership/weights are the
	// immutable frame-global main-player selection, including nested views.
	SelectRealtimeLocals(*data);
	ShadowCasterVolume_t query; query.m_nPlaneCount=0;
	query.m_vecMins.Init(FLT_MAX,FLT_MAX,FLT_MAX); query.m_vecMaxs.Init(-FLT_MAX,-FLT_MAX,-FLT_MAX);
	bool needsCasters=g_Sun>=0;
	for ( int i=0;i<data->relevant.Count();++i )
	{
		if ( !data->realtimeLocals[i] ) continue;
		const LocalLight_t &light=g_Locals[data->relevant[i]];
		needsCasters=true;
		if ( light.world.radius<=0 )
		{
			query.m_vecMins.Init(MIN_COORD_FLOAT,MIN_COORD_FLOAT,MIN_COORD_FLOAT);
			query.m_vecMaxs.Init(MAX_COORD_FLOAT,MAX_COORD_FLOAT,MAX_COORD_FLOAT);
		}
		else AddBounds(query.m_vecMins,query.m_vecMaxs,light.influence.mins,light.influence.maxs);
	}
	// Baked-only locals never require caster input. Sun still needs every
	// outside-BSP/upstream registration for conservative depth coverage.
	if ( g_Sun>=0 )
	{
		query.m_vecMins.Init(MIN_COORD_FLOAT,MIN_COORD_FLOAT,MIN_COORD_FLOAT);
		query.m_vecMaxs.Init(MAX_COORD_FLOAT,MAX_COORD_FLOAT,MAX_COORD_FLOAT);
	}
	if ( data->shadowsEnabled && needsCasters )
	{
		VPROF_BUDGET( "ShadowMapsDX12::EnumerateCasters", "Shadowmaps" );
		CollectReceiverCasters(*data,query);
	}
	data->stats.casters=data->casters.Count(); data->stats.relevantLights=data->relevant.Count()+(g_Sun>=0?1:0);
	Vector supportedMins=map.worldMins,supportedMaxs=map.worldMaxs;
	{
		VPROF_BUDGET( "ShadowMapsDX12::SupportedCasterBounds", "Shadowmaps" );
		AddBounds(supportedMins,supportedMaxs,receiverVolume.m_vecMins,receiverVolume.m_vecMaxs);
		for ( int c=0;c<data->casters.Count();++c ) AddBounds(supportedMins,supportedMaxs,data->casters[c].mins,data->casters[c].maxs);
		data->hasDetails=DetailObjectSystem()->GetShadowCasterBounds(data->detailMins,data->detailMaxs);
		if ( data->hasDetails ) AddBounds(supportedMins,supportedMaxs,data->detailMins,data->detailMaxs);
	}
	{
		VPROF_BUDGET( "ShadowMapsDX12::AllocateSlots", "Shadowmaps" );
		for ( int i=0;i<data->relevant.Count();++i )
		{
			LocalLight_t &light=g_Locals[data->relevant[i]];
			if ( !data->realtimeLocals[i] ) continue;
			// A common far plane only grows to cover supported geometry/views. Do
			// not shrink it on A->B->A receiver switches and thrash static depth.
			// This is projection coverage, never an illumination-radius cutoff.
			if ( light.world.radius<=0 && ShadowMapScene_LocalFar(light.world,supportedMins,supportedMaxs)>light.influence.zFar )
				ShadowMapScene_LocalInfluence(light.world,supportedMins,supportedMaxs,light.influence);
			for ( int f=0;data->shadowsEnabled && f<light.faceCount;++f ) if ( !AllocateSlot(data->relevant[i],f) )
			{
				++data->stats.residencyFailures; Fail(SHADOWMAP_ERR_RESIDENCY); return AbortView(*data);
			}
		}
	}
	if ( data->shadowsEnabled && !DistributeCasters(*data) ) return AbortView(*data);
	{
		VPROF_BUDGET( "ShadowMapsDX12::BuildLocals", "Shadowmaps" );
		for ( int i=0;i<data->relevant.Count();++i ) if ( !BuildLocal(*data,data->relevant[i],i) ) return AbortView(*data);
	}
	if ( !BuildSun(*data,clip,forward) || !BuildTiles(*data,clip,forward) ) return AbortView(*data);
	{
		VPROF_BUDGET( "ShadowMapsDX12::LeaseLocalTargets", "Shadowmaps" );
		if ( data->shadowsEnabled ) data->targets.SetCount(g_Pages.Count());
		if ( data->targets.Count() ) memset(data->targets.Base(),0,data->targets.Count()*sizeof(DX12ShadowTarget_t));
		for ( int l=0;data->shadowsEnabled && l<data->relevant.Count();++l )
		{
			LocalLight_t &light=g_Locals[data->relevant[l]];
			for ( int f=0;f<light.faceCount;++f )
			{
				if ( !data->lights[l].faces[f][3] ) continue;
				int p=light.page[f];
				if ( !data->targets[p] ) { data->targets[p]=g_Pages[p].work; ++data->stats.pages; }
				if ( !data->Lease(data->targets[p]) ) { Fail(SHADOWMAP_ERR_RESIDENCY); return AbortView(*data); }
			}
		}
	}
	data->packet.viewportX=setup.x; data->packet.viewportY=setup.y; data->packet.viewportWidth=setup.width; data->packet.viewportHeight=setup.height;
	data->packet.localTargets=data->targets.Base(); data->packet.localTargetCount=data->targets.Count();
	data->packet.lights=data->lights.Base(); data->packet.lightCount=data->lights.Count(); constants.cShadowView1[2]=data->lights.Count();
	// Keep the complete CSR prefix for models/detail/moved/non-highres receivers.
	// Baked-direct world draws use only this ascending packet-local GPU-index
	// tail in the existing t1028 buffer. Publish after BuildSun, which owns xy.
	const int residentCount=(int)data->stats.positiveWeightLocals;
	if ( residentCount>INT_MAX-data->indices.Count() ) { Fail(SHADOWMAP_ERR_RESIDENCY); return AbortView(*data); }
	constants.cSunIdentity[2]=(uint32)data->indices.Count();
	data->indices.EnsureCapacity(data->indices.Count()+residentCount);
	for ( int l=0;l<data->lights.Count();++l )
		if ( data->lights[l].realtimeWeight>0 ) data->indices.AddToTail((uint32)l);
	constants.cSunIdentity[3]=(uint32)data->indices.Count()-constants.cSunIdentity[2];
	Assert(constants.cSunIdentity[3]==data->stats.positiveWeightLocals);
	// Appending may have reallocated; no packet pointer may keep the CSR's old base.
	data->packet.tileIndices=data->indices.Base(); data->packet.tileIndexCount=data->indices.Count();
	DetailObjectSystem()->GetShadowReport(data->detailReport);
	if ( constants.cShadowView0[3]==DX12_SHADOW_DEBUG_CASTER_BOUNDS )
	{
		VPROF_BUDGET( "ShadowMapsDX12::DebugCasterBounds", "Shadowmaps" );
		for ( int i=0;i<data->casters.Count();++i )
		{
			const ShadowMapSceneCaster_t &caster=data->casters[i];
			debugoverlay->AddBoxOverlay(vec3_origin,caster.mins,caster.maxs,vec3_angle,caster.immutable?0:255,caster.immutable?255:64,64,32,0);
		}
	}
	// Nonzero-view status remains Pending until EndView replay completes.
	// PollCompletedViews owns its sole terminal consumption after that point.
	{
		VPROF_BUDGET( "ShadowMapsDX12::BeginView.UploadPacket", "Shadowmaps" );
		g_Lighting->BeginView(data->packet);
	}
	g_ViewStack.AddToTail(data); return true;
}
void ShadowMapsDX12_EndReceiverView()
{
	VPROF_BUDGET( "ShadowMapsDX12_EndReceiverView", "Shadowmaps" );
	if ( !g_ViewStack.Count() ) return;
	CShadowViewData *data=g_ViewStack.Tail(); g_ViewStack.Remove(g_ViewStack.Count()-1);
	{
		VPROF_BUDGET( "ShadowMapsDX12::EndView", "Shadowmaps" );
		g_Lighting->EndView();
	}
	ShadowFrameViewStats_t &frame=g_FrameViewStats[data->kind];
	++frame.views; frame.depthRenders+=data->stats.depthRenders;
	frame.rigidLayerRebuilds+=data->stats.rigidLayerRebuilds; frame.overlayCasterDraws+=data->stats.overlayCasterDraws;
	frame.rigidLayerBypasses+=data->stats.rigidLayerBypasses; frame.invalidRigidLayers+=data->stats.invalidRigidLayers;
	data->active=false;
	data->AddRef(); g_PendingViews.AddToTail(data);
	PollCompletedViews();
	if ( g_ViewStack.Count() )
	{
		const ShadowMapDetailOrientation_t &parent=g_ViewStack.Tail()->detail;
		DetailObjectSystem()->SnapshotShadowOrientation(parent.m_vecViewOrigin,parent.m_vecViewForward,parent.m_vecViewRight,parent.m_vecViewUp);
	}
}
void ShadowMapsDX12_OnCasterMoved( IClientRenderable *renderable, const Vector &oldMins, const Vector &oldMaxs, const Vector &newMins, const Vector &newMaxs )
{
	IClientUnknown *unknown=renderable ? renderable->GetIClientUnknown() : NULL;
	C_BaseEntity *entity=unknown ? unknown->GetBaseEntity() : NULL;
	if ( entity && !entity->AdvanceShadowDepthRevision() ) ShadowMapsDX12_InvalidateCasterRegistration(renderable);
	++g_CasterContentGeneration;
	if ( !g_CasterContentGeneration ) ShadowMapsDX12_InvalidateCasterRegistration();
	for ( int l=0;l<g_Locals.Count();++l )
	{
		LocalLight_t &local=g_Locals[l];
		for ( int f=0;f<local.faceCount;++f )
		{
			ShadowCache_t &cache=local.cache[f];
			// Uninitialized/pending faces are conservatively notified; initialized
			// faces include their committed camera; a pending replacement is
			// conservatively covered even before its volume is initialized.
			if ( !cache.valid || cache.pending ||
				BoxInVolume(local.faceVolumes[f],oldMins,oldMaxs) || BoxInVolume(local.faceVolumes[f],newMins,newMaxs) )
			{
				++cache.notifications;
				// Wrap cannot alias a stamp, even in the theoretical 2^64-event case.
				if ( !cache.notifications ) cache.committedFrame=-1;
				cache.dirty=true;
			}
		}
	}
	for ( int v=0;v<g_ViewPool.Count();++v ) for ( int c=0;c<4;++c )
	{
		CShadowViewData &data=*g_ViewPool[v];
		ShadowCache_t &cache=data.cascades[c];
		if ( !cache.valid || cache.pending || BoxInVolume(data.cascadeVolumes[c],oldMins,oldMaxs) ||
			BoxInVolume(data.cascadeVolumes[c],newMins,newMaxs) )
		{
			++cache.notifications;
			if ( !cache.notifications ) cache.committedFrame=-1;
			cache.dirty=true;
		}
	}
}
void ShadowMapsDX12_InvalidateCasterRegistration( IClientRenderable *renderable )
{
	IClientUnknown *unknown=renderable ? renderable->GetIClientUnknown() : NULL;
	C_BaseEntity *entity=unknown ? unknown->GetBaseEntity() : NULL;
	++g_CasterContentGeneration;
	bool wrapped=++g_CasterRegistrationGeneration==0;
	if ( wrapped ) ++g_CasterRegistrationGeneration;
	// Stamp from a global sequence, not a constructor-reset local counter:
	// pointer/EHANDLE reuse cannot alias an older rigid registration key.
	if ( entity ) entity->SetShadowDepthRegistrationRevision(g_CasterRegistrationGeneration);
	if ( renderable && !wrapped ) return;
	// Unspecified invalidation and theoretical wraps cannot be localized.
	for ( int l=0;l<g_Locals.Count();++l ) for ( int f=0;f<g_Locals[l].faceCount;++f )
	{
		ShadowCache_t &cache=g_Locals[l].cache[f];
		if ( wrapped ) cache.committedFrame=-1;
		cache.rigidValid=false; cache.pendingRigidInputsInvalidated=true;
	}
	for ( int v=0;v<g_ViewPool.Count();++v ) for ( int c=0;c<4;++c )
	{
		ShadowCache_t &cache=g_ViewPool[v]->cascades[c];
		if ( wrapped ) cache.committedFrame=-1;
		cache.rigidValid=false; cache.pendingRigidInputsInvalidated=true;
	}
}
void ShadowMapsDX12_InvalidateStatic()
{
	++g_StaticGeneration; if ( !g_StaticGeneration ) ++g_StaticGeneration;
	ClearStaticPropModelVisibility();
	++g_CasterContentGeneration;
	if ( !g_CasterContentGeneration ) ShadowMapsDX12_InvalidateCasterRegistration();
}

namespace
{
bool ParseSpawnInteger( const char *text, int &value )
{
	char *end;
	long parsed = strtol( text, &end, 10 );
	if ( !*text || *end || parsed < 0 || parsed > INT_MAX )
		return false;
	value = (int)parsed;
	return true;
}

void ShadowSpawnRenderablesCommand( const CCommand &args )
{
	const char *usage = "Usage: r_shadowmap_spawn_renderables <count> [model] [ox oy oz] [gridX gridY] [sx sy sz]\n";
	int argc = args.ArgC(), count;
	if ( ( argc != 2 && argc != 3 && argc != 6 && argc != 8 && argc != 11 ) ||
		!ParseSpawnInteger( args[1], count ) )
	{
		Warning( "%s", usage );
		return;
	}
	if ( count == 0 )
	{
		if ( !g_SpawnedRenderables.Count() )
			Msg( "ShadowMapSpawnRenderables: destroyed=0\n" );
		else
			DestroySpawnedRenderables();
		return;
	}
	if ( !engine->IsInGame() )
	{
		Warning( "ShadowMapSpawnRenderables: load a map before spawning renderables\n" );
		return;
	}

	const char *modelName = argc >= 3 ? args[2] : "models/props_junk/garbage_carboard001a.mdl";
	Vector origin( -448, -448, 8 ), spacing( 2, 2, 0 );
	int gridX = 17, gridY = 241;
	if ( argc >= 6 )
		origin.Init( V_atof( args[3] ), V_atof( args[4] ), V_atof( args[5] ) );
	if ( argc >= 8 && ( !ParseSpawnInteger( args[6], gridX ) || !ParseSpawnInteger( args[7], gridY ) ) )
	{
		Warning( "%s", usage );
		return;
	}
	if ( argc == 11 )
		spacing.Init( V_atof( args[8] ), V_atof( args[9] ), V_atof( args[10] ) );
	if ( gridX <= 0 || gridY <= 0 || !origin.IsValid() || !spacing.IsValid() )
	{
		Warning( "ShadowMapSpawnRenderables: require finite coordinates and positive grid dimensions\n" );
		return;
	}

	const model_t *model = modelinfo->FindOrLoadModel( modelName );
	if ( !model || !filesystem->FileExists( modelName, "GAME" ) ||
		modelinfo->GetModelType( model ) != mod_studio || !modelinfo->GetStudiomodel( model ) )
	{
		Warning( "ShadowMapSpawnRenderables: missing or invalid studio model %s\n", modelName );
		return;
	}
	// Loading a model does not necessarily give it an index usable by client entities.
	if ( modelinfo->GetModelIndex( modelName ) == -1 && modelinfo->RegisterDynamicModel( modelName, true ) == -1 )
	{
		Warning( "ShadowMapSpawnRenderables: cannot register model %s\n", modelName );
		return;
	}

	DestroySpawnedRenderables();
	int nonNetworkable = ClientEntityList().NumberOfEntities( true ) - ClientEntityList().NumberOfEntities( false );
	if ( count > NUM_ENT_ENTRIES - MAX_EDICTS - 1 - nonNetworkable )
	{
		Warning( "ShadowMapSpawnRenderables: insufficient client-only entity slots for %d renderables\n", count );
		return;
	}
	g_SpawnedRenderables.EnsureCapacity( count );
	for ( int i = 0; i < count; ++i )
	{
		CShadowMapFixtureRenderable *entity = new CShadowMapFixtureRenderable;
		if ( !entity->InitializeAsClientEntity( modelName, RENDER_GROUP_OPAQUE_ENTITY ) )
		{
			entity->Release();
			DestroySpawnedRenderables();
			Warning( "ShadowMapSpawnRenderables: failed to initialize renderable %d for %s\n", i, modelName );
			return;
		}
		entity->SetAbsOrigin( origin + Vector( ( i % gridX ) * spacing.x, ( i / gridX ) * spacing.y, spacing.z ) );
		entity->SetAbsAngles( vec3_angle );
		// Refresh the registered leaf-system bounds after positioning; no think/update loop is needed.
		entity->AddToLeafSystem( RENDER_GROUP_OPAQUE_ENTITY );
		EHANDLE handle = entity;
		g_SpawnedRenderables.AddToTail( handle );
	}
	Msg( "ShadowMapSpawnRenderables: spawned=%d model=%s\n", count, modelName );
}

void ReadSunVisibilityStats( DX12LightingSunVisibilityStats &stats )
{
	memset(&stats,0,sizeof(stats));
	if ( g_Lighting && g_MapGeneration ) g_Lighting->GetSunVisibilityStats(g_MapGeneration,stats);
}
void PrintStats( const ShadowStats_t &s, const DX12LightingSunVisibilityStats &visibility )
{
	Msg("Shadowmaps: selectedLights=%u relevantLights=%u pages=%u casters=%u staticRedraws=%u dynamicRedraws=%u depthCopies=%u csrEntries=%u residencyFailures=%u\n",
		s.selectedLights,s.relevantLights,s.pages,s.casters,s.staticRedraws,s.dynamicRedraws,s.depthCopies,s.csrEntries,s.residencyFailures);
	Msg("Shadowmaps hybrid: realtimeLights=%u positiveWeightLocals=%u bakedOnlyLocals=%u transitioningLocals=%u promotions=%u demotions=%u (sun excluded)\n",
		s.realtimeLights,s.positiveWeightLocals,s.bakedOnlyLocals,s.transitioningLocals,s.promotions,s.demotions);
	Msg("Shadowmaps depthWork: frame=%u renders=%u pageCopies=%u; lastView depthRenders=%u deferredCharts=%u initializingCharts=%u reusedCharts=%u pageCopies=%u crossFrameReusedCharts=%u crossFrameReusedSunCharts=%u uncertifiedCasters=%u rigidLayerRebuilds=%u overlayCasterDraws=%u\n",
		g_DepthFrame,g_FrameDepthRenders,FramePageCopies(),s.depthRenders,s.deferredCharts,s.initializingCharts,s.reusedCharts,s.pageCopies,
		s.crossFrameReusedCharts,s.crossFrameReusedSunCharts,s.uncertifiedCasters,s.rigidLayerRebuilds,s.overlayCasterDraws);
	Msg("Shadowmaps footprint: footprintRestorePixels=%llu fullRestores=%u (work restores only)\n",s.footprintRestorePixels,s.fullRestores);
	const ShadowFullRestoreReasons_t &r=s.fullRestoreReasons;
	Msg("Shadowmaps fullRestoreReasons: unknownPrev=%u rebuild=%u matrix=%u depth=%u slot=%u target=%u source=%u cow=%u rope=%u customDraw=%u nonfinite=%u wNonPositive=%u invalidBounds=%u unknownGeometry=%u queuedRope=%u boundsCoverChart=%u\n",
		r.unknownPrev,r.rebuild,r.matrix,r.depth,r.slot,r.target,r.source,r.cow,r.rope,r.customDraw,r.nonfinite,r.wNonPositive,r.invalidBounds,r.unknownGeometry,r.queuedRope,r.boundsCoverChart);
	Msg("Shadowmaps frameRigid: frame=%u rigidLayerRebuilds=%u\n",g_DepthFrame,g_FrameRigidLayerRebuilds);
	for ( int k=0;k<=SHADOWMAP_VIEW_VIEWMODEL;++k ) if ( g_FrameViewStats[k].views )
	{
		const ShadowFrameViewStats_t &v=g_FrameViewStats[k];
		Msg("Shadowmaps viewDepth: kind=%d views=%u renders=%u rigidLayerRebuilds=%u overlayCasterDraws=%u rigidLayerBypasses=%u invalidRigidLayers=%u\n",
			k,v.views,v.depthRenders,v.rigidLayerRebuilds,v.overlayCasterDraws,v.rigidLayerBypasses,v.invalidRigidLayers);
	}
	for ( int i=0;i<g_FrameRigidEvents.Count();++i )
	{
		const ShadowRigidEvent_t &e=g_FrameRigidEvents[i];
		Msg("Shadowmaps rigidChart: kind=%d light=%d face=%d action=%d reasons=%u entity=%d keyFields=%u\n",
			e.kind,e.light,e.face,e.action,e.change.flags,e.change.entity.ToInt(),e.change.keyFields);
	}
	Msg("Shadowmaps sunVisibility: receiverFaces=%u mappedFaces=%u pages=%u unresolvedDraws=%u\n",
		visibility.receiverFaces,visibility.mappedFaces,visibility.pages,visibility.unresolvedDraws);
}
void ShadowStatsCommand()
{
	PollCompletedViews();
	DX12LightingSunVisibilityStats visibility;
	ReadSunVisibilityStats(visibility);
	PrintStats(g_LastStats,visibility);
	DX12StaticPropVisibilityStats props = {};
	if ( g_Lighting && g_MapGeneration ) g_Lighting->GetStaticPropVisibilityStats(g_MapGeneration,props);
	Msg("Shadowmaps propVisibility: registeredProps=%u mappedDraws=%llu unmatchedDraws=%llu movedDraws=%llu ambiguousDraws=%llu topologyCacheBuilds=%llu\n",
		props.registeredProps,props.mappedDraws,props.unmatchedDraws,props.movedDraws,props.ambiguousDraws,props.topologyCacheBuilds);
	Msg("Shadowmaps model attribution: modelDraws=%llu authoredModelDraws=%llu registeredReceiverDraws=%llu registeredReceiverFallbackDraws=%llu detailOverflowDraws=%llu\n",
		props.modelDraws,props.authoredModelDraws,props.registeredReceiverDraws,props.registeredReceiverFallbackDraws,props.detailOverflowDraws);
	for ( uint32 reason=0;reason<DX12_PROP_VISIBILITY_REASON_COUNT;++reason )
		if ( props.reasonCounts[reason] ) Msg("Shadowmaps prop reason %s=%llu\n",
			DX12StaticPropVisibilityReasonName(DX12StaticPropVisibilityReason(reason)),props.reasonCounts[reason]);
	if ( g_LastMain ) for ( int r=0;r<g_LastMain->lightRanks.Count();++r )
	{
		const ShadowLightRank_t &rank=g_LastMain->lightRanks[r];
		if ( rank.resident ) Msg("Shadowmaps realtimeLightIds: lightId=%u score=%.9g weight=%.9g departing=%d\n",rank.lightId,rank.score,rank.weight,rank.desired ? 0 : 1);
	}
	if ( g_Highres )
	{
		DX12HighresMapStatus status={}; char error[256]={0};
		g_Highres->GetStatus(status,error,sizeof(error));
		Msg("Highres: state=%u density=%u native=%llu layout=%llu faceLump=%u lightingLump=%u faces=%u pages=%u gpuBytes=%llu assetBytes=%llu error=\"%s\"\n",
			unsigned(status.state),status.density,static_cast<unsigned long long>(status.nativeMapGeneration),
			static_cast<unsigned long long>(status.layoutGeneration),status.faceLump,status.lightingLump,
			status.faceCount,status.pageCount,static_cast<unsigned long long>(status.gpuBytes),
			static_cast<unsigned long long>(status.assetBytes),error);
	}
}
class CReportCasterSink : public IShadowCasterSink
{
public:
	virtual void Add( IClientRenderable *renderable, bool ) { entries.AddToTail(renderable); }
	CUtlVector<IClientRenderable *> entries;
};
int PointerCompare( IClientRenderable * const *a, IClientRenderable * const *b )
{
	uintp x=(uintp)*a,y=(uintp)*b; return x<y?-1:x>y?1:0;
}
void JsonString( CUtlBuffer &out, const char *text )
{
	out.PutChar('"');
	for ( const unsigned char *p=(const unsigned char *)text;*p;++p )
	{
		if ( *p=='"' || *p=='\\' ) { out.PutChar('\\'); out.PutChar(*p); }
		else if ( *p<32 ) out.Printf("\\u%04x",(unsigned)*p);
		else out.PutChar(*p);
	}
	out.PutChar('"');
}
class CReportPropVisibilitySink : public IDX12StaticPropVisibilityDetailsSink
{
public:
	explicit CReportPropVisibilitySink( CUtlBuffer &out ) : m_Out(out), m_First(true) {}
	virtual void OnStaticPropVisibilityDetail( const DX12StaticPropVisibilityDetail &detail )
	{
		if ( !m_First ) m_Out.PutChar(',');
		m_First=false;
		m_Out.PutString("{\"reason\":"); JsonString(m_Out,DX12StaticPropVisibilityReasonName(detail.reason));
		m_Out.PutString(",\"model\":"); JsonString(m_Out,detail.modelName ? detail.modelName : "");
		m_Out.PutString(",\"material\":"); JsonString(m_Out,detail.materialName ? detail.materialName : "");
		m_Out.PutString(",\"vertexShader\":"); JsonString(m_Out,detail.vertexShader ? detail.vertexShader : "");
		m_Out.PutString(",\"pixelShader\":"); JsonString(m_Out,detail.pixelShader ? detail.pixelShader : "");
		m_Out.Printf(",\"draws\":%llu,\"meshToken\":%llu,\"materialToken\":%llu,\"checksum\":%u,"
			"\"bodyPart\":%u,\"subModel\":%u,\"lod\":%u,\"studioMesh\":%u,\"stripGroup\":%u,\"skin\":%u,"
			"\"pass\":%u,\"flags\":%u,\"primitive\":%u,\"positionSlot\":%u,\"positionFormat\":%u,\"positionRepetitions\":%u}",
			detail.draws,detail.meshToken,detail.materialToken,detail.modelChecksum,detail.bodyPart,detail.subModel,
			detail.lod,detail.studioMesh,detail.stripGroup,detail.skin,detail.pass,detail.flags,detail.primitive,
			detail.positionSlot,detail.positionFormat,detail.positionRepetitions);
	}
private:
	CUtlBuffer &m_Out;
	bool m_First;
};
void WriteDepthWorkReport( CUtlBuffer &out )
{
	out.Printf("\"depthWork\":{\"frame\":%u,\"renders\":%u,\"pageCopies\":%u,\"rigidLayerRebuilds\":%u,"
		"\"reasonBits\":{\"noLayer\":1,\"clean\":2,\"projection\":4,\"depth\":8,\"slot\":16,\"target\":32,\"membership\":64,"
		"\"registration\":128,\"entityKey\":256,\"materialKey\":512,\"drawNotification\":1024,\"proofInvalidated\":2048,\"noCasters\":4096},"
		"\"keyFieldBits\":{\"transform\":1,\"identity\":4,\"appearance\":8,\"drawSettings\":16},\"views\":[",
		g_DepthFrame,g_FrameDepthRenders,FramePageCopies(),g_FrameRigidLayerRebuilds);
	bool comma=false;
	for ( int k=0;k<=SHADOWMAP_VIEW_VIEWMODEL;++k ) if ( g_FrameViewStats[k].views )
	{
		const ShadowFrameViewStats_t &v=g_FrameViewStats[k];
		out.Printf("%s{\"kind\":%d,\"views\":%u,\"depthRenders\":%u,\"rigidLayerRebuilds\":%u,\"overlayCasterDraws\":%u,\"rigidLayerBypasses\":%u,\"invalidRigidLayers\":%u}",
			comma ? "," : "",k,v.views,v.depthRenders,v.rigidLayerRebuilds,v.overlayCasterDraws,v.rigidLayerBypasses,v.invalidRigidLayers);
		comma=true;
	}
	out.PutString("],\"rigidEvents\":[");
	for ( int i=0;i<g_FrameRigidEvents.Count();++i )
	{
		const ShadowRigidEvent_t &e=g_FrameRigidEvents[i];
		out.Printf("%s{\"kind\":%d,\"light\":%d,\"face\":%d,\"action\":%d,\"reasons\":%u,\"entity\":%d,\"keyFields\":%u,\"model\":",
			i ? "," : "",e.kind,e.light,e.face,e.action,e.change.flags,e.change.entity.ToInt(),e.change.keyFields);
		C_BaseEntity *entity=e.change.entity.Get();
		const model_t *model=entity ? entity->GetModel() : NULL;
		JsonString(out,model ? modelinfo->GetModelName(model) : "");
		out.PutChar('}');
	}
	out.PutString("]},");
}
void ShadowReportCommand( const CCommand &args )
{
	if ( args.ArgC()!=2 ) { Warning("Usage: r_shadowmap_report <path>\n"); return; }
	PollCompletedViews();
	CShadowViewData *data=g_LastMain;
	if ( data ) data->AddRef();
	if ( !data ) { Warning("Shadowmaps: no completed main receiver view\n"); return; }
	CUtlBuffer out(0,0,CUtlBuffer::TEXT_BUFFER);
	const ShadowStats_t &s=data->stats;
	DX12LightingSunVisibilityStats visibility;
	ReadSunVisibilityStats(visibility);
	out.Printf("{\"schema\":1,\"counters\":{\"selectedLights\":%u,\"relevantLights\":%u,\"pages\":%u,\"casters\":%u,\"staticRedraws\":%u,\"dynamicRedraws\":%u,\"depthCopies\":%u,\"csrEntries\":%u,\"residencyFailures\":%u,\"depthRenders\":%u,\"deferredCharts\":%u,\"initializingCharts\":%u,\"reusedCharts\":%u,\"pageCopies\":%u,\"crossFrameReusedCharts\":%u,\"crossFrameReusedSunCharts\":%u,\"uncertifiedCasters\":%u,\"rigidLayerRebuilds\":%u,\"overlayCasterDraws\":%u,\"rigidLayerBypasses\":%u,\"invalidRigidLayers\":%u,\"realtimeLights\":%u,\"positiveWeightLocals\":%u,\"bakedOnlyLocals\":%u,\"transitioningLocals\":%u,\"promotions\":%u,\"demotions\":%u,\"footprintRestorePixels\":%llu,\"fullRestores\":%u",
		s.selectedLights,s.relevantLights,s.pages,s.casters,s.staticRedraws,s.dynamicRedraws,s.depthCopies,s.csrEntries,s.residencyFailures,
		s.depthRenders,s.deferredCharts,s.initializingCharts,s.reusedCharts,s.pageCopies,
		s.crossFrameReusedCharts,s.crossFrameReusedSunCharts,s.uncertifiedCasters,s.rigidLayerRebuilds,s.overlayCasterDraws,
		s.rigidLayerBypasses,s.invalidRigidLayers,s.realtimeLights,s.positiveWeightLocals,s.bakedOnlyLocals,s.transitioningLocals,s.promotions,s.demotions,
		s.footprintRestorePixels,s.fullRestores);
	const ShadowFullRestoreReasons_t &r=s.fullRestoreReasons;
	out.Printf(",\"fullRestoreReasons\":{\"unknownPrev\":%u,\"rebuild\":%u,\"matrix\":%u,\"depth\":%u,\"slot\":%u,\"target\":%u,\"source\":%u,\"cow\":%u,\"rope\":%u,\"customDraw\":%u,\"nonfinite\":%u,\"wNonPositive\":%u,\"invalidBounds\":%u,\"unknownGeometry\":%u,\"queuedRope\":%u,\"boundsCoverChart\":%u}},",
		r.unknownPrev,r.rebuild,r.matrix,r.depth,r.slot,r.target,r.source,r.cow,r.rope,r.customDraw,r.nonfinite,r.wNonPositive,r.invalidBounds,r.unknownGeometry,r.queuedRope,r.boundsCoverChart);
	out.Printf("\"realtimeLightIds\":[");
	bool firstRealtime=true;
	for ( int l=0;l<data->lightRanks.Count();++l ) if ( data->lightRanks[l].resident )
	{
		const ShadowLightRank_t &rank=data->lightRanks[l];
		out.Printf("%s{\"lightId\":%u,\"score\":%.9g,\"weight\":%.9g,\"departing\":%s}",firstRealtime ? "" : ",",rank.lightId,rank.score,rank.weight,rank.desired ? "false" : "true"); firstRealtime=false;
	}
	out.Printf("],");
	WriteDepthWorkReport(out);
	DX12StaticPropVisibilityStats props = {};
	if ( g_Lighting && g_MapGeneration ) g_Lighting->GetStaticPropVisibilityStats(g_MapGeneration,props);
	out.Printf("\"propVisibility\":{\"registeredProps\":%u,\"mappedDraws\":%llu,\"unmatchedDraws\":%llu,\"movedDraws\":%llu,\"ambiguousDraws\":%llu,\"topologyCacheBuilds\":%llu,"
		"\"modelDraws\":%llu,\"authoredModelDraws\":%llu,\"registeredReceiverDraws\":%llu,\"registeredReceiverFallbackDraws\":%llu,\"detailOverflowDraws\":%llu,\"reasons\":{",
		props.registeredProps,props.mappedDraws,props.unmatchedDraws,props.movedDraws,props.ambiguousDraws,props.topologyCacheBuilds,
		props.modelDraws,props.authoredModelDraws,props.registeredReceiverDraws,props.registeredReceiverFallbackDraws,props.detailOverflowDraws);
	for ( uint32 reason=0;reason<DX12_PROP_VISIBILITY_REASON_COUNT;++reason )
	{
		if ( reason ) out.PutChar(',');
		JsonString(out,DX12StaticPropVisibilityReasonName(DX12StaticPropVisibilityReason(reason)));
		out.Printf(":%llu",props.reasonCounts[reason]);
	}
	out.PutString("},\"details\":[");
	CReportPropVisibilitySink propDetails(out);
	if ( g_Lighting && g_MapGeneration ) g_Lighting->GetStaticPropVisibilityDetails(g_MapGeneration,propDetails);
	out.PutString("]},");
	out.Printf("\"sunVisibility\":{\"receiverFaces\":%u,\"mappedFaces\":%u,\"pages\":%u,\"unresolvedDraws\":%u},\"casterValidation\":[",
		visibility.receiverFaces,visibility.mappedFaces,visibility.pages,visibility.unresolvedDraws);
	CReportCasterSink reported,exhaustive;
	for ( int v=0;v<data->volumes.Count();++v )
	{
		reported.entries.RemoveAll(); exhaustive.entries.RemoveAll();
		ClientLeafSystem()->EnumerateShadowCasters(data->volumes[v].volume,reported);
		ClientLeafSystem()->EnumerateShadowCastersExhaustive(data->volumes[v].volume,exhaustive);
		reported.entries.Sort(PointerCompare); exhaustive.entries.Sort(PointerCompare);
		int a=0,b=0,mismatch=0;
		while ( a<reported.entries.Count() || b<exhaustive.entries.Count() )
		{
			if ( a==reported.entries.Count() ) { mismatch+=exhaustive.entries.Count()-b; break; }
			if ( b==exhaustive.entries.Count() ) { mismatch+=reported.entries.Count()-a; break; }
			if ( reported.entries[a]==exhaustive.entries[b] ) { ++a; ++b; }
			else if ( (uintp)reported.entries[a]<(uintp)exhaustive.entries[b] ) { ++a; ++mismatch; }
			else { ++b; ++mismatch; }
		}
		out.Printf("%s{\"light\":%d,\"testedCount\":%d,\"reportedCount\":%d,\"exhaustiveCount\":%d,\"mismatchCount\":%d,\"exceeds4096\":%s}",v?",":"",data->volumes[v].light,exhaustive.entries.Count(),reported.entries.Count(),exhaustive.entries.Count(),mismatch,exhaustive.entries.Count()>4096?"true":"false");
	}
	const IDetailObjectSystem::ShadowReport_t &detail=data->detailReport;
	out.Printf("],\"detailCases\":[{\"name\":\"fast\",\"fast\":true,\"matched\":%s,\"count\":%d},{\"name\":\"ordinary\",\"fast\":false,\"matched\":%s,\"count\":%d}],\"mapState\":{",detail.m_bShadowLitSprites?"true":"false",detail.m_nFastSprites,detail.m_bShadowLitSprites?"true":"false",detail.m_nOrdinarySprites);
	const ShadowMapClientMapState &map=ShadowMapsDX12_MapState();
	out.Printf("\"featureMap\":%s,\"runtimeActive\":%s,\"mode\":%d,\"selectedLights\":%u,\"sun\":%d,\"error\":",map.featureMap?"true":"false",map.runtimeActive?"true":"false",map.selectedMode,map.lightCount,map.sunLightIndex);
	JsonString(out,g_Error[0]?g_Error:map.error);
	const CViewSetup &view=data->receiver;
	out.Printf("},\"receiver\":{\"kind\":%d,\"frame\":%u,\"generation\":%u,"
		"\"origin\":[%.9g,%.9g,%.9g],\"angles\":[%.9g,%.9g,%.9g],"
		"\"fov\":%.9g,\"aspect\":%.9g,\"near\":%.9g,\"far\":%.9g,"
		"\"viewport\":[%d,%d,%d,%d],\"orthographic\":%s}}\n",
		(int)data->kind,data->lastUsedFrame,data->packet.viewGeneration,
		view.origin.x,view.origin.y,view.origin.z,view.angles.x,view.angles.y,view.angles.z,
		view.fov,view.m_flAspectRatio,view.zNear,view.zFar,
		view.x,view.y,view.width,view.height,view.m_bOrtho?"true":"false");
	if ( filesystem->WriteFile(args[1],"GAME",out) ) Msg("Shadowmaps: wrote %s\n",args[1]);
	else Warning("Shadowmaps: cannot write %s\n",args[1]);
	data->Release();
}
ConCommand r_shadowmap_stats("r_shadowmap_stats",ShadowStatsCommand,"Print last completed shadow receiver view counters",FCVAR_NONE);
ConCommand r_shadowmap_report("r_shadowmap_report",ShadowReportCommand,"Write last completed main-view shadow data and exhaustive caster comparison as JSON",FCVAR_CHEAT);
ConCommand r_shadowmap_spawn_renderables( "r_shadowmap_spawn_renderables", ShadowSpawnRenderablesCommand,
	"Spawn client-only shadow caster fixtures: <count> [model] [ox oy oz] [gridX gridY] [sx sy sz]; 0 destroys them", FCVAR_CHEAT );
}
