//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef RESTIR_COMMON_GLSL
#define RESTIR_COMMON_GLSL

const float RESTIR_PI = 3.14159265358979323846;
const float RESTIR_EQUAL_EPSILON = 0.001;
const float RESTIR_DIST_EPSILON = 0.03125;
const float RESTIR_WEIGHT_EPS = 0.00001;
// utils/vrad/radial.h:22-25
const float RESTIR_RADIALDIST = 1.42;
const float RESTIR_RADIALDIST2 = 2.0;
const float RESTIR_TRACE_LENGTH = 32768.0 * 1.732050807569;
const float RESTIR_AMBIENT_LENGTH = 32768.0 * 1.74;
const uint RESTIR_NO_HIT = 0xFFFFFFFFu;

// Deterministic PCG hash + digitally scrambled base-2 radical inverse. The key
// mixes seed, face, sample index WITHIN the face, style slot and iteration in
// that order. Each dimension has an independent digital scramble and uses a
// scrambled radical inverse of candidate+1. Reuse draws use separate streams.
// No key contains a dispatch/global invocation index, so batching is invariant.
uint Hash( uint value )
{
	uint state = value * 747796405u + 2891336453u;
	uint word = ( ( state >> ( ( state >> 28u ) + 4u ) ) ^ state ) * 277803737u;
	return ( word >> 22u ) ^ word;
}

uint SampleSeed( uint face, uint sampleInFace, uint slot, uint stream )
{
	uint key = Hash( pc.seed ^ 0x9E3779B9u );
	key = Hash( key ^ face );
	key = Hash( key ^ sampleInFace );
	key = Hash( key ^ slot );
	key = Hash( key ^ pc.iteration );
	return Hash( key ^ stream );
}

float Random( inout uint state )
{
	state = Hash( state );
	return float( state >> 8u ) * ( 1.0 / 16777216.0 );
}

float CandidateDimension( uint key, uint candidate, uint dimension )
{
	uint scramble = Hash( key ^ Hash( dimension + 0x68BC21EBu ) );
	return float( ( bitfieldReverse( candidate + 1u ) ^ scramble ) >> 8u ) / 16777216.0;
}

float Luminance( vec3 value )
{
	return dot( value, vec3( 0.2126, 0.7152, 0.0722 ) );
}

vec3 SafeNormal( vec3 value )
{
	float lengthSquared = dot( value, value );
	return lengthSquared > 0.0 ? value * inversesqrt( lengthSquared ) : vec3( 0.0, 0.0, 1.0 );
}

vec3 FiniteRadiance( vec3 value )
{
	return vec3( isnan( value.x ) || isinf( value.x ) ? 0.0 : max( value.x, 0.0 ),
		isnan( value.y ) || isinf( value.y ) ? 0.0 : max( value.y, 0.0 ),
		isnan( value.z ) || isinf( value.z ) ? 0.0 : max( value.z, 0.0 ) );
}

vec3 LocalDirection( vec3 normal, vec3 local )
{
	vec3 tangent = SafeNormal( cross( abs( normal.z ) < 0.999 ? vec3( 0.0, 0.0, 1.0 ) : vec3( 0.0, 1.0, 0.0 ), normal ) );
	return tangent * local.x + cross( normal, tangent ) * local.y + normal * local.z;
}

vec3 CosineDirection( vec3 normal, vec2 uniformValue )
{
	float radius = sqrt( uniformValue.x );
	float angle = 2.0 * RESTIR_PI * uniformValue.y;
	return LocalDirection( normal, vec3( radius * cos( angle ), radius * sin( angle ), sqrt( 1.0 - uniformValue.x ) ) );
}

// mathlib/halton.cpp:9-29, public/mathlib/halton.h:43-64. NextValue starts at
// element 1; the original GetElement reads the sampler's incremented seed.
float Halton( uint element, uint base )
{
	float value = 0.0;
	float scale = 1.0 / float( base );
	while ( element != 0u )
	{
		value += float( element % base ) * scale;
		element /= base;
		scale /= float( base );
	}
	return value;
}

vec3 DirectionalSample( uint sampleIndex )
{
	// C++ seed++ in NextValue occurs before GetElement reads member seed.
	uint element = sampleIndex + 2u;
	float z = 2.0 * Halton( element, 2u ) - 1.0;
	float angle = 2.0 * RESTIR_PI * Halton( element, 3u );
	float radius = sqrt( max( 0.0, 1.0 - z * z ) );
	return vec3( cos( angle ) * radius, sin( angle ) * radius, z );
}

