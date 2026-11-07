//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/effects/terror_particle_gap_helpers.h"
#include "l4d2/terror_physics_math.h"
#include "c_effects.h"
#include "raytrace.h"
#include <math.h>
#include <limits.h>
#include <float.h>
#include <stddef.h>
#include "tier0/memalloc.h"

#include "tier0/memdbgon.h"

#pragma float_control( precise, on, push )

class C_INIT_PositionPlaceOnGround : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_PositionPlaceOnGround );
public:
	virtual void InitParams( CParticleSystemDefinition *definition, CDmxElement *element ) OVERRIDE
	{
		m_nCollisionGroup = g_pParticleSystemMgr->Query()->GetCollisionGroupFromName( m_szCollisionGroup );
		m_nTraceMask = m_bIncludeWater ? 100679739u : 100679691u;
	}
	virtual uint32 GetWrittenAttributes() const OVERRIDE { return 7; }
	virtual uint32 GetReadAttributes() const OVERRIDE { return 5; }
	virtual bool InitMultipleOverride() OVERRIDE { return true; }
	virtual void InitNewParticlesScalar( CParticleCollection *particles, int first, int count, int writeMask, void *context ) const OVERRIDE
	{
		for ( int index = first; index < first + count; ++index )
		{
			Vector position = TerrorGapReadVector( particles, PARTICLE_ATTRIBUTE_XYZ, index );
			Vector end( position.x + m_flTraceLength * 0.0f, position.y + m_flTraceLength * 0.0f, position.z + m_flTraceLength * -1.0f );
			CBaseTrace trace;
			g_pParticleSystemMgr->Query()->TraceLine( position, end, m_nTraceMask, NULL, m_nCollisionGroup, &trace );
			if ( trace.fraction == 1.0f && m_bKillOnMiss )
				*particles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_LIFE_DURATION, index ) = -1.0f;
			else
			{
				float *out = particles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_XYZ, index );
				float *previous = particles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_PREV_XYZ, index );
				const float delta = m_flTraceLength * trace.fraction - m_flOffset;
				out[8] -= delta; previous[8] -= delta;
			}
		}
	}
private:
	float m_flOffset, m_flTraceLength;
	char m_szCollisionGroup[128];
	int m_nCollisionGroup;
	unsigned int m_nTraceMask;
	bool m_bKillOnMiss, m_bIncludeWater;
};
DEFINE_PARTICLE_OPERATOR( C_INIT_PositionPlaceOnGround, "Position Modify Place On Ground", OPERATOR_GENERIC );
BEGIN_PARTICLE_OPERATOR_UNPACK( C_INIT_PositionPlaceOnGround )
	DMXELEMENT_UNPACK_FIELD( "offset", "0", float, m_flOffset )
	DMXELEMENT_UNPACK_FIELD( "kill on no collision", "0", bool, m_bKillOnMiss )
	DMXELEMENT_UNPACK_FIELD( "include water", "0", bool, m_bIncludeWater )
	DMXELEMENT_UNPACK_FIELD( "max trace length", "128", float, m_flTraceLength )
	DMXELEMENT_UNPACK_FIELD_STRING( "collision group", "NONE", m_szCollisionGroup )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_PositionPlaceOnGround )

class C_INIT_ColorLitPerParticle : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_ColorLitPerParticle );
public:
	virtual uint32 GetWrittenAttributes() const OVERRIDE { return PARTICLE_ATTRIBUTE_TINT_RGB_MASK; }
	virtual uint32 GetReadAttributes() const OVERRIDE { return PARTICLE_ATTRIBUTE_XYZ_MASK; }
	virtual void InitNewParticlesScalar( CParticleCollection *particles, int first, int count, int writeMask, void *context ) const OVERRIDE
	{
		Color lighting( 255, 255, 255, 255 );
		for ( int index = first; index < first + count; ++index )
		{
			g_pParticleSystemMgr->Query()->GetLightingAtPoint( TerrorGapReadVector( particles, PARTICLE_ATTRIBUTE_XYZ, index ), lighting );
			const float random = particles->RandomFloat( 0.0f, 1.0f );
			float *out = particles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_TINT_RGB, index );
			for ( int axis = 0; axis < 3; ++axis )
			{
				const unsigned char lit = MAX( m_ClampMin[axis], MIN( lighting[axis], m_ClampMax[axis] ) );
				const unsigned char color = static_cast< int >( static_cast< float >( m_Color2[axis] - m_Color1[axis] ) * random + m_Color1[axis] );
				unsigned char blend = lit;
				if ( m_nBlendMode == 1 )
					blend = lit >= 128 ? 2 * ( color - 255 ) * ( 255 - lit ) / 255 - 1 : 2 * color * lit / 255;
				else if ( m_nBlendMode == 2 ) blend = MIN( lit, color );
				else if ( m_nBlendMode == 3 ) blend = MAX( lit, color );
				out[axis * 4] = ( ( static_cast< float >( blend ) - color ) * m_flLightBias + color ) * 0.0039215689f;
			}
		}
	}
private:
	Color m_Color1, m_Color2, m_ClampMin, m_ClampMax;
	float m_flLightBias;
	int m_nBlendMode;
};
DEFINE_PARTICLE_OPERATOR( C_INIT_ColorLitPerParticle, "Color Lit Per Particle", OPERATOR_PI_TINT_RGB );
BEGIN_PARTICLE_OPERATOR_UNPACK( C_INIT_ColorLitPerParticle )
	DMXELEMENT_UNPACK_FIELD( "color1", "255 255 255 255", Color, m_Color1 )
	DMXELEMENT_UNPACK_FIELD( "color2", "255 255 255 255", Color, m_Color2 )
	DMXELEMENT_UNPACK_FIELD( "light bias", "0.0", float, m_flLightBias )
	DMXELEMENT_UNPACK_FIELD( "tint clamp min", "0 0 0 0", Color, m_ClampMin )
	DMXELEMENT_UNPACK_FIELD( "tint clamp max", "255 255 255 255", Color, m_ClampMax )
	DMXELEMENT_UNPACK_FIELD( "tint blend mode", "0", int, m_nBlendMode )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_ColorLitPerParticle )

