//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef RESTIR_LIGHTS_GLSL
#define RESTIR_LIGHTS_GLSL
#include "restir_lightmap.glsl"

float TriangleArea( ReSTIRGpuEmitterTriangle triangle )
{
	return 0.5 * length( cross( triangle.v1.xyz - triangle.v0.xyz, triangle.v2.xyz - triangle.v0.xyz ) );
}

// Reservoir reuse stores a world-space point, not UVs. Recover its barycentrics
// on the original emitter triangle so texture emission is unchanged at a new receiver.
vec2 EmitterBarycentrics( ReSTIRGpuEmitterTriangle triangle, vec3 source )
{
	vec3 edge1 = triangle.v1.xyz - triangle.v0.xyz;
	vec3 edge2 = triangle.v2.xyz - triangle.v0.xyz;
	vec3 offset = source - triangle.v0.xyz;
	vec3 areaNormal = cross( edge1, edge2 );
	return vec2( dot( cross( offset, edge2 ), areaNormal ), dot( cross( edge1, offset ), areaNormal ) ) / dot( areaNormal, areaNormal );
}

// utils/vrad/lightmap.cpp:1909-1929: one-sided surface emission, inverse-square
// falloff. Texture emission and the winding normal belong to this triangle,
// including curved displacements and prop meshes, not the light's mean normal.
vec3 SurfaceLightRadiance( ReSTIRGpuLight light, ReSTIRGpuEmitterTriangle triangle, vec3 origin, vec3 source, vec2 barycentrics )
{
	vec3 offset = source - origin;
	float distanceSquared = dot( offset, offset );
	if ( distanceSquared <= 0.0 )
		return vec3( 0.0 );
	vec3 normal = normalize( cross( triangle.v1.xyz - triangle.v0.xyz, triangle.v2.xyz - triangle.v0.xyz ) );
	float falloff = max( -dot( offset * inversesqrt( distanceSquared ), normal ), 0.0 ) / distanceSquared;
	if ( falloff <= 0.0 )
		return vec3( 0.0 );
	vec3 emission = light.intensity.rgb;
	if ( light.emissionTexture >= 0 )
	{
		vec2 uv0 = vec2( triangle.v0.w, triangle.v1.w );
		vec2 uv1 = vec2( triangle.v2.w, triangle.uv.x );
		vec2 uv = uv0 * ( 1.0 - barycentrics.x - barycentrics.y ) + uv1 * barycentrics.x + triangle.uv.yz * barycentrics.y;
		ivec2 size = textureSize( sceneTextures[nonuniformEXT( light.emissionTexture )], 0 );
		ivec2 texel = ivec2( floor( fract( uv ) * vec2( size ) ) );
		emission *= pow( texelFetch( sceneTextures[nonuniformEXT( light.emissionTexture )], texel, 0 ).rgb, vec3( 2.2 ) );
	}
	return emission * falloff;
}

// utils/vrad/lightmap.cpp:1835-1977 GatherSampleStandardLightSSE. The fade is
// quintic (not GLSL smoothstep); capped falloff uses max(distance,1). Surface
// lights use SurfaceLightRadiance: their radius cutoff and fade fields are zero
// (restir_types.h emit_surface contract), and their bounding radius is not attenuation.
vec3 StandardLightRadiance( ReSTIRGpuLight light, vec3 origin, vec3 source )
{
	vec3 offset = source - origin;
	float distanceSquared = dot( offset, offset );
	if ( distanceSquared <= 0.0 )
		return vec3( 0.0 );
	float distanceToLight = sqrt( distanceSquared );
	if ( light.origin.w > 0.0 && distanceToLight > light.origin.w )
		return vec3( 0.0 );
	bool hardFade = light.fade.y > light.fade.x;
	if ( hardFade && distanceToLight > light.fade.y )
		return vec3( 0.0 );
	vec3 direction = offset / distanceToLight;
	float distanceClamped = max( distanceToLight, 1.0 );
	float evaluationDistance = min( distanceClamped, light.fade.z );
	float denominator = light.attenuation.x + evaluationDistance * light.attenuation.y + evaluationDistance * evaluationDistance * light.attenuation.z;
	float falloff = denominator > 0.0 ? 1.0 / denominator : 0.0;
	if ( light.type == EMIT_SPOTLIGHT )
	{
		float coneDot = -dot( direction, light.normal.xyz );
		if ( coneDot <= light.fade.w )
			return vec3( 0.0 );
		float cone = 1.0;
		if ( coneDot <= light.normal.w )
		{
			cone = clamp( ( coneDot - light.fade.w ) / ( light.normal.w - light.fade.w ), 0.0, 1.0 );
			if ( light.attenuation.w != 0.0 && light.attenuation.w != 1.0 )
				cone = pow( cone, light.attenuation.w );
		}
		falloff *= coneDot * cone;
	}
	if ( hardFade )
	{
		float t = 1.0 - clamp( ( distanceClamped - light.fade.x ) / ( light.fade.y - light.fade.x ), 0.0, 1.0 );
		falloff *= t * t * t * ( t * ( t * 6.0 - 15.0 ) + 10.0 );
	}
	return light.intensity.rgb * falloff;
}

