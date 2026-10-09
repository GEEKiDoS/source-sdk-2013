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
#include "tier1/utlmap.h"
#include "gamebspfile.h"
#include "datacache/imdlcache.h"
#include "view.h"
#include "viewrender.h"
#include "shadowmaps_dx12.h"
#include "materialsystem/imaterial.h"
#include "c_physicsprop.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

class VMatrix;  // forward decl

static ConVar cl_drawleaf("cl_drawleaf", "-1", FCVAR_CHEAT );
static ConVar r_PortalTestEnts( "r_PortalTestEnts", "1", FCVAR_CHEAT, "Clip entities against portal frustums." );
static ConVar r_portalsopenall( "r_portalsopenall", "0", FCVAR_CHEAT, "Open all portals" );
static ConVar cl_threaded_client_leaf_system("cl_threaded_client_leaf_system", "0"  );
static ConVar r_shadowmap_validate_casters( "r_shadowmap_validate_casters", "0", FCVAR_CHEAT, "Compare shadow caster queries with an exhaustive registration/AABB scan." );
static void ShadowPhysicsPolicyChanged( IConVar *pConVar, const char *pOldString, float flOldValue );
static ConVar r_shadowmap_force_physics_shadows( "r_shadowmap_force_physics_shadows", "0", FCVAR_ARCHIVE,
	"Allow solid ordinary physics props to cast native shadowmaps despite authored DisableShadow (does not change entity effects).",
	ShadowPhysicsPolicyChanged );


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

//-----------------------------------------------------------------------------
// The client leaf system
//-----------------------------------------------------------------------------
class CClientLeafSystem : public IClientLeafSystem, public ISpatialLeafEnumerator
{
public:
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
	virtual void ComputeTranslucentRenderLeaf( int count, const LeafIndex_t *pLeafList, const LeafFogVolume_t *pLeafFogVolumeList, int frameNumber, int viewID );
	virtual void CollateViewModelRenderables( CUtlVector< IClientRenderable * >& opaque, CUtlVector< IClientRenderable * >& translucent );
	virtual void BuildRenderablesList( const SetupRenderInfo_t &info );
			void CollateRenderablesInLeaf( int leaf, int worldListLeafIndex, const SetupRenderInfo_t &info );
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
	virtual void EnumerateShadowCasters( const ShadowCasterVolume_t &volume, IShadowCasterSink &sink );
	virtual void EnumerateShadowCastersExhaustive( const ShadowCasterVolume_t &volume, IShadowCasterSink &sink );
	void OnShadowPhysicsPolicyChanged();

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
	void InsertIntoTree( ClientRenderHandle_t &handle );
	void RemoveFromTree( ClientRenderHandle_t handle );

	// Runtime shadow maps use their own stamps and registration bookkeeping.
	void UpdateUnlinkedShadowCaster( ClientRenderHandle_t handle );
	void RemoveUnlinkedShadowCaster( ClientRenderHandle_t handle );
	void UpdateShadowCasterBounds( ClientRenderHandle_t handle );
	void RecordShadowCasterBounds( ClientRenderHandle_t handle, const Vector &mins, const Vector &maxs );
	const char *InitShadowStaticPropPolicy();
	bool IsEligibleShadowCaster( ClientRenderHandle_t handle );
	void CollectShadowCaster( ClientRenderHandle_t handle, const ShadowCasterVolume_t &volume );
	bool EnumerateShadowCasterLeaf( int leaf, const ShadowCasterVolume_t &volume );
	void ValidateShadowCasters( const ShadowCasterVolume_t &volume );
	void BeginShadowCasterQuery( const ShadowCasterVolume_t &volume );
	void EmitShadowCasters( IShadowCasterSink &sink );
	void InvalidateShadowCasterQuery()
	{
		if ( ++m_ShadowCasterGeneration==0 ) ++m_ShadowCasterGeneration;
		m_CachedShadowCasterFrame=-1;
	}

	class CShadowCasterLeafEnumerator : public ISpatialLeafEnumerator
	{
	public:
		CShadowCasterLeafEnumerator( CClientLeafSystem &system, const ShadowCasterVolume_t &volume ) : m_System( system ), m_Volume( volume ) {}
		virtual bool EnumerateLeaf( int leaf, intp ) { return m_System.EnumerateShadowCasterLeaf( leaf, m_Volume ); }
	private:
		CClientLeafSystem &m_System;
		const ShadowCasterVolume_t &m_Volume;
	};

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
		RENDER_FLAGS_PHYSICS_PROP = 0x40,
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
		uint64				m_ShadowCasterTestStamp;
		uint64				m_ShadowCasterReportStamp;
		int					m_UnlinkedShadowCasterIndex;
		Vector				m_ShadowCasterMins, m_ShadowCasterMaxs;
		bool				m_bShadowCasterBoundsValid;
		bool				m_bUpdatingShadowCasterBounds;
	};

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

	// Capacity is retained between lights; query results never use render-group limits.
	CUtlVector< ClientRenderHandle_t >	m_UnlinkedShadowCasters;
	CUtlVector< ClientRenderHandle_t >	m_ShadowCasters[2];	// entities, then contiguous engine static props
	CUtlVector< IMaterial * >			m_ShadowCasterMaterials;
	// Authored sprp flags keyed by the engine's stable public collideable identity.
	CUtlMap< ICollideable *, bool, int >	m_ShadowStaticPropPolicy;
	uint64								m_ShadowCasterQueryStamp;
	// Retain every first-visited registration, including ineligible casters.
	// Reuse only spatial traversal: eligibility, live bounds and sink content
	// are evaluated again, since materials/opacity need not relink a handle.
	CUtlVector< ClientRenderHandle_t >	m_ShadowCasterCandidates;
	uint64								m_ShadowCasterGeneration, m_CachedShadowCasterGeneration;
	int									m_CachedShadowCasterFrame;
	ShadowCasterVolume_t				m_CachedShadowCasterVolume;
	bool								m_bRecordShadowCasterCandidates;

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