class C_INIT_ChaoticAttractor : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_ChaoticAttractor );
public:
	virtual uint32 GetWrittenAttributes() const OVERRIDE { return 5; }
	virtual uint32 GetReadAttributes() const OVERRIDE { return 0; }
	virtual uint64 GetReadControlPointMask() const OVERRIDE { return TerrorGapCPMask( m_nControlPoint ); }
	virtual size_t GetRequiredContextBytes() const OVERRIDE { return sizeof( Vector ); }
	virtual void InitializeContextData( CParticleCollection *particles, void *context ) const OVERRIDE { static_cast< Vector * >( context )->Init(); }
	virtual void InitNewParticlesScalar( CParticleCollection *particles, int first, int count, int writeMask, void *context ) const OVERRIDE
	{
		if ( !TerrorGapCPValid( m_nControlPoint ) ) return;
		const CParticleControlPoint &point = particles->ControlPoint( TerrorGapCurrentCP( particles, m_nControlPoint ) );
		Vector &state = *static_cast< Vector * >( context );
		for ( int index = first; index < first + count; ++index )
		{
			const float x = state.x * m_flScale, y = m_flScale * state.y, z = m_flScale * state.z;
			Vector position;
			for ( int axis = 0; axis < 3; ++axis )
				position[axis] = ( ( point.m_RightVector[axis] * x + point.m_Position[axis] ) + point.m_ForwardVector[axis] * y ) + point.m_UpVector[axis] * z;
			TerrorGapWriteVector( particles, PARTICLE_ATTRIBUTE_XYZ, index, position );
			Vector next;
			next.x = sin( static_cast< double >( state.y ) * m_flA ) - cos( static_cast< double >( state.x ) * m_flB ) * state.z;
			next.y = state.z * sin( static_cast< double >( state.x ) * m_flC ) - cos( static_cast< double >( state.y ) * m_flD );
			next.z = sin( static_cast< double >( state.x ) );
			Vector previous = position;
			if ( fabs( static_cast< double >( m_flSpeedMax ) ) > 0.0 )
			{
				Vector direction = next - state;
				if ( !m_bUniformSpeed )
				{
					const float length = sqrtf( ( direction.y * direction.y + direction.z * direction.z ) + direction.x * direction.x );
					if ( fabs( static_cast< double >( length ) ) > 0.0 )
					{
						const double inverse = 1.0 / length;
						direction.x = direction.x * inverse; direction.y = direction.y * inverse; direction.z = direction.z * inverse;
					}
				}
				// Native uses integer RNG on the two serialized FLOAT endpoints.
				// C3911C4/C3911C9: CVTTSS2SI32 returns INT_MIN on invalid conversion.
				const int speedMin = _mm_cvttss_si32( _mm_set_ss( m_flSpeedMin ) ), speedMax = _mm_cvttss_si32( _mm_set_ss( m_flSpeedMax ) );
				const int64 range = static_cast< int64 >( speedMax ) + 1 - speedMin;
				if ( range < INT_MIN || range > INT_MAX ) { state = next; continue; }
				const float speed = particles->RandomInt( speedMin, speedMax );
				previous.x = particles->m_flPreviousDt * ( speed * direction.x ) + position.x;
				previous.y = ( direction.y * speed ) * particles->m_flPreviousDt + position.y;
				previous.z = ( speed * direction.z ) * particles->m_flPreviousDt + position.z;
			}
			TerrorGapWriteVector( particles, PARTICLE_ATTRIBUTE_PREV_XYZ, index, previous );
			state = next;
		}
	}
private:
	float m_flA, m_flB, m_flC, m_flD, m_flScale, m_flSpeedMin, m_flSpeedMax;
	int m_nControlPoint;
	bool m_bUniformSpeed;
};
DEFINE_PARTICLE_OPERATOR( C_INIT_ChaoticAttractor, "Position From Chaotic Attractor", OPERATOR_PI_POSITION );
BEGIN_PARTICLE_OPERATOR_UNPACK( C_INIT_ChaoticAttractor )
	DMXELEMENT_UNPACK_FIELD( "Pickover A Parameter", "-0.9629629", float, m_flA )
	DMXELEMENT_UNPACK_FIELD( "Pickover B Parameter", "2.791139", float, m_flB )
	DMXELEMENT_UNPACK_FIELD( "Pickover C Parameter", "1.85185185", float, m_flC )
	DMXELEMENT_UNPACK_FIELD( "Pickover D Parameter", "1.5", float, m_flD )
	DMXELEMENT_UNPACK_FIELD( "Speed Min", "0", float, m_flSpeedMin )
	DMXELEMENT_UNPACK_FIELD( "Speed Max", "0", float, m_flSpeedMax )
	DMXELEMENT_UNPACK_FIELD( "Uniform speed", "0", bool, m_bUniformSpeed )
	DMXELEMENT_UNPACK_FIELD( "Relative Control point number", "0", int, m_nControlPoint )
	DMXELEMENT_UNPACK_FIELD( "Scale", "1", float, m_flScale )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_ChaoticAttractor )

