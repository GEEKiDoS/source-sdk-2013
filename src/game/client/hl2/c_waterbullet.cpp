//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#include "cbase.h"
#include "particles_simple.h"

// memdbgon must be the last include file in a .cpp file!!!
#if defined( L4D2 )
#include <stdlib.h>
#endif
#include "tier0/memdbgon.h"

class C_WaterBullet : public C_BaseAnimating
{
public:

	DECLARE_CLIENTCLASS();
	DECLARE_CLASS( C_WaterBullet, C_BaseAnimating );

	C_WaterBullet( void )
	{
#if defined( L4D2 )
		// NativeA6930 inlines this own-class producer after actual BA ctor.
		AddToEntityList( ENTITY_LIST_SIMULATE );
#endif
	};
	~C_WaterBullet( void ) {};

	void OnDataChanged( DataUpdateType_t updateType )
	{
		BaseClass::OnDataChanged( updateType );

		if ( updateType == DATA_UPDATE_CREATED )
		{
			m_pEmitter = CSimpleEmitter::Create( "FX_Bubble" );
			m_pEmitter->SetSortOrigin( GetAbsOrigin() );

			m_vecLastOrigin = GetAbsOrigin();
		}
	}

#define	BUBBLES_PER_INCH	0.2

#if defined( L4D2 )
	virtual bool Simulate() OVERRIDE;
#else
	void AddEntity( void )
	{
		Vector	direction = GetAbsOrigin() - m_vecLastOrigin;
		float	flDist = VectorNormalize( direction );

		int	numBubbles = (int) ( flDist * BUBBLES_PER_INCH );

		if ( numBubbles < 1 )
			numBubbles = 1;

		// Make bubbles
		SimpleParticle *sParticle;

		Vector	offset;

		for ( int i = 0; i < numBubbles; i++ )
		{
			offset = m_vecLastOrigin + ( direction * ( flDist / numBubbles ) * i ) + RandomVector( -2.5f, 2.5f );

			sParticle = (SimpleParticle *) m_pEmitter->AddParticle( sizeof(SimpleParticle), m_pEmitter->GetPMaterial( "effects/bubble" ), offset );

			if ( sParticle )
			{
				sParticle->m_flLifetime		= 0.0f;
				sParticle->m_flDieTime		= random->RandomFloat( 0.75f, 1.25f );

				sParticle->m_flRoll			= 0;
				sParticle->m_flRollDelta	= 0;

				unsigned char color = random->RandomInt( 128, 255 );

				sParticle->m_uchColor[0]	= color;
				sParticle->m_uchColor[1]	= color;
				sParticle->m_uchColor[2]	= color;
				sParticle->m_uchStartAlpha	= 255;
				sParticle->m_uchEndAlpha	= 0;
				sParticle->m_uchStartSize	= random->RandomInt( 1, 2 );
				sParticle->m_uchEndSize		= sParticle->m_uchStartSize;
				
				sParticle->m_vecVelocity	= ( direction * 64.0f ) + Vector( 0, 0, 32 );
			}
			
			sParticle = (SimpleParticle *) m_pEmitter->AddParticle( sizeof(SimpleParticle), m_pEmitter->GetPMaterial( "effects/splash2" ), offset );

			if ( sParticle )
			{
				sParticle->m_flLifetime		= 0.0f;
				sParticle->m_flDieTime		= 0.2f;

				sParticle->m_flRoll			= random->RandomInt( 0, 360 );
				sParticle->m_flRollDelta	= random->RandomInt( -4, 4 );

				unsigned char color = random->RandomInt( 200, 255 );

				sParticle->m_uchColor[0]	= color;
				sParticle->m_uchColor[1]	= color;
				sParticle->m_uchColor[2]	= color;
				sParticle->m_uchStartAlpha	= 128;
				sParticle->m_uchEndAlpha	= 0;
				sParticle->m_uchStartSize	= 2;
				sParticle->m_uchEndSize		= sParticle->m_uchStartSize * 4;
				
				sParticle->m_vecVelocity	= ( direction * 64.0f ) + Vector( 0, 0, 32 );
			}
		}

		// Save our last position
		m_vecLastOrigin = GetAbsOrigin();

		BaseClass::AddEntity();
	}
#endif

	bool ShouldDraw( void ) { return true; }

private:
	C_WaterBullet( const C_WaterBullet & );

