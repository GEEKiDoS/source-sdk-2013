//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "particles/particles.h"
#include "l4d2/effects/terror_particle_attributes.h"
#include "mathlib/ssemath.h"
#include <math.h>

#include "tier0/memdbgon.h"

// RVA comments refer to the pinned PE32 L4D2 client, not target object layouts.
// Retail NORMAL21 and GLOW_RGB22 use separate scalar components in this SDK;
// every other output retains the native vector/SOA layout and block stride.
struct TerrorParticleColorOutput_t
{
	fltx4 *m_pRed;
	fltx4 *m_pGreen;
	fltx4 *m_pBlue;
	size_t m_nRedStride;
	size_t m_nGreenStride;
	size_t m_nBlueStride;

	void Init( CParticleCollection *pParticles, int nOutputField )
	{
		if ( nOutputField == 21 || nOutputField == 22 )
		{
			int nFirstAttribute = nOutputField == 21 ? TERROR_PARTICLE_NORMAL_X : TERROR_PARTICLE_GLOW_RED;
			m_pRed = pParticles->GetM128AttributePtrForWrite( nFirstAttribute, &m_nRedStride );
			m_pGreen = pParticles->GetM128AttributePtrForWrite( nFirstAttribute + 1, &m_nGreenStride );
			m_pBlue = pParticles->GetM128AttributePtrForWrite( nFirstAttribute + 2, &m_nBlueStride );
		}
		else
		{
			m_pRed = pParticles->GetM128AttributePtrForWrite( nOutputField, &m_nRedStride );
			m_pGreen = m_pRed + 1;
			m_pBlue = m_pRed + 2;
			m_nGreenStride = m_nRedStride;
			m_nBlueStride = m_nRedStride;
		}
	}

	void WriteScalar( int nParticle, const Vector &vecColor ) const
	{
		int nBlock = nParticle / 4;
		int nLane = nParticle & 3;
		reinterpret_cast< float * >( m_pRed + nBlock * m_nRedStride )[nLane] = vecColor.x;
		reinterpret_cast< float * >( m_pGreen + nBlock * m_nGreenStride )[nLane] = vecColor.y;
		reinterpret_cast< float * >( m_pBlue + nBlock * m_nBlueStride )[nLane] = vecColor.z;
	}

	void WriteBlock( int nBlock, const FourVectors &vecColor ) const
	{
		m_pRed[nBlock * m_nRedStride] = vecColor.x;
		m_pGreen[nBlock * m_nGreenStride] = vecColor.y;
		m_pBlue[nBlock * m_nBlueStride] = vecColor.z;
	}
};


// C:0x39D140. Mode 1 deliberately truncates the normalized overlay to a byte,
// without scaling by 255. The original scalar and SIMD results are different.
template< int nBlendMode >
static FORCEINLINE float TerrorBlendParticleColorScalar( float flBase, float flTint, float flTintPerc )
{
	if ( nBlendMode == -1 )
		return flBase;

	float flTarget;
	if ( nBlendMode == 0 )
	{
		flTarget = flTint;
	}
	else if ( nBlendMode == 1 )
	{
		float flOverlay;
		if ( flTint >= 0.5f )
			flOverlay = 1.0f - ( ( 1.0f - flBase ) * 2.0f ) * ( 1.0f - flTint );
		else
			flOverlay = ( flBase * 2.0f ) * flTint;
		flTarget = (unsigned char)(int)flOverlay;
	}
	else if ( nBlendMode == 2 )
	{
		flTarget = flBase <= flTint ? flBase : flTint;
	}
	else
	{
		flTarget = flTint <= flBase ? flBase : flTint;
	}
	return ( flTarget - flBase ) * flTintPerc + flBase;
}


// C:0x39D9E0. Keep the block overlay floating-point and preserve its multiply
// order: the block low branch multiplies base*tint before multiplying by two.
template< int nBlendMode >
static FORCEINLINE fltx4 TerrorBlendParticleColorSIMD( const fltx4 &fl4Base, const fltx4 &fl4Tint, const fltx4 &fl4TintPerc )
{
	if ( nBlendMode == -1 )
		return fl4Base;

	fltx4 fl4Target;
	if ( nBlendMode == 0 )
	{
		fl4Target = fl4Tint;
	}
	else if ( nBlendMode == 1 )
	{
		fltx4 fl4Low = MulSIMD( MulSIMD( fl4Base, fl4Tint ), Four_Twos );
		fltx4 fl4High = SubSIMD( Four_Ones, MulSIMD( MulSIMD( SubSIMD( Four_Ones, fl4Base ), SubSIMD( Four_Ones, fl4Tint ) ), Four_Twos ) );
		fl4Target = MaskedAssign( CmpLtSIMD( fl4Tint, Four_PointFives ), fl4Low, fl4High );
	}
	else if ( nBlendMode == 2 )
	{
		fl4Target = MinSIMD( fl4Base, fl4Tint );
	}
	else
	{
		fl4Target = MaxSIMD( fl4Base, fl4Tint );
	}
	return AddSIMD( MulSIMD( SubSIMD( fl4Target, fl4Base ), fl4TintPerc ), fl4Base );
}


