//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Raise brush lightmap density of an already-compiled BSP.
//
// VBSP fixes lightmap density per side ("lightmapscale") and splits any face
// whose lightmap would exceed MAX_BRUSH_LIGHTMAP_DIM_WITHOUT_BORDER luxels
// (utils/vbsp/faces.cpp:1167-1239). The engine, VRAD and this baker all rely
// on that invariant, so denser lightmaps mean re-splitting faces here with the
// same rule, then re-pointing every lump that indexes faces.
//
// Displacement faces keep their density: their luxel grid is baked into
// LUMP_DISP_LIGHTMAP_SAMPLE_POSITIONS/ALPHAS and uses a separate 125-luxel cap.
//
//=============================================================================//
#include "stdafx.h"
#include "restir_lightmap_rescale.h"
#include "vrad_restir.h"
#include "bsplib.h"
#include "polylib.h"
#include "tier1/utlvector.h"
#include "tier1/utlmap.h"

// utils/vbsp/faces.cpp:29-30
#define RESCALE_INTEGRAL_EPSILON	0.01f
#define RESCALE_POINT_EPSILON		0.1f

//-----------------------------------------------------------------------------
// Vertex/edge welding with VBSP's rules (faces.cpp:112-156 GetVertexnum,
// :895-921 GetEdge2): a vertex within POINT_EPSILON is reused; an edge whose
// reverse already exists with a free second face is shared as a negative
// surfedge.
//-----------------------------------------------------------------------------
class CRescaleGeometry
{
public:
	CRescaleGeometry();
	int FindOrAddVertex( const Vector &point );
	int FindOrAddEdge( int v1, int v2, int face );
	bool AppendSurfedge( int surfedge );

private:
	static unsigned int HashPoint( const Vector &point );

	CUtlVector<int> m_VertexChain;		// per vertex: next vertex in the same hash bucket
	CUtlVector<int> m_VertexBuckets;	// 4096 heads, -1 = empty
	CUtlVector< CUtlVector<int> > m_VertexEdges;	// per vertex: edges starting at that vertex
	CUtlVector<int> m_EdgeSecondFace;	// per edge: face using the reverse direction, -1 = none
};

CRescaleGeometry::CRescaleGeometry()
{
	m_VertexBuckets.SetCount( 4096 );
	for ( int i = 0; i < m_VertexBuckets.Count(); ++i )
	{
		m_VertexBuckets[i] = -1;
	}
	m_VertexChain.SetCount( numvertexes );
	m_VertexEdges.SetCount( numvertexes );
	for ( int i = 0; i < numvertexes; ++i )
	{
		const unsigned int bucket = HashPoint( dvertexes[i].point );
		m_VertexChain[i] = m_VertexBuckets[bucket];
		m_VertexBuckets[bucket] = i;
	}
	// Existing edges: both faces may already be assigned. Only the surfedge
	// tables know which; mark every existing edge as fully used so the loaded
	// map is never rewired, only extended.
	m_EdgeSecondFace.SetCount( numedges );
	for ( int i = 0; i < numedges; ++i )
	{
		m_EdgeSecondFace[i] = MAX_MAP_FACES;
	}
}

unsigned int CRescaleGeometry::HashPoint( const Vector &point )
{
	// utils/vbsp/faces.cpp:88-106 HashVec: 64-unit cells over x/y.
	const int x = ( (int)( point.x + 4096.0f ) >> 6 ) & 63;
	const int y = ( (int)( point.y + 4096.0f ) >> 6 ) & 63;
	return (unsigned int)( y * 64 + x );
}

int CRescaleGeometry::FindOrAddVertex( const Vector &source )
{
	Vector point = source;
	for ( int axis = 0; axis < 3; ++axis )
	{
		const float rounded = (float)(int)( point[axis] + 0.5f );
		if ( fabs( point[axis] - rounded ) < RESCALE_INTEGRAL_EPSILON )
		{
			point[axis] = rounded;
		}
	}
	const unsigned int bucket = HashPoint( point );
	for ( int vertex = m_VertexBuckets[bucket]; vertex >= 0; vertex = m_VertexChain[vertex] )
	{
		const Vector &existing = dvertexes[vertex].point;
		if ( fabs( existing.x - point.x ) < RESCALE_POINT_EPSILON &&
			fabs( existing.y - point.y ) < RESCALE_POINT_EPSILON &&
			fabs( existing.z - point.z ) < RESCALE_POINT_EPSILON )
		{
			return vertex;
		}
	}
	if ( numvertexes >= MAX_MAP_VERTS )
	{
		return -1;
	}
	const int vertex = numvertexes++;
	dvertexes[vertex].point = point;
	m_VertexChain.AddToTail( m_VertexBuckets[bucket] );
	m_VertexBuckets[bucket] = vertex;
	m_VertexEdges.AddToTail();
	return vertex;
}