class C_INIT_ModelCull : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_ModelCull );
public:
	virtual void InitParams( CParticleSystemDefinition *definition, CDmxElement *element ) OVERRIDE { m_nControlPoint = clamp( m_nControlPoint, 0, 63 ); }
	virtual uint32 GetWrittenAttributes() const OVERRIDE { return 1; } // Native metadata bug: lifetime writes, XYZ declared.
	virtual uint32 GetReadAttributes() const OVERRIDE { return 1; }
	virtual uint64 GetReadControlPointMask() const OVERRIDE { return TerrorGapCPMask( m_nControlPoint ); }
	virtual bool InitMultipleOverride() OVERRIDE { return true; }
	virtual void InitNewParticlesScalar( CParticleCollection *particles, int first, int count, int writeMask, void *context ) const OVERRIDE
	{
		particles->UpdateHitBoxInfo( m_nControlPoint );
		const CModelHitBoxesInfo &boxes = particles->m_ControlPointHitBoxes[TerrorGapCurrentCP( particles, m_nControlPoint )];
		if ( !boxes.m_nNumHitBoxes || boxes.m_nNumPrevHitBoxes != boxes.m_nNumHitBoxes ) return;
		for ( int index = first; index < first + count; ++index )
		{
			const bool inside = g_pParticleSystemMgr->Query()->IsPointInControllingObjectHitBox( particles, m_nControlPoint, TerrorGapReadVector( particles, PARTICLE_ATTRIBUTE_XYZ, index ), m_bBoundsOnly );
			if ( inside != m_bCullOutside ) *particles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_LIFE_DURATION, index ) = -1.0f;
		}
	}
private:
	int m_nControlPoint;
	bool m_bBoundsOnly, m_bCullOutside;
};
DEFINE_PARTICLE_OPERATOR( C_INIT_ModelCull, "Cull relative to model", OPERATOR_GENERIC );
BEGIN_PARTICLE_OPERATOR_UNPACK( C_INIT_ModelCull )
	DMXELEMENT_UNPACK_FIELD( "control_point_number", "0", int, m_nControlPoint )
	DMXELEMENT_UNPACK_FIELD( "use only bounding box", "0", bool, m_bBoundsOnly )
	DMXELEMENT_UNPACK_FIELD( "cull outside instead of inside", "0", bool, m_bCullOutside )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_ModelCull )

class C_INIT_RtEnvCull : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_RtEnvCull );
public:
	virtual void InitParams( CParticleSystemDefinition *definition, CDmxElement *element ) OVERRIDE
	{
		m_nEnvironment = GetPrecipitationRayEnvironmentIndex( m_szEnvironment );
	}
	// C39F920/930: native declares XYZ despite writing the always-present life field.
	virtual uint32 GetWrittenAttributes() const OVERRIDE { return PARTICLE_ATTRIBUTE_XYZ_MASK; }
	virtual uint32 GetReadAttributes() const OVERRIDE { return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK; }
	virtual bool InitMultipleOverride() OVERRIDE { return true; }
	virtual void InitNewParticlesScalar( CParticleCollection *particles, int first, int count, int writeMask, void *context ) const OVERRIDE;
	virtual void InitNewParticlesBlock( CParticleCollection *particles, int firstBlock, int count, int writeMask, void *context ) const OVERRIDE;

private:
	Vector m_vecDirection, m_vecCullNormal;
	int m_nEnvironment;
	bool m_bUseVelocity, m_bCullOnMiss, m_bAdjustLifespan;
	char m_szEnvironment[128];
};
DEFINE_PARTICLE_OPERATOR( C_INIT_RtEnvCull, "Cull relative to Ray Trace Environment", OPERATOR_GENERIC );
BEGIN_PARTICLE_OPERATOR_UNPACK( C_INIT_RtEnvCull )
	DMXELEMENT_UNPACK_FIELD( "cull on miss", "0", bool, m_bCullOnMiss )
	DMXELEMENT_UNPACK_FIELD( "velocity test adjust lifespan", "0", bool, m_bAdjustLifespan )
	DMXELEMENT_UNPACK_FIELD( "use velocity for test direction", "0", bool, m_bUseVelocity )
	DMXELEMENT_UNPACK_FIELD( "test direction", "0 0 1", Vector, m_vecDirection )
	DMXELEMENT_UNPACK_FIELD( "cull normal", "0 0 0", Vector, m_vecCullNormal )
	DMXELEMENT_UNPACK_FIELD_STRING( "ray trace environment name", "PRECIPITATION", m_szEnvironment )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_RtEnvCull )

