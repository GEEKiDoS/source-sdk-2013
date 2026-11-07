//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $Revision: $
// $NoKeywords: $
//
// This file contains code to allow us to associate client data with bsp leaves.
//===========================================================================//

#include "cbase.h"
#include "clientleafsystem.h"
#include "utlbidirectionalset.h"
#include "model_types.h"
#include "ivrenderview.h"
#include "tier0/vprof.h"
#include "bsptreedata.h"
#include "detailobjectsystem.h"
#include "engine/IStaticPropMgr.h"
#include "engine/ivdebugoverlay.h"
#include "vstdlib/jobthread.h"
#include "tier1/utllinkedlist.h"
#include "datacache/imdlcache.h"
#include "view.h"
#include "viewrender.h"

// memdbgon must be the last include file in a .cpp file!!!
#if defined( L4D2 )
#include "c_baseentity.h"
#include "c_baseplayer.h"
#include "tier0/threadtools.h"
#endif // L4D2
#if defined( L4D2 )
#include <emmintrin.h>
#endif
#include "tier0/memdbgon.h"

class VMatrix;  // forward decl

static ConVar cl_drawleaf("cl_drawleaf", "-1", 
#if defined( L4D2 )
	0x00004008
#else
	FCVAR_CHEAT
#endif
 );
static ConVar r_PortalTestEnts( "r_PortalTestEnts", "1", 
#if defined( L4D2 )
	0x00004008
#else
	FCVAR_CHEAT
#endif
, "Clip entities against portal frustums." );
static ConVar r_portalsopenall( "r_portalsopenall", "0", 
#if defined( L4D2 )
	0x00004008
#else
	FCVAR_CHEAT
#endif
, "Open all portals" );
static ConVar cl_threaded_client_leaf_system("cl_threaded_client_leaf_system", "0"  );


DEFINE_FIXEDSIZE_ALLOCATOR( CClientRenderablesList, 1, CUtlMemoryPool::GROW_SLOW );

//-----------------------------------------------------------------------------
// Threading helpers
//-----------------------------------------------------------------------------

static void FrameLock()
{
	mdlcache->BeginLock();
}

static void FrameUnlock()
{
	mdlcache->EndLock();
}

static void CallComputeFXBlend( IClientRenderable *&pRenderable )
{
	pRenderable->ComputeFxBlend();
}

#if defined( L4D2 )
struct RenderAlphaScreenState_t;
extern ConVar cl_leveloverview;
extern ConVar cl_leafsystemvis;
#endif

//-----------------------------------------------------------------------------
// The client leaf system
//-----------------------------------------------------------------------------
class CClientLeafSystem : public IClientLeafSystem, public ISpatialLeafEnumerator
{
public:
#if defined( L4D2 )
	bool CullCachedWorldBounds( ClientRenderHandle_t handle );
#endif

#if defined( L4D2 )
	void SetModelTranslucencyType( ClientRenderHandle_t handle, int type );
	void SetDrawForLocalUser( ClientRenderHandle_t handle, bool draw );
	void SetRenderWithViewModels( ClientRenderHandle_t handle, bool enable );
	void EnableBloatedRenderBounds( ClientRenderHandle_t handle, bool enable );
	void DisableCachedRenderBounds( ClientRenderHandle_t handle, bool disable );
	void AddEntityRenderable( C_BaseEntity *entity, bool renderWithViewModels, int type, bool drawForLocalUser );
	void UpdateModelType( ClientRenderHandle_t handle );
#endif

	virtual char const *Name() { return "CClientLeafSystem"; }

	// constructor, destructor
	CClientLeafSystem();
	virtual ~CClientLeafSystem();

	// Methods of IClientSystem
	bool Init() { return true; }
	void PostInit() {}
	void Shutdown() {}

	virtual bool IsPerFrame() { return true; }

	void PreRender();
	void PostRender() { }
	void Update( float frametime ) { }

	void LevelInitPreEntity();
	void LevelInitPostEntity() {}
	void LevelShutdownPreEntity();
	void LevelShutdownPostEntity();

	virtual void OnSave() {}
	virtual void OnRestore() {}
	virtual void SafeRemoveIfDesired() {}

// Methods of IClientLeafSystem
public:
	
	virtual void AddRenderable( IClientRenderable* pRenderable, RenderGroup_t group );
	virtual bool IsRenderableInPVS( IClientRenderable *pRenderable );
	virtual void CreateRenderableHandle( IClientRenderable* pRenderable, bool bIsStaticProp );
	virtual void RemoveRenderable( ClientRenderHandle_t handle );


	virtual void SetSubSystemDataInLeaf( int leaf, int nSubSystemIdx, CClientLeafSubSystemData *pData );
	virtual CClientLeafSubSystemData *GetSubSystemDataInLeaf( int leaf, int nSubSystemIdx );

	// FIXME: There's an incestuous relationship between DetailObjectSystem
	// and the ClientLeafSystem. Maybe they should be the same system?
	virtual void GetDetailObjectsInLeaf( int leaf, int& firstDetailObject, int& detailObjectCount );
 	virtual void SetDetailObjectsInLeaf( int leaf, int firstDetailObject, int detailObjectCount );
	virtual void DrawDetailObjectsInLeaf( int leaf, int frameNumber, int& nFirstDetailObject, int& nDetailObjectCount );
	virtual bool ShouldDrawDetailObjectsInLeaf( int leaf, int frameNumber );
	virtual void RenderableChanged( ClientRenderHandle_t handle );
	virtual void SetRenderGroup( ClientRenderHandle_t handle, RenderGroup_t group );
#if defined( L4D2 )
	void EnableRendering( ClientRenderHandle_t handle, bool enable );
#endif
	virtual void ComputeTranslucentRenderLeaf( int count, const LeafIndex_t *pLeafList, const LeafFogVolume_t *pLeafFogVolumeList, int frameNumber, int viewID );
#if defined( L4D2 )
	virtual void CollateViewModelRenderables( CUtlVector< ClientViewModelRenderable_t > &opaque, CUtlVector< ClientViewModelRenderable_t > &translucent );
#else
	virtual void CollateViewModelRenderables( CUtlVector< IClientRenderable * >& opaque, CUtlVector< IClientRenderable * >& translucent );
#endif
	virtual void BuildRenderablesList( const SetupRenderInfo_t &info );
#if !defined( L4D2 )
			void CollateRenderablesInLeaf( int leaf, int worldListLeafIndex, const SetupRenderInfo_t &info );
#endif
	virtual void DrawStaticProps( bool enable );
	virtual void DrawSmallEntities( bool enable );
	virtual void EnableAlternateSorting( ClientRenderHandle_t handle, bool bEnable );

	// Adds a renderable to a set of leaves
	virtual void AddRenderableToLeaves( ClientRenderHandle_t handle, int nLeafCount, unsigned short *pLeaves );

	// The following methods are related to shadows...
	virtual ClientLeafShadowHandle_t AddShadow( ClientShadowHandle_t userId, unsigned short flags );
	virtual void RemoveShadow( ClientLeafShadowHandle_t h );

	virtual void ProjectShadow( ClientLeafShadowHandle_t handle, int nLeafCount, const int *pLeafList );
	virtual void ProjectFlashlight( ClientLeafShadowHandle_t handle, int nLeafCount, const int *pLeafList );

	// Find all shadow casters in a set of leaves
	virtual void EnumerateShadowsInLeaves( int leafCount, LeafIndex_t* pLeaves, IClientLeafShadowEnum* pEnum );

	// methods of ISpatialLeafEnumerator
public:

	bool EnumerateLeaf( int leaf, intp context );

	// Adds a shadow to a leaf
	void AddShadowToLeaf( int leaf, ClientLeafShadowHandle_t handle );

	// Fill in a list of the leaves this renderable is in.
	// Returns -1 if the handle is invalid.
	int GetRenderableLeaves( ClientRenderHandle_t handle, int leaves[128] );

	// Get leaves this renderable is in
	virtual bool GetRenderableLeaf ( ClientRenderHandle_t handle, int* pOutLeaf, const int* pInIterator = 0, int* pOutIterator = 0 );

	// Singleton instance...
	static CClientLeafSystem s_ClientLeafSystem;

private:
	// Creates a new renderable
	void NewRenderable( IClientRenderable* pRenderable, RenderGroup_t type, int flags = 0 );

	// Adds a renderable to the list of renderables
	void AddRenderableToLeaf( int leaf, ClientRenderHandle_t handle );

	void SortEntities(  const Vector &vecRenderOrigin, const Vector &vecRenderForward, CClientRenderablesList::CEntry *pEntities, int nEntities );

	// Returns -1 if the renderable spans more than one area. If it's totally in one area, then this returns the leaf.
	short GetRenderableArea( ClientRenderHandle_t handle );

	// remove renderables from leaves
#if defined( L4D2 )
	void InsertIntoTree( ClientRenderHandle_t handle, const Vector &absMins, const Vector &absMaxs );
#else
	void InsertIntoTree( ClientRenderHandle_t &handle );
#endif
	void RemoveFromTree( ClientRenderHandle_t handle );

	// Returns if it's a view model render group
	inline bool IsViewModelRenderGroup( RenderGroup_t group ) const;

	// Adds, removes renderables from view model list
	void AddToViewModelList( ClientRenderHandle_t handle );
	void RemoveFromViewModelList( ClientRenderHandle_t handle );

	// Insert translucent renderables into list of translucent objects
	void InsertTranslucentRenderable( IClientRenderable* pRenderable,
		int& count, IClientRenderable** pList, float* pDist );

	// Used to change renderables from translucent to opaque
	// Only really used by the static prop fading...
	void ChangeRenderableRenderGroup( ClientRenderHandle_t handle, RenderGroup_t group );

	// Adds a shadow to a leaf/removes shadow from renderable
	void AddShadowToRenderable( ClientRenderHandle_t renderHandle, ClientLeafShadowHandle_t shadowHandle );
	void RemoveShadowFromRenderables( ClientLeafShadowHandle_t handle );

	// Adds a shadow to a leaf/removes shadow from renderable
	bool ShouldRenderableReceiveShadow( ClientRenderHandle_t renderHandle, int nShadowFlags );

	// Adds a shadow to a leaf/removes shadow from leaf
	void RemoveShadowFromLeaves( ClientLeafShadowHandle_t handle );

	// Methods associated with the various bi-directional sets
	static unsigned int& FirstRenderableInLeaf( int leaf ) 
	{ 
		return s_ClientLeafSystem.m_Leaf[leaf].m_FirstElement;
	}

	static unsigned int& FirstLeafInRenderable( unsigned short renderable ) 
	{ 
		return s_ClientLeafSystem.m_Renderables[renderable].m_LeafList;
	}

	static unsigned short& FirstShadowInLeaf( int leaf ) 
	{ 
		return s_ClientLeafSystem.m_Leaf[leaf].m_FirstShadow;
	}

	static unsigned short& FirstLeafInShadow( ClientLeafShadowHandle_t shadow ) 
	{ 
		return s_ClientLeafSystem.m_Shadows[shadow].m_FirstLeaf;
	}

	static unsigned short& FirstShadowOnRenderable( unsigned short renderable ) 
	{ 
		return s_ClientLeafSystem.m_Renderables[renderable].m_FirstShadow;
	}

	static unsigned short& FirstRenderableInShadow( ClientLeafShadowHandle_t shadow ) 
	{ 
		return s_ClientLeafSystem.m_Shadows[shadow].m_FirstRenderable;
	}

	void FrameLock()
	{
		mdlcache->BeginLock();
	}

	void FrameUnlock()
	{
		mdlcache->EndLock();
	}

private:
	enum
	{
		RENDER_FLAGS_TWOPASS		= 0x01,
		RENDER_FLAGS_STATIC_PROP	= 0x02,
		RENDER_FLAGS_BRUSH_MODEL	= 0x04,
		RENDER_FLAGS_STUDIO_MODEL	= 0x08,
		RENDER_FLAGS_HASCHANGED		= 0x10,
		RENDER_FLAGS_ALTERNATE_SORTING = 0x20,
	};

	// All the information associated with a particular handle
	struct RenderableInfo_t
	{
		IClientRenderable*	m_pRenderable;
		int					m_RenderFrame;	// which frame did I render it in?
		int					m_RenderFrame2;
		int					m_EnumCount;	// Have I been added to a particular shadow yet?
		int					m_TranslucencyCalculated;
		unsigned int		m_LeafList;		// What leafs is it in?
		unsigned int		m_RenderLeaf;	// What leaf do I render in?
		unsigned char		m_Flags;		// rendering flags
		unsigned char		m_RenderGroup;	// RenderGroup_t type
		unsigned short		m_FirstShadow;	// The first shadow caster that cast on it
		short m_Area;	// -1 if the renderable spans multiple areas.
		signed char			m_TranslucencyCalculatedView;
#if defined( L4D2 )
		C_BaseEntity *m_pL4D2AlphaOwner;
		int m_nL4D2WorldDataIndex;
		unsigned char m_nL4D2TranslucencyType;
		bool m_bL4D2DrawForLocalUser;
		bool m_bL4D2RenderingEnabled; // Native WORD22 disable flag0x1, inverse.
		bool m_bL4D2ViewModel;
		unsigned char m_nL4D2BoundsFlags;
		Vector m_vecCachedWorldMins;
		Vector m_vecCachedWorldMaxs;
		Vector m_vecPlacementMins;
		Vector m_vecPlacementMaxs;
#endif
	};
#if defined( L4D2 )
	struct L4D2EntityRenderData_t
	{
		RenderableInfo_t *m_pInfo;
		C_BaseEntity *m_pAlphaOwner;
		Vector m_vecMins, m_vecMaxs, m_vecCenter;
		float m_flDistanceSquared, m_flRadius, m_flFade;
		ClientRenderHandle_t m_hRenderable;
		int m_nOpaqueWorldLeaf, m_nTranslucentWorldLeaf;
		short m_nArea;
		unsigned char m_nAlpha;
		bool m_bIgnoreZ, m_bCulled;
	};
	// The native2048-row scratch uses the existing SDK fixed+growable allocator:
	// no per-frame heap allocation below that size, no artificial hard cap.
	typedef CUtlVectorFixedGrowable< L4D2EntityRenderData_t, 2048 > L4D2EntityRenderDataList_t;
	void CollectEntityRenderables( const SetupRenderInfo_t &setup, L4D2EntityRenderDataList_t &rows );
	void ComputeEntityWorldBounds( L4D2EntityRenderDataList_t &rows );
	void CullEntityWorldBounds( const SetupRenderInfo_t &setup, L4D2EntityRenderDataList_t &rows );
	void ComputeEntityRenderAlpha( const SetupRenderInfo_t &setup, L4D2EntityRenderDataList_t &rows );
	void ApplyEntityDistanceFade( const SetupRenderInfo_t &setup, L4D2EntityRenderDataList_t &rows );
	void ApplyEntityScreenFade( const RenderAlphaScreenState_t &screen, float minimum,
		float maximum, L4D2EntityRenderDataList_t &rows );
	void CullOccludedEntityRenderables( L4D2EntityRenderDataList_t &rows );
	void AppendEntityRenderable( const SetupRenderInfo_t &setup, int worldLeaf,
		const L4D2EntityRenderData_t &row );
	void CollateRenderablesInLeaf( int leaf, int worldListLeafIndex, const SetupRenderInfo_t &info,
		const L4D2EntityRenderDataList_t &rows );
	void ComputePlacementBounds( RenderableInfo_t &info, Vector &mins, Vector &maxs );
	void ComputeCachedWorldBounds( RenderableInfo_t &info );
	void UpdateSDKRenderGroupForEntity( RenderableInfo_t &info );
#endif

