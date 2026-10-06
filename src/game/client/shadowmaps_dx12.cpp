//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "c_baseanimating.h"
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
#include "engine/ivdebugoverlay.h"
#include "filesystem.h"
#include "studio.h"
#include "model_types.h"
#include "worldsize.h"
#include "tier0/threadtools.h"
#include "tier0/memdbgon.h"


ConVar r_csm_distance( "r_csm_distance", "4096", FCVAR_ARCHIVE, "Sun cascade receiver distance", true, 128, true, 32768 );
ConVar r_shadowmap_filter( "r_shadowmap_filter", "0", FCVAR_ARCHIVE, "0: four-tap PCF, 1: bounded contact-hardening PCSS", true, 0, true, 1 );
ConVar r_shadowmap_spot_near( "r_shadowmap_spot_near", "4", FCVAR_ARCHIVE, "Spotlight shadow-camera near distance in Source units (including six-face spots); does not change illumination", true, 0.1f, true, 64.0f );
ConVar r_shadowmap_autoexec( "r_shadowmap_autoexec", "", FCVAR_CHEAT, "Acceptance automation: cfg exec'd once per map after the first completed main receiver view" );
ConVar r_shadowmap_debug( "r_shadowmap_debug", "0", FCVAR_CHEAT, "0 normal, 1 cascades, 2 visibility, 3 pages/faces, 4 caster bounds, 5 blockers, 6 radius; debug builds also check projection math" );