int CRescaleGeometry::FindOrAddEdge( int v1, int v2, int face )
{
	// Share the reverse of an edge created by this pass (faces.cpp:901-918).
	const CUtlVector<int> &candidates = m_VertexEdges[v2];
	for ( int i = 0; i < candidates.Count(); ++i )
	{
		const int edge = candidates[i];
		if ( dedges[edge].v[0] == v2 && dedges[edge].v[1] == v1 && m_EdgeSecondFace[edge] < 0 )
		{
			m_EdgeSecondFace[edge] = face;
			return -edge;
		}
	}
	if ( numedges >= MAX_MAP_EDGES )
	{
		return 0;
	}
	const int edge = numedges++;
	dedges[edge].v[0] = (unsigned short)v1;
	dedges[edge].v[1] = (unsigned short)v2;
	m_EdgeSecondFace.AddToTail( -1 );
	m_VertexEdges[v1].AddToTail( edge );
	return edge;
}

bool CRescaleGeometry::AppendSurfedge( int surfedge )
{
	if ( numsurfedges >= MAX_MAP_SURFEDGES )
	{
		return false;
	}
	dsurfedges[numsurfedges++] = surfedge;
	return true;
}

//-----------------------------------------------------------------------------
// Face splitting
//-----------------------------------------------------------------------------
struct RescaleFace
{
	dface_t		face;		// template (texinfo, plane, flags); edges filled on emit
	winding_t	*winding;
	int			source;		// original dface index
};

// Port: utils/common/bsplib.cpp:3319-3350 (extent of a winding in luxel space).
static void LuxelExtent( const winding_t *winding, const texinfo_t &info, int axis, float &mins, float &maxs )
{
	mins = 1e24f;
	maxs = -1e24f;
	for ( int i = 0; i < winding->numpoints; ++i )
	{
		const float value = DotProduct( winding->p[i], *(Vector *)info.lightmapVecsLuxelsPerWorldUnits[axis] ) +
			info.lightmapVecsLuxelsPerWorldUnits[axis][3];
		mins = MIN( mins, value );
		maxs = MAX( maxs, value );
	}
}

// Port: utils/vbsp/faces.cpp:1167-1239 SubdivideFace. VBSP compares the raw
// luxel extent against g_maxLightmapDimension (32) and cuts at
// (mins + 31) / luxelsPerWorldUnit along the luxel axis.
static bool SubdivideFace( const RescaleFace &input, const texinfo_t &info, CUtlVector<RescaleFace> &output )
{
	for ( int axis = 0; axis < 2; ++axis )
	{
		float mins, maxs;
		LuxelExtent( input.winding, info, axis, mins, maxs );
		if ( maxs - mins <= (float)MAX_BRUSH_LIGHTMAP_DIM_WITHOUT_BORDER )
		{
			continue;
		}
		Vector direction( info.lightmapVecsLuxelsPerWorldUnits[axis][0],
			info.lightmapVecsLuxelsPerWorldUnits[axis][1], info.lightmapVecsLuxelsPerWorldUnits[axis][2] );
		const float luxelsPerWorldUnit = VectorNormalize( direction );
		// mins/maxs above include the texinfo offset; the clip plane must not.
		const float offset = info.lightmapVecsLuxelsPerWorldUnits[axis][3];
		const float distance = ( mins - offset + (float)MAX_BRUSH_LIGHTMAP_DIM_WITHOUT_BORDER - 1.0f ) / luxelsPerWorldUnit;
		winding_t *front = NULL;
		winding_t *back = NULL;
		ClipWindingEpsilon( input.winding, direction, distance, ON_EPSILON, &front, &back );
		if ( !front || !back )
		{
			if ( front )
			{
				FreeWinding( front );
			}
			if ( back )
			{
				FreeWinding( back );
			}
			Warning( "VRAD ReSTIR: lightmap rescale could not split face %d\n", input.source );
			return false;
		}
		RescaleFace a = input;
		a.winding = front;
		RescaleFace b = input;
		b.winding = back;
		const bool ok = SubdivideFace( a, info, output ) && SubdivideFace( b, info, output );
		return ok;
	}
	output.AddToTail( input );
	return true;
}

