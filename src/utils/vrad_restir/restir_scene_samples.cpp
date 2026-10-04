//========= Copyright Valve Corporation, All rights reserved. ============//
// Face sample and luxel construction, ported from VRAD without lighting rays.

#include "restir_scene_internal.h"
#include "vrad_restir.h"
#include "bsplib.h"
#include "cmdlib.h"
#include "mathlib/bumpvects.h"
#include <math.h>
#include <string.h>

#ifndef SMOOTHING_GROUP_HARD_EDGE
#define SMOOTHING_GROUP_HARD_EDGE 0xff000000
#endif

// utils/vrad/lightmap.cpp:417-475 — CalcFaceVectors.
void ReSTIR_SceneCalcFaceVectors( const dface_t *pFace, const Vector &origin, const Vector &normal,
	Vector &luxelOrigin, Vector worldToLuxel[2], Vector luxelToWorld[2] )
{
	const texinfo_t &info = texinfo[pFace->texinfo];
	for ( int axis = 0; axis < 2; ++axis )
	{
		for ( int component = 0; component < 3; ++component )
		{
			worldToLuxel[axis][component] = info.lightmapVecsLuxelsPerWorldUnits[axis][component];
		}
		luxelToWorld[axis].Init();
	}

	const Vector luxelSpaceCross = CrossProduct( worldToLuxel[1], worldToLuxel[0] );
	const float determinant = -DotProduct( normal, luxelSpaceCross );
	if ( fabs( determinant ) < 1.0e-20 )
	{
		Warning( " warning - face vectors parallel to face normal. bad lighting will be produced\n" );
		luxelOrigin = vec3_origin;
	}
	else
	{
		const float faceDistance = dplanes[pFace->planenum].dist;
		luxelToWorld[0].x = ( normal.z * worldToLuxel[1].y - normal.y * worldToLuxel[1].z ) / determinant;
		luxelToWorld[1].x = ( normal.y * worldToLuxel[0].z - normal.z * worldToLuxel[0].y ) / determinant;
		luxelOrigin.x = -( faceDistance * luxelSpaceCross.x ) / determinant;
		luxelToWorld[0].y = ( normal.x * worldToLuxel[1].z - normal.z * worldToLuxel[1].x ) / determinant;
		luxelToWorld[1].y = ( normal.z * worldToLuxel[0].x - normal.x * worldToLuxel[0].z ) / determinant;
		luxelOrigin.y = -( faceDistance * luxelSpaceCross.y ) / determinant;
		luxelToWorld[0].z = ( normal.y * worldToLuxel[1].x - normal.x * worldToLuxel[1].y ) / determinant;
		luxelToWorld[1].z = ( normal.x * worldToLuxel[0].y - normal.y * worldToLuxel[0].x ) / determinant;
		luxelOrigin.z = -( faceDistance * luxelSpaceCross.z ) / determinant;
		VectorMA( luxelOrigin, -info.lightmapVecsLuxelsPerWorldUnits[0][3], luxelToWorld[0], luxelOrigin );
		VectorMA( luxelOrigin, -info.lightmapVecsLuxelsPerWorldUnits[1][3], luxelToWorld[1], luxelOrigin );
	}
	VectorAdd( luxelOrigin, origin, luxelOrigin );
}

// utils/vrad/lightmap.cpp:151-325 — PairEdges vertexref/vertexface indices.
void ReSTIR_SceneBuildFaceNeighbors( ReSTIRSceneBuildContext &context, ReSTIRScene &scene )
{
	(void)scene;
	CUtlVector<int> *vertexFaces = new CUtlVector<int>[MAX( numvertexes, 1 )];
	for ( int faceIndex = 0; faceIndex < context.faceCount; ++faceIndex )
	{
		const dface_t &face = g_pFaces[faceIndex];
		for ( int edge = 0; edge < face.numedges; ++edge )
		{
			const int vertex = ReSTIR_SceneFaceVertex( &face, edge );
			if ( vertexFaces[vertex].Find( faceIndex ) < 0 )
			{
				vertexFaces[vertex].AddToTail( faceIndex );
			}
		}
	}
	context.neighborLists.SetCount( context.faceCount );
	context.vertexNormals.SetCount( context.faceCount );
	context.faceCentroids.SetCount( context.faceCount );
	for ( int faceIndex = 0; faceIndex < context.faceCount; ++faceIndex )
	{
		const dface_t &face = g_pFaces[faceIndex];
		context.neighborLists[faceIndex] = new CUtlVector<int>;
		context.vertexNormals[faceIndex] = new CUtlVector<Vector>;
		context.vertexNormals[faceIndex]->SetCount( face.numedges );
		context.faceNormals[faceIndex] = dplanes[face.planenum].normal;
		// utils/vrad/vrad.cpp:619-620 — face_centroids use WindingCenter (vertex average).
		context.faceCentroids[faceIndex] = ReSTIR_SceneFaceCentroid( &face, vec3_origin );
	}
	for ( int faceIndex = 0; faceIndex < context.faceCount; ++faceIndex )
	{
		const dface_t &face = g_pFaces[faceIndex];
		const bool hasDisp = face.dispinfo >= 0;
		for ( int edge = 0; edge < face.numedges; ++edge )
		{
			Vector &normal = (*context.vertexNormals[faceIndex])[edge];
			normal.Init();
			const int vertex = ReSTIR_SceneFaceVertex( &face, edge );
			for ( int i = 0; i < vertexFaces[vertex].Count(); ++i )
			{
				const int other = vertexFaces[vertex][i];
				if ( other == faceIndex )
				{
					continue;
				}
				if ( !hasDisp && g_pFaces[other].dispinfo >= 0 )
				{
					continue;
				}
				if ( !hasDisp )
				{
					if ( face.smoothingGroups == 0 && g_pFaces[other].smoothingGroups == 0 )
					{
						if ( DotProduct( context.faceNormals[faceIndex], context.faceNormals[other] ) <
							context.options->smoothingThreshold )
						{
							continue;
						}
					}
					else
					{
						const unsigned int group = face.smoothingGroups & g_pFaces[other].smoothingGroups;
						if ( ( group & SMOOTHING_GROUP_HARD_EDGE ) != 0 || group == 0 )
						{
							continue;
						}
					}
				}
				normal += context.faceNormals[other];
				if ( context.neighborLists[faceIndex]->Find( other ) < 0 )
				{
					context.neighborLists[faceIndex]->AddToTail( other );
				}
			}
			normal += context.faceNormals[faceIndex];
			VectorNormalize( normal );
		}
	}
	delete[] vertexFaces;
}