// utils/vrad/lightmap.cpp:1673-1831. Sun visibility requires the FIRST shadow
// hit to be SKY. Ambient divides visible cosine sum by all hemisphere cosine
// sum, not by the number of rays: the continuum kernel is intensity / pi.
// Sampling this kernel with a cosine PDF gives intensity for unobstructed sky.
vec3 EvaluateLight( uint lightIndex, vec3 origin, vec3 source, uint emitterTri )
{
	ReSTIRGpuLight light = lights[lightIndex];
	if ( light.type == EMIT_SURFACE )
	{
		ReSTIRGpuEmitterTriangle triangle = emitterTriangles[emitterTri];
		vec2 barycentrics = light.emissionTexture >= 0 ? EmitterBarycentrics( triangle, source ) : vec2( 0.0 );
		return SurfaceLightRadiance( light, triangle, origin, source, barycentrics );
	}
	if ( light.type == EMIT_SKYLIGHT )
		return light.intensity.rgb;
	if ( light.type == EMIT_SKYAMBIENT )
		return light.intensity.rgb / RESTIR_PI;
	return StandardLightRadiance( light, origin, source );
}

bool LightVisibility( uint lightIndex, vec3 origin, vec3 source, uint emitterTri, uint skipHitId )
{
	ReSTIRGpuLight light = lights[lightIndex];
	vec3 offset = source - origin;
	float distanceToLight = length( offset );
	if ( distanceToLight <= RESTIR_DIST_EPSILON )
		return false;
	vec3 direction = offset / distanceToLight;
	if ( light.type == EMIT_SKYLIGHT || light.type == EMIT_SKYAMBIENT )
	{
		HitInfo hit;
		return TraceRay( origin, direction, RESTIR_DIST_EPSILON, RESTIR_TRACE_LENGTH, RESTIR_RAY_MASK_SHADOW, skipHitId, hit ) && ( hit.flags & RESTIR_TRI_SKY ) != 0u;
	}
	// utils/vrad/lightmap.cpp:1909-1912 moves the source off the emitter.
	if ( light.type == EMIT_SURFACE )
	{
		ReSTIRGpuEmitterTriangle triangle = emitterTriangles[emitterTri];
		vec3 normal = normalize( cross( triangle.v1.xyz - triangle.v0.xyz, triangle.v2.xyz - triangle.v0.xyz ) );
		offset += normal * RESTIR_DIST_EPSILON;
		distanceToLight = length( offset );
		direction = offset / distanceToLight;
	}
	return TraceVisibility( origin, direction, RESTIR_DIST_EPSILON, max( RESTIR_DIST_EPSILON, distanceToLight - RESTIR_DIST_EPSILON ), RESTIR_RAY_MASK_SHADOW, skipHitId );
}

