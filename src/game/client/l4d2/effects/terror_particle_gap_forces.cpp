//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/effects/terror_particle_gap_helpers.h"
#include <math.h>
#include <float.h>
#include <limits.h>

#include "tier0/memdbgon.h"
#pragma float_control( precise, on, push )

#include "l4d2/effects/terror_particle_gap_noise_data.inc"

static FourVectors TerrorGapNoiseLerp( const FourVectors &a, const FourVectors &b, const fltx4 &fraction )
{
	FourVectors value;
	value.x = AddSIMD( MulSIMD( SubSIMD( b.x, a.x ), fraction ), a.x );
	value.y = AddSIMD( MulSIMD( SubSIMD( b.y, a.y ), fraction ), a.y );
	value.z = AddSIMD( MulSIMD( SubSIMD( b.z, a.z ), fraction ), a.z );
	return value;
}

// C4F1690/C4F0920. The target header declares DNoiseSIMD, but its linked
// mathlib.lib has no implementation. This is the native vector-valued lattice
// function, not three calls to scalar NoiseSIMD and not a curl approximation.
static FourVectors TerrorGapDNoiseSIMD( const FourVectors &input )
{
	const fltx4 mask = _mm_castsi128_ps( _mm_set1_epi32( 0xffff ) );
	const fltx4 magic = ReplicateX4( 32768.0f );
	const fltx4 xBits = AndSIMD( AddSIMD( input.x, magic ), mask );
	const fltx4 yBits = AndSIMD( AddSIMD( input.y, magic ), mask );
	const fltx4 zBits = AndSIMD( AddSIMD( input.z, magic ), mask );
	fltx4 xFraction, yFraction, zFraction;
	FourVectors corners[8];
	for ( int lane = 0; lane < 4; ++lane )
	{
		const unsigned int xb = SubInt( xBits, lane ), yb = SubInt( yBits, lane ), zb = SubInt( zBits, lane );
		SubFloat( xFraction, lane ) = ( xb & 255 ) * ( 1.0f / 256.0f );
		SubFloat( yFraction, lane ) = ( yb & 255 ) * ( 1.0f / 256.0f );
		SubFloat( zFraction, lane ) = ( zb & 255 ) * ( 1.0f / 256.0f );
		const unsigned int x = xb >> 8, y = yb >> 8, z = zb >> 8;
		for ( int dx = 0; dx < 2; ++dx )
		{
			const int a = s_TerrorGapNoisePermA[( x + dx ) & 255];
			for ( int dy = 0; dy < 2; ++dy )
			{
				const int b = s_TerrorGapNoisePermB[( y + dy + a ) & 255];
				for ( int dz = 0; dz < 2; ++dz )
				{
					const int sample = s_TerrorGapNoisePermC[( z + dz + b ) & 255];
					FourVectors &corner = corners[( dx << 2 ) | ( dy << 1 ) | dz];
					SubFloat( corner.x, lane ) = s_TerrorGapNoiseVectors[sample][0];
					SubFloat( corner.y, lane ) = s_TerrorGapNoiseVectors[sample][1];
					SubFloat( corner.z, lane ) = s_TerrorGapNoiseVectors[sample][2];
				}
			}
		}
	}
	const FourVectors xy00 = TerrorGapNoiseLerp( corners[0], corners[4], xFraction );
	const FourVectors xy01 = TerrorGapNoiseLerp( corners[1], corners[5], xFraction );
	const FourVectors xy10 = TerrorGapNoiseLerp( corners[2], corners[6], xFraction );
	const FourVectors xy11 = TerrorGapNoiseLerp( corners[3], corners[7], xFraction );
	return TerrorGapNoiseLerp( TerrorGapNoiseLerp( xy00, xy10, yFraction ), TerrorGapNoiseLerp( xy01, xy11, yFraction ), zFraction );
}