class CTerror_INIT_RandomColor : public CParticleOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( CTerror_INIT_RandomColor );
public:
	virtual void InitParams( CParticleSystemDefinition *pDef, CDmxElement *pElement );
	virtual uint32 GetWrittenAttributes() const
	{
		if ( m_nOutputField == 21 )
			return TERROR_PARTICLE_NORMAL_MASK;
		if ( m_nOutputField == 22 )
			return TERROR_PARTICLE_GLOW_RGB_MASK;
		return (uint32)1 << m_nOutputField;
	}
	virtual uint32 GetReadAttributes() const { return 0; }
	virtual uint64 GetReadControlPointMask() const { return (uint64)1 << m_nTintControlPoint; }
	virtual bool ShouldRunBeforeEmitters() const { return false; }
	virtual bool InitMultipleOverride() { return false; }
	virtual size_t GetRequiredContextBytes() const { return sizeof( Vector ); }
	virtual void InitializeContextData( CParticleCollection *pParticles, void *pContext ) const
	{
		// C:0x39D020 / 0x39D030: one cached CP position, initially vec3_origin.
		*static_cast< Vector * >( pContext ) = vec3_origin;
	}
	virtual void InitNewParticlesScalar( CParticleCollection *pParticles, int nFirstParticle, int nParticles, int nAttributeWriteMask, void *pContext ) const;
	virtual void InitNewParticlesBlock( CParticleCollection *pParticles, int nStartBlock, int nBlocks, int nAttributeWriteMask, void *pContext ) const;
private:
	Vector GetTint( CParticleCollection *pParticles, void *pContext ) const;
	template< int nBlendMode >
	void InitScalar( CParticleCollection *pParticles, int nFirstParticle, int nParticles, const Vector &vecTint ) const;
	template< int nBlendMode >
	void InitBlock( CParticleCollection *pParticles, int nStartBlock, int nBlocks, const Vector &vecTint, int nRandomContext ) const;

	Vector m_vecColor1;
	Vector m_vecColor2;
	Color m_Color1;
	Color m_Color2;
	Color m_TintClampMin;
	Color m_TintClampMax;
	float m_flTintPerc;
	float m_flTintUpdateMovementThreshold;
	int m_nTintControlPoint;
	int m_nOutputField;
	int m_nTintBlendMode;
};

// C:0x3A0250 / 0x387620. Preserve serialized fields, types and defaults;
// InitParams does not clamp, reorder or otherwise tune these parameters.
DEFINE_PARTICLE_OPERATOR( CTerror_INIT_RandomColor, "Color Random", OPERATOR_GENERIC );
BEGIN_PARTICLE_OPERATOR_UNPACK( CTerror_INIT_RandomColor )
	DMXELEMENT_UNPACK_FIELD( "color1", "255 255 255 255", Color, m_Color1 )
	DMXELEMENT_UNPACK_FIELD( "color2", "255 255 255 255", Color, m_Color2 )
	DMXELEMENT_UNPACK_FIELD( "tint clamp min", "0 0 0 0", Color, m_TintClampMin )
	DMXELEMENT_UNPACK_FIELD( "tint clamp max", "255 255 255 255", Color, m_TintClampMax )
	DMXELEMENT_UNPACK_FIELD( "tint_perc", "0.0", float, m_flTintPerc )
	DMXELEMENT_UNPACK_FIELD( "tint update movement threshold", "32", float, m_flTintUpdateMovementThreshold )
	DMXELEMENT_UNPACK_FIELD( "tint control point", "0", int, m_nTintControlPoint )
	DMXELEMENT_UNPACK_FIELD_USERDATA( "output field", "6", int, m_nOutputField, "particlefield_vector" )
	DMXELEMENT_UNPACK_FIELD( "tint blend mode", "0", int, m_nTintBlendMode )
END_PARTICLE_OPERATOR_UNPACK( CTerror_INIT_RandomColor )