	// The leaf contains an index into a list of renderables
	struct ClientLeaf_t
	{
		unsigned int	m_FirstElement;
		unsigned short	m_FirstShadow;

		unsigned short	m_FirstDetailProp;
		unsigned short	m_DetailPropCount;
		int				m_DetailPropRenderFrame;
		CClientLeafSubSystemData *m_pSubSystemData[N_CLSUBSYSTEMS];

	};

	// Shadow information
	struct ShadowInfo_t
	{
		unsigned short	m_FirstLeaf;
		unsigned short	m_FirstRenderable;
		int				m_EnumCount;
		ClientShadowHandle_t	m_Shadow;
		unsigned short	m_Flags;
	};

	struct EnumResult_t
	{
		int leaf;
		EnumResult_t *pNext;
	};

	struct EnumResultList_t
	{
		EnumResult_t *pHead;
		ClientRenderHandle_t handle;
	};

	// Stores data associated with each leaf.
	CUtlVector< ClientLeaf_t >	m_Leaf;

	// Stores all unique non-detail renderables
	CUtlLinkedList< RenderableInfo_t, ClientRenderHandle_t, false, unsigned int >	m_Renderables;

	// Information associated with shadows registered with the client leaf system
	CUtlLinkedList< ShadowInfo_t, ClientLeafShadowHandle_t, false, unsigned int >	m_Shadows;

	// Maintains the list of all renderables in a particular leaf
	CBidirectionalSet< int, ClientRenderHandle_t, unsigned int, unsigned int >	m_RenderablesInLeaf;

	// Maintains a list of all shadows in a particular leaf 
	CBidirectionalSet< int, ClientLeafShadowHandle_t, unsigned short, unsigned int >	m_ShadowsInLeaf;

	// Maintains a list of all shadows cast on a particular renderable
	CBidirectionalSet< ClientRenderHandle_t, ClientLeafShadowHandle_t, unsigned short, unsigned int >	m_ShadowsOnRenderable;

	// Dirty list of renderables
	CUtlVector< ClientRenderHandle_t >	m_DirtyRenderables;

	// List of renderables in view model render groups
	CUtlVector< ClientRenderHandle_t >	m_ViewModels;

	// Should I draw static props?
	bool m_DrawStaticProps;
	bool m_DrawSmallObjects;

	// A little enumerator to help us when adding shadows to renderables
	int	m_ShadowEnum;

	CTSList<EnumResultList_t> m_DeferredInserts;
};


//-----------------------------------------------------------------------------
// Expose IClientLeafSystem to the client dll.
//-----------------------------------------------------------------------------
CClientLeafSystem CClientLeafSystem::s_ClientLeafSystem;
IClientLeafSystem *g_pClientLeafSystem = &CClientLeafSystem::s_ClientLeafSystem;
EXPOSE_SINGLE_INTERFACE_GLOBALVAR( CClientLeafSystem, IClientLeafSystem, CLIENTLEAFSYSTEM_INTERFACE_VERSION, CClientLeafSystem::s_ClientLeafSystem );

void CalcRenderableWorldSpaceAABB_Fast( IClientRenderable *pRenderable, Vector &absMin, Vector &absMax );

//-----------------------------------------------------------------------------
// Helper functions.
//-----------------------------------------------------------------------------
void DefaultRenderBoundsWorldspace( IClientRenderable *pRenderable, Vector &absMins, Vector &absMaxs )
{
	// Tracker 37433:  This fixes a bug where if the stunstick is being wielded by a combine soldier, the fact that the stick was
	//  attached to the soldier's hand would move it such that it would get frustum culled near the edge of the screen.
	IClientUnknown *pUnk = pRenderable->GetIClientUnknown();
	C_BaseEntity *pEnt = pUnk->GetBaseEntity();
	if ( pEnt && pEnt->IsFollowingEntity() )
	{
		C_BaseEntity *pParent = pEnt->GetFollowedEntity();
		if ( pParent )
		{
			// Get the parent's abs space world bounds.
			CalcRenderableWorldSpaceAABB_Fast( pParent, absMins, absMaxs );

			// Add the maximum of our local render bounds. This is making the assumption that we can be at any
			// point and at any angle within the parent's world space bounds.
			Vector vAddMins, vAddMaxs;
			pEnt->GetRenderBounds( vAddMins, vAddMaxs );
			// if our origin is actually farther away than that, expand again
			float radius = pEnt->GetLocalOrigin().Length();

			float flBloatSize = MAX( vAddMins.Length(), vAddMaxs.Length() );
			flBloatSize = MAX(flBloatSize, radius);
			absMins -= Vector( flBloatSize, flBloatSize, flBloatSize );
			absMaxs += Vector( flBloatSize, flBloatSize, flBloatSize );
			return;
		}
	}

	Vector mins, maxs;
	pRenderable->GetRenderBounds( mins, maxs );

	// FIXME: Should I just use a sphere here?
	// Another option is to pass the OBB down the tree; makes for a better fit
	// Generate a world-aligned AABB
	const QAngle& angles = pRenderable->GetRenderAngles();
	const Vector& origin = pRenderable->GetRenderOrigin();
	if (angles == vec3_angle)
	{
		VectorAdd( mins, origin, absMins );
		VectorAdd( maxs, origin, absMaxs );
	}
	else
	{
		matrix3x4_t	boxToWorld;
		AngleMatrix( angles, origin, boxToWorld );
		TransformAABB( boxToWorld, mins, maxs, absMins, absMaxs );
	}
	Assert( absMins.IsValid() && absMaxs.IsValid() );
}

// Figure out a world space bounding box that encloses the entity's local render bounds in world space.
inline void CalcRenderableWorldSpaceAABB( 
	IClientRenderable *pRenderable, 
	Vector &absMins,
	Vector &absMaxs )
{
	pRenderable->GetRenderBoundsWorldspace( absMins, absMaxs );
}


// This gets an AABB for the renderable, but it doesn't cause a parent's bones to be setup.
// This is used for placement in the leaves, but the more expensive version is used for culling.
#if !defined( L4D2 )
void CalcRenderableWorldSpaceAABB_Fast( IClientRenderable *pRenderable, Vector &absMin, Vector &absMax )
{
	C_BaseEntity *pEnt = pRenderable->GetIClientUnknown()->GetBaseEntity();
	if ( pEnt && pEnt->IsFollowingEntity() )
	{
		C_BaseEntity *pParent = pEnt->GetMoveParent();
		Assert( pParent );

		// Get the parent's abs space world bounds.
		CalcRenderableWorldSpaceAABB_Fast( pParent, absMin, absMax );

		// Add the maximum of our local render bounds. This is making the assumption that we can be at any
		// point and at any angle within the parent's world space bounds.
		Vector vAddMins, vAddMaxs;
		pEnt->GetRenderBounds( vAddMins, vAddMaxs );
		// if our origin is actually farther away than that, expand again
		float radius = pEnt->GetLocalOrigin().Length();

		float flBloatSize = MAX( vAddMins.Length(), vAddMaxs.Length() );
		flBloatSize = MAX(flBloatSize, radius);
		absMin -= Vector( flBloatSize, flBloatSize, flBloatSize );
		absMax += Vector( flBloatSize, flBloatSize, flBloatSize );
	}
	else
	{
		// Start out with our own render bounds. Since we don't have a parent, this won't incur any nasty 
		CalcRenderableWorldSpaceAABB( pRenderable, absMin, absMax );
	}
}
#endif


//-----------------------------------------------------------------------------
// constructor, destructor
//-----------------------------------------------------------------------------
CClientLeafSystem::CClientLeafSystem() : m_DrawStaticProps(true), m_DrawSmallObjects(true)
{
	// Set up the bi-directional lists...
	m_RenderablesInLeaf.Init( FirstRenderableInLeaf, FirstLeafInRenderable );
	m_ShadowsInLeaf.Init( FirstShadowInLeaf, FirstLeafInShadow ); 
	m_ShadowsOnRenderable.Init( FirstShadowOnRenderable, FirstRenderableInShadow );
}

CClientLeafSystem::~CClientLeafSystem()
{
}

//-----------------------------------------------------------------------------
// Activate, deactivate static props
//-----------------------------------------------------------------------------
void CClientLeafSystem::DrawStaticProps( bool enable )
{
	m_DrawStaticProps = enable;
}

void CClientLeafSystem::DrawSmallEntities( bool enable )
{
	m_DrawSmallObjects = enable;
}


//-----------------------------------------------------------------------------
// Level init, shutdown
//-----------------------------------------------------------------------------
void CClientLeafSystem::LevelInitPreEntity()
{
	MEM_ALLOC_CREDIT();

	m_Renderables.EnsureCapacity( 1024 );
	m_RenderablesInLeaf.EnsureCapacity( 1024 );
	m_ShadowsInLeaf.EnsureCapacity( 256 );
	m_ShadowsOnRenderable.EnsureCapacity( 256 );
	m_DirtyRenderables.EnsureCapacity( 256 );

	// Add all the leaves we'll need
	int leafCount = engine->LevelLeafCount();
	m_Leaf.EnsureCapacity( leafCount );

	ClientLeaf_t newLeaf;
	newLeaf.m_FirstElement = m_RenderablesInLeaf.InvalidIndex();
	newLeaf.m_FirstShadow = m_ShadowsInLeaf.InvalidIndex();
	memset( newLeaf.m_pSubSystemData, 0, sizeof( newLeaf.m_pSubSystemData ) );
	newLeaf.m_FirstDetailProp = 0;
	newLeaf.m_DetailPropCount = 0;
	newLeaf.m_DetailPropRenderFrame = -1;
	while ( --leafCount >= 0 )
	{
		m_Leaf.AddToTail( newLeaf );
	}
}

void CClientLeafSystem::LevelShutdownPreEntity()
{
}

void CClientLeafSystem::LevelShutdownPostEntity()
{
	m_ViewModels.Purge();
	m_Renderables.Purge();
	m_RenderablesInLeaf.Purge();
	m_Shadows.Purge();

	// delete subsystem data
	for( int i = 0; i < m_Leaf.Count() ; i++ )
	{
		for( int j = 0 ; j < ARRAYSIZE( m_Leaf[i].m_pSubSystemData ) ; j++ )
		{
			if ( m_Leaf[i].m_pSubSystemData[j] )
			{
				delete m_Leaf[i].m_pSubSystemData[j];
				m_Leaf[i].m_pSubSystemData[j] = NULL;
			}
		}
	}
	m_Leaf.Purge();
	m_ShadowsInLeaf.Purge();
	m_ShadowsOnRenderable.Purge();
	m_DirtyRenderables.Purge();
}


//-----------------------------------------------------------------------------
// This is what happens before rendering a particular view
//-----------------------------------------------------------------------------
#if !defined( L4D2 )
void CClientLeafSystem::PreRender()
{
	VPROF_BUDGET( "CClientLeafSystem::PreRender", "PreRender" );

	int i;
	int nIterations = 0;

	while ( m_DirtyRenderables.Count() )
	{
		if ( ++nIterations > 10 )
		{
			Warning( "Too many dirty renderables!\n" );
			break;
		}

		int nDirty = m_DirtyRenderables.Count();
		for ( i = nDirty; --i >= 0; )
		{
			ClientRenderHandle_t handle = m_DirtyRenderables[i];
			Assert( m_Renderables[ handle ].m_Flags & RENDER_FLAGS_HASCHANGED );

			// Update position in leaf system
			RemoveFromTree( handle );
		}

		bool bThreaded = false;//( nDirty > 5 && cl_threaded_client_leaf_system.GetBool() && g_pThreadPool->NumThreads() );

		if ( !bThreaded )
		{
			for ( i = nDirty; --i >= 0; )
			{
				InsertIntoTree( m_DirtyRenderables[i] );
			}
		}
		else
		{
			// InsertIntoTree can result in new renderables being added, so copy:
			ClientRenderHandle_t *pDirtyRenderables = (ClientRenderHandle_t *)alloca( sizeof(ClientRenderHandle_t) * nDirty );
			memcpy( pDirtyRenderables, m_DirtyRenderables.Base(), sizeof(ClientRenderHandle_t) * nDirty );
			ParallelProcess( "CClientLeafSystem::PreRender", pDirtyRenderables, nDirty, this, &CClientLeafSystem::InsertIntoTree, &CClientLeafSystem::FrameLock, &CClientLeafSystem::FrameUnlock );
		}

		if ( m_DeferredInserts.Count() )
		{
			EnumResultList_t enumResultList;
			while ( m_DeferredInserts.PopItem( &enumResultList ) )
			{
				m_ShadowEnum++;
				while ( enumResultList.pHead )
				{
					EnumResult_t *p = enumResultList.pHead;
					enumResultList.pHead = p->pNext;
					AddRenderableToLeaf( p->leaf, enumResultList.handle );
					delete p;
				}
			}
		}

		for ( i = nDirty; --i >= 0; )
		{
			// Cache off the area it's sitting in.
			ClientRenderHandle_t handle = m_DirtyRenderables[i];
			RenderableInfo_t& renderable = m_Renderables[ handle ];

			renderable.m_Flags &= ~RENDER_FLAGS_HASCHANGED;
			m_Renderables[handle].m_Area = GetRenderableArea( handle );
		}

		m_DirtyRenderables.RemoveMultiple( 0, nDirty );
	}
}
#endif


//-----------------------------------------------------------------------------
// Creates a new renderable
//-----------------------------------------------------------------------------
void CClientLeafSystem::NewRenderable( IClientRenderable* pRenderable, RenderGroup_t type, int flags )
{
	Assert( pRenderable );
	Assert( pRenderable->RenderHandle() == INVALID_CLIENT_RENDER_HANDLE );

	ClientRenderHandle_t handle = m_Renderables.AddToTail();
	RenderableInfo_t &info = m_Renderables[handle];

	// We need to know if it's a brush model for shadows
	int modelType = modelinfo->GetModelType( pRenderable->GetModel() );
	if (modelType == mod_brush)
	{
		flags |= RENDER_FLAGS_BRUSH_MODEL;
	}
	else if ( modelType == mod_studio )
	{
		flags |= RENDER_FLAGS_STUDIO_MODEL;
	}

	info.m_pRenderable = pRenderable;
	info.m_RenderFrame = -1;
	info.m_RenderFrame2 = -1;
	info.m_TranslucencyCalculated = -1;
	info.m_TranslucencyCalculatedView = VIEW_ILLEGAL;
	info.m_FirstShadow = m_ShadowsOnRenderable.InvalidIndex();
	info.m_LeafList = m_RenderablesInLeaf.InvalidIndex();
	info.m_Flags = flags;
	info.m_RenderGroup = (unsigned char)type;
	info.m_EnumCount = 0;
	info.m_RenderLeaf = m_RenderablesInLeaf.InvalidIndex();
#if defined( L4D2 )
	info.m_pL4D2AlphaOwner = pRenderable->GetIClientUnknown()->GetBaseEntity();
	info.m_nL4D2WorldDataIndex = -1;
	// Native metadata is assigned by the ONE typed root registration/model
	// hook, not recomputed by this allocator or derived from current alpha.
	info.m_nL4D2TranslucencyType = 0;
	info.m_bL4D2DrawForLocalUser = true;
	info.m_bL4D2RenderingEnabled = true;
	info.m_bL4D2ViewModel = IsViewModelRenderGroup( (RenderGroup_t)info.m_RenderGroup );
	info.m_nL4D2BoundsFlags = 0;
	info.m_vecCachedWorldMins.Init();
	info.m_vecCachedWorldMaxs.Init();
	info.m_vecPlacementMins.Init( FLT_MAX, FLT_MAX, FLT_MAX );
	info.m_vecPlacementMaxs.Init( -FLT_MAX, -FLT_MAX, -FLT_MAX );
#endif
	if ( IsViewModelRenderGroup( (RenderGroup_t)info.m_RenderGroup ) )
	{
		AddToViewModelList( handle );
	}

	pRenderable->RenderHandle() = handle;
}

