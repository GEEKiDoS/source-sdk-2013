//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Deals with singleton  
//
// $Revision: $
// $NoKeywords: $
//=============================================================================//

#if !defined( DETAILOBJECTSYSTEM_H )
#define DETAILOBJECTSYSTEM_H
#ifdef _WIN32
#pragma once
#endif

#include "igamesystem.h"
#include "icliententityinternal.h"
#include "engine/ivmodelrender.h"
#include "mathlib/vector.h"
#include "ivrenderview.h"
#include "clientleafsystem.h"
#include "shadowmap_bsp.h"

struct model_t;
struct DetailPropLightstylesLump_t;
struct ShadowMapDepthFootprint_t;

//-----------------------------------------------------------------------------
// Receiver-view billboard orientation snapshot used by every shadow view of a
// receiver view (runtime shadow maps). Cards are oriented toward THIS view,
// never re-oriented per cube face / cascade. Fixed detail stays in clean depth;
// only orientation-dependent casters intersecting a volume affect its working depth.
//-----------------------------------------------------------------------------
struct ShadowMapDetailOrientation_t
{
	Vector	m_vecViewOrigin, m_vecViewForward, m_vecViewRight, m_vecViewUp;
	uint32	m_nGeneration;		// 0 = none; increments whenever the orientation basis changes
};


//-----------------------------------------------------------------------------
// Responsible for managing detail objects
//-----------------------------------------------------------------------------
abstract_class IDetailObjectSystem : public IGameSystem
{
public:
    // Gets a particular detail object
	virtual IClientRenderable* GetDetailModel( int idx ) = 0;

	// Gets called each view
	virtual void BuildDetailObjectRenderLists( const Vector &vViewOrigin ) = 0;

	// Renders all opaque detail objects in a particular set of leaves
	virtual void RenderOpaqueDetailObjects( int nLeafCount, LeafIndex_t *pLeafList ) = 0;

	// Call this before rendering translucent detail objects
	virtual void BeginTranslucentDetailRendering( ) = 0;

	// Renders all translucent detail objects in a particular set of leaves
	virtual void RenderTranslucentDetailObjects( const Vector &viewOrigin, const Vector &viewForward, const Vector &viewRight, const Vector &viewUp, int nLeafCount, LeafIndex_t *pLeafList ) =0;

	// Renders all translucent detail objects in a particular leaf up to a particular point
	virtual void RenderTranslucentDetailObjectsInLeaf( const Vector &viewOrigin, const Vector &viewForward, const Vector &viewRight, const Vector &viewUp, int nLeaf, const Vector *pVecClosestPoint ) = 0;

	// ---- Runtime shadow maps -------------------------------------------------------------------------
	// Detail lighting is the ordinary shared dprp/dplt/dplh data (the baker strips selected direct for both modes).

	// Snapshot the receiver view's billboard basis before any shadow view of that receiver view is built.
	virtual const ShadowMapDetailOrientation_t &SnapshotShadowOrientation( const Vector &viewOrigin, const Vector &viewForward, const Vector &viewRight, const Vector &viewUp ) = 0;

	// Shadow-only enumeration + draw of every detail caster whose leaf intersects the volume (all spatially
	// intersecting detail leaves; NOT the camera distance lists). Detail models go through the model depth
	// path; sprite cards use the snapshot orientation and the authored cutout alpha test (DX12_DetailShadowLit
	// shadow-only variant). Called inside an active shadow pass with the depth-write override in place.
	// Optional synchronous footprint feedback covers the actual submitted shadow geometry.
	virtual void DrawShadowCasters( const ShadowCasterVolume_t &volume, const ShadowMapDetailOrientation_t &orientation, bool orientationDependent, ShadowMapDepthFootprint_t *footprint ) = 0;

	// World AABB enclosing every detail caster (for the whole-map static sun map); false when none.
	virtual bool GetShadowCasterBounds( Vector &mins, Vector &maxs ) = 0;
	// Map-built spatial bounds index; no allocation or full-detail scan per view/light.
	virtual bool HasShadowCasters( const ShadowCasterVolume_t &volume, bool orientationDependent ) const = 0;

	// Whether the active map renders detail sprites through the feature-only lit material (converted mode).
	virtual bool UsesShadowLitSprites() const = 0;

	// Acceptance report (r_shadowmap_report): counts of fast / ordinary sprite objects and models, whether the
	// feature-lit sprite material is in use, and the last shadow-caster draw counts.
	struct ShadowReport_t
	{
		int		m_nFastSprites, m_nOrdinarySprites, m_nModels;
		bool	m_bShadowLitSprites;
		int		m_nLastDrawModels, m_nLastDrawCards;
		uint32	m_nOrientationGeneration;
	};
	virtual void GetShadowReport( ShadowReport_t &report ) const = 0;
};

//-----------------------------------------------------------------------------
// System for dealing with detail objects
//-----------------------------------------------------------------------------
IDetailObjectSystem* DetailObjectSystem();


#endif // DETAILOBJECTSYSTEM_H