namespace
{
struct ShadowStats_t
{
	uint32 selectedLights, relevantLights, pages, casters, staticRedraws, dynamicRedraws, depthCopies, csrEntries, residencyFailures;
};
struct ShadowVolumeReport_t
{
	int light;
	ShadowCasterVolume_t volume;
};
struct ShadowTileRect_t { int x0, y0, x1, y1; };
struct ShadowCache_t
{
	ShadowCache_t() : staticGeneration(0), detailGeneration(0), slotGeneration(0), hadDynamic(false), dirty(true) { memset(matrix,0,sizeof(matrix)); }
	uint32 staticGeneration, detailGeneration, slotGeneration;
	float matrix[16];
	bool hadDynamic, dirty;
};
struct LocalLight_t
{
	LocalLight_t() : projectionNear(0), projectionFar(0), faceCount(0), relevant(false)
	{
		projection.Identity(); memset(faceVolumes,0,sizeof(faceVolumes));
		for ( int i=0;i<6;++i ) { page[i]=-1; slot[i]=-1; }
	}
	dworldlight_t world;
	DX12LightingSelectedLight selected;
	ShadowMapInfluenceVolume_t influence;
	VMatrix projection;
	ShadowCasterVolume_t faceVolumes[6];
	float projectionNear, projectionFar; // Shadow-camera cache key, independent of the conservative influence near.
	int faceCount, page[6], slot[6];
	ShadowCache_t cache[6];
	bool relevant;
};
struct LocalPage_t
{
	LocalPage_t() : work(0), clean(0), generation(1) { for ( int i=0;i<64;++i ) owners[i]=-1; }
	DX12ShadowTarget_t work, clean;
	int owners[64];
	uint32 generation;
};
struct SparePage_t { int page; DX12ShadowTarget_t target; };

IShaderAPIDX12Lighting *g_Lighting = NULL;
IShaderAPIDX12HighresLightmaps *g_Highres = NULL;
uint32 g_MapGeneration = 0, g_ViewGeneration = 0, g_StaticGeneration = 1;
bool g_Admitted = false, g_TransmitReady = false, g_MaterialFeature = false;
bool g_ClientLevelShutdown = false;
bool g_DeviceCallbacksRegistered = false;
bool g_DeviceResetPending = false, g_DeviceRestoreComplete = false;
bool g_ResourceResetInProgress = false;
char g_Error[256] = "";
CUtlVector<DX12LightingSelectedLight> g_Selected;
CUtlVector<LocalLight_t> g_Locals;
CUtlVector<LocalPage_t> g_Pages;
CUtlVector<SparePage_t> g_PageSpares;
int g_Sun = -1;
ShadowMapClientMapState g_State;
uint32 g_MapCounter = 0;
char g_MapName[MAX_PATH] = "";

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
bool ExtractVolume( const VMatrix &clip, ShadowCasterVolume_t &volume, const VMatrix *clipToWorld = NULL )
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
	CShadowViewData() : active(false), lastUsedFrame(0xffffffffu), hasDetails(false), drawCandidateFirst(-1), drawCandidateCount(0), cascadeClean(0), cascadeWork(0), staticSun(0), staticSunGeneration(0), staticSunDetail(0)
	{ memset(&packet,0,sizeof(packet)); memset(staticSunMatrix,0,sizeof(staticSunMatrix)); }
	~CShadowViewData()
	{
		ReleaseLeases(); DestroyTarget(cascadeClean); DestroyTarget(cascadeWork); DestroyTarget(staticSun);
	}
	bool Idle() const { return !active && GetRefCount()==1; }
	bool Lease( DX12ShadowTarget_t target )
	{
		if ( !target ) return true;
		for ( int i=0;i<leasedTargets.Count();++i ) if ( leasedTargets[i]==target ) return true;
		IRefCounted *lease=g_Lighting->RetainShadowDepthTarget(target);
		if ( !lease ) return false;
		leasedTargets.AddToTail(target); leases.AddToTail(lease); return true;
	}
	void ReleaseLeases()
	{
		for ( int i=0;i<leases.Count();++i ) leases[i]->Release();
		leases.RemoveAll(); leasedTargets.RemoveAll();
	}
	void Prepare()
	{
		active=true; ReleaseLeases();
		memset(&packet,0,sizeof(packet)); memset(&stats,0,sizeof(stats));
		// Freeze one finite setting for every spotlight face in this receiver packet.
		spotShadowNear=r_shadowmap_spot_near.GetFloat();
		spotShadowNear=ShadowMap_IsFiniteFloat(spotShadowNear) ? clamp(spotShadowNear,0.1f,64.0f) : 4.0f;
		lights.RemoveAll(); targets.RemoveAll(); ranges.RemoveAll(); indices.RemoveAll(); rects.RemoveAll(); casters.RemoveAll(); volumes.RemoveAll(); relevant.RemoveAll();
		casterRanges.RemoveAll(); casterIndices.RemoveAll(); drawCandidateFirst=-1; drawCandidateCount=0;
	}
	bool active;
	ShadowMapReceiverViewKind_t kind;
	uint32 lastUsedFrame;
	CViewSetup receiver;
	ShadowMapDetailOrientation_t detail;
	bool hasDetails;
	Vector detailMins,detailMaxs;
	float spotShadowNear;
	DX12LightingViewPacket packet;
	ShadowStats_t stats;
	CUtlVector<RuntimeShadowLightGpu> lights;
	CUtlVector<DX12ShadowTarget_t> targets, leasedTargets;
	CUtlVector<IRefCounted *> leases;
	CUtlVector<uint32> ranges, indices, cursor;
	CUtlVector<ShadowTileRect_t> rects;
	CUtlVector<ShadowMapSceneCaster_t> casters;
	CUtlVector<ShadowVolumeReport_t> volumes;
	CUtlVector<int> relevant;
	CUtlVector<uint32> casterRanges;
	CUtlVector<int> casterIndices;
	int drawCandidateFirst, drawCandidateCount;
	DX12ShadowTarget_t cascadeClean, cascadeWork, staticSun;
	ShadowCache_t cascades[4];
	ShadowCasterVolume_t cascadeVolumes[4];
	uint32 staticSunGeneration, staticSunDetail;
	float staticSunMatrix[16];
	IDetailObjectSystem::ShadowReport_t detailReport;
};

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
	// Device recreation erased old lighting statuses. Do not interpret them as
	// failed views, release their CPU leases, or readmit before native restoration.
	if ( g_DeviceResetPending ) return !g_Error[0];
	if ( !g_Lighting ) return !g_Error[0];
	char error[256]={0};
	if ( g_Admitted && g_Highres && !g_ClientLevelShutdown && !g_Error[0] )
	{
		DX12HighresMapStatus native={};
		g_Highres->GetStatus(native,error,sizeof(error));
		if ( native.state==DX12_HIGHRES_REJECTED || native.nativeMapGeneration!=g_State.nativeMapGeneration )
			Fail(error[0]?error:"Highres lightmaps: active native generation changed");
	}
	if ( g_MapGeneration && !g_Error[0] && g_Lighting->GetStatus(g_MapGeneration,0,error,sizeof(error))==DX12_LIGHTING_STATUS_FAILED ) Fail(error);
	int pending=0;
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
	if ( !data.Lease(dst) || !data.Lease(src) ) { Fail(SHADOWMAP_ERR_RESIDENCY); return; }
	g_Lighting->CopyShadowDepthRect(dst,src,x,y,x,y,size,size);
	++data.stats.depthCopies;
}

class CCollectCasters : public IShadowCasterSink
{
public:
	CCollectCasters( CShadowViewData &data ) : m_Data(data) {}
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
		m_Data.casters.AddToTail(caster);
	}
private:
	CShadowViewData &m_Data;
	// Shared by every caster query; no material array allocation per prop.
	static CUtlVector<IMaterial *> m_Materials;
};
CUtlVector<IMaterial *> CCollectCasters::m_Materials;