// C39AFF0: scalar heads/tails deliberately retain native's unrefined RSQRT,
// normal-filter and lifespan-adjust behavior, which differs from full blocks.
void C_INIT_RtEnvCull::InitNewParticlesScalar( CParticleCollection *particles, int first,
	int count, int writeMask, void *context ) const
{
	const bool testNormal = m_vecCullNormal.x != 0.0f || m_vecCullNormal.y != 0.0f || m_vecCullNormal.z != 0.0f;
	for ( int index = first; index < first + count; ++index )
	{
		float *life = particles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_LIFE_DURATION, index );
		if ( *life == -1.0f )
			continue;
		FourRays rays;
		rays.origin.DuplicateVector( TerrorGapReadVector( particles, PARTICLE_ATTRIBUTE_XYZ, index ) );
		fltx4 speed = Four_Zeros;
		if ( m_bUseVelocity )
		{
			FourVectors delta = rays.origin;
			FourVectors previous;
			previous.DuplicateVector( TerrorGapReadVector( particles, PARTICLE_ATTRIBUTE_PREV_XYZ, index ) );
			delta -= previous;
			const fltx4 lengthSquared = AddSIMD( AddSIMD( MulSIMD( delta.x, delta.x ), MulSIMD( delta.y, delta.y ) ), MulSIMD( delta.z, delta.z ) );
			speed = DivSIMD( SqrtSIMD( lengthSquared ), ReplicateX4( particles->m_flPreviousDt ) );
			const fltx4 inverse = _mm_rsqrt_ps( lengthSquared );
			rays.direction.x = MulSIMD( delta.x, inverse );
			rays.direction.y = MulSIMD( delta.y, inverse );
			rays.direction.z = MulSIMD( delta.z, inverse );
		}
		else
			rays.direction.DuplicateVector( m_vecDirection );

		RayTracingResult result;
		const fltx4 maximum = m_bUseVelocity ? MulSIMD( ReplicateX4( *life ), speed ) : ReplicateX4( FLT_MAX );
		if ( !TracePrecipitationRayEnvironment( m_nEnvironment, rays, Four_Zeros, maximum, &result ) )
			continue;
		const bool normalMatches = SubFloat( result.surface_normal.x, 0 ) == m_vecCullNormal.x &&
			SubFloat( result.surface_normal.y, 0 ) == m_vecCullNormal.y && SubFloat( result.surface_normal.z, 0 ) == m_vecCullNormal.z;
		const bool hit = result.HitIds[0] != -1 && ( !testNormal || normalMatches );
		if ( m_bUseVelocity && m_bAdjustLifespan && hit )
		{
			const float duration = SubFloat( DivSIMD( result.HitDistance, AddSIMD( speed, ReplicateX4( FLT_EPSILON ) ) ), 0 );
			if ( *life > duration )
				*life = duration;
		}
		else if ( hit ? ( !m_bCullOnMiss || ( !m_bUseVelocity && testNormal ) ) : m_bCullOnMiss )
		{
			*life = -1.0f;
		}
	}
}

// C393A30: trace four particles together, including the native per-block
// lifespan-adjust mask. No per-particle allocation or world/sky-trace surrogate.
void C_INIT_RtEnvCull::InitNewParticlesBlock( CParticleCollection *particles, int firstBlock,
	int count, int writeMask, void *context ) const
{
	const bool testNormal = m_vecCullNormal.x != 0.0f || m_vecCullNormal.y != 0.0f || m_vecCullNormal.z != 0.0f;
	const fltx4 dead = ReplicateX4( -1.0f );
	size_t positionStride, previousStride, lifeStride;
	const FourVectors *position = particles->Get4VAttributePtr( PARTICLE_ATTRIBUTE_XYZ, &positionStride );
	const FourVectors *previous = particles->Get4VAttributePtr( PARTICLE_ATTRIBUTE_PREV_XYZ, &previousStride );
	fltx4 *life = particles->GetM128AttributePtrForWrite( PARTICLE_ATTRIBUTE_LIFE_DURATION, &lifeStride );
	position += firstBlock * positionStride;
	previous += firstBlock * previousStride;
	life += firstBlock * lifeStride;
	// Native uses each vector's stride for its initial offset, but the life
	// stride for both subsequent vector advances (including constant streams).
	for ( int block = 0; block < count; ++block, position += lifeStride, previous += lifeStride, life += lifeStride )
	{
		const fltx4 wasDead = _mm_cmpeq_ps( *life, dead );
		if ( _mm_movemask_ps( wasDead ) == 15 )
			continue;
		FourRays rays;
		rays.origin = *position;
		fltx4 speed = Four_Zeros;
		if ( m_bUseVelocity )
		{
			FourVectors delta = *position;
			delta -= *previous;
			const fltx4 lengthSquared = AddSIMD( AddSIMD( MulSIMD( delta.x, delta.x ), MulSIMD( delta.y, delta.y ) ), MulSIMD( delta.z, delta.z ) );
			speed = DivSIMD( SqrtSIMD( lengthSquared ), ReplicateX4( particles->m_flPreviousDt ) );
			const fltx4 estimate = _mm_rsqrt_ps( lengthSquared );
			const fltx4 inverse = MulSIMD( MulSIMD( SubSIMD( ReplicateX4( 3.0f ), MulSIMD( MulSIMD( estimate, estimate ), lengthSquared ) ), estimate ), ReplicateX4( 0.5f ) );
			rays.direction.x = MulSIMD( delta.x, inverse );
			rays.direction.y = MulSIMD( delta.y, inverse );
			rays.direction.z = MulSIMD( delta.z, inverse );
		}
		else
			rays.direction.DuplicateVector( m_vecDirection );

		RayTracingResult result;
		const fltx4 maximum = m_bUseVelocity ? MulSIMD( *life, speed ) : ReplicateX4( FLT_MAX );
		if ( !TracePrecipitationRayEnvironment( m_nEnvironment, rays, Four_Zeros, maximum, &result ) )
			continue;
		const fltx4 hitIds = _mm_cvtepi32_ps( _mm_load_si128( reinterpret_cast< const __m128i * >( result.HitIds ) ) );
		fltx4 cull = m_bCullOnMiss ? _mm_cmplt_ps( hitIds, Four_Zeros ) : _mm_cmple_ps( Four_Zeros, hitIds );
		if ( testNormal )
		{
			const fltx4 normalMatches = _mm_and_ps( _mm_and_ps(
				_mm_cmpeq_ps( result.surface_normal.x, ReplicateX4( m_vecCullNormal.x ) ),
				_mm_cmpeq_ps( result.surface_normal.y, ReplicateX4( m_vecCullNormal.y ) ) ),
				_mm_cmpeq_ps( result.surface_normal.z, ReplicateX4( m_vecCullNormal.z ) ) );
			cull = m_bCullOnMiss ? _mm_or_ps( cull, normalMatches ) : _mm_and_ps( cull, normalMatches );
		}
		fltx4 replacement = dead;
		if ( m_bUseVelocity && m_bAdjustLifespan )
		{
			replacement = *life;
			if ( _mm_movemask_ps( cull ) )
			{
				const fltx4 duration = DivSIMD( result.HitDistance, AddSIMD( speed, ReplicateX4( FLT_EPSILON ) ) );
				replacement = _mm_or_ps( _mm_andnot_ps( wasDead, duration ), _mm_and_ps( wasDead, dead ) );
			}
		}
		const fltx4 selected = _mm_or_ps( _mm_andnot_ps( cull, *life ), _mm_and_ps( cull, replacement ) );
		*life = m_bUseVelocity ? _mm_min_ps( *life, selected ) : selected;
	}
}