// utils/vrad/lightmap.cpp:38-112 — CNormalList: exact-match dedup over an 8^3 grid.
class CReSTIRNormalList
{
public:
	int FindOrAdd( const Vector &normal )
	{
		int cell[3];
		for ( int axis = 0; axis < 3; ++axis )
		{
			cell[axis] = (int)( ( ( normal[axis] + 1.0f ) * 0.5f ) * NUM_SUBDIVS - 0.000001f );
			cell[axis] = MIN( cell[axis], NUM_SUBDIVS );
			cell[axis] = MAX( cell[axis], 0 );
		}
		CUtlVector<int> &bucket = m_Grid[cell[0]][cell[1]][cell[2]];
		for ( int i = 0; i < bucket.Count(); ++i )
		{
			if ( m_Normals[bucket[i]] == normal )
			{
				return bucket[i];
			}
		}
		bucket.AddToTail( m_Normals.Count() );
		return m_Normals.AddToTail( normal );
	}
	CUtlVector<Vector> m_Normals;

private:
	enum { NUM_SUBDIVS = 8 };
	CUtlVector<int> m_Grid[NUM_SUBDIVS + 1][NUM_SUBDIVS + 1][NUM_SUBDIVS + 1];
};

// utils/vrad/lightmap.cpp:328-373 — SaveVertexNormals: one index per face edge in
// face order, so the lumps must be regenerated whenever the face array changes.
bool ReSTIR_SceneSaveVertexNormals( const ReSTIRSceneBuildContext &context )
{
	CReSTIRNormalList normals;
	g_numvertnormalindices = 0;
	for ( int faceIndex = 0; faceIndex < context.faceCount; ++faceIndex )
	{
		const dface_t &face = g_pFaces[faceIndex];
		const CUtlVector<Vector> *faceNormals = context.vertexNormals[faceIndex];
		for ( int edge = 0; edge < face.numedges; ++edge )
		{
			if ( g_numvertnormalindices >= MAX_MAP_VERTNORMALINDICES )
			{
				Warning( "VRAD ReSTIR: vertex normal indices exceed MAX_MAP_VERTNORMALINDICES\n" );
				return false;
			}
			const Vector normal = faceNormals && edge < faceNormals->Count() ? (*faceNormals)[edge] : vec3_origin;
			g_vertnormalindices[g_numvertnormalindices++] = (unsigned short)normals.FindOrAdd( normal );
		}
	}
	if ( normals.m_Normals.Count() > MAX_MAP_VERTNORMALS )
	{
		Warning( "VRAD ReSTIR: vertex normals exceed MAX_MAP_VERTNORMALS\n" );
		return false;
	}
	g_numvertnormals = normals.m_Normals.Count();
	memcpy( g_vertnormals, normals.m_Normals.Base(), g_numvertnormals * sizeof( g_vertnormals[0] ) );
	return true;
}

// utils/vrad/lightmap.cpp:2118-2215 — center/edge barycentric interpolation.
Vector ReSTIR_ScenePhongNormal( const ReSTIRSceneBuildContext &context, int faceIndex,
	const Vector &point, const Vector &faceNormal )
{
	if ( context.options->smoothingThreshold == 1.0f )
	{
		return faceNormal;
	}
	const dface_t &face = g_pFaces[faceIndex];
	const Vector center = context.faceCentroids[faceIndex];
	const Vector spot = point - context.faceOrigins[faceIndex] - center;
	for ( int edge = 0; edge < face.numedges; ++edge )
	{
		const Vector v1 = dvertexes[ReSTIR_SceneFaceVertex( &face, edge )].point - center;
		const Vector v2 = dvertexes[ReSTIR_SceneFaceVertex( &face, edge + 1 )].point - center;
		const float aa = DotProduct( v1, v1 );
		const float bb = DotProduct( v2, v2 );
		const float ab = DotProduct( v1, v2 );
		const float denominator = aa * bb - ab * ab;
		// Preserve VRAD's barycentric arithmetic, including degenerate edge wedges.
		const float a1 = ( bb * DotProduct( v1, spot ) - ab * DotProduct( spot, v2 ) ) / denominator;
		const float a2 = ( DotProduct( spot, v2 ) - a1 * ab ) / bb;
		if ( a1 >= 0.0f && a2 >= 0.0f )
		{
			Vector normal = context.faceNormals[faceIndex] * ( 1.0f - a1 - a2 );
			normal += (*context.vertexNormals[faceIndex])[edge] * a1;
			normal += (*context.vertexNormals[faceIndex])[( edge + 1 ) % face.numedges] * a2;
			VectorNormalize( normal );
			return normal;
		}
	}
	return faceNormal;
}

