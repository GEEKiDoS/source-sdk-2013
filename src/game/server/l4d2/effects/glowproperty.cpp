//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/effects/glowproperty.h"

#include "tier0/memdbgon.h"
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif

// S:105BE0, sendtable107F9D58. These four integers are signed 32-bit
// fields even though the packed color's copying proxy is unsigned.
BEGIN_SEND_TABLE_NOBASE( CGlowProperty, DT_GlowProperty )
	SendPropInt( SENDINFO( m_iGlowType ), 32, 0 ),
	SendPropInt( SENDINFO( m_nGlowRange ), 32, 0 ),
	SendPropInt( SENDINFO( m_nGlowRangeMin ), 32, 0 ),
	SendPropInt( SENDINFO( m_glowColorOverride ), 32, 0, g_StandardSendProxies.m_UInt32ToInt32 ),
	SendPropBool( SENDINFO( m_bFlashing ) ),
END_SEND_TABLE()

// S:105EC0, datamap1076F684: only type is saved. Do not add the other
// network fields or the client-only team/eligibility/cache to save games.
BEGIN_DATADESC_NO_BASE( CGlowProperty )
	DEFINE_FIELD( m_iGlowType, FIELD_INTEGER ),
END_DATADESC()

// S:105E80. Color's own constructor zeros its packed value before Init.
CGlowProperty::CGlowProperty()
{
	Init( NULL );
}

// S:105CD0. In particular, do not call SetColor here: native Init leaves
// the cached RGB floats alone, even while resetting the packed override.
void CGlowProperty::Init( CBaseEntity *pEntity )
{
	m_pOuter = pEntity;
	m_iGlowType = 0;
	m_nGlowRange = 0;
	m_nGlowRangeMin = 0;
	m_iTeam = -1;
	m_glowColorOverride = Color( 0, 0, 0, 0 );
	m_bEnabled = false;
	m_bFlashing = false;
}

// S:105D60. CBaseAnimating invokes this before its base destruction.
void CGlowProperty::Term()
{
	Init( NULL );
}

// S:105D70. Even an unchanged zero type clears the eligibility byte.
void CGlowProperty::SetType( int type )
{
	m_iGlowType = type;
	if ( m_iGlowType == 0 )
		m_bEnabled = false;
}

// S:105E10. The cached RGB is refreshed even when the packed color did
// not change. Native uses multiplication by this exact float constant.
void CGlowProperty::SetColor( const Color &color )
{
	m_glowColorOverride = color;
	m_glowColor.x = static_cast< float >( color.r() ) * 0.0039215688593685626983642578125f;
	m_glowColor.y = static_cast< float >( color.g() ) * 0.0039215688593685626983642578125f;
	m_glowColor.z = static_cast< float >( color.b() ) * 0.0039215688593685626983642578125f;
}

// S:105B30. Preserve unset-override in/out semantics, flashing phase,
// float multiplication, truncation and output ordering.
void CGlowProperty::GetColor( unsigned char *r, unsigned char *g, unsigned char *b ) const
{
	const Color &color = m_glowColorOverride.Get();
	if ( color.GetRawColor() != 0 )
	{
		*r = color.r();
		*g = color.g();
		*b = color.b();
	}
	if ( m_bFlashing )
	{
		const float scale = L4D2_GlowFlashScale( gpGlobals->curtime );
		*r = static_cast< unsigned char >( static_cast< int >( static_cast< float >( *r ) * scale ) );
		*g = static_cast< unsigned char >( static_cast< int >( static_cast< float >( *g ) * scale ) );
		*b = static_cast< unsigned char >( static_cast< int >( static_cast< float >( *b ) * scale ) );
	}
}

#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