static void ShadowPhysicsPolicyChanged( IConVar *pConVar, const char *pOldString, float flOldValue )
{
	ConVarRef var( pConVar );
	if ( var.GetBool() == ( atoi( pOldString ) != 0 ) || !g_pClientLeafSystem )
		return;
	CClientLeafSystem::s_ClientLeafSystem.OnShadowPhysicsPolicyChanged();
}

void CClientLeafSystem::OnShadowPhysicsPolicyChanged()
{
	InvalidateShadowCasterQuery();
	if ( !ShadowMapsDX12_Active() )
		return;
	for ( ClientRenderHandle_t handle = m_Renderables.Head(); handle != m_Renderables.InvalidIndex(); handle = m_Renderables.Next( handle ) )
	{
		if ( !( m_Renderables[handle].m_Flags & RENDER_FLAGS_PHYSICS_PROP ) )
			continue;
		IClientRenderable *pRenderable = m_Renderables[handle].m_pRenderable;
		C_PhysicsProp *pPhysicsProp = static_cast<C_PhysicsProp *>( pRenderable->GetIClientUnknown()->GetBaseEntity() );
		if ( !pPhysicsProp->CanOverrideShadowMapNoShadow() || !pPhysicsProp->IsEffectActive( EF_NOSHADOW ) )
			continue;
		// Invalidate membership and both rigid/overlay depth at the existing bounds,
		// even when an already-loaded/restored prop has not moved this frame.
		ShadowMapsDX12_InvalidateCasterRegistration( pRenderable );
		RenderableChanged( handle );
	}
}

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


//-----------------------------------------------------------------------------
// constructor, destructor
//-----------------------------------------------------------------------------
CClientLeafSystem::CClientLeafSystem() : m_ShadowStaticPropPolicy( DefLessFunc( ICollideable * ) ), m_ShadowCasterQueryStamp(0),
	m_ShadowCasterGeneration(0), m_CachedShadowCasterGeneration(0), m_CachedShadowCasterFrame(-1), m_bRecordShadowCasterCandidates(false),
	m_DrawStaticProps(true), m_DrawSmallObjects(true)
{
	// Set up the bi-directional lists...
	m_RenderablesInLeaf.Init( FirstRenderableInLeaf, FirstLeafInRenderable );
	m_ShadowsInLeaf.Init( FirstShadowInLeaf, FirstLeafInShadow ); 
	m_ShadowsOnRenderable.Init( FirstShadowOnRenderable, FirstRenderableInShadow );
}

// sprp arrays are count-prefixed. Check before advancing or multiplying, rather
// than trusting the engine's deserializer to validate authored caster metadata.
static bool ReadShadowStaticPropArray( const uint8 *bytes, int size, int &offset, int stride,
	int &count, const uint8 *&records )
{
	if ( offset > size || size - offset < sizeof( int ) )
		return false;
	memcpy( &count, bytes + offset, sizeof( count ) );
	offset += sizeof( count );
	if ( count < 0 || count > ( size - offset ) / stride )
		return false;
	records = bytes + offset;
	offset += count * stride;
	return true;
}

template< class T >
static void ReadShadowStaticPropRecord( const uint8 *bytes, StaticPropLump_t &prop )
{
	T record;
	memcpy( &record, bytes, sizeof( record ) );
	prop = record;
}