// C:0x39D060. Alpha is serialized but does not participate in the RGB output.
void CTerror_INIT_RandomColor::InitParams( CParticleSystemDefinition *pDef, CDmxElement *pElement )
{
	m_vecColor1.Init( m_Color1.r() * ( 1.0f / 255.0f ), m_Color1.g() * ( 1.0f / 255.0f ), m_Color1.b() * ( 1.0f / 255.0f ) );
	m_vecColor2.Init( m_Color2.r() * ( 1.0f / 255.0f ), m_Color2.g() * ( 1.0f / 255.0f ), m_Color2.b() * ( 1.0f / 255.0f ) );
}


Vector CTerror_INIT_RandomColor::GetTint( CParticleCollection *pParticles, void *pContext ) const
{
	Color tint( 255, 255, 255, 255 );
	// C:0x39D140 / 0x39D9E0 read collection +140. C:0x3711A0 explicitly
	// stores the direct parent there, separately from sibling links +112/+116.
	// Target particles.lib Color Random and Position From Parent Particles
	// both read +0x22B0: the public m_pParent, not the child-list head.
	CParticleCollection *pParent = pParticles->m_pParent;
	if ( pParent && pParent->m_LocalLightingCP == m_nTintControlPoint )
	{
		tint = pParent->m_LocalLighting;
	}
	else
	{
		Vector vecPosition;
		pParticles->GetControlPointAtTime( m_nTintControlPoint, pParticles->m_flCurTime, &vecPosition );
		Vector *pPreviousPosition = static_cast< Vector * >( pContext );
		Vector vecDelta = *pPreviousPosition - vecPosition;
		float flDistance = sqrtf( ( vecDelta.x * vecDelta.x + vecDelta.y * vecDelta.y ) + vecDelta.z * vecDelta.z );
		if ( flDistance >= m_flTintUpdateMovementThreshold || pParticles->m_LocalLightingCP == -1 )
		{
			g_pParticleSystemMgr->Query()->GetLightingAtPoint( vecPosition, tint );
			pParticles->m_LocalLighting = tint;
			pParticles->m_LocalLightingCP = m_nTintControlPoint;
			*pPreviousPosition = vecPosition;
		}
		else
		{
			tint = pParticles->m_LocalLighting;
		}
	}

	// The original applies the upper bound first, then the lower bound, even
	// for reversed clamp bounds. Do not replace this with parameter validation.
	return Vector(
		MAX( m_TintClampMin.r(), MIN( tint.r(), m_TintClampMax.r() ) ) * ( 1.0f / 255.0f ),
		MAX( m_TintClampMin.g(), MIN( tint.g(), m_TintClampMax.g() ) ) * ( 1.0f / 255.0f ),
		MAX( m_TintClampMin.b(), MIN( tint.b(), m_TintClampMax.b() ) ) * ( 1.0f / 255.0f ) );
}


template< int nBlendMode >
void CTerror_INIT_RandomColor::InitScalar( CParticleCollection *pParticles, int nFirstParticle, int nParticles, const Vector &vecTint ) const
{
	TerrorParticleColorOutput_t output;
	output.Init( pParticles, m_nOutputField );
	Vector vecDelta = m_vecColor2 - m_vecColor1;
	for ( int i = nFirstParticle; i < nFirstParticle + nParticles; ++i )
	{
		// C:0x39D140: one collection-stream sample, shared across RGB.
		float flRandom = pParticles->RandomFloat( 0.0f, 1.0f );
		Vector vecColor(
			vecDelta.x * flRandom + m_vecColor1.x,
			vecDelta.y * flRandom + m_vecColor1.y,
			vecDelta.z * flRandom + m_vecColor1.z );
		vecColor.x = TerrorBlendParticleColorScalar< nBlendMode >( vecColor.x, vecTint.x, m_flTintPerc );
		vecColor.y = TerrorBlendParticleColorScalar< nBlendMode >( vecColor.y, vecTint.y, m_flTintPerc );
		vecColor.z = TerrorBlendParticleColorScalar< nBlendMode >( vecColor.z, vecTint.z, m_flTintPerc );
		output.WriteScalar( i, vecColor );
	}
}