void CClientLeafSystem::CreateRenderableHandle( IClientRenderable* pRenderable, bool bIsStaticProp )
{
	// FIXME: The argument is unnecessary if we could get this next line to work
	// the reason why we can't is because currently there are IClientRenderables
	// which don't correctly implement GetRefEHandle.

	//bool bIsStaticProp = staticpropmgr->IsStaticProp( pRenderable->GetIClientUnknown() );

	// Add the prop to all the leaves it lies in
	RenderGroup_t group = pRenderable->IsTransparent() ? RENDER_GROUP_TRANSLUCENT_ENTITY : RENDER_GROUP_OPAQUE_ENTITY;

	bool bTwoPass = false;
	if ( group == RENDER_GROUP_TRANSLUCENT_ENTITY )
	{
		bTwoPass = pRenderable->IsTwoPass( );
	}

	int flags = 0;
	if ( bIsStaticProp )
	{
		flags = RENDER_FLAGS_STATIC_PROP;
		if ( group == RENDER_GROUP_OPAQUE_ENTITY )
		{
			group = RENDER_GROUP_OPAQUE_STATIC;
		}
	}

	if (bTwoPass)
	{
		flags |= RENDER_FLAGS_TWOPASS;
	}

	NewRenderable( pRenderable, group, flags );
}


//-----------------------------------------------------------------------------
// Used to change renderables from translucent to opaque
//-----------------------------------------------------------------------------
void CClientLeafSystem::ChangeRenderableRenderGroup( ClientRenderHandle_t handle, RenderGroup_t group )
{
	RenderableInfo_t &info = m_Renderables[handle];
	info.m_RenderGroup = (unsigned char)group;
}


//-----------------------------------------------------------------------------
// Use alternate translucent sorting algorithm (draw translucent objects in the furthest leaf they lie in)
//-----------------------------------------------------------------------------
void CClientLeafSystem::EnableAlternateSorting( ClientRenderHandle_t handle, bool bEnable )
{
	RenderableInfo_t &info = m_Renderables[handle];
	if ( bEnable )
	{
		info.m_Flags |= RENDER_FLAGS_ALTERNATE_SORTING;
	}
	else
	{
		info.m_Flags &= ~RENDER_FLAGS_ALTERNATE_SORTING; 
	}
}


//-----------------------------------------------------------------------------
// Add/remove renderable
//-----------------------------------------------------------------------------
void CClientLeafSystem::AddRenderable( IClientRenderable* pRenderable, RenderGroup_t group )
{
	// force a relink we we try to draw it for the first time
	int flags = RENDER_FLAGS_HASCHANGED;

	if ( group == RENDER_GROUP_TWOPASS )
	{
		group = RENDER_GROUP_TRANSLUCENT_ENTITY;
		flags |= RENDER_FLAGS_TWOPASS;
	}

	NewRenderable( pRenderable, group, flags );
	ClientRenderHandle_t handle = pRenderable->RenderHandle();
	m_DirtyRenderables.AddToTail( handle );
}

void CClientLeafSystem::RemoveRenderable( ClientRenderHandle_t handle )
{
	// This can happen upon level shutdown
	if (!m_Renderables.IsValidIndex(handle))
		return;

	// Reset the render handle in the entity.
	IClientRenderable *pRenderable = m_Renderables[handle].m_pRenderable;
	Assert( handle == pRenderable->RenderHandle() );
	pRenderable->RenderHandle() = INVALID_CLIENT_RENDER_HANDLE;

	// Reemove the renderable from the dirty list
	if ( m_Renderables[handle].m_Flags & RENDER_FLAGS_HASCHANGED )
	{
		// NOTE: This isn't particularly fast (linear search),
		// but I'm assuming it's an unusual case where we remove 
		// renderables that are changing or that m_DirtyRenderables usually
		// only has a couple entries
		int i = m_DirtyRenderables.Find( handle );
		Assert( i != m_DirtyRenderables.InvalidIndex() );
		m_DirtyRenderables.FastRemove( i ); 
	}

	if ( IsViewModelRenderGroup( (RenderGroup_t)m_Renderables[handle].m_RenderGroup ) )
	{
		RemoveFromViewModelList( handle );
	}

	RemoveFromTree( handle );
	m_Renderables.Remove( handle );
}


int CClientLeafSystem::GetRenderableLeaves( ClientRenderHandle_t handle, int leaves[128] )
{
	if ( !m_Renderables.IsValidIndex( handle ) )
		return -1;

	RenderableInfo_t *pRenderable = &m_Renderables[handle];
	if ( pRenderable->m_LeafList == m_RenderablesInLeaf.InvalidIndex() )
		return -1;

	int nLeaves = 0;
	for ( int i=m_RenderablesInLeaf.FirstBucket( handle ); i != m_RenderablesInLeaf.InvalidIndex(); i = m_RenderablesInLeaf.NextBucket( i ) )
	{
		leaves[nLeaves++] = m_RenderablesInLeaf.Bucket( i );
		if ( nLeaves >= 128 )
			break;
	}
	return nLeaves;
}


//-----------------------------------------------------------------------------
// Retrieve leaf handles to leaves a renderable is in
// the pOutLeaf parameter is filled with the leaf the renderable is in.
// If pInIterator is not specified, pOutLeaf is the first leaf in the list.
// if pInIterator is specified, that iterator is used to return the next leaf
// in the list in pOutLeaf.
// the pOutIterator parameter is filled with the iterater which index to the pOutLeaf returned.
//
// Returns false on failure cases where pOutLeaf will be invalid. CHECK THE RETURN!
//-----------------------------------------------------------------------------
bool CClientLeafSystem::GetRenderableLeaf(ClientRenderHandle_t handle, int* pOutLeaf, const int* pInIterator /* = 0 */, int* pOutIterator /* = 0  */)
{
	// bail on invalid handle
	if ( !m_Renderables.IsValidIndex( handle ) )
		return false;

	// bail on no output value pointer
	if ( !pOutLeaf )
		return false;

	// an iterator was specified
	if ( pInIterator )
	{
		int iter = *pInIterator;

		// test for invalid iterator
		if ( iter == m_RenderablesInLeaf.InvalidIndex() )
			return false;

		int iterNext =  m_RenderablesInLeaf.NextBucket( iter );

		// test for end of list
		if ( iterNext == m_RenderablesInLeaf.InvalidIndex() )
			return false;

		// Give the caller the iterator used
		if ( pOutIterator )
		{
			*pOutIterator = iterNext;
		}
		
		// set output value to the next leaf
		*pOutLeaf = m_RenderablesInLeaf.Bucket( iterNext );

	}
	else // no iter param, give them the first bucket in the renderable's list
	{
		int iter = m_RenderablesInLeaf.FirstBucket( handle );

		if ( iter == m_RenderablesInLeaf.InvalidIndex() )
			return false;

		// Set output value to this leaf
		*pOutLeaf = m_RenderablesInLeaf.Bucket( iter );

		// give this iterator to caller
		if ( pOutIterator )
		{
			*pOutIterator = iter;
		}
		
	}
	
	return true;
}

bool CClientLeafSystem::IsRenderableInPVS( IClientRenderable *pRenderable )
{
	ClientRenderHandle_t handle = pRenderable->RenderHandle();
	int leaves[128];
	int nLeaves = GetRenderableLeaves( handle, leaves );
	if ( nLeaves == -1 )
		return false;

	// Ask the engine if this guy is visible.
	return render->AreAnyLeavesVisible( leaves, nLeaves );
}

short CClientLeafSystem::GetRenderableArea( ClientRenderHandle_t handle )
{
	int leaves[128];
	int nLeaves = GetRenderableLeaves( handle, leaves );
	if ( nLeaves == -1 )
		return 0;

	// Now ask the 
	return engine->GetLeavesArea( leaves, nLeaves );
}


void CClientLeafSystem::SetSubSystemDataInLeaf( int leaf, int nSubSystemIdx, CClientLeafSubSystemData *pData )
{
	assert( nSubSystemIdx < N_CLSUBSYSTEMS );
	if ( !m_Leaf.IsValidIndex( leaf ) )
	{
		Assert( false );
		return;
	}
	if ( m_Leaf[leaf].m_pSubSystemData[nSubSystemIdx] )
		delete m_Leaf[leaf].m_pSubSystemData[nSubSystemIdx];
	m_Leaf[leaf].m_pSubSystemData[nSubSystemIdx] = pData;
}

CClientLeafSubSystemData *CClientLeafSystem::GetSubSystemDataInLeaf( int leaf, int nSubSystemIdx )
{
	assert( nSubSystemIdx < N_CLSUBSYSTEMS );
	if ( !m_Leaf.IsValidIndex( leaf ) )
	{
		Assert( false );
		return NULL;
	}
	return m_Leaf[leaf].m_pSubSystemData[nSubSystemIdx];
}

//-----------------------------------------------------------------------------
// Indicates which leaves detail objects are in
//-----------------------------------------------------------------------------
void CClientLeafSystem::SetDetailObjectsInLeaf( int leaf, int firstDetailObject,
											    int detailObjectCount )
{
	if ( !m_Leaf.IsValidIndex( leaf ) )
	{
		Assert( false );
		return;
	}
	m_Leaf[leaf].m_FirstDetailProp = firstDetailObject;
	m_Leaf[leaf].m_DetailPropCount = detailObjectCount;
}

//-----------------------------------------------------------------------------
// Returns the detail objects in a leaf
//-----------------------------------------------------------------------------
void CClientLeafSystem::GetDetailObjectsInLeaf( int leaf, int& firstDetailObject,
											    int& detailObjectCount )
{
	if ( !m_Leaf.IsValidIndex( leaf ) )
	{
		Assert( false );
		return;
	}
	firstDetailObject = m_Leaf[leaf].m_FirstDetailProp;
	detailObjectCount = m_Leaf[leaf].m_DetailPropCount;
}


//-----------------------------------------------------------------------------
// Create/destroy shadows...
//-----------------------------------------------------------------------------
ClientLeafShadowHandle_t CClientLeafSystem::AddShadow( ClientShadowHandle_t userId, unsigned short flags )
{
	ClientLeafShadowHandle_t idx = m_Shadows.AddToTail();
	m_Shadows[idx].m_Shadow = userId;
	m_Shadows[idx].m_FirstLeaf = m_ShadowsInLeaf.InvalidIndex();
	m_Shadows[idx].m_FirstRenderable = m_ShadowsOnRenderable.InvalidIndex();
	m_Shadows[idx].m_EnumCount = 0;
	m_Shadows[idx].m_Flags = flags;
	return idx;
}

void CClientLeafSystem::RemoveShadow( ClientLeafShadowHandle_t handle )
{
	// Remove the shadow from all leaves + renderables...
	RemoveShadowFromLeaves( handle );
	RemoveShadowFromRenderables( handle );

	// Blow away the handle
	m_Shadows.Remove( handle );
}


//-----------------------------------------------------------------------------
// Adds a shadow to a leaf/removes shadow from renderable
//-----------------------------------------------------------------------------
inline bool CClientLeafSystem::ShouldRenderableReceiveShadow( ClientRenderHandle_t renderHandle, int nShadowFlags )
{
	RenderableInfo_t &renderable = m_Renderables[renderHandle];
	if( !( renderable.m_Flags & ( RENDER_FLAGS_BRUSH_MODEL | RENDER_FLAGS_STATIC_PROP | RENDER_FLAGS_STUDIO_MODEL ) ) )
		return false;

	return renderable.m_pRenderable->ShouldReceiveProjectedTextures( nShadowFlags );
}


//-----------------------------------------------------------------------------
// Adds a shadow to a leaf/removes shadow from renderable
//-----------------------------------------------------------------------------
void CClientLeafSystem::AddShadowToRenderable( ClientRenderHandle_t renderHandle, 
										ClientLeafShadowHandle_t shadowHandle )
{
	// Check if this renderable receives the type of projected texture that shadowHandle refers to.
	int nShadowFlags = m_Shadows[shadowHandle].m_Flags;
	if ( !ShouldRenderableReceiveShadow( renderHandle, nShadowFlags ) )
		return;

	m_ShadowsOnRenderable.AddElementToBucket( renderHandle, shadowHandle );

	// Also, do some stuff specific to the particular types of renderables

	// If the renderable is a brush model, then add this shadow to it
	if (m_Renderables[renderHandle].m_Flags & RENDER_FLAGS_BRUSH_MODEL)
	{
		IClientRenderable* pRenderable = m_Renderables[renderHandle].m_pRenderable;
		g_pClientShadowMgr->AddShadowToReceiver( m_Shadows[shadowHandle].m_Shadow,
			pRenderable, SHADOW_RECEIVER_BRUSH_MODEL );
	}
	else if( m_Renderables[renderHandle].m_Flags & RENDER_FLAGS_STATIC_PROP )
	{
		IClientRenderable* pRenderable = m_Renderables[renderHandle].m_pRenderable;
		g_pClientShadowMgr->AddShadowToReceiver( m_Shadows[shadowHandle].m_Shadow,
			pRenderable, SHADOW_RECEIVER_STATIC_PROP );
	}
	else if( m_Renderables[renderHandle].m_Flags & RENDER_FLAGS_STUDIO_MODEL )
	{
		IClientRenderable* pRenderable = m_Renderables[renderHandle].m_pRenderable;
		g_pClientShadowMgr->AddShadowToReceiver( m_Shadows[shadowHandle].m_Shadow,
			pRenderable, SHADOW_RECEIVER_STUDIO_MODEL );
	}
}

void CClientLeafSystem::RemoveShadowFromRenderables( ClientLeafShadowHandle_t handle )
{
	m_ShadowsOnRenderable.RemoveElement( handle );
}


//-----------------------------------------------------------------------------
// Adds a shadow to a leaf/removes shadow from leaf
//-----------------------------------------------------------------------------
void CClientLeafSystem::AddShadowToLeaf( int leaf, ClientLeafShadowHandle_t shadow )
{
	m_ShadowsInLeaf.AddElementToBucket( leaf, shadow ); 

	// Add the shadow exactly once to all renderables in the leaf
	unsigned int i = m_RenderablesInLeaf.FirstElement( leaf );
	while ( i != m_RenderablesInLeaf.InvalidIndex() )
	{
		ClientRenderHandle_t renderable = m_RenderablesInLeaf.Element(i);
		RenderableInfo_t& info = m_Renderables[renderable];

		// Add each shadow exactly once to each renderable
		if (info.m_EnumCount != m_ShadowEnum)
		{
			AddShadowToRenderable( renderable, shadow );
			info.m_EnumCount = m_ShadowEnum;
		}

		Assert( m_ShadowsInLeaf.NumAllocated() < 2000 );

		i = m_RenderablesInLeaf.NextElement(i);
	}
}

void CClientLeafSystem::RemoveShadowFromLeaves( ClientLeafShadowHandle_t handle )
{
	m_ShadowsInLeaf.RemoveElement( handle );
}


//-----------------------------------------------------------------------------
// Adds a shadow to all leaves listed
//-----------------------------------------------------------------------------
void CClientLeafSystem::ProjectShadow( ClientLeafShadowHandle_t handle, int nLeafCount, const int *pLeafList )
{
	// Remove the shadow from any leaves it current exists in
	RemoveShadowFromLeaves( handle );
	RemoveShadowFromRenderables( handle );

	Assert( ( m_Shadows[handle].m_Flags & SHADOW_FLAGS_PROJECTED_TEXTURE_TYPE_MASK ) == SHADOW_FLAGS_SHADOW );

	// This will help us to avoid adding the shadow multiple times to a renderable
	++m_ShadowEnum;

	for ( int i = 0; i < nLeafCount; ++i )
	{
		AddShadowToLeaf( pLeafList[i], handle );
	}
}

