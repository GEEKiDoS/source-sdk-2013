//========= Copyright Valve Corporation, All rights reserved. ============//
// Face sample and luxel construction, ported from VRAD without lighting rays.

#include "restir_scene_internal.h"
#include "vrad_restir.h"
#include "restir_vulkan.h"
#include "bsplib.h"
#include "cmdlib.h"
#include "coordsize.h"
#include "mathlib/bumpvects.h"
#include <math.h>
#include <string.h>
#include <float.h>
#include <limits.h>

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

// GPU StandardLightRadiance (shaders/restir_lights.glsl) times the receiver cosine, without
// visibility: zero outside the radius, past a hard fade, outside a spot's outer cone, or when
// every normal faces away. Mirrors VRAD's fxdot test (lightmap.cpp:2512-2527) minus the shadow ray.
static bool PointLightReaches( const ReSTIRGpuLight &light, const Vector &origin,
	const Vector *normals, int normalCount )
{
	const Vector offset = ReSTIR_SceneV4( light.origin ) - origin;
	const float distanceSquared = offset.LengthSqr();
	if ( distanceSquared <= 0.0f )
	{
		return false;
	}
	const float distance = sqrtf( distanceSquared );
	if ( light.origin[3] > 0.0f && distance > light.origin[3] )
	{
		return false;
	}
	if ( light.fade[1] > light.fade[0] && distance > light.fade[1] )
	{
		return false;
	}
	const Vector direction = offset / distance;
	if ( light.type == emit_spotlight && -DotProduct( direction, ReSTIR_SceneV4( light.normal ) ) <= light.fade[3] )
	{
		return false;
	}
	const float evaluationDistance = MIN( MAX( distance, 1.0f ), light.fade[2] );
	const float denominator = light.attenuation[0] + evaluationDistance * light.attenuation[1] +
		evaluationDistance * evaluationDistance * light.attenuation[2];
	if ( !( denominator > 0.0f ) )
	{
		return false;
	}
	for ( int i = 0; i < normalCount; ++i )
	{
		if ( DotProduct( normals[i], direction ) > 0.0f )
		{
			return true;
		}
	}
	return false;
}

// Points of `face` an unshadowed point/spot/quake light reaches: every sample (and, on
// displacements, every luxel) at the GPU's direct-lighting origin. Appends one shadow ray per
// point to `pRays` (LightVisibility in shaders/restir_lights.glsl); with NULL, stops at the first.
static int FaceLightReachRays( const ReSTIRScene &scene, const ReSTIRGpuFace &face,
	const ReSTIRGpuLight &light, CUtlVector<ReSTIRGpuRay> *pRays )
{
	const Vector faceNormal = ReSTIR_SceneV4( face.faceNormal );
	const Vector lightOrigin = ReSTIR_SceneV4( light.origin );
	const bool bumped = ( face.flags & RESTIR_FACE_BUMPED ) != 0;
	const bool disp = ( face.flags & RESTIR_FACE_DISP ) != 0;
	const int numSamples = face.numSamples;
	const int numPoints = numSamples + ( disp ? face.luxelW * face.luxelH : 0 );
	int reached = 0;
	Vector normals[NUM_BUMP_VECTS + 1];
	for ( int k = 0; k < numPoints; ++k )
	{
		Vector origin;
		int normalCount = 1;
		if ( k < numSamples )
		{
			const ReSTIRGpuSample &sample = scene.samples[face.firstSample + k];
			origin = ReSTIR_SceneV4( sample.position ) + faceNormal;
			normals[0] = ReSTIR_SceneV4( sample.normal );
			if ( bumped )
			{
				for ( int b = 0; b < NUM_BUMP_VECTS; ++b )
				{
					normals[normalCount++] = ReSTIR_SceneV4( sample.bump[b] );
				}
			}
		}
		else
		{
			const ReSTIRGpuLuxel &luxel = scene.luxels[face.firstLuxel + k - numSamples];
			origin = ReSTIR_SceneV4( luxel.position ) + faceNormal;
			normals[0] = ReSTIR_SceneV4( luxel.normal );
		}
		if ( !PointLightReaches( light, origin, normals, normalCount ) )
		{
			continue;
		}
		++reached;
		if ( !pRays )
		{
			break;
		}
		Vector direction = lightOrigin - origin;
		const float distance = VectorNormalize( direction );
		const float epsilon = (float)DIST_EPSILON;	// shaders/restir_common.glsl RESTIR_DIST_EPSILON
		ReSTIRGpuRay &ray = pRays->Element( pRays->AddToTail() );
		ReSTIR_SceneSet4( ray.origin, origin, epsilon );
		ReSTIR_SceneSet4( ray.direction, direction, MAX( epsilon, distance - epsilon ) );
	}
	return reached;
}

