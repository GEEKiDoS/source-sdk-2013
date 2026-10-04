//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef RESTIR_TRACE_GLSL
#define RESTIR_TRACE_GLSL

struct HitInfo
{
	float t;
	int triangle;
	int face;
	uint hitId;
	uint flags;
	vec3 normal;
	vec2 barycentrics;
};

// utils/vrad/trace.cpp:127-147 ComputeCoverageFromTexture: interpolated UV,
// repeating R8 coverage; restir_types.h:115-116 fixes the binary threshold.
bool TriangleAccepts( uint triangleIndex, vec2 barycentrics, vec3 direction, uint mask, uint skipHitId, bool frontFaceOnly )
{
	ReSTIRGpuTriangle triangle = triangles[triangleIndex];
	if ( ( triangle.flags & mask ) == 0u || triangle.hitId == skipHitId )
		return false;
	if ( frontFaceOnly && dot( direction, cross( triangle.v1.xyz - triangle.v0.xyz, triangle.v2.xyz - triangle.v0.xyz ) ) > 0.0 )
		return false;
	if ( ( triangle.flags & RESTIR_TRI_NONOPAQUE ) != 0u )
	{
		int textureIndex = materials[triangle.material].coverageTexture;
		if ( textureIndex >= 0 )
		{
			vec2 uv0 = vec2( triangle.v0.w, triangle.v1.w );
			vec2 uv1 = vec2( triangle.v2.w, triangle.uv.x );
			vec2 uv2 = triangle.uv.yz;
			vec2 uv = uv0 * ( 1.0 - barycentrics.x - barycentrics.y ) + uv1 * barycentrics.x + uv2 * barycentrics.y;
			ivec2 size = textureSize( sceneTextures[nonuniformEXT( textureIndex )], 0 );
			ivec2 texel = ivec2( floor( fract( uv ) * vec2( size ) ) );
			if ( texelFetch( sceneTextures[nonuniformEXT( textureIndex )], texel, 0 ).r < 0.5 )
				return false;
		}
	}
	return true;
}

HitInfo MakeHit( uint triangleIndex, float t, vec2 barycentrics )
{
	ReSTIRGpuTriangle triangle = triangles[triangleIndex];
	return HitInfo( t, int( triangleIndex ), triangle.face, triangle.hitId, triangle.flags,
		SafeNormal( cross( triangle.v1.xyz - triangle.v0.xyz, triangle.v2.xyz - triangle.v0.xyz ) ), barycentrics );
}

#if RESTIR_HW_RAYQUERY
bool TraceRayInternal( vec3 origin, vec3 direction, float tMin, float tMax, uint mask, uint skipHitId, bool anyHit, bool frontFaceOnly, out HitInfo hit )
{
	hit = HitInfo( -1.0, -1, -1, 0u, 0u, vec3( 0.0 ), vec2( 0.0 ) );
	if ( pc.numTriangles == 0u || tMax <= tMin )
		return false;
	// Force candidate processing when hit-class exclusion or gather backface
	// culling is required. Otherwise BLAS opaque geometry confirms automatically.
	uint flags = 0u;
	bool canonicalMask = mask == RESTIR_RAY_MASK_SHADOW || mask == RESTIR_RAY_MASK_WORLDFACE || mask == RESTIR_RAY_MASK_ALL;
	if ( skipHitId != RESTIR_NO_HIT || frontFaceOnly || !canonicalMask )
		flags |= gl_RayFlagsNoOpaqueEXT;
	if ( anyHit )
		flags |= gl_RayFlagsTerminateOnFirstHitEXT;
	rayQueryEXT query;
	// The host has separate SHADOW/WORLDFACE instances. Arbitrary service
	// masks must traverse both classes and filter their actual triangle flags.
	uint instanceMask = canonicalMask ? mask & 0xFFu : RESTIR_RAY_MASK_SHADOW | RESTIR_RAY_MASK_WORLDFACE;
	rayQueryInitializeEXT( query, tlas, flags, instanceMask, origin, tMin, direction, tMax );
	while ( rayQueryProceedEXT( query ) )
	{
		if ( rayQueryGetIntersectionTypeEXT( query, false ) == gl_RayQueryCandidateIntersectionTriangleEXT )
		{
			uint mapIndex = rayQueryGetIntersectionInstanceCustomIndexEXT( query, false ) + rayQueryGetIntersectionPrimitiveIndexEXT( query, false );
			uint triangleIndex = hwPrimMap[mapIndex];
			vec2 barycentrics = rayQueryGetIntersectionBarycentricsEXT( query, false );
			if ( TriangleAccepts( triangleIndex, barycentrics, direction, mask, skipHitId, frontFaceOnly ) )
				rayQueryConfirmIntersectionEXT( query );
		}
	}
	if ( rayQueryGetIntersectionTypeEXT( query, true ) == gl_RayQueryCommittedIntersectionNoneEXT )
		return false;
	uint mapIndex = rayQueryGetIntersectionInstanceCustomIndexEXT( query, true ) + rayQueryGetIntersectionPrimitiveIndexEXT( query, true );
	hit = MakeHit( hwPrimMap[mapIndex], rayQueryGetIntersectionTEXT( query, true ), rayQueryGetIntersectionBarycentricsEXT( query, true ) );
	return true;
}
#else
bool IntersectBounds( vec3 origin, vec3 direction, vec3 boundsMin, vec3 boundsMax, float tMin, float tMax, out float nearT )
{
	nearT = tMin;
	float farT = tMax;
	for ( int axis = 0; axis < 3; ++axis )
	{
		if ( direction[axis] == 0.0 )
		{
			if ( origin[axis] < boundsMin[axis] || origin[axis] > boundsMax[axis] )
				return false;
		}
		else
		{
			float first = ( boundsMin[axis] - origin[axis] ) / direction[axis];
			float second = ( boundsMax[axis] - origin[axis] ) / direction[axis];
			nearT = max( nearT, min( first, second ) );
			farT = min( farT, max( first, second ) );
			if ( nearT > farT )
				return false;
		}
	}
	return true;
}

