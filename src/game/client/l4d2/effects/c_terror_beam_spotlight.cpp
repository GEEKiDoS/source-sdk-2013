#include "cbase.h"
#include "beam_shared.h"
#include "dlight.h"
#include "iefx.h"
#include "physics.h"
#include "cliententitylist.h"
#include <xmmintrin.h>
#include "tier0/memdbgon.h"

static int SpotlightTruncate( float value ) { return _mm_cvttss_si32( _mm_set_ss( value ) ); }

extern "C" double L4D2_GlowNormalize( Vector *direction );

class C_BeamSpotLight : public C_BaseEntity
{
public:
	DECLARE_CLASS( C_BeamSpotLight, C_BaseEntity );
	DECLARE_CLIENTCLASS();
	datamap_t *GetDataDescMap() OVERRIDE { return NULL; } // Native primary17 C6C3D0.
	C_BeamSpotLight() : m_flSpotlightCurLength( 0.0f ), m_flSpotlightLightScale( 100.0f ), m_pDynamicLight( NULL ), m_flLastUpdateTime( 0.0f )
	{
		m_vecSpotlightTarget.Init(); m_vecSpotlightCurrentPos.Init(); m_angRotation.Init();
	}
	~C_BeamSpotLight() { DestroyBeam(); }
	bool ShouldDraw() OVERRIDE { return false; } // Native renderable slot3 C6C550.
	void Release() OVERRIDE { DestroyBeam(); BaseClass::Release(); } // Native primary44 C6C780.
	void OnDataChanged( DataUpdateType_t type ) OVERRIDE
	{
		BaseClass::OnDataChanged( type );
		if ( type == DATA_UPDATE_CREATED ) m_flSpotlightCurLength = m_flSpotlightMaxLength;
		SetNextClientThink( CLIENT_THINK_ALWAYS );
	}
	void ClientThink() OVERRIDE;
private:
	struct TraceCache_t { Vector origin; float length; };
	void DestroyBeam() { if ( m_hBeam.Get() ) { ClientEntityList().AddToDeleteList( m_hBeam.Get() ); m_hBeam = NULL; } }
	void ComputeEndpoint( Vector &endpoint );
	void CreateBeam();
	void UpdateBeamEffects();
	int m_nHaloIndex, m_nRotationAxis;
	float m_flRotationSpeed;
	bool m_bSpotlightOn, m_bHasDynamicLight, m_bNoFog;
	float m_flSpotlightMaxLength, m_flSpotlightGoalWidth, m_flHDRColorScale;
	Vector m_vecSpotlightTarget, m_vecSpotlightCurrentPos;
	QAngle m_angRotation;
	CHandle< C_Beam > m_hBeam;
	float m_flSpotlightCurLength, m_flSpotlightLightScale;
	dlight_t *m_pDynamicLight;
	float m_flLastUpdateTime;
	CUtlVector< TraceCache_t > m_TraceCache;
};
LINK_ENTITY_TO_CLASS( beam_spotlight, C_BeamSpotLight );
IMPLEMENT_CLIENTCLASS_DT( C_BeamSpotLight, DT_BeamSpotlight, CBeamSpotlight )
	RecvPropInt( RECVINFO( m_nHaloIndex ) ),
	RecvPropBool( RECVINFO( m_bSpotlightOn ) ),
	RecvPropBool( RECVINFO( m_bHasDynamicLight ) ),
	RecvPropBool( RECVINFO( m_bNoFog ) ),
	RecvPropFloat( RECVINFO( m_flSpotlightMaxLength ) ),
	RecvPropFloat( RECVINFO( m_flSpotlightGoalWidth ) ),
	RecvPropFloat( RECVINFO( m_flHDRColorScale ) ),
	RecvPropInt( RECVINFO( m_nRotationAxis ) ),
	RecvPropFloat( RECVINFO( m_flRotationSpeed ) ),
END_RECV_TABLE()

