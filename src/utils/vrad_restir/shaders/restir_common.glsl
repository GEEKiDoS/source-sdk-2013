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

// Deterministic PCG hash + digitally scrambled base-2 radical inverse. SampleStream mixes seed,
// face, sample index WITHIN the face and style slot: it is the same in every iteration, so a
// low-discrepancy sequence indexed by iteration stays one sequence per sample. SampleSeed adds
// the iteration for draws that are independent per iteration. No key contains a dispatch or
// global invocation index, so batching is invariant.
uint Hash( uint value )
{
	uint state = value * 747796405u + 2891336453u;
	uint word = ( ( state >> ( ( state >> 28u ) + 4u ) ) ^ state ) * 277803737u;
	return ( word >> 22u ) ^ word;
}

uint SampleStream( uint face, uint sampleInFace, uint slot, uint stream )
{
	uint key = Hash( pc.seed ^ 0x9E3779B9u );
	key = Hash( key ^ face );
	key = Hash( key ^ sampleInFace );
	key = Hash( key ^ slot );
	return Hash( key ^ stream );
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

// Point `index` of the base-2 (0,2)-sequence (van der Corput, Sobol' dimension 2) under a
// per-sample digital shift. Every aligned block of 2^m consecutive indices is a (0,m,2)-net,
// so the directions a sample draws over its iterations stratify the unit square.
vec2 SobolPoint( uint index, uvec2 shift )
{
	uint x = bitfieldReverse( index );
	uint y = 0u;
	for ( uint direction = 0x80000000u; index != 0u; index >>= 1u, direction ^= direction >> 1u )
	{
		if ( ( index & 1u ) != 0u )
			y ^= direction;
	}
	return vec2( float( ( x ^ shift.x ) >> 8u ), float( ( y ^ shift.y ) >> 8u ) ) * ( 1.0 / 16777216.0 );
}

// Bounce light is fed back from the irradiance already resolved on the hit face, so the first
// iterations only deepen the bounce chain. The final mean restarts here, once every estimate
// covers nearly the converged integrand; exact direct light seeds the feedback from iteration 0,
// so a short burn-in is enough.
uint FirstAveragedIteration()
{
	return max( 1u, min( pc.totalIterations / 4u, 8u ) );
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

// FiniteRadiance without the clamp: SH coefficients of bands above DC are signed.
vec3 FiniteSigned( vec3 value )
{
	return vec3( isnan( value.x ) || isinf( value.x ) ? 0.0 : value.x,
		isnan( value.y ) || isinf( value.y ) ? 0.0 : value.y,
		isnan( value.z ) || isinf( value.z ) ? 0.0 : value.z );
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

uint ReservoirAddress( uint index )
{
	return ( pc.flags & RESTIR_PC_RESERVOIR_PAGE ) != 0u ?
		index - pc.pageBase.x : index;
}

uint SampleAddress( uint index )
{
	return ( pc.flags & RESTIR_PC_RESERVOIR_PAGE ) != 0u ?
		index - pc.pageBase.y : index;
}

uint LuxelAddress( uint index )
{
	return ( pc.flags & RESTIR_PC_RESERVOIR_PAGE ) != 0u ?
		index - pc.pageBase.z : index;
}

uint OutputAddress( uint index )
{
	return ( pc.flags & RESTIR_PC_RESERVOIR_PAGE ) != 0u ?
		index - pc.pageBase.w : index;
}

uint AccumulationAddress( uint index )
{
	return ReservoirAddress( index ) * RESTIR_MAX_CHANNELS;
}
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

ReSTIRLightSample EmptyLightSample()
{
	return ReSTIRLightSample( vec4( 0.0 ), vec4( 0.0 ), 0.0, RESTIR_NO_HIT, RESTIR_NO_HIT );
}

// utils/vrad/lightmap.cpp:1985-1993: bump channels share light visibility and source radiance
// but evaluate their own cosine.
vec3 ChannelNormal( ReSTIRGpuSample sampleRecord, int channel )
{
	if ( channel == 1 )
		return sampleRecord.bump0.xyz;
	if ( channel == 2 )
		return sampleRecord.bump1.xyz;
	if ( channel == 3 )
		return sampleRecord.bump2.xyz;
	return sampleRecord.normal.xyz;
}

#endif