// utils/vrad/lightmap.cpp:1012-1039 — directlight_t PVS retained for the whole face pass.
struct ReSTIRSceneLightVis
{
	int bytes;
	CUtlVector<byte> lightPVS;
	CUtlVector<byte> allVisible;
	CUtlVector<CUtlVector<byte> *> clusterPVS;

	ReSTIRSceneLightVis()
		: bytes( dvis ? ( dvis->numclusters / 8 ) + 1 : 1 )
	{
		allVisible.SetCount( bytes );
		memset( allVisible.Base(), 255, bytes );
		if ( dvis && visdatasize )
		{
			clusterPVS.SetCount( dvis->numclusters );
			for ( int cluster = 0; cluster < clusterPVS.Count(); ++cluster )
			{
				clusterPVS[cluster] = NULL;
			}
		}
	}

	~ReSTIRSceneLightVis()
	{
		clusterPVS.PurgeAndDeleteElements();
	}
};

// utils/vrad/lightmap.cpp:1012-1039,2308-2340 — SetDLightVis, MergeDLightVis, GetVisCache.
// Share decompressed cluster PVS across lights; never decompress inside a face/sample loop.
static const byte *GetCachedClusterVis( ReSTIRSceneLightVis &vis, int cluster )
{
	if ( !visdatasize || cluster < 0 )
	{
		return vis.allVisible.Base();
	}
	if ( !dvis || cluster >= dvis->numclusters )
	{
		Warning( "Invalid light PVS cluster %d\n", cluster );
		return NULL;
	}
	if ( !vis.clusterPVS[cluster] )
	{
		const int offset = dvis->bitofs[cluster][DVIS_PVS];
		if ( offset == -1 )
		{
			Warning( "visofs == -1\n" );
			return NULL;
		}
		CUtlVector<byte> *pvs = new CUtlVector<byte>;
		pvs->SetCount( vis.bytes );
		memset( pvs->Base(), 0, vis.bytes );
		DecompressVis( &dvisdata[offset], pvs->Base() );
		vis.clusterPVS[cluster] = pvs;
	}
	return vis.clusterPVS[cluster]->Base();
}

// utils/vrad/lightmap.cpp:1022-1039 — MergeDLightVis.
static bool MergeCachedClusterVis( ReSTIRSceneLightVis &vis, byte *destination, int cluster )
{
	const byte *source = GetCachedClusterVis( vis, cluster );
	if ( !source )
	{
		return false;
	}
	for ( int i = 0; i < vis.bytes; ++i )
	{
		destination[i] |= source[i];
	}
	return true;
}

// utils/vrad/lightmap.cpp:1344-1371 — sky-containing leaf PVS merges.
// The later CanLeafTraceToSky CPU-ray heuristic is deliberately not part of this GPU baker.
static bool BuildLightVis( const ReSTIRScene &scene, ReSTIRSceneLightVis &vis )
{
	vis.lightPVS.SetCount( scene.lights.Count() * vis.bytes );
	if ( vis.lightPVS.Count() == 0 )
	{
		return true;
	}
	memset( vis.lightPVS.Base(), 0, vis.lightPVS.Count() );
	CUtlVector<byte> skyPVS;
	skyPVS.SetCount( vis.bytes );
	memset( skyPVS.Base(), visdatasize ? 0 : 255, vis.bytes );
	bool hasSkyLight = false;
	for ( int lightIndex = 0; lightIndex < scene.lights.Count(); ++lightIndex )
	{
		const int type = scene.lights[lightIndex].type;
		if ( type == emit_skylight || type == emit_skyambient )
		{
			hasSkyLight = true;
			break;
		}
	}
	if ( hasSkyLight )
	{
		for ( int leafIndex = 0; leafIndex < numleafs; ++leafIndex )
		{
			const dleaf_t &leaf = dleafs[leafIndex];
			for ( int leafFace = 0; leafFace < leaf.numleaffaces; ++leafFace )
			{
				const dface_t &face = g_pFaces[dleaffaces[leaf.firstleafface + leafFace]];
				if ( texinfo[face.texinfo].flags & SURF_SKY )
				{
					if ( !MergeCachedClusterVis( vis, skyPVS.Base(), leaf.cluster ) )
					{
						return false;
					}
					break;
				}
			}
		}
	}

	CUtlVector<bool> hasExportVis;
	hasExportVis.SetCount( scene.lights.Count() );
	for ( int lightIndex = 0; lightIndex < hasExportVis.Count(); ++lightIndex )
	{
		hasExportVis[lightIndex] = false;
	}
	// Surface GPU emitters combine the PVS of their exported leaf-patch lights,
	// including the solid-space winding-point fallback computed by the light builder.
	for ( int exportIndex = 0; exportIndex < scene.exportLights.Count(); ++exportIndex )
	{
		const int lightIndex = scene.exportLightToGpuLight[exportIndex];
		const int type = scene.lights[lightIndex].type;
		if ( type == emit_skylight || type == emit_skyambient )
		{
			continue;
		}
		if ( !MergeCachedClusterVis( vis, vis.lightPVS.Base() + lightIndex * vis.bytes,
			scene.exportLights[exportIndex].cluster ) )
		{
			return false;
		}
		hasExportVis[lightIndex] = true;
	}
	for ( int lightIndex = 0; lightIndex < scene.lights.Count(); ++lightIndex )
	{
		byte *destination = vis.lightPVS.Base() + lightIndex * vis.bytes;
		const ReSTIRGpuLight &light = scene.lights[lightIndex];
		if ( light.type == emit_skylight || light.type == emit_skyambient )
		{
			memcpy( destination, skyPVS.Base(), vis.bytes );
		}
		else if ( !hasExportVis[lightIndex] )
		{
			const int cluster = ReSTIR_SceneClusterFromPoint( ReSTIR_SceneV4( light.origin ) );
			if ( !MergeCachedClusterVis( vis, destination, cluster ) )
			{
				return false;
			}
		}
	}
	return true;
}

