//========= Copyright Valve Corporation, All rights reserved. ============//
// Pinned L4D2 client: DT_GlowProperty (0x10109B10), not SDK2013 outlines.

#include "cbase.h"
#include "l4d2/effects/glowproperty.h"
#include "l4d2/effects/terror_glow_property.h"
#include "recvproxy.h"
#include "collisionutils.h"

extern "C" bool L4D2_GlowDistanceWithinRange( const Vector *point,
	const Vector *nearestPoint, int signedRangeSquared );
extern "C" double L4D2_GlowNormalize( Vector *direction );

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif

namespace
{
CUtlVector< CGlowProperty * > s_GlowProperties;

void RecvProxy_GlowType( const CRecvProxyData *pData, void *pStruct, void *pOut )
{
	static_cast< CGlowProperty * >( pStruct )->SetType( pData->m_Value.m_Int );
}

void RecvProxy_GlowColor( const CRecvProxyData *pData, void *pStruct, void *pOut )
{
	const unsigned int packed = static_cast< unsigned int >( pData->m_Value.m_Int );
	static_cast< CGlowProperty * >( pStruct )->SetColor( Color(
		packed & 0xff, ( packed >> 8 ) & 0xff,
		( packed >> 16 ) & 0xff, ( packed >> 24 ) & 0xff ) );
}
}

BEGIN_RECV_TABLE_NOBASE( CGlowProperty, DT_GlowProperty )
	RecvPropInt( RECVINFO( m_iGlowType ), 0, RecvProxy_GlowType ),
	RecvPropInt( RECVINFO( m_nGlowRange ) ),
	RecvPropInt( RECVINFO( m_nGlowRangeMin ) ),
	RecvPropInt( RECVINFO( m_glowColorOverride ), 0, RecvProxy_GlowColor ),
	RecvPropBool( RECVINFO( m_bFlashing ) ),
END_RECV_TABLE()

BEGIN_DATADESC_NO_BASE( CGlowProperty )
	DEFINE_FIELD( m_iGlowType, FIELD_INTEGER ),
END_DATADESC()

CGlowProperty::CGlowProperty()
{
	// Native109BE0: the actual Color member constructor supplies packed0.
	Init( NULL );
}

void CGlowProperty::Init( CBaseEntity *pEntity )
{
	// 0x10109970 does not initialize the cached RGB floats. They are read
	// only for a nonzero packed override, after the color receive proxy.
	m_pOuter = pEntity;
	m_iGlowType = 0;
	m_nGlowRange = 0;
	m_nGlowRangeMin = 0;
	m_glowColorOverride = Color( 0, 0, 0, 0 );
	m_iTeam = -1;
	m_bEnabled = false;
	m_bFlashing = false;
}

void CGlowProperty::Term()
{
	// 0x10109A00: reset owner/state, then unregister even if already type 0.
	Init( NULL );
	s_GlowProperties.FindAndFastRemove( this );
}

void CGlowProperty::SetType( int type )
{
	m_iGlowType = type;
	// 0x10109A20 / receive proxy 0x10109AB0 act on the final value even
	// when it equals the old value. Invalid nonzero values do not remove
	// a pre-existing registration; the eligibility pass rejects them.
	if ( type >= 1 && type <= 3 )
	{
		if ( s_GlowProperties.Find( this ) == s_GlowProperties.InvalidIndex() )
			s_GlowProperties.AddToTail( this );
	}
	else if ( type == 0 )
	{
		s_GlowProperties.FindAndFastRemove( this );
		m_bEnabled = false;
	}
}

void CGlowProperty::SetColor( const Color &color )
{
	// 0x101098A0: packed RGBA is received intact, alpha is not opacity.
	// The cache must refresh even if the packed value did not change.
	m_glowColorOverride = color;
	const float inverse255 = 0.0039215689f;
	m_glowColor.x = color.r() * inverse255;
	m_glowColor.y = color.g() * inverse255;
	m_glowColor.z = color.b() * inverse255;
}

