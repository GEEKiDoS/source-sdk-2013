//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_PARTICLE_GAP_HELPERS_H
#define TERROR_PARTICLE_GAP_HELPERS_H
#include "particles/particles.h"
#include "l4d2/effects/terror_particle_attributes.h"

#pragma float_control( precise, on, push )

// Keep serialized retail field numbers; use the existing transport for 21..23.
inline uint32 TerrorGapAttributeMask( int field )
{
	if ( field < 0 || field > 23 ) return 0; // Standing minimal undefined-step rule.
	if ( field == 21 ) return TERROR_PARTICLE_NORMAL_MASK;
	if ( field == 22 ) return TERROR_PARTICLE_GLOW_RGB_MASK;
	if ( field == 23 ) return TERROR_PARTICLE_GLOW_ALPHA_MASK;
	return 1u << field;
}
inline int TerrorGapScalarAttribute( int field )
{
	return field == 21 ? TERROR_PARTICLE_NORMAL_X : field == 23 ? TERROR_PARTICLE_GLOW_ALPHA : field;
}
inline bool TerrorGapVectorField( int field )
{
	return field == 0 || field == 2 || field == 6 || field == 15 || field == 17 || field == 18 || field == 20 || field == 21 || field == 22;
}
inline uint64 TerrorGapCPMask( int point ) { return point >= 0 && point < MAX_PARTICLE_CONTROL_POINTS ? 1ull << point : 0; }
inline bool TerrorGapCPValid( int point ) { return point >= 0 && point < MAX_PARTICLE_CONTROL_POINTS; }
inline int TerrorGapCurrentCP( CParticleCollection *particles, int point ) { return MIN( point, particles->GetHighestControlPoint() ); }
inline Vector TerrorGapReadVector( CParticleCollection *particles, int field, int index )
{
	if ( field == 21 ) return TerrorReadParticleNormal( particles, index );
	if ( field == 22 ) return TerrorReadParticleGlow( particles, index );
	const float *value = particles->GetFloatAttributePtr( field, index );
	return Vector( value[0], value[4], value[8] );
}
inline void TerrorGapWriteVector( CParticleCollection *particles, int field, int index, const Vector &value )
{
	if ( field == 21 ) { TerrorWriteParticleNormal( particles, index, value ); return; }
	if ( field == 22 ) { TerrorWriteParticleGlow( particles, index, value ); return; }
	float *out = particles->GetFloatAttributePtrForWrite( field, index );
	out[0] = value.x; out[4] = value.y; out[8] = value.z;
}

// A native vector can be three SoA registers or the existing three scalar fields.
struct TerrorGapVectorWrite_t
{
	fltx4 *value[3];
	size_t stride[3];
	void Init( CParticleCollection *particles, int field )
	{
		if ( field == 21 || field == 22 )
		{
			const int first = field == 21 ? TERROR_PARTICLE_NORMAL_X : TERROR_PARTICLE_GLOW_RED;
			for ( int axis = 0; axis < 3; ++axis ) value[axis] = particles->GetM128AttributePtrForWrite( first + axis, &stride[axis] );
		}
		else
		{
			value[0] = particles->GetM128AttributePtrForWrite( field, &stride[0] );
			value[1] = value[0] + 1; value[2] = value[0] + 2;
			stride[1] = stride[2] = stride[0];
		}
	}
	void Store( const FourVectors &v ) { *value[0] = v.x; *value[1] = v.y; *value[2] = v.z; }
	void Advance() { for ( int axis = 0; axis < 3; ++axis ) value[axis] += stride[axis]; }
};
inline fltx4 TerrorGapLength( const FourVectors &v )
{
	return SqrtSIMD( AddSIMD( AddSIMD( MulSIMD( v.y, v.y ), MulSIMD( v.x, v.x ) ), MulSIMD( v.z, v.z ) ) );
}
inline fltx4 TerrorGapUnitRange( const fltx4 &v ) { return MaxSIMD( Four_Zeros, MinSIMD( Four_Ones, v ) ); }
inline fltx4 TerrorGapRemapSIMD( const fltx4 &v, float inMin, float inMax, float outMin, float outMax )
{
	// SIMD uses denominator 1 for equal endpoints; scalar uses a step instead.
	const fltx4 t = TerrorGapUnitRange( DivSIMD( SubSIMD( v, ReplicateX4( inMin ) ), ReplicateX4( inMin == inMax ? 1.0f : inMax - inMin ) ) );
	return AddSIMD( MulSIMD( t, ReplicateX4( outMax - outMin ) ), ReplicateX4( outMin ) );
}
inline float TerrorGapClampUnitScalar( float value )
{
	if ( value < 0.0f ) return 0.0f;
	if ( value > 1.0f ) return 1.0f;
	return value;
}
inline float TerrorGapRemapScalar( float value, float inMin, float inMax, float outMin, float outMax )
{
	if ( inMin == inMax ) return value >= inMax ? outMax : outMin;
	const float fraction = TerrorGapClampUnitScalar( ( value - inMin ) / ( inMax - inMin ) );
	return ( outMax - outMin ) * fraction + outMin;
}
inline bool TerrorGapLifetimeRange( float time, float start, float end )
{
	// Native COMISS/JA then COMISS/JB accepts unordered operands here.
	return ( !( start > time ) && !( time >= end ) ) || start == -1.0f || end == -1.0f;
}
#pragma float_control( pop )
#endif