static winding_t *WindingForDFace( const dface_t &face )
{
	winding_t *winding = AllocWinding( face.numedges );
	winding->numpoints = face.numedges;
	for ( int i = 0; i < face.numedges; ++i )
	{
		const int surfedge = dsurfedges[face.firstedge + i];
		const int vertex = surfedge >= 0 ? dedges[surfedge].v[0] : dedges[-surfedge].v[1];
		winding->p[i] = dvertexes[vertex].point;
	}
	return winding;
}

// Port: utils/common/bsplib.cpp:3319-3380 CalcFaceExtents, returning false
// instead of Error() so the caller can abort the rescale.
static bool CalcExtents( dface_t &face )
{
	const texinfo_t &info = texinfo[face.texinfo];
	const int maxDim = face.dispinfo == -1 ? MAX_LIGHTMAP_DIM_WITHOUT_BORDER : MAX_DISP_LIGHTMAP_DIM_WITHOUT_BORDER;
	for ( int axis = 0; axis < 2; ++axis )
	{
		float mins = 1e24f;
		float maxs = -1e24f;
		for ( int i = 0; i < face.numedges; ++i )
		{
			const int surfedge = dsurfedges[face.firstedge + i];
			const Vector &point = dvertexes[surfedge >= 0 ? dedges[surfedge].v[0] : dedges[-surfedge].v[1]].point;
			const float value = DotProduct( point, *(Vector *)info.lightmapVecsLuxelsPerWorldUnits[axis] ) +
				info.lightmapVecsLuxelsPerWorldUnits[axis][3];
			mins = MIN( mins, value );
			maxs = MAX( maxs, value );
		}
		mins = (float)floor( mins );
		maxs = (float)ceil( maxs );
		face.m_LightmapTextureMinsInLuxels[axis] = (int)mins;
		face.m_LightmapTextureSizeInLuxels[axis] = (int)( maxs - mins );
		if ( face.m_LightmapTextureSizeInLuxels[axis] > maxDim + 1 )
		{
			return false;
		}
	}
	return true;
}

static bool FaceIsLit( const dface_t &face )
{
	return face.texinfo >= 0 && !( texinfo[face.texinfo].flags & ( SURF_SKY | SURF_NOLIGHT ) );
}

//-----------------------------------------------------------------------------
// Remap helpers for the face-indexed lumps
//-----------------------------------------------------------------------------
template <typename T>
static void RemapFaceList( T *faces, int count, const CUtlVector<int> &newFirst, const CUtlVector<int> &newCount, CUtlVector<T> &out )
{
	for ( int i = 0; i < count; ++i )
	{
		const int old = faces[i];
		for ( int k = 0; k < newCount[old]; ++k )
		{
			out.AddToTail( (T)( newFirst[old] + k ) );
		}
	}
}

// The applied scale is recorded on worldspawn so the launcher's second -both pass
// (which reloads the BSP this pass wrote) does not densify a second time.
static const char *const s_pScaleKey = "_restir_lightmapscale";

