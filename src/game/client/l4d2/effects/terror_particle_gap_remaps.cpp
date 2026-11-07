//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/effects/terror_particle_gap_helpers.h"
#include <math.h>

#include "tier0/memdbgon.h"

#pragma float_control( precise, on, push )

class C_INIT_RemapSpeedToScalar : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_RemapSpeedToScalar );
public:
	virtual void InitParams( CParticleSystemDefinition *definition, CDmxElement *element ) OVERRIDE
	{
		if ( static_cast< double >( m_flInputMin ) < 0.001 ) m_flInputMin = static_cast< float >( 0.001 );
		if ( static_cast< double >( m_flInputMax ) < 0.001 ) m_flInputMax = static_cast< float >( 0.001 );
	}
	virtual uint32 GetWrittenAttributes() const OVERRIDE { return TerrorGapAttributeMask( m_nOutput ); }
	virtual uint32 GetReadAttributes() const OVERRIDE { return TerrorGapAttributeMask( m_nOutput ) | 5; }
	// Main-approved API-shape adaptation: the multiply body reads real initial storage.
	virtual uint32 GetReadInitialAttributes() const OVERRIDE { return m_bScaleInitial ? TerrorGapAttributeMask( m_nOutput ) : 0; }
	virtual bool InitMultipleOverride() OVERRIDE { return true; }
	virtual void InitNewParticlesScalar( CParticleCollection *particles, int first, int count, int writeMask, void *context ) const OVERRIDE
	{
		if ( !TerrorGapAttributeMask( m_nOutput ) || !TerrorGapLifetimeRange( particles->m_flCurTime, m_flStart, m_flEnd ) ) return;
		const int attribute = TerrorGapScalarAttribute( m_nOutput );
		float low = m_flOutputMin, high = m_flOutputMax;
		if ( m_nOutput == 7 || m_nOutput == 16 ) { low = TerrorGapClampUnitScalar( low ); high = TerrorGapClampUnitScalar( high ); }
		if ( m_bPerParticle )
		{
			for ( int index = first; index < first + count; ++index )
			{
				const Vector delta = TerrorGapReadVector( particles, PARTICLE_ATTRIBUTE_XYZ, index ) - TerrorGapReadVector( particles, PARTICLE_ATTRIBUTE_PREV_XYZ, index );
				const float speed = sqrtf( ( delta.y * delta.y + delta.z * delta.z ) + delta.x * delta.x ) / particles->m_flPreviousDt;
				float value = TerrorGapRemapScalar( speed, m_flInputMin, m_flInputMax, low, high );
				if ( m_bScaleInitial ) value *= *particles->GetInitialFloatAttributePtr( attribute, index );
				*particles->GetFloatAttributePtrForWrite( attribute, index ) = value;
			}
		}
		else
		{
			if ( !TerrorGapCPValid( m_nControlPoint ) ) return;
			Vector previous;
			particles->GetControlPointAtPrevTime( m_nControlPoint, &previous );
			const Vector delta = particles->ControlPoint( TerrorGapCurrentCP( particles, m_nControlPoint ) ).m_Position - previous;
			const float speed = sqrtf( ( delta.y * delta.y + delta.z * delta.z ) + delta.x * delta.x ) / particles->m_flPreviousDt;
			float value = TerrorGapRemapScalar( speed, m_flInputMin, m_flInputMax, low, high );
			for ( int index = first; index < first + count; ++index )
			{
				// Native bug: the scaled value carries into the next particle.
				if ( m_bScaleInitial ) value *= *particles->GetInitialFloatAttributePtr( attribute, index );
				*particles->GetFloatAttributePtrForWrite( attribute, index ) = value;
			}
		}
	}
	virtual void InitNewParticlesBlock( CParticleCollection *particles, int firstBlock, int count, int writeMask, void *context ) const OVERRIDE
	{
		if ( !TerrorGapAttributeMask( m_nOutput ) || !TerrorGapLifetimeRange( particles->m_flCurTime, m_flStart, m_flEnd ) ) return;
		const int attribute = TerrorGapScalarAttribute( m_nOutput );
		float low = m_flOutputMin, high = m_flOutputMax;
		if ( m_nOutput == 7 || m_nOutput == 16 ) { low = TerrorGapClampUnitScalar( low ); high = TerrorGapClampUnitScalar( high ); }
		size_t outputStride, initialStride, positionStride, previousStride;
		fltx4 *out = particles->GetM128AttributePtrForWrite( attribute, &outputStride );
		const fltx4 *initial = particles->GetInitialM128AttributePtr( attribute, &initialStride );
		const FourVectors *position = particles->Get4VAttributePtr( PARTICLE_ATTRIBUTE_XYZ, &positionStride );
		const FourVectors *previous = particles->Get4VAttributePtr( PARTICLE_ATTRIBUTE_PREV_XYZ, &previousStride );
		out += firstBlock * outputStride;
		if ( initial ) initial += firstBlock * initialStride;
		position += firstBlock * positionStride; previous += firstBlock * previousStride;
		if ( m_bPerParticle )
		{
			for ( int block = 0; block < count; ++block )
			{
				FourVectors delta = *position; delta -= *previous;
				fltx4 value = TerrorGapRemapSIMD( DivSIMD( TerrorGapLength( delta ), ReplicateX4( particles->m_flPreviousDt ) ), m_flInputMin, m_flInputMax, low, high );
				if ( m_bScaleInitial ) value = MulSIMD( value, *initial );
				*out = value;
				// C395360 uses the INITIAL OUTPUT stride for both position streams,
				// not their own strides. Preserve this defined native quirk.
				position = reinterpret_cast< const FourVectors * >( reinterpret_cast< const fltx4 * >( position ) + 3 * initialStride );
				previous = reinterpret_cast< const FourVectors * >( reinterpret_cast< const fltx4 * >( previous ) + 3 * initialStride );
				out += outputStride; if ( initial ) initial += initialStride;
			}
		}
		else
		{
			if ( !TerrorGapCPValid( m_nControlPoint ) ) return;
			Vector oldPoint;
			particles->GetControlPointAtPrevTime( m_nControlPoint, &oldPoint );
			const Vector delta = particles->ControlPoint( TerrorGapCurrentCP( particles, m_nControlPoint ) ).m_Position - oldPoint;
			const float speed = sqrtf( ( delta.x * delta.x + delta.y * delta.y ) + delta.z * delta.z ) / particles->m_flPreviousDt;
			fltx4 value = TerrorGapRemapSIMD( ReplicateX4( speed ), m_flInputMin, m_flInputMax, low, high );
			for ( int block = 0; block < count; ++block )
			{
				if ( m_bScaleInitial ) value = MulSIMD( value, *initial ); // Native per-lane carry between blocks.
				*out = value;
				// Native uses initial stride for its output stream in this branch.
				out += initialStride; if ( initial ) initial += initialStride;
			}
		}
	}