// Explicit SIMD operations retain native arithmetic grouping and unordered masks.
class C_OP_BoxConstraint : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_BoxConstraint );
public:
	virtual uint32 GetWrittenAttributes() const OVERRIDE { return PARTICLE_ATTRIBUTE_XYZ_MASK; }
	virtual uint32 GetReadAttributes() const OVERRIDE { return PARTICLE_ATTRIBUTE_XYZ_MASK; }
	virtual bool EnforceConstraint( int first, int count, CParticleCollection *particles, void *context, int validLast ) const OVERRIDE
	{
		size_t stride;
		FourVectors *position = particles->Get4VAttributePtrForWrite( PARTICLE_ATTRIBUTE_XYZ, &stride );
		position += first * stride;
		FourVectors low, high; low.DuplicateVector( m_vecMin ); high.DuplicateVector( m_vecMax );
		fltx4 unchanged = ReplicateX4( -1.0f );
		for ( int block = 0; block < count; ++block, position += stride )
		{
			const fltx4 x = MinSIMD( MaxSIMD( position->x, low.x ), high.x );
			const fltx4 y = MinSIMD( MaxSIMD( position->y, low.y ), high.y );
			const fltx4 z = MinSIMD( MaxSIMD( position->z, low.z ), high.z );
			unchanged = AndSIMD( AndSIMD( AndSIMD( unchanged, CmpEqSIMD( position->x, x ) ), CmpEqSIMD( position->y, y ) ), CmpEqSIMD( position->z, z ) );
			position->x = x; position->y = y; position->z = z;
		}
		return _mm_movemask_ps( unchanged ) != 15;
	}
private:
	Vector m_vecMin, m_vecMax;
};
DEFINE_PARTICLE_OPERATOR( C_OP_BoxConstraint, "Constrain particles to a box", OPERATOR_GENERIC );
BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_BoxConstraint )
	DMXELEMENT_UNPACK_FIELD( "min coords", "0 0 0", Vector, m_vecMin )
	DMXELEMENT_UNPACK_FIELD( "max coords", "0 0 0", Vector, m_vecMax )
END_PARTICLE_OPERATOR_UNPACK( C_OP_BoxConstraint )

class C_OP_ForceBasedOnDistanceToPlane : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_ForceBasedOnDistanceToPlane );
public:
	virtual uint32 GetWrittenAttributes() const OVERRIDE { return 0; }
	virtual uint32 GetReadAttributes() const OVERRIDE { return 0; } // Native declaration, despite position reads.
	virtual uint64 GetReadControlPointMask() const OVERRIDE { return TerrorGapCPMask( m_nControlPoint ); }
	virtual void AddForces( FourVectors *forces, CParticleCollection *particles, int count, float strength, void *context ) const OVERRIDE
	{
		if ( !TerrorGapCPValid( m_nControlPoint ) ) return;
		const float exponent = m_flExponent * 4.0f;
		if ( !( static_cast< double >( exponent ) > INT_MIN && static_cast< double >( exponent ) < 2147483648.0 ) ) return;
		const Vector &origin = particles->ControlPoint( TerrorGapCurrentCP( particles, m_nControlPoint ) ).m_Position;
		FourVectors center, normal, start, delta;
		center.DuplicateVector( origin ); normal.DuplicateVector( m_vecNormal ); start.DuplicateVector( m_vecMinForce ); delta.DuplicateVector( m_vecMaxForce - m_vecMinForce );
		const float range = m_flMaxDistance - m_flMinDistance;
		const fltx4 inverse = ReplicateX4( range != 0.0f ? 1.0f / range : 0.0f );
		const fltx4 minimum = ReplicateX4( m_flMinDistance );
		size_t stride;
		const FourVectors *position = particles->Get4VAttributePtr( PARTICLE_ATTRIBUTE_XYZ, &stride );
		for ( int block = 0; block < count; ++block, position += stride )
		{
			const fltx4 distance = AddSIMD( AddSIMD( MulSIMD( SubSIMD( position->y, center.y ), normal.y ), MulSIMD( SubSIMD( position->x, center.x ), normal.x ) ), MulSIMD( SubSIMD( position->z, center.z ), normal.z ) );
			const fltx4 fraction = Pow_FixedPoint_Exponent_SIMD( TerrorGapUnitRange( MulSIMD( SubSIMD( distance, minimum ), inverse ) ), static_cast< int >( m_flExponent * 4.0f ) );
			// Native C37D960 ignores strength, including non-unit strengths.
			forces[block].x = AddSIMD( AddSIMD( MulSIMD( delta.x, fraction ), start.x ), forces[block].x );
			forces[block].y = AddSIMD( forces[block].y, AddSIMD( MulSIMD( delta.y, fraction ), start.y ) );
			forces[block].z = AddSIMD( forces[block].z, AddSIMD( MulSIMD( delta.z, fraction ), start.z ) );
		}
	}