void CClientLeafSystem::ProjectFlashlight( ClientLeafShadowHandle_t handle, int nLeafCount, const int *pLeafList )
{
	VPROF_BUDGET( "CClientLeafSystem::ProjectFlashlight", VPROF_BUDGETGROUP_SHADOW_DEPTH_TEXTURING );

	// Remove the shadow from any leaves it current exists in
	RemoveShadowFromLeaves( handle );
	RemoveShadowFromRenderables( handle );

	Assert( ( m_Shadows[handle].m_Flags & SHADOW_FLAGS_PROJECTED_TEXTURE_TYPE_MASK ) == SHADOW_FLAGS_FLASHLIGHT );
	
	// This will help us to avoid adding the shadow multiple times to a renderable
	++m_ShadowEnum;

	for ( int i = 0; i < nLeafCount; ++i )
	{
		AddShadowToLeaf( pLeafList[i], handle );
	}
}


//-----------------------------------------------------------------------------
// Find all shadow casters in a set of leaves
//-----------------------------------------------------------------------------
void CClientLeafSystem::EnumerateShadowsInLeaves( int leafCount, LeafIndex_t* pLeaves, IClientLeafShadowEnum* pEnum )
{
	if (leafCount == 0)
		return;

	// This will help us to avoid enumerating the shadow multiple times
	++m_ShadowEnum;

	for (int i = 0; i < leafCount; ++i)
	{
		int leaf = pLeaves[i];

		unsigned short j = m_ShadowsInLeaf.FirstElement( leaf );
		while ( j != m_ShadowsInLeaf.InvalidIndex() )
		{
			ClientLeafShadowHandle_t shadow = m_ShadowsInLeaf.Element(j);
			ShadowInfo_t& info = m_Shadows[shadow];

			if (info.m_EnumCount != m_ShadowEnum)
			{
				pEnum->EnumShadow(info.m_Shadow);
				info.m_EnumCount = m_ShadowEnum;
			}

			j = m_ShadowsInLeaf.NextElement(j);
		}
	}
}


//-----------------------------------------------------------------------------
// Adds a renderable to a leaf
//-----------------------------------------------------------------------------
void CClientLeafSystem::AddRenderableToLeaf( int leaf, ClientRenderHandle_t renderable )
{
#ifdef VALIDATE_CLIENT_LEAF_SYSTEM
	m_RenderablesInLeaf.ValidateAddElementToBucket( leaf, renderable );
#endif

#ifdef DUMP_RENDERABLE_LEAFS
	static uint32 count = 0;
	if (count < m_RenderablesInLeaf.NumAllocated())
	{
		count = m_RenderablesInLeaf.NumAllocated();
		Msg("********** frame: %d count:%u ***************\n", gpGlobals->framecount, count );

		if (count >= 20000)
		{
			for (int j = 0; j < m_RenderablesInLeaf.NumAllocated(); j++)
			{
				const ClientRenderHandle_t& renderable = m_RenderablesInLeaf.Element(j);
				RenderableInfo_t& info = m_Renderables[renderable];

				char pTemp[256];
				const char *pClassName = "<unknown renderable>";
				C_BaseEntity *pEnt = info.m_pRenderable->GetIClientUnknown()->GetBaseEntity();
				if ( pEnt )
				{
					pClassName = pEnt->GetClassname();
				}
				else
				{
					CNewParticleEffect *pEffect = dynamic_cast< CNewParticleEffect*>( info.m_pRenderable );
					if ( pEffect )
					{
						Vector mins, maxs;
						pEffect->GetRenderBounds(mins, maxs);
						Q_snprintf( pTemp, sizeof(pTemp), "ps: %s %.2f,%.2f", pEffect->GetEffectName(), maxs.x - mins.x, maxs.y - mins.y );
						pClassName = pTemp;
					}
					else if ( dynamic_cast< CParticleEffectBinding* >( info.m_pRenderable ) )
					{
						pClassName = "<old particle system>";
					}
				}

				Msg(" %d: %p group:%d %s %d %d TransCalc:%d renderframe:%d\n", j, info.m_pRenderable, info.m_RenderGroup, pClassName,
					info.m_LeafList, info.m_RenderLeaf, info.m_TranslucencyCalculated, info.m_RenderFrame);
			}

			DebuggerBreak();
		}
	}
#endif // DUMP_RENDERABLE_LEAFS

	m_RenderablesInLeaf.AddElementToBucket(leaf, renderable);

	if ( !ShouldRenderableReceiveShadow( renderable, SHADOW_FLAGS_PROJECTED_TEXTURE_TYPE_MASK ) )
		return;

	// Add all shadows in the leaf to the renderable...
	unsigned short i = m_ShadowsInLeaf.FirstElement( leaf );
	while (i != m_ShadowsInLeaf.InvalidIndex() )
	{
		ClientLeafShadowHandle_t shadow = m_ShadowsInLeaf.Element(i);
		ShadowInfo_t& info = m_Shadows[shadow];

		// Add each shadow exactly once to each renderable
		if (info.m_EnumCount != m_ShadowEnum)
		{
			AddShadowToRenderable( renderable, shadow );
			info.m_EnumCount = m_ShadowEnum;
		}

		i = m_ShadowsInLeaf.NextElement(i);
	}
}


//-----------------------------------------------------------------------------
// Adds a renderable to a set of leaves
//-----------------------------------------------------------------------------
void CClientLeafSystem::AddRenderableToLeaves( ClientRenderHandle_t handle, int nLeafCount, unsigned short *pLeaves )
{ 
	for (int j = 0; j < nLeafCount; ++j)
	{
		AddRenderableToLeaf( pLeaves[j], handle ); 
	}
	m_Renderables[handle].m_Area = GetRenderableArea( handle );
}


//-----------------------------------------------------------------------------
// Inserts an element into the tree
//-----------------------------------------------------------------------------
bool CClientLeafSystem::EnumerateLeaf( int leaf, intp context )
{
	EnumResultList_t *pList = (EnumResultList_t *)context;
	if ( ThreadInMainThread() )
	{
		AddRenderableToLeaf( leaf, pList->handle );
	}
	else
	{
		EnumResult_t *p = new EnumResult_t;
		p->leaf = leaf;
		p->pNext = pList->pHead;
		pList->pHead = p;
	}
	return true;
}

#if defined( L4D2 )
void CClientLeafSystem::InsertIntoTree( ClientRenderHandle_t handle, const Vector &absMins, const Vector &absMaxs )
{
	// Native C1C40 commits the placement box before enumerating its BSP leaves.
	m_Renderables[handle].m_vecPlacementMins = absMins;
	m_Renderables[handle].m_vecPlacementMaxs = absMaxs;
	++m_ShadowEnum;

	EnumResultList_t list = { NULL, handle };
	Assert( absMins.IsValid() && absMaxs.IsValid() );
	ISpatialQuery *pQuery = engine->GetBSPTreeQuery();
	pQuery->EnumerateLeavesInBox( absMins, absMaxs, this, (intp)&list );
	m_Renderables[handle].m_Area = GetRenderableArea( handle );
}
#else
void CClientLeafSystem::InsertIntoTree( ClientRenderHandle_t &handle )
{
	if ( ThreadInMainThread() )
	{
		// When we insert into the tree, increase the shadow enumerator
		// to make sure each shadow is added exactly once to each renderable
		m_ShadowEnum++;
	}

	EnumResultList_t list = { NULL, handle };

	// NOTE: The render bounds here are relative to the renderable's coordinate system
	IClientRenderable* pRenderable = m_Renderables[handle].m_pRenderable;
	Vector absMins, absMaxs;
	
	CalcRenderableWorldSpaceAABB_Fast( pRenderable, absMins, absMaxs );
	Assert( absMins.IsValid() && absMaxs.IsValid() );

	ISpatialQuery* pQuery = engine->GetBSPTreeQuery();
	pQuery->EnumerateLeavesInBox( absMins, absMaxs, this, (intp)&list );

	if ( list.pHead )
	{
		m_DeferredInserts.PushItem( list );
	}
}
#endif

//-----------------------------------------------------------------------------
// Removes an element from the tree
//-----------------------------------------------------------------------------
void CClientLeafSystem::RemoveFromTree( ClientRenderHandle_t handle )
{
	m_RenderablesInLeaf.RemoveElement( handle );

	// Remove all shadows cast onto the object
	m_ShadowsOnRenderable.RemoveBucket( handle );

	// If the renderable is a brush model, then remove all shadows from it
	if (m_Renderables[handle].m_Flags & RENDER_FLAGS_BRUSH_MODEL)
	{
		g_pClientShadowMgr->RemoveAllShadowsFromReceiver( 
			m_Renderables[handle].m_pRenderable, SHADOW_RECEIVER_BRUSH_MODEL );
	}
	else if( m_Renderables[handle].m_Flags & RENDER_FLAGS_STUDIO_MODEL )
	{
		g_pClientShadowMgr->RemoveAllShadowsFromReceiver( 
			m_Renderables[handle].m_pRenderable, SHADOW_RECEIVER_STUDIO_MODEL );
	}
}


//-----------------------------------------------------------------------------
// Call this when the renderable moves
//-----------------------------------------------------------------------------
#if !defined( L4D2 )
void CClientLeafSystem::RenderableChanged( ClientRenderHandle_t handle )
{
	Assert ( handle != INVALID_CLIENT_RENDER_HANDLE );
	Assert( m_Renderables.IsValidIndex( handle ) );
	if ( !m_Renderables.IsValidIndex( handle ) )
		return;

	if ( (m_Renderables[handle].m_Flags & RENDER_FLAGS_HASCHANGED ) == 0 )
	{
		m_Renderables[handle].m_Flags |= RENDER_FLAGS_HASCHANGED;
		m_DirtyRenderables.AddToTail( handle );
	}
#if _DEBUG
	else
	{
		// It had better be in the list
		Assert( m_DirtyRenderables.Find( handle ) != m_DirtyRenderables.InvalidIndex() );
	}
#endif
}
#endif


//-----------------------------------------------------------------------------
// Returns if it's a view model render group
//-----------------------------------------------------------------------------
inline bool CClientLeafSystem::IsViewModelRenderGroup( RenderGroup_t group ) const
{
	return (group == RENDER_GROUP_VIEW_MODEL_TRANSLUCENT) || (group == RENDER_GROUP_VIEW_MODEL_OPAQUE);
}


//-----------------------------------------------------------------------------
// Adds, removes renderables from view model list
//-----------------------------------------------------------------------------
void CClientLeafSystem::AddToViewModelList( ClientRenderHandle_t handle )
{
	MEM_ALLOC_CREDIT();
	Assert( m_ViewModels.Find( handle ) == m_ViewModels.InvalidIndex() );
	m_ViewModels.AddToTail( handle );
}

void CClientLeafSystem::RemoveFromViewModelList( ClientRenderHandle_t handle )
{
	int i = m_ViewModels.Find( handle );
	Assert( i != m_ViewModels.InvalidIndex() );
	m_ViewModels.FastRemove( i );
}


//-----------------------------------------------------------------------------
// Call this to change the render group
//-----------------------------------------------------------------------------
void CClientLeafSystem::SetRenderGroup( ClientRenderHandle_t handle, RenderGroup_t group )
{
	RenderableInfo_t *pInfo = &m_Renderables[handle];

	bool twoPass = false;
	if ( group == RENDER_GROUP_TWOPASS )
	{
		twoPass = true;
		group = RENDER_GROUP_TRANSLUCENT_ENTITY;
	}

	if ( twoPass )
	{
		pInfo->m_Flags |= RENDER_FLAGS_TWOPASS;
	}
	else
	{
		pInfo->m_Flags &= ~RENDER_FLAGS_TWOPASS;
	}

	bool bOldViewModelRenderGroup = IsViewModelRenderGroup( (RenderGroup_t)pInfo->m_RenderGroup );
	bool bNewViewModelRenderGroup = IsViewModelRenderGroup( group );
	if ( bOldViewModelRenderGroup != bNewViewModelRenderGroup )
	{
		if ( bOldViewModelRenderGroup )
		{
			RemoveFromViewModelList( handle );
		}
		else 
		{
			AddToViewModelList( handle );
		}
	}

	pInfo->m_RenderGroup = group;
#if defined( L4D2 )
	// Retained engine/nonentity SDK metadata interop, not entity native0/1/2.
	// AddEffect's genuine existing particle VM-translucent bucket remains.
	pInfo->m_bL4D2ViewModel = bNewViewModelRenderGroup;
#endif

}


//-----------------------------------------------------------------------------
// Detail system marks 
//-----------------------------------------------------------------------------
void CClientLeafSystem::DrawDetailObjectsInLeaf( int leaf, int nFrameNumber, int& nFirstDetailObject, int& nDetailObjectCount )
{
	ClientLeaf_t &leafInfo = m_Leaf[leaf];
	leafInfo.m_DetailPropRenderFrame = nFrameNumber;
	nFirstDetailObject = leafInfo.m_FirstDetailProp;
	nDetailObjectCount = leafInfo.m_DetailPropCount;
}


//-----------------------------------------------------------------------------
// Are we close enough to this leaf to draw detail props *and* are there any props in the leaf?
//-----------------------------------------------------------------------------
bool CClientLeafSystem::ShouldDrawDetailObjectsInLeaf( int leaf, int frameNumber )
{
	ClientLeaf_t &leafInfo = m_Leaf[leaf];
	return ( (leafInfo.m_DetailPropRenderFrame == frameNumber ) &&
			 ( ( leafInfo.m_DetailPropCount != 0 ) || ( leafInfo.m_pSubSystemData[CLSUBSYSTEM_DETAILOBJECTS] ) ) );
}