private:
	int m_nOutput, m_nControlPoint;
	float m_flStart, m_flEnd, m_flInputMin, m_flInputMax, m_flOutputMin, m_flOutputMax;
	bool m_bScaleInitial, m_bPerParticle;
};
DEFINE_PARTICLE_OPERATOR( C_INIT_RemapSpeedToScalar, "Remap Speed to Scalar", OPERATOR_GENERIC );
BEGIN_PARTICLE_OPERATOR_UNPACK( C_INIT_RemapSpeedToScalar )
	DMXELEMENT_UNPACK_FIELD( "emitter lifetime start time (seconds)", "-1", float, m_flStart )
	DMXELEMENT_UNPACK_FIELD( "emitter lifetime end time (seconds)", "-1", float, m_flEnd )
	DMXELEMENT_UNPACK_FIELD( "control point number (ignored if per particle)", "0", int, m_nControlPoint )
	DMXELEMENT_UNPACK_FIELD( "per particle", "0", bool, m_bPerParticle )
	DMXELEMENT_UNPACK_FIELD( "input minimum", "0", float, m_flInputMin )
	DMXELEMENT_UNPACK_FIELD( "input maximum", "1", float, m_flInputMax )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "output field", "3", int, m_nOutput, "intchoice particlefield_scalar" )
	DMXELEMENT_UNPACK_FIELD( "output minimum", "0", float, m_flOutputMin )
	DMXELEMENT_UNPACK_FIELD( "output maximum", "1", float, m_flOutputMax )
	DMXELEMENT_UNPACK_FIELD( "output is scalar of initial random range", "0", bool, m_bScaleInitial )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_RemapSpeedToScalar )