private:
	float m_flMinDistance;
	Vector m_vecMinForce;
	float m_flMaxDistance;
	Vector m_vecMaxForce, m_vecNormal;
	int m_nControlPoint;
	float m_flExponent;
};
DEFINE_PARTICLE_OPERATOR( C_OP_ForceBasedOnDistanceToPlane, "Force based on distance from plane", OPERATOR_GENERIC );
BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_ForceBasedOnDistanceToPlane )
	DMXELEMENT_UNPACK_FIELD( "Min distance from plane", "0", float, m_flMinDistance )
	DMXELEMENT_UNPACK_FIELD( "Force at Min distance", "0 0 0", Vector, m_vecMinForce )
	DMXELEMENT_UNPACK_FIELD( "Max Distance from plane", "1", float, m_flMaxDistance )
	DMXELEMENT_UNPACK_FIELD( "Force at Max distance", "0 0 0", Vector, m_vecMaxForce )
	DMXELEMENT_UNPACK_FIELD( "Plane Normal", "0 0 1", Vector, m_vecNormal )
	DMXELEMENT_UNPACK_FIELD( "Control point number", "0", int, m_nControlPoint )
	DMXELEMENT_UNPACK_FIELD( "Exponent", "1", float, m_flExponent )
END_PARTICLE_OPERATOR_UNPACK( C_OP_ForceBasedOnDistanceToPlane )

class C_OP_TimeVaryingForce : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_TimeVaryingForce );
public:
	virtual uint32 GetWrittenAttributes() const OVERRIDE { return 0; }
	virtual uint32 GetReadAttributes() const OVERRIDE { return PARTICLE_ATTRIBUTE_CREATION_TIME_MASK; }
	virtual void AddForces( FourVectors *forces, CParticleCollection *particles, int count, float strength, void *context ) const OVERRIDE
	{
		FourVectors start, delta;
		start.DuplicateVector( m_vecStart * strength );
		delta.DuplicateVector( Vector( m_vecEnd.x * strength, m_vecEnd.y * strength, m_vecEnd.z * strength ) );
		delta -= start;
		const fltx4 inverse = ReplicateX4( static_cast< float >( 1.0 / ( static_cast< double >( m_flEnd ) - m_flStart ) ) );
		const fltx4 begin = ReplicateX4( m_flStart );
		size_t stride;
		const fltx4 *created = particles->GetM128AttributePtr( PARTICLE_ATTRIBUTE_CREATION_TIME, &stride );
		for ( int block = 0; block < count; ++block, created += stride )
		{
			const fltx4 fraction = TerrorGapUnitRange( MulSIMD( SubSIMD( SubSIMD( particles->m_fl4CurTime, *created ), begin ), inverse ) );
			forces[block].x = AddSIMD( AddSIMD( MulSIMD( delta.x, fraction ), start.x ), forces[block].x );
			forces[block].y = AddSIMD( forces[block].y, AddSIMD( MulSIMD( delta.y, fraction ), start.y ) );
			forces[block].z = AddSIMD( forces[block].z, AddSIMD( MulSIMD( delta.z, fraction ), start.z ) );
		}
	}
private:
	float m_flStart;
	Vector m_vecStart;
	float m_flEnd;
	Vector m_vecEnd;
};
DEFINE_PARTICLE_OPERATOR( C_OP_TimeVaryingForce, "time varying force", OPERATOR_GENERIC );
BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_TimeVaryingForce )
	DMXELEMENT_UNPACK_FIELD( "time to start transition", "0", float, m_flStart )
	DMXELEMENT_UNPACK_FIELD( "starting force", "0 0 0", Vector, m_vecStart )
	DMXELEMENT_UNPACK_FIELD( "time to end transition", "10", float, m_flEnd )
	DMXELEMENT_UNPACK_FIELD( "ending force", "0 0 0", Vector, m_vecEnd )
END_PARTICLE_OPERATOR_UNPACK( C_OP_TimeVaryingForce )

