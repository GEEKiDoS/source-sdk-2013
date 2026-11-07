//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/effects/terror_particle_parse.h"
#include "particles/particles.h"
#include "l4d2/effects/terror_particle_attributes.h"
#include "mathlib/mathlib.h"
#include <math.h>

#include "tier0/memdbgon.h"

// These functions are registered with the native particle manager. The wire
// adapter deliberately does not rename operators or remove graph dependencies.
// RVA comments refer to the pinned PE32 L4D2 client, not target object layouts.

static void TerrorWriteParticleVector( float *pAttribute, const Vector &value )
{
	pAttribute[0] = value.x;
	pAttribute[4] = value.y;
	pAttribute[8] = value.z;
}


// C:0x39E6B0 / 0x39F2C0 use one inclusive collection-stream integer per
// particle. The SDK's block overrides instead write continuous SIMD floats;
// fractional second sequences corrupt the native two-sequence byte packet.
static void TerrorInitRandomSequence( CParticleCollection *pParticles, int nFirstParticle,
	int nParticles, int nAttribute, int nSequenceMin, int nSequenceMax )
{
	for ( int i = nFirstParticle; i < nFirstParticle + nParticles; ++i )
	{
		float *pSequence = pParticles->GetFloatAttributePtrForWrite( nAttribute, i );
		*pSequence = (float)pParticles->RandomInt( nSequenceMin, nSequenceMax );
	}
}

class CTerror_INIT_RandomSequence : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( CTerror_INIT_RandomSequence );
public:
	virtual uint32 GetWrittenAttributes() const { return PARTICLE_ATTRIBUTE_SEQUENCE_NUMBER_MASK; }
	virtual uint32 GetReadAttributes() const { return 0; }
	virtual size_t GetRequiredContextBytes() const { return sizeof( Context_t ); }
	virtual void InitializeContextData( CParticleCollection *pParticles, void *pContext ) const;
	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int nFirstParticle, int nParticles, int nAttributeWriteMask, void *pContext ) const;

private:
	struct Context_t
	{
		int m_nNextSequence;
		int m_Sequences[64];
	};

	void Shuffle( CParticleCollection *pParticles, Context_t *pContext ) const;

	int m_nSequenceMin;
	int m_nSequenceMax;
	bool m_bShuffle;
	bool m_bLinear;
};

DEFINE_PARTICLE_OPERATOR( CTerror_INIT_RandomSequence, "Sequence Random", OPERATOR_GENERIC );
BEGIN_PARTICLE_OPERATOR_UNPACK( CTerror_INIT_RandomSequence )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "sequence_min", "0", int, m_nSequenceMin, "sheetsequencepicker" )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "sequence_max", "0", int, m_nSequenceMax, "sheetsequencepicker" )
	DMXELEMENT_UNPACK_FIELD( "shuffle", "0", bool, m_bShuffle )
	DMXELEMENT_UNPACK_FIELD( "linear", "0", bool, m_bLinear )
END_PARTICLE_OPERATOR_UNPACK( CTerror_INIT_RandomSequence )

// C:0x39E5D0 initializes this fixed, per-collection table only for the two
// ordered modes. Shuffle takes precedence when both serialized flags are set.
void CTerror_INIT_RandomSequence::Shuffle( CParticleCollection *pParticles, Context_t *pContext ) const
{
	for ( int i = m_nSequenceMax; i > m_nSequenceMin; --i )
	{
		int nOther = pParticles->RandomInt( m_nSequenceMin, i );
		int nSequence = pContext->m_Sequences[i];
		pContext->m_Sequences[i] = pContext->m_Sequences[nOther];
		pContext->m_Sequences[nOther] = nSequence;
	}
}

void CTerror_INIT_RandomSequence::InitializeContextData( CParticleCollection *pParticles, void *pContext ) const
{
	if ( !m_bShuffle && !m_bLinear )
		return;

	Context_t *pCtx = static_cast< Context_t * >( pContext );
	pCtx->m_nNextSequence = m_nSequenceMin;
	for ( int i = m_nSequenceMin; i <= m_nSequenceMax; ++i )
		pCtx->m_Sequences[i] = i;
	if ( m_bShuffle )
		Shuffle( pParticles, pCtx );
}