const char *CClientLeafSystem::InitShadowStaticPropPolicy()
{
	if ( !staticpropmgr )
		return SHADOWMAP_ERR_INVALID_METADATA ": static-prop manager unavailable for authored caster policy";

	CUtlVector< ICollideable * > props;
	staticpropmgr->GetAllStaticProps( &props );
	const int size = engine->GameLumpSize( GAMELUMP_STATIC_PROPS );
	if ( size == 0 && props.Count() == 0 )
		return NULL;
	if ( size <= 0 )
		return SHADOWMAP_ERR_INVALID_METADATA ": static-prop caster policy missing sprp data";

	const int version = engine->GameLumpVersion( GAMELUMP_STATIC_PROPS );
	int stride;
	switch ( version )
	{
	case 4: stride = sizeof( StaticPropLumpV4_t ); break;
	case 5: stride = sizeof( StaticPropLumpV5_t ); break;
	case 6: stride = sizeof( StaticPropLumpV6_t ); break;
	// SDK engine promotes v7 to v10; both use this SDK's widened flags layout.
	case 7:
	case 10: stride = sizeof( StaticPropLump_t ); break;
	default:
		return SHADOWMAP_ERR_INVALID_METADATA ": unsupported sprp version for authored caster policy (supported: 4, 5, 6, 7, 10)";
	}

	CUtlMemory< uint8 > bytes;
	bytes.EnsureCapacity( size );
	if ( !engine->LoadGameLump( GAMELUMP_STATIC_PROPS, bytes.Base(), size ) )
		return SHADOWMAP_ERR_INVALID_METADATA ": cannot load engine-resolved sprp caster policy";

	int offset = 0, dictCount, leafCount, propCount;
	const uint8 *records;
	if ( !ReadShadowStaticPropArray( bytes.Base(), size, offset, sizeof( StaticPropDictLump_t ), dictCount, records ) ||
		!ReadShadowStaticPropArray( bytes.Base(), size, offset, sizeof( StaticPropLeafLump_t ), leafCount, records ) ||
		!ReadShadowStaticPropArray( bytes.Base(), size, offset, stride, propCount, records ) || offset != size )
		return SHADOWMAP_ERR_INVALID_METADATA ": malformed sprp arrays for authored caster policy";
	if ( propCount != props.Count() )
		return SHADOWMAP_ERR_INVALID_METADATA ": sprp caster policy and public static-prop counts differ";

	m_ShadowStaticPropPolicy.EnsureCapacity( propCount );
	for ( int i = 0; i < propCount; ++i )
	{
		StaticPropLump_t prop;
		const uint8 *record = records + i * stride;
		switch ( version )
		{
		case 4: ReadShadowStaticPropRecord< StaticPropLumpV4_t >( record, prop ); break;
		case 5: ReadShadowStaticPropRecord< StaticPropLumpV5_t >( record, prop ); break;
		case 6: ReadShadowStaticPropRecord< StaticPropLumpV6_t >( record, prop ); break;
		case 7:
		case 10: ReadShadowStaticPropRecord< StaticPropLump_t >( record, prop ); break;
		}
		if ( prop.m_PropType >= dictCount || prop.m_FirstLeaf > leafCount ||
			prop.m_LeafCount > leafCount - prop.m_FirstLeaf )
			return SHADOWMAP_ERR_INVALID_METADATA ": malformed sprp record for authored caster policy";

		// Engine GetAllStaticProps enumerates the same disk-indexed vector as
		// GetStaticPropByIndex. Compare the public interfaces, never addresses
		// inferred from private object strides, model names or positions.
		ICollideable *pProp = staticpropmgr->GetStaticPropByIndex( i );
		if ( !pProp || pProp != props[i] || m_ShadowStaticPropPolicy.Find( pProp ) != m_ShadowStaticPropPolicy.InvalidIndex() )
			return SHADOWMAP_ERR_INVALID_METADATA ": public static-prop caster identity mismatch";
		m_ShadowStaticPropPolicy.Insert( pProp, ( prop.m_Flags & STATIC_PROP_NO_SHADOW ) == 0 );
		ShadowMapsDX12_RegisterStaticPropReceiver( i, pProp, prop );
	}
	return NULL;
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

	m_ShadowStaticPropPolicy.Purge();
	if ( ShadowMapsDX12_MapState().featureMap )
	{
		const char *error = InitShadowStaticPropPolicy();
		if ( error )
		{
			m_ShadowStaticPropPolicy.Purge();
			ShadowMapsDX12_RejectMap( error );
		}
	}

	m_Renderables.EnsureCapacity( 1024 );
	m_RenderablesInLeaf.EnsureCapacity( 1024 );
	m_ShadowsInLeaf.EnsureCapacity( 256 );
	m_ShadowsOnRenderable.EnsureCapacity( 256 );
	m_DirtyRenderables.EnsureCapacity( 256 );
	m_UnlinkedShadowCasters.EnsureCapacity( 1024 );
	m_ShadowCasters[0].EnsureCapacity( 1024 );
	m_ShadowCasters[1].EnsureCapacity( 1024 );
	m_ShadowCasterCandidates.EnsureCapacity( 1024 );

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
	m_UnlinkedShadowCasters.Purge();
	m_ShadowCasters[0].Purge();
	m_ShadowCasters[1].Purge();
	m_ShadowCasterMaterials.Purge();
	m_ShadowStaticPropPolicy.Purge();
	m_ShadowCasterQueryStamp = 0;
	m_ShadowCasterCandidates.Purge();
	InvalidateShadowCasterQuery();
	m_bRecordShadowCasterCandidates=false;
}