// utils/vrad/vrad.h:372-383 — PVSCheck.
static bool ClusterVisible( int cluster, const byte *pvs )
{
	return cluster < 0 || ( pvs[cluster >> 3] & ( 1 << ( cluster & 7 ) ) ) != 0;
}

// utils/vrad/lightmap.cpp:2495-2527 — PVS eligibility before style allocation.
// The host preassigns plane-facing slots; actual cosine/falloff/visibility remain on the GPU.
static bool LightAffectsFace( const ReSTIRSceneBuildContext &context,
	const ReSTIRGpuFace &face, const ReSTIRGpuLight &light, const byte *pvs )
{
	bool visible = false;
	const CUtlVector<int> *clusters = context.faceClusterLists[face.dface];
	if ( clusters && clusters->Count() > 0 )
	{
		for ( int i = 0; i < clusters->Count(); ++i )
		{
			if ( ClusterVisible( (*clusters)[i], pvs ) )
			{
				visible = true;
				break;
			}
		}
	}
	else
	{
		visible = ClusterVisible( context.faceClusters[face.dface], pvs );
	}
	if ( !visible )
	{
		return false;
	}
	if ( light.type == emit_skylight || light.type == emit_skyambient )
	{
		return true;
	}
	const dface_t &dface = g_pFaces[face.dface];
	const Vector normal = ReSTIR_SceneV4( face.faceNormal );
	const Vector lightOrigin = ReSTIR_SceneV4( light.origin );
	const float faceDistance = dplanes[dface.planenum].dist +
		DotProduct( context.faceOrigins[face.dface], normal );
	return DotProduct( normal, lightOrigin ) - faceDistance > 0.0f;
}

// utils/vrad/lightmap.cpp:2467-2481 — bump bases derived from the smoothed normal.
static void FillBumpNormals( const ReSTIRGpuFace &face, const Vector &normal, Vector *bumpNormals )
{
	GetBumpNormals( ReSTIR_SceneV4( face.textureS ), ReSTIR_SceneV4( face.textureT ),
		ReSTIR_SceneV4( face.faceNormal ), normal, bumpNormals );
}

// utils/vrad/lightmap.cpp:709-729; vraddisps.cpp:1638-1644 — pack sample_t fields.
static void AddSample( ReSTIRScene &scene, int faceIndex,
	int s, int t, const Vector &point, const Vector &worldPoint, const Vector &normal,
	const Vector &mins, const Vector &maxs, float area, const Vector *bumpNormals )
{
	ReSTIRGpuSample sample;
	memset( &sample, 0, sizeof( sample ) );
	sample.face = faceIndex;
	sample.s = s;
	sample.t = t;
	sample.lmCoord[0] = point.x;
	sample.lmCoord[1] = point.y;
	sample.lmCoord[2] = mins.x;
	sample.lmCoord[3] = mins.y;
	sample.lmMaxs[0] = maxs.x;
	sample.lmMaxs[1] = maxs.y;
	ReSTIR_SceneSet4( sample.position, worldPoint, area );
	ReSTIR_SceneSet4( sample.normal, normal );
	if ( bumpNormals )
	{
		for ( int i = 0; i < NUM_BUMP_VECTS; ++i )
		{
			ReSTIR_SceneSet4( sample.bump[i], bumpNormals[i] );
		}
	}
	scene.samples.AddToTail( sample );
}