void CGlowProperty::GetColor( float *red, float *green, float *blue ) const
{
	// 0x10109900: packed zero means retain the caller's default RGB.
	const Color &color = m_glowColorOverride.Get();
	if ( color.r() || color.g() || color.b() || color.a() )
	{
		*red = m_glowColor.x;
		*green = m_glowColor.y;
		*blue = m_glowColor.z;
	}
	if ( m_bFlashing )
		L4D2_GlowFlashColor( gpGlobals->curtime, red, green, blue );
}

const CUtlVector< CGlowProperty * > &TerrorGlow_GetProperties()
{
	return s_GlowProperties;
}

bool TerrorGlow_PropertyInRange( const CGlowProperty &glow, C_BasePlayer *pPlayer )
{
	// 0x101094D0: integer truncation occurs AFTER the origin distance sqrt.
	const Vector delta = pPlayer->GetAbsOrigin() - glow.GetOuter()->GetAbsOrigin();
	const int distance = static_cast< int >( sqrtf( delta.y * delta.y +
		delta.z * delta.z + delta.x * delta.x ) );
	return !( glow.GetRange() > 0 && distance > glow.GetRange() ) &&
		!( glow.GetRangeMin() > 0 && distance < glow.GetRangeMin() );
}

bool TerrorGlow_PropertyUsableInView( const CGlowProperty &glow, C_BasePlayer *pPlayer,
	float useTolerance )
{
	// Native type-1 predicate 0x101092F0. The separate type-2 IsAbleToSee
	// cache is NOT replaced by this trace or by a generic FVisible call.
	C_BaseEntity *pEntity = glow.GetOuter();
	if ( !pEntity->IsPotentiallyUseable() )
		return false;

	Vector useOrigin = pPlayer->EyePosition();
	useOrigin.x = pPlayer->GetAbsOrigin().x;
	useOrigin.y = pPlayer->GetAbsOrigin().y;
	Vector localPoint, nearestPoint;
	pEntity->CollisionProp()->WorldToCollisionSpace( useOrigin, &localPoint );
	CalcClosestPointOnAABB( pEntity->CollisionProp()->OBBMins(),
		pEntity->CollisionProp()->OBBMaxs(), localPoint, nearestPoint );
	const unsigned int range = static_cast< unsigned int >( glow.GetRange() );
	const int rangeSquared = static_cast< int >( range * range );
	if ( !L4D2_GlowDistanceWithinRange( &localPoint, &nearestPoint, rangeSquared ) )
		return false;

	Vector forward;
	pPlayer->EyeVectors( &forward );
	Vector direction = pEntity->WorldSpaceCenter() - useOrigin;
	L4D2_GlowNormalize( &direction );
	if ( useTolerance > DotProduct( forward, direction ) )
		return false;

	trace_t trace;
	CTraceFilterSimple filter( pPlayer, COLLISION_GROUP_NONE );
	UTIL_TraceLine( pPlayer->EyePosition(), pEntity->WorldSpaceCenter(),
		0x0200608B, &filter, &trace );
	return trace.fraction >= 1.0f || trace.m_pEnt == pEntity;
}

bool TerrorGlow_PropertyMatchesTeam( const CGlowProperty &glow, C_BasePlayer *pPlayer )
{
	return glow.GetTeam() == -1 || glow.GetTeam() == pPlayer->GetTeamNumber();
}

float TerrorGlow_PropertyBackfaceScale( const CGlowProperty &glow, C_BasePlayer *pPlayer,
	float backfaceMultiplier )
{
	// 0x10273210: a value >1 bypasses the backface test rather than
	// increasing brightness. No clamping of the <=1 branch is performed.
	if ( backfaceMultiplier > 1.0f )
		return 1.0f;
	Vector forward;
	AngleVectors( glow.GetOuter()->GetAbsAngles(), &forward );
	const Vector direction = pPlayer->GetAbsOrigin() - glow.GetOuter()->GetAbsOrigin();
	return DotProduct( forward, direction ) < 0.0f ? backfaceMultiplier : 1.0f;
}

#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