// utils/vrad/lightmap.cpp:2495-2527 — a style slot is allocated only for a light that reaches
// some sample. PVS uses every cluster the face touches; point/spot/quake lights are then tested
// per sample (and per luxel on displacements, whose border reconstruction reads luxels) at the
// GPU's direct-lighting origin. Surface and sky emitters keep the plane-side test.
static bool LightAffectsFace( const ReSTIRSceneBuildContext &context, const ReSTIRScene &scene,
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
	const Vector faceNormal = ReSTIR_SceneV4( face.faceNormal );
	if ( light.type == emit_surface )
	{
		const dface_t &dface = g_pFaces[face.dface];
		const float faceDistance = dplanes[dface.planenum].dist +
			DotProduct( context.faceOrigins[face.dface], faceNormal );
		return DotProduct( faceNormal, ReSTIR_SceneV4( light.origin ) ) - faceDistance > 0.0f;
	}
	return FaceLightReachRays( scene, face, light, NULL ) > 0;
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

// Planar BSP windings can contain retraced edges, and clipping can make a
// concave cell. Absolute triangle-fan areas count those signed cancellations
// as extra surface. Accumulate signed XY moments in double precision instead;
// reversing the winding changes both signs, not its area or balance point.
static double PlanarSampleAreaAndBalancePoint( const winding_t *winding, Vector *balance )
{
	if ( balance )
	{
		balance->Init();
	}
	if ( winding->numpoints < 3 )
	{
		return 0.0f;
	}
	const Vector &origin = winding->p[0];
	double twiceArea = 0.0;
	double momentX = 0.0;
	double momentY = 0.0;
	for ( int point = 2; point < winding->numpoints; ++point )
	{
		const double ax = (double)winding->p[point - 1].x - origin.x;
		const double ay = (double)winding->p[point - 1].y - origin.y;
		const double bx = (double)winding->p[point].x - origin.x;
		const double by = (double)winding->p[point].y - origin.y;
		const double cross = ax * by - ay * bx;
		twiceArea += cross;
		if ( balance )
		{
			momentX += cross * ( ax + bx );
			momentY += cross * ( ay + by );
		}
	}
	if ( balance && twiceArea != 0.0 )
	{
		balance->x = (float)( origin.x + momentX / ( 3.0 * twiceArea ) );
		balance->y = (float)( origin.y + momentY / ( 3.0 * twiceArea ) );
	}
	return fabs( twiceArea ) * 0.5;
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
	if ( PlanarSampleAreaAndBalancePoint( remaining, NULL ) == 0.0 )
	{
		// A collinear or completely retraced winding has no receiver surface.
		// Do not turn clipping roundoff into positive-area cells on such a face.
		FreeWinding( remaining );
		return;
	}
	const float worldAreaPerLuxel = 1.0 /
		( sqrt( DotProduct( worldToLuxel[0], worldToLuxel[0] ) ) *
		sqrt( DotProduct( worldToLuxel[1], worldToLuxel[1] ) ) );
	// Cells meet at the exact luxel plane; an epsilon band must not retain
	// off-plane vertices in both independent receiver cells.
	for ( int t = 0; t < face.luxelH && remaining; ++t )
	{
		winding_t *row = NULL;
		winding_t *nextRow = NULL;
		ClipWindingEpsilon( remaining, Vector( 0, 1, 0 ), (float)t + 1.0f,
			0.0f, &nextRow, &row );
		FreeWinding( remaining );
		remaining = nextRow;
		for ( int s = 0; s < face.luxelW && row; ++s )
		{
			winding_t *nextCell = NULL;
			winding_t *cell = NULL;
			ClipWindingEpsilon( row, Vector( 1, 0, 0 ), (float)s + 1.0f,
				0.0f, &nextCell, &cell );
			FreeWinding( row );
			row = nextCell;
			if ( !cell )
			{
				continue;
			}
			Vector balance, mins, maxs;
			const float area = (float)PlanarSampleAreaAndBalancePoint( cell, &balance );
			if ( area == 0.0f )
			{
				FreeWinding( cell );
				continue;
			}
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
	float u, float v, int snapU, int snapV, int width, int height, bool *pValid )
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
		if ( pValid )
			*pValid = normal.IsValid() && normal.LengthSqr() > 0.0f;
	}
}

// utils/vrad/vrad_dispcoll.cpp:269-319 — DispUVToSurf_TriBLToTR.
static void DispUVToSurfTriBLToTR( const CCoreDispInfo &disp, Vector &point, float push,
	float u, float v, int snapU, int snapV, int width, int height, bool *pValid )
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
		if ( pValid )
			*pValid = normal.IsValid() && normal.LengthSqr() > 0.0f;
	}
}