namespace
{
enum
{
	IMPACT_TRACE_CACHE_MODE = 2,
	IMPACT_TRACE_MAX_POINTS = 41
};

// The native and SDK particle collections share this exact cache transport.
// Mode 2 belongs to the impact-lifetime initializer; the parent owns its cache
// and the prebuilt collection destructor frees it with MemAlloc_FreeAligned.
// CAlignedNewDelete uses the same aligned allocator as the SDK cache class.
struct ALIGN16 TerrorImpactTraceCache_t : public CAlignedNewDelete<16>
{
	FourVectors m_TraceStartPoint[IMPACT_TRACE_MAX_POINTS];
	FourVectors m_TraceEndPoint[IMPACT_TRACE_MAX_POINTS];
	FourVectors m_PointOnPlane[IMPACT_TRACE_MAX_POINTS];
	FourVectors m_PlaneNormal[IMPACT_TRACE_MAX_POINTS];
	int m_nActivePlanes;
	int m_nNextPlane;
	float m_flLastUpdateTime;
	Vector m_vecLastUpdateOrigin;
	bool m_bPlaneActive[IMPACT_TRACE_MAX_POINTS];
};
static_assert( sizeof( TerrorImpactTraceCache_t ) == 7952, "Particle collision-cache ABI" );
static_assert( offsetof( TerrorImpactTraceCache_t, m_TraceEndPoint ) == 1968, "Trace endpoint offset" );
static_assert( offsetof( TerrorImpactTraceCache_t, m_PointOnPlane ) == 3936, "Trace hit-point offset" );
static_assert( offsetof( TerrorImpactTraceCache_t, m_PlaneNormal ) == 5904, "Trace normal offset" );
static_assert( offsetof( TerrorImpactTraceCache_t, m_nActivePlanes ) == 7872, "Trace count offset" );
static_assert( offsetof( TerrorImpactTraceCache_t, m_nNextPlane ) == 7876, "Trace cursor offset" );
static_assert( offsetof( TerrorImpactTraceCache_t, m_bPlaneActive ) == 7896, "Trace activity offset" );
static_assert( IMPACT_TRACE_CACHE_MODE < NUM_COLLISION_CACHE_MODES, "Impact cache slot" );

static TerrorImpactTraceCache_t &GetImpactTraceCache( CParticleCollection *particles )
{
	CParticleCollection *owner = particles->m_pParent ? particles->m_pParent : particles;
	CWorldCollideContextData *&slot = owner->m_pCollisionCacheData[IMPACT_TRACE_CACHE_MODE];
	if ( !slot )
	{
		TerrorImpactTraceCache_t *cache = new TerrorImpactTraceCache_t;
		cache->m_nActivePlanes = 0;
		cache->m_nNextPlane = 0;
		slot = reinterpret_cast< CWorldCollideContextData * >( cache );
	}
	return *reinterpret_cast< TerrorImpactTraceCache_t * >( slot );
}

// C399FF0/C39A7A0 compare BOTH endpoints to the cached endpoints. The SDK's
// closest-point-on-segment metric can reuse a different surface behind a roof.
static fltx4 ImpactTraceEndpointError( const TerrorImpactTraceCache_t &cache, int plane,
	const FourVectors &start, const FourVectors &end )
{
	FourVectors startDelta = cache.m_TraceStartPoint[plane];
	FourVectors endDelta = cache.m_TraceEndPoint[plane];
	startDelta -= start;
	endDelta -= end;
	const fltx4 startError = AddSIMD( AddSIMD( MulSIMD( startDelta.z, startDelta.z ),
		MulSIMD( startDelta.y, startDelta.y ) ), MulSIMD( startDelta.x, startDelta.x ) );
	const fltx4 endError = AddSIMD( AddSIMD( MulSIMD( endDelta.z, endDelta.z ),
		MulSIMD( endDelta.y, endDelta.y ) ), MulSIMD( endDelta.x, endDelta.x ) );
	return MaxSIMD( startError, endError );
}
}

class CTerror_INIT_LifespanFromVelocity : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( CTerror_INIT_LifespanFromVelocity );
public:
	virtual void InitParams( CParticleSystemDefinition *definition, CDmxElement *element ) OVERRIDE
	{
		m_nCollisionGroup = g_pParticleSystemMgr->Query()->GetCollisionGroupFromName( m_szCollisionGroup );
		m_nAllowedPlanes = MIN( m_nMaxPlanes, IMPACT_TRACE_MAX_POINTS ) - 1;
		m_nTraceMask = m_bCollideWithWater ? 100679739u : 100679691u;
	}
	virtual uint32 GetWrittenAttributes() const OVERRIDE { return PARTICLE_ATTRIBUTE_LIFE_DURATION_MASK; }
	virtual uint32 GetReadAttributes() const OVERRIDE
	{
		return PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_PREV_XYZ_MASK | PARTICLE_ATTRIBUTE_CREATION_TIME_MASK;
	}
	virtual size_t GetRequiredContextBytes() const OVERRIDE { return sizeof( TerrorImpactTraceCache_t ); }
	virtual bool InitMultipleOverride() OVERRIDE { return true; }
	virtual void InitNewParticlesScalar( CParticleCollection *particles, int first, int count, int writeMask, void *context ) const OVERRIDE;
	virtual void InitNewParticlesBlock( CParticleCollection *particles, int firstBlock, int count, int writeMask, void *context ) const OVERRIDE;