void CTerror_INIT_RandomSequence::InitNewParticlesScalar( CParticleCollection *pParticles, int nFirstParticle, int nParticles, int nAttributeWriteMask, void *pContext ) const
{
	if ( !m_bShuffle && !m_bLinear )
	{
		TerrorInitRandomSequence( pParticles, nFirstParticle, nParticles,
			PARTICLE_ATTRIBUTE_SEQUENCE_NUMBER, m_nSequenceMin, m_nSequenceMax );
		return;
	}

	Context_t *pCtx = static_cast< Context_t * >( pContext );
	for ( int i = nFirstParticle; i < nFirstParticle + nParticles; ++i )
	{
		if ( pCtx->m_nNextSequence > m_nSequenceMax )
		{
			if ( m_bShuffle )
				Shuffle( pParticles, pCtx );
			pCtx->m_nNextSequence = m_nSequenceMin;
		}
		*pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_SEQUENCE_NUMBER, i ) =
			(float)pCtx->m_Sequences[pCtx->m_nNextSequence];
		++pCtx->m_nNextSequence;
	}
}

class CTerror_INIT_RandomSecondSequence : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( CTerror_INIT_RandomSecondSequence );
public:
	virtual uint32 GetWrittenAttributes() const { return PARTICLE_ATTRIBUTE_SEQUENCE_NUMBER1_MASK; }
	virtual uint32 GetReadAttributes() const { return 0; }
	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int nFirstParticle, int nParticles, int nAttributeWriteMask, void *pContext ) const
	{
		TerrorInitRandomSequence( pParticles, nFirstParticle, nParticles,
			PARTICLE_ATTRIBUTE_SEQUENCE_NUMBER1, m_nSequenceMin, m_nSequenceMax );
	}

private:
	int m_nSequenceMin;
	int m_nSequenceMax;
};

DEFINE_PARTICLE_OPERATOR( CTerror_INIT_RandomSecondSequence, "Sequence Two Random", OPERATOR_GENERIC );
BEGIN_PARTICLE_OPERATOR_UNPACK( CTerror_INIT_RandomSecondSequence )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "sequence_min", "0", int, m_nSequenceMin, "sheetsequencepicker_second" )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "sequence_max", "0", int, m_nSequenceMax, "sheetsequencepicker_second" )
END_PARTICLE_OPERATOR_UNPACK( CTerror_INIT_RandomSecondSequence )

// Both inherit the real base block callback, as native C:0x377DC0 does:
// dispatch to their scalar callback with four times the block start/count.
static void RegisterTerrorParticleSequenceInitializers()
{
	bool bFoundSequence = false;
	bool bFoundSecondSequence = false;
	CUtlVector< IParticleOperatorDefinition * > &initializers = g_pParticleSystemMgr->GetAvailableParticleOperatorList( FUNCTION_INITIALIZER );
	FOR_EACH_VEC( initializers, i )
	{
		if ( !Q_stricmp( initializers[i]->GetName(), "Sequence Random" ) )
		{
			initializers[i] = &s_CTerror_INIT_RandomSequenceFactory;
			bFoundSequence = true;
		}
		else if ( !Q_stricmp( initializers[i]->GetName(), "Sequence Two Random" ) )
		{
			initializers[i] = &s_CTerror_INIT_RandomSecondSequenceFactory;
			bFoundSecondSequence = true;
		}
	}
	if ( !bFoundSequence || !bFoundSecondSequence )
		Error( "Cannot register Terror sequence initializers: native sequence factories are missing.\n" );
}

class CTerror_INIT_NormalAlignToCP : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( CTerror_INIT_NormalAlignToCP );
public:
	virtual void InitParams( CParticleSystemDefinition *pDef, CDmxElement *pElement )
	{
		m_nControlPoint = clamp( m_nControlPoint, 0, MAX_PARTICLE_CONTROL_POINTS - 1 );
	}
	virtual uint32 GetWrittenAttributes() const { return TERROR_PARTICLE_NORMAL_MASK; }
	virtual uint32 GetReadAttributes() const { return 0; }
	virtual uint64 GetReadControlPointMask() const { return (uint64)1 << m_nControlPoint; }
	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int nFirstParticle, int nParticles, int nAttributeWriteMask, void *pContext ) const;