//-----------------------------------------------------------------------------
// Compute which leaf the translucent renderables should render in
//-----------------------------------------------------------------------------
void CClientLeafSystem::ComputeTranslucentRenderLeaf( int count, const LeafIndex_t *pLeafList, const LeafFogVolume_t *pLeafFogVolumeList, int frameNumber, int viewID )
{
	ASSERT_NO_REENTRY();
	VPROF_BUDGET( "CClientLeafSystem::ComputeTranslucentRenderLeaf", "ComputeTranslucentRenderLeaf"  );

	#define LeafToMarker( leaf ) reinterpret_cast<RenderableInfo_t *>(( (intp)(leaf) << 1 ) | 1)
	#define IsLeafMarker( p ) (bool)((reinterpret_cast<size_t>(p)) & 1)
	#define MarkerToLeaf( p ) (int)((reinterpret_cast<size_t>(p)) >> 1)

	// For better sorting, we're gonna choose the leaf that is closest to the camera.
	// The leaf list passed in here is sorted front to back
	bool bThreaded = false;//( cl_threaded_client_leaf_system.GetBool() && g_pThreadPool->NumThreads() );
	int globalFrameCount = gpGlobals->framecount;
	int i;

	static CUtlVector<RenderableInfo_t *> orderedList; // @MULTICORE (toml 8/30/2006): will need to make non-static if thread this function
	static CUtlVector<IClientRenderable *> renderablesToUpdate;
	int leaf = 0;
	for ( i = 0; i < count; ++i )
	{
		leaf = pLeafList[i];
		orderedList.AddToTail( LeafToMarker( leaf ) );

		// iterate over all elements in this leaf
		unsigned int idx = m_RenderablesInLeaf.FirstElement(leaf);
		while (idx != m_RenderablesInLeaf.InvalidIndex())
		{
			RenderableInfo_t& info = m_Renderables[m_RenderablesInLeaf.Element(idx)];
#if defined( L4D2 )
			if ( !info.m_bL4D2RenderingEnabled || !info.m_bL4D2DrawForLocalUser )
			{
				idx = m_RenderablesInLeaf.NextElement(idx);
				continue;
			}
#endif
			if (
#if defined( L4D2 )
				!info.m_pL4D2AlphaOwner &&
#endif
				( info.m_TranslucencyCalculated != globalFrameCount || info.m_TranslucencyCalculatedView != viewID ) )
			{ 
				// Compute translucency
				if ( bThreaded )
				{
					renderablesToUpdate.AddToTail( info.m_pRenderable );
				}
				else
				{
					info.m_pRenderable->ComputeFxBlend();
				}
				info.m_TranslucencyCalculated = globalFrameCount;
				info.m_TranslucencyCalculatedView = viewID;
			}
			orderedList.AddToTail( &info );
			idx = m_RenderablesInLeaf.NextElement(idx); 
		}
	}

	if ( bThreaded )
	{
		ParallelProcess( "CClientLeafSystem::ComputeTranslucentRenderLeaf", renderablesToUpdate.Base(), renderablesToUpdate.Count(), &CallComputeFXBlend, &::FrameLock, &::FrameUnlock );
		renderablesToUpdate.RemoveAll();
	}

	for ( i = 0; i != orderedList.Count(); i++ )
	{
		RenderableInfo_t *pInfo = orderedList[i];
		if ( !IsLeafMarker( pInfo ) )
		{
			if( pInfo->m_RenderFrame != frameNumber )
			{   
				if (
#if defined( L4D2 )
					pInfo->m_pL4D2AlphaOwner ||
#endif
					pInfo->m_RenderGroup == RENDER_GROUP_TRANSLUCENT_ENTITY )
				{
					pInfo->m_RenderLeaf = leaf;
				}
				pInfo->m_RenderFrame = frameNumber;
			}
			else if ( pInfo->m_Flags & RENDER_FLAGS_ALTERNATE_SORTING )
			{
				if (
#if defined( L4D2 )
					pInfo->m_pL4D2AlphaOwner ||
#endif
					pInfo->m_RenderGroup == RENDER_GROUP_TRANSLUCENT_ENTITY )
				{
					pInfo->m_RenderLeaf = leaf;
				}
			}

		}
		else
		{
			leaf = MarkerToLeaf( pInfo );
		}
	}

	orderedList.RemoveAll();
}


//-----------------------------------------------------------------------------
// Adds a renderable to the list of renderables to render this frame
//-----------------------------------------------------------------------------
inline void AddRenderableToRenderList( CClientRenderablesList &renderList, IClientRenderable *pRenderable,
	int iLeaf, RenderGroup_t group, ClientRenderHandle_t renderHandle, bool bTwoPass = false
#if defined( L4D2 )
	, unsigned char alpha = 255, bool gameOwnedAlpha = false, bool ignoreZ = false
#endif
	)
{
#ifdef _DEBUG
	if (cl_drawleaf.GetInt() >= 0)
	{
		if (iLeaf != cl_drawleaf.GetInt())
			return;
	}
#endif

	Assert( group >= 0 && group < RENDER_GROUP_COUNT );
	
	int &curCount = renderList.m_RenderGroupCounts[group];
	if ( curCount < CClientRenderablesList::MAX_GROUP_ENTITIES )
	{
		Assert( (iLeaf >= 0) && (iLeaf <= 65535) );

		CClientRenderablesList::CEntry *pEntry = &renderList.m_RenderGroups[group][curCount];
		pEntry->m_pRenderable = pRenderable;
		pEntry->m_iWorldListInfoLeaf = iLeaf;
		pEntry->m_TwoPass = bTwoPass;
		pEntry->m_RenderHandle = renderHandle;
#if defined( L4D2 )
		pEntry->m_nL4D2Alpha = alpha;
		pEntry->m_bL4D2GameOwnedAlpha = gameOwnedAlpha;
		pEntry->m_bL4D2IgnoreZ = ignoreZ;
#endif
		curCount++;
	}
	else
	{
		engine->Con_NPrintf( 10, "Warning: overflowed CClientRenderablesList group %d", group );
	}
}


//-----------------------------------------------------------------------------
// Purpose: 
// Input  : renderList - 
//			renderGroup - 
//-----------------------------------------------------------------------------
#if !defined( L4D2 )
void CClientLeafSystem::CollateViewModelRenderables( CUtlVector< IClientRenderable * >& opaque, CUtlVector< IClientRenderable * >& translucent )
{
	for ( int i = m_ViewModels.Count()-1; i >= 0; --i )
	{
		ClientRenderHandle_t handle = m_ViewModels[i];
		RenderableInfo_t& renderable = m_Renderables[handle];

		// NOTE: In some cases, this removes the entity from the view model list
		renderable.m_pRenderable->ComputeFxBlend();
		
		// That's why we need to test RENDER_GROUP_OPAQUE_ENTITY - it may have changed in ComputeFXBlend()
		if ( renderable.m_RenderGroup == RENDER_GROUP_VIEW_MODEL_OPAQUE || renderable.m_RenderGroup == RENDER_GROUP_OPAQUE_ENTITY )
		{
			opaque.AddToTail( renderable.m_pRenderable );
		}
		else
		{
			translucent.AddToTail( renderable.m_pRenderable );
		}
	}
}
#endif

static RenderGroup_t DetectBucketedRenderGroup( RenderGroup_t group, float fDimension )
{
	float const arrThresholds[ 3 ] = {
		200.f,	// tree size
		80.f,	// player size
		30.f,	// crate size
	};
	Assert( ARRAYSIZE( arrThresholds ) + 1 >= RENDER_GROUP_CFG_NUM_OPAQUE_ENT_BUCKETS );
	Assert( group >= RENDER_GROUP_OPAQUE_STATIC && group <= RENDER_GROUP_OPAQUE_ENTITY );

	int bucketedGroupIndex;
	if ( RENDER_GROUP_CFG_NUM_OPAQUE_ENT_BUCKETS <= 2 ||
		fDimension >= arrThresholds[1] )
	{
		if ( RENDER_GROUP_CFG_NUM_OPAQUE_ENT_BUCKETS <= 1 ||
			fDimension >= arrThresholds[0] )
			bucketedGroupIndex = 0;
		else
			bucketedGroupIndex = 1;
	}
	else
	{
		if ( RENDER_GROUP_CFG_NUM_OPAQUE_ENT_BUCKETS <= 3 ||
			fDimension >= arrThresholds[2] )
			bucketedGroupIndex = 2;
		else
			bucketedGroupIndex = 3;
	}

	// Determine the new bucketed group
	RenderGroup_t bucketedGroup = RenderGroup_t( group - ( ( RENDER_GROUP_CFG_NUM_OPAQUE_ENT_BUCKETS - 1 ) - bucketedGroupIndex ) * 2 );
	Assert( bucketedGroup >= RENDER_GROUP_OPAQUE_STATIC_HUGE && bucketedGroup <= RENDER_GROUP_OPAQUE_ENTITY );
	
	return bucketedGroup;
}

void CClientLeafSystem::CollateRenderablesInLeaf( int leaf, int worldListLeafIndex, const SetupRenderInfo_t &info
#if defined( L4D2 )
	, const L4D2EntityRenderDataList_t &rows
#endif
	)
{
	bool portalTestEnts = r_PortalTestEnts.GetBool() && !r_portalsopenall.GetBool();
	
	// Place a fake entity for static/opaque ents in this leaf
	AddRenderableToRenderList( *info.m_pRenderList, NULL, worldListLeafIndex, RENDER_GROUP_OPAQUE_STATIC, NULL );
	AddRenderableToRenderList( *info.m_pRenderList, NULL, worldListLeafIndex, RENDER_GROUP_OPAQUE_ENTITY, NULL );

	// Collate everything.
	unsigned int idx = m_RenderablesInLeaf.FirstElement(leaf);
	for ( ;idx != m_RenderablesInLeaf.InvalidIndex(); idx = m_RenderablesInLeaf.NextElement(idx) )
	{
		ClientRenderHandle_t handle = m_RenderablesInLeaf.Element(idx);
		RenderableInfo_t& renderable = m_Renderables[handle];
#if defined( L4D2 )
		if ( !renderable.m_bL4D2RenderingEnabled || !renderable.m_bL4D2DrawForLocalUser ) continue;
		if ( renderable.m_pL4D2AlphaOwner )
		{
			if ( renderable.m_nL4D2WorldDataIndex != -1 )
				AppendEntityRenderable( info, worldListLeafIndex, rows[renderable.m_nL4D2WorldDataIndex] );
			continue;
		}
#endif

		// Early out on static props if we don't want to render them
		if ((!m_DrawStaticProps) && (renderable.m_Flags & RENDER_FLAGS_STATIC_PROP))
			continue;

		// Early out if we're told to not draw small objects (top view only,
		/* that's why we don't check the z component).
		if (!m_DrawSmallObjects)
		{
			CCachedRenderInfo& cachedInfo =  m_CachedRenderInfos[renderable.m_CachedRenderInfo];
			float sizeX = cachedInfo.m_Maxs.x - cachedInfo.m_Mins.x;
			float sizeY = cachedInfo.m_Maxs.y - cachedInfo.m_Mins.y;
			if ((sizeX < 50.f) && (sizeY < 50.f))
				continue;
		}*/

		Assert( m_DrawSmallObjects ); // MOTODO

		// Don't hit the same ent in multiple leaves twice.
		if ( renderable.m_RenderGroup != RENDER_GROUP_TRANSLUCENT_ENTITY )
		{
			if ( renderable.m_RenderFrame2 == info.m_nRenderFrame )
				continue;

			renderable.m_RenderFrame2 = info.m_nRenderFrame;
		}
		else // translucent
		{
			// Shadow depth skips ComputeTranslucentRenderLeaf!

			// Translucent entities already have had ComputeTranslucentRenderLeaf called on them
			// so m_RenderLeaf should be set to the nearest leaf, so that's what we want here.
			if ( renderable.m_RenderLeaf != leaf )
				continue;
		}

		unsigned char nAlpha = 255;
		if ( info.m_bDrawTranslucentObjects ) 
		{
			// Prevent culling if the renderable is invisible
			// NOTE: OPAQUE objects can have alpha == 0. 
			// They are made to be opaque because they don't have to be sorted.
			nAlpha = renderable.m_pRenderable->GetFxBlend();
			if ( nAlpha == 0 )
				continue;
		}

		Vector absMins, absMaxs;
#if defined( L4D2 )
		// Cache policy applies to EVERY leaf row, including real engine-owned
		// nonentities. Their existing alpha/order path above remains untouched.
		ComputeCachedWorldBounds( renderable );
		absMins = renderable.m_vecCachedWorldMins;
		absMaxs = renderable.m_vecCachedWorldMaxs;
#else
		CalcRenderableWorldSpaceAABB( renderable.m_pRenderable, absMins, absMaxs );
#endif
		// If the renderable is inside an area, cull it using the frustum for that area.
		if ( portalTestEnts && renderable.m_Area != -1 )
		{
			VPROF( "r_PortalTestEnts" );
			if ( !engine->DoesBoxTouchAreaFrustum( absMins, absMaxs, renderable.m_Area ) )
				continue;
		}
		else
		{
			// cull with main frustum
			if ( engine->CullBox( absMins, absMaxs ) )
				continue;
		}

		// UNDONE: Investigate speed tradeoffs of occlusion culling brush models too?
		if ( renderable.m_Flags & RENDER_FLAGS_STUDIO_MODEL )
		{
			// test to see if this renderable is occluded by the engine's occlusion system
			if ( engine->IsOccluded( absMins, absMaxs ) )
				continue;
		}

#ifdef INVASION_CLIENT_DLL
		if (info.m_flRenderDistSq != 0.0f)
		{
			Vector mins, maxs;
			renderable.m_pRenderable->GetRenderBounds( mins, maxs );

			if ((maxs.z - mins.z) < 100)
			{
				Vector vCenter;
				VectorLerp( mins, maxs, 0.5f, vCenter );
				vCenter += renderable.m_pRenderable->GetRenderOrigin();

				float flDistSq = info.m_vecRenderOrigin.DistToSqr( vCenter );
				if (info.m_flRenderDistSq <= flDistSq)
					continue;
			}
		}
#endif

		if( renderable.m_RenderGroup != RENDER_GROUP_TRANSLUCENT_ENTITY )
		{
			RenderGroup_t group = (RenderGroup_t)renderable.m_RenderGroup;

			// Determine object group offset
			if ( RENDER_GROUP_CFG_NUM_OPAQUE_ENT_BUCKETS > 1 &&
				 group >= RENDER_GROUP_OPAQUE_STATIC &&
				 group <= RENDER_GROUP_OPAQUE_ENTITY )
			{
				Vector dims;
				VectorSubtract( absMaxs, absMins, dims );

				float const fDimension = MAX( MAX( fabs(dims.x), fabs(dims.y) ), fabs(dims.z) );
				group = DetectBucketedRenderGroup( group, fDimension );
				
				Assert( group >= RENDER_GROUP_OPAQUE_STATIC_HUGE && group <= RENDER_GROUP_OPAQUE_ENTITY );
			}

			AddRenderableToRenderList( *info.m_pRenderList, renderable.m_pRenderable, 
				worldListLeafIndex, group, handle);
		}
		else
		{
			bool bTwoPass = ((renderable.m_Flags & RENDER_FLAGS_TWOPASS) != 0) && ( nAlpha == 255 );	// Two pass?

			// Add to appropriate list if drawing translucent objects (shadow depth mapping will skip this)
			if ( info.m_bDrawTranslucentObjects ) 
			{
				AddRenderableToRenderList( *info.m_pRenderList, renderable.m_pRenderable, 
					worldListLeafIndex, (RenderGroup_t)renderable.m_RenderGroup, handle, bTwoPass );
			}
			
			if ( bTwoPass )	// Also add to opaque list if it's a two-pass model... 
			{
				AddRenderableToRenderList( *info.m_pRenderList, renderable.m_pRenderable, 
					worldListLeafIndex, RENDER_GROUP_OPAQUE_ENTITY, handle, bTwoPass );
			}
		}
	}

	// Do detail objects.
	// These don't have render handles!
	if ( info.m_bDrawDetailObjects && ShouldDrawDetailObjectsInLeaf( leaf, info.m_nDetailBuildFrame ) )
	{
		idx = m_Leaf[leaf].m_FirstDetailProp;
		int count = m_Leaf[leaf].m_DetailPropCount;
		while( --count >= 0 )
		{
			IClientRenderable* pRenderable = DetailObjectSystem()->GetDetailModel(idx);

			// FIXME: This if check here is necessary because the detail object system also maintains lists of sprites...
			if (pRenderable)
			{
				if( pRenderable->IsTransparent() )
				{
					if ( info.m_bDrawTranslucentObjects )	// Don't draw translucent objects into shadow depth maps
					{
						// Lots of the detail entities are invisible so avoid sorting them and all that.
						if( pRenderable->GetFxBlend() > 0 )
						{
							AddRenderableToRenderList( *info.m_pRenderList, pRenderable, 
								worldListLeafIndex, RENDER_GROUP_TRANSLUCENT_ENTITY, DETAIL_PROP_RENDER_HANDLE );
						}
					}
				}
				else
				{
					AddRenderableToRenderList( *info.m_pRenderList, pRenderable, 
						worldListLeafIndex, RENDER_GROUP_OPAQUE_ENTITY, DETAIL_PROP_RENDER_HANDLE );
				}
			}
			++idx;
		}
	}
}