class C_OP_RemapSpeed : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_RemapSpeed );
public:
	virtual void InitParams( CParticleSystemDefinition *definition, CDmxElement *element ) OVERRIDE
	{
		if ( static_cast< double >( m_flInputMin ) < 0.001 ) m_flInputMin = static_cast< float >( 0.001 );
		if ( static_cast< double >( m_flInputMax ) < 0.001 ) m_flInputMax = static_cast< float >( 0.001 );
	}
	virtual uint32 GetWrittenAttributes() const OVERRIDE { return TerrorGapAttributeMask( m_nOutput ); }
	virtual uint32 GetReadAttributes() const OVERRIDE { return 5; }
	virtual uint32 GetReadInitialAttributes() const OVERRIDE { return TerrorGapAttributeMask( m_nOutput ); }
	virtual void Operate( CParticleCollection *particles, float strength, void *context ) const OVERRIDE
	{
		if ( !TerrorGapAttributeMask( m_nOutput ) ) return;
		const int attribute = TerrorGapScalarAttribute( m_nOutput );
		float low = m_flOutputMin, high = m_flOutputMax;
		if ( m_nOutput == 7 || m_nOutput == 16 ) { low = TerrorGapClampUnitScalar( low ); high = TerrorGapClampUnitScalar( high ); }
		size_t positionStride, previousStride, outputStride, initialStride;
		const FourVectors *position = particles->Get4VAttributePtr( PARTICLE_ATTRIBUTE_XYZ, &positionStride );
		const FourVectors *previous = particles->Get4VAttributePtr( PARTICLE_ATTRIBUTE_PREV_XYZ, &previousStride );
		fltx4 *out = particles->GetM128AttributePtrForWrite( attribute, &outputStride );
		const fltx4 *initial = particles->GetInitialM128AttributePtr( attribute, &initialStride );
		for ( int block = 0; block < particles->m_nPaddedActiveParticles; ++block )
		{
			FourVectors delta = *position; delta -= *previous;
			fltx4 value = TerrorGapRemapSIMD( DivSIMD( TerrorGapLength( delta ), ReplicateX4( particles->m_flDt ) ), m_flInputMin, m_flInputMax, low, high );
			if ( m_bScaleInitial ) value = MulSIMD( value, *initial );
			*out = AddSIMD( MulSIMD( SubSIMD( value, *out ), ReplicateX4( strength ) ), *out );
			position += positionStride; previous += previousStride; out += outputStride; initial += initialStride;
		}
	}
private:
	int m_nOutput;
	float m_flInputMin, m_flInputMax, m_flOutputMin, m_flOutputMax;
	bool m_bScaleInitial;
};
DEFINE_PARTICLE_OPERATOR( C_OP_RemapSpeed, "Remap Speed to Scalar", OPERATOR_GENERIC );
BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_RemapSpeed )
	DMXELEMENT_UNPACK_FIELD( "input minimum", "0", float, m_flInputMin )
	DMXELEMENT_UNPACK_FIELD( "input maximum", "1", float, m_flInputMax )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "output field", "3", int, m_nOutput, "intchoice particlefield_scalar" )
	DMXELEMENT_UNPACK_FIELD( "output minimum", "0", float, m_flOutputMin )
	DMXELEMENT_UNPACK_FIELD( "output maximum", "1", float, m_flOutputMax )
	DMXELEMENT_UNPACK_FIELD( "output is scalar of initial random range", "0", bool, m_bScaleInitial )
END_PARTICLE_OPERATOR_UNPACK( C_OP_RemapSpeed )