// utils/vrad/lightmap.cpp:650-806 — BuildFacesamples; radial.cpp:27-35 — unpushed positions.
static void BuildRegularFaceSamples( ReSTIRSceneBuildContext &context, ReSTIRScene &scene,
	int faceIndex, ReSTIRGpuFace &face )
{
	const dface_t &dface = g_pFaces[face.dface];
	const Vector luxelOrigin = ReSTIR_SceneV4( face.luxelOrigin );
	Vector worldToLuxel[2], luxelToWorld[2];
	for ( int axis = 0; axis < 2; ++axis )
	{
		worldToLuxel[axis] = ReSTIR_SceneV4( face.worldToLuxel[axis] );
		luxelToWorld[axis] = ReSTIR_SceneV4( face.luxelToWorld[axis] );
	}
	face.firstSample = scene.samples.Count();
	winding_t *remaining = AllocWinding( dface.numedges );
	remaining->numpoints = dface.numedges;
	for ( int edge = 0; edge < dface.numedges; ++edge )
	{
		const Vector point = dvertexes[ReSTIR_SceneFaceVertex( &dface, edge )].point + context.faceOrigins[face.dface];
		remaining->p[edge].Init( DotProduct( point - luxelOrigin, worldToLuxel[0] ) - face.lmMins[0],
			DotProduct( point - luxelOrigin, worldToLuxel[1] ) - face.lmMins[1], 0.0f );
	}
	const float worldAreaPerLuxel = 1.0 /
		( sqrt( DotProduct( worldToLuxel[0], worldToLuxel[0] ) ) *
		sqrt( DotProduct( worldToLuxel[1], worldToLuxel[1] ) ) );
	for ( int t = 0; t < face.luxelH && remaining; ++t )
	{
		winding_t *row = NULL;
		winding_t *nextRow = NULL;
		ClipWindingEpsilon( remaining, Vector( 0, 1, 0 ), (float)t + 1.0f,
			ON_EPSILON / 16.0f, &nextRow, &row );
		FreeWinding( remaining );
		remaining = nextRow;
		for ( int s = 0; s < face.luxelW && row; ++s )
		{
			winding_t *nextCell = NULL;
			winding_t *cell = NULL;
			ClipWindingEpsilon( row, Vector( 1, 0, 0 ), (float)s + 1.0f,
				ON_EPSILON / 16.0f, &nextCell, &cell );
			FreeWinding( row );
			row = nextCell;
			if ( !cell )
			{
				continue;
			}
			Vector balance, mins, maxs;
			const float area = WindingAreaAndBalancePoint( cell, balance );
			WindingBounds( cell, mins, maxs );
			const Vector samplePosition = ReSTIR_SceneLuxelToWorld( luxelOrigin, luxelToWorld,
				balance.x + face.lmMins[0], balance.y + face.lmMins[1] );
			const Vector sampleNormal = ReSTIR_ScenePhongNormal( context, face.dface,
				samplePosition, ReSTIR_SceneV4( face.faceNormal ) );
			Vector bumpNormals[NUM_BUMP_VECTS];
			if ( face.flags & RESTIR_FACE_BUMPED )
			{
				FillBumpNormals( face, sampleNormal, bumpNormals );
			}
			AddSample( scene, faceIndex, s, t, balance, samplePosition,
				sampleNormal, mins, maxs, area * worldAreaPerLuxel,
				( face.flags & RESTIR_FACE_BUMPED ) ? bumpNormals : NULL );
			FreeWinding( cell );
		}
		if ( row )
		{
			FreeWinding( row );
		}
	}
	if ( remaining )
	{
		FreeWinding( remaining );
	}
	if ( scene.samples.Count() == face.firstSample )
	{
		Msg( "no samples %d\n", face.dface );
	}
}

// utils/vrad/vrad_dispcoll.cpp:214-264 — DispUVToSurf_TriTLToBR.
static void DispUVToSurfTriTLToBR( const CCoreDispInfo &disp, Vector &point, float push,
	float u, float v, int snapU, int snapV, int width, int height )
{
	int nextU = snapU + 1;
	int nextV = snapV + 1;
	if ( nextU == width )
	{
		--nextU;
	}
	if ( nextV == height )
	{
		--nextV;
	}
	const float fracU = u - static_cast<float>( snapU );
	const float fracV = v - static_cast<float>( snapV );
	Vector edgeU, edgeV;
	if ( ( fracU + fracV ) >= ( 1.0f + 0.001f ) )
	{
		const Vector &base = disp.GetVert( nextV * width + nextU );
		edgeU = disp.GetVert( nextV * width + snapU ) - base;
		edgeV = disp.GetVert( snapV * width + nextU ) - base;
		point = base + edgeU * ( 1.0f - fracU ) + edgeV * ( 1.0f - fracV );
	}
	else
	{
		const Vector &base = disp.GetVert( snapV * width + snapU );
		edgeU = disp.GetVert( snapV * width + nextU ) - base;
		edgeV = disp.GetVert( nextV * width + snapU ) - base;
		point = base + edgeU * fracU + edgeV * fracV;
	}
	if ( push != 0.0f )
	{
		Vector normal = CrossProduct( edgeU, edgeV );
		VectorNormalize( normal );
		point += normal * push;
	}
}

// utils/vrad/vrad_dispcoll.cpp:269-319 — DispUVToSurf_TriBLToTR.
static void DispUVToSurfTriBLToTR( const CCoreDispInfo &disp, Vector &point, float push,
	float u, float v, int snapU, int snapV, int width, int height )
{
	int nextU = snapU + 1;
	int nextV = snapV + 1;
	if ( nextU == width )
	{
		--nextU;
	}
	if ( nextV == height )
	{
		--nextV;
	}
	const float fracU = u - static_cast<float>( snapU );
	const float fracV = v - static_cast<float>( snapV );
	Vector edgeU, edgeV;
	if ( fracU < fracV )
	{
		const Vector &base = disp.GetVert( nextV * width + snapU );
		edgeU = disp.GetVert( nextV * width + nextU ) - base;
		edgeV = disp.GetVert( snapV * width + snapU ) - base;
		point = base + edgeU * fracU + edgeV * ( 1.0f - fracV );
	}
	else
	{
		const Vector &base = disp.GetVert( snapV * width + nextU );
		edgeU = disp.GetVert( snapV * width + snapU ) - base;
		edgeV = disp.GetVert( nextV * width + nextU ) - base;
		point = base + edgeU * ( 1.0f - fracU ) + edgeV * fracV;
	}
	if ( push != 0.0f )
	{
		Vector normal = CrossProduct( edgeV, edgeU );
		VectorNormalize( normal );
		point += normal * push;
	}
}

// utils/vrad/vrad_dispcoll.cpp:178-209 — DispUVToSurfPoint.
static void DispUVToSurfPoint( const CCoreDispInfo &disp, const Vector2D &uv, Vector &point, float push )
{
	if ( uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f )
	{
		return;
	}
	const int width = ( 1 << disp.GetPower() ) + 1;
	const int height = width;
	const float u = uv.x * static_cast<float>( width - 1.000001f );
	const float v = uv.y * static_cast<float>( height - 1.000001f );
	const int snapU = static_cast<int>( u );
	const int snapV = static_cast<int>( v );
	const bool odd = ( ( snapV * width + snapU ) % 2 == 1 );
	if ( odd )
	{
		DispUVToSurfTriTLToBR( disp, point, push, u, v, snapU, snapV, width, height );
	}
	else
	{
		DispUVToSurfTriBLToTR( disp, point, push, u, v, snapU, snapV, width, height );
	}
}

