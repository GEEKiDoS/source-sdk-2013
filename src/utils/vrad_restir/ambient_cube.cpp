//========= Copyright Valve Corporation, All rights reserved. ============//
// Per-leaf ambient cube generation for VRAD ReSTIR.

#include "ambient_cube.h"
#include "restir_vulkan.h"
#include "bsplib.h"
#include "bsptreedata.h"
#include "cmdlib.h"
#include "polylib.h"

#include "coordsize.h"
#include "mathlib/anorms.h"
#include "vstdlib/random.h"

#include <float.h>

extern dface_t *g_pFaces;

namespace
{
static const Vector s_BoxDirections[6] =
{
	Vector( 1, 0, 0 ), Vector( -1, 0, 0 ),
	Vector( 0, 1, 0 ), Vector( 0, -1, 0 ),
	Vector( 0, 0, 1 ), Vector( 0, 0, -1 )
};

struct AmbientSample
{
	Vector pos;
	Vector cube[6];
};

struct AmbientCandidate
{
	int leaf;
	int tries;
	bool accepted;
	Vector position;
};

struct AmbientCube
{
	Vector side[6];
};

struct AmbientDirectRay
{
	int query;
	int light;
};

// Port of utils/vrad/vrad.cpp:146-162 (MakeParents).
static void MakeParents( int nodeIndex, int parent, CUtlVector<int> &leafParents, CUtlVector<int> &nodeParents )
{
	if ( nodeIndex < 0 || nodeIndex >= numnodes )
		return;

	nodeParents[nodeIndex] = parent;
	dnode_t *node = &dnodes[nodeIndex];
	for ( int side = 0; side < 2; ++side )
	{
		int child = node->children[side];
		if ( child < 0 )
		{
			int leaf = -child - 1;
			if ( leaf >= 0 && leaf < numleafs )
				leafParents[leaf] = nodeIndex;
		}
		else
		{
			MakeParents( child, nodeIndex, leafParents, nodeParents );
		}
	}
}

// Port of utils/vrad/leaf_ambient_lighting.cpp:278-303.
static void GetLeafBoundaryPlanes( CUtlVector<dplane_t> &list, int leafIndex, const CUtlVector<int> &leafParents, const CUtlVector<int> &nodeParents )
{
	list.RemoveAll();
	if ( leafIndex < 0 || leafIndex >= leafParents.Count() )
		return;

	int nodeIndex = leafParents[leafIndex];
	int child = -(leafIndex + 1);
	while ( nodeIndex >= 0 && nodeIndex < numnodes )
	{
		dnode_t *node = &dnodes[nodeIndex];
		dplane_t *nodePlane = &dplanes[node->planenum];
		if ( node->children[0] == child )
		{
			list.AddToTail( *nodePlane );
		}
		else
		{
			int planeIndex = list.AddToTail();
			list[planeIndex].dist = -nodePlane->dist;
			list[planeIndex].normal = -nodePlane->normal;
			list[planeIndex].type = nodePlane->type;
		}
		child = nodeIndex;
		nodeIndex = nodeParents[child];
	}
}

// Port of utils/vrad/leaf_ambient_lighting.cpp:211-270. The six probes are
// emitted by ReSTIR_ComputeLeafAmbientLighting in a cross-leaf batch.
static bool GenerateLeafSamplePosition( int leafIndex, const CUtlVector<dplane_t> &leafPlanes, CUniformRandomStream &random, Vector &samplePosition )
{
	dleaf_t *leaf = &dleafs[leafIndex];
	float dx = leaf->maxs[0] - leaf->mins[0];
	float dy = leaf->maxs[1] - leaf->mins[1];
	float dz = leaf->maxs[2] - leaf->mins[2];

	samplePosition.x = leaf->mins[0] + random.RandomFloat( 0, dx );
	samplePosition.y = leaf->mins[1] + random.RandomFloat( 0, dy );
	samplePosition.z = leaf->mins[2] + random.RandomFloat( 0, dz );
	for ( int j = leafPlanes.Count(); --j >= 0; )
	{
		float d = DotProduct( leafPlanes[j].normal, samplePosition ) - leafPlanes[j].dist;
		if ( d < DIST_EPSILON )
			return false;
	}
	return true;
}

// Port of utils/vrad/trace.cpp:54-82 (TraceLeafBrushes) + :274-332 (DM_ClipBoxToBrush point
// case) start-solid detection: a point is inside an opaque brush of the leaf when every
// non-bevel side has d <= 0. VRAD's CastRayInLeaf reports t == 0 for such a start point.
static bool PointInOpaqueLeafBrush( int leafIndex, const Vector &point )
{
	const dleaf_t *leaf = &dleafs[leafIndex];
	for ( int i = 0; i < leaf->numleafbrushes; ++i )
	{
		const dbrush_t *brush = &dbrushes[dleafbrushes[leaf->firstleafbrush + i]];
		if ( !( brush->contents & MASK_OPAQUE ) || !brush->numsides )
			continue;
		bool startout = false;
		for ( int s = 0; s < brush->numsides && !startout; ++s )
		{
			const dbrushside_t *side = &dbrushsides[brush->firstside + s];
			if ( side->bevel == 1 )
				continue;
			const dplane_t *plane = &dplanes[side->planenum];
			if ( DotProduct( point, plane->normal ) - plane->dist > 0 )
				startout = true;
		}
		if ( !startout )
			return true;
	}
	return false;
}

// Port of utils/vrad/leaf_ambient_lighting.cpp:314-382.
static void AddSampleToList( CUtlVector<AmbientSample> &list, const Vector &samplePosition, const AmbientCube &cube )
{
	const int MAX_SAMPLES = 16;
	int index = list.AddToTail();
	list[index].pos = samplePosition;
	for ( int i = 0; i < 6; ++i )
		list[index].cube[i] = cube.side[i];

	if ( list.Count() <= MAX_SAMPLES )
		return;

	int nearestNeighborIndex = 0;
	float nearestNeighborDist = FLT_MAX;
	float nearestNeighborTotal = 0;
	for ( int i = 0; i < list.Count(); ++i )
	{
		int closestIndex = 0;
		float closestDist = FLT_MAX;
		float totalDC = 0;
		for ( int j = 0; j < list.Count(); ++j )
		{
			if ( j == i )
				continue;
			float dist = ( list[i].pos - list[j].pos ).Length();
			float maxDC = 0;
			for ( int k = 0; k < 6; ++k )
			{
				for ( int s = 0; s < 3; ++s )
				{
					float dc = fabs( list[i].cube[k][s] - list[j].cube[k][s] );
					maxDC = max( maxDC, dc );
				}
				totalDC += maxDC;
			}
			if ( maxDC < 1e-4f )
				maxDC = 0;
			else if ( maxDC > 1.0f )
				maxDC = 1.0f;
			float distanceFactor = 0.1f + maxDC * 0.9f;
			dist *= distanceFactor;
			if ( dist < closestDist )
			{
				closestDist = dist;
				closestIndex = j;
			}
		}
		if ( closestDist < nearestNeighborDist || ( closestDist == nearestNeighborDist && totalDC < nearestNeighborTotal ) )
		{
			// utils/vrad/leaf_ambient_lighting.cpp:375-379 never updates nearestNeighborTotal; kept as-is.
			nearestNeighborDist = closestDist;
			nearestNeighborIndex = i;
		}
	}
	list.FastRemove( nearestNeighborIndex );
}

// Port of utils/vrad/leaf_ambient_lighting.cpp:384-400.
static int CubeDeltaGammaSpace( const AmbientCube &cube0, const AmbientCube &cube1 )
{
	int maxDelta = 0;
	for ( int i = 0; i < 6; ++i )
	{
		for ( int j = 0; j < 3; ++j )
		{
			int val0 = LinearToScreenGamma( cube0.side[i][j] );
			int val1 = LinearToScreenGamma( cube1.side[i][j] );
			int delta = abs( val0 - val1 );
			if ( delta > maxDelta )
				maxDelta = delta;
		}
	}
	return maxDelta;
}

// Port of utils/vrad/leaf_ambient_lighting.cpp:404-429.
static void ModLeafAmbientColorAtPos( AmbientCube &out, const Vector &pos, const CUtlVector<AmbientSample> &list, int skipIndex )
{
	for ( int i = 0; i < 6; ++i )
		out.side[i].Init();

	float totalFactor = 0;
	for ( int i = 0; i < list.Count(); ++i )
	{
		if ( i == skipIndex )
			continue;
		float dist = ( list[i].pos - pos ).LengthSqr();
		float factor = 1.0f / ( dist + 1.0f );
		totalFactor += factor;
		for ( int j = 0; j < 6; ++j )
			out.side[j] += list[i].cube[j] * factor;
	}
	for ( int i = 0; i < 6; ++i )
		out.side[i] *= 1.0f / totalFactor;
}

// Port of utils/vrad/leaf_ambient_lighting.cpp:432-447.
static void CompressAmbientSampleList( CUtlVector<AmbientSample> &list )
{
	AmbientCube testCube;
	for ( int i = 0; i < list.Count(); ++i )
	{
		if ( list.Count() > 1 )
		{
			ModLeafAmbientColorAtPos( testCube, list[i].pos, list, i );
			AmbientCube originalCube;
			for ( int side = 0; side < 6; ++side )
				originalCube.side[side] = list[i].cube[side];
			if ( CubeDeltaGammaSpace( testCube, originalCube ) < 3 )
			{
				list.FastRemove( i );
				--i;
			}
		}
	}
}

// Port of utils/vrad/leaf_ambient_lighting.cpp:449-459.
static float AABBDistance( const Vector &mins0, const Vector &maxs0, const Vector &mins1, const Vector &maxs1 )
{
	Vector delta;
	for ( int i = 0; i < 3; ++i )
	{
		float greatestMin = max( mins0[i], mins1[i] );
		float leastMax = min( maxs0[i], maxs1[i] );
		delta[i] = ( greatestMin < leastMax ) ? 0 : ( leastMax - greatestMin );
	}
	return delta.Length();
}

class CAmbientLeafList : public ISpatialLeafEnumerator
{
public:
	virtual bool EnumerateLeaf( int leaf, intp context )
	{
		m_list.AddToTail( leaf );
		return true;
	}
	CUtlVector<int> m_list;
};

// Port of utils/vrad/leaf_ambient_lighting.cpp:475-482.
static void LeafBounds( int leafIndex, Vector &mins, Vector &maxs )
{
	for ( int i = 0; i < 3; ++i )
	{
		mins[i] = dleafs[leafIndex].mins[i];
		maxs[i] = dleafs[leafIndex].maxs[i];
	}
}

// Port of utils/vrad/leaf_ambient_lighting.cpp:487-511.
static int NearestNeighborWithLight( int leafID )
{
	Vector mins, maxs;
	LeafBounds( leafID, mins, maxs );
	Vector size = maxs - mins;
	CAmbientLeafList leafList;
	ToolBSPTree()->EnumerateLeavesInBox( mins - size, maxs + size, &leafList, 0 );
	float bestDist = FLT_MAX;
	int bestIndex = leafID;
	for ( int i = 0; i < leafList.m_list.Count(); ++i )
	{
		int testIndex = leafList.m_list[i];
		if ( !g_pLeafAmbientIndex->Element( testIndex ).ambientSampleCount )
			continue;
		Vector testMins, testMaxs;
		LeafBounds( testIndex, testMins, testMaxs );
		float dist = AABBDistance( mins, maxs, testMins, testMaxs );
		if ( dist < bestDist )
		{
			bestDist = dist;
			bestIndex = testIndex;
		}
	}
	return bestIndex;
}

// Port of utils/vrad/leaf_ambient_lighting.cpp:514-521.
static byte Fixed8Fraction( float t, float tMin, float tMax )
{
	if ( tMax <= tMin )
		return 0;
	float frac = RemapValClamped( t, tMin, tMax, 0.0f, 255.0f );
	return byte( frac + 0.5f );
}

// Ports utils/vrad/leaf_ambient_lighting.cpp:58-89 and :92-136.
static float AmbientWorldLightDistanceFalloff( const dworldlight_t *light, const Vector &delta )
{
	if ( light->radius != 0 && DotProduct( delta, delta ) > light->radius * light->radius )
		return 0.0f;
	return InvRSquared( delta );
}

static float AmbientWorldLightAngle( const dworldlight_t *light, const Vector &lightNormal, const Vector &sampleNormal, const Vector &delta )
{
	float dot = DotProduct( sampleNormal, delta );
	if ( dot < 0 )
		return 0.0f;
	float dot2 = -DotProduct( delta, lightNormal );
	if ( dot2 <= ON_EPSILON / 10 )
		return 0.0f;
	return dot * dot2;
}

static bool IsLeafAmbientSurfaceLight( const dworldlight_t *light )
{
	// Port of utils/vrad/leaf_ambient_lighting.cpp:183-198.
	static const float worldLightMinEmitSurface = 0.005f;
	static const float worldLightMinEmitSurfaceDistanceRatio = InvRSquared( Vector( 0, 0, 512 ) );
	if ( light->type != emit_surface || light->style != 0 )
		return false;
	float intensity = max( light->intensity[0], light->intensity[1] );
	intensity = max( intensity, light->intensity[2] );
	return ( intensity * worldLightMinEmitSurfaceDistanceRatio ) < worldLightMinEmitSurface;
}

static bool IsDisplacementHit( const ReSTIRScene &scene, const ReSTIRGpuHit &hit )
{
	if ( hit.triangle < 0 || hit.triangle >= scene.triangles.Count() )
		return false;
	if ( hit.face >= 0 && hit.face < MAX_MAP_FACES && g_pFaces && g_pFaces[hit.face].dispinfo != -1 )
		return true;
	if ( hit.face < 0 || hit.face >= scene.dfaceToFace.Count() )
		return false;
	int face = scene.dfaceToFace[hit.face];
	return face >= 0 && face < scene.faces.Count() && ( scene.faces[face].flags & RESTIR_FACE_DISP ) != 0;
}

static void SetRandomSeedForLeaf( CUniformRandomStream &random, const ReSTIROptions &options, int leaf )
{
	unsigned int seed = (unsigned int)options.seed;
	seed ^= 0x9E3779B9u + (unsigned int)leaf + ( seed << 6 ) + ( seed >> 2 );
	random.SetSeed( (int)seed );
}

static int LeafSampleCount( int leafID )
{
	// Port of utils/vrad/leaf_ambient_lighting.cpp:532-545. ReSTIR has no
	// -fastambient option, so the normal volume heuristic is always used.
	int xSize = ( dleafs[leafID].maxs[0] - dleafs[leafID].mins[0] ) / 32;
	int ySize = ( dleafs[leafID].maxs[1] - dleafs[leafID].mins[1] ) / 32;
	int zSize = ( dleafs[leafID].maxs[2] - dleafs[leafID].mins[2] ) / 64;
	xSize = max( xSize, 1 );
	ySize = max( xSize, 1 );
	zSize = max( xSize, 1 );
	int volumeCount = xSize * ySize * zSize;
	return clamp( volumeCount, 1, 128 );
}


// Port of utils/vrad/leaf_ambient_lighting.cpp:525-566. Candidate
// generation is cross-leaf batched so every rejection round uses one GPU
// probe dispatch.
static bool BuildLeafCandidates( const ReSTIROptions &options, const ReSTIRScene &scene, CReSTIRVulkanDevice &device, const CUtlVector< CUtlVector<dplane_t> > &leafPlanes, CUtlVector<AmbientCandidate> &candidates )
{
	CUtlVector<CUniformRandomStream> randomStreams;
	randomStreams.SetCount( numleafs );
	for ( int leaf = 0; leaf < numleafs; ++leaf )
		SetRandomSeedForLeaf( randomStreams[leaf], options, leaf );

	CUtlVector<int> pending;
	for ( int leaf = 0; leaf < numleafs; ++leaf )
	{
		if ( dleafs[leaf].contents & CONTENTS_SOLID )
			continue;
		int sampleCount = LeafSampleCount( leaf );
		for ( int sample = 0; sample < sampleCount; ++sample )
		{
			int index = candidates.AddToTail();
			candidates[index].leaf = leaf;
			candidates[index].tries = 0;
			candidates[index].accepted = false;
			candidates[index].position.Init();
			pending.AddToTail( index );
		}
	}

	CUtlVector<ReSTIRGpuRay> rays;
	CUtlVector<ReSTIRGpuHit> hits;
	while ( pending.Count() > 0 )
	{
		rays.RemoveAll();
		CUtlVector<int> rayOffsets;
		rayOffsets.SetCount( pending.Count() );
		CUtlVector<int> nextPending;

		for ( int p = 0; p < pending.Count(); ++p )
		{
			int candidateIndex = pending[p];
			AmbientCandidate &candidate = candidates[candidateIndex];
			++candidate.tries;
			bool inside = GenerateLeafSamplePosition( candidate.leaf, leafPlanes[candidate.leaf], randomStreams[candidate.leaf], candidate.position );
			// VRAD CastRayInLeaf t == 0: start point inside an opaque leaf brush (func_detail) -> try again.
			if ( inside && PointInOpaqueLeafBrush( candidate.leaf, candidate.position ) )
				inside = false;
			if ( !inside )
			{
				rayOffsets[p] = -1;
				if ( candidate.tries >= 1000 )
				{
					candidate.position = ( Vector( dleafs[candidate.leaf].mins[0], dleafs[candidate.leaf].mins[1], dleafs[candidate.leaf].mins[2] ) + Vector( dleafs[candidate.leaf].maxs[0], dleafs[candidate.leaf].maxs[1], dleafs[candidate.leaf].maxs[2] ) ) * 0.5f;
					candidate.accepted = true;
				}
				else
					nextPending.AddToTail( candidateIndex );
				continue;
			}

			rayOffsets[p] = rays.Count();
			for ( int side = 0; side < 6; ++side )
			{
				Vector endpoint = candidate.position;
				int axis = side % 3;
				endpoint[axis] = ( side < 3 ) ? dleafs[candidate.leaf].mins[axis] : dleafs[candidate.leaf].maxs[axis];
				Vector delta = endpoint - candidate.position;
				ReSTIRGpuRay ray;
				ray.origin[0] = candidate.position.x;
				ray.origin[1] = candidate.position.y;
				ray.origin[2] = candidate.position.z;
				ray.origin[3] = 0.0f;
				ray.direction[0] = delta.x;
				ray.direction[1] = delta.y;
				ray.direction[2] = delta.z;
				ray.direction[3] = 1.0f;
				rays.AddToTail( ray );
			}
		}

		if ( rays.Count() > 0 )
		{
			// CastRayInLeaf tests leaf brushes + displacements: both are RESTIR_TRI_SHADOW geometry.
			if ( !device.TraceRays( rays, RESTIR_RAY_MASK_SHADOW, hits ) || hits.Count() != rays.Count() )
			{
				Warning( "VRAD ReSTIR: ambient probe tracing failed.\n" );
				return false;
			}
		}

		for ( int p = 0; p < pending.Count(); ++p )
		{
			int candidateIndex = pending[p];
			AmbientCandidate &candidate = candidates[candidateIndex];
			if ( candidate.accepted || rayOffsets[p] < 0 )
				continue;
			bool valid = true;
			int rayStart = rayOffsets[p];
			for ( int side = 0; side < 6; ++side )
			{
				const ReSTIRGpuHit &hit = hits[rayStart + side];
				if ( hit.t == 0.0f )
				{
					valid = false;
					break;
				}
				if ( hit.t >= 0.0f && hit.t != 1.0f && IsDisplacementHit( scene, hit ) )
				{
					int axis = side % 3;
					Vector endpoint = candidate.position;
					endpoint[axis] = ( side < 3 ) ? dleafs[candidate.leaf].mins[axis] : dleafs[candidate.leaf].maxs[axis];
					Vector delta = endpoint - candidate.position;
					Vector normal( hit.normal[0], hit.normal[1], hit.normal[2] );
					if ( DotProduct( delta, normal ) > 0 )
					{
						valid = false;
						break;
					}
				}
			}
			if ( valid )
			{
				candidate.accepted = true;
			}
			else if ( candidate.tries >= 1000 )
			{
				candidate.position = ( Vector( dleafs[candidate.leaf].mins[0], dleafs[candidate.leaf].mins[1], dleafs[candidate.leaf].mins[2] ) + Vector( dleafs[candidate.leaf].maxs[0], dleafs[candidate.leaf].maxs[1], dleafs[candidate.leaf].maxs[2] ) ) * 0.5f;
				candidate.accepted = true;
			}
			else
			{
				nextPending.AddToTail( candidateIndex );
			}
		}

		pending.RemoveAll();
		for ( int i = 0; i < nextPending.Count(); ++i )
			pending.AddToTail( nextPending[i] );
	}
	return true;
}
}