bool SampleLight( uint lightIndex, vec3 origin, vec3 normal, vec3 uniformValue, out ReSTIRReservoir candidate )
{
	candidate = EmptyReservoir();
	ReSTIRGpuLight light = lights[lightIndex];
	candidate.light = lightIndex;
	candidate.flags = RESTIR_RES_VALID;
	candidate.sourcePdf = 1.0;
	vec3 source = light.origin.xyz;
	vec2 emitterBarycentrics = vec2( 0.0 );
	if ( light.type == EMIT_SURFACE )
	{
		if ( light.numTris <= 0 )
			return false;
		// Inclusive, power-weighted CDF in emitterTriangles: first cdf > u.x.
		// Binary search replaces the per-sample scan over all emitter geometry.
		int first = light.firstTri;
		int end = first + light.numTris;
		int low = first;
		int high = end;
		while ( low < high )
		{
			int middle = low + ( high - low ) / 2;
			if ( emitterTriangles[middle].uv.w > uniformValue.x )
				high = middle;
			else
				low = middle + 1;
		}
		int selected = min( low, end - 1 );
		ReSTIRGpuEmitterTriangle triangle = emitterTriangles[selected];
		float area = TriangleArea( triangle );
		float previousCdf = selected > first ? emitterTriangles[selected - 1].uv.w : 0.0;
		float probability = triangle.uv.w - previousCdf;
		if ( area <= 0.0 || probability <= 0.0 )
			return false;
		float root = sqrt( uniformValue.y );
		emitterBarycentrics = vec2( root * ( 1.0 - uniformValue.z ), root * uniformValue.z );
		source = triangle.v0.xyz * ( 1.0 - root ) + triangle.v1.xyz * emitterBarycentrics.x + triangle.v2.xyz * emitterBarycentrics.y;
		candidate.emitterTri = uint( selected );
		candidate.sourcePdf = probability / area;
		candidate.radiance.rgb = SurfaceLightRadiance( light, triangle, origin, source, emitterBarycentrics );
	}
	else if ( light.type == EMIT_SKYAMBIENT )
	{
		vec3 direction = CosineDirection( normal, uniformValue.xy );
		source = origin + direction * 100000.0;
		candidate.sourcePdf = max( dot( normal, direction ), 0.0 ) / RESTIR_PI;
	}
	else if ( light.type == EMIT_SKYLIGHT )
	{
		// VRAD stores the sun's TRAVEL direction (lightmap.cpp:1686 dot = -(n . normal), :1711 ray = -normal).
		vec3 direction = SafeNormal( -light.normal.xyz );
		float coneCos = cos( radians( light.sunSpreadAngle ) );
		if ( coneCos < 1.0 )
		{
			float z = mix( 1.0, coneCos, uniformValue.x );
			float radius = sqrt( max( 0.0, 1.0 - z * z ) );
			float angle = 2.0 * RESTIR_PI * uniformValue.y;
			direction = LocalDirection( direction, vec3( radius * cos( angle ), radius * sin( angle ), z ) );
			// Sun intensity is total cone irradiance, not radiance per steradian.
			candidate.sourcePdf = 1.0 / ( 2.0 * RESTIR_PI * ( 1.0 - coneCos ) );
		}
		source = origin + direction * 100000.0;
	}
	candidate.samplePos = vec4( source, 0.0 );
	if ( light.type != EMIT_SURFACE )
		candidate.radiance.rgb = EvaluateLight( lightIndex, origin, source, candidate.emitterTri );
	if ( light.type == EMIT_SKYLIGHT )
		candidate.radiance.rgb *= candidate.sourcePdf;
	vec3 direction = SafeNormal( source - origin );
	candidate.radiance.w = Luminance( candidate.radiance.rgb ) * max( dot( normal, direction ), 0.0 );
	return true;
}

ReSTIRReservoir Reevaluate( ReSTIRReservoir source, vec3 origin, vec3 normal )
{
	if ( ( source.flags & RESTIR_RES_VALID ) == 0u )
		return source;
	if ( ( source.flags & RESTIR_RES_PATH ) == 0u )
	{
		float angularScale = 1.0;
		if ( lights[source.light].type == EMIT_SKYLIGHT && lights[source.light].sunSpreadAngle > 0.0 )
			angularScale = 1.0 / ( 2.0 * RESTIR_PI * ( 1.0 - cos( radians( lights[source.light].sunSpreadAngle ) ) ) );
		source.radiance.rgb = EvaluateLight( source.light, origin, source.samplePos.xyz, source.emitterTri ) * angularScale;
	}
	source.radiance.w = Luminance( source.radiance.rgb ) * max( dot( normal, SafeNormal( source.samplePos.xyz - origin ) ), 0.0 );
	source.flags &= ~RESTIR_RES_VISIBLE;
	return source;
}

bool ReservoirVisibility( ReSTIRReservoir reservoir, vec3 origin )
{
	if ( ( reservoir.flags & RESTIR_RES_PATH ) == 0u )
		return LightVisibility( reservoir.light, origin, reservoir.samplePos.xyz, reservoir.emitterTri, RESTIR_NO_HIT );
	vec3 offset = reservoir.samplePos.xyz - origin;
	float distanceToVertex = length( offset );
	return TraceVisibility( origin, SafeNormal( offset ), RESTIR_DIST_EPSILON, max( RESTIR_DIST_EPSILON, distanceToVertex - RESTIR_DIST_EPSILON ), RESTIR_RAY_MASK_SHADOW, RESTIR_NO_HIT );
}

