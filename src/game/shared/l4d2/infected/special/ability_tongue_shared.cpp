//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#ifdef CLIENT_DLL
#include "l4d2/player/c_terrorplayer.h"
#else
#include "l4d2/player/terrorplayer.h"
#endif
#include "l4d2/infected/special/ability_tongue.h"
#include "in_buttons.h"
#include "gamerules.h"
#include "tier0/memdbgon.h"

IMPLEMENT_NETWORKCLASS_ALIASED( Tongue, DT_AbilityTongue )
BEGIN_NETWORK_TABLE( CTongue, DT_AbilityTongue )
#ifdef CLIENT_DLL
	RecvPropInt( RECVINFO( m_tongueGrabStartingHealth ) ),
	RecvPropFloat( RECVINFO( m_tongueHitRange ) ),
	RecvPropFloat( RECVINFO( m_tongueHitTimestamp ) ),
	RecvPropBool( RECVINFO( m_tongueHitWasAmbush ) ),
	RecvPropFloat( RECVINFO( m_tongueVictimDistance ) ),
	RecvPropFloat( RECVINFO( m_tongueVictimPositionTime ) ),
	RecvPropFloat( RECVINFO( m_tongueVictimLastOnGroundTime ) ),
	RecvPropEHandle( RECVINFO( m_potentialTarget ) ),
	RecvPropEHandle( RECVINFO( m_currentTipTarget ) ),
	RecvPropInt( RECVINFO( m_tongueState ) ),
	RecvPropInt( RECVINFO( m_bendPointCount ) ),
	RecvPropVector( RECVINFO( m_tipPosition ) ),
	RecvPropArray( RecvPropVector( RECVINFO( m_bendPositions[0] ) ), m_bendPositions ),
#else
	SendPropInt( SENDINFO( m_tongueGrabStartingHealth ), 32 ),
	SendPropFloat( SENDINFO( m_tongueHitRange ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_tongueHitTimestamp ), 0, SPROP_NOSCALE ),
	SendPropBool( SENDINFO( m_tongueHitWasAmbush ) ),
	SendPropFloat( SENDINFO( m_tongueVictimDistance ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_tongueVictimPositionTime ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_tongueVictimLastOnGroundTime ), 0, SPROP_NOSCALE ),
	SendPropEHandle( SENDINFO( m_potentialTarget ) ),
	SendPropEHandle( SENDINFO( m_currentTipTarget ) ),
	SendPropInt( SENDINFO( m_tongueState ), 32 ),
	SendPropInt( SENDINFO( m_bendPointCount ), 32 ),
	SendPropVector( SENDINFO( m_tipPosition ), 0, SPROP_NOSCALE ),
	SendPropArray( SendPropVector( SENDINFO_ARRAY( m_bendPositions ), -1, SPROP_COORD_MP ), m_bendPositions ),
#endif
END_NETWORK_TABLE()

// C_Tongue's native prediction map contains only its invalid sentinel. None
// of its replicated bend/target/tip fields are locally predicted.
#if defined( CLIENT_DLL ) && !defined( NO_ENTITY_PREDICTION )
datamap_t CTongue::m_PredMap = { 0, 0, "C_Tongue", &CBaseAbility::m_PredMap };
datamap_t *CTongue::GetPredDescMap() { return &m_PredMap; }
BEGIN_PREDICTION_DATA_GUTS( CTongue )
#else
BEGIN_PREDICTION_DATA( CTongue )
#endif
END_PREDICTION_DATA()

int CTongue::GetActivationButton() const { return IN_ATTACK; }
bool CTongue::IsReadyToActivate() { return GetPotentialTarget() != NULL; }
CTerrorPlayer *CTongue::GetPotentialTarget() const { return ToTerrorPlayer( m_potentialTarget.Get() ); }
bool CTongue::IsTongueExtended() const { return m_tongueState != 0; }
#ifdef CLIENT_DLL

// C28E730; native unsigned comparison also accepts negative raw states.
bool CTongue::IsPastTongueLaunchPhase() const
{
	return static_cast< unsigned int >( m_tongueState.Get() ) >= 2u;
}
#endif

// S36A2E0/C28E7F0. With no bend, return the owner's origin unchanged;
// only a bend position is converted from absolute to standing-feet space.
Vector CTongue::GetPullPosition( bool absolute ) const
{
	CTerrorPlayer *owner = GetOwner();
	if ( !owner ) return vec3_origin;
	if ( m_bendPointCount == 0 ) return owner->GetAbsOrigin();
	Vector position = m_bendPositions[m_bendPointCount - 1];
	if ( !absolute ) position -= g_pGameRules->GetViewVectors()->m_vView;
	return position;
}

const Vector &CTongue::GetPullGroundPosition()
{
	CTerrorPlayer *owner = GetOwner();
	if ( !owner ) return vec3_origin;
	const Vector position = m_bendPointCount ? m_bendPositions[m_bendPointCount - 1] : owner->GetAbsOrigin();
	if ( position != m_groundTracePosition )
	{
		m_groundTracePosition = position;
		CTraceFilterNoNPCsOrPlayer filter( NULL, COLLISION_GROUP_NONE );
		trace_t trace;
		UTIL_TraceLine( position, position - Vector( 0, 0, 1000 ), 33636875, &filter, &trace );
		m_groundTraceResult = trace.endpos;
	}
	return m_groundTraceResult;
}

int CTongue::GetSegmentCount() const
{
	return m_tongueState == 3 ? m_bendPointCount + 1 : 0;
}

Vector CTongue::GetSegmentStart( int segment )
{
#ifdef CLIENT_DLL
	Vector &result = m_effectStarts[segment];
	if ( segment ) result = m_bendPositions[segment - 1];
	else if ( GetOwner() ) GetOwner()->GetAttachment( GetOwner()->LookupAttachment( "smoker_mouth" ), result );
	return result;
#else
	if ( segment ) return m_bendPositions[segment - 1];
	if ( !GetOwner() ) return vec3_origin;
	Vector result;
	QAngle angles;
	GetOwner()->GetAttachment( GetOwner()->LookupAttachment( "smoker_mouth" ), result, angles );
	return result;
#endif
}

Vector CTongue::GetSegmentEnd( int segment )
{
#ifdef CLIENT_DLL
	Vector &result = m_effectEnds[segment];
	if ( segment < m_bendPointCount ) result = m_bendPositions[segment];
	else if ( m_tongueState == 2 ) result = m_tipPosition;
	else if ( GetOwner() && GetOwner()->GetTongueVictim() )
	{
		CTerrorPlayer *victim = GetOwner()->GetTongueVictim();
		victim->GetAttachment( victim->LookupAttachment( "medkit" ), result );
	}
	return result;
#else
	if ( segment < m_bendPointCount ) return m_bendPositions[segment];
	CTerrorPlayer *victim = GetOwner() ? GetOwner()->GetTongueVictim() : NULL;
	if ( !victim ) return vec3_origin;
	Vector result;
	QAngle angles;
	victim->GetAttachment( victim->LookupAttachment( "medkit" ), result, angles );
	return result;
#endif
}