private:
	Vector TraceAndCache( TerrorImpactTraceCache_t &cache, const Vector &start, const Vector &end,
		float effectiveLength, float minimumFraction ) const;

	Vector m_vecComponentScale;
	float m_flTraceOffset, m_flMaxTraceLength, m_flTraceTolerance;
	int m_nCollisionGroup, m_nMaxPlanes, m_nAllowedPlanes;
	char m_szCollisionGroup[128];
	unsigned int m_nTraceMask;
	bool m_bCollideWithWater;
};
DEFINE_PARTICLE_OPERATOR( CTerror_INIT_LifespanFromVelocity, "Lifetime from Time to Impact", OPERATOR_GENERIC );
BEGIN_PARTICLE_OPERATOR_UNPACK( CTerror_INIT_LifespanFromVelocity )
	DMXELEMENT_UNPACK_FIELD_STRING( "trace collision group", "NONE", m_szCollisionGroup )
	DMXELEMENT_UNPACK_FIELD( "maximum trace length", "1024", float, m_flMaxTraceLength )
	DMXELEMENT_UNPACK_FIELD( "trace offset", "0", float, m_flTraceOffset )
	DMXELEMENT_UNPACK_FIELD( "trace recycle tolerance", "64", float, m_flTraceTolerance )
	DMXELEMENT_UNPACK_FIELD( "maximum points to cache", "16", int, m_nMaxPlanes )
	DMXELEMENT_UNPACK_FIELD( "bias distance", "1 1 1", Vector, m_vecComponentScale )
	DMXELEMENT_UNPACK_FIELD( "collide with water", "1", bool, m_bCollideWithWater )
END_PARTICLE_OPERATOR_UNPACK( CTerror_INIT_LifespanFromVelocity )

Vector CTerror_INIT_LifespanFromVelocity::TraceAndCache( TerrorImpactTraceCache_t &cache,
	const Vector &start, const Vector &end, float effectiveLength, float minimumFraction ) const
{
	CBaseTrace trace;
	g_pParticleSystemMgr->Query()->TraceLine( start, end, m_nTraceMask, NULL, m_nCollisionGroup, &trace );
	Vector point = vec3_invalid;
	Vector normal = trace.fraction == 1.0f ? vec3_invalid : trace.plane.normal;
	if ( minimumFraction >= trace.fraction || trace.startsolid || trace.allsolid || effectiveLength == 0.0f )
	{
		normal = vec3_invalid;
	}
	else
	{
		// A plain miss retains endpos, but has an invalid plane normal.
		// A rejected/start-solid trace keeps the invalid POINT sentinel.
		point = trace.endpos;
	}
	const int plane = cache.m_nNextPlane;
	cache.m_bPlaneActive[plane] = true;
	cache.m_PointOnPlane[plane].DuplicateVector( point );
	cache.m_PlaneNormal[plane].DuplicateVector( normal );
	cache.m_TraceStartPoint[plane].DuplicateVector( start );
	cache.m_TraceEndPoint[plane].DuplicateVector( end );
	if ( ++cache.m_nNextPlane > m_nAllowedPlanes )
		cache.m_nNextPlane = 0;
	cache.m_nActivePlanes = MIN( m_nAllowedPlanes, cache.m_nActivePlanes + 1 );
	return point;
}

// C399FF0: never resurrect a particle already killed by a preceding initializer.
// Scalar uses the native wide-length normalization and ALL-component sentinel.
void CTerror_INIT_LifespanFromVelocity::InitNewParticlesScalar( CParticleCollection *particles,
	int first, int count, int writeMask, void *context ) const
{
	TerrorImpactTraceCache_t &cache = GetImpactTraceCache( particles );
	const fltx4 toleranceSquared = ReplicateX4( m_flTraceTolerance * m_flTraceTolerance );
	const float effectiveLength = m_flMaxTraceLength - m_flTraceOffset;
	const float minimumFraction = 1.0f - m_flMaxTraceLength / ( effectiveLength + FLT_EPSILON );
	for ( int index = first; index < first + count; ++index )
	{
		float *life = particles->GetFloatAttributePtrForWrite( PARTICLE_ATTRIBUTE_LIFE_DURATION, index );
		if ( !( *life >= 0.0f ) )
			continue;
		const Vector position = TerrorGapReadVector( particles, PARTICLE_ATTRIBUTE_XYZ, index );
		Vector velocity = position - TerrorGapReadVector( particles, PARTICLE_ATTRIBUTE_PREV_XYZ, index );
		velocity *= 1.0f / particles->m_flPreviousDt;
		const float speed = sqrtf( ( velocity.x * velocity.x + velocity.y * velocity.y ) + velocity.z * velocity.z );
		TerrorPhysicsNormalize( velocity );
		const Vector start = position + velocity * m_flTraceOffset;
		const Vector end = start + velocity * effectiveLength;
		FourVectors starts, ends, point;
		starts.DuplicateVector( start );
		ends.DuplicateVector( end );
		point.DuplicateVector( vec3_invalid );
		fltx4 bestDistance = ReplicateX4( FLT_MAX );
		for ( int plane = 0; plane < cache.m_nActivePlanes; ++plane )
		{
			if ( !cache.m_bPlaneActive[plane] )
				continue;
			const fltx4 distance = ImpactTraceEndpointError( cache, plane, starts, ends );
			if ( _mm_movemask_ps( _mm_cmple_ps( distance, bestDistance ) ) )
			{
				bestDistance = distance;
				point = cache.m_PointOnPlane[plane];
			}
		}
		if ( !_mm_movemask_ps( _mm_cmple_ps( bestDistance, toleranceSquared ) ) )
			point.DuplicateVector( TraceAndCache( cache, start, end, effectiveLength, minimumFraction ) );
		const Vector hit( point.X( 0 ), point.Y( 0 ), point.Z( 0 ) );
		if ( hit.x == vec3_invalid.x && hit.y == vec3_invalid.y && hit.z == vec3_invalid.z )
		{
			*life = -1.0f;
		}
		else
		{
			const float x = ( start.x - hit.x ) * m_vecComponentScale.x;
			const float y = ( start.y - hit.y ) * m_vecComponentScale.y;
			const float z = ( start.z - hit.z ) * m_vecComponentScale.z;
			*life = ( sqrtf( ( x * x + y * y ) + z * z ) + m_flTraceOffset ) / ( speed + FLT_EPSILON );
		}
	}
}