// utils/vrad/vrad_dispcoll.cpp:48-67,323-379 — Create copies core normals, then bilinear interpolation.
// Geometry owns the once-created, neighbor-smoothed core; no collision tree/ray data is rebuilt here.
static void DispUVToSurfNormal( const CCoreDispInfo &disp, const Vector2D &uv, Vector &normal )
{
	if ( uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f )
	{
		return;
	}
	const int width = ( 1 << disp.GetPower() ) + 1;
	const int height = width;
	const float u = uv.x * static_cast<float>( width - 1.000001f );
	const float v = uv.y * static_cast<float>( height - 1.000001f );
	const int snapU = static_cast<int>( u );
	const int snapV = static_cast<int>( v );
	int nextU = snapU + 1;
	int nextV = snapV + 1;
	if ( nextU == width )
	{
		--nextU;
	}
	if ( nextV == height )
	{
		--nextV;
	}
	const float fracU = u - static_cast<float>( snapU );
	const float fracV = v - static_cast<float>( snapV );
	Vector blended[2];
	blended[0] = disp.GetNormal( snapV * width + snapU ) * ( 1.0f - fracU ) +
		disp.GetNormal( snapV * width + nextU ) * fracU;
	VectorNormalize( blended[0] );
	blended[1] = disp.GetNormal( nextV * width + snapU ) * ( 1.0f - fracU ) +
		disp.GetNormal( nextV * width + nextU ) * fracU;
	VectorNormalize( blended[1] );
	normal = blended[0] * ( 1.0f - fracV ) + blended[1] * fracV;
	VectorNormalize( normal );
}

// utils/vrad/vraddisps.cpp:1559-1665 — CVRadDispMgr::BuildDispSamples.
static void BuildDisplacementSamples( const CCoreDispInfo &disp, ReSTIRScene &scene,
	int faceIndex, ReSTIRGpuFace &face )
{
	face.firstSample = scene.samples.Count();
	const int width = face.luxelW;
	const int height = face.luxelH;
	const float stepU = 1.0f / static_cast<float>( width );
	const float stepV = 1.0f / static_cast<float>( height );
	const float halfStepU = stepU * 0.5f;
	const float halfStepV = stepV * 0.5f;
	CUtlVector<Vector> worldPoints;
	worldPoints.SetCount( ( width + 1 ) * ( height + 1 ) );
	for ( int t = 0; t < height + 1; ++t )
	{
		for ( int s = 0; s < width + 1; ++s )
		{
			DispUVToSurfPoint( disp, Vector2D( s * stepU, t * stepV ),
				worldPoints[t * ( width + 1 ) + s], 0.0f );
		}
	}

	winding_t *winding = AllocWinding( 4 );
	winding->numpoints = 4;
	for ( int t = 0; t < height; ++t )
	{
		for ( int s = 0; s < width; ++s )
		{
			winding->p[0] = worldPoints[t * ( width + 1 ) + s];
			winding->p[1] = worldPoints[( t + 1 ) * ( width + 1 ) + s];
			winding->p[2] = worldPoints[( t + 1 ) * ( width + 1 ) + s + 1];
			winding->p[3] = worldPoints[t * ( width + 1 ) + s + 1];
			const float area = WindingArea( winding );
			const Vector2D uv( s * stepU + halfStepU, t * stepV + halfStepV );
			Vector point, normal;
			DispUVToSurfPoint( disp, uv, point, 1.0f );
			DispUVToSurfNormal( disp, uv, normal );
			Vector bumpNormals[NUM_BUMP_VECTS];
			if ( face.flags & RESTIR_FACE_BUMPED )
			{
				FillBumpNormals( face, normal, bumpNormals );
			}
			// VRAD leaves displacement mins/maxs at their zero-initialized values.
			AddSample( scene, faceIndex, s, t, Vector( uv.x, uv.y, 0.0f ),
				point, normal, vec3_origin, vec3_origin, area,
				( face.flags & RESTIR_FACE_BUMPED ) ? bumpNormals : NULL );
		}
	}
	FreeWinding( winding );
}

// utils/vrad/lightmap.cpp:847-868; vraddisps.cpp:1670-1704 — BuildFaceLuxels/BuildDispLuxels.
static void BuildFaceLuxels( ReSTIRSceneBuildContext &context, ReSTIRScene &scene,
	ReSTIRGpuFace &face, const CCoreDispInfo *disp )
{
	const dface_t &dface = g_pFaces[face.dface];
	face.firstLuxel = scene.luxels.Count();
	const Vector normal = ReSTIR_SceneV4( face.faceNormal );
	Vector luxelOrigin, worldToLuxel[2], luxelToWorld[2];
	ReSTIR_SceneCalcFaceVectors( &dface, context.faceOrigins[face.dface], normal,
		luxelOrigin, worldToLuxel, luxelToWorld );
	ReSTIR_SceneSet4( face.luxelOrigin, luxelOrigin );
	for ( int axis = 0; axis < 2; ++axis )
	{
		ReSTIR_SceneSet4( face.worldToLuxel[axis], worldToLuxel[axis] );
		ReSTIR_SceneSet4( face.luxelToWorld[axis], luxelToWorld[axis] );
	}
	const float stepU = disp ? 1.0f / static_cast<float>( face.luxelW - 1 ) : 0.0f;
	const float stepV = disp ? 1.0f / static_cast<float>( face.luxelH - 1 ) : 0.0f;
	for ( int t = 0; t < face.luxelH; ++t )
	{
		for ( int s = 0; s < face.luxelW; ++s )
		{
			Vector point, luxelNormal;
			if ( disp )
			{
				const Vector2D uv( s * stepU, t * stepV );
				DispUVToSurfPoint( *disp, uv, point, 1.0f );
				DispUVToSurfNormal( *disp, uv, luxelNormal );
			}
			else
			{
				point = ReSTIR_SceneLuxelToWorld( luxelOrigin, luxelToWorld,
					s + face.lmMins[0], t + face.lmMins[1] );
				luxelNormal = ReSTIR_ScenePhongNormal( context, face.dface, point, normal );
			}
			ReSTIRGpuLuxel luxel;
			memset( &luxel, 0, sizeof( luxel ) );
			ReSTIR_SceneSet4( luxel.position, point );
			ReSTIR_SceneSet4( luxel.normal, luxelNormal );
			scene.luxels.AddToTail( luxel );
		}
	}
}