private:
	int m_nControlPoint;
};

DEFINE_PARTICLE_OPERATOR( CTerror_INIT_NormalAlignToCP, "Normal Align to CP", OPERATOR_GENERIC );
BEGIN_PARTICLE_OPERATOR_UNPACK( CTerror_INIT_NormalAlignToCP )
	DMXELEMENT_UNPACK_FIELD( "control_point_number", "0", int, m_nControlPoint )
END_PARTICLE_OPERATOR_UNPACK( CTerror_INIT_NormalAlignToCP )

// C:0x3944D0. The original does not declare creation-time as a read attribute,
// even though its body samples orientation using each particle's creation time.
void CTerror_INIT_NormalAlignToCP::InitNewParticlesScalar( CParticleCollection *pParticles, int nFirstParticle, int nParticles, int nAttributeWriteMask, void *pContext ) const
{
	for ( int i = nFirstParticle; i < nFirstParticle + nParticles; ++i )
	{
		float flCreationTime = *pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_CREATION_TIME, i );
		Vector vecForward, vecRight, vecUp;
		pParticles->GetControlPointOrientationAtTime( m_nControlPoint, flCreationTime, &vecForward, &vecRight, &vecUp );
		TerrorWriteParticleNormal( pParticles, i, vecForward );
	}
}

class CTerror_INIT_VelocityFromCP : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( CTerror_INIT_VelocityFromCP );
public:
	virtual uint32 GetWrittenAttributes() const { return PARTICLE_ATTRIBUTE_PREV_XYZ_MASK; }
	virtual uint32 GetReadAttributes() const { return PARTICLE_ATTRIBUTE_PREV_XYZ_MASK; }
	virtual uint64 GetReadControlPointMask() const
	{
		uint64 nMask = (uint64)1 << m_nControlPoint;
		if ( m_nComparisonControlPoint != -1 )
			nMask |= (uint64)1 << m_nComparisonControlPoint;
		if ( m_nLocalSpaceControlPoint != -1 )
			nMask |= (uint64)1 << m_nLocalSpaceControlPoint;
		return nMask;
	}
	virtual bool InitMultipleOverride() { return true; }
	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int nFirstParticle, int nParticles, int nAttributeWriteMask, void *pContext ) const;
private:
	int m_nControlPoint;
	int m_nComparisonControlPoint;
	int m_nLocalSpaceControlPoint;
	float m_flVelocityScale;
};

DEFINE_PARTICLE_OPERATOR( CTerror_INIT_VelocityFromCP, "Velocity Set from Control Point", OPERATOR_GENERIC );
BEGIN_PARTICLE_OPERATOR_UNPACK( CTerror_INIT_VelocityFromCP )
	DMXELEMENT_UNPACK_FIELD( "control point number", "0", int, m_nControlPoint )
	DMXELEMENT_UNPACK_FIELD( "velocity scale", "1", float, m_flVelocityScale )
	DMXELEMENT_UNPACK_FIELD( "comparison control point number", "-1", int, m_nComparisonControlPoint )
	DMXELEMENT_UNPACK_FIELD( "local space control point number", "-1", int, m_nLocalSpaceControlPoint )
END_PARTICLE_OPERATOR_UNPACK( CTerror_INIT_VelocityFromCP )