//-----------------------------------------------------------------------------
// This is what happens before rendering a particular view
//-----------------------------------------------------------------------------
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
			UpdateUnlinkedShadowCaster( handle );
		}

		m_DirtyRenderables.RemoveMultiple( 0, nDirty );
	}
}


//-----------------------------------------------------------------------------
// Creates a new renderable
//-----------------------------------------------------------------------------
void CClientLeafSystem::NewRenderable( IClientRenderable* pRenderable, RenderGroup_t type, int flags )
{
	Assert( pRenderable );
	Assert( pRenderable->RenderHandle() == INVALID_CLIENT_RENDER_HANDLE );
	InvalidateShadowCasterQuery();
	ShadowMapsDX12_InvalidateCasterRegistration( pRenderable );

	// Cache only network class identity, never the received ordinary-prop bit:
	// render registration may precede its first network receive/save restoration.
	IClientUnknown *pUnknown = pRenderable->GetIClientUnknown();
	C_BaseEntity *pEntity = pUnknown ? pUnknown->GetBaseEntity() : NULL;
	ClientClass *pClientClass = pEntity ? pEntity->GetClientClass() : NULL;
	if ( pClientClass && ( FStrEq( pClientClass->GetName(), "CPhysicsProp" ) ||
		FStrEq( pClientClass->GetName(), "CPhysicsPropMultiplayer" ) ) )
	{
		flags |= RENDER_FLAGS_PHYSICS_PROP;
	}
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
	info.m_ShadowCasterTestStamp = 0;
	info.m_ShadowCasterReportStamp = 0;
	info.m_UnlinkedShadowCasterIndex = -1;
	info.m_bShadowCasterBoundsValid = false;
	info.m_bUpdatingShadowCasterBounds = false;
	if ( IsViewModelRenderGroup( (RenderGroup_t)info.m_RenderGroup ) )
	{
		AddToViewModelList( handle );
	}

	pRenderable->RenderHandle() = handle;
	// Reserve at registration time, not once per light or in a bounded draw group.
	int nRenderableCount = m_Renderables.Count();
	if ( nRenderableCount > m_ShadowCasters[0].NumAllocated() )
	{
		int nCapacity = MAX( nRenderableCount, MAX( 1024, m_ShadowCasters[0].NumAllocated() * 2 ) );
		m_UnlinkedShadowCasters.EnsureCapacity( nCapacity );
		m_ShadowCasters[0].EnsureCapacity( nCapacity );
		m_ShadowCasters[1].EnsureCapacity( nCapacity );
		m_ShadowCasterCandidates.EnsureCapacity( nCapacity );
	}
	UpdateUnlinkedShadowCaster( handle );
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
	UpdateShadowCasterBounds( pRenderable->RenderHandle() );
}


//-----------------------------------------------------------------------------
// Used to change renderables from translucent to opaque
//-----------------------------------------------------------------------------
void CClientLeafSystem::ChangeRenderableRenderGroup( ClientRenderHandle_t handle, RenderGroup_t group )
{
	RenderableInfo_t &info = m_Renderables[handle];
	if ( info.m_RenderGroup!=(unsigned char)group )
	{
		InvalidateShadowCasterQuery();
		ShadowMapsDX12_InvalidateCasterRegistration( info.m_pRenderable );
	}
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
	UpdateShadowCasterBounds( handle );
}

void CClientLeafSystem::RemoveRenderable( ClientRenderHandle_t handle )
{
	// This can happen upon level shutdown
	if (!m_Renderables.IsValidIndex(handle))
		return;
	InvalidateShadowCasterQuery();
	ShadowMapsDX12_InvalidateCasterRegistration( m_Renderables[handle].m_pRenderable );

	// Invalidate both the last observed bounds and the current bounds before unlinking.
	UpdateShadowCasterBounds( handle );
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
	RemoveUnlinkedShadowCaster( handle );
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
	InvalidateShadowCasterQuery();
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
	RemoveUnlinkedShadowCaster( renderable );

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
	UpdateUnlinkedShadowCaster( handle );
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
	if ( ThreadInMainThread() )
	{
		UpdateUnlinkedShadowCaster( handle );
		// Some entity setters mark the handle dirty before committing the transform.
		// Reconcile at the ordinary flush as well as at the immediate notification.
		UpdateShadowCasterBounds( handle );
	}
}