//-----------------------------------------------------------------------------
// Sort entities in a back-to-front ordering
//-----------------------------------------------------------------------------
void CClientLeafSystem::SortEntities( const Vector &vecRenderOrigin, const Vector &vecRenderForward, CClientRenderablesList::CEntry *pEntities, int nEntities )
{
	// Don't sort if we only have 1 entity
	if ( nEntities <= 1 )
		return;

	float dists[CClientRenderablesList::MAX_GROUP_ENTITIES];

	// First get a distance for each entity.
	int i;
	for( i=0; i < nEntities; i++ )
	{
		IClientRenderable *pRenderable = pEntities[i].m_pRenderable;

		// Compute the center of the object (needed for translucent brush models)
		Vector boxcenter;
		Vector mins,maxs;
		pRenderable->GetRenderBounds( mins, maxs );
		VectorAdd( mins, maxs, boxcenter );
		VectorMA( pRenderable->GetRenderOrigin(), 0.5f, boxcenter, boxcenter );

		// Compute distance...
		Vector delta;
		VectorSubtract( boxcenter, vecRenderOrigin, delta );
		dists[i] = DotProduct( delta, vecRenderForward );
	}

	// H-sort.
	int stepSize = 4;
	while( stepSize )
	{
		int end = nEntities - stepSize;
		for( i=0; i < end; i += stepSize )
		{
			if( dists[i] > dists[i+stepSize] )
			{
				::V_swap( pEntities[i], pEntities[i+stepSize] );
				::V_swap( dists[i], dists[i+stepSize] );

				if( i == 0 )
				{
					i = -stepSize;
				}
				else
				{
					i -= stepSize << 1;
				}
			}
		}

		stepSize >>= 1;
	}
}


#if !defined( L4D2 )
void CClientLeafSystem::BuildRenderablesList( const SetupRenderInfo_t &info )
{
	VPROF_BUDGET( "BuildRenderablesList", "BuildRenderablesList" );
	int leafCount = info.m_pWorldListInfo->m_LeafCount;
	const Vector &vecRenderOrigin = info.m_vecRenderOrigin;
	const Vector &vecRenderForward = info.m_vecRenderForward;
	CClientRenderablesList::CEntry *pTranslucentEntries = info.m_pRenderList->m_RenderGroups[RENDER_GROUP_TRANSLUCENT_ENTITY];
	int &nTranslucentEntries = info.m_pRenderList->m_RenderGroupCounts[RENDER_GROUP_TRANSLUCENT_ENTITY];

	for( int i = 0; i < leafCount; i++ )
	{
		int nTranslucent = nTranslucentEntries;

		// Add renderables from this leaf...
		CollateRenderablesInLeaf( info.m_pWorldListInfo->m_pLeafList[i], i, info );

		int nNewTranslucent = nTranslucentEntries - nTranslucent;
		if( (nNewTranslucent != 0 ) && info.m_bDrawTranslucentObjects )
		{
			// Sort the new translucent entities.
			SortEntities( vecRenderOrigin, vecRenderForward, &pTranslucentEntries[nTranslucent], nNewTranslucent );
		}
	}
}
#endif

//========= Copyright Valve Corporation, All rights reserved. ============//
// Compose into EXISTING clientleafsystem.cpp after its class declaration.
// Definitions replace the L4D2 responsibilities, not the public engine ABI.
#if defined( L4D2 )

// Native BD380. Domain0/1/2 is not RenderGroup_t. Keep the actual two-bit
// assignment, including the native behavior for an explicitly supplied3.
void CClientLeafSystem::SetModelTranslucencyType( ClientRenderHandle_t handle, int type )
{
	if ( handle == INVALID_CLIENT_RENDER_HANDLE )
		return;
	RenderableInfo_t &info = m_Renderables[handle];
	info.m_nL4D2TranslucencyType = type & 3;
	UpdateSDKRenderGroupForEntity( info );
}

// Native BD3F0, folded to the one real view user0. This is actual eligibility,
// not a synthetic local-resolvability or input-camera flag.
void CClientLeafSystem::SetDrawForLocalUser( ClientRenderHandle_t handle, bool draw )
{
	if ( handle != INVALID_CLIENT_RENDER_HANDLE )
		m_Renderables[handle].m_bL4D2DrawForLocalUser = draw;
}

// Native BD4C0/+120: WORD22 flag0x1, not local-user draw flags/+116.
// No RenderableChanged callback; both world and VM admission read the state.
void CClientLeafSystem::EnableRendering( ClientRenderHandle_t handle, bool enable )
{
	if ( handle != INVALID_CLIENT_RENDER_HANDLE )
		m_Renderables[handle].m_bL4D2RenderingEnabled = enable;
}

// Native C2940. Enabling adds the VM list BEFORE removing spatial/shadow
// memberships. Disabling removes VM membership BEFORE RenderableChanged.
void CClientLeafSystem::SetRenderWithViewModels( ClientRenderHandle_t handle, bool enable )
{
	if ( handle == INVALID_CLIENT_RENDER_HANDLE )
		return;
	RenderableInfo_t &info = m_Renderables[handle];
	if ( enable )
	{
		if ( !info.m_bL4D2ViewModel )
		{
			info.m_bL4D2ViewModel = true;
			AddToViewModelList( handle );
			RemoveFromTree( handle );
			UpdateSDKRenderGroupForEntity( info );
		}
	}
	else if ( info.m_bL4D2ViewModel )
	{
		info.m_bL4D2ViewModel = false;
		RemoveFromViewModelList( handle );
		RenderableChanged( handle );
		UpdateSDKRenderGroupForEntity( info );
	}
}

// Only a bucket adapter for existing SDK consumers. Native metadata remains
// separate, and neither alpha255 nor an SDK bucket destroys native type2.
void CClientLeafSystem::UpdateSDKRenderGroupForEntity( RenderableInfo_t &info )
{
	if ( !info.m_pL4D2AlphaOwner )
		return;
	if ( info.m_bL4D2ViewModel )
	{
		info.m_RenderGroup = info.m_nL4D2TranslucencyType == 0 ?
			RENDER_GROUP_VIEW_MODEL_OPAQUE : RENDER_GROUP_VIEW_MODEL_TRANSLUCENT;
	}
	else if ( info.m_nL4D2TranslucencyType != 0 )
	{
		info.m_RenderGroup = RENDER_GROUP_TRANSLUCENT_ENTITY;
	}
	else
	{
		info.m_RenderGroup = ( info.m_Flags & RENDER_FLAGS_BRUSH_MODEL ) ?
			RENDER_GROUP_OPAQUE_BRUSH : RENDER_GROUP_OPAQUE_ENTITY;
	}
	if ( info.m_nL4D2TranslucencyType == 2 )
		info.m_Flags |= RENDER_FLAGS_TWOPASS;
	else
		info.m_Flags &= ~RENDER_FLAGS_TWOPASS;
}

static void AddL4D2ViewModelRenderable( CUtlVector< ClientViewModelRenderable_t > &list,
	IClientRenderable *renderable, unsigned char alpha, bool gameOwnedAlpha )
{
	ClientViewModelRenderable_t &entry = list[list.AddToTail()];
	entry.m_pRenderable = renderable;
	entry.m_nAlpha = alpha;
	entry.m_bGameOwnedAlpha = gameOwnedAlpha;
}

// Whole native C1ED0 on game-owned rows. Main's explicit target-engine
// capability adaptation keeps nonentity rows on their preexisting SDK path.
// In particular, native2 gets BOTH passes even when its translucent alpha0.
void CClientLeafSystem::CollateViewModelRenderables(
	CUtlVector< ClientViewModelRenderable_t > &opaque,
	CUtlVector< ClientViewModelRenderable_t > &translucent )
{
	for ( int i = m_ViewModels.Count() - 1; i >= 0; --i )
	{
		const ClientRenderHandle_t handle = m_ViewModels[i];
		RenderableInfo_t &info = m_Renderables[handle];
		// Native draw-user metadata applies to EVERY row, including genuine
		// game-owned nonentity particles whose GetBaseEntity is NULL.
		if ( !info.m_bL4D2RenderingEnabled || !info.m_bL4D2DrawForLocalUser )
			continue;
		if ( info.m_pL4D2AlphaOwner )
		{
			const unsigned char alpha = info.m_pL4D2AlphaOwner->ComputeRenderAlpha();
			const unsigned char type = info.m_nL4D2TranslucencyType;
			if ( alpha != 255 || type != 0 )
			{
				AddL4D2ViewModelRenderable( translucent, info.m_pRenderable, alpha, true );
				if ( type != 2 )
					continue;
			}
			AddL4D2ViewModelRenderable( opaque, info.m_pRenderable, 255, true );
		}
		else
		{
			// Original SDK order/side effects retained for engine/nonproperty
			// renderables. ComputeFxBlend can remove a VM-list membership.
			info.m_pRenderable->ComputeFxBlend();
			if ( info.m_RenderGroup == RENDER_GROUP_VIEW_MODEL_OPAQUE ||
				info.m_RenderGroup == RENDER_GROUP_OPAQUE_ENTITY )
			{
				AddL4D2ViewModelRenderable( opaque, info.m_pRenderable, 255, false );
			}
			else
			{
				AddL4D2ViewModelRenderable( translucent, info.m_pRenderable, 255, false );
			}
		}
	}
}

void ClientLeafSystem_SetModelTranslucencyType( ClientRenderHandle_t handle, int type )
{
	CClientLeafSystem::s_ClientLeafSystem.SetModelTranslucencyType( handle, type );
}

void ClientLeafSystem_SetDrawForLocalUser( ClientRenderHandle_t handle, bool draw )
{
	CClientLeafSystem::s_ClientLeafSystem.SetDrawForLocalUser( handle, draw );
}

void ClientLeafSystem_SetRenderWithViewModels( ClientRenderHandle_t handle, bool enable )
{
	CClientLeafSystem::s_ClientLeafSystem.SetRenderWithViewModels( handle, enable );
}

void ClientLeafSystem_EnableRendering( ClientRenderHandle_t handle, bool enable )
{
	CClientLeafSystem::s_ClientLeafSystem.EnableRendering( handle, enable );
}
#endif

//========= Copyright Valve Corporation, All rights reserved. ============//
// Compose into EXISTING clientleafsystem.cpp AFTER CClientLeafSystem's class
// declaration. Not a new TU; no engine interface/ABI extension. Native cache
//20/40 and cached world AABB are new PRIVATE per-renderable game-owned data.
#if defined( L4D2 )
enum
{
	L4D2_BOUNDS_CACHE_VALID = 0x20,
	L4D2_BOUNDS_DISABLE_CACHE = 0x40
};

// NativeC0910 serializes invalidation + its existing dirty-queue admission.
static CThreadFastMutex s_L4D2RenderableChangedMutex;

// Whole shared compute-or-reuse responsibility of nativeBD760/BDF00. Native
// batch output copies these six cached floats; the standalone query passes the
// actual cache directly. The standalone consumer is frustum CULL, never PVS.
void CClientLeafSystem::ComputeCachedWorldBounds( RenderableInfo_t &info )
{
	if ( ( info.m_nL4D2BoundsFlags & L4D2_BOUNDS_CACHE_VALID ) == 0 )
	{
		info.m_pRenderable->GetRenderBoundsWorldspace( info.m_vecCachedWorldMins,
			info.m_vecCachedWorldMaxs );
		if ( ( info.m_nL4D2BoundsFlags & L4D2_BOUNDS_DISABLE_CACHE ) == 0 )
		{
			// NativeGetIClientUnknown ->GetBaseEntity, not GetBaseAnimating.
			// Native516/C1B030 is the actual live hierarchy parent; use the
			// real target hierarchy, not received network-parent308/storage.
			C_BaseEntity *entity = info.m_pRenderable->GetIClientUnknown()->GetBaseEntity();
			if ( !entity || !entity->GetMoveParent() )
				info.m_nL4D2BoundsFlags |= L4D2_BOUNDS_CACHE_VALID;
		}
	}
}

// Whole nativeBD550. IMPORTANT retained bug: toggling40 does NOT clear an
// already-valid20 cache. Do NOT call RenderableChanged, dirty bounds, or repair
// stale vectors here; the separate native invalidation owner does that.
void CClientLeafSystem::DisableCachedRenderBounds( ClientRenderHandle_t handle, bool disable )
{
	if ( handle == INVALID_CLIENT_RENDER_HANDLE )
		return;
	RenderableInfo_t &info = m_Renderables[handle];
	if ( disable )
		info.m_nL4D2BoundsFlags |= L4D2_BOUNDS_DISABLE_CACHE;
	else
		info.m_nL4D2BoundsFlags &= ~L4D2_BOUNDS_DISABLE_CACHE;
}

// Whole native BDF00. TRUE means culled; an invalid handle is not culled.
// No PVS/cluster test, inverted visibility result, or model-bound substitute.
bool CClientLeafSystem::CullCachedWorldBounds( ClientRenderHandle_t handle )
{
	if ( handle == INVALID_CLIENT_RENDER_HANDLE )
		return false;
	RenderableInfo_t &info = m_Renderables[handle];
	ComputeCachedWorldBounds( info );
	return engine->CullBox( info.m_vecCachedWorldMins, info.m_vecCachedWorldMaxs );
}

// Real game-DLL-only backend function, declared in game/client/clientleafsystem.h.
// It does actual flag work above; it is NOT an appended virtual in a public
// engine header, a vtable cast, a no-op setter, or a second leaf/cache registry.
void ClientLeafSystem_DisableCachedRenderBounds( ClientRenderHandle_t handle, bool disable )
{
	CClientLeafSystem::s_ClientLeafSystem.DisableCachedRenderBounds( handle, disable );
}

void ClientLeafSystem_EnableBloatedRenderBounds( ClientRenderHandle_t handle, bool enable )
{
	CClientLeafSystem::s_ClientLeafSystem.EnableBloatedRenderBounds( handle, enable );
}

bool ClientLeafSystem_CullCachedWorldBounds( ClientRenderHandle_t handle )
{
	return CClientLeafSystem::s_ClientLeafSystem.CullCachedWorldBounds( handle );
}
#endif

//========= Copyright Valve Corporation, All rights reserved. ============//
// Compose into EXISTING clientleafsystem.cpp after its class declaration and
// client_leaf_cached_bounds.cpp. Stock PreRender/RenderableChanged remain !L4D2.
#if defined( L4D2 )
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif

static bool s_L4D2RecomputingRenderableLeaves = false;

// Whole native C0910. Cache invalidation happens under the SAME mutex and
// BEFORE already-dirty admission/re-entrancy behavior; no extra queue owner.
void CClientLeafSystem::RenderableChanged( ClientRenderHandle_t handle )
{
	if ( !m_Renderables.IsValidIndex( handle ) ) return;
	RenderableInfo_t &info = m_Renderables[handle];
	AUTO_LOCK( s_L4D2RenderableChangedMutex );
	info.m_nL4D2BoundsFlags &= ~L4D2_BOUNDS_CACHE_VALID;
	if ( info.m_Flags & RENDER_FLAGS_HASCHANGED )
	{
		if ( s_L4D2RecomputingRenderableLeaves )
		{
			Warning( "------------------------------------------------------------\n" );
			Warning( "------------------------------------------------------------\n" );
			Warning( "------------------------------------------------------------\n" );
			Warning( "------------------------------------------------------------\n" );
			Warning( "Re-entrancy found in CClientLeafSystem::RenderableChanged\n" );
			Warning( "Contact Shanon or Brian\n" );
			Warning( "------------------------------------------------------------\n" );
			Warning( "------------------------------------------------------------\n" );
			Warning( "------------------------------------------------------------\n" );
			Warning( "------------------------------------------------------------\n" );
		}
	}
	else
	{
		info.m_Flags |= RENDER_FLAGS_HASCHANGED;
		m_DirtyRenderables.AddToTail( handle );
	}
}