template< int nBlendMode >
void CTerror_INIT_RandomColor::InitBlock( CParticleCollection *pParticles, int nStartBlock, int nBlocks, const Vector &vecTint, int nRandomContext ) const
{
	TerrorParticleColorOutput_t output;
	output.Init( pParticles, m_nOutputField );
	FourVectors vecColor1;
	vecColor1.DuplicateVector( m_vecColor1 );
	FourVectors vecDelta;
	vecDelta.DuplicateVector( m_vecColor2 - m_vecColor1 );
	FourVectors vecTint4;
	vecTint4.DuplicateVector( vecTint );
	fltx4 fl4TintPerc = ReplicateX4( m_flTintPerc );
	for ( int i = nStartBlock; i < nStartBlock + nBlocks; ++i )
	{
		// C:0x368940 -> 0x3687D0: one draw for each four-particle block.
		fltx4 fl4Random = RandSIMD( nRandomContext );
		FourVectors vecColor;
		vecColor.x = AddSIMD( MulSIMD( vecDelta.x, fl4Random ), vecColor1.x );
		vecColor.y = AddSIMD( MulSIMD( vecDelta.y, fl4Random ), vecColor1.y );
		vecColor.z = AddSIMD( MulSIMD( vecDelta.z, fl4Random ), vecColor1.z );
		vecColor.x = TerrorBlendParticleColorSIMD< nBlendMode >( vecColor.x, vecTint4.x, fl4TintPerc );
		vecColor.y = TerrorBlendParticleColorSIMD< nBlendMode >( vecColor.y, vecTint4.y, fl4TintPerc );
		vecColor.z = TerrorBlendParticleColorSIMD< nBlendMode >( vecColor.z, vecTint4.z, fl4TintPerc );
		output.WriteBlock( i, vecColor );
	}
}


// The target's nonvirtual CParticleOperatorInstance::InitNewParticles already
// matches C:0x369AF0: counts <16 go straight to scalar; otherwise scalar head
// reaches the next absolute four-particle boundary, then blocks and scalar tail.
// Keep both overrides so that split also preserves the two RNG streams and the
// scalar-only mode-1 byte-cast quirk. The write-mask argument is ignored retail.
void CTerror_INIT_RandomColor::InitNewParticlesScalar( CParticleCollection *pParticles, int nFirstParticle, int nParticles, int nAttributeWriteMask, void *pContext ) const
{
	if ( m_flTintPerc == 0.0f )
	{
		InitScalar< -1 >( pParticles, nFirstParticle, nParticles, vec3_origin );
		return;
	}

	Vector vecTint = GetTint( pParticles, pContext );
	switch ( m_nTintBlendMode )
	{
	case 0: InitScalar< 0 >( pParticles, nFirstParticle, nParticles, vecTint ); break;
	case 1: InitScalar< 1 >( pParticles, nFirstParticle, nParticles, vecTint ); break;
	case 2: InitScalar< 2 >( pParticles, nFirstParticle, nParticles, vecTint ); break;
	case 3: InitScalar< 3 >( pParticles, nFirstParticle, nParticles, vecTint ); break;
	// Invalid modes still do the tint/cache lookup, but write and draw nothing.
	}
}


void CTerror_INIT_RandomColor::InitNewParticlesBlock( CParticleCollection *pParticles, int nStartBlock, int nBlocks, int nAttributeWriteMask, void *pContext ) const
{
	// C:0x368980 / 0x3689D0: lease once per block call, before tint lookup,
	// including invalid modes and zero blocks. Never seed the global streams.
	int nRandomContext = GetSIMDRandContext();
	if ( m_flTintPerc == 0.0f )
	{
		InitBlock< -1 >( pParticles, nStartBlock, nBlocks, vec3_origin, nRandomContext );
	}
	else
	{
		Vector vecTint = GetTint( pParticles, pContext );
		switch ( m_nTintBlendMode )
		{
		case 0: InitBlock< 0 >( pParticles, nStartBlock, nBlocks, vecTint, nRandomContext ); break;
		case 1: InitBlock< 1 >( pParticles, nStartBlock, nBlocks, vecTint, nRandomContext ); break;
		case 2: InitBlock< 2 >( pParticles, nStartBlock, nBlocks, vecTint, nRandomContext ); break;
		case 3: InitBlock< 3 >( pParticles, nStartBlock, nBlocks, vecTint, nRandomContext ); break;
		}
	}
	ReleaseSIMDRandContext( nRandomContext );
}


void RegisterTerrorParticleColorInitializer()
{
	CUtlVector< IParticleOperatorDefinition * > &initializers = g_pParticleSystemMgr->GetAvailableParticleOperatorList( FUNCTION_INITIALIZER );
	FOR_EACH_VEC( initializers, i )
	{
		if ( Q_stricmp( initializers[i]->GetName(), "Color Random" ) )
			continue;
		// Replace in place before PCFs load; repeated registration is idempotent.
		// Appending a duplicate would leave native factory selection ambiguous.
		initializers[i] = &s_CTerror_INIT_RandomColorFactory;
		return;
	}
	Error( "Cannot register Terror Color Random: native Color Random factory is missing.\n" );
}