// utils/vrad/vrad_dispcoll.cpp:84-109 — CalcSampleRadius2AndBox, direct-sample support.
static float DispSampleRadiusSquared( const texinfo_t &info )
{
	const Vector axis( info.lightmapVecsLuxelsPerWorldUnits[0][0],
		info.lightmapVecsLuxelsPerWorldUnits[0][1], info.lightmapVecsLuxelsPerWorldUnits[0][2] );
	const float width = 1.0f / VectorLength( axis );
	const float height = width;
	float radius = sqrt( width * width + height * height ) * 2.2f;
	if ( radius > 512.0f )
	{
		radius = 512.0f;
	}
	return radius * radius;
}

// utils/vrad/lightmap.cpp:2408-2431,2529-2541 — encounter order and one overflow warning.
static void AssignFaceStyles( ReSTIRSceneBuildContext &context, const ReSTIRScene &scene,
	const ReSTIRSceneLightVis &vis, ReSTIRGpuFace &face )
{
	face.numStyles = 1;
	face.styles[0] = 0;
	for ( int i = 1; i < MAXLIGHTMAPS; ++i )
	{
		face.styles[i] = 255;
	}
	bool warned = false;
	for ( int lightIndex = 0; lightIndex < scene.lights.Count(); ++lightIndex )
	{
		const ReSTIRGpuLight &light = scene.lights[lightIndex];
		const byte *pvs = vis.lightPVS.Base() + lightIndex * vis.bytes;
		if ( light.style == 0 || !LightAffectsFace( context, face, light, pvs ) )
		{
			continue;
		}
		bool duplicate = false;
		for ( int slot = 0; slot < face.numStyles; ++slot )
		{
			if ( face.styles[slot] == light.style )
			{
				duplicate = true;
				break;
			}
		}
		if ( duplicate )
		{
			continue;
		}
		if ( face.numStyles >= MAXLIGHTMAPS )
		{
			if ( !warned )
			{
				const ReSTIRGpuLuxel &luxel = scene.luxels[face.firstLuxel];
				const Vector point = ReSTIR_SceneV4( luxel.position );
				Warning( "Too many light styles on a face at (%f, %f, %f)\n", point.x, point.y, point.z );
				warned = true;
			}
			continue;
		}
		face.styles[face.numStyles++] = light.style;
	}
}

// utils/vrad/lightmap.cpp:2484-2485 — sample clusters use the stored, not illumination-pushed point.
// Include displacement luxels too so border reconstruction cannot lose an affecting style.
static void CacheDisplacementFaceClusters( ReSTIRSceneBuildContext &context,
	const ReSTIRScene &scene, const ReSTIRGpuFace &face )
{
	CUtlVector<int> *&clusters = context.faceClusterLists[face.dface];
	if ( !clusters )
	{
		clusters = new CUtlVector<int>;
	}
	for ( int sampleIndex = face.firstSample; sampleIndex < face.firstSample + face.numSamples; ++sampleIndex )
	{
		const int cluster = ReSTIR_SceneClusterFromPoint( ReSTIR_SceneV4( scene.samples[sampleIndex].position ) );
		if ( clusters->Find( cluster ) < 0 )
		{
			clusters->AddToTail( cluster );
		}
	}
	const int numLuxels = face.luxelW * face.luxelH;
	for ( int luxelIndex = face.firstLuxel; luxelIndex < face.firstLuxel + numLuxels; ++luxelIndex )
	{
		const int cluster = ReSTIR_SceneClusterFromPoint( ReSTIR_SceneV4( scene.luxels[luxelIndex].position ) );
		if ( clusters->Find( cluster ) < 0 )
		{
			clusters->AddToTail( cluster );
		}
	}
}