// Whole native BD500. Disable invalidates/requeues ONLY when10 was set;
// enable merely sets10. This is distinct from40, which must never invalidate20.
void CClientLeafSystem::EnableBloatedRenderBounds( ClientRenderHandle_t handle, bool enable )
{
	if ( handle == INVALID_CLIENT_RENDER_HANDLE ) return;
	RenderableInfo_t &info = m_Renderables[handle];
	if ( enable ) info.m_nL4D2BoundsFlags |= 0x10;
	else if ( info.m_nL4D2BoundsFlags & 0x10 )
	{
		info.m_nL4D2BoundsFlags &= ~0x10;
		RenderableChanged( handle );
	}
}

// Whole native BD180 placement responsibility. Cached culling boxes+48 are
// separate from these 32-unit placement boxes+24; do not merge their lifetimes.
void CClientLeafSystem::ComputePlacementBounds( RenderableInfo_t &info, Vector &mins, Vector &maxs )
{
	CalcRenderableWorldSpaceAABB_Fast( info.m_pRenderable, mins, maxs );
	mins.x = static_cast< float >( floor( static_cast< double >( mins.x ) * 0.03125 ) * 32.0 );
	mins.y = static_cast< float >( floor( static_cast< double >( mins.y ) * 0.03125 ) * 32.0 );
	mins.z = static_cast< float >( floor( static_cast< double >( mins.z ) * 0.03125 ) * 32.0 );
	maxs.x = static_cast< float >( ceil( static_cast< double >( maxs.x ) * 0.03125 ) * 32.0 );
	maxs.y = static_cast< float >( ceil( static_cast< double >( maxs.y ) * 0.03125 ) * 32.0 );
	maxs.z = static_cast< float >( ceil( static_cast< double >( maxs.z ) * 0.03125 ) * 32.0 );
	if ( info.m_nL4D2BoundsFlags & 0x10 )
	{
		Vector combinedMins, combinedMaxs;
		combinedMins.x = mins.x > info.m_vecPlacementMins.x ? info.m_vecPlacementMins.x : mins.x;
		combinedMins.y = mins.y > info.m_vecPlacementMins.y ? info.m_vecPlacementMins.y : mins.y;
		combinedMins.z = mins.z;
		if ( combinedMins.z > info.m_vecPlacementMins.z ) combinedMins.z = info.m_vecPlacementMins.z;
		combinedMaxs.x = maxs.x;
		if ( info.m_vecPlacementMaxs.x > combinedMaxs.x ) combinedMaxs.x = info.m_vecPlacementMaxs.x;
		combinedMaxs.y = info.m_vecPlacementMaxs.y > maxs.y ? info.m_vecPlacementMaxs.y : maxs.y;
		combinedMaxs.z = info.m_vecPlacementMaxs.z > maxs.z ? info.m_vecPlacementMaxs.z : maxs.z;
		const float dx = combinedMaxs.x - combinedMins.x;
		const float dy = combinedMaxs.y - combinedMins.y;
		const float dz = combinedMaxs.z - combinedMins.z;
		const float combinedSize = ( dy * dy + dx * dx ) + dz * dz;
		const float ownX = maxs.x - mins.x;
		const float ownY = maxs.y - mins.y;
		const float ownZ = maxs.z - mins.z;
		const float ownSize = ( ownY * ownY + ownX * ownX ) + ownZ * ownZ;
		if ( combinedSize <= 32768.0f || ownSize * 2.0f >= combinedSize )
		{
			mins = combinedMins;
			maxs = combinedMaxs;
		}
	}
}

// Whole native C2520 scheduling/placement ownership. Reverse batch order;
// clear dirty BEFORE callbacks; only changed placement boxes leave/reenter the
// retained real BSP/shadow backend. Newly queued entries survive RemoveMultiple.
void CClientLeafSystem::PreRender()
{
	VPROF_BUDGET( "CClientLeafSystem::RecomputeRenderableLeaves", "ClientLeafSystem" );
	const bool visualize = cl_leafsystemvis.GetBool();
	int iterations = 0;
	while ( m_DirtyRenderables.Count() )
	{
		if ( ++iterations > 10 )
		{
			Warning( "Too many dirty renderables!\n" );
			break;
		}
		const int dirtyCount = m_DirtyRenderables.Count();
		s_L4D2RecomputingRenderableLeaves = true;
		for ( int i = dirtyCount - 1; i >= 0; --i )
		{
			const ClientRenderHandle_t handle = m_DirtyRenderables[i];
			RenderableInfo_t &info = m_Renderables[handle];
			info.m_Flags &= ~RENDER_FLAGS_HASCHANGED;
			if ( info.m_bL4D2ViewModel ) continue;
			Vector mins, maxs;
			ComputePlacementBounds( info, mins, maxs );
			if ( info.m_vecPlacementMins.x != mins.x || info.m_vecPlacementMins.y != mins.y ||
				info.m_vecPlacementMins.z != mins.z || info.m_vecPlacementMaxs.x != maxs.x ||
				info.m_vecPlacementMaxs.y != maxs.y || info.m_vecPlacementMaxs.z != maxs.z )
			{
				RemoveFromTree( handle );
				InsertIntoTree( handle, mins, maxs );
				if ( visualize ) debugoverlay->AddBoxOverlay( vec3_origin, mins, maxs,
					vec3_angle, 0, 255, 0, 0, 0.0f );
			}
		}
		s_L4D2RecomputingRenderableLeaves = false;
		m_DirtyRenderables.RemoveMultiple( 0, dirtyCount );
	}
}

#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
#endif

//========= Copyright Valve Corporation, All rights reserved. ============//
// Replace ONLY the L4D2 arm of existing CalcRenderableWorldSpaceAABB_Fast in
// clientleafsystem.cpp. Stock function remains !L4D2. No second public provider.
#if defined( L4D2 )
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif

static float L4D2BoundsSquareRoot( float value )
{
	return _mm_cvtss_f32( _mm_sqrt_ss( _mm_set_ss( value ) ) );
}

// Whole native BCF50. Native504 is the ALREADY received parent attachment byte,
// not a new bool/policy. Attachment parenting also takes the parent-fast-box
// arm even without IsFollowingEntity. The parent is freshly resolved again at
// the adjustment itself; no missing-parent guard or cached network-parent308.
void CalcRenderableWorldSpaceAABB_Fast( IClientRenderable *renderable, Vector &mins, Vector &maxs )
{
	C_BaseEntity *entity = renderable->GetIClientUnknown()->GetBaseEntity();
	if ( !entity || ( !entity->IsFollowingEntity() &&
		( !entity->GetMoveParent() || entity->GetParentAttachment() == 0 ) ) )
	{
		renderable->GetRenderBoundsWorldspace( mins, maxs );
		return;
	}
	C_BaseEntity *parent = entity->GetMoveParent();
	CalcRenderableWorldSpaceAABB_Fast( parent, mins, maxs );
	Vector localMins, localMaxs;
	entity->GetRenderBounds( localMins, localMaxs );
	const Vector &offset = entity->GetLocalOrigin();
	const float offsetSize = ( offset.x * offset.x + offset.y * offset.y ) + offset.z * offset.z;
	const float offsetLength = L4D2BoundsSquareRoot( offsetSize );
	const float maximumSize = ( localMaxs.y * localMaxs.y + localMaxs.x * localMaxs.x ) + localMaxs.z * localMaxs.z;
	const float minimumSize = ( localMins.z * localMins.z + localMins.x * localMins.x ) + localMins.y * localMins.y;
	const float maximumLength = L4D2BoundsSquareRoot( maximumSize );
	const float minimumLength = L4D2BoundsSquareRoot( minimumSize );
	float radius = maximumLength > minimumLength ? maximumLength : minimumLength;
	radius = radius > offsetLength ? radius : offsetLength;
	mins.x -= radius;
	mins.y -= radius;
	mins.z -= radius;
	maxs.x += radius;
	maxs.y = radius + maxs.y;
	maxs.z = radius + maxs.z;
}

#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
#endif

//========= Copyright Valve Corporation, All rights reserved. ============//
// Compose into EXISTING clientleafsystem.cpp after DetectBucketedRenderGroup.
// Private declaration/entry transport is in client_world_alpha_transport.patch.txt.
// Main-approved Plan A: retain SDK PVS/fog/area-frustum, engine/nonentity rows,
// detail objects, opaque buckets and translucent sorting. No engine ABI changes.
#if defined( L4D2 )
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif

void CClientLeafSystem::CollectEntityRenderables( const SetupRenderInfo_t &setup,
	L4D2EntityRenderDataList_t &rows )
{
	for ( int worldLeaf = 0; worldLeaf < setup.m_pWorldListInfo->m_LeafCount; ++worldLeaf )
	{
		const int leaf = setup.m_pWorldListInfo->m_pLeafList[worldLeaf];
		for ( unsigned int index = m_RenderablesInLeaf.FirstElement( leaf );
			index != m_RenderablesInLeaf.InvalidIndex(); index = m_RenderablesInLeaf.NextElement( index ) )
		{
			const ClientRenderHandle_t handle = m_RenderablesInLeaf.Element( index );
			RenderableInfo_t &info = m_Renderables[handle];
			if ( !info.m_pL4D2AlphaOwner ) continue;
			if ( info.m_RenderFrame2 != setup.m_nRenderFrame )
			{
				info.m_RenderFrame2 = setup.m_nRenderFrame;
				info.m_nL4D2WorldDataIndex = -1;
				if ( ( !m_DrawStaticProps && ( info.m_Flags & RENDER_FLAGS_STATIC_PROP ) ) ||
					!info.m_bL4D2RenderingEnabled || !info.m_bL4D2DrawForLocalUser || info.m_bL4D2ViewModel ||
					( !setup.m_bDrawTranslucentObjects && info.m_nL4D2TranslucencyType == 1 ) )
					continue;
				info.m_nL4D2WorldDataIndex = rows.AddToTail();
				L4D2EntityRenderData_t &row = rows[info.m_nL4D2WorldDataIndex];
				row.m_pInfo = &info;
				row.m_pAlphaOwner = info.m_pL4D2AlphaOwner;
				row.m_hRenderable = handle;
				row.m_nOpaqueWorldLeaf = worldLeaf;
				row.m_nTranslucentWorldLeaf = -1;
				row.m_nAlpha = 255;
				row.m_bIgnoreZ = false;
				row.m_bCulled = false;
			}
			if ( info.m_nL4D2WorldDataIndex != -1 && info.m_RenderLeaf == static_cast< unsigned int >( leaf ) )
				rows[info.m_nL4D2WorldDataIndex].m_nTranslucentWorldLeaf = worldLeaf;
		}
	}
}

// Native BD760 completes ALL bounds before BD870 starts any frustum callback.
void CClientLeafSystem::ComputeEntityWorldBounds( L4D2EntityRenderDataList_t &rows )
{
	for ( int i = 0; i < rows.Count(); ++i )
	{
		L4D2EntityRenderData_t &row = rows[i];
		RenderableInfo_t &info = *row.m_pInfo;
		row.m_nArea = info.m_Area;
		ComputeCachedWorldBounds( info );
		row.m_vecMins = info.m_vecCachedWorldMins;
		row.m_vecMaxs = info.m_vecCachedWorldMaxs;
	}
}

// MAXPS(value, zero) selects the second operand on equality AND unordered.
static float L4D2PositiveBoxDelta( float value )
{
	return value > 0.0f ? value : 0.0f;
}

void CClientLeafSystem::CullEntityWorldBounds( const SetupRenderInfo_t &setup,
	L4D2EntityRenderDataList_t &rows )
{
	const bool portalTestEnts = r_PortalTestEnts.GetBool() && !r_portalsopenall.GetBool();
	for ( int i = 0; i < rows.Count(); ++i )
	{
		L4D2EntityRenderData_t &row = rows[i];
		// Main retains the actual SDK area-frustum backend, not a reconstructed
		// native engine frustum buffer or an added public-interface virtual.
		if ( portalTestEnts && row.m_nArea != -1 )
			row.m_bCulled = !engine->DoesBoxTouchAreaFrustum( row.m_vecMins, row.m_vecMaxs, row.m_nArea );
		else
			row.m_bCulled = engine->CullBox( row.m_vecMins, row.m_vecMaxs );
		if ( row.m_bCulled ) continue;
		if ( !portalTestEnts )
		{
			// Actual native BD870 behavior: no-portal branch writes ZERO, not
			// closest-box distance. Preserve this defined distance-fade bug.
			row.m_flDistanceSquared = 0.0f;
			continue;
		}
		const Vector &origin = setup.m_vecRenderOrigin;
		const float x = L4D2PositiveBoxDelta( origin.x - row.m_vecMaxs.x ) +
			L4D2PositiveBoxDelta( row.m_vecMins.x - origin.x );
		const float y = L4D2PositiveBoxDelta( origin.y - row.m_vecMaxs.y ) +
			L4D2PositiveBoxDelta( row.m_vecMins.y - origin.y );
		const float z = L4D2PositiveBoxDelta( origin.z - row.m_vecMaxs.z ) +
			L4D2PositiveBoxDelta( row.m_vecMins.z - origin.z );
		row.m_flDistanceSquared = ( y * y + x * x ) + z * z;
		// CM(P)LE is ordered: equality culls; unordered does not.
		if ( setup.m_flRenderDistSq <= row.m_flDistanceSquared ) row.m_bCulled = true;
	}
}

void CClientLeafSystem::AppendEntityRenderable( const SetupRenderInfo_t &setup,
	int worldLeaf, const L4D2EntityRenderData_t &row )
{
	if ( row.m_bCulled ) return;
	const RenderableInfo_t &info = *row.m_pInfo;
	const int type = info.m_nL4D2TranslucencyType;
	const bool translucent = row.m_nAlpha != 255 || type != 0;
	const bool twoPass = type == 2 && row.m_nAlpha == 255;
	if ( translucent && setup.m_bDrawTranslucentObjects && worldLeaf == row.m_nTranslucentWorldLeaf )
	{
		AddRenderableToRenderList( *setup.m_pRenderList, info.m_pRenderable, worldLeaf,
			RENDER_GROUP_TRANSLUCENT_ENTITY, row.m_hRenderable, twoPass, row.m_nAlpha, true, row.m_bIgnoreZ );
	}
	// World type2 is dual pass ONLY at alpha255. VM type2 is different and
	// retains opaque255 even at translucent alpha0; C1ED0 owns that policy.
	const int opaqueLeaf = twoPass && setup.m_bDrawTranslucentObjects ?
		row.m_nTranslucentWorldLeaf : row.m_nOpaqueWorldLeaf;
	if ( ( !translucent || twoPass ) && worldLeaf == opaqueLeaf )
	{
		RenderGroup_t group = ( info.m_Flags & RENDER_FLAGS_BRUSH_MODEL ) ?
			RENDER_GROUP_OPAQUE_BRUSH : RENDER_GROUP_OPAQUE_ENTITY;
		if ( RENDER_GROUP_CFG_NUM_OPAQUE_ENT_BUCKETS > 1 && group == RENDER_GROUP_OPAQUE_ENTITY )
		{
			Vector dimensions;
			VectorSubtract( row.m_vecMaxs, row.m_vecMins, dimensions );
			const float dimension = MAX( MAX( fabs( dimensions.x ), fabs( dimensions.y ) ), fabs( dimensions.z ) );
			group = DetectBucketedRenderGroup( group, dimension );
		}
		AddRenderableToRenderList( *setup.m_pRenderList, info.m_pRenderable, worldLeaf,
			group, row.m_hRenderable, twoPass, 255, true, false );
	}
}