// C:0x38C260. CP position (or the difference of two CP positions) encodes a
// velocity, not control-point motion over time. No random numbers are drawn.
void CTerror_INIT_VelocityFromCP::InitNewParticlesScalar( CParticleCollection *pParticles, int nFirstParticle, int nParticles, int nAttributeWriteMask, void *pContext ) const
{
	Vector vecVelocity = pParticles->ControlPoint( m_nControlPoint ).m_Position;
	if ( m_nComparisonControlPoint > -1 )
		vecVelocity -= pParticles->ControlPoint( m_nComparisonControlPoint ).m_Position;
	if ( m_nLocalSpaceControlPoint > -1 )
	{
		matrix3x4_t matControlPoint;
		pParticles->GetControlPointTransformAtCurrentTime( m_nLocalSpaceControlPoint, &matControlPoint );
		Vector vecLocalVelocity = vecVelocity;
		VectorRotate( vecLocalVelocity, matControlPoint, vecVelocity );
	}

	// Preserve the multiplication order and previous simulation timestep.
	vecVelocity.x = m_flVelocityScale * ( pParticles->m_flPreviousDt * vecVelocity.x );
	vecVelocity.y = m_flVelocityScale * ( pParticles->m_flPreviousDt * vecVelocity.y );
	vecVelocity.z = m_flVelocityScale * ( pParticles->m_flPreviousDt * vecVelocity.z );
	for ( int i = nFirstParticle; i < nFirstParticle + nParticles; ++i )
	{
		float *pPrevious = pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_PREV_XYZ, i );
		pPrevious[0] -= vecVelocity.x;
		pPrevious[4] -= vecVelocity.y;
		pPrevious[8] -= vecVelocity.z;
	}
}

class CTerror_INIT_RingWave : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( CTerror_INIT_RingWave );
public:
	virtual void InitParams( CParticleSystemDefinition *pDef, CDmxElement *pElement );
	virtual uint32 GetWrittenAttributes() const { return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK; }
	virtual uint32 GetReadAttributes() const { return PARTICLE_ATTRIBUTE_CREATION_TIME_MASK; }
	virtual uint64 GetReadControlPointMask() const { return (uint64)1 << m_nControlPoint; }
	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int nFirstParticle, int nParticles, int nAttributeWriteMask, void *pContext ) const;
private:
	int m_nControlPoint;
	float m_flRadius;
	float m_flThickness;
	float m_flSpeedMin;
	float m_flSpeedMax;
	float m_flRoll;
	float m_flPitch;
	bool m_bEvenDistribution;
	bool m_bXYVelocityOnly;
};

DEFINE_PARTICLE_OPERATOR( CTerror_INIT_RingWave, "Position Along Ring", OPERATOR_PI_POSITION );
BEGIN_PARTICLE_OPERATOR_UNPACK( CTerror_INIT_RingWave )
	DMXELEMENT_UNPACK_FIELD( "control point number", "0", int, m_nControlPoint )
	DMXELEMENT_UNPACK_FIELD( "initial radius", "0", float, m_flRadius )
	DMXELEMENT_UNPACK_FIELD( "thickness", "0", float, m_flThickness )
	DMXELEMENT_UNPACK_FIELD( "min initial speed", "0", float, m_flSpeedMin )
	DMXELEMENT_UNPACK_FIELD( "max initial speed", "0", float, m_flSpeedMax )
	DMXELEMENT_UNPACK_FIELD( "roll", "0", float, m_flRoll )
	DMXELEMENT_UNPACK_FIELD( "pitch", "0", float, m_flPitch )
	DMXELEMENT_UNPACK_FIELD( "even distribution", "0", bool, m_bEvenDistribution )
	DMXELEMENT_UNPACK_FIELD( "XY velocity only", "1", bool, m_bXYVelocityOnly )
END_PARTICLE_OPERATOR_UNPACK( CTerror_INIT_RingWave )

// C:0x39C7D0. The original converts to radians here, but later passes these
// members to the degrees-based QAngle rotation helper. Retain that quirk.
void CTerror_INIT_RingWave::InitParams( CParticleSystemDefinition *pDef, CDmxElement *pElement )
{
	m_nControlPoint = clamp( m_nControlPoint, 0, MAX_PARTICLE_CONTROL_POINTS - 1 );
	float flSpeedMax = m_flSpeedMin;
	if ( m_flSpeedMin <= m_flSpeedMax )
		flSpeedMax = m_flSpeedMax;
	m_flSpeedMax = flSpeedMax;
	if ( m_flRadius < 0.0f )
		m_flRadius = 0.0f;
	m_flRoll = ( m_flRoll * 6.283185307179586 ) * 0.002777777777777778;
	m_flPitch = 0.002777777777777778 * ( 6.283185307179586 * m_flPitch );
}