class C_OP_RemapBoundstoCP : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_RemapBoundstoCP );
public:
	virtual void InitParams( CParticleSystemDefinition *definition, CDmxElement *element ) OVERRIDE { m_nControlPoint = clamp( m_nControlPoint, 0, 63 ); }
	virtual uint32 GetWrittenAttributes() const OVERRIDE { return 0; }
	virtual uint32 GetReadAttributes() const OVERRIDE { return 0; }
	virtual void Operate( CParticleCollection *particles, float strength, void *context ) const OVERRIDE
	{
		Vector low, high; particles->GetBounds( &low, &high );
		const Vector extent = high - low;
		const float volume = ( extent.y * extent.x ) * extent.z;
		Vector point = particles->ControlPoint( TerrorGapCurrentCP( particles, m_nControlPoint ) ).m_Position;
		// Invalid component would overwrite a stack local; skip only that step.
		if ( m_nComponent < 0 || m_nComponent > 2 ) return;
		point[m_nComponent] = TerrorGapRemapScalar( volume, m_flInputMin, m_flInputMax, m_flOutputMin, m_flOutputMax );
		particles->SetControlPoint( m_nControlPoint, point );
	}
private:
	int m_nControlPoint, m_nComponent;
	float m_flInputMin, m_flInputMax, m_flOutputMin, m_flOutputMax;
};
DEFINE_PARTICLE_OPERATOR( C_OP_RemapBoundstoCP, "Remap Particle Bounding Volume to CP", OPERATOR_GENERIC );
BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_RemapBoundstoCP )
	DMXELEMENT_UNPACK_FIELD( "Control Point Number to Set", "1", int, m_nControlPoint )
	DMXELEMENT_UNPACK_FIELD( "Input Min", "0", float, m_flInputMin )
	DMXELEMENT_UNPACK_FIELD( "Input Max", "100", float, m_flInputMax )
	DMXELEMENT_UNPACK_FIELD( "Output Min", "0", float, m_flOutputMin )
	DMXELEMENT_UNPACK_FIELD( "Output Max", "1", float, m_flOutputMax )
	DMXELEMENT_UNPACK_FIELD( "Output field 0-2 X/Y/Z", "0", int, m_nComponent )
END_PARTICLE_OPERATOR_UNPACK( C_OP_RemapBoundstoCP )

class C_OP_RemapCPtoVector : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_RemapCPtoVector );
public:
	virtual uint32 GetWrittenAttributes() const OVERRIDE { return TerrorGapAttributeMask( m_nOutput ) | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK; }
	virtual uint32 GetReadAttributes() const OVERRIDE { return PARTICLE_ATTRIBUTE_CREATION_TIME_MASK; }
	virtual uint64 GetReadControlPointMask() const OVERRIDE { return TerrorGapCPMask( m_nInputCP ) | ( m_nLocalCP == -1 ? 0 : TerrorGapCPMask( m_nLocalCP ) ); }
	virtual void Operate( CParticleCollection *particles, float strength, void *context ) const OVERRIDE
	{
		if ( !TerrorGapVectorField( m_nOutput ) || !TerrorGapCPValid( m_nInputCP ) || ( m_nLocalCP != -1 && !TerrorGapCPValid( m_nLocalCP ) ) ) return;
		Vector input; particles->GetControlPointAtTime( m_nInputCP, particles->m_flCurTime, &input );
		Vector outputMin = m_vecOutputMin, outputMax = m_vecOutputMax;
		if ( m_nLocalCP != -1 )
		{
			matrix3x4_t transform;
			particles->GetControlPointTransformAtTime( m_nLocalCP, particles->m_flCurTime, &transform );
			VectorRotate( m_vecOutputMin, transform, outputMin ); VectorRotate( m_vecOutputMax, transform, outputMax );
		}
		for ( int index = 0; index < particles->m_nActiveParticles; ++index )
		{
			if ( !TerrorGapLifetimeRange( *particles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_CREATION_TIME, index ), m_flStart, m_flEnd ) ) continue;
			Vector value;
			for ( int axis = 0; axis < 3; ++axis ) value[axis] = TerrorGapRemapScalar( input[axis], m_vecInputMin[axis], m_vecInputMax[axis], outputMin[axis], outputMax[axis] );
			const Vector current = TerrorGapReadVector( particles, m_nOutput, index );
			if ( m_bScaleInitial ) { value.x *= current.x; value.y *= current.y; value.z *= current.z; }
			if ( m_nOutput == PARTICLE_ATTRIBUTE_TINT_RGB )
			{
				for ( int axis = 0; axis < 3; ++axis )
				{
					const float component = value[axis];
					value[axis] = !( 1.0f > component ) ? 1.0f :
						( 0.0f > component ? 0.0f : component );
				}
				TerrorGapWriteVector( particles, m_nOutput, index, value );
				continue;
			}
			Vector previous = TerrorGapReadVector( particles, PARTICLE_ATTRIBUTE_PREV_XYZ, index );
			if ( m_bOffset ) value += current;
			if ( m_bAccelerate )
			{
				if ( m_bOffset )
				{
					previous += value;
					TerrorGapWriteVector( particles, m_nOutput, index, value * particles->m_flDt + value );
					TerrorGapWriteVector( particles, PARTICLE_ATTRIBUTE_PREV_XYZ, index, previous );
				}
				else TerrorGapWriteVector( particles, m_nOutput, index, value * particles->m_flDt );
			}
			else
			{
				if ( m_bOffset ) previous += value; else previous = value;
				TerrorGapWriteVector( particles, m_nOutput, index, value );
				TerrorGapWriteVector( particles, PARTICLE_ATTRIBUTE_PREV_XYZ, index, previous );
			}
		}
	}