//-----------------------------------------------------------------------------
// Removes an element from the tree
//-----------------------------------------------------------------------------
void CClientLeafSystem::RemoveFromTree( ClientRenderHandle_t handle )
{
	InvalidateShadowCasterQuery();
	m_RenderablesInLeaf.RemoveElement( handle );
	UpdateUnlinkedShadowCaster( handle );

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
void CClientLeafSystem::RenderableChanged( ClientRenderHandle_t handle )
{
	Assert ( handle != INVALID_CLIENT_RENDER_HANDLE );
	Assert( m_Renderables.IsValidIndex( handle ) );
	if ( !m_Renderables.IsValidIndex( handle ) )
		return;
	InvalidateShadowCasterQuery();

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
	UpdateUnlinkedShadowCaster( handle );
	// Bounds can change without moving the root (bones, parenting or opacity/pose).
	UpdateShadowCasterBounds( handle );
}

//-----------------------------------------------------------------------------
// Independent runtime shadow caster bookkeeping and spatial enumeration.
//-----------------------------------------------------------------------------
void CClientLeafSystem::RemoveUnlinkedShadowCaster( ClientRenderHandle_t handle )
{
	RenderableInfo_t &info = m_Renderables[handle];
	int index = info.m_UnlinkedShadowCasterIndex;
	if ( index < 0 )
		return;

	ClientRenderHandle_t tail = m_UnlinkedShadowCasters.Tail();
	m_UnlinkedShadowCasters.FastRemove( index );
	if ( index < m_UnlinkedShadowCasters.Count() )
	{
		m_Renderables[tail].m_UnlinkedShadowCasterIndex = index;
	}
	info.m_UnlinkedShadowCasterIndex = -1;
	InvalidateShadowCasterQuery();
}

void CClientLeafSystem::UpdateUnlinkedShadowCaster( ClientRenderHandle_t handle )
{
	RenderableInfo_t &info = m_Renderables[handle];
	if ( info.m_LeafList != m_RenderablesInLeaf.InvalidIndex() )
	{
		RemoveUnlinkedShadowCaster( handle );
	}
	else if ( info.m_UnlinkedShadowCasterIndex < 0 )
	{
		info.m_UnlinkedShadowCasterIndex = m_UnlinkedShadowCasters.AddToTail( handle );
		InvalidateShadowCasterQuery();
	}
}

void CClientLeafSystem::UpdateShadowCasterBounds( ClientRenderHandle_t handle )
{
	// Ordinary maps must not trigger hitbox/bone work from registration notifications.
	if ( !ShadowMapsDX12_Active() )
		return;

	// Following actors can set up their parent's bones while obtaining world bounds.
	// A nested RenderableChanged still marks the registration dirty, but must not recurse.
	if ( m_Renderables[handle].m_bUpdatingShadowCasterBounds )
		return;

	IClientRenderable *pRenderable = m_Renderables[handle].m_pRenderable;
	m_Renderables[handle].m_bUpdatingShadowCasterBounds = true;
	Vector newMins, newMaxs;
	CalcRenderableWorldSpaceAABB( pRenderable, newMins, newMaxs );

	// World-bound callbacks may allocate registrations. Do not retain a linked-list
	// element reference across them (its backing storage can move).
	if ( !m_Renderables.IsValidIndex( handle ) || m_Renderables[handle].m_pRenderable != pRenderable )
		return;
	m_Renderables[handle].m_bUpdatingShadowCasterBounds = false;
	RecordShadowCasterBounds( handle, newMins, newMaxs );
}

void CClientLeafSystem::RecordShadowCasterBounds( ClientRenderHandle_t handle, const Vector &mins, const Vector &maxs )
{
	if ( !ShadowMapsDX12_Active() )
		return;
	InvalidateShadowCasterQuery();

	RenderableInfo_t &info = m_Renderables[handle];
	Vector oldMins = info.m_bShadowCasterBoundsValid ? info.m_ShadowCasterMins : mins;
	Vector oldMaxs = info.m_bShadowCasterBoundsValid ? info.m_ShadowCasterMaxs : maxs;
	info.m_ShadowCasterMins = mins;
	info.m_ShadowCasterMaxs = maxs;
	info.m_bShadowCasterBoundsValid = true;
	// Deliberately notify even for identical bounds: animation/alpha changes can
	// alter a silhouette without altering the root's bounding box.
	ShadowMapsDX12_OnCasterMoved( info.m_pRenderable, oldMins, oldMaxs, mins, maxs );
}

bool CClientLeafSystem::IsEligibleShadowCaster( ClientRenderHandle_t handle )
{
	if ( handle == DETAIL_PROP_RENDER_HANDLE || !m_Renderables.IsValidIndex( handle ) )
		return false;

	IClientRenderable *pRenderable = m_Renderables[handle].m_pRenderable;
	unsigned char flags = m_Renderables[handle].m_Flags;
	RenderGroup_t group = (RenderGroup_t)m_Renderables[handle].m_RenderGroup;
	if ( !pRenderable || pRenderable->RenderHandle() == DETAIL_PROP_RENDER_HANDLE || IsViewModelRenderGroup( group ) )
		return false;

	IClientUnknown *pUnknown = pRenderable->GetIClientUnknown();
	C_BaseEntity *pEntity = pUnknown ? pUnknown->GetBaseEntity() : NULL;
	if ( pEntity && pEntity->IsEffectActive( EF_NODRAW ) )
		return false;
	if ( pEntity && pEntity->IsEffectActive( EF_NOSHADOW ) &&
		!( ShadowMapsDX12_Active() && r_shadowmap_force_physics_shadows.GetBool() &&
		   ( flags & RENDER_FLAGS_PHYSICS_PROP ) &&
		   static_cast<C_PhysicsProp *>( pEntity )->CanOverrideShadowMapNoShadow() ) )
		return false;

	if ( ( flags & RENDER_FLAGS_STATIC_PROP ) && ShadowMapsDX12_MapState().featureMap )
	{
		ICollideable *pCollideable = pUnknown ? pUnknown->GetCollideable() : NULL;
		int policy = m_ShadowStaticPropPolicy.Find( pCollideable );
		if ( policy == m_ShadowStaticPropPolicy.InvalidIndex() )
		{
			ShadowMapsDX12_RejectMap( SHADOWMAP_ERR_INVALID_METADATA ": static-prop caster identity is not in the admitted sprp policy" );
			return false;
		}
		if ( !m_ShadowStaticPropPolicy[policy] )
			return false;
	}

	const model_t *pModel = pRenderable->GetModel();
	int modelType = pModel ? modelinfo->GetModelType( pModel ) : mod_bad;
	bool bTwoPass = ( flags & RENDER_FLAGS_TWOPASS ) != 0 || pRenderable->IsTwoPass();
	if ( pModel && ( modelType == mod_studio || modelType == mod_brush ) )
	{
		bTwoPass = bTwoPass || modelinfo->IsTranslucentTwoPass( pModel );
	}
	if ( bTwoPass )
		return true;

	// Do not use the ordinary render bucket or FxBlend here: distance-faded engine
	// static props can be in its translucent bucket while their materials are opaque.
	bool bTranslucent = pModel && modelinfo->IsTranslucent( pModel );
	if ( !( flags & RENDER_FLAGS_STATIC_PROP ) )
	{
		bTranslucent = bTranslucent || pRenderable->IsTransparent();
	}
	if ( !bTranslucent && !pRenderable->UsesPowerOfTwoFrameBufferTexture() )
		return true;

	// Actual cutout materials have a depth path even if the registration is marked
	// translucent. In particular, ShadowCastType()==SHADOWS_NONE does not disqualify
	// brush models; their ordinary brush depth draw does not use projected shadows.
	if ( !pModel )
		return false;
	int nMaterials = modelinfo->GetModelMaterialCount( pModel );
	m_ShadowCasterMaterials.SetCount( nMaterials );
	if ( nMaterials )
	{
		modelinfo->GetModelMaterials( pModel, nMaterials, m_ShadowCasterMaterials.Base() );
	}
	for ( int i = 0; i < nMaterials; ++i )
	{
		IMaterial *pMaterial = m_ShadowCasterMaterials[i];
		if ( pMaterial && !pMaterial->GetMaterialVarFlag( MATERIAL_VAR_NO_DRAW ) && pMaterial->IsAlphaTested() )
			return true;
	}
	return false;
}

static bool ShadowCasterIntersectsVolume( const Vector &mins, const Vector &maxs, const ShadowCasterVolume_t &volume )
{
	for ( int axis = 0; axis < 3; ++axis )
	{
		if ( maxs[axis] < volume.m_vecMins[axis] || mins[axis] > volume.m_vecMaxs[axis] )
			return false;
	}
	for ( int i = 0; i < volume.m_nPlaneCount; ++i )
	{
		const VPlane &plane = volume.m_Planes[i];
		Vector positiveVertex;
		for ( int axis = 0; axis < 3; ++axis )
		{
			positiveVertex[axis] = plane.m_Normal[axis] >= 0.0f ? maxs[axis] : mins[axis];
		}
		// Inward normals: only a box whose furthest vertex is behind the plane
		// is fully outside. Touching boxes and plane-straddling boxes participate.
		if ( plane.DistTo( positiveVertex ) < 0.0f )
			return false;
	}
	return true;
}

void CClientLeafSystem::CollectShadowCaster( ClientRenderHandle_t handle, const ShadowCasterVolume_t &volume )
{
	if ( handle == DETAIL_PROP_RENDER_HANDLE || !m_Renderables.IsValidIndex( handle ) )
		return;
	if ( m_Renderables[handle].m_ShadowCasterTestStamp == m_ShadowCasterQueryStamp )
		return;
	m_Renderables[handle].m_ShadowCasterTestStamp = m_ShadowCasterQueryStamp;
	if ( m_bRecordShadowCasterCandidates ) m_ShadowCasterCandidates.AddToTail( handle );
	if ( !IsEligibleShadowCaster( handle ) )
		return;

	IClientRenderable *pRenderable = m_Renderables[handle].m_pRenderable;
	Vector mins, maxs;
	// Never use cached registration bounds, m_RenderLeaf or the fast insertion
	// approximation for this test. Parented actors need their current world bounds.
	CalcRenderableWorldSpaceAABB( pRenderable, mins, maxs );
	if ( !m_Renderables.IsValidIndex( handle ) || m_Renderables[handle].m_pRenderable != pRenderable )
		return;
	if ( !m_Renderables[handle].m_bShadowCasterBoundsValid ||
		m_Renderables[handle].m_ShadowCasterMins != mins || m_Renderables[handle].m_ShadowCasterMaxs != maxs )
	{
		// Include transforms committed after RenderableChanged, even when this query
		// precedes the ordinary flush. This also invalidates the light the caster left.
		RecordShadowCasterBounds( handle, mins, maxs );
	}
	if ( !ShadowCasterIntersectsVolume( mins, maxs, volume ) )
		return;

	m_Renderables[handle].m_ShadowCasterReportStamp = m_ShadowCasterQueryStamp;
	int group = ( m_Renderables[handle].m_Flags & RENDER_FLAGS_STATIC_PROP ) ? 1 : 0;
	m_ShadowCasters[group].AddToTail( handle );
}

bool CClientLeafSystem::EnumerateShadowCasterLeaf( int leaf, const ShadowCasterVolume_t &volume )
{
	for ( unsigned int index = m_RenderablesInLeaf.FirstElement( leaf );
		index != m_RenderablesInLeaf.InvalidIndex(); index = m_RenderablesInLeaf.NextElement( index ) )
	{
		CollectShadowCaster( m_RenderablesInLeaf.Element( index ), volume );
	}
	return true;
}

void CClientLeafSystem::ValidateShadowCasters( const ShadowCasterVolume_t &volume )
{
	int nTested = 0, nExpected = 0, nMismatches = 0;
	for ( ClientRenderHandle_t handle = m_Renderables.Head(); handle != m_Renderables.InvalidIndex(); handle = m_Renderables.Next( handle ) )
	{
		++nTested;
		bool bExpected = IsEligibleShadowCaster( handle );
		Vector mins, maxs;
		CalcRenderableWorldSpaceAABB( m_Renderables[handle].m_pRenderable, mins, maxs );
		bExpected = bExpected && ShadowCasterIntersectsVolume( mins, maxs, volume );
		bool bReported = m_Renderables[handle].m_ShadowCasterReportStamp == m_ShadowCasterQueryStamp;
		if ( bExpected )
			++nExpected;
		if ( bExpected != bReported )
		{
			++nMismatches;
			Warning( "ShadowMapCasterValidationMismatch: handle=%u expected=%d reported=%d mins=(%g,%g,%g) maxs=(%g,%g,%g)\n",
				(unsigned int)handle, (int)bExpected, (int)bReported, mins.x, mins.y, mins.z, maxs.x, maxs.y, maxs.z );
		}
	}
	int nReported = m_ShadowCasters[0].Count() + m_ShadowCasters[1].Count();
	Msg( "ShadowMapCasterValidation: testedCount=%d reportedCount=%d exhaustiveCount=%d mismatchCount=%d exceeds4096=%d\n",
		nTested, nReported, nExpected, nMismatches, (int)( nReported > 4096 ) );
	Assert( nMismatches == 0 );
}

void CClientLeafSystem::EnumerateShadowCasters( const ShadowCasterVolume_t &volume, IShadowCasterSink &sink )
{
	VPROF_BUDGET( "EnumerateShadowCasters", VPROF_BUDGETGROUP_SHADOW_DEPTH_TEXTURING );

	// A stable registration/leaf generation proves the same first-visit order.
	// Do not cache eligibility or bounds: even unnotified material changes are
	// observed by CollectShadowCaster and EmitShadowCasters on every receiver.
	const uint64 generation=m_ShadowCasterGeneration;
	bool sameVolume=m_CachedShadowCasterFrame==gpGlobals->framecount &&
		m_CachedShadowCasterGeneration==generation &&
		m_CachedShadowCasterVolume.m_vecMins==volume.m_vecMins &&
		m_CachedShadowCasterVolume.m_vecMaxs==volume.m_vecMaxs &&
		m_CachedShadowCasterVolume.m_nPlaneCount==volume.m_nPlaneCount;
	for ( int p=0;sameVolume && p<volume.m_nPlaneCount;++p )
		sameVolume=m_CachedShadowCasterVolume.m_Planes[p].m_Normal==volume.m_Planes[p].m_Normal &&
			m_CachedShadowCasterVolume.m_Planes[p].m_Dist==volume.m_Planes[p].m_Dist;
	bool reused=false;
	if ( sameVolume && !r_shadowmap_validate_casters.GetBool() )
	{
		VPROF_BUDGET( "EnumerateShadowCasters.ReuseSpatialQuery", "Shadowmaps" );
		BeginShadowCasterQuery( volume );
		for ( int i=0;i<m_ShadowCasterCandidates.Count();++i )
			CollectShadowCaster( m_ShadowCasterCandidates[i], volume );
		// Bounds callbacks can register/relink casters while being queried.
		// Restart normal spatial enumeration if that proof changed mid-query.
		reused=m_ShadowCasterGeneration==generation;
	}
	if ( !reused )
	{
		m_ShadowCasterCandidates.RemoveAll();
		m_bRecordShadowCasterCandidates=true;
		{
			VPROF_BUDGET( "EnumerateShadowCasters.BeginQuery", "Shadowmaps" );
			BeginShadowCasterQuery( volume );
		}
		{
			VPROF_BUDGET( "EnumerateShadowCasters.SpatialQuery", "Shadowmaps" );
			CShadowCasterLeafEnumerator enumerator( *this, volume );
			engine->GetBSPTreeQuery()->EnumerateLeavesInBox( volume.m_vecMins, volume.m_vecMaxs, &enumerator, 0 );
		}

		// Callers flush ordinary registrations before querying, but the ten-iteration
		// warning and registrations/moves after that flush cannot authorize an omission.
		{
			VPROF_BUDGET( "EnumerateShadowCasters.DirtyAndUnlinked", "Shadowmaps" );
			for ( int i = 0; i < m_DirtyRenderables.Count(); ++i )
			{
				CollectShadowCaster( m_DirtyRenderables[i], volume );
			}
			for ( int i = 0; i < m_UnlinkedShadowCasters.Count(); ++i )
			{
				CollectShadowCaster( m_UnlinkedShadowCasters[i], volume );
			}
		}
		if ( r_shadowmap_validate_casters.GetBool() )
		{
			VPROF_BUDGET( "EnumerateShadowCasters.Validate", "Shadowmaps" );
			ValidateShadowCasters( volume );
		}
		m_bRecordShadowCasterCandidates=false;
		m_CachedShadowCasterVolume=volume;
		m_CachedShadowCasterGeneration=generation;
		m_CachedShadowCasterFrame=gpGlobals->framecount;
	}

	{
		VPROF_BUDGET( "EnumerateShadowCasters.Emit", "Shadowmaps" );
		EmitShadowCasters( sink );
	}
}

void CClientLeafSystem::EnumerateShadowCastersExhaustive( const ShadowCasterVolume_t &volume, IShadowCasterSink &sink )
{
	BeginShadowCasterQuery( volume );
	// Deliberately bypass leaves, dirty lists, unlinked lists and all camera state.
	for ( ClientRenderHandle_t handle = m_Renderables.Head(); handle != m_Renderables.InvalidIndex(); handle = m_Renderables.Next( handle ) )
	{
		CollectShadowCaster( handle, volume );
	}
	EmitShadowCasters( sink );
}

void CClientLeafSystem::BeginShadowCasterQuery( const ShadowCasterVolume_t &volume )
{
	Assert( volume.m_nPlaneCount >= 0 && volume.m_nPlaneCount <= ARRAYSIZE( volume.m_Planes ) );
	// This stamp is unrelated to camera render/detail frames or projected shadows.
	if ( ++m_ShadowCasterQueryStamp == 0 )
	{
		for ( ClientRenderHandle_t handle = m_Renderables.Head(); handle != m_Renderables.InvalidIndex(); handle = m_Renderables.Next( handle ) )
		{
			m_Renderables[handle].m_ShadowCasterTestStamp = 0;
			m_Renderables[handle].m_ShadowCasterReportStamp = 0;
		}
		++m_ShadowCasterQueryStamp;
	}
	m_ShadowCasters[0].RemoveAll();
	m_ShadowCasters[1].RemoveAll();
}

void CClientLeafSystem::EmitShadowCasters( IShadowCasterSink &sink )
{
	// Static registrations are contiguous, so consumers can issue their existing
	// 512-prop batches. Neither vector truncates when there are more than 4096.
	for ( int group = 0; group < ARRAYSIZE( m_ShadowCasters ); ++group )
	{
		for ( int i = 0; i < m_ShadowCasters[group].Count(); ++i )
		{
			ClientRenderHandle_t handle = m_ShadowCasters[group][i];
			if ( m_Renderables.IsValidIndex( handle ) )
			{
				sink.Add( m_Renderables[handle].m_pRenderable, group == 1 );
			}
		}
	}
}


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
	if ( pInfo->m_RenderGroup!=(unsigned char)group || ((pInfo->m_Flags&RENDER_FLAGS_TWOPASS)!=0)!=twoPass )
	{
		InvalidateShadowCasterQuery();
		ShadowMapsDX12_InvalidateCasterRegistration( pInfo->m_pRenderable );
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
			if ( info.m_TranslucencyCalculated != globalFrameCount || info.m_TranslucencyCalculatedView != viewID )
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
				if( pInfo->m_RenderGroup == RENDER_GROUP_TRANSLUCENT_ENTITY )
				{
					pInfo->m_RenderLeaf = leaf;
				}
				pInfo->m_RenderFrame = frameNumber;
			}
			else if ( pInfo->m_Flags & RENDER_FLAGS_ALTERNATE_SORTING )
			{
				if( pInfo->m_RenderGroup == RENDER_GROUP_TRANSLUCENT_ENTITY )
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
	int iLeaf, RenderGroup_t group,	ClientRenderHandle_t renderHandle, bool bTwoPass = false )
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

void CClientLeafSystem::CollateRenderablesInLeaf( int leaf, int worldListLeafIndex,	const SetupRenderInfo_t &info )
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
		CalcRenderableWorldSpaceAABB( renderable.m_pRenderable, absMins, absMaxs );
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