// C:0x36AC30. The original sphere sample consumes THREE consecutive samples,
// unlike the SDK convenience overload's single query-counter increment.
static Vector TerrorRandomPointInSphere( CParticleCollection *pParticles )
{
	float flZSample = pParticles->RandomFloat( 0.0f, 1.0f );
	float flAzimuthSample = pParticles->RandomFloat( 0.0f, 1.0f );
	float flRadiusSample = pParticles->RandomFloat( 0.0f, 1.0f );
	float flPolar = acos( 1.0 - ( flZSample + flZSample ) );
	float flAzimuth = flAzimuthSample * 6.283185307179586;
	float flRadius = pow( (double)flRadiusSample * 0.99999f + 0.00001f, (double)( 1.0f / 3.0f ) );
	float flCosPolar = cos( flPolar );
	float flSinPolar = sin( flPolar );
	float flCosAzimuth = cos( flAzimuth );
	float flSinAzimuth = sin( flAzimuth );
	return Vector( ( flSinPolar * flRadius ) * flCosAzimuth,
		( flSinPolar * flRadius ) * flSinAzimuth, flCosPolar * flRadius );
}

// C:0x381BF0. In particular, random distribution still multiplies its random
// angle by (absolute particle index + 1), and even distribution uses the size
// of this initialization batch, not the total active-particle count.
void CTerror_INIT_RingWave::InitNewParticlesScalar( CParticleCollection *pParticles, int nFirstParticle, int nParticles, int nAttributeWriteMask, void *pContext ) const
{
	float flAngleStep = 6.283185307179586 / (double)nParticles;
	QAngle angles( m_flRoll, m_flPitch, 0.0f );
	for ( int i = nFirstParticle; i < nFirstParticle + nParticles; ++i )
	{
		Vector vecJitter = TerrorRandomPointInSphere( pParticles );
		float flCreationTime = *pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_CREATION_TIME, i );
		matrix3x4_t matControlPoint;
		pParticles->GetControlPointTransformAtTime( m_nControlPoint, flCreationTime, &matControlPoint );
		Vector vecCenter;
		MatrixGetColumn( matControlPoint, 3, vecCenter );
		float flStep = m_bEvenDistribution ? flAngleStep : pParticles->RandomFloat( 0.0f, 1.0f ) * 6.2831855f;
		float flAngle = ( i + 1 ) * flStep;
		float flSin = sin( flAngle );
		float flCos = cos( flAngle );
		Vector vecRing( m_flRadius * flCos, m_flRadius * flSin, m_flRadius * 0.0f );
		Vector vecLocalPosition;
		VectorRotate( vecRing, angles, vecLocalPosition );
		float flSpeed = pParticles->RandomFloat( m_flSpeedMin, m_flSpeedMax );
		vecLocalPosition += vecJitter * m_flThickness;
		Vector vecPosition;
		VectorTransform( vecLocalPosition, matControlPoint, vecPosition );
		Vector vecDirection = vecPosition - vecCenter;
		VectorNormalize( vecDirection );
		float flDisplacement = flSpeed * pParticles->m_flPreviousDt;
		Vector vecPrevious = vecPosition - vecDirection * flDisplacement;
		TerrorWriteParticleVector( pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_XYZ, i ), vecPosition );
		if ( nAttributeWriteMask & PARTICLE_ATTRIBUTE_PREV_XYZ_MASK )
		{
			if ( m_bXYVelocityOnly )
				vecPrevious.z = vecPosition.z;
			TerrorWriteParticleVector( pParticles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_PREV_XYZ, i ), vecPrevious );
		}
	}
}