// C39A7A0: retain the native reciprocal/RSQRT, per-lane endpoint cache,
// ANY-component invalid test, zero invalid life, and negative/NaN lane masking.
void CTerror_INIT_LifespanFromVelocity::InitNewParticlesBlock( CParticleCollection *particles,
	int firstBlock, int count, int writeMask, void *context ) const
{
	TerrorImpactTraceCache_t &cache = GetImpactTraceCache( particles );
	const fltx4 toleranceSquared = ReplicateX4( m_flTraceTolerance * m_flTraceTolerance );
	const float effectiveLength = m_flMaxTraceLength - m_flTraceOffset;
	const float minimumFraction = 1.0f - m_flMaxTraceLength / ( effectiveLength + FLT_EPSILON );
	const fltx4 dt = ReplicateX4( particles->m_flPreviousDt );
	const fltx4 estimate = _mm_rcp_ps( dt );
	const fltx4 inverseDt = SubSIMD( AddSIMD( estimate, estimate ), MulSIMD( MulSIMD( estimate, estimate ), dt ) );
	const fltx4 offset = ReplicateX4( m_flTraceOffset );
	const fltx4 length = ReplicateX4( effectiveLength );
	size_t positionStride, previousStride, lifeStride;
	const FourVectors *position = particles->Get4VAttributePtr( PARTICLE_ATTRIBUTE_XYZ, &positionStride );
	const FourVectors *previous = particles->Get4VAttributePtr( PARTICLE_ATTRIBUTE_PREV_XYZ, &previousStride );
	fltx4 *life = particles->GetM128AttributePtrForWrite( PARTICLE_ATTRIBUTE_LIFE_DURATION, &lifeStride );
	position += firstBlock * positionStride;
	previous += firstBlock * previousStride;
	life += firstBlock * lifeStride;
	// Native starts with each stream's own stride, then advances XYZ/PREV by
	// the life stride, just as its ray-environment culling initializer does.
	for ( int block = 0; block < count; ++block, position += lifeStride, previous += lifeStride, life += lifeStride )
	{
		const fltx4 valid = _mm_cmple_ps( Four_Zeros, *life );
		if ( !_mm_movemask_ps( valid ) )
			continue;
		const fltx4 negative = _mm_cmplt_ps( *life, Four_Zeros );
		FourVectors velocity = *position;
		velocity -= *previous;
		velocity *= inverseDt;
		const fltx4 lengthSquared = AddSIMD( AddSIMD( MulSIMD( velocity.y, velocity.y ),
			MulSIMD( velocity.x, velocity.x ) ), MulSIMD( velocity.z, velocity.z ) );
		const fltx4 speed = SqrtSIMD( lengthSquared );
		velocity *= _mm_rsqrt_ps( lengthSquared );
		FourVectors start = velocity;
		start *= offset;
		start += *position;
		FourVectors end = velocity;
		end *= length;
		end += start;
		fltx4 bestDistance = _mm_andnot_ps( negative, ReplicateX4( FLT_MAX ) );
		FourVectors point;
		point.DuplicateVector( vec3_invalid );
		for ( int plane = 0; plane < cache.m_nActivePlanes; ++plane )
		{
			if ( !cache.m_bPlaneActive[plane] )
				continue;
			const fltx4 distance = ImpactTraceEndpointError( cache, plane, start, end );
			const fltx4 nearer = _mm_cmple_ps( distance, bestDistance );
			bestDistance = _mm_or_ps( _mm_and_ps( nearer, distance ), _mm_andnot_ps( nearer, bestDistance ) );
			point.x = _mm_or_ps( _mm_and_ps( nearer, cache.m_PointOnPlane[plane].x ), _mm_andnot_ps( nearer, point.x ) );
			point.y = _mm_or_ps( _mm_and_ps( nearer, cache.m_PointOnPlane[plane].y ), _mm_andnot_ps( nearer, point.y ) );
			point.z = _mm_or_ps( _mm_and_ps( nearer, cache.m_PointOnPlane[plane].z ), _mm_andnot_ps( nearer, point.z ) );
		}
		const int traceMask = _mm_movemask_ps( _mm_cmplt_ps( toleranceSquared, bestDistance ) );
		for ( int lane = 0; lane < 4; ++lane )
		{
			if ( !( traceMask & ( 1 << lane ) ) )
				continue;
			const Vector traceStart( start.X( lane ), start.Y( lane ), start.Z( lane ) );
			const Vector traceEnd( end.X( lane ), end.Y( lane ), end.Z( lane ) );
			const Vector hit = TraceAndCache( cache, traceStart, traceEnd, effectiveLength, minimumFraction );
			point.X( lane ) = hit.x;
			point.Y( lane ) = hit.y;
			point.Z( lane ) = hit.z;
		}
		FourVectors delta = start;
		delta -= point;
		delta.x = MulSIMD( delta.x, ReplicateX4( m_vecComponentScale.x ) );
		delta.y = MulSIMD( delta.y, ReplicateX4( m_vecComponentScale.y ) );
		delta.z = MulSIMD( delta.z, ReplicateX4( m_vecComponentScale.z ) );
		const fltx4 invalid = _mm_or_ps( _mm_or_ps(
			_mm_cmpeq_ps( point.y, ReplicateX4( vec3_invalid.y ) ),
			_mm_cmpeq_ps( point.x, ReplicateX4( vec3_invalid.x ) ) ),
			_mm_cmpeq_ps( point.z, ReplicateX4( vec3_invalid.z ) ) );
		const fltx4 duration = DivSIMD( AddSIMD( TerrorGapLength( delta ), offset ),
			AddSIMD( speed, ReplicateX4( FLT_EPSILON ) ) );
		const fltx4 result = _mm_andnot_ps( invalid, duration );
		*life = _mm_or_ps( _mm_and_ps( valid, result ), _mm_andnot_ps( valid, *life ) );
	}
}

