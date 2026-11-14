//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/effects/terror_particle_render.h"
#include "l4d2/effects/terror_particle_attributes.h"
#include "l4d2/effects/terror_particle_sheet.h"
#include "l4d2/effects/terror_materials.h"
#include "dmxloader/dmxloader.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/imaterialvar.h"
#include "materialsystem/imesh.h"
#include <float.h>
#include <math.h>

#include "tier0/memdbgon.h"

// This is the four-argument export in the target particles.lib, not the later
// renderer variant which also accepts an IMatRenderContext.
void SetupParticleVisibility( CParticleCollection *pParticles, CParticleVisibilityData *pVisibilityData, const CParticleVisibilityInputs *pVisibilityInputs, int *pQueryHandle );

static IParticleOperatorDefinition *s_pNativeSpriteFactory = NULL;

// C36CF70 applies these matrices around each screen-space renderer. The
// target particle library does not retain that authored definition flag.
class CScopedTerrorParticleScreenSpace
{
public:
	CScopedTerrorParticleScreenSpace( IMatRenderContext *pRenderContext, bool bScreenSpace ) :
		m_pRenderContext( bScreenSpace ? pRenderContext : NULL )
	{
		if ( !m_pRenderContext )
		{
			return;
		}
		m_pRenderContext->MatrixMode( MATERIAL_VIEW );
		m_pRenderContext->PushMatrix();
		m_pRenderContext->LoadIdentity();
		m_pRenderContext->MatrixMode( MATERIAL_PROJECTION );
		m_pRenderContext->PushMatrix();
		m_pRenderContext->LoadIdentity();
		m_pRenderContext->Ortho( -100.0, -100.0, 100.0, 100.0, -100.0, 100.0 );
	}

	~CScopedTerrorParticleScreenSpace()
	{
		if ( !m_pRenderContext )
		{
			return;
		}
		m_pRenderContext->MatrixMode( MATERIAL_VIEW );
		m_pRenderContext->PopMatrix();
		m_pRenderContext->MatrixMode( MATERIAL_PROJECTION );
		m_pRenderContext->PopMatrix();
	}

	// C3C1160 reads the real camera only for the distance test, temporarily
	// restoring the matrices beneath this renderer's screen-space pair.
	void GetWorldSpaceCameraPosition( IMatRenderContext *pRenderContext, Vector *pCamera ) const
	{
		if ( m_pRenderContext )
		{
			m_pRenderContext->MatrixMode( MATERIAL_VIEW );
			m_pRenderContext->PopMatrix();
			m_pRenderContext->MatrixMode( MATERIAL_PROJECTION );
			m_pRenderContext->PopMatrix();
		}
		pRenderContext->GetWorldSpaceCameraPosition( pCamera );
		if ( m_pRenderContext )
		{
			m_pRenderContext->MatrixMode( MATERIAL_VIEW );
			m_pRenderContext->PushMatrix();
			m_pRenderContext->LoadIdentity();
			m_pRenderContext->MatrixMode( MATERIAL_PROJECTION );
			m_pRenderContext->PushMatrix();
			m_pRenderContext->LoadIdentity();
			m_pRenderContext->Ortho( -100.0, -100.0, 100.0, 100.0, -100.0, 100.0 );
		}
	}

private:
	IMatRenderContext *m_pRenderContext;
};

// Original C:0x4E94E0 builds512 samples. Target CSheet's1024-row builder
// uses exactly twice the knots/range, so target row2*i equals original rowi.
// Keep the original index schedule rather than selecting intervening rows.
enum { TERROR_SHEET_SAMPLE_COUNT = 512 };

// Original default sample at C:714898; quad corners below at C:71621C.
static const SheetSequenceSample_t s_TerrorDefaultSheetSample =
{
	{
		{ 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f },
		{ 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f }
	},
	1.0f
};

static const SheetSequenceSample_t *GetTerrorSheetSample( CSheet *pSheet, float flElapsed, float flAgeScale, int nSequence )
{
	if ( pSheet->m_nNumFrames[nSequence] == 1 )
		return pSheet->m_pSamples[nSequence];
	// C:0x3BF830 truncates to signed64, then keeps unsigned32. In clamp mode
	// negative wrapped indices therefore select the final row, not row zero.
	unsigned int nSample = (unsigned int)(int64)( flElapsed * flAgeScale );
	if ( pSheet->m_bClamp[nSequence] )
		nSample = MIN( nSample, (unsigned int)( TERROR_SHEET_SAMPLE_COUNT - 1 ) );
	else
		nSample &= TERROR_SHEET_SAMPLE_COUNT - 1;
	return pSheet->m_pSamples[nSequence] + 2 * nSample;
}

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif

static void GetTerrorDualSheetSamples( CSheet *pSheet, float flSequence, float flSequence2,
	bool bSequence2HasStride, float flCurrentTime, float flCreationTime,
	float flAgeScale, float flAgeScale2, bool bAnimateInFPS,
	const SheetSequenceSample_t **ppSample, const SheetSequenceSample_t **ppSample2 )
{
	float flElapsed = _mm_cvtss_f32( _mm_sub_ss( _mm_set_ss( flCurrentTime ), _mm_set_ss( flCreationTime ) ) );
	// C3CBCA0/C3CB360 pack the two float attributes into bytes using these
	// ordered float operations, not integer conversion. A constant second
	// sequence (zero SOA stride) is deliberately omitted by the native packer.
	fltx4 flPacked = _mm_set_ss( flSequence );
	if ( bSequence2HasStride )
	{
		flPacked = _mm_add_ss( _mm_mul_ss( _mm_set_ss( flSequence2 ), _mm_set_ss( 256.0f ) ), flPacked );
	}
	flPacked = _mm_add_ss( flPacked, _mm_set_ss( 8388608.0f ) );
	float flSequenceBits = _mm_cvtss_f32( flPacked );
	unsigned int nSequences = FloatBits( flSequenceBits );
	int nSequence = nSequences & 0xFF;
	int nSequence2 = ( nSequences >> 8 ) & 0xFF;
	if ( bAnimateInFPS )
	{
		flAgeScale /= pSheet->m_flFrameSpan[nSequence];
		flAgeScale2 /= pSheet->m_flFrameSpan[nSequence2];
	}
	// C3C4F50 ignores animation_fit_lifetime for both sequences.
	*ppSample = GetTerrorSheetSample( pSheet, flElapsed, flAgeScale, nSequence );
	*ppSample2 = GetTerrorSheetSample( pSheet, flElapsed, flAgeScale2, nSequence2 );
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif

// Cache native SOA addresses once per render/batch; no per-particle lookup of
// sheet data, and no copying or ownership of the collection's native CSheet.
struct TerrorSpriteRenderInfo_t
{
	enum Attribute_t
	{
		XYZ,
		TINT,
		ROTATION,
		YAW,
		CREATION_TIME,
		SEQUENCE,
		SEQUENCE2,
		GLOW_RED,
		GLOW_GREEN,
		GLOW_BLUE,
		ATTRIBUTE_COUNT
	};

	const fltx4 *m_pAttribute[ATTRIBUTE_COUNT];
	size_t m_nStride[ATTRIBUTE_COUNT];
	CSheet *m_pSheet;
	CParticleCollection *m_pParticles;
	int m_nVertexOffset;
	float m_flAgeScale;
	float m_flAgeScale2;
	bool m_bWriteGlow;
	bool m_bDualSequence;

	void InitAttribute( CParticleCollection *pParticles, Attribute_t nSlot, int nAttribute )
	{
		m_pAttribute[nSlot] = pParticles->GetM128AttributePtr( nAttribute, &m_nStride[nSlot] );
	}

	void InitDualSequenceRates( float flRate, float flRate2 )
	{
		// C3BEE90 receives both scaled rates once, before the render list.
		m_flAgeScale = _mm_cvtss_f32( _mm_mul_ss( _mm_set_ss( flRate ), _mm_set_ss( TERROR_SHEET_SAMPLE_COUNT ) ) );
		m_flAgeScale2 = _mm_cvtss_f32( _mm_mul_ss( _mm_set_ss( flRate2 ), _mm_set_ss( TERROR_SHEET_SAMPLE_COUNT ) ) );
	}

	void Init( CParticleCollection *pParticles, int nVertexOffset, bool bReadGlow, bool bWriteGlow, bool bDualSequence )
	{
		InitAttribute( pParticles, XYZ, PARTICLE_ATTRIBUTE_XYZ );
		InitAttribute( pParticles, TINT, PARTICLE_ATTRIBUTE_TINT_RGB );
		InitAttribute( pParticles, ROTATION, PARTICLE_ATTRIBUTE_ROTATION );
		InitAttribute( pParticles, YAW, PARTICLE_ATTRIBUTE_YAW );
		InitAttribute( pParticles, CREATION_TIME, PARTICLE_ATTRIBUTE_CREATION_TIME );
		InitAttribute( pParticles, SEQUENCE, PARTICLE_ATTRIBUTE_SEQUENCE_NUMBER );
		if ( bDualSequence )
			InitAttribute( pParticles, SEQUENCE2, PARTICLE_ATTRIBUTE_SEQUENCE_NUMBER1 );
		if ( bReadGlow && bWriteGlow )
		{
			InitAttribute( pParticles, GLOW_RED, TERROR_PARTICLE_GLOW_RED );
			InitAttribute( pParticles, GLOW_GREEN, TERROR_PARTICLE_GLOW_GREEN );
			InitAttribute( pParticles, GLOW_BLUE, TERROR_PARTICLE_GLOW_BLUE );
		}
		m_pSheet = pParticles->m_Sheet();
		m_pParticles = pParticles;
		m_nVertexOffset = nVertexOffset;
		m_bWriteGlow = bWriteGlow;
		m_bDualSequence = bDualSequence;
	}

	float Read( Attribute_t nSlot, int nGroup, int nLane, int nComponent = 0 ) const
	{
		return SubFloat( m_pAttribute[nSlot][nGroup * m_nStride[nSlot] + nComponent], nLane );
	}
};

class CTerror_OP_RenderSprites : public CParticleRenderOperatorInstance
{
	DECLARE_PARTICLE_OPERATOR( CTerror_OP_RenderSprites );
public:
	CTerror_OP_RenderSprites() :
		m_pNativeRenderer( NULL ),
		m_bHasGlowRGBProducer( false ),
		m_bScreenSpace( false )
	{
	}
	virtual ~CTerror_OP_RenderSprites() { delete m_pNativeRenderer; }

	virtual void InitParams( CParticleSystemDefinition *pDef, CDmxElement *pElement );
	virtual size_t GetRequiredContextBytes() const;
	virtual void InitializeContextData( CParticleCollection *pParticles, void *pContext ) const;
	virtual uint32 GetWrittenAttributes() const { return m_pNativeRenderer->GetWrittenAttributes(); }
	virtual uint32 GetReadAttributes() const;
	virtual uint32 GetReadInitialAttributes() const { return m_pNativeRenderer->GetReadInitialAttributes(); }
	virtual uint64 GetReadControlPointMask() const;
	virtual bool IsBatchable() const
	{
		if ( m_bScreenSpace )
		{
			return false;
		}
		return m_pNativeRenderer->IsBatchable();
	}
	virtual bool RequiresOrderInvariance() const { return m_pNativeRenderer->RequiresOrderInvariance(); }
	virtual bool ShouldRunBeforeEmitters() const { return m_pNativeRenderer->ShouldRunBeforeEmitters(); }
	virtual bool IsScrubSafe() { return m_pNativeRenderer->IsScrubSafe(); }
	virtual void InitParticleSystem( CParticleCollection *pParticles, void *pContext ) const
	{
		m_pNativeRenderer->InitParticleSystem( pParticles, GetNativeContext( pContext ) );
	}
	virtual void Restart( CParticleCollection *pParticles, void *pContext )
	{
		m_pNativeRenderer->Restart( pParticles, GetNativeContext( pContext ) );
	}
	virtual void SkipToTime( float flTime, CParticleCollection *pParticles, void *pContext ) const
	{
		m_pNativeRenderer->SkipToTime( flTime, pParticles, GetNativeContext( pContext ) );
	}
	virtual void Render( CParticleCollection *pParticles ) const
	{
		m_pNativeRenderer->Render( pParticles );
	}
	virtual int GetParticlesToRender( CParticleCollection *pParticles, void *pContext, int nFirstParticle, int nRemainingVertices, int nRemainingIndices, int *pVertsUsed, int *pIndicesUsed ) const;
	virtual void Render( IMatRenderContext *pRenderContext, CParticleCollection *pParticles, void *pContext ) const;
	virtual void RenderUnsorted( CParticleCollection *pParticles, void *pContext, IMatRenderContext *pRenderContext, CMeshBuilder &meshBuilder, int nVertexOffset, int nFirstParticle, int nParticleCount ) const;

private:
	struct Context_t
	{
		unsigned int m_nOrientationVarToken;
		unsigned int m_nOrientationMatrixVarToken;
		CParticleVisibilityData m_VisibilityData;
		int m_nQueryHandle;
	};

	// The collection provides one 16-byte-aligned allocation. Its tail belongs
	// exclusively to the native renderer, sized through the public interface.
	enum { NATIVE_CONTEXT_OFFSET = ( sizeof( Context_t ) + 15 ) & ~15 };
	static void *GetNativeContext( void *pContext )
	{
		return static_cast< byte * >( pContext ) + NATIVE_CONTEXT_OFFSET;
	}

	static IMaterial *PrepareMaterial( CParticleCollection *pParticles );
	bool UsesLocalCardRenderer( IMaterial *pMaterial, bool *pDualSequence = NULL ) const;
	void SetOrientation( CParticleCollection *pParticles, Context_t *pContext, IMaterial *pMaterial ) const;
	void UpdateVisibility( CParticleCollection *pParticles, Context_t *pContext ) const;
	void UpdateNativeVisibility( CParticleCollection *pParticles, Context_t *pContext, IMatRenderContext *pRenderContext, const CScopedTerrorParticleScreenSpace *pScreenSpace ) const;
	void RenderSprite( CMeshBuilder &meshBuilder, TerrorSpriteRenderInfo_t &info, const ParticleRenderData_t &renderData ) const;

	// These are the target native schema, not an overlay on its private class.
	float m_flAnimationRate;
	float m_flAnimationRate2;
	bool m_bFitCycleToLifetime;
	bool m_bAnimateInFPS;
	int m_nOrientationType;
	int m_nOrientationControlPoint;
	// C3BEF50's visibility inputs are absent from the target native schema.
	float m_flVisibilityInputDistanceMin;
	float m_flVisibilityInputDistanceMax;
	float m_flVisibilityInputDotMin;
	float m_flVisibilityInputDotMax;

	CParticleOperatorInstance *m_pNativeRenderer;
	bool m_bHasGlowRGBProducer;
	bool m_bScreenSpace;
};

DEFINE_PARTICLE_OPERATOR( CTerror_OP_RenderSprites, "render_animated_sprites", OPERATOR_GENERIC );
BEGIN_PARTICLE_RENDER_OPERATOR_UNPACK( CTerror_OP_RenderSprites )
	DMXELEMENT_UNPACK_FIELD( "animation rate", ".1", float, m_flAnimationRate )
	DMXELEMENT_UNPACK_FIELD( "animation_fit_lifetime", "0", bool, m_bFitCycleToLifetime )
	DMXELEMENT_UNPACK_FIELD( "orientation_type", "0", int, m_nOrientationType )
	DMXELEMENT_UNPACK_FIELD( "orientation control point", "-1", int, m_nOrientationControlPoint )
	DMXELEMENT_UNPACK_FIELD( "second sequence animation rate", "0", float, m_flAnimationRate2 )
	DMXELEMENT_UNPACK_FIELD( "use animation rate as FPS", "0", bool, m_bAnimateInFPS )
	DMXELEMENT_UNPACK_FIELD( "Visibility input distance minimum", "0", float, m_flVisibilityInputDistanceMin )
	DMXELEMENT_UNPACK_FIELD( "Visibility input distance maximum", "0", float, m_flVisibilityInputDistanceMax )
	DMXELEMENT_UNPACK_FIELD( "Visibility input dot minimum", "0", float, m_flVisibilityInputDotMin )
	DMXELEMENT_UNPACK_FIELD( "Visibility input dot maximum", "0", float, m_flVisibilityInputDotMax )
END_PARTICLE_OPERATOR_UNPACK( CTerror_OP_RenderSprites )

void CTerror_OP_RenderSprites::InitParams( CParticleSystemDefinition *pDef, CDmxElement *pElement )
{
	m_bScreenSpace = pElement->GetValue< bool >( "screen space effect" );
	// Native InitParams does not admit materials. Keep both renderers available
	// until a real collection supplies its precached material.
	// InitParams receives the SYSTEM element. The operator's attributes have
	// already been unpacked into this wrapper, so round-trip only its public
	// schema into a genuine native factory instance. Never apply the system
	// element, or our member offsets, directly to the native object.
	m_pNativeRenderer = s_pNativeSpriteFactory->CreateInstance( GetId() );
	// Native ReadParticleConfigFile already owns the DMX context. Contexts
	// are not nestable: ending an inner one disables DMXAlloc for the
	// remaining operators (target DMXAlloc0x26D110/EndDMXContext0x26D1B0).
	CDmxElement *pParams = CreateDmxElement( "DmeParticleOperator" );
	{
		CDmxElementModifyScope modify( pParams );
		pParams->AddAttributesFromStructure( this, GetDefinition()->GetUnpackStructure() );
	}
	pParams->UnpackIntoStructure( m_pNativeRenderer, s_pNativeSpriteFactory->GetClassSize(), s_pNativeSpriteFactory->GetUnpackStructure() );
	m_pNativeRenderer->InitParams( pDef, pElement );
	CleanupDMX( pParams );

	// Original C:3711A0 initializes GlowRGB22 and GlowAlpha23 to one. All
	// reached custom consumers either retain that RGB constant or write it
	// with Color Random/output field22; none writes GlowAlpha23. Translate
	// the proven constants here instead of mutating native private storage.
	const CUtlVector< CDmxElement * > &initializers = pElement->GetArray< CDmxElement * >( "initializers" );
	FOR_EACH_VEC( initializers, i )
	{
		if ( !Q_stricmp( initializers[i]->GetValueString( "functionName" ), "Color Random" ) &&
			initializers[i]->GetValue<int>( "output field", PARTICLE_ATTRIBUTE_TINT_RGB ) == 22 )
		{
			m_bHasGlowRGBProducer = true;
			break;
		}
	}
}

size_t CTerror_OP_RenderSprites::GetRequiredContextBytes() const
{
	return NATIVE_CONTEXT_OFFSET + m_pNativeRenderer->GetRequiredContextBytes();
}

IMaterial *CTerror_OP_RenderSprites::PrepareMaterial( CParticleCollection *pParticles )
{
	IMaterial *pMaterial = pParticles->m_pDef->GetMaterial();
	if ( pMaterial && !Terror_PrepareParticleMaterial( pMaterial, "TerrorSpriteCard" ) )
	{
		Warning( "Unable to prepare particle material %s for %s.\n", pParticles->m_pDef->MaterialName(), pParticles->m_pDef->GetName() );
	}
	return pMaterial;
}

void CTerror_OP_RenderSprites::InitializeContextData( CParticleCollection *pParticles, void *pContext ) const
{
	PrepareMaterial( pParticles );
	m_pNativeRenderer->InitializeContextData( pParticles, GetNativeContext( pContext ) );
	Context_t *pCtx = static_cast< Context_t * >( pContext );
	pCtx->m_nOrientationVarToken = 0;
	pCtx->m_nOrientationMatrixVarToken = 0;
	pCtx->m_nQueryHandle = 0;
	pCtx->m_VisibilityData.m_flAlphaVisibility = 1.0f;
	pCtx->m_VisibilityData.m_flRadiusVisibility = 1.0f;
	pCtx->m_VisibilityData.m_flCameraBias = VisibilityInputs.m_flCameraBias;
	pCtx->m_VisibilityData.m_bUseVisibility = VisibilityInputs.m_nCPin >= 0;
}

uint32 CTerror_OP_RenderSprites::GetReadAttributes() const
{
	// The material is not admitted yet, so reserve the inputs of either path.
	uint32 nMask = m_pNativeRenderer->GetReadAttributes() |
		PARTICLE_ATTRIBUTE_XYZ_MASK | PARTICLE_ATTRIBUTE_ROTATION_MASK |
		PARTICLE_ATTRIBUTE_YAW_MASK | PARTICLE_ATTRIBUTE_RADIUS_MASK | PARTICLE_ATTRIBUTE_TINT_RGB_MASK |
		PARTICLE_ATTRIBUTE_ALPHA_MASK | PARTICLE_ATTRIBUTE_ALPHA2_MASK | PARTICLE_ATTRIBUTE_CREATION_TIME_MASK |
		PARTICLE_ATTRIBUTE_SEQUENCE_NUMBER_MASK;
	if ( VisibilityInputs.m_nCPin >= 0 )
		nMask |= PARTICLE_ATTRIBUTE_SEQUENCE_NUMBER1_MASK;
	if ( m_bHasGlowRGBProducer )
		nMask |= TERROR_PARTICLE_GLOW_RGB_MASK;
	return nMask;
}

uint64 CTerror_OP_RenderSprites::GetReadControlPointMask() const
{
	uint64 nMask = m_pNativeRenderer->GetReadControlPointMask();
	if ( m_nOrientationControlPoint >= 0 )
		nMask |= (uint64)1 << m_nOrientationControlPoint;
	if ( VisibilityInputs.m_nCPin >= 0 )
		nMask |= (uint64)1 << VisibilityInputs.m_nCPin;
	return nMask;
}

void CTerror_OP_RenderSprites::SetOrientation( CParticleCollection *pParticles, Context_t *pContext, IMaterial *pMaterial ) const
{
	IMaterialVar *pVar = pMaterial->FindVarFast( "$orientation", &pContext->m_nOrientationVarToken );
	if ( pVar )
		pVar->SetIntValue( clamp( m_nOrientationType, 0, 3 ) );
	if ( m_nOrientationType == 2 )
	{
		pVar = pMaterial->FindVarFast( "$orientationMatrix", &pContext->m_nOrientationMatrixVarToken );
		if ( pVar )
		{
			VMatrix matrix;
			if ( m_nOrientationControlPoint < 0 )
				MatrixSetIdentity( matrix );
			else
				pParticles->GetControlPointTransformAtCurrentTime( m_nOrientationControlPoint, &matrix );
			pVar->SetMatrixValue( matrix );
		}
	}
}

void CTerror_OP_RenderSprites::UpdateVisibility( CParticleCollection *pParticles, Context_t *pContext ) const
{
	if ( pContext->m_VisibilityData.m_bUseVisibility )
		SetupParticleVisibility( pParticles, &pContext->m_VisibilityData, &VisibilityInputs, &pContext->m_nQueryHandle );
}

bool CTerror_OP_RenderSprites::UsesLocalCardRenderer( IMaterial *pMaterial, bool *pDualSequence ) const
{
	if ( pDualSequence )
		*pDualSequence = false;
	if ( !Q_stricmp( pMaterial->GetShaderName(), "TerrorSpriteCard" ) )
		return TexCoordSize( 3, pMaterial->GetVertexFormat() ) == 2;
	if ( VisibilityInputs.m_nCPin < 0 || !pMaterial->IsSpriteCard() )
		return false;
	// Spline cards have a four-component corner. Ordinary cards use either
	// five coordinates or all eight for the native dual-sequence packet.
	VertexFormat_t format = pMaterial->GetVertexFormat();
	if ( TexCoordSize( 3, format ) != 2 )
		return false;
	bool bDualSequence = TexCoordSize( 5, format ) == 4 &&
		TexCoordSize( 6, format ) == 4 && TexCoordSize( 7, format ) == 4;
	if ( pDualSequence )
		*pDualSequence = bDualSequence;
	return TexCoordSize( 5, format ) == 0 || bDualSequence;
}

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif

static float RemapTerrorParticleVisibility( float flValue, float flMin, float flMax )
{
	float flResult = ( flValue - flMin ) / ( flMax - flMin );
	if ( flResult < 0.0f )
		return 0.0f;
	if ( flResult > 1.0f )
		return 1.0f;
	return flResult;
}

// C3C1160: equal ranges disable their tests, including +0/-0. Unequal
// unordered inputs still enter, and the explicit remap retains their NaNs.
// Keep this separate from the existing outlined-card visibility path.
void CTerror_OP_RenderSprites::UpdateNativeVisibility( CParticleCollection *pParticles, Context_t *pContext, IMatRenderContext *pRenderContext, const CScopedTerrorParticleScreenSpace *pScreenSpace ) const
{
	if ( !pContext->m_VisibilityData.m_bUseVisibility )
		return;

	int nControlPoint = MIN( VisibilityInputs.m_nCPin, pParticles->GetHighestControlPoint() );
	Vector vecOrigin = pParticles->ControlPoint( nControlPoint ).m_Position;
	float flVisibility = 1.0f;
	if ( VisibilityInputs.m_flInputMin != VisibilityInputs.m_flInputMax )
	{
		float flPixelVisibility = g_pParticleSystemMgr->Query()->GetPixelVisibility(
			&pContext->m_nQueryHandle, vecOrigin, VisibilityInputs.m_flProxyRadius );
		// Retail remaps the proxy RADIUS, not the returned pixel fraction.
		flVisibility = RemapTerrorParticleVisibility( VisibilityInputs.m_flProxyRadius,
			VisibilityInputs.m_flInputMin, VisibilityInputs.m_flInputMax ) * flPixelVisibility;
	}
	if ( m_flVisibilityInputDotMin != m_flVisibilityInputDotMax )
	{
		Vector vecPosition;
		pParticles->GetControlPointAtTime( nControlPoint, pParticles->m_flCurTime, &vecPosition );
		Vector vecForward = pParticles->ControlPoint( nControlPoint ).m_ForwardVector;
		Vector vecDelta = vecPosition - g_pParticleSystemMgr->Query()->GetLocalPlayerPos();
		// C35F4C0 computes the length in x87, rounds it to float, then
		// divides by length + the native FLT_EPSILON before scaling XYZ.
		float flLength = (float)sqrt( ( (double)vecDelta.x * vecDelta.x +
			(double)vecDelta.y * vecDelta.y ) + (double)vecDelta.z * vecDelta.z );
		float flScale = 1.0f / ( flLength + FLT_EPSILON );
		vecDelta *= flScale;
		float flDot = ( vecDelta.y * vecForward.y + vecDelta.x * vecForward.x ) + vecDelta.z * vecForward.z;
		flVisibility = RemapTerrorParticleVisibility( flDot,
			m_flVisibilityInputDotMin, m_flVisibilityInputDotMax ) * flVisibility;
	}
	if ( m_flVisibilityInputDistanceMin != m_flVisibilityInputDistanceMax )
	{
		Vector vecCamera;
		if ( pScreenSpace )
			pScreenSpace->GetWorldSpaceCameraPosition( pRenderContext, &vecCamera );
		else
			pRenderContext->GetWorldSpaceCameraPosition( &vecCamera );
		Vector vecDelta = vecOrigin - vecCamera;
		float flDistance = sqrtf( ( vecDelta.y * vecDelta.y + vecDelta.z * vecDelta.z ) + vecDelta.x * vecDelta.x );
		flVisibility = RemapTerrorParticleVisibility( flDistance,
			m_flVisibilityInputDistanceMin, m_flVisibilityInputDistanceMax ) * flVisibility;
	}
	pContext->m_VisibilityData.m_flAlphaVisibility =
		( VisibilityInputs.m_flAlphaScaleMax - VisibilityInputs.m_flAlphaScaleMin ) * flVisibility + VisibilityInputs.m_flAlphaScaleMin;
	pContext->m_VisibilityData.m_flRadiusVisibility =
		( VisibilityInputs.m_flRadiusScaleMax - VisibilityInputs.m_flRadiusScaleMin ) * flVisibility + VisibilityInputs.m_flRadiusScaleMin;
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif

int CTerror_OP_RenderSprites::GetParticlesToRender( CParticleCollection *pParticles, void *pContext, int nFirstParticle, int nRemainingVertices, int nRemainingIndices, int *pVertsUsed, int *pIndicesUsed ) const
{
	// Batch counts are collected before the caller binds or begins its mesh.
	IMaterial *pMaterial = PrepareMaterial( pParticles );
	if ( !UsesLocalCardRenderer( pMaterial ) )
		return m_pNativeRenderer->GetParticlesToRender( pParticles, GetNativeContext( pContext ), nFirstParticle, nRemainingVertices, nRemainingIndices, pVertsUsed, pIndicesUsed );
	int nCount = min( pParticles->m_nActiveParticles - nFirstParticle, min( nRemainingVertices / 4, nRemainingIndices / 6 ) );
	*pVertsUsed = nCount * 4;
	*pIndicesUsed = nCount * 6;
	return nCount;
}

void CTerror_OP_RenderSprites::RenderSprite( CMeshBuilder &meshBuilder, TerrorSpriteRenderInfo_t &info, const ParticleRenderData_t &renderData ) const
{
	int nGroup = renderData.m_nIndex / 4;
	int nLane = renderData.m_nIndex & 3;
	Vector position( info.Read( TerrorSpriteRenderInfo_t::XYZ, nGroup, nLane ),
		info.Read( TerrorSpriteRenderInfo_t::XYZ, nGroup, nLane, 1 ), info.Read( TerrorSpriteRenderInfo_t::XYZ, nGroup, nLane, 2 ) );
	unsigned char r = FastFToC( info.Read( TerrorSpriteRenderInfo_t::TINT, nGroup, nLane ) );
	unsigned char g = FastFToC( info.Read( TerrorSpriteRenderInfo_t::TINT, nGroup, nLane, 1 ) );
	unsigned char b = FastFToC( info.Read( TerrorSpriteRenderInfo_t::TINT, nGroup, nLane, 2 ) );
	float flRotation = info.Read( TerrorSpriteRenderInfo_t::ROTATION, nGroup, nLane );
	float flYaw = info.Read( TerrorSpriteRenderInfo_t::YAW, nGroup, nLane );

	// Original C:3C9060/3CC770 transport Glow as raw floats in TEXCOORD5;
	// only primary RGB/alpha are byte-quantized. Never substitute Tint here.
	Vector glow( 1.0f, 1.0f, 1.0f );
	if ( info.m_bWriteGlow && m_bHasGlowRGBProducer )
	{
		glow.Init( info.Read( TerrorSpriteRenderInfo_t::GLOW_RED, nGroup, nLane ),
			info.Read( TerrorSpriteRenderInfo_t::GLOW_GREEN, nGroup, nLane ),
			info.Read( TerrorSpriteRenderInfo_t::GLOW_BLUE, nGroup, nLane ) );
	}

	const SheetSequenceSample_t *pSample = &s_TerrorDefaultSheetSample;
	const SheetSequenceSample_t *pSample2 = &s_TerrorDefaultSheetSample;
	if ( info.m_pSheet && info.m_bDualSequence )
	{
		GetTerrorDualSheetSamples( info.m_pSheet,
			info.Read( TerrorSpriteRenderInfo_t::SEQUENCE, nGroup, nLane ),
			info.m_nStride[TerrorSpriteRenderInfo_t::SEQUENCE2] != 0 ?
				info.Read( TerrorSpriteRenderInfo_t::SEQUENCE2, nGroup, nLane ) : 0.0f,
			info.m_nStride[TerrorSpriteRenderInfo_t::SEQUENCE2] != 0,
			info.m_pParticles->m_flCurTime, info.Read( TerrorSpriteRenderInfo_t::CREATION_TIME, nGroup, nLane ),
			info.m_flAgeScale, info.m_flAgeScale2, m_bAnimateInFPS, &pSample, &pSample2 );
	}
	else if ( info.m_pSheet )
	{
		int nSequence = info.Read( TerrorSpriteRenderInfo_t::SEQUENCE, nGroup, nLane );
		float flAgeScale = m_flAnimationRate * TERROR_SHEET_SAMPLE_COUNT;
		if ( m_bAnimateInFPS )
			flAgeScale /= info.m_pSheet->m_flFrameSpan[nSequence];
		// Original standard C3C95D0 and outlined C3C9060 both ignore
		// animation_fit_lifetime and sample only the first sequence here.
		pSample = GetTerrorSheetSample( info.m_pSheet, info.m_pParticles->m_flCurTime -
			info.Read( TerrorSpriteRenderInfo_t::CREATION_TIME, nGroup, nLane ), flAgeScale, nSequence );
	}
	const SequenceSampleTextureCoords_t &sample = pSample->m_TextureCoordData[0];
	const SequenceSampleTextureCoords_t &secondImage = pSample->m_TextureCoordData[1];
	static const float corners[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
	for ( int i = 0; i < 4; ++i )
	{
		meshBuilder.Position3fv( position.Base() );
		meshBuilder.Color4ub( r, g, b, renderData.m_nAlpha );
		meshBuilder.TexCoord4f( 0, sample.m_fLeft_U0, sample.m_fTop_V0, sample.m_fRight_U0, sample.m_fBottom_V0 );
		meshBuilder.TexCoord4f( 1, sample.m_fLeft_U1, sample.m_fTop_V1, sample.m_fRight_U1, sample.m_fBottom_V1 );
		meshBuilder.TexCoord4f( 2, pSample->m_fBlendFactor, flRotation, renderData.m_flRadius, flYaw );
		meshBuilder.TexCoord2f( 3, corners[i][0], corners[i][1] );
		meshBuilder.TexCoord4f( 4, secondImage.m_fLeft_U0, secondImage.m_fTop_V0, secondImage.m_fRight_U0, secondImage.m_fBottom_V0 );
		if ( info.m_bWriteGlow )
			meshBuilder.TexCoord4f( 5, glow.x, glow.y, glow.z, 1.0f );
		else if ( info.m_bDualSequence )
		{
			const SequenceSampleTextureCoords_t &sequence2 = pSample2->m_TextureCoordData[0];
			meshBuilder.TexCoord4f( 5, sequence2.m_fLeft_U0, sequence2.m_fTop_V0, sequence2.m_fRight_U0, sequence2.m_fBottom_V0 );
			meshBuilder.TexCoord4f( 6, sequence2.m_fLeft_U1, sequence2.m_fTop_V1, sequence2.m_fRight_U1, sequence2.m_fBottom_V1 );
			meshBuilder.TexCoord4f( 7, pSample2->m_fBlendFactor, 0.0f, 0.0f, 0.0f );
		}
		meshBuilder.AdvanceVertex();
	}
	meshBuilder.FastIndex( info.m_nVertexOffset );
	meshBuilder.FastIndex( info.m_nVertexOffset + 1 );
	meshBuilder.FastIndex( info.m_nVertexOffset + 2 );
	meshBuilder.FastIndex( info.m_nVertexOffset );
	meshBuilder.FastIndex( info.m_nVertexOffset + 2 );
	meshBuilder.FastIndex( info.m_nVertexOffset + 3 );
	info.m_nVertexOffset += 4;
}

void CTerror_OP_RenderSprites::Render( IMatRenderContext *pRenderContext, CParticleCollection *pParticles, void *pContext ) const
{
	IMaterial *pMaterial = PrepareMaterial( pParticles );
	CScopedTerrorParticleScreenSpace screenSpace( pRenderContext, m_bScreenSpace );
	bool bDualSequence;
	if ( !UsesLocalCardRenderer( pMaterial, &bDualSequence ) )
	{
		m_pNativeRenderer->Render( pRenderContext, pParticles, GetNativeContext( pContext ) );
		return;
	}
	Context_t *pCtx = static_cast< Context_t * >( pContext );
	bool bWriteGlow = !Q_stricmp( pMaterial->GetShaderName(), "TerrorSpriteCard" );
	if ( bWriteGlow )
		UpdateVisibility( pParticles, pCtx );
	else
		UpdateNativeVisibility( pParticles, pCtx, pRenderContext, &screenSpace );
	SetOrientation( pParticles, pCtx, pMaterial );
	pRenderContext->Bind( pMaterial );
	TerrorSpriteRenderInfo_t info;
	if ( bDualSequence )
		info.InitDualSequenceRates( m_flAnimationRate, m_flAnimationRate2 );

	int nParticles;
	const ParticleRenderData_t *pRenderList = pParticles->GetRenderList( pRenderContext, true, &nParticles, &pCtx->m_VisibilityData );
	if ( !nParticles )
		return;
	int nMaxParticles = min( pRenderContext->GetMaxVerticesToRender( pMaterial ) / 4, pRenderContext->GetMaxIndicesToRender() / 6 );
	// Native C3C9870 batches without a capacity guard, even at zero.

	info.Init( pParticles, 0, m_bHasGlowRGBProducer, bWriteGlow, bDualSequence );
	while ( nParticles )
	{
		int nBatchCount = min( nMaxParticles, nParticles );
		nParticles -= nBatchCount;
		IMesh *pMesh = pRenderContext->GetDynamicMesh( true );
		CMeshBuilder meshBuilder;
		meshBuilder.Begin( pMesh, MATERIAL_TRIANGLES, nBatchCount * 4, nBatchCount * 6 );
		info.m_nVertexOffset = 0;
		for ( int i = 0; i < nBatchCount; ++i )
		{
			--pRenderList;
			// Original C:3C9060 omits zero-alpha particles before vertex output.
			if ( pRenderList->m_nAlpha )
				RenderSprite( meshBuilder, info, *pRenderList );
		}
		g_pParticleSystemMgr->TallyParticlesRendered( info.m_nVertexOffset * 3, ( info.m_nVertexOffset / 4 ) * 6 * 3 );
		meshBuilder.End();
		pMesh->Draw();
	}
}

void CTerror_OP_RenderSprites::RenderUnsorted( CParticleCollection *pParticles, void *pContext, IMatRenderContext *pRenderContext, CMeshBuilder &meshBuilder, int nVertexOffset, int nFirstParticle, int nParticleCount ) const
{
	// The batch count phase admitted this material before the caller began
	// its mesh. Do not prepare materials or drain queued draws while borrowed.
	IMaterial *pMaterial = pParticles->m_pDef->GetMaterial();
	bool bDualSequence;
	if ( !UsesLocalCardRenderer( pMaterial, &bDualSequence ) )
	{
		m_pNativeRenderer->RenderUnsorted( pParticles, GetNativeContext( pContext ), pRenderContext, meshBuilder, nVertexOffset, nFirstParticle, nParticleCount );
		return;
	}
	Context_t *pCtx = static_cast< Context_t * >( pContext );
	bool bWriteGlow = !Q_stricmp( pMaterial->GetShaderName(), "TerrorSpriteCard" );
	if ( bWriteGlow )
		UpdateVisibility( pParticles, pCtx );
	else
		UpdateNativeVisibility( pParticles, pCtx, pRenderContext, NULL );
	SetOrientation( pParticles, pCtx, pMaterial );
	TerrorSpriteRenderInfo_t info;
	if ( bDualSequence )
		info.InitDualSequenceRates( m_flAnimationRate, m_flAnimationRate2 );
	int nParticles;
	const ParticleRenderData_t *pEnd = pParticles->GetRenderList( pRenderContext, false, &nParticles, &pCtx->m_VisibilityData );
	const ParticleRenderData_t *pRenderList = pEnd - nParticles + nFirstParticle;
	info.Init( pParticles, nVertexOffset, m_bHasGlowRGBProducer, bWriteGlow, bDualSequence );
	for ( int i = 0; i < nParticleCount; ++i )
	{
		// Batch callers reserve four vertices per particle. Keep even zero-alpha
		// slots so the following renderer's nVertexOffset remains valid; the
		// SpriteCard VS performs the original alpha-byte radius cull.
		RenderSprite( meshBuilder, info, pRenderList[i] );
	}
}

void RegisterTerrorParticleRenderers()
{
	CUtlVector< IParticleOperatorDefinition * > &renderers = g_pParticleSystemMgr->GetAvailableParticleOperatorList( FUNCTION_RENDERER );
	FOR_EACH_VEC( renderers, i )
	{
		if ( Q_stricmp( renderers[i]->GetName(), "render_animated_sprites" ) )
			continue;
		if ( renderers[i] == &s_CTerror_OP_RenderSpritesFactory )
			return;
		s_pNativeSpriteFactory = renderers[i];
		renderers[i] = &s_CTerror_OP_RenderSpritesFactory;
		return;
	}
	Error( "Cannot register TerrorSpriteCard: native render_animated_sprites factory is missing.\n" );
}