// utils/vrad/radial.cpp:27-65, restir_types.h:175-177.
vec2 WorldToLuxelSpace( ReSTIRGpuFace face, vec3 position )
{
	vec3 offset = position - face.luxelOrigin.xyz;
	return vec2( dot( offset, face.worldToLuxel0.xyz ), dot( offset, face.worldToLuxel1.xyz ) ) - vec2( face.lmMins );
}

// utils/vrad/radial.cpp:27-65, restir_types.h:175-177 (inverse transform).
vec3 LuxelSpaceToWorld( ReSTIRGpuFace face, vec2 coord )
{
	coord += vec2( face.lmMins );
	return face.luxelOrigin.xyz + coord.x * face.luxelToWorld0.xyz + coord.y * face.luxelToWorld1.xyz;
}

// utils/vrad/lightmap.cpp:2445-2451 pushes every sample (including displacements).
vec3 ShadingOrigin( ReSTIRGpuFace face, ReSTIRGpuSample sampleRecord )
{
	return sampleRecord.position.xyz + face.faceNormal.xyz;
}

void ResolveReservoir( uint reservoirIndex, out uint faceIndex, out uint sampleIndex, out uint slot )
{
	uint begin = 0u;
	uint end = pc.numFaces;
	while ( begin < end )
	{
		uint middle = begin + ( end - begin ) / 2u;
		if ( uint( faces[middle].firstReservoir ) <= reservoirIndex )
			begin = middle + 1u;
		else
			end = middle;
	}
	faceIndex = begin - 1u;
	uint offset = reservoirIndex - uint( faces[faceIndex].firstReservoir );
	slot = offset % uint( faces[faceIndex].numStyles );
	sampleIndex = uint( faces[faceIndex].firstSample ) + offset / uint( faces[faceIndex].numStyles );
}

void ResolveOutput( uint outputIndex, out uint faceIndex, out uint slot, out uint channel, out uint luxel )
{
	uint begin = 0u;
	uint end = pc.numFaces;
	while ( begin < end )
	{
		uint middle = begin + ( end - begin ) / 2u;
		if ( uint( faces[middle].firstOutput ) <= outputIndex )
			begin = middle + 1u;
		else
			end = middle;
	}
	faceIndex = begin - 1u;
	ReSTIRGpuFace face = faces[faceIndex];
	uint count = uint( face.luxelW * face.luxelH );
	uint offset = outputIndex - uint( face.firstOutput );
	luxel = offset % count;
	channel = ( offset / count ) % uint( face.numChannels );
	slot = offset / ( count * uint( face.numChannels ) );
}

uint ReservoirIndex( ReSTIRGpuFace face, uint sampleIndex, uint slot )
{
	return uint( face.firstReservoir ) + ( sampleIndex - uint( face.firstSample ) ) * uint( face.numStyles ) + slot;
}

int FindStyle( ReSTIRGpuFace face, int style )
{
	for ( int slot = 0; slot < face.numStyles; ++slot )
	{
		if ( face.styles[slot] == style )
			return slot;
	}
	return -1;
}

ReSTIRReservoir EmptyReservoir()
{
	return ReSTIRReservoir( vec4( 0.0 ), vec4( 0.0 ), 0.0, 0.0, 0.0, 0.0, RESTIR_NO_HIT, 0u, 0u, 0u );
}

// Bitterli et al. 2020, weighted reservoir sampling; no floating-point atomics.
void ReservoirUpdate( inout ReSTIRReservoir result, ReSTIRReservoir candidate, float weight, float count, inout uint rng )
{
	result.M += count;
	result.wSum += weight;
	if ( weight > 0.0 && Random( rng ) * result.wSum < weight )
	{
		float sum = result.wSum;
		float totalCount = result.M;
		result = candidate;
		result.wSum = sum;
		result.M = totalCount;
	}
}

void ReservoirNormalize( inout ReSTIRReservoir reservoir )
{
	reservoir.W = reservoir.M > 0.0 && reservoir.radiance.w > 0.0 ? reservoir.wSum / ( reservoir.M * reservoir.radiance.w ) : 0.0;
}

#endif