bool ReSTIR_RescaleLightmaps( const ReSTIROptions &options )
{
	float scale = options.lightmapScale;
	if ( scale >= 1.0f )
	{
		return true;
	}
	if ( num_entities > 0 )
	{
		const float applied = FloatForKey( &entities[0], (char *)s_pScaleKey );
		if ( applied > 0.0f )
		{
			if ( fabs( applied - scale ) < 0.0005f )
			{
				Msg( "VRAD ReSTIR: BSP already carries lightmap scale %.3f\n", applied );
				return true;
			}
			// Compose: the BSP is at `applied`, the request is absolute.
			scale = scale / applied;
			if ( scale >= 1.0f )
			{
				Warning( "VRAD ReSTIR: BSP already carries lightmap scale %.3f; cannot coarsen to %.3f (recompile with VBSP)\n", applied, options.lightmapScale );
				return false;
			}
		}
	}
	dface_t *faces = g_pFaces;
	const int faceCount = g_pFaces == dfaces_hdr ? numfaces_hdr : numfaces;
	const int originalVerts = numvertexes;
	const int originalEdges = numedges;
	const int originalSurfedges = numsurfedges;

	// 1. Densify every texinfo used by a lit brush face. Displacement texinfos
	//    are shared with nothing else (VBSP emits one per side), so a texinfo
	//    referenced by any displacement face is skipped as a whole.
	CUtlVector<bool> rescaleTexinfo;
	rescaleTexinfo.SetCount( texinfo.Count() );
	for ( int i = 0; i < texinfo.Count(); ++i )
	{
		rescaleTexinfo[i] = false;
	}
	for ( int i = 0; i < faceCount; ++i )
	{
		if ( FaceIsLit( faces[i] ) && faces[i].dispinfo == -1 )
		{
			rescaleTexinfo[faces[i].texinfo] = true;
		}
	}
	for ( int i = 0; i < faceCount; ++i )
	{
		if ( faces[i].dispinfo != -1 && faces[i].texinfo >= 0 )
		{
			rescaleTexinfo[faces[i].texinfo] = false;
		}
	}
	int rescaled = 0;
	for ( int i = 0; i < texinfo.Count(); ++i )
	{
		if ( !rescaleTexinfo[i] )
		{
			continue;
		}
		for ( int axis = 0; axis < 2; ++axis )
		{
			for ( int k = 0; k < 4; ++k )
			{
				texinfo[i].lightmapVecsLuxelsPerWorldUnits[axis][k] /= scale;
			}
		}
		++rescaled;
	}

	// 2. Split faces. Children of face i occupy [newFirst[i], newFirst[i]+newCount[i]).
	CUtlVector<RescaleFace> output;
	CUtlVector<int> newFirst;
	CUtlVector<int> newCount;
	newFirst.SetCount( faceCount );
	newCount.SetCount( faceCount );
	int splitFaces = 0;
	CUtlMap<int, int> originalTexinfo( DefLessFunc( int ) );	// densified texinfo -> unscaled clone for slivers
	for ( int i = 0; i < faceCount; ++i )
	{
		newFirst[i] = output.Count();
		RescaleFace source;
		source.face = faces[i];
		source.winding = NULL;
		source.source = i;
		if ( !rescaleTexinfo[faces[i].texinfo >= 0 ? faces[i].texinfo : 0] || faces[i].texinfo < 0 || faces[i].numedges < 3 )
		{
			output.AddToTail( source );
			newCount[i] = 1;
			continue;
		}
		source.winding = WindingForDFace( faces[i] );
		Vector center;
		if ( WindingAreaAndBalancePoint( source.winding, center ) <= 0.0f )
		{
			// VBSP emits zero-area slivers (collinear windings) and skips their lightmap
			// size check; the engine's Mod_LoadFaces recomputes extents from the vertices
			// at load and errors ("Bad surface extents") above the cap. They never get
			// samples, so keep them at their original density on a private texinfo copy.
			FreeWinding( source.winding );
			source.winding = NULL;
			int clone = originalTexinfo.Find( faces[i].texinfo );
			if ( clone == originalTexinfo.InvalidIndex() )
			{
				if ( texinfo.Count() >= MAX_MAP_TEXINFO )
				{
					Warning( "VRAD ReSTIR: lightmap rescale exceeded MAX_MAP_TEXINFO\n" );
					goto fail;
				}
				texinfo_t copy = texinfo[faces[i].texinfo];
				for ( int axis = 0; axis < 2; ++axis )
				{
					for ( int k = 0; k < 4; ++k )
					{
						copy.lightmapVecsLuxelsPerWorldUnits[axis][k] *= scale;
					}
				}
				clone = originalTexinfo.Insert( faces[i].texinfo, texinfo.AddToTail( copy ) );
			}
			source.face.texinfo = (short)originalTexinfo[clone];
			output.AddToTail( source );
			newCount[i] = 1;
			continue;
		}
		const int before = output.Count();
		if ( !SubdivideFace( source, texinfo[faces[i].texinfo], output ) )
		{
			goto fail;
		}
		newCount[i] = output.Count() - before;
		if ( newCount[i] > 1 )
		{
			++splitFaces;
		}
	}
	if ( output.Count() > MAX_MAP_FACES )
	{
		Warning( "VRAD ReSTIR: lightmap rescale needs %d faces, MAX_MAP_FACES is %d\n", output.Count(), MAX_MAP_FACES );
		goto fail;
	}

	// 3. Emit geometry for split faces; unsplit faces keep their surfedges.
	{
		CRescaleGeometry geometry;
		for ( int i = 0; i < output.Count(); ++i )
		{
			RescaleFace &entry = output[i];
			if ( !entry.winding )
			{
				continue;
			}
			if ( newCount[entry.source] == 1 )
			{
				// Unsplit: keep the original edge references untouched.
				FreeWinding( entry.winding );
				entry.winding = NULL;
				continue;
			}
			winding_t *winding = entry.winding;
			CUtlVector<int> vertices;
			for ( int k = 0; k < winding->numpoints; ++k )
			{
				const int vertex = geometry.FindOrAddVertex( winding->p[k] );
				if ( vertex < 0 )
				{
					Warning( "VRAD ReSTIR: lightmap rescale exceeded MAX_MAP_VERTS\n" );
					goto fail;
				}
				if ( vertices.Count() == 0 || vertices[vertices.Count() - 1] != vertex )
				{
					vertices.AddToTail( vertex );
				}
			}
			if ( vertices.Count() > 1 && vertices[0] == vertices[vertices.Count() - 1] )
			{
				vertices.RemoveMultipleFromTail( 1 );
			}
			if ( vertices.Count() < 3 )
			{
				Warning( "VRAD ReSTIR: lightmap rescale produced a degenerate face from face %d\n", entry.source );
				goto fail;
			}
			entry.face.firstedge = numsurfedges;
			entry.face.numedges = (short)vertices.Count();
			for ( int k = 0; k < vertices.Count(); ++k )
			{
				const int surfedge = geometry.FindOrAddEdge( vertices[k], vertices[( k + 1 ) % vertices.Count()], i );
				if ( surfedge == 0 || !geometry.AppendSurfedge( surfedge ) )
				{
					Warning( "VRAD ReSTIR: lightmap rescale exceeded MAX_MAP_EDGES/MAX_MAP_SURFEDGES\n" );
					goto fail;
				}
			}
			// Split faces lose their T-junction primitives: the engine draws the
			// new polygon as a fan and the shared split edge is exact on both sides.
			entry.face.SetNumPrims( 0 );
			entry.face.firstPrimID = 0;
			FreeWinding( winding );
			entry.winding = NULL;
		}
	}

	// 4. Recompute extents for every lit face (children and rescaled unsplit faces).
	for ( int i = 0; i < output.Count(); ++i )
	{
		dface_t &face = output[i].face;
		if ( !FaceIsLit( face ) || face.dispinfo != -1 )
		{
			continue;
		}
		if ( !CalcExtents( face ) )
		{
			Warning( "VRAD ReSTIR: lightmap rescale left face %d (from %d) too large for a lightmap\n", i, output[i].source );
			goto fail;
		}
		face.lightofs = -1;
	}

	// 5. Budget: LUMP_LIGHTING is capped at MAX_MAP_LIGHTING bytes per mode. Each lit
	//    face costs (styles * channels * luxels + styles) * 4; the baked style count is
	//    unknown here, so assume what the original face used (at least 1).
	{
		int64 bytes = 0;
		for ( int i = 0; i < output.Count(); ++i )
		{
			const dface_t &face = output[i].face;
			if ( !FaceIsLit( face ) )
			{
				continue;
			}
			int styles = 0;
			const dface_t &source = faces[output[i].source];
			for ( int k = 0; k < MAXLIGHTMAPS; ++k )
			{
				styles += source.styles[k] != 255 ? 1 : 0;
			}
			styles = MAX( styles, 1 );
			const int channels = ( texinfo[face.texinfo].flags & SURF_BUMPLIGHT ) ? NUM_BUMP_VECTS + 1 : 1;
			const int64 luxels = (int64)( face.m_LightmapTextureSizeInLuxels[0] + 1 ) * ( face.m_LightmapTextureSizeInLuxels[1] + 1 );
			bytes += ( (int64)styles * channels * luxels + styles ) * 4;
		}
		if ( bytes > MAX_MAP_LIGHTING )
		{
			Warning( "VRAD ReSTIR: lightmap scale %.3f needs about %lld bytes of light data per mode; the engine limit (MAX_MAP_LIGHTING) is %d (%.0f%% over). Use a larger -restir_lightmapscale.\n",
				options.lightmapScale, (long long)bytes, MAX_MAP_LIGHTING, 100.0 * ( (double)bytes / MAX_MAP_LIGHTING - 1.0 ) );
			goto fail;
		}
		Msg( "VRAD ReSTIR: lightmap scale %.3f: about %lld of %d light data bytes per mode\n", options.lightmapScale, (long long)bytes, MAX_MAP_LIGHTING );
	}

	// 6. Write the new face arrays. Both modes share geometry, and the other mode's
	//    lighting was laid out for the old extents, so it is invalidated (the
	//    launcher's -both runs both passes; a single pass leaves the other mode unlit).
	{
		CUtlVector<dface_t> otherFaces;
		dface_t *other = g_pFaces == dfaces_hdr ? dfaces : dfaces_hdr;
		const int otherCount = g_pFaces == dfaces_hdr ? numfaces : numfaces_hdr;
		for ( int i = 0; i < output.Count(); ++i )
		{
			dface_t face = output[i].face;
			face.lightofs = -1;
			memset( face.styles, 255, sizeof( face.styles ) );
			otherFaces.AddToTail( face );
		}
		for ( int i = 0; i < output.Count(); ++i )
		{
			faces[i] = output[i].face;
		}
		if ( otherCount == faceCount )
		{
			for ( int i = 0; i < output.Count(); ++i )
			{
				other[i] = otherFaces[i];
			}
		}
		if ( g_pFaces == dfaces_hdr )
		{
			numfaces_hdr = output.Count();
			if ( otherCount == faceCount )
			{
				numfaces = output.Count();
			}
		}
		else
		{
			numfaces = output.Count();
			if ( otherCount == faceCount )
			{
				numfaces_hdr = output.Count();
			}
		}
	}

	// 7. Face-indexed lumps.
	{
		// Models and nodes reference contiguous ranges; children stay adjacent to
		// their parent, so [firstface, firstface+numfaces) maps to
		// [newFirst[firstface], newFirst[last]+newCount[last]).
		for ( int i = 0; i < nummodels; ++i )
		{
			dmodel_t &model = dmodels[i];
			if ( model.numfaces <= 0 )
			{
				model.firstface = model.firstface < faceCount ? newFirst[model.firstface] : output.Count();
				continue;
			}
			const int last = model.firstface + model.numfaces - 1;
			const int first = newFirst[model.firstface];
			model.numfaces = newFirst[last] + newCount[last] - first;
			model.firstface = first;
		}
		for ( int i = 0; i < numnodes; ++i )
		{
			dnode_t &node = dnodes[i];
			if ( node.numfaces == 0 )
			{
				node.firstface = (unsigned short)( node.firstface < faceCount ? newFirst[node.firstface] : output.Count() );
				continue;
			}
			const int last = node.firstface + node.numfaces - 1;
			const int first = newFirst[node.firstface];
			const int count = newFirst[last] + newCount[last] - first;
			if ( first > 65535 || count > 65535 )
			{
				Warning( "VRAD ReSTIR: lightmap rescale overflowed dnode_t face range\n" );
				goto fail;
			}
			node.firstface = (unsigned short)first;
			node.numfaces = (unsigned short)count;
		}
		// Leaf face lists are explicit index lists.
		CUtlVector<unsigned short> leafFaces;
		for ( int i = 0; i < numleafs; ++i )
		{
			dleaf_t &leaf = dleafs[i];
			const int first = leafFaces.Count();
			RemapFaceList( dleaffaces + leaf.firstleafface, leaf.numleaffaces, newFirst, newCount, leafFaces );
			leaf.firstleafface = (unsigned short)first;
			leaf.numleaffaces = (unsigned short)( leafFaces.Count() - first );
		}
		if ( leafFaces.Count() > MAX_MAP_LEAFFACES )
		{
			Warning( "VRAD ReSTIR: lightmap rescale needs %d leaf faces, MAX_MAP_LEAFFACES is %d\n", leafFaces.Count(), MAX_MAP_LEAFFACES );
			goto fail;
		}
		memcpy( dleaffaces, leafFaces.Base(), leafFaces.Count() * sizeof( dleaffaces[0] ) );
		numleaffaces = leafFaces.Count();

		// Per-face side tables: duplicate the parent's entry for each child.
		if ( dfaceids.Count() == faceCount )
		{
			CUtlVector<dfaceid_t> ids;
			for ( int i = 0; i < output.Count(); ++i )
			{
				ids.AddToTail( dfaceids[output[i].source] );
			}
			dfaceids.CopyArray( ids.Base(), ids.Count() );
			numfaceids = ids.Count();
		}
		if ( g_FaceMacroTextureInfos.Count() == faceCount )
		{
			CUtlVector<CFaceMacroTextureInfo> macro;
			for ( int i = 0; i < output.Count(); ++i )
			{
				macro.AddToTail( g_FaceMacroTextureInfos[output[i].source] );
			}
			g_FaceMacroTextureInfos.CopyArray( macro.Base(), macro.Count() );
		}
		for ( int i = 0; i < g_dispinfo.Count(); ++i )
		{
			g_dispinfo[i].m_iMapFace = (unsigned short)newFirst[g_dispinfo[i].m_iMapFace];
		}
		// Overlays list the faces they decal; a split face contributes all children,
		// bounded by the fixed array size.
		for ( int i = 0; i < g_nOverlayCount; ++i )
		{
			doverlay_t &overlay = g_Overlays[i];
			CUtlVector<int> list;
			RemapFaceList( overlay.aFaces, overlay.GetFaceCount(), newFirst, newCount, list );
			if ( list.Count() > OVERLAY_BSP_FACE_COUNT )
			{
				Warning( "VRAD ReSTIR: overlay %d would span %d faces after rescale (max %d); truncating\n", i, list.Count(), OVERLAY_BSP_FACE_COUNT );
				list.SetCountNonDestructively( OVERLAY_BSP_FACE_COUNT );
			}
			for ( int k = 0; k < list.Count(); ++k )
			{
				overlay.aFaces[k] = list[k];
			}
			overlay.SetFaceCount( (unsigned short)list.Count() );
		}
		for ( int i = 0; i < g_nWaterOverlayCount; ++i )
		{
			dwateroverlay_t &overlay = g_WaterOverlays[i];
			CUtlVector<int> list;
			RemapFaceList( overlay.aFaces, overlay.GetFaceCount(), newFirst, newCount, list );
			if ( list.Count() > WATEROVERLAY_BSP_FACE_COUNT )
			{
				Warning( "VRAD ReSTIR: water overlay %d would span %d faces after rescale (max %d); truncating\n", i, list.Count(), WATEROVERLAY_BSP_FACE_COUNT );
				list.SetCountNonDestructively( WATEROVERLAY_BSP_FACE_COUNT );
			}
			for ( int k = 0; k < list.Count(); ++k )
			{
				overlay.aFaces[k] = list[k];
			}
			overlay.SetFaceCount( (unsigned short)list.Count() );
		}
		// Vertex normals are re-emitted by the scene builder from the new faces.
		g_numvertnormalindices = 0;
		g_numvertnormals = 0;
	}

	if ( num_entities > 0 )
	{
		char value[32];
		V_snprintf( value, sizeof( value ), "%.4f", options.lightmapScale );
		SetKeyValue( &entities[0], s_pScaleKey, value );
		UnparseEntities();
	}
	Msg( "VRAD ReSTIR: lightmap scale %.3f: %d texinfos densified, %d of %d faces split into %d, +%d verts +%d edges +%d surfedges\n",
		options.lightmapScale, rescaled, splitFaces, faceCount, output.Count(), numvertexes - originalVerts, numedges - originalEdges, numsurfedges - originalSurfedges );
	return true;

fail:
	for ( int i = 0; i < output.Count(); ++i )
	{
		if ( output[i].winding )
		{
			FreeWinding( output[i].winding );
		}
	}
	Warning( "VRAD ReSTIR: lightmap rescale aborted; reload the BSP before baking\n" );
	return false;
}