private:
	int m_nInputCP, m_nOutput;
	Vector m_vecInputMin, m_vecInputMax, m_vecOutputMin, m_vecOutputMax;
	float m_flStart, m_flEnd;
	bool m_bScaleInitial, m_bOffset, m_bAccelerate;
	int m_nLocalCP;
};
DEFINE_PARTICLE_OPERATOR( C_OP_RemapCPtoVector, "Remap Control Point to Vector", OPERATOR_GENERIC );
BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_RemapCPtoVector )
	DMXELEMENT_UNPACK_FIELD( "emitter lifetime start time (seconds)", "-1", float, m_flStart )
	DMXELEMENT_UNPACK_FIELD( "emitter lifetime end time (seconds)", "-1", float, m_flEnd )
	DMXELEMENT_UNPACK_FIELD( "input control point number", "0", int, m_nInputCP )
	DMXELEMENT_UNPACK_FIELD( "input minimum", "0 0 0", Vector, m_vecInputMin )
	DMXELEMENT_UNPACK_FIELD( "input maximum", "0 0 0", Vector, m_vecInputMax )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "output field", "0", int, m_nOutput, "intchoice particlefield_vector" )
	DMXELEMENT_UNPACK_FIELD( "output minimum", "0 0 0", Vector, m_vecOutputMin )
	DMXELEMENT_UNPACK_FIELD( "output maximum", "0 0 0", Vector, m_vecOutputMax )
	DMXELEMENT_UNPACK_FIELD( "output is scalar of initial random range", "0", bool, m_bScaleInitial )
	DMXELEMENT_UNPACK_FIELD( "offset position", "0", bool, m_bOffset )
	DMXELEMENT_UNPACK_FIELD( "accelerate position", "0", bool, m_bAccelerate )
	DMXELEMENT_UNPACK_FIELD( "local space CP", "-1", int, m_nLocalCP )
END_PARTICLE_OPERATOR_UNPACK( C_OP_RemapCPtoVector )