// utils/vrad/lightmap.cpp:3096-3101 — TEX_SPECIAL/degenerate eligibility;
// radial.cpp:676 — entity _minlight; PairEdges:151-325 — complete mapped neighbor lists.
bool ReSTIR_SceneBuildSamples( ReSTIRSceneBuildContext &context, ReSTIRScene &scene )
{
	ReSTIRSceneLightVis vis;
	if ( !BuildLightVis( scene, vis ) )
	{
		return false;
	}
	scene.dfaceToFace.SetCount( context.faceCount );
	for ( int i = 0; i < context.faceCount; ++i )
	{
		scene.dfaceToFace[i] = -1;
	}

	for ( int dfaceIndex = 0; dfaceIndex < context.faceCount; ++dfaceIndex )
	{
		const dface_t &dface = g_pFaces[dfaceIndex];
		if ( dface.texinfo < 0 || dface.texinfo >= texinfo.Count() )
		{
			continue;
		}
		const texinfo_t &info = texinfo[dface.texinfo];
		if ( info.flags & TEX_SPECIAL )
		{
			continue;
		}
		if ( dface.m_LightmapTextureSizeInLuxels[0] < 0 || dface.m_LightmapTextureSizeInLuxels[1] < 0 )
		{
			continue;
		}
		if ( ReSTIR_SceneFaceArea( &dface, context.faceOrigins[dfaceIndex] ) <= 0.0f )
		{
			continue;
		}
		const CCoreDispInfo *disp = NULL;
		if ( dface.dispinfo >= 0 )
		{
			disp = context.displacements[dface.dispinfo];
			if ( !disp )
			{
				Warning( "Missing displacement surface for face %d\n", dfaceIndex );
				return false;
			}
		}

		ReSTIRGpuFace face;
		memset( &face, 0, sizeof( face ) );
		face.dface = dfaceIndex;
		face.material = ReSTIR_GetOrAddMaterial( scene, ReSTIR_SceneFaceMaterial( &dface ),
			dtexdata[info.texdata].reflectivity, context.options->textureShadows, NULL );
		if ( context.options->textureAlbedo )
		{
			ReSTIR_LoadMaterialAlbedo( scene, face.material, ReSTIR_SceneFaceMaterial( &dface ),
				dtexdata[info.texdata].width, dtexdata[info.texdata].height );
		}
		face.lmMins[0] = dface.m_LightmapTextureMinsInLuxels[0];
		face.lmMins[1] = dface.m_LightmapTextureMinsInLuxels[1];
		face.luxelW = dface.m_LightmapTextureSizeInLuxels[0] + 1;
		face.luxelH = dface.m_LightmapTextureSizeInLuxels[1] + 1;
		face.flags = dface.dispinfo >= 0 ? RESTIR_FACE_DISP : 0;
		if ( info.flags & SURF_BUMPLIGHT )
		{
			face.flags |= RESTIR_FACE_BUMPED;
		}
		face.numChannels = ( face.flags & RESTIR_FACE_BUMPED ) ? NUM_BUMP_VECTS + 1 : 1;
		ReSTIR_SceneSet4( face.faceNormal, context.faceNormals[dfaceIndex] );
		ReSTIR_SceneSet4( face.reflectivity, dtexdata[info.texdata].reflectivity );
		if ( disp )
		{
			// Host/GPU reconstruction contract: squared world-space displacement support.
			face.reflectivity[3] = DispSampleRadiusSquared( info );
		}
		// utils/vrad/lightmap.cpp:2473-2475,3047-3049 — bump bases use texture axes, not luxel axes.
		// w carries the texel offset so the GPU can rebuild the brush texture UV at a hit.
		ReSTIR_SceneSet4( face.textureS, Vector( info.textureVecsTexelsPerWorldUnits[0][0],
			info.textureVecsTexelsPerWorldUnits[0][1], info.textureVecsTexelsPerWorldUnits[0][2] ),
			info.textureVecsTexelsPerWorldUnits[0][3] );
		ReSTIR_SceneSet4( face.textureT, Vector( info.textureVecsTexelsPerWorldUnits[1][0],
			info.textureVecsTexelsPerWorldUnits[1][1], info.textureVecsTexelsPerWorldUnits[1][2] ),
			info.textureVecsTexelsPerWorldUnits[1][3] );

		const int faceIndex = scene.faces.Count();
		BuildFaceLuxels( context, scene, face, disp );
		if ( disp )
		{
			BuildDisplacementSamples( *disp, scene, faceIndex, face );
		}
		else
		{
			BuildRegularFaceSamples( context, scene, faceIndex, face );
		}
		face.numSamples = scene.samples.Count() - face.firstSample;
		if ( disp )
		{
			CacheDisplacementFaceClusters( context, scene, face );
		}
		AssignFaceStyles( context, scene, vis, face );
		face.firstOutput = scene.numOutputValues;
		scene.numOutputValues += face.numStyles * face.numChannels * face.luxelW * face.luxelH;
		entity_t *entity = ReSTIR_SceneEntityForModel( context.faceModels[dfaceIndex] );
		const float minLight = entity ? FloatForKey( entity, "_minlight" ) * 128.0f : 0.0f;
		scene.faceMinLight.AddToTail( Vector( minLight, minLight, minLight ) );
		scene.dfaceToFace[dfaceIndex] = scene.faces.AddToTail( face );
	}
	// Build neighbors only after every dface-to-scene mapping exists. A regular
	// pair now contains both directions; VRAD's displacement exception stays directed.
	scene.faceNeighbors.RemoveAll();
	for ( int sceneFaceIndex = 0; sceneFaceIndex < scene.faces.Count(); ++sceneFaceIndex )
	{
		ReSTIRGpuFace &face = scene.faces[sceneFaceIndex];
		face.firstNeighbor = scene.faceNeighbors.Count();
		const int dfaceIndex = face.dface;
		if ( context.neighborLists[dfaceIndex] )
		{
			for ( int i = 0; i < context.neighborLists[dfaceIndex]->Count(); ++i )
			{
				const int neighborDface = (*context.neighborLists[dfaceIndex])[i];
				const int neighborFace = scene.dfaceToFace[neighborDface];
				if ( neighborFace >= 0 )
				{
					scene.faceNeighbors.AddToTail( neighborFace );
				}
			}
		}
		face.numNeighbors = scene.faceNeighbors.Count() - face.firstNeighbor;
	}
	return true;
}