static void RegisterTerrorImpactLifespanInitializer()
{
	// Replace in place before the first PCF is parsed: the game definition
	// wins by name without a duplicate factory or a particle-library ABI change.
	CUtlVector< IParticleOperatorDefinition * > &initializers = g_pParticleSystemMgr->GetAvailableParticleOperatorList( FUNCTION_INITIALIZER );
	FOR_EACH_VEC( initializers, index )
	{
		if ( !Q_stricmp( initializers[index]->GetName(), "Lifetime from Time to Impact" ) )
		{
			initializers[index] = &s_CTerror_INIT_LifespanFromVelocityFactory;
			return;
		}
	}
	Warning( "Cannot register Terror impact-lifetime initializer: builtin 'Lifetime from Time to Impact' is missing; keeping the existing registry.\n" );
}

class C_INIT_NormalOffset : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( C_INIT_NormalOffset );
public:
	virtual void InitParams( CParticleSystemDefinition *definition, CDmxElement *element ) OVERRIDE { m_nControlPoint = clamp( m_nControlPoint, 0, 63 ); }
	virtual uint32 GetWrittenAttributes() const OVERRIDE { return TERROR_PARTICLE_NORMAL_MASK | PARTICLE_ATTRIBUTE_CREATION_TIME_MASK; }
	virtual uint32 GetReadAttributes() const OVERRIDE { return TERROR_PARTICLE_NORMAL_MASK; }
	virtual uint64 GetReadControlPointMask() const OVERRIDE { return TerrorGapCPMask( m_nControlPoint ); }
	virtual bool InitMultipleOverride() OVERRIDE { return true; }
	virtual void InitNewParticlesScalar( CParticleCollection *particles, int first, int count, int writeMask, void *context ) const OVERRIDE
	{
		for ( int index = first; index < first + count; ++index )
		{
			Vector offset;
			// Target vector RNG has the native doubled-seed indexing, but advances
			// the query counter only once. Consume the other two native samples.
			particles->RandomVector( m_vecMin, m_vecMax, &offset );
			particles->RandomFloat( 0.0f, 1.0f ); particles->RandomFloat( 0.0f, 1.0f );
			if ( m_bLocal )
			{
				matrix3x4_t transform;
				particles->GetControlPointTransformAtTime( m_nControlPoint, *particles->GetFloatAttributePtr( PARTICLE_ATTRIBUTE_CREATION_TIME, index ), &transform );
				Vector local = offset; VectorRotate( local, transform, offset );
			}
			TerrorWriteParticleNormal( particles, index, TerrorReadParticleNormal( particles, index ) + offset );
		}
	}
private:
	Vector m_vecMin, m_vecMax;
	int m_nControlPoint;
	bool m_bLocal;
};
DEFINE_PARTICLE_OPERATOR( C_INIT_NormalOffset, "Normal Modify Offset Random", OPERATOR_GENERIC );
BEGIN_PARTICLE_OPERATOR_UNPACK( C_INIT_NormalOffset )
	DMXELEMENT_UNPACK_FIELD( "control_point_number", "0", int, m_nControlPoint )
	DMXELEMENT_UNPACK_FIELD( "offset min", "0 0 0", Vector, m_vecMin )
	DMXELEMENT_UNPACK_FIELD( "offset max", "0 0 0", Vector, m_vecMax )
	DMXELEMENT_UNPACK_FIELD( "offset in local space 0/1", "0", bool, m_bLocal )
END_PARTICLE_OPERATOR_UNPACK( C_INIT_NormalOffset )

#pragma float_control( pop )

void RegisterTerrorParticleGapSpeedInitializer();

void RegisterTerrorParticleGapInitializers()
{
	RegisterTerrorImpactLifespanInitializer();
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_ColorLitPerParticle );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_PositionPlaceOnGround );
	RegisterTerrorParticleGapSpeedInitializer();
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_ChaoticAttractor );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_ModelCull );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_RtEnvCull );
	REGISTER_PARTICLE_OPERATOR( FUNCTION_INITIALIZER, C_INIT_NormalOffset );
}