class C_OP_RemapVelocityToVector : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_RemapVelocityToVector );
public:
	virtual uint32 GetWrittenAttributes() const OVERRIDE { return TerrorGapAttributeMask( m_nOutput ); }
	virtual uint32 GetReadAttributes() const OVERRIDE { return 3; } // Native mask bug (not PREV_XYZ).
	virtual void Operate( CParticleCollection *particles, float strength, void *context ) const OVERRIDE
	{
		if ( !TerrorGapVectorField( m_nOutput ) ) return;
		size_t positionStride, previousStride;
		const FourVectors *position = particles->Get4VAttributePtr( PARTICLE_ATTRIBUTE_XYZ, &positionStride );
		const FourVectors *previous = particles->Get4VAttributePtr( PARTICLE_ATTRIBUTE_PREV_XYZ, &previousStride );
		TerrorGapVectorWrite_t out; out.Init( particles, m_nOutput );
		const double timestep = static_cast< double >( particles->m_flPreviousDt ) < 1.0e-20 ? 1.0e-20 : particles->m_flPreviousDt;
		const fltx4 scale = ReplicateX4( m_bNormalize ? m_flScale : static_cast< float >( m_flScale / timestep ) );
		for ( int block = 0; block < particles->m_nPaddedActiveParticles; ++block, position += positionStride, previous += previousStride, out.Advance() )
		{
			FourVectors value = *position; value -= *previous;
			if ( m_bNormalize )
			{
				const fltx4 length = AddSIMD( AddSIMD( MulSIMD( value.y, value.y ), MulSIMD( value.x, value.x ) ), MulSIMD( value.z, value.z ) );
				const fltx4 estimate = _mm_rsqrt_ps( length );
				const fltx4 inverse = MulSIMD( MulSIMD( SubSIMD( ReplicateX4( 3.0f ), MulSIMD( MulSIMD( estimate, estimate ), length ) ), estimate ), ReplicateX4( 0.5f ) );
				value *= inverse; // Zero displacement remains NaN, as native.
			}
			value *= scale; out.Store( value );
		}
	}
private:
	int m_nOutput;
	float m_flScale;
	bool m_bNormalize;
};
DEFINE_PARTICLE_OPERATOR( C_OP_RemapVelocityToVector, "Remap Velocity to Vector", OPERATOR_GENERIC );
BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_RemapVelocityToVector )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "output field", "0", int, m_nOutput, "intchoice particlefield_vector" )
	DMXELEMENT_UNPACK_FIELD( "normalize", "0", bool, m_bNormalize )
	DMXELEMENT_UNPACK_FIELD( "scale factor", "1", float, m_flScale )
END_PARTICLE_OPERATOR_UNPACK( C_OP_RemapVelocityToVector )

class C_OP_RemapControlPointDirectionToVector : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_OP_RemapControlPointDirectionToVector );
public:
	virtual uint32 GetWrittenAttributes() const OVERRIDE { return TerrorGapAttributeMask( m_nOutput ); }
	virtual uint32 GetReadAttributes() const OVERRIDE { return 0; }
	virtual uint64 GetReadControlPointMask() const OVERRIDE { return TerrorGapCPMask( m_nControlPoint ); }
	virtual void Operate( CParticleCollection *particles, float strength, void *context ) const OVERRIDE
	{
		if ( !TerrorGapVectorField( m_nOutput ) || !TerrorGapCPValid( m_nControlPoint ) ) return;
		FourVectors value; value.DuplicateVector( particles->ControlPoint( TerrorGapCurrentCP( particles, m_nControlPoint ) ).m_ForwardVector * m_flScale );
		TerrorGapVectorWrite_t out; out.Init( particles, m_nOutput );
		for ( int block = 0; block < particles->m_nPaddedActiveParticles; ++block, out.Advance() ) out.Store( value );
	}
private:
	int m_nOutput;
	float m_flScale;
	int m_nControlPoint;
};
DEFINE_PARTICLE_OPERATOR( C_OP_RemapControlPointDirectionToVector, "Remap Control Point Direction to Vector", OPERATOR_GENERIC );
BEGIN_PARTICLE_OPERATOR_UNPACK( C_OP_RemapControlPointDirectionToVector )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "output field", "0", int, m_nOutput, "intchoice particlefield_vector" )
	DMXELEMENT_UNPACK_FIELD( "control point number", "0", int, m_nControlPoint )
	DMXELEMENT_UNPACK_FIELD( "scale factor", "1", float, m_flScale )
END_PARTICLE_OPERATOR_UNPACK( C_OP_RemapControlPointDirectionToVector )

#pragma float_control( pop )

void RegisterTerrorParticleGapSpeedInitializer()
{
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_RemapSpeedToScalar );
}

void RegisterTerrorParticleGapRemaps()
{
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_RemapSpeed );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_RemapCPtoVector );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_RemapVelocityToVector );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_RemapControlPointDirectionToVector );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, C_OP_RemapBoundstoCP );
}