// utils/vrad/vrad_dispcoll.cpp:178-209 — DispUVToSurfPoint.
static void DispUVToSurfPoint( const CCoreDispInfo &disp, const Vector2D &uv, Vector &point, float push,
	bool *pValid = NULL )
{
	if ( pValid )
		*pValid = false;
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
		DispUVToSurfTriTLToBR( disp, point, push, u, v, snapU, snapV, width, height, pValid );
	}
	else
	{
		DispUVToSurfTriBLToTR( disp, point, push, u, v, snapU, snapV, width, height, pValid );
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

// BSP brush faces are convex. Classify in their own world-space winding, then
// project exterior grid points onto the closest edge of THAT face (not a
// neighboring transport sample). Winding sign is independent of face orientation.
static bool BuildSunFacePolygon( const ReSTIRSceneBuildContext &context, const ReSTIRGpuFace &face,
	CUtlVector<Vector> &polygon, float &windingSign )
{
	const dface_t &dface = g_pFaces[face.dface];
	if ( dface.numedges < 3 )
		return false;
	polygon.SetCount( dface.numedges );
	for ( int edge = 0; edge < dface.numedges; ++edge )
	{
		polygon[edge] = dvertexes[ReSTIR_SceneFaceVertex( &dface, edge )].point + context.faceOrigins[face.dface];
		if ( !polygon[edge].IsValid() )
			return false;
	}
	Vector areaNormal( 0, 0, 0 );
	for ( int edge = 1; edge + 1 < polygon.Count(); ++edge )
		areaNormal += CrossProduct( polygon[edge] - polygon[0], polygon[edge + 1] - polygon[0] );
	const float signedArea = DotProduct( areaNormal, ReSTIR_SceneV4( face.faceNormal ) );
	if ( !IsFinite( signedArea ) || signedArea == 0.0f )
		return false;
	windingSign = signedArea > 0.0f ? 1.0f : -1.0f;
	return true;
}

static bool ClampSunPointToFace( const CUtlVector<Vector> &polygon, float windingSign,
	const Vector &normal, const Vector &point, Vector &receiver )
{
	if ( !point.IsValid() )
		return false;
	receiver = point - normal * ( DotProduct( point - polygon[0], normal ) / normal.LengthSqr() );
	if ( !receiver.IsValid() )
		return false;
	bool inside = true;
	for ( int edgeIndex = 0; edgeIndex < polygon.Count(); ++edgeIndex )
	{
		const Vector &start = polygon[edgeIndex];
		const Vector edge = polygon[( edgeIndex + 1 ) % polygon.Count()] - start;
		if ( edge.LengthSqr() > 0.0f && windingSign * DotProduct( CrossProduct( edge, receiver - start ), normal ) <= 0.0f )
		{
			inside = false;
			break;
		}
	}
	if ( inside )
		return true;
	bool haveClosest = false;
	float closestDistance = 0.0f;
	Vector closest;
	for ( int edgeIndex = 0; edgeIndex < polygon.Count(); ++edgeIndex )
	{
		const Vector &start = polygon[edgeIndex];
		const Vector edge = polygon[( edgeIndex + 1 ) % polygon.Count()] - start;
		const float lengthSquared = edge.LengthSqr();
		if ( lengthSquared <= 0.0f )
			continue;
		const float fraction = MAX( 0.0f, MIN( 1.0f, DotProduct( receiver - start, edge ) / lengthSquared ) );
		const Vector candidate = start + edge * fraction;
		const float distance = ( candidate - receiver ).LengthSqr();
		if ( !haveClosest || distance < closestDistance )
		{
			closest = candidate;
			closestDistance = distance;
			haveClosest = true;
		}
	}
	if ( !haveClosest )
		return false;
	// Approach boundary luxels from their own face's interior. Exact shared
	// edges have backend-dependent triangle ownership, even for a central ray.
	// The displacement is roundoff-sized relative to this polygon, not a
	// world-space leak bias, and leaves transport sample positions unchanged.
	Vector centroid( 0, 0, 0 );
	for ( int vertex = 0; vertex < polygon.Count(); ++vertex )
		centroid += polygon[vertex];
	centroid /= polygon.Count();
	receiver = closest + ( centroid - closest ) * ( 32.0f * FLT_EPSILON );
	if ( !receiver.IsValid() )
		return false;
	for ( int edgeIndex = 0; edgeIndex < polygon.Count(); ++edgeIndex )
	{
		const Vector &start = polygon[edgeIndex];
		const Vector edge = polygon[( edgeIndex + 1 ) % polygon.Count()] - start;
		if ( edge.LengthSqr() > 0.0f && windingSign * DotProduct( CrossProduct( edge, receiver - start ), normal ) <= 0.0f )
			return false;
	}
	return true;
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
	// Only the internal sampling domain changes. The selected dface/grid and
	// texinfo remain byte-for-byte native; reconstruction uses these forward vectors.
	const int density = context.options->shadowMaps ? context.options->highresDensity : 1;
	for ( int axis = 0; axis < 2; ++axis )
	{
		worldToLuxel[axis] *= density;
		luxelToWorld[axis] /= density;
	}
	ReSTIR_SceneSet4( face.luxelOrigin, luxelOrigin );
	for ( int axis = 0; axis < 2; ++axis )
	{
		ReSTIR_SceneSet4( face.worldToLuxel[axis], worldToLuxel[axis] );
		ReSTIR_SceneSet4( face.luxelToWorld[axis], luxelToWorld[axis] );
	}
	const bool hasSun = context.options->shadowMaps && scene.skyLight >= 0 && scene.skyLight < scene.lights.Count() &&
		scene.lights[scene.skyLight].type == emit_skylight &&
		( scene.lights[scene.skyLight].lightFlags & RESTIR_LIGHT_RUNTIME_DIRECT ) != 0;
	CUtlVector<Vector> sunPolygon;
	float windingSign = 0.0f;
	const float determinant = hasSun && !disp ? DotProduct( normal, CrossProduct( worldToLuxel[1], worldToLuxel[0] ) ) : 0.0f;
	const bool validNormal = hasSun && normal.IsValid() && normal.LengthSqr() > 0.0f;
	const bool validSunFace = hasSun && validNormal && ( disp ?
		( face.luxelW >= 1 && face.luxelH >= 1 ) :
		( IsFinite( determinant ) && fabs( determinant ) >= 1.0e-20 &&
			BuildSunFacePolygon( context, face, sunPolygon, windingSign ) ) );
	const float stepU = disp && face.luxelW > 1 ? 1.0f / static_cast<float>( face.luxelW - 1 ) : 0.0f;
	const float stepV = disp && face.luxelH > 1 ? 1.0f / static_cast<float>( face.luxelH - 1 ) : 0.0f;
	for ( int t = 0; t < face.luxelH; ++t )
	{
		for ( int s = 0; s < face.luxelW; ++s )
		{
			Vector point, luxelNormal;
			bool surfaceValid = true;
			if ( disp )
			{
				const Vector2D uv( s * stepU, t * stepV );
				DispUVToSurfPoint( *disp, uv, point, 1.0f, hasSun ? &surfaceValid : NULL );
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
			if ( hasSun )
			{
				Vector receiver = point;
				const bool valid = validSunFace && surfaceValid && point.IsValid() &&
					( disp || ClampSunPointToFace( sunPolygon, windingSign, normal, point, receiver ) );
				// Displacements already carry their geometry push. Only append the
				// same face-normal offset as ShadingOrigin; never change luxel.position.
				const Vector origin = receiver + normal;
				scene.sunVisibilityOrigins.AddToTail( valid && origin.IsValid() ?
					Vector4D( origin.x, origin.y, origin.z, 1.0f ) : Vector4D( 0, 0, 0, 0 ) );
			}
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

// Source slots stay in light encounter order; enhanced transport can carry the full style domain.
static bool AllocateFaceStyles( const ReSTIRScene &scene, const int *lightIndices, int count,
	int maxStyles, ReSTIRGpuFace &face )
{
	face.numStyles = 1;
	face.styles[0] = 0;
	for ( int i = 1; i < RESTIR_MAX_FACE_STYLES; ++i )
	{
		face.styles[i] = 255;
	}
	bool fits = true;
	for ( int i = 0; i < count; ++i )
	{
		const int style = scene.lights[lightIndices[i]].style;
		bool present = false;
		for ( int slot = 0; slot < face.numStyles; ++slot )
		{
			present |= face.styles[slot] == style;
		}
		if ( present )
		{
			continue;
		}
		if ( face.numStyles >= maxStyles )
		{
			fits = false;
			continue;
		}
		face.styles[face.numStyles++] = style;
	}
	return fits;
}

// utils/vrad/lightmap.cpp:2529-2541 — one overflow warning per face.
static void WarnFaceStyleOverflow( const ReSTIRScene &scene, const ReSTIRGpuFace &face )
{
	const Vector point = ReSTIR_SceneV4( scene.luxels[face.firstLuxel].position );
	Warning( "Too many light styles on a face at (%f, %f, %f)\n", point.x, point.y, point.z );
}

// More than four source candidates need shadow-ray resolution even when all fit in enhanced
// transport. Keep the complete provisional source layout; resolution can only shrink it.
static void AssignFaceStyles( ReSTIRSceneBuildContext &context, ReSTIRScene &scene,
	const ReSTIRSceneLightVis &vis, int faceIndex, ReSTIRGpuFace &face )
{
	const int first = scene.styleCandidateLights.Count();
	for ( int lightIndex = 0; lightIndex < scene.lights.Count(); ++lightIndex )
	{
		const ReSTIRGpuLight &light = scene.lights[lightIndex];
		const byte *pvs = vis.lightPVS.Base() + lightIndex * vis.bytes;
		if ( light.style != 0 && LightAffectsFace( context, scene, face, light, pvs ) )
		{
			scene.styleCandidateLights.AddToTail( lightIndex );
		}
	}
	const int count = scene.styleCandidateLights.Count() - first;
	const int maxStyles = context.options->shadowMaps ? RESTIR_MAX_FACE_STYLES : MAXLIGHTMAPS;
	const bool fits = AllocateFaceStyles( scene, scene.styleCandidateLights.Base() + first, count, maxStyles, face );
	if ( fits && face.numStyles <= MAXLIGHTMAPS )
	{
		scene.styleCandidateLights.SetCountNonDestructively( first );
		return;
	}
	scene.styleOverflowFaces.AddToTail( faceIndex );
	scene.styleCandidateFirst.AddToTail( first );
	scene.styleCandidateCount.AddToTail( count );
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
	// Build() resets the entire scene; also make direct sample rebuilds discard
	// stale receiver origins and all of their geometric/style indexing arrays.
	scene.sunVisibilityOrigins.RemoveAll();
	scene.faces.RemoveAll();
	scene.samples.RemoveAll();
	scene.luxels.RemoveAll();
	scene.faceNeighbors.RemoveAll();
	scene.faceMinLight.RemoveAll();
	scene.styleOverflowFaces.RemoveAll();
	scene.styleCandidateFirst.RemoveAll();
	scene.styleCandidateCount.RemoveAll();
	scene.styleCandidateLights.RemoveAll();
	scene.numOutputValues = 0;
	scene.receiverStyleMask = ~uint64( 0 );
	if (context.options->shadowMaps)
	{
		for (int i = 0; i < scene.lights.Count(); ++i)
		{
			if (scene.lights[i].style < 0 || scene.lights[i].style >= 64)
			{
				Warning("Hlight: light %d authored style %d exceeds the native 64-style domain\n",i,scene.lights[i].style);
				return false;
			}
		}
		scene.receiverStyleMask = uint64( 1 );
		for ( int i = 0; i < scene.lights.Count(); ++i )
		{
			const ReSTIRGpuLight &light = scene.lights[i];
			if ( !( light.lightFlags & RESTIR_LIGHT_RUNTIME_DIRECT ) )
			{
				scene.receiverStyleMask |= uint64( 1 ) << light.style;
			}
		}
	}
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
		const int density = context.options->shadowMaps ? context.options->highresDensity : 1;
		for ( int axis = 0; axis < 2; ++axis )
		{
			if ( (int64)dface.m_LightmapTextureSizeInLuxels[axis] * density + 3 > 16384 ||
				(int64)dface.m_LightmapTextureMinsInLuxels[axis] * density < INT_MIN ||
				(int64)dface.m_LightmapTextureMinsInLuxels[axis] * density > INT_MAX )
			{
				Warning( "Hlight: face %d axis %d cannot fit requested density %d (native extent %d)\n",
					dfaceIndex, axis, density, dface.m_LightmapTextureSizeInLuxels[axis] );
				return false;
			}
			face.lmMins[axis] = dface.m_LightmapTextureMinsInLuxels[axis] * density;
		}
		face.luxelW = dface.m_LightmapTextureSizeInLuxels[0] * density + 1;
		face.luxelH = dface.m_LightmapTextureSizeInLuxels[1] * density + 1;
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
			face.reflectivity[3] = DispSampleRadiusSquared( info ) / ( (float)density * density );
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
		if ( face.numSamples == 0 && scene.sunVisibilityOrigins.Count() > 0 )
		{
			for ( int luxel = face.firstLuxel; luxel < face.firstLuxel + face.luxelW * face.luxelH; ++luxel )
				scene.sunVisibilityOrigins[luxel].Init( 0, 0, 0, 0 );
		}
		if ( disp )
		{
			CacheDisplacementFaceClusters( context, scene, face );
		}
		AssignFaceStyles( context, scene, vis, faceIndex, face );
		face.firstOutput = scene.numOutputValues;
		const int64 outputs = (int64)face.numStyles * face.numChannels * face.luxelW * face.luxelH;
		if ( outputs > INT_MAX - scene.numOutputValues )
		{
			Warning( "Hlight: face %d exceeds signed GPU output indexing at density %d\n", dfaceIndex, density );
			return false;
		}
		scene.numOutputValues += (int)outputs;
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

// utils/vrad/lightmap.cpp:2512-2541 — overflowing faces keep only the candidates whose shadow
// ray reaches the light from at least one point, in light order; a real overflow warns like VRAD.
// Slots only shrink, so the output layout sized from the provisional styles stays valid.
bool ReSTIR_ResolveFaceStyles( ReSTIRScene &scene, CReSTIRVulkanDevice &device )
{
	if ( !scene.styleOverflowFaces.Count() )
	{
		return true;
	}
	// Per candidate: [rayFirst, +rayCount) shadow rays; rayCount < 0 = emitter kept without rays.
	const int numCandidates = scene.styleCandidateLights.Count();
	CUtlVector<ReSTIRGpuRay> rays;
	CUtlVector<int> rayFirst, rayCount;
	rayFirst.SetCount( numCandidates );
	rayCount.SetCount( numCandidates );
	for ( int o = 0; o < scene.styleOverflowFaces.Count(); ++o )
	{
		const ReSTIRGpuFace &face = scene.faces[scene.styleOverflowFaces[o]];
		for ( int c = scene.styleCandidateFirst[o]; c < scene.styleCandidateFirst[o] + scene.styleCandidateCount[o]; ++c )
		{
			const ReSTIRGpuLight &light = scene.lights[scene.styleCandidateLights[c]];
			rayFirst[c] = rays.Count();
			rayCount[c] = ( light.type == emit_surface || light.type == emit_skylight || light.type == emit_skyambient ) ?
				-1 : FaceLightReachRays( scene, face, light, &rays );
		}
	}
	CUtlVector<ReSTIRGpuHit> hits;
	if ( rays.Count() && !device.TraceRays( rays, RESTIR_RAY_MASK_SHADOW, hits ) )
	{
		return false;
	}
	int warnings = 0;
	CUtlVector<int> visible;
	for ( int o = 0; o < scene.styleOverflowFaces.Count(); ++o )
	{
		ReSTIRGpuFace &face = scene.faces[scene.styleOverflowFaces[o]];
		visible.RemoveAll();
		for ( int c = scene.styleCandidateFirst[o]; c < scene.styleCandidateFirst[o] + scene.styleCandidateCount[o]; ++c )
		{
			bool reaches = rayCount[c] < 0;
			for ( int r = rayFirst[c]; r < rayFirst[c] + rayCount[c] && !reaches; ++r )
			{
				reaches = hits[r].triangle < 0;
			}
			if ( reaches )
			{
				visible.AddToTail( scene.styleCandidateLights[c] );
			}
		}
		const int maxStyles = g_ReSTIROptions.shadowMaps ? RESTIR_MAX_FACE_STYLES : MAXLIGHTMAPS;
		const bool fits = AllocateFaceStyles( scene, visible.Base(), visible.Count(), maxStyles, face );
		int receiverStyles = 0;
		for ( int slot = 0; slot < face.numStyles; ++slot )
		{
			receiverStyles += scene.IsReceiverStyle( face.styles[slot] ) ? 1 : 0;
		}
		if ( !fits || receiverStyles > MAXLIGHTMAPS )
		{
			WarnFaceStyleOverflow( scene, face );
			++warnings;
		}
	}
	Msg( "VRAD ReSTIR: %d faces reached by more than %d source light styles; %d receiver style overflows after shadow rays\n",
		scene.styleOverflowFaces.Count(), MAXLIGHTMAPS, warnings );
	if ( warnings && g_ReSTIROptions.shadowMaps )
	{
		Warning( "Hlight: refusing to discard authored styles from %d overflowing faces\n", warnings );
		return false;
	}
	scene.styleOverflowFaces.Purge();
	scene.styleCandidateFirst.Purge();
	scene.styleCandidateCount.Purge();
	scene.styleCandidateLights.Purge();
	return device.UpdateFaceStyles( scene );
}