bool DistributeCasters( CShadowViewData &data )
{
	data.casterRanges.SetCount(data.relevant.Count()*2);
	for ( int l=0;l<data.relevant.Count();++l )
	{
		const ShadowMapInfluenceVolume_t &volume=g_Locals[data.relevant[l]].influence;
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
	if ( !TargetIdle(g_Pages[index].work) || !TargetIdle(g_Pages[index].clean) ) return false;
	for ( int i=0;i<g_PageSpares.Count();++i ) if ( g_PageSpares[i].page==index && !TargetIdle(g_PageSpares[i].target) ) return false;
	return true;
}
bool PageInactive( const LocalPage_t &page )
{
	for ( int s=0;s<64;++s ) if ( page.owners[s]>=0 && g_Locals[page.owners[s]].relevant ) return false;
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
			for ( int f=0;f<light.faceCount;++f ) if ( light.page[f]==index )
			{
				light.page[f]=-1; light.slot[f]=-1;
				// Eviction invalidates pixels, not the immutable light projection.
				ShadowCache_t &cache=light.cache[f];
				cache.staticGeneration=0; cache.detailGeneration=0; cache.slotGeneration=0;
				cache.hadDynamic=false; cache.dirty=true;
			}
		}
		page.owners[s]=-1;
	}
	DestroyTarget(page.work); DestroyTarget(page.clean); ++page.generation;
	for ( int i=g_PageSpares.Count()-1;i>=0;--i ) if ( g_PageSpares[i].page==index )
	{
		DestroyTarget(g_PageSpares[i].target); g_PageSpares.FastRemove(i);
	}
}
bool CreatePage( int index )
{
	LocalPage_t &page=g_Pages[index];
	if ( page.work && page.clean ) return true;
	char name[80]; V_snprintf(name,sizeof(name),"shadow_local_%u_%d_work",g_MapGeneration,index);
	page.work=g_Lighting->CreateShadowDepthTarget(name,4096,4096);
	V_snprintf(name,sizeof(name),"shadow_local_%u_%d_static",g_MapGeneration,index);
	page.clean=g_Lighting->CreateShadowDepthTarget(name,4096,4096);
	if ( page.work && page.clean ) return true;
	DestroyTarget(page.work); DestroyTarget(page.clean); return false;
}
bool AllocateSlot( int owner, int face )
{
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
	LocalPage_t &page=g_Pages[pageIndex];
	if ( !ParentSamples(page.work) ) return true;
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
	page.work=next;
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
bool HasDynamic( const CShadowViewData &data, const ShadowCasterVolume_t &volume )
{
	int count=data.drawCandidateFirst>=0 ? data.drawCandidateCount : data.casters.Count();
	for ( int i=0;i<count;++i )
	{
		int index=data.drawCandidateFirst>=0 ? data.casterIndices[data.drawCandidateFirst+i] : i;
		const ShadowMapSceneCaster_t &caster=data.casters[index];
		if ( !caster.immutable && BoxInVolume(volume,caster.mins,caster.maxs) ) return true;
	}
	return false;
}
void AddReportVolume( CShadowViewData &data, int id, const ShadowCasterVolume_t &volume )
{
	ShadowVolumeReport_t report; report.light=id; report.volume=volume; data.volumes.AddToTail(report);
}
bool RenderDepth( CShadowViewData &data, DX12ShadowTarget_t target, int x, int y, int size,
	const CViewSetup &lightView, const ShadowCasterVolume_t &volume, bool clear, bool world, bool immutable, bool dynamic, bool detail )
{
	if ( !data.Lease(target) ) return Fail(SHADOWMAP_ERR_RESIDENCY);
	ShadowMapDepthScene_t scene;
	scene.lightView=lightView; scene.receiverView=data.receiver; scene.volume=volume; scene.detail=data.detail;
	scene.casters=&data.casters; scene.lighting=g_Lighting; scene.target=target;
	scene.candidateIndices=data.drawCandidateFirst>=0 ? &data.casterIndices : NULL;
	scene.candidateFirst=data.drawCandidateFirst; scene.candidateCount=data.drawCandidateCount;
	scene.x=x; scene.y=y; scene.size=size; scene.clear=clear;
	scene.drawWorld=world; scene.drawStatic=immutable; scene.drawDynamic=dynamic; scene.drawDetail=detail;
	ViewRender_DrawShadowMapScene(scene);
	if ( world || immutable || detail ) ++data.stats.staticRedraws;
	if ( dynamic ) ++data.stats.dynamicRedraws;
	return true;
}

bool BuildLocal( CShadowViewData &data, int index, int relevantIndex )
{
	LocalLight_t &local=g_Locals[index];
	data.drawCandidateFirst=data.casterRanges[relevantIndex*2]; data.drawCandidateCount=data.casterRanges[relevantIndex*2+1];
	RuntimeShadowLightGpu &gpu=data.lights[data.lights.AddToTail()]; memset(&gpu,0,sizeof(gpu));
	const DX12LightingSelectedLight &light=local.selected;
	gpu.lightId=light.lightId; gpu.type=light.type; gpu.style=light.style; gpu.faceCount=local.faceCount;
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
	bool projectionChanged=local.projectionNear!=gpu.shadowNear || local.projectionFar!=gpu.shadowFar;
	AddReportVolume(data,(int)light.lightId,CasterVolume(local.influence));
	for ( int f=0;f<local.faceCount;++f )
	{
		ShadowCache_t &cache=local.cache[f];
		if ( projectionChanged )
		{
			VMatrix view,clip;
			ShadowMapScene_LocalFaceMatrices(local.world,f,gpu.shadowNear,gpu.shadowFar,512,476,view,local.projection,clip,gpu.tanRenderedHalfFov);
			CopyMatrix(clip,cache.matrix); if ( !ExtractVolume(clip,local.faceVolumes[f]) ) return false;
		}
		memcpy(gpu.worldToClip[f],cache.matrix,sizeof(cache.matrix));
		int pageIndex=local.page[f], slot=local.slot[f], x=(slot%8)*512, y=(slot/8)*512;
		LocalPage_t &page=g_Pages[pageIndex];
		gpu.faces[f][0]=pageIndex; gpu.faces[f][1]=x; gpu.faces[f][2]=y; gpu.faces[f][3]=512;
		const ShadowCasterVolume_t &volume=local.faceVolumes[f];
		bool hasDetail=data.hasDetails && BoxInVolume(volume,data.detailMins,data.detailMaxs);
		uint32 detailGeneration=hasDetail ? data.detail.m_nGeneration : 0;
		bool rebuild=projectionChanged || cache.staticGeneration!=g_StaticGeneration || cache.detailGeneration!=detailGeneration || cache.slotGeneration!=page.generation;
		bool dynamic=HasDynamic(data,volume);
		bool restore=rebuild || dynamic || cache.hadDynamic || cache.dirty;
		if ( !restore ) continue;
		if ( !WritablePage(data,pageIndex) ) return Fail(SHADOWMAP_ERR_RESIDENCY);
		Vector forward, up, right;
		if ( local.faceCount==6 ) ShadowMapScene_CubeFaceBasis(f,forward,up,right);
		else { forward=local.world.normal; VectorNormalize(forward); ShadowMapScene_SunBasis(forward,right,up); }
		CViewSetup setup; SetupLightView(setup,local.world.origin,forward,up,local.projection,gpu.shadowNear,gpu.shadowFar,512,false,0,gpu.tanRenderedHalfFov);
		if ( rebuild )
		{
			if ( !RenderDepth(data,page.clean,x,y,512,setup,volume,true,true,true,false,hasDetail) ) return false;
			cache.staticGeneration=g_StaticGeneration; cache.detailGeneration=detailGeneration; cache.slotGeneration=page.generation;
		}
		// Always restore CLEAN static depth, including when a caster has left.
		CopyDepthRect(data,page.work,page.clean,x,y,512);
		if ( g_Error[0] ) return false;
		if ( dynamic && !RenderDepth(data,page.work,x,y,512,setup,volume,false,false,false,true,false) ) return false;
		cache.hadDynamic=dynamic; cache.dirty=false;
	}
	local.projectionNear=gpu.shadowNear; local.projectionFar=gpu.shadowFar; return true;
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
	data.drawCandidateFirst=-1; data.drawCandidateCount=0;
	if ( g_Sun<0 ) return true;
	const ShadowMapClientMapState &map=ShadowMapsDX12_MapState();
	const DX12LightingSelectedLight &sun=g_Selected[g_Sun];
	Vector travel(sun.direction[0],sun.direction[1],sun.direction[2]), basisX,basisY; VectorNormalize(travel); ShadowMapScene_SunBasis(travel,basisX,basisY);
	DX12LightingViewConstantsV1 &constants=data.packet.constants;
	constants.cShadowView1[3]=DX12_SHADOW_VIEW_HAS_SUN;
	float style=data.packet.styles[sun.style];
	for ( int i=0;i<3;++i ) constants.cSunRadiance[i]=sun.radiance[i]*style;
	constants.cSunRadiance[3]=tanf(DEG2RAD(sun.shadowSunAngularRadius));
	CopyVector3(travel,constants.cSunTravel); CopyVector3(basisX,constants.cSunBasisX); CopyVector3(basisY,constants.cSunBasisY);
	constants.cSunIdentity[0]=sun.lightId; constants.cSunIdentity[1]=sun.style;
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
	VMatrix staticClip, staticClipToWorld; CViewSetup staticView;
	SunProjection(staticMins,staticMaxs,SnapSunCenter(center,radius,basisX,basisY,4060),radius,travel,basisX,basisY,4096,4060,staticClip,staticClipToWorld,staticView,constants.cShadowDepthRecords[4]);
	CopyMatrix(staticClip,constants.cStaticSunWorldToClip);
	constants.cStaticSunRect[2]=4096;
	ShadowCasterVolume_t staticVolume; if ( !ExtractVolume(staticClip,staticVolume,&staticClipToWorld) ) return false;
	AddReportVolume(data,-5,staticVolume);
	if ( !CreateSunTarget(data.staticSun,"shadow_static_sun") ) return Fail(SHADOWMAP_ERR_RESIDENCY);
	uint32 staticDetailGeneration=data.hasDetails ? data.detail.m_nGeneration : 0;
	if ( data.staticSunGeneration!=g_StaticGeneration || data.staticSunDetail!=staticDetailGeneration || memcmp(data.staticSunMatrix,constants.cStaticSunWorldToClip,sizeof(data.staticSunMatrix)) )
	{
		if ( !RenderDepth(data,data.staticSun,0,0,4096,staticView,staticVolume,true,true,true,false,data.hasDetails) ) return false;
		data.staticSunGeneration=g_StaticGeneration; data.staticSunDetail=staticDetailGeneration;
		memcpy(data.staticSunMatrix,constants.cStaticSunWorldToClip,sizeof(data.staticSunMatrix));
	}
	if ( !data.Lease(data.staticSun) ) return Fail(SHADOWMAP_ERR_RESIDENCY);
	data.packet.staticSunTarget=data.staticSun; constants.cShadowView1[3]|=DX12_SHADOW_VIEW_STATIC_SUN_VALID;
	float splits[5],blend[3],farDepth=MIN(data.receiver.zFar,r_csm_distance.GetFloat());
	constants.cSunTravel[3]=farDepth; constants.cSunBasisX[3]=0.9f*farDepth;
	if ( !ShadowMapScene_CascadeSplits(data.receiver.zNear,farDepth,splits,blend) ) return true;
	for ( int i=0;i<4;++i ) constants.cCascadeSplits[i]=splits[i+1];
	for ( int i=0;i<3;++i ) constants.cCascadeBlend[i]=blend[i];
	if ( !CreateSunTarget(data.cascadeClean,"shadow_cascade_static") || !CreateSunTarget(data.cascadeWork,"shadow_cascade_work") ) return Fail(SHADOWMAP_ERR_RESIDENCY);
	for ( int i=0;i<4;++i )
	{
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
		VMatrix clip, clipToWorld; CViewSetup lightView;
		SunProjection(depthMins,depthMaxs,snapped,sphereRadius,travel,basisX,basisY,2048,2012,clip,clipToWorld,lightView,constants.cShadowDepthRecords[i]);
		CopyMatrix(clip,constants.cSunWorldToClip[i]);
		int x=(i&1)*2048,y=(i/2)*2048;
		constants.cCascadeRects[i][0]=x; constants.cCascadeRects[i][1]=y; constants.cCascadeRects[i][2]=2048;
		if ( !ExtractVolume(clip,data.cascadeVolumes[i],&clipToWorld) ) return false;
		AddReportVolume(data,-1-i,data.cascadeVolumes[i]);
		ShadowCache_t &cache=data.cascades[i];
		bool hasDetail=data.hasDetails && BoxInVolume(data.cascadeVolumes[i],data.detailMins,data.detailMaxs);
		uint32 detailGeneration=hasDetail ? data.detail.m_nGeneration : 0;
		bool rebuild=cache.staticGeneration!=g_StaticGeneration || cache.detailGeneration!=detailGeneration || memcmp(cache.matrix,constants.cSunWorldToClip[i],sizeof(cache.matrix));
		bool dynamic=HasDynamic(data,data.cascadeVolumes[i]);
		if ( rebuild )
		{
			if ( !RenderDepth(data,data.cascadeClean,x,y,2048,lightView,data.cascadeVolumes[i],true,true,true,false,hasDetail) ) return false;
			cache.staticGeneration=g_StaticGeneration; cache.detailGeneration=detailGeneration;
			memcpy(cache.matrix,constants.cSunWorldToClip[i],sizeof(cache.matrix));
		}
		if ( rebuild || dynamic || cache.hadDynamic || cache.dirty )
		{
			CopyDepthRect(data,data.cascadeWork,data.cascadeClean,x,y,2048);
			if ( g_Error[0] ) return false;
			if ( dynamic && !RenderDepth(data,data.cascadeWork,x,y,2048,lightView,data.cascadeVolumes[i],false,false,false,true,false) ) return false;
		}
		cache.hadDynamic=dynamic; cache.dirty=false;
	}
	if ( !data.Lease(data.cascadeWork) ) return Fail(SHADOWMAP_ERR_RESIDENCY);
	data.packet.cascadeAtlasTarget=data.cascadeWork; constants.cShadowView1[3]|=DX12_SHADOW_VIEW_CSM_VALID;
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
	int x=(data.receiver.width+15)/16,y=(data.receiver.height+15)/16;
	uint64 tileCount=(uint64)x*y;
	if ( !x || !y || tileCount>INT_MAX/2 ) return Fail(SHADOWMAP_ERR_RESIDENCY);
	data.ranges.SetCount((int)tileCount*2); memset(data.ranges.Base(),0,data.ranges.Count()*sizeof(uint32));
	data.cursor.SetCount((int)tileCount); data.rects.SetCount(data.relevant.Count());
	for ( int l=0;l<data.relevant.Count();++l )
	{
		ShadowTileRect_t rect=ProjectInfluence(g_Locals[data.relevant[l]],data,clip,forward,x,y); data.rects[l]=rect;
		for ( int row=rect.y0;row<rect.y1;++row ) for ( int column=rect.x0;column<rect.x1;++column ) ++data.ranges[2*(row*x+column)+1];
	}
	uint64 total=0;
	for ( int t=0;t<(int)tileCount;++t )
	{
		data.ranges[2*t]=(uint32)total; data.cursor[t]=(uint32)total; total+=data.ranges[2*t+1];
		if ( total>INT_MAX ) return Fail(SHADOWMAP_ERR_RESIDENCY);
	}
	data.indices.SetCount((int)total);
	for ( int l=0;l<data.rects.Count();++l )
	{
		const ShadowTileRect_t &r=data.rects[l];
		for ( int row=r.y0;row<r.y1;++row ) for ( int column=r.x0;column<r.x1;++column ) data.indices[data.cursor[row*x+column]++]=l;
	}
	data.packet.constants.cShadowView1[0]=x; data.packet.constants.cShadowView1[1]=y;
	data.packet.tileCount=(uint32)tileCount; data.packet.tileRanges=data.ranges.Base();
	data.packet.tileIndices=data.indices.Base(); data.packet.tileIndexCount=(uint32)total;
	data.stats.csrEntries=(uint32)total; return true;
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

void ProjectionSelfCheck()
{
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
			LocalLight_t local; local.world=record.light; local.selected=selected;
			ShadowMapScene_LocalInfluence(local.world,g_State.worldMins,g_State.worldMaxs,local.influence);
			local.faceCount=local.influence.cube?6:1; g_Locals.AddToTail(local);
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
	if ( !g_ResourceResetInProgress ) DestroySpawnedRenderables();
	g_TransmitReady=false;
	if ( g_Lighting ) g_Lighting->SetReceiverFeatureGeneration(0);
	for ( int p=0;p<g_Pages.Count();++p ) { DestroyTarget(g_Pages[p].work); DestroyTarget(g_Pages[p].clean); }
	for ( int i=0;i<g_PageSpares.Count();++i ) DestroyTarget(g_PageSpares[i].target);
	g_PageSpares.RemoveAll();
	g_Pages.RemoveAll(); g_Locals.RemoveAll(); g_Selected.RemoveAll(); g_Sun=-1;
	if ( g_LastMain ) { g_LastMain->Release(); g_LastMain=NULL; }
	memset(&g_LastStats,0,sizeof(g_LastStats));
	for ( int i=0;i<g_ViewPool.Count();++i )
	{
		CShadowViewData *data=g_ViewPool[i];
		DestroyTarget(data->cascadeClean); DestroyTarget(data->cascadeWork); DestroyTarget(data->staticSun);
		data->ReleaseLeases(); data->active=false; data->staticSunGeneration=0;
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
bool ShadowMapsDX12_BeginReceiverView( const CViewSetup &setup, ShadowMapReceiverViewKind_t kind )
{
	if ( g_ClientLevelShutdown || g_DeviceResetPending || !PollCompletedViews() ) return false;
	AutoexecTick(kind);
	if ( !ShadowMapsDX12_Active() ) return false;
	DX12HighresMapStatus native={}; char nativeError[256]={0};
	if ( !g_Highres ) return Fail(SHADOWMAP_ERR_REQUIRES_DX12);
	g_Highres->GetStatus(native,nativeError,sizeof(nativeError));
	const hlight::ManifestModeDisk &mode=*g_State.manifest.mode[g_State.selectedMode].record;
	if ( native.state!=DX12_HIGHRES_READY || native.nativeMapGeneration!=g_State.nativeMapGeneration ||
		native.faceLump!=mode.faceLump || native.lightingLump!=mode.lightingLump )
		return Fail(nativeError[0]?nativeError:"Highres lightmaps: receiver requires READY exact native generation/mode");
	if ( !g_TransmitReady ) return Fail(SHADOWMAP_ERR_TRANSMIT_UNAVAILABLE);
	if ( setup.width<=0 || setup.height<=0 ) return Fail(SHADOWMAP_ERR_INVALID_METADATA);
	CShadowViewData *data=AcquireView(setup,kind); data->receiver=setup; data->kind=kind;
	if ( !++g_ViewGeneration ) ++g_ViewGeneration;
	data->packet.mapGeneration=g_MapGeneration; data->packet.viewGeneration=g_ViewGeneration;
	data->packet.highresRoute=1; data->packet.nativeMapGeneration=native.nativeMapGeneration;
	for ( uint32 i=0;i<hlight::kLightstyleCount;++i )
		data->packet.styles[i]=engine->LightStyleValue(i);
	data->stats.selectedLights=g_Selected.Count();
	DX12LightingViewConstantsV1 &constants=data->packet.constants;
	constants.cShadowView0[0]=g_MapGeneration; constants.cShadowView0[1]=g_ViewGeneration;
	constants.cShadowView0[2]=r_shadowmap_filter.GetInt(); constants.cShadowView0[3]=r_shadowmap_debug.GetInt();
	constants.cShadowViewport[0]=(float)setup.x; constants.cShadowViewport[1]=(float)setup.y;
	constants.cShadowViewport[2]=1.0f/setup.width; constants.cShadowViewport[3]=1.0f/setup.height;
	constants.cSunIdentity[0]=0xffffffffu;
	Vector forward,right,up; AngleVectors(setup.angles,&forward,&right,&up);
	CopyVector3(setup.origin,constants.cEyePosition); constants.cEyePosition[3]=setup.zNear;
	CopyVector3(forward,constants.cViewForward);
	// Binding policy: shadow-only detail models/cards are UNSWAYED and immutable
	// except for this snapshotted receiver billboard orientation. Only caches
	// intersecting their conservative bounds carry the orientation generation.
	data->detail=DetailObjectSystem()->SnapshotShadowOrientation(setup.origin,forward,right,up);
#ifdef _DEBUG
	static int checkedMode=0;
	if ( r_shadowmap_debug.GetInt()!=checkedMode ) { checkedMode=r_shadowmap_debug.GetInt(); if ( checkedMode>0 ) ProjectionSelfCheck(); }
#endif
	// The engine writes all four outputs unconditionally (NULL crashes inside its matrix concat).
	VMatrix worldToView,projection,clip,worldToPixels;
	render->GetMatricesForView(setup,&worldToView,&projection,&clip,&worldToPixels);
	ShadowCasterVolume_t receiverVolume; if ( !ExtractVolume(clip,receiverVolume) ) return AbortView(*data);
	const ShadowMapClientMapState &map=ShadowMapsDX12_MapState();
	ShadowCasterVolume_t query; query.m_nPlaneCount=0; query.m_vecMins=map.worldMins; query.m_vecMaxs=map.worldMaxs;
	// One spatial enumeration for the whole receiver. Includes outside-BSP
	// registrations and upstream sun casters, not the receiver/camera PVS.
	if ( g_Sun>=0 ) { query.m_vecMins.Init(MIN_COORD_FLOAT,MIN_COORD_FLOAT,MIN_COORD_FLOAT); query.m_vecMaxs.Init(MAX_COORD_FLOAT,MAX_COORD_FLOAT,MAX_COORD_FLOAT); }
	for ( int i=0;i<g_Locals.Count();++i )
	{
		LocalLight_t &light=g_Locals[i];
		light.relevant=light.world.radius<=0 || (BoxInVolume(receiverVolume,light.influence.mins,light.influence.maxs) && ShadowMapScene_VolumeIntersectsBox(light.influence,receiverVolume.m_vecMins,receiverVolume.m_vecMaxs));
		if ( !light.relevant ) continue;
		data->relevant.AddToTail(i); AddBounds(query.m_vecMins,query.m_vecMaxs,light.influence.mins,light.influence.maxs);
		if ( light.world.radius<=0 ) { query.m_vecMins.Init(MIN_COORD_FLOAT,MIN_COORD_FLOAT,MIN_COORD_FLOAT); query.m_vecMaxs.Init(MAX_COORD_FLOAT,MAX_COORD_FLOAT,MAX_COORD_FLOAT); }
	}
	CCollectCasters sink(*data); ClientLeafSystem()->EnumerateShadowCasters(query,sink);
	data->stats.casters=data->casters.Count(); data->stats.relevantLights=data->relevant.Count()+(g_Sun>=0?1:0);
	Vector supportedMins=map.worldMins,supportedMaxs=map.worldMaxs;
	AddBounds(supportedMins,supportedMaxs,receiverVolume.m_vecMins,receiverVolume.m_vecMaxs);
	for ( int c=0;c<data->casters.Count();++c ) AddBounds(supportedMins,supportedMaxs,data->casters[c].mins,data->casters[c].maxs);
	data->hasDetails=DetailObjectSystem()->GetShadowCasterBounds(data->detailMins,data->detailMaxs);
	if ( data->hasDetails ) AddBounds(supportedMins,supportedMaxs,data->detailMins,data->detailMaxs);
	for ( int i=0;i<data->relevant.Count();++i )
	{
		LocalLight_t &light=g_Locals[data->relevant[i]];
		// A common far plane only grows to cover supported geometry/views. Do
		// not shrink it on A->B->A receiver switches and thrash static depth.
		// This is projection coverage, never an illumination-radius cutoff.
		if ( light.world.radius<=0 && ShadowMapScene_LocalFar(light.world,supportedMins,supportedMaxs)>light.influence.zFar )
			ShadowMapScene_LocalInfluence(light.world,supportedMins,supportedMaxs,light.influence);
		for ( int f=0;f<light.faceCount;++f ) if ( !AllocateSlot(data->relevant[i],f) )
		{
			++data->stats.residencyFailures; Fail(SHADOWMAP_ERR_RESIDENCY); return AbortView(*data);
		}
	}
	if ( !DistributeCasters(*data) ) return AbortView(*data);
	for ( int i=0;i<data->relevant.Count();++i ) if ( !BuildLocal(*data,data->relevant[i],i) ) return AbortView(*data);
	if ( !BuildSun(*data,clip,forward) || !BuildTiles(*data,clip,forward) ) return AbortView(*data);
	data->targets.SetCount(g_Pages.Count()); memset(data->targets.Base(),0,data->targets.Count()*sizeof(DX12ShadowTarget_t));
	for ( int l=0;l<data->relevant.Count();++l )
	{
		LocalLight_t &light=g_Locals[data->relevant[l]];
		for ( int f=0;f<light.faceCount;++f )
		{
			int p=light.page[f];
			if ( !data->targets[p] ) { data->targets[p]=g_Pages[p].work; ++data->stats.pages; }
			if ( !data->Lease(data->targets[p]) ) { Fail(SHADOWMAP_ERR_RESIDENCY); return AbortView(*data); }
		}
	}
	data->packet.viewportX=setup.x; data->packet.viewportY=setup.y; data->packet.viewportWidth=setup.width; data->packet.viewportHeight=setup.height;
	data->packet.localTargets=data->targets.Base(); data->packet.localTargetCount=data->targets.Count();
	data->packet.lights=data->lights.Base(); data->packet.lightCount=data->lights.Count(); constants.cShadowView1[2]=data->lights.Count();
	DetailObjectSystem()->GetShadowReport(data->detailReport);
	if ( constants.cShadowView0[3]==DX12_SHADOW_DEBUG_CASTER_BOUNDS )
	{
		for ( int i=0;i<data->casters.Count();++i )
		{
			const ShadowMapSceneCaster_t &caster=data->casters[i];
			debugoverlay->AddBoxOverlay(vec3_origin,caster.mins,caster.maxs,vec3_angle,caster.immutable?0:255,caster.immutable?255:64,64,32,0);
		}
	}
	// Nonzero-view status remains Pending until EndView replay completes.
	// PollCompletedViews owns its sole terminal consumption after that point.
	g_Lighting->BeginView(data->packet);
	g_ViewStack.AddToTail(data); return true;
}
void ShadowMapsDX12_EndReceiverView()
{
	if ( !g_ViewStack.Count() ) return;
	CShadowViewData *data=g_ViewStack.Tail(); g_ViewStack.Remove(g_ViewStack.Count()-1);
	g_Lighting->EndView();
	data->active=false;
	data->AddRef(); g_PendingViews.AddToTail(data);
	PollCompletedViews();
	if ( g_ViewStack.Count() )
	{
		const ShadowMapDetailOrientation_t &parent=g_ViewStack.Tail()->detail;
		DetailObjectSystem()->SnapshotShadowOrientation(parent.m_vecViewOrigin,parent.m_vecViewForward,parent.m_vecViewRight,parent.m_vecViewUp);
	}
}
void ShadowMapsDX12_OnCasterMoved( const Vector &oldMins, const Vector &oldMaxs, const Vector &newMins, const Vector &newMaxs )
{
	for ( int l=0;l<g_Locals.Count();++l )
	{
		LocalLight_t &local=g_Locals[l];
		if ( ShadowMapScene_VolumeIntersectsBox(local.influence,oldMins,oldMaxs) || ShadowMapScene_VolumeIntersectsBox(local.influence,newMins,newMaxs) )
			for ( int f=0;f<local.faceCount;++f )
				if ( BoxInVolume(local.faceVolumes[f],oldMins,oldMaxs) || BoxInVolume(local.faceVolumes[f],newMins,newMaxs) ) local.cache[f].dirty=true;
	}
	for ( int v=0;v<g_ViewPool.Count();++v ) for ( int c=0;c<4;++c )
	{
		CShadowViewData &data=*g_ViewPool[v];
		if ( data.cascades[c].staticGeneration && (BoxInVolume(data.cascadeVolumes[c],oldMins,oldMaxs)||BoxInVolume(data.cascadeVolumes[c],newMins,newMaxs)) ) data.cascades[c].dirty=true;
	}
}
void ShadowMapsDX12_InvalidateStatic() { ++g_StaticGeneration; if ( !g_StaticGeneration ) ++g_StaticGeneration; }

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
	Msg("Shadowmaps sunVisibility: receiverFaces=%u mappedFaces=%u pages=%u unresolvedDraws=%u\n",
		visibility.receiverFaces,visibility.mappedFaces,visibility.pages,visibility.unresolvedDraws);
}
void ShadowStatsCommand()
{
	PollCompletedViews();
	DX12LightingSunVisibilityStats visibility;
	ReadSunVisibilityStats(visibility);
	PrintStats(g_LastStats,visibility);
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
	out.Printf("{\"schema\":1,\"counters\":{\"selectedLights\":%u,\"relevantLights\":%u,\"pages\":%u,\"casters\":%u,\"staticRedraws\":%u,\"dynamicRedraws\":%u,\"depthCopies\":%u,\"csrEntries\":%u,\"residencyFailures\":%u},\"sunVisibility\":{\"receiverFaces\":%u,\"mappedFaces\":%u,\"pages\":%u,\"unresolvedDraws\":%u},\"casterValidation\":[",
		s.selectedLights,s.relevantLights,s.pages,s.casters,s.staticRedraws,s.dynamicRedraws,s.depthCopies,s.csrEntries,s.residencyFailures,
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
	JsonString(out,g_Error[0]?g_Error:map.error); out.Printf("}}\n");
	if ( filesystem->WriteFile(args[1],"GAME",out) ) Msg("Shadowmaps: wrote %s\n",args[1]);
	else Warning("Shadowmaps: cannot write %s\n",args[1]);
	data->Release();
}
ConCommand r_shadowmap_stats("r_shadowmap_stats",ShadowStatsCommand,"Print last completed shadow receiver view counters",FCVAR_NONE);
ConCommand r_shadowmap_report("r_shadowmap_report",ShadowReportCommand,"Write last completed main-view shadow data and exhaustive caster comparison as JSON",FCVAR_CHEAT);
ConCommand r_shadowmap_spawn_renderables( "r_shadowmap_spawn_renderables", ShadowSpawnRenderablesCommand,
	"Spawn client-only shadow caster fixtures: <count> [model] [ox oy oz] [gridX gridY] [sx sy sz]; 0 destroys them", FCVAR_CHEAT );
}