class C_OP_TurbulenceForce : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_TurbulenceForce );
public:
	virtual uint32 GetWrittenAttributes() const OVERRIDE { return 0; }
	virtual uint32 GetReadAttributes() const OVERRIDE { return PARTICLE_ATTRIBUTE_XYZ_MASK; }
	virtual void AddForces( FourVectors *forces, CParticleCollection *particles, int count, float strength, void *context ) const OVERRIDE
	{
		fltx4 scales[4]; FourVectors amounts[4];
		for ( int octave = 0; octave < 4; ++octave ) { scales[octave] = ReplicateX4( m_flScale[octave] ); amounts[octave].DuplicateVector( m_vecAmount[octave] ); }
		size_t stride;
		const FourVectors *position = particles->Get4VAttributePtr( PARTICLE_ATTRIBUTE_XYZ, &stride );
		for ( int block = 0; block < count; ++block, position += stride )
			for ( int octave = 0; octave < 4; ++octave )
			{
				FourVectors input; input.x = MulSIMD( scales[octave], position->x ); input.y = MulSIMD( scales[octave], position->y ); input.z = MulSIMD( scales[octave], position->z );
				const FourVectors noise = TerrorGapDNoiseSIMD( input );
				// C37DD90 evaluates even zero-scale octaves and ignores strength.
				forces[block].x = AddSIMD( forces[block].x, MulSIMD( amounts[octave].x, noise.x ) );
				forces[block].y = AddSIMD( forces[block].y, MulSIMD( amounts[octave].y, noise.y ) );
				forces[block].z = AddSIMD( forces[block].z, MulSIMD( amounts[octave].z, noise.z ) );
			}
	}
private:
	float m_flScale[4];
	Vector m_vecAmount[4];
};
DEFINE_PARTICLE_OPERATOR( C_OP_TurbulenceForce, "turbulent force", OPERATOR_GENERIC );
BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_TurbulenceForce )
	DMXELEMENT_UNPACK_FIELD( "Noise scale 0", "1", float, m_flScale[0] )
	DMXELEMENT_UNPACK_FIELD( "Noise amount 0", "1 1 1", Vector, m_vecAmount[0] )
	DMXELEMENT_UNPACK_FIELD( "Noise scale 1", "0", float, m_flScale[1] )
	DMXELEMENT_UNPACK_FIELD( "Noise amount 1", ".5 .5 .5", Vector, m_vecAmount[1] )
	DMXELEMENT_UNPACK_FIELD( "Noise scale 2", "0", float, m_flScale[2] )
	DMXELEMENT_UNPACK_FIELD( "Noise amount 2", ".25 .25 .25", Vector, m_vecAmount[2] )
	DMXELEMENT_UNPACK_FIELD( "Noise scale 3", "0", float, m_flScale[3] )
	DMXELEMENT_UNPACK_FIELD( "Noise amount 3", ".125 .125 .125", Vector, m_vecAmount[3] )
END_PARTICLE_OPERATOR_UNPACK( C_OP_TurbulenceForce )