	CSmartPtr<CSimpleEmitter> m_pEmitter;

	Vector		m_vecLastOrigin;
};

IMPLEMENT_CLIENTCLASS_DT( C_WaterBullet, DT_WaterBullet, CWaterBullet )
END_RECV_TABLE()

#if defined( L4D2 )
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
extern "C" int L4D2_NormalizeWaterBulletAndCount( Vector *direction, float *distance );
extern "C" void L4D2_StoreParticleAggregationCenter( Vector *destination, const Vector *origin );

bool C_WaterBullet::Simulate()
{
	Vector direction = GetAbsOrigin() - m_vecLastOrigin;
	float distance;
	int count = L4D2_NormalizeWaterBulletAndCount( &direction, &distance );
	if ( count < 1 ) count = 1;
	distance = distance / static_cast< float >( count );
	for ( int i = 0; i < count; ++i )
	{
		const float xJitter = ( static_cast< float >( rand() ) * 0.000030518509f ) * 5.0f - 2.5f;
		const float yJitter = ( static_cast< float >( rand() ) * 0.000030518509f ) * 5.0f - 2.5f;
		const float zJitter = ( static_cast< float >( rand() ) * 0.000030518509f ) * 5.0f - 2.5f;
		const float step = static_cast< float >( i );
		Vector offset;
		offset.x = ( direction.x * distance ) * step + m_vecLastOrigin.x + xJitter;
		offset.y = ( direction.y * distance ) * step + m_vecLastOrigin.y + yJitter;
		offset.z = ( direction.z * distance ) * step + m_vecLastOrigin.z + zJitter;

		CSimpleEmitter *emitter = m_pEmitter.operator->();
		const auto bubbleMaterial = emitter->GetPMaterial( "effects/bubble" );
		SimpleParticle *particle = (SimpleParticle *)emitter->AddParticle( sizeof(SimpleParticle), bubbleMaterial, offset );
		if ( particle )
		{
			particle->m_flLifetime = 0.0f;
			particle->m_flDieTime = random->RandomFloat( 0.75f, 1.25f );
			particle->m_flRoll = 0.0f;
			particle->m_flRollDelta = 0.0f;
			const unsigned char color = static_cast< unsigned char >( random->RandomInt( 128, 255 ) );
			particle->m_uchColor[0] = color;
			particle->m_uchColor[1] = color;
			particle->m_uchColor[2] = color;
			particle->m_uchStartAlpha = 255;
			particle->m_uchEndAlpha = 0;
			const unsigned char size = static_cast< unsigned char >( random->RandomInt( 1, 2 ) );
			particle->m_uchStartSize = size;
			particle->m_uchEndSize = size;
			particle->m_vecVelocity.x = direction.x * 64.0f;
			particle->m_vecVelocity.y = direction.y * 64.0f;
			particle->m_vecVelocity.z = direction.z * 64.0f + 32.0f;
		}

		emitter = m_pEmitter.operator->();
		const auto splashMaterial = emitter->GetPMaterial( "effects/splash2" );
		particle = (SimpleParticle *)emitter->AddParticle( sizeof(SimpleParticle), splashMaterial, offset );
		if ( particle )
		{
			particle->m_flLifetime = 0.0f;
			particle->m_flDieTime = 0.2f;
			particle->m_flRoll = static_cast< float >( random->RandomInt( 0, 360 ) );
			particle->m_flRollDelta = static_cast< float >( random->RandomInt( -4, 4 ) );
			const unsigned char color = static_cast< unsigned char >( random->RandomInt( 200, 255 ) );
			particle->m_uchColor[0] = color;
			particle->m_uchColor[1] = color;
			particle->m_uchColor[2] = color;
			particle->m_uchStartAlpha = 128;
			particle->m_uchEndAlpha = 0;
			particle->m_uchStartSize = 2;
			particle->m_uchEndSize = 8;
			particle->m_vecVelocity.x = direction.x * 64.0f;
			particle->m_vecVelocity.y = direction.y * 64.0f;
			particle->m_vecVelocity.z = direction.z * 64.0f + 32.0f;
		}
	}
	L4D2_StoreParticleAggregationCenter( &m_vecLastOrigin, &GetAbsOrigin() );
	BaseClass::Simulate();
	return true;
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
#endif