// Whole retained SDK BuildRenderablesList, L4D2 definition. Stock body is !L4D2.
void CClientLeafSystem::BuildRenderablesList( const SetupRenderInfo_t &setup )
{
	VPROF_BUDGET( "BuildRenderablesList", "BuildRenderablesList" );
	L4D2EntityRenderDataList_t rows;
	CollectEntityRenderables( setup, rows );
	ComputeEntityWorldBounds( rows );
	CullEntityWorldBounds( setup, rows );
	// C1FE0 calls BEBC0 only when DrawTranslucentObjects is enabled. A depth
	// pass retains alpha255/ignoreZfalse and performs NO alpha/shadow callbacks.
	if ( setup.m_bDrawTranslucentObjects ) ComputeEntityRenderAlpha( setup, rows );
	CullOccludedEntityRenderables( rows );
	CClientRenderablesList::CEntry *translucentEntries =
		setup.m_pRenderList->m_RenderGroups[RENDER_GROUP_TRANSLUCENT_ENTITY];
	int &translucentCount = setup.m_pRenderList->m_RenderGroupCounts[RENDER_GROUP_TRANSLUCENT_ENTITY];
	for ( int i = 0; i < setup.m_pWorldListInfo->m_LeafCount; ++i )
	{
		const int previousCount = translucentCount;
		CollateRenderablesInLeaf( setup.m_pWorldListInfo->m_pLeafList[i], i, setup, rows );
		const int newCount = translucentCount - previousCount;
		if ( newCount != 0 && setup.m_bDrawTranslucentObjects )
			SortEntities( setup.m_vecRenderOrigin, setup.m_vecRenderForward,
				&translucentEntries[previousCount], newCount );
	}
}

#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
#endif

//========= Copyright Valve Corporation, All rights reserved. ============//
// Compose into EXISTING clientleafsystem.cpp after its class declaration.
// Main-approved A: SDK PVS/frustum collection precedes this ONE entity batch;
// SDK bucket/sort and untouched engine-row paths consume its stored results.
// Required real includes: emmintrin.h, mathlib/vmatrix.h, materialsystem/
// imaterialsystem.h, iclientshadowmgr.h, iviewrender.h, viewrender.h, view.h.
#if defined( L4D2 )
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif

struct RenderAlphaScreenState_t
{
	VMatrix m_WorldToProjection;
	Vector m_Up;
	int m_nHeight;
};

// Whole native BEAE0 via the actual target material context, not an engine
// vtable cast or a guessed view/FOV projection. Matrix/query order is retained.
static void CaptureRenderAlphaScreenState( RenderAlphaScreenState_t &state )
{
	CMatRenderContextPtr context( materials );
	VMatrix viewMatrix, projectionMatrix;
	context->GetMatrix( MATERIAL_VIEW, &viewMatrix );
	context->GetMatrix( MATERIAL_PROJECTION, &projectionMatrix );
	MatrixMultiply( projectionMatrix, viewMatrix, state.m_WorldToProjection );
	int x, y, width;
	context->GetViewport( x, y, width, state.m_nHeight );
	context->GetWorldSpaceCameraVectors( NULL, NULL, &state.m_Up );
}

// Whole BC780. The two near-plane tests use COMISS/JB, including unordered.
// Plus projection spills float; the final minus product/difference stays wide.
static double RenderAlphaProjectedSphereSize( const Vector &center, float radius,
	const RenderAlphaScreenState_t &state )
{
	const VMatrix &matrix = state.m_WorldToProjection;
	const Vector &up = state.m_Up;
	const float yCenter = ( ( center.x * matrix[1][0] + center.y * matrix[1][1] ) +
		center.z * matrix[1][2] ) + matrix[1][3];
	const float yRadius = ( ( up.x * matrix[1][0] + up.y * matrix[1][1] ) +
		up.z * matrix[1][2] ) * radius;
	const float wCenter = ( ( matrix[3][0] * center.x + matrix[3][1] * center.y ) +
		matrix[3][2] * center.z ) + matrix[3][3];
	const float wRadius = ( ( matrix[3][0] * up.x + matrix[3][1] * up.y ) +
		matrix[3][2] * up.z ) * radius;
	const float plusW = wRadius + wCenter;
	const float plusY = yRadius + yCenter;
	const float plusInverse = !( plusW >= 0.001f ) ? 1000.0f : 1.0f / plusW;
	const float plusProjected = plusInverse * plusY;
	const float minusW = wCenter - wRadius;
	const float minusY = yCenter - yRadius;
	const float minusInverse = !( minusW >= 0.001f ) ? 1000.0f : 1.0f / minusW;
	return fabs( static_cast< double >( minusInverse ) * minusY - plusProjected ) *
		state.m_nHeight * 0.5;
}

// Whole BBE10 over the eligible property rows. The actual local user/FOV is
// queried ONCE, after every B89B0 query, not once per leaf or per entity.
void CClientLeafSystem::ApplyEntityDistanceFade( const SetupRenderInfo_t &setup,
	L4D2EntityRenderDataList_t &rows )
{
	float fovSquared = 1.0f;
	C_BasePlayer *local = C_BasePlayer::GetLocalPlayer();
	if ( local )
	{
		const double fov = local->GetFOVDistanceAdjustFactor();
		fovSquared = static_cast< float >( fov * fov );
	}
	const float globalMaximum = setup.m_flRenderDistSq;
	const float globalMinimum = globalMaximum * 0.90249997f;
	const float globalInverse = 1.0f / ( globalMaximum - globalMinimum );
	for ( int i = 0; i < rows.Count(); ++i )
	{
		L4D2EntityRenderData_t &row = rows[i];
		if ( row.m_bCulled ) continue;
		C_BaseEntity *entity = row.m_pAlphaOwner;
		const float distanceSquared = row.m_flDistanceSquared * fovSquared;
		bool useGlobalInverse = true;
		float minimum = globalMinimum;
		float maximum = globalMaximum;
		if ( entity->m_nRenderAlphaFadeMax != 0 )
		{
			const float entityMinimum = entity->m_nRenderAlphaFadeMin;
			const float minimumSquared = entityMinimum * entityMinimum;
			if ( globalMinimum > minimumSquared )
			{
				useGlobalInverse = false;
				minimum = minimumSquared;
			}
			const float entityMaximum = entity->m_nRenderAlphaFadeMax;
			const float maximumSquared = entityMaximum * entityMaximum;
			if ( globalMaximum > maximumSquared )
			{
				useGlobalInverse = false;
				maximum = maximumSquared;
			}
		}
		if ( minimum < distanceSquared )
		{
			if ( distanceSquared < maximum )
			{
				row.m_flFade = useGlobalInverse ?
					( maximum - distanceSquared ) * globalInverse :
					( maximum - distanceSquared ) / ( maximum - minimum );
			}
			else row.m_flFade = 0.0f;
		}
	}
}

// Whole BC8B0. Do not rewrite the guards as positive ordered tests: native
// COMISS/JNB admits NaN minimum/scale; COMISS/JB sends NaN maximum to full1.
void CClientLeafSystem::ApplyEntityScreenFade( const RenderAlphaScreenState_t &screen,
	float minimum, float maximum, L4D2EntityRenderDataList_t &rows )
{
	if ( minimum >= maximum ) maximum = minimum;
	if ( minimum <= 0.0f ) return;
	const float inverse = maximum == minimum ? 1.0f : 1.0f / ( maximum - minimum );
	for ( int i = 0; i < rows.Count(); ++i )
	{
		L4D2EntityRenderData_t &row = rows[i];
		if ( row.m_bCulled ) continue;
		const float scale = row.m_pAlphaOwner->m_flRenderAlphaFadeScale;
		if ( scale <= 0.0f ) continue;
		const double size = RenderAlphaProjectedSphereSize( row.m_vecCenter, row.m_flRadius, screen ) /
			scale * 2.0;
		float fade = 0.0f;
		if ( size > minimum )
		{
			const float spilledSize = static_cast< float >( size );
			if ( !( maximum >= 0.0f ) || !( maximum > spilledSize ) ) fade = 1.0f;
			else fade = ( spilledSize - minimum ) * inverse;
		}
		if ( fade > row.m_flFade ) fade = row.m_flFade;
		row.m_flFade = fade;
	}
}

// Whole BEBC0 alpha responsibilities on rows that actually have a game-owned
// property. All real bounds/frustum results have already been collected. Its
// only B89B0 queries happen in the first complete query pass below.
void CClientLeafSystem::ComputeEntityRenderAlpha( const SetupRenderInfo_t &setup,
	L4D2EntityRenderDataList_t &rows )
{
	for ( int i = 0; i < rows.Count(); ++i )
	{
		L4D2EntityRenderData_t &row = rows[i];
		if ( row.m_bCulled ) continue;
		row.m_pAlphaOwner = row.m_pInfo->m_pL4D2AlphaOwner;
		row.m_vecCenter.x = ( row.m_vecMins.x + row.m_vecMaxs.x ) * 0.5f;
		row.m_vecCenter.y = ( row.m_vecMins.y + row.m_vecMaxs.y ) * 0.5f;
		row.m_vecCenter.z = ( row.m_vecMins.z + row.m_vecMaxs.z ) * 0.5f;
		const float dx = row.m_vecCenter.x - row.m_vecMaxs.x;
		const float dy = row.m_vecCenter.y - row.m_vecMaxs.y;
		const float dz = row.m_vecCenter.z - row.m_vecMaxs.z;
		const float radiusSquared = ( dy * dy + dz * dz ) + dx * dx;
		row.m_flRadius = _mm_cvtss_f32( _mm_sqrt_ss( _mm_set_ss( radiusSquared ) ) );
		row.m_flFade = 1.0f;
	}
	for ( int i = 0; i < rows.Count(); ++i )
	{
		L4D2EntityRenderData_t &row = rows[i];
		if ( row.m_bCulled ) continue;
		row.m_nAlpha = row.m_pAlphaOwner->ComputeRenderAlpha();
		const int mode = ( row.m_pAlphaOwner->m_nRenderAlphaFlags >> 5 ) & 0x0F;
		row.m_bIgnoreZ = mode == 3 || mode == 9;
	}

	// Native view1 is 3Dsky. These are the genuine existing module/cvar/view
	// owners, not private replacements or a second once-per-frame scope.
	if ( !g_MakingDevShots && cl_leveloverview.GetInt() == 0 && setup.m_nViewID != VIEW_3DSKY )
	{
		ApplyEntityDistanceFade( setup, rows );
		RenderAlphaScreenState_t screen;
		CaptureRenderAlphaScreenState( screen );
		float minimum, maximum;
		modelinfo->GetLevelScreenFadeRange( &minimum, &maximum );
		ApplyEntityScreenFade( screen, minimum, maximum, rows );
		view->GetScreenFadeDistances( &minimum, &maximum );
		ApplyEntityScreenFade( screen, minimum, maximum, rows );
		for ( int i = 0; i < rows.Count(); ++i )
		{
			L4D2EntityRenderData_t &row = rows[i];
			if ( row.m_bCulled ) continue;
			const float product = static_cast< float >( row.m_nAlpha ) * row.m_flFade;
			int alpha = _mm_cvtt_ss2si( _mm_set_ss( product ) );
			if ( alpha > 255 ) alpha = 255;
			else if ( alpha < 0 ) alpha = 0;
			row.m_nAlpha = static_cast< unsigned char >( alpha );
		}
	}

	// Every shadow is updated BEFORE any alpha0 entry is dropped, and before
	// the later occlusion pass. Native400 callback receives EFFECTIVE alpha.
	for ( int i = 0; i < rows.Count(); ++i )
	{
		L4D2EntityRenderData_t &row = rows[i];
		if ( row.m_bCulled ) continue;
		C_BaseEntity *entity = row.m_pAlphaOwner;
		if ( entity->m_hRenderAlphaShadow != CLIENTSHADOW_INVALID_HANDLE )
		{
			unsigned char shadowAlpha = row.m_nAlpha;
			if ( entity->m_nRenderAlphaFlags & 0x0400 )
				shadowAlpha = entity->OverrideShadowAlphaModulation( shadowAlpha );
			g_pClientShadowMgr->SetFalloffBias( entity->m_hRenderAlphaShadow,
				static_cast< unsigned char >( 255 - shadowAlpha ) );
		}
	}
	for ( int i = 0; i < rows.Count(); ++i )
	{
		L4D2EntityRenderData_t &row = rows[i];
		if ( !row.m_bCulled && row.m_nAlpha == 0 ) row.m_bCulled = true;
	}
}

// Native BDB10 responsibility after the completed alpha batch. This is a
// reference to the genuine engine-owned cvar, not a newly defined control.
void CClientLeafSystem::CullOccludedEntityRenderables( L4D2EntityRenderDataList_t &rows )
{
	static ConVarRef occlusion( "r_occlusion" );
	if ( !occlusion.GetBool() ) return;
	for ( int i = 0; i < rows.Count(); ++i )
	{
		L4D2EntityRenderData_t &row = rows[i];
		if ( !row.m_bCulled && ( row.m_pInfo->m_Flags & RENDER_FLAGS_STUDIO_MODEL ) &&
			engine->IsOccluded( row.m_vecMins, row.m_vecMaxs ) )
		{
			row.m_bCulled = true;
		}
	}
}

#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
#endif

#if defined( L4D2 )
void CClientLeafSystem::AddEntityRenderable( C_BaseEntity *entity,
	bool renderWithViewModels, int type, bool drawForLocalUser )
{
	// NewRenderable is the existing allocation/model-query/VM-list owner.
	// SDK bucket here is only initial storage; native0/1/2 stays independent.
	NewRenderable( entity, renderWithViewModels ? RENDER_GROUP_VIEW_MODEL_OPAQUE :
		RENDER_GROUP_OPAQUE_ENTITY, RENDER_FLAGS_HASCHANGED );
	const ClientRenderHandle_t handle = entity->GetRenderHandle();
	RenderableInfo_t &info = m_Renderables[handle];
	info.m_nL4D2TranslucencyType = type & 3;
	info.m_bL4D2DrawForLocalUser = drawForLocalUser;
	UpdateSDKRenderGroupForEntity( info );
	m_DirtyRenderables.AddToTail( handle );
}
#endif

#if defined( L4D2 )
void CClientLeafSystem::UpdateModelType( ClientRenderHandle_t handle )
{
	if ( handle == INVALID_CLIENT_RENDER_HANDLE )
		return;
	RenderableInfo_t &info = m_Renderables[handle];
	const int modelType = modelinfo->GetModelType( info.m_pRenderable->GetModel() );
	const int modelFlags = modelType == mod_brush ? RENDER_FLAGS_BRUSH_MODEL :
		( modelType == mod_studio ? RENDER_FLAGS_STUDIO_MODEL : 0 );
	const int mask = RENDER_FLAGS_BRUSH_MODEL | RENDER_FLAGS_STUDIO_MODEL;
	if ( ( info.m_Flags & mask ) != modelFlags )
	{
		info.m_Flags = ( info.m_Flags & ~mask ) | modelFlags;
		RenderableChanged( handle );
	}
}
#endif

#if defined( L4D2 )
void ClientLeafSystem_AddEntityRenderable( C_BaseEntity *entity,
	bool renderWithViewModels, int type, bool drawForLocalUser )
{
	CClientLeafSystem::s_ClientLeafSystem.AddEntityRenderable(
		entity, renderWithViewModels, type, drawForLocalUser );
}
#endif

#if defined( L4D2 )
void ClientLeafSystem_UpdateModelType( ClientRenderHandle_t handle )
{
	CClientLeafSystem::s_ClientLeafSystem.UpdateModelType( handle );
}
#endif