class C_OP_ParentVortices : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_ParentVortices );
public:
	virtual uint32 GetWrittenAttributes() const OVERRIDE { return 0; }
	virtual uint32 GetReadAttributes() const OVERRIDE { return PARTICLE_ATTRIBUTE_XYZ_MASK; }
	virtual void AddForces( FourVectors *forces, CParticleCollection *particles, int count, float strength, void *context ) const OVERRIDE
	{
		CParticleCollection *parent = particles->m_pParent;
		if ( !parent || !parent->m_nActiveParticles ) return;
		struct Vortex_t { FourVectors center; fltx4 inverseRadiusSquared, force; };
		Vortex_t *vortices = static_cast< Vortex_t * >( stackalloc( parent->m_nActiveParticles * sizeof( Vortex_t ) ) );
		FourVectors axis; axis.DuplicateVector( m_vecAxis );
		const fltx4 axisLength = AddSIMD( AddSIMD( MulSIMD( axis.y, axis.y ), MulSIMD( axis.x, axis.x ) ), MulSIMD( axis.z, axis.z ) );
		const fltx4 axisEstimate = _mm_rsqrt_ps( axisLength );
		const fltx4 axisInverse = MulSIMD( MulSIMD( SubSIMD( ReplicateX4( 3.0f ), MulSIMD( MulSIMD( axisEstimate, axisEstimate ), axisLength ) ), axisEstimate ), ReplicateX4( 0.5f ) );
		axis *= axisInverse;
		for ( int index = 0; index < parent->m_nActiveParticles; ++index )
		{
			const float radiusInverse = static_cast< float >( 1.0 / ( static_cast< double >( *parent->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_RADIUS, index ) ) + 0.00001 ) );
			vortices[index].inverseRadiusSquared = ReplicateX4( radiusInverse * radiusInverse );
			vortices[index].force = ReplicateX4( ( m_flForce * strength ) * *parent->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_ALPHA, index ) );
			Vector center = TerrorGapReadVector( parent, PARTICLE_ATTRIBUTE_XYZ, index );
			// Native bug: yaw flips the CENTER, not the twist axis.
			if ( m_bFlipWithYaw && static_cast< double >( *parent->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_YAW, index ) ) >= M_PI ) center *= -1.0f;
			vortices[index].center.DuplicateVector( center );
		}
		size_t stride;
		const FourVectors *position = particles->Get4VAttributePtr( PARTICLE_ATTRIBUTE_XYZ, &stride );
		for ( int block = 0; block < count; ++block, position += stride )
		{
			FourVectors sum; sum.x = sum.y = sum.z = Four_Zeros;
			for ( int index = 0; index < parent->m_nActiveParticles; ++index )
			{
				FourVectors direction = *position; direction -= vortices[index].center;
				const fltx4 length = AddSIMD( AddSIMD( MulSIMD( direction.x, direction.x ), MulSIMD( direction.y, direction.y ) ), MulSIMD( direction.z, direction.z ) );
				const fltx4 estimate = _mm_rsqrt_ps( length );
				const fltx4 inverse = MulSIMD( MulSIMD( SubSIMD( ReplicateX4( 3.0f ), MulSIMD( MulSIMD( estimate, estimate ), length ) ), estimate ), ReplicateX4( 0.5f ) );
				direction *= inverse;
				const fltx4 dot = AddSIMD( AddSIMD( MulSIMD( direction.y, axis.y ), MulSIMD( direction.x, axis.x ) ), MulSIMD( direction.z, axis.z ) );
				// Native bug: subtract direction*dot, not axis*dot.
				FourVectors radial = direction; radial.x = SubSIMD( direction.x, MulSIMD( direction.x, dot ) ); radial.y = SubSIMD( direction.y, MulSIMD( direction.y, dot ) ); radial.z = SubSIMD( direction.z, MulSIMD( direction.z, dot ) );
				const fltx4 radialLength = AddSIMD( AddSIMD( MulSIMD( radial.x, radial.x ), MulSIMD( radial.y, radial.y ) ), MulSIMD( radial.z, radial.z ) );
				const fltx4 valid = AndSIMD( CmpLtSIMD( ReplicateX4( FLT_EPSILON ), radialLength ), CmpLtSIMD( ReplicateX4( FLT_EPSILON ), length ) );
				const fltx4 radialEstimate = _mm_rsqrt_ps( radialLength );
				const fltx4 radialInverse = MulSIMD( MulSIMD( SubSIMD( ReplicateX4( 3.0f ), MulSIMD( MulSIMD( radialEstimate, radialEstimate ), radialLength ) ), radialEstimate ), ReplicateX4( 0.5f ) );
				radial *= radialInverse;
				const fltx4 force = MulSIMD( MaxSIMD( Four_Zeros, SubSIMD( Four_Ones, MulSIMD( vortices[index].inverseRadiusSquared, length ) ) ), vortices[index].force );
				sum.x = AddSIMD( AndSIMD( MulSIMD( SubSIMD( MulSIMD( radial.y, axis.z ), MulSIMD( radial.z, axis.y ) ), force ), valid ), sum.x );
				sum.y = AddSIMD( AndSIMD( MulSIMD( SubSIMD( MulSIMD( radial.z, axis.x ), MulSIMD( radial.x, axis.z ) ), force ), valid ), sum.y );
				sum.z = AddSIMD( AndSIMD( MulSIMD( SubSIMD( MulSIMD( radial.x, axis.y ), MulSIMD( radial.y, axis.x ) ), force ), valid ), sum.z );
			}
			forces[block] += sum;
		}
	}
private:
	float m_flForce;
	Vector m_vecAxis;
	bool m_bFlipWithYaw;
};
DEFINE_PARTICLE_OPERATOR( C_OP_ParentVortices, "Create vortices from parent particles", OPERATOR_GENERIC );
BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_ParentVortices )
	DMXELEMENT_UNPACK_FIELD( "amount of force", "0", float, m_flForce )
	DMXELEMENT_UNPACK_FIELD( "twist axis", "0 0 1", Vector, m_vecAxis )
	DMXELEMENT_UNPACK_FIELD( "flip twist axis with yaw", "0", bool, m_bFlipWithYaw )
END_PARTICLE_OPERATOR_UNPACK( C_OP_ParentVortices )

#pragma float_control( pop )

void RegisterTerrorParticleGapForces()
{
	REGISTER_PARTICLE_OPERATOR( FUNCTION_CONSTRAINT, C_OP_BoxConstraint );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_FORCEGENERATOR, C_OP_ParentVortices );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_FORCEGENERATOR, C_OP_TimeVaryingForce );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_FORCEGENERATOR, C_OP_TurbulenceForce );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_FORCEGENERATOR, C_OP_ForceBasedOnDistanceToPlane );
}