#pragma float_control( precise, on, push )
#if defined( _MSC_VER ) && _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
void C_BeamSpotLight::ComputeEndpoint( Vector &endpoint )
{
	const Vector origin = GetAbsOrigin();
	matrix3x4_t entity, rotation, transform;
	AngleMatrix( GetAbsAngles(), entity );
	AngleMatrix( m_angRotation, rotation );
	L4D2ClientConcatTransforms( rotation, entity, transform ); // Native C35FDD0, rotation first.
	int bucket = -1;
	// The wire field has two bits; value3 is representable. Main's minimal
	// undefined-step rule skips only its out-of-QAngle rotation/cache access.
	if ( m_TraceCache.Count() && m_nRotationAxis >= 0 && m_nRotationAxis < 3 )
	{
		bucket = SpotlightTruncate( ( m_angRotation[m_nRotationAxis] * 64.0f ) * 0.0027777778f ) & 63;
		const TraceCache_t &cache = m_TraceCache[bucket];
		const Vector delta = cache.origin - origin;
		const float lengthSqr = ( delta.y * delta.y + delta.x * delta.x ) + delta.z * delta.z;
		if ( cache.length > 0.0f && lengthSqr < 1.0f )
		{
			Vector rotated;
			VectorRotate( Vector( cache.length, 0, 0 ), transform, rotated );
			endpoint = origin + rotated;
			return;
		}
	}
	Vector offset;
	VectorRotate( Vector( m_flSpotlightMaxLength * 2.0f, 0, 0 ), transform, offset );
	trace_t trace;
	UTIL_TraceLine( origin, origin + offset, 0x400B, this, COLLISION_GROUP_NONE, &trace );
	if ( bucket >= 0 )
	{
		TraceCache_t &cache = m_TraceCache[bucket];
		const Vector delta = trace.endpos - origin;
		cache.length = sqrtf( ( delta.y * delta.y + delta.z * delta.z ) + delta.x * delta.x );
		cache.origin = origin;
	}
	endpoint = trace.endpos;
}
void C_BeamSpotLight::CreateBeam()
{
	ComputeEndpoint( m_vecSpotlightTarget );
	C_Beam *beam = C_Beam::BeamCreate( m_bNoFog ? "sprites/glow_test02_nofog.vmt" : "sprites/glow_test02.vmt", m_flSpotlightGoalWidth );
	if ( beam ) ClientEntityList().AddNonNetworkableEntity( beam ); // Native C6CAD0 -> CEA8C0 before weak handle publication.
	m_hBeam = beam;
	if ( !beam ) return; // native unchecked allocation dereference: minimal skip
	beam->SetHDRColorScale( m_flHDRColorScale );
	const color32 color = GetRenderColor();
	beam->SetColor( color.r, color.g, color.b );
	beam->SetHaloTexture( m_nHaloIndex );
	beam->SetHaloScale( 60.0f );
	beam->SetEndWidth( m_flSpotlightGoalWidth > 102.3f ? 102.3f : m_flSpotlightGoalWidth );
	beam->SetBeamFlags( 640 );
	beam->SetBrightness( 64 );
	beam->SetNoise( 0 );
	beam->SetMinCPULevel( GetMinCPULevel() );
	beam->SetMaxCPULevel( GetMaxCPULevel() );
	beam->SetMinGPULevel( GetMinGPULevel() );
	beam->SetMaxGPULevel( GetMaxGPULevel() );
	beam->PointsInit( GetAbsOrigin(), m_vecSpotlightTarget );
}
void C_BeamSpotLight::UpdateBeamEffects()
{
	C_Beam *beam = m_hBeam.Get();
	float fade;
	if ( m_flSpotlightCurLength > m_flSpotlightMaxLength * 2.0f ) { SetRenderColorA( 0 ); fade = m_flSpotlightMaxLength; }
	else if ( !( m_flSpotlightCurLength > m_flSpotlightMaxLength ) ) { SetRenderColorA( 1 ); fade = m_flSpotlightCurLength; }
	else { SetRenderColorA( static_cast< unsigned char >( SpotlightTruncate( 1.0f - ( m_flSpotlightCurLength - m_flSpotlightMaxLength ) / m_flSpotlightMaxLength ) ) ); fade = m_flSpotlightMaxLength; }
	if ( beam ) beam->SetFadeLength( fade );
	float width = ( m_flSpotlightCurLength / m_flSpotlightMaxLength ) * m_flSpotlightGoalWidth;
	if ( width > 102.3f ) width = 102.3f;
	else if ( width < 0.0f ) width = 0.0f;
	if ( beam ) beam->SetEndWidth( width );
	if ( m_bHasDynamicLight )
	{
		const double scale = static_cast< double >( width ) * 1.8;
		m_flSpotlightLightScale = static_cast< float >( scale );
		if ( scale > 0.0 )
		{
			const color32 color = GetRenderColor();
			const float alpha = static_cast< float >( color.a ) * 0.0039215689f;
			const unsigned char r = static_cast< unsigned char >( SpotlightTruncate( color.r * alpha ) );
			const unsigned char g = static_cast< unsigned char >( SpotlightTruncate( color.g * alpha ) );
			const unsigned char b = static_cast< unsigned char >( SpotlightTruncate( color.b * alpha ) );
			if ( r || g || b )
			{
				if ( !m_pDynamicLight || m_pDynamicLight->key != entindex() ) m_pDynamicLight = effects->CL_AllocDlight( entindex() );
				if ( m_pDynamicLight )
				{
					m_pDynamicLight->radius = m_flSpotlightLightScale * 3.0f;
					m_pDynamicLight->origin = GetAbsOrigin() + Vector( 0, 0, 5 );
					m_pDynamicLight->die = gpGlobals->curtime + 0.05f;
					m_pDynamicLight->color.r = r; m_pDynamicLight->color.g = g; m_pDynamicLight->color.b = b; m_pDynamicLight->color.exponent = 0;
				}
			}
		}
	}
}
void C_BeamSpotLight::ClientThink()
{
	float dt = gpGlobals->curtime - m_flLastUpdateTime;
	if ( m_flLastUpdateTime == 0.0f ) dt = 0.0f;
	m_flLastUpdateTime = gpGlobals->curtime;
	if ( m_hBeam.Get() )
	{
		if ( !m_bSpotlightOn ) { DestroyBeam(); SetNextClientThink( CLIENT_THINK_NEVER ); return; }
	}
	else
	{
		if ( !m_bSpotlightOn ) { SetNextClientThink( CLIENT_THINK_NEVER ); return; }
		CreateBeam();
	}
	if ( m_flRotationSpeed != 0.0f )
	{
		if ( m_nRotationAxis >= 0 && m_nRotationAxis < 3 )
		{
			float &angle = m_angRotation[m_nRotationAxis];
			angle = m_flRotationSpeed * dt + angle;
			angle = static_cast< unsigned short >( SpotlightTruncate( angle * 182.04445f ) ) * 0.0054931641f;
		}
		if ( !m_TraceCache.Count() )
		{
			m_TraceCache.SetCount( 64 );
			FOR_EACH_VEC( m_TraceCache, i ) { m_TraceCache[i].origin.Init(); m_TraceCache[i].length = -1.0f; }
		}
	}
	ComputeEndpoint( m_vecSpotlightCurrentPos );
	C_Beam *beam = m_hBeam.Get();
	if ( beam ) { beam->SetAbsOrigin( GetAbsOrigin() ); beam->SetNetworkOrigin( GetAbsOrigin() ); beam->SetEndPos( m_vecSpotlightCurrentPos ); }
	Vector delta = m_vecSpotlightCurrentPos - GetAbsOrigin();
	const double length = L4D2_GlowNormalize( &delta );
	m_flSpotlightCurLength = static_cast< float >( static_cast< double >( m_flSpotlightCurLength ) * 0.6 + length * 0.4 );
	UpdateBeamEffects();
	beam = m_hBeam.Get();
	if ( beam ) beam->RelinkBeam();
	if ( !GetMoveParent() && m_flRotationSpeed == 0.0f ) SetNextClientThink( CLIENT_THINK_NEVER );
}
#pragma float_control( pop )