bool ReSTIR_ComputeLeafAmbientLighting( const ReSTIROptions &options, const ReSTIRScene &scene, CReSTIRVulkanDevice &device, const ReSTIRLightmapResult &lightmap )
{
	(void)lightmap;
	if ( !g_pLeafAmbientIndex || !g_pLeafAmbientLighting )
	{
		Warning( "VRAD ReSTIR: ambient lump vectors are not selected.\n" );
		return false;
	}
	if ( scene.sceneStyles.Count() <= 0 )
	{
		Warning( "VRAD ReSTIR: scene has no style-0 slot for ambient gather.\n" );
		return false;
	}

	// Port of utils/vrad/leaf_ambient_lighting.cpp:621-642. bsp_output has
	// removed selected analytic lights; retained worldlights stay in bake order.
	// Only INAMBIENTCUBE belongs to this reflected/weak-surface gather.
	// bsp_output finalizes the written worldlight CRC after this update.
	int nInAmbientCube = 0;
	int nSurfaceLights = 0;
	for ( int i = 0; i < *pNumworldlights; ++i )
	{
		dworldlight_t *light = &dworldlights[i];
		if ( IsLeafAmbientSurfaceLight( light ) )
			light->flags |= DWL_FLAGS_INAMBIENTCUBE;
		else
			light->flags &= ~DWL_FLAGS_INAMBIENTCUBE;
		if ( light->type == emit_surface )
			++nSurfaceLights;
		if ( light->flags & DWL_FLAGS_INAMBIENTCUBE )
			++nInAmbientCube;
	}
	Msg( "%d of %d (%d%% of) surface lights went in leaf ambient cubes.\n", nInAmbientCube, nSurfaceLights, nSurfaceLights ? ( nInAmbientCube * 100 ) / nSurfaceLights : 0 );

	CUtlVector<int> leafParents;
	CUtlVector<int> nodeParents;
	leafParents.SetCount( numleafs );
	nodeParents.SetCount( numnodes );
	for ( int leaf = 0; leaf < leafParents.Count(); ++leaf )
		leafParents[leaf] = -1;
	for ( int node = 0; node < nodeParents.Count(); ++node )
		nodeParents[node] = -1;
	if ( numnodes > 0 )
		MakeParents( 0, -1, leafParents, nodeParents );

	CUtlVector< CUtlVector<dplane_t> > leafPlanes;
	leafPlanes.SetCount( numleafs );
	for ( int leaf = 0; leaf < numleafs; ++leaf )
		GetLeafBoundaryPlanes( leafPlanes[leaf], leaf, leafParents, nodeParents );

	CUtlVector<AmbientCandidate> candidates;
	if ( !BuildLeafCandidates( options, scene, device, leafPlanes, candidates ) )
		return false;

	CUtlVector<ReSTIRGpuAmbientQuery> queries;
	CUtlVector<int> candidateToQuery;
	candidateToQuery.SetCount( candidates.Count() );
	for ( int i = 0; i < candidates.Count(); ++i )
	{
		candidateToQuery[i] = -1;
		if ( !candidates[i].accepted )
			continue;
		ReSTIRGpuAmbientQuery query;
		query.position[0] = candidates[i].position.x;
		query.position[1] = candidates[i].position.y;
		query.position[2] = candidates[i].position.z;
		query.position[3] = 0.0f;
		candidateToQuery[i] = queries.AddToTail( query );
	}

	CUtlVector<ReSTIRGpuAmbientResult> results;
	int numStyles = scene.sceneStyles.Count();
	if ( queries.Count() > 0 && ( !device.GatherAmbient( queries, results ) || results.Count() != queries.Count() * numStyles ) )
	{
		Warning( "VRAD ReSTIR: ambient lightmap gather failed.\n" );
		return false;
	}

	CUtlVector<AmbientCube> cubes;
	cubes.SetCount( queries.Count() );
	for ( int q = 0; q < queries.Count(); ++q )
	{
		const ReSTIRGpuAmbientResult &result = results[q * numStyles + 0];
		for ( int side = 0; side < 6; ++side )
		{
			cubes[q].side[side].x = result.box[side][0];
			cubes[q].side[side].y = result.box[side][1];
			cubes[q].side[side].z = result.box[side][2];
		}
	}

	// Port of utils/vrad/leaf_ambient_lighting.cpp:92-136. Visibility is
	// GPU-traced in one batch; only flagged surface lights are added here.
	CUtlVector<ReSTIRGpuRay> directRays;
	CUtlVector<AmbientDirectRay> directOwners;
	for ( int candidateIndex = 0; candidateIndex < candidates.Count(); ++candidateIndex )
	{
		int query = candidateToQuery[candidateIndex];
		if ( query < 0 )
			continue;
		for ( int lightIndex = 0; lightIndex < *pNumworldlights; ++lightIndex )
		{
			const dworldlight_t &light = dworldlights[lightIndex];
			if ( !( light.flags & DWL_FLAGS_INAMBIENTCUBE ) || light.type != emit_surface )
				continue;
			Vector delta = light.origin - candidates[candidateIndex].position;
			if ( delta.LengthSqr() <= 1e-20f )
				continue;
			ReSTIRGpuRay ray;
			ray.origin[0] = candidates[candidateIndex].position.x;
			ray.origin[1] = candidates[candidateIndex].position.y;
			ray.origin[2] = candidates[candidateIndex].position.z;
			ray.origin[3] = 0.0f;
			ray.direction[0] = delta.x;
			ray.direction[1] = delta.y;
			ray.direction[2] = delta.z;
			ray.direction[3] = 1.0f;
			directRays.AddToTail( ray );
			AmbientDirectRay owner;
			owner.query = query;
			owner.light = lightIndex;
			directOwners.AddToTail( owner );
		}
	}
	if ( directRays.Count() > 0 )
	{
		CUtlVector<ReSTIRGpuHit> directHits;
		// leaf_ambient_lighting.cpp:111 uses TestLine (g_RtEnv occluders: brushes, bmodel casters, props).
		if ( !device.TraceRays( directRays, RESTIR_RAY_MASK_SHADOW, directHits ) || directHits.Count() != directRays.Count() )
		{
			Warning( "VRAD ReSTIR: ambient direct-light visibility tracing failed.\n" );
			return false;
		}
		for ( int i = 0; i < directHits.Count(); ++i )
		{
			if ( directHits[i].t >= 0.0f && directHits[i].t < 1.0f )
				continue;
			const AmbientDirectRay &owner = directOwners[i];
			const dworldlight_t &light = dworldlights[owner.light];
			Vector queryPosition( queries[owner.query].position[0], queries[owner.query].position[1], queries[owner.query].position[2] );
			Vector delta = light.origin - queryPosition;
			Vector deltaNorm = delta;
			VectorNormalize( deltaNorm );
			float ratio = AmbientWorldLightDistanceFalloff( &light, delta ) * AmbientWorldLightAngle( &light, light.normal, deltaNorm, deltaNorm );
			if ( ratio == 0.0f )
				continue;
			for ( int side = 0; side < 6; ++side )
			{
				float t = DotProduct( s_BoxDirections[side], deltaNorm );
				if ( t > 0.0f )
					cubes[owner.query].side[side] += light.intensity * ( t * ratio );
			}
		}
	}

	bool hasMaterialEmitters = false;
	for ( int light = 0; light < scene.lights.Count(); ++light )
	{
		if ( scene.lights[light].lightFlags & RESTIR_LIGHT_MATERIAL )
		{
			hasMaterialEmitters = true;
			break;
		}
	}
	if ( hasMaterialEmitters && queries.Count() > 0 )
	{
		// The engine lights models from LUMP_WORLDLIGHTS only; material emitters
		// exist only in the bake. Fold their direct light into the six cube sides,
		// like VRAD's DWL_FLAGS_INAMBIENTCUBE surface lights
		// (utils/vrad/leaf_ambient_lighting.cpp:177-198). LightPoints returns VRAD light
		// units; cube sides use the exported 1/255 scale (dworldlight_t::intensity, RayAmbientColor).
		CUtlVector<ReSTIRGpuPointQuery> emitterQueries;
		emitterQueries.SetCount( queries.Count() * 6 );
		for ( int q = 0; q < queries.Count(); ++q )
		{
			for ( int side = 0; side < 6; ++side )
			{
				ReSTIRGpuPointQuery &query = emitterQueries[q * 6 + side];
				memset( &query, 0, sizeof( query ) );
				for ( int axis = 0; axis < 3; ++axis )
				{
					query.position[axis] = queries[q].position[axis];
					query.normal[axis] = s_BoxDirections[side][axis];
				}
				query.flags = RESTIR_POINT_EMITTERS_ONLY;
			}
		}
		CUtlVector<ReSTIRGpuPointResult> emitterResults;
		if ( !device.LightPoints( emitterQueries, emitterResults ) || emitterResults.Count() != emitterQueries.Count() * numStyles )
		{
			Warning( "VRAD ReSTIR: ambient material-emitter direct lighting failed.\n" );
			return false;
		}
		for ( int q = 0; q < queries.Count(); ++q )
		{
			for ( int side = 0; side < 6; ++side )
			{
				const ReSTIRGpuPointResult &result = emitterResults[( q * 6 + side ) * numStyles + 0];
				cubes[q].side[side] += Vector( result.direct[0], result.direct[1], result.direct[2] ) * ( 1.0f / 255.0f );
			}
		}
	}

	CUtlVector< CUtlVector<AmbientSample> > leafSamples;
	leafSamples.SetCount( numleafs );
	for ( int candidateIndex = 0; candidateIndex < candidates.Count(); ++candidateIndex )
	{
		int query = candidateToQuery[candidateIndex];
		if ( query < 0 )
			continue;
		AmbientCube &cube = cubes[query];
		AddSampleToList( leafSamples[candidates[candidateIndex].leaf], candidates[candidateIndex].position, cube );
	}
	for ( int leaf = 0; leaf < numleafs; ++leaf )
		CompressAmbientSampleList( leafSamples[leaf] );

	// Port of utils/vrad/leaf_ambient_lighting.cpp:659-691. Only the ReSTIR
	// ambient lumps are written; dleaf_t legacy ambient fields are untouched.
	g_pLeafAmbientIndex->RemoveAll();
	g_pLeafAmbientLighting->RemoveAll();
	g_pLeafAmbientIndex->SetCount( numleafs );
	g_pLeafAmbientLighting->EnsureCapacity( numleafs * 4 );
	for ( int leaf = 0; leaf < numleafs; ++leaf )
	{
		const CUtlVector<AmbientSample> &list = leafSamples[leaf];
		g_pLeafAmbientIndex->Element( leaf ).ambientSampleCount = list.Count();
		if ( !list.Count() )
		{
			g_pLeafAmbientIndex->Element( leaf ).firstAmbientSample = 0;
			continue;
		}
		g_pLeafAmbientIndex->Element( leaf ).firstAmbientSample = g_pLeafAmbientLighting->Count();
		for ( int sample = 0; sample < list.Count(); ++sample )
		{
			int outIndex = g_pLeafAmbientLighting->AddToTail();
			dleafambientlighting_t &light = g_pLeafAmbientLighting->Element( outIndex );
			light.x = Fixed8Fraction( list[sample].pos.x, dleafs[leaf].mins[0], dleafs[leaf].maxs[0] );
			light.y = Fixed8Fraction( list[sample].pos.y, dleafs[leaf].mins[1], dleafs[leaf].maxs[1] );
			light.z = Fixed8Fraction( list[sample].pos.z, dleafs[leaf].mins[2], dleafs[leaf].maxs[2] );
			light.pad = 0;
			for ( int side = 0; side < 6; ++side )
				VectorToColorRGBExp32( list[sample].cube[side], light.cube.m_Color[side] );
		}
	}

	// Port of utils/vrad/leaf_ambient_lighting.cpp:693-708.
	for ( int leaf = 0; leaf < numleafs; ++leaf )
	{
		if ( g_pLeafAmbientIndex->Element( leaf ).ambientSampleCount != 0 )
			continue;
		if ( !( dleafs[leaf].contents & CONTENTS_SOLID ) )
			Msg( "Bad leaf ambient for leaf %d\n", leaf );
		int refLeaf = NearestNeighborWithLight( leaf );
		g_pLeafAmbientIndex->Element( leaf ).ambientSampleCount = 0;
		g_pLeafAmbientIndex->Element( leaf ).firstAmbientSample = refLeaf;
	}
	return true;
}