class CTerror_OP_NormalLock : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( CTerror_OP_NormalLock );
public:
	virtual void InitParams( CParticleSystemDefinition *pDef, CDmxElement *pElement )
	{
		m_nControlPoint = clamp( m_nControlPoint, 0, MAX_PARTICLE_CONTROL_POINTS - 1 );
	}
	virtual uint32 GetWrittenAttributes() const { return TERROR_PARTICLE_NORMAL_MASK; }
	virtual uint32 GetReadAttributes() const { return PARTICLE_ATTRIBUTE_CREATION_TIME_MASK; }
	virtual uint64 GetReadControlPointMask() const { return (uint64)1 << m_nControlPoint; }
	virtual size_t GetRequiredContextBytes() const { return sizeof( matrix3x4_t ); }
	virtual void InitializeContextData( CParticleCollection *pParticles, void *pContext ) const
	{
		pParticles->GetControlPointTransformAtTime( m_nControlPoint, pParticles->m_flCurTime, static_cast< matrix3x4_t * >( pContext ) );
	}
	virtual void Operate( CParticleCollection *pParticles, float flOpStrength, void *pContext ) const;
private:
	int m_nControlPoint;
};

DEFINE_PARTICLE_OPERATOR( CTerror_OP_NormalLock, "Normal Lock to Control Point", OPERATOR_GENERIC );
BEGIN_PARTICLE_OPERATOR_UNPACK( CTerror_OP_NormalLock )
	DMXELEMENT_UNPACK_FIELD( "control_point_number", "0", int, m_nControlPoint )
END_PARTICLE_OPERATOR_UNPACK( CTerror_OP_NormalLock )

// C:0x3B0F40. Blend only the fraction of this frame for which a newly emitted
// particle existed. The original does not normalize or strength-scale normals.
void CTerror_OP_NormalLock::Operate( CParticleCollection *pParticles, float flOpStrength, void *pContext ) const
{
	matrix3x4_t matCurrent, matPreviousInverse, matDelta;
	pParticles->GetControlPointTransformAtTime( m_nControlPoint, pParticles->m_flCurTime, &matCurrent );
	matrix3x4_t *pPrevious = static_cast< matrix3x4_t * >( pContext );
	MatrixInvert( *pPrevious, matPreviousInverse );
	ConcatTransforms( matCurrent, matPreviousInverse, matDelta );
	for ( int i = 0; i < pParticles->m_nActiveParticles; ++i )
	{
		float flCreationTime = *pParticles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_CREATION_TIME, i );
		float flAge = MIN( pParticles->m_flCurTime - flCreationTime, pParticles->m_flDt );
		float flFraction = ( 1.0f / pParticles->m_flDt ) * flAge;
		Vector vecNormal = TerrorReadParticleNormal( pParticles, i );
		Vector vecRotated;
		VectorRotate( vecNormal, matDelta, vecRotated );
		vecRotated.x = ( vecRotated.x - vecNormal.x ) * flFraction + vecNormal.x;
		vecRotated.y = ( vecRotated.y - vecNormal.y ) * flFraction + vecNormal.y;
		vecRotated.z = ( vecRotated.z - vecNormal.z ) * flFraction + vecNormal.z;
		TerrorWriteParticleNormal( pParticles, i, vecRotated );
	}
	MatrixCopy( matCurrent, *pPrevious );
}

class CTerror_OP_SetControlPointToImpactPoint : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( CTerror_OP_SetControlPointToImpactPoint );
public:
	virtual void InitParams( CParticleSystemDefinition *pDef, CDmxElement *pElement )
	{
		m_nCollisionGroup = g_pParticleSystemMgr->Query()->GetCollisionGroupFromName( m_szCollisionGroup );
		m_nTraceControlPoint = clamp( m_nTraceControlPoint, 0, MAX_PARTICLE_CONTROL_POINTS - 1 );
		m_nOutputControlPoint = clamp( m_nOutputControlPoint, 0, MAX_PARTICLE_CONTROL_POINTS - 1 );
	}
	virtual uint32 GetWrittenAttributes() const { return 0; }
	virtual uint32 GetReadAttributes() const { return 0; }
	// The original reports no CP reads despite reading its trace source.
	virtual uint64 GetReadControlPointMask() const { return 0; }
	virtual bool ShouldRunBeforeEmitters() const { return true; }
	virtual size_t GetRequiredContextBytes() const { return sizeof( float ); }
	virtual void InitializeContextData( CParticleCollection *pParticles, void *pContext ) const
	{
		*static_cast< float * >( pContext ) = -m_flTraceUpdateRate;
	}
	virtual void Operate( CParticleCollection *pParticles, float flOpStrength, void *pContext ) const;