bool IntersectTriangle( uint index, vec3 origin, vec3 direction, float tMin, float tMax, out float t, out vec2 barycentrics )
{
	ReSTIRGpuTriangle triangle = triangles[index];
	vec3 edge1 = triangle.v1.xyz - triangle.v0.xyz;
	vec3 edge2 = triangle.v2.xyz - triangle.v0.xyz;
	vec3 p = cross( direction, edge2 );
	float determinant = dot( edge1, p );
	if ( abs( determinant ) < 1.0e-12 )
		return false;
	vec3 offset = origin - triangle.v0.xyz;
	float u = dot( offset, p ) / determinant;
	vec3 q = cross( offset, edge1 );
	float v = dot( direction, q ) / determinant;
	t = dot( edge2, q ) / determinant;
	barycentrics = vec2( u, v );
	return u >= 0.0 && v >= 0.0 && u + v <= 1.0 && t >= tMin && t <= tMax;
}

bool TraceRayInternal( vec3 origin, vec3 direction, float tMin, float tMax, uint mask, uint skipHitId, bool anyHit, bool frontFaceOnly, out HitInfo hit )
{
	hit = HitInfo( -1.0, -1, -1, 0u, 0u, vec3( 0.0 ), vec2( 0.0 ) );
	if ( pc.numTriangles == 0u || tMax <= tMin )
		return false;
	// Unique 64-bit Morton/index keys bound Patricia-tree depth to 64. One
	// deferred sibling per level fits this fixed stack without dropping nodes.
	uint stack[64];
	uint stackSize = 0u;
	uint nodeIndex = 0u;
	float closest = tMax;
	while ( true )
	{
		ReSTIRBvhNode node = bvhNodes[nodeIndex];
		float nearT;
		if ( IntersectBounds( origin, direction, node.boundsMin.xyz, node.boundsMax.xyz, tMin, closest, nearT ) )
		{
			if ( node.primitive != RESTIR_NO_HIT )
			{
				uint triangleIndex = bvhPrims[node.primitive];
				float t;
				vec2 barycentrics;
				if ( IntersectTriangle( triangleIndex, origin, direction, tMin, closest, t, barycentrics ) &&
					TriangleAccepts( triangleIndex, barycentrics, direction, mask, skipHitId, frontFaceOnly ) )
				{
					if ( hit.triangle < 0 || t < closest || ( t == closest && triangleIndex < uint( hit.triangle ) ) )
					{
						closest = t;
						hit = MakeHit( triangleIndex, t, barycentrics );
					}
					if ( anyHit )
						return true;
				}
			}
			else
			{
				uint left = floatBitsToUint( node.boundsMin.w );
				uint right = floatBitsToUint( node.boundsMax.w );
				stack[stackSize++] = right;
				nodeIndex = left;
				continue;
			}
		}
		if ( stackSize == 0u )
			break;
		nodeIndex = stack[--stackSize];
	}
	return hit.triangle >= 0;
}
#endif

bool TraceRay( vec3 origin, vec3 direction, float tMin, float tMax, uint mask, uint skipHitId, out HitInfo hit )
{
	return TraceRayInternal( origin, direction, tMin, tMax, mask, skipHitId, false, false, hit );
}

// utils/vrad/vraddetailprops.cpp:437 CLightSurface rejects back-facing hits;
// generic TraceRay deliberately does not impose this gather-only policy.
bool TraceGatherRay( vec3 origin, vec3 direction, float tMin, float tMax, out HitInfo hit )
{
	return TraceRayInternal( origin, direction, tMin, tMax, RESTIR_RAY_MASK_WORLDFACE, RESTIR_NO_HIT, false, true, hit );
}

bool TraceVisibility( vec3 origin, vec3 direction, float tMin, float tMax, uint mask, uint skipHitId )
{
	HitInfo hit;
	return !TraceRayInternal( origin, direction, tMin, tMax, mask, skipHitId, true, false, hit );
}
#endif