// Irradiance accumulated so far at the sample nearest to a lightmap hit (style 0, base channel).
// Zero in iteration 0; afterwards the running mean of every previous iteration, i.e. direct plus
// the bounces already resolved — the progressive equivalent of VRAD's patch radiosity
// (patch exitance = reflectivity * (direct + bounced), vrad.cpp BounceLight / radial.cpp:718-729).
// Requires restir_lightmap.glsl (SceneFaceFromHit, LightmapCoord).
vec3 PreviousIrradianceAtHit( HitInfo hit, vec3 position, out vec3 reflectivity )
{
	reflectivity = vec3( 0.0 );
	int faceIndex = SceneFaceFromHit( hit );
	if ( faceIndex < 0 )
		return vec3( 0.0 );
	ReSTIRGpuFace face = faces[faceIndex];
	reflectivity = HitAlbedo( face, hit, position );
	ivec2 coord = clamp( ivec2( LightmapCoord( face, hit, position ) ), ivec2( 0 ), ivec2( face.luxelW - 1, face.luxelH - 1 ) );
	int sampleIndex = cellSamples[face.firstLuxel + coord.x + coord.y * face.luxelW];
	if ( sampleIndex < 0 )
	{
		// Displacement cells and clipped brush cells without a sample: nearest sample of the face by distance.
		float best = 1.0e30;
		for ( int k = 0; k < face.numSamples; ++k )
		{
			vec3 delta = samples[face.firstSample + k].position.xyz - position;
			float d2 = dot( delta, delta );
			if ( d2 < best )
			{
				best = d2;
				sampleIndex = face.firstSample + k;
			}
		}
		if ( sampleIndex < 0 )
			return vec3( 0.0 );
	}
	vec4 value = accumulation[ReservoirIndex( face, uint( sampleIndex ), 0u ) * RESTIR_MAX_CHANNELS];
	return value.w > 0.0 ? value.rgb / value.w : vec3( 0.0 );
}

// Indirect candidate (slot 0 only, I4): one cosine-distributed direction; the bounce surface is
// the lightmapped face seen in that direction (TraceGatherRay: WORLDFACE, front faces), occluded
// by the shadow geometry like every other ray. Its outgoing radiance is reflectivity * E / pi
// with E the irradiance already resolved there (multi-bounce emerges over iterations, bounded by
// pc.maxBounces iterations of feedback). Static props and sky do not bounce (VRAD radiosity has
// no prop patches).
ReSTIRReservoir GeneratePathCandidate( vec3 origin, vec3 normal, inout uint rng )
{
	ReSTIRReservoir candidate = EmptyReservoir();
	vec3 direction = CosineDirection( normal, vec2( Random( rng ), Random( rng ) ) );
	float cosine = max( dot( normal, direction ), 0.0 );
	candidate.sourcePdf = cosine / RESTIR_PI;
	if ( cosine <= 0.0 || pc.iteration == 0u )
		return candidate;
	HitInfo occluder;
	float occluderT = RESTIR_TRACE_LENGTH;
	if ( TraceRay( origin, direction, RESTIR_DIST_EPSILON, RESTIR_TRACE_LENGTH, RESTIR_RAY_MASK_SHADOW, RESTIR_NO_HIT, occluder ) )
	{
		if ( ( occluder.flags & RESTIR_TRI_SKY ) != 0u )
			return candidate;
		occluderT = occluder.t;
	}
	HitInfo surface;
	if ( !TraceGatherRay( origin, direction, RESTIR_DIST_EPSILON, occluderT + 1.0, surface ) )
		return candidate;
	vec3 vertex = origin + direction * surface.t;
	vec3 reflectivity;
	vec3 irradiance = PreviousIrradianceAtHit( surface, vertex, reflectivity );
	candidate.samplePos = vec4( vertex, 0.0 );
	candidate.hitClass = surface.hitId;
	candidate.flags = RESTIR_RES_VALID | RESTIR_RES_PATH | RESTIR_RES_VISIBLE;
	vec3 radiance = reflectivity * irradiance / RESTIR_PI;
	candidate.radiance = vec4( radiance, Luminance( radiance ) * cosine );
	return candidate;
}
#endif