private:
	int m_nOutputControlPoint;
	int m_nTraceControlPoint;
	int m_nCollisionGroup;
	float m_flTraceUpdateRate;
	float m_flTraceLength;
	Vector m_vecDirection;
	char m_szCollisionGroup[128];
};

DEFINE_PARTICLE_OPERATOR( CTerror_OP_SetControlPointToImpactPoint, "Set Control Point to Impact Point", OPERATOR_GENERIC );
BEGIN_PARTICLE_OPERATOR_UNPACK( CTerror_OP_SetControlPointToImpactPoint )
	DMXELEMENT_UNPACK_FIELD( "Control Point to Set", "1", int, m_nOutputControlPoint )
	DMXELEMENT_UNPACK_FIELD( "Control Point to Trace From", "1", int, m_nTraceControlPoint )
	DMXELEMENT_UNPACK_FIELD( "Trace Direction Override", "0 0 0", Vector, m_vecDirection )
	DMXELEMENT_UNPACK_FIELD( "Trace Update Rate", "0.5", float, m_flTraceUpdateRate )
	DMXELEMENT_UNPACK_FIELD( "Max Trace Length", "1024", float, m_flTraceLength )
	DMXELEMENT_UNPACK_FIELD_STRING( "trace collision group", "NONE", m_szCollisionGroup )
END_PARTICLE_OPERATOR_UNPACK( CTerror_OP_SetControlPointToImpactPoint )

// C:0x3B89A0. This is a periodically sampled line trace, not particle collision
// or an every-frame ground snap. Only the CP forward vector becomes the normal.
void CTerror_OP_SetControlPointToImpactPoint::Operate( CParticleCollection *pParticles, float flOpStrength, void *pContext ) const
{
	float *pNextTrace = static_cast< float * >( pContext );
	if ( pParticles->m_flCurTime < *pNextTrace )
		return;
	const CParticleControlPoint &source = pParticles->ControlPoint( m_nTraceControlPoint );
	Vector vecDirection = m_vecDirection;
	if ( vecDirection == vec3_origin )
		vecDirection = source.m_ForwardVector;
	Vector vecStart = source.m_Position;
	Vector vecEnd = vecStart + vecDirection * m_flTraceLength;
	CBaseTrace trace;
	g_pParticleSystemMgr->Query()->TraceLine( vecStart, vecEnd, MASK_ALL, NULL, m_nCollisionGroup, &trace );
	pParticles->SetControlPoint( m_nOutputControlPoint, trace.endpos );
	pParticles->SetControlPointForwardVector( m_nOutputControlPoint, trace.plane.normal );
	*pNextTrace = m_flTraceUpdateRate + pParticles->m_flCurTime;
}

void RegisterTerrorParticleColorInitializer();
void RegisterTerrorParticleGapForces();
void RegisterTerrorParticleGapInitializers();
void RegisterTerrorParticleGapRemaps();

void RegisterTerrorParticleOperators()
{
	// Native registrations live for the DLL lifetime, including dictionary
	// recreation at map change. Never append duplicate factories on reload.
	static bool s_bRegistered = false;
	if ( s_bRegistered )
		return;
	s_bRegistered = true;
	RegisterTerrorParticleColorInitializer();
	RegisterTerrorParticleSequenceInitializers();
	RegisterTerrorParticleGapForces();
	RegisterTerrorParticleGapInitializers();
	RegisterTerrorParticleGapRemaps();
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, CTerror_INIT_NormalAlignToCP );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, CTerror_OP_NormalLock );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, CTerror_INIT_VelocityFromCP );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, CTerror_INIT_RingWave );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_OPERATOR, CTerror_OP_SetControlPointToImpactPoint );
}
