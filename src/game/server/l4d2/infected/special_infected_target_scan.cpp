//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/infected/common/infected.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/infected/special_infected_target_scan.h"
#include "querycache.h"
#include "debugoverlay_shared.h"
#include "l4d2/terror_trace_filter.h"
#include "tier0/memdbgon.h"
ConVar target_scan_use_query_cache( "target_scan_use_query_cache", "1", 0x00000006 );
extern ConVar tongue_debug;

bool TongueTraceIgnoresGhosts( IHandleEntity *handle, int contentsMask )
{
	CBaseEntity *entity = EntityFromEntityHandle( handle );
	CTerrorPlayer *player = ToTerrorPlayer( entity );
	return !player || !player->IsGhost();
}
bool TongueTraceIgnoresInfectedOrGhosts( IHandleEntity *handle, int contentsMask )
{
	CBaseEntity *entity = EntityFromEntityHandle( handle );
	if ( entity && entity->MyInfectedPointer() ) return false;
	return TongueTraceIgnoresGhosts( handle, contentsMask );
}

TongueTargetScan::TongueTargetScan( CTerrorPlayer *owner, float tolerance, float range ) :
	TargetScan<CTerrorPlayer>( owner, TEAM_SURVIVOR, tolerance, range ),
	m_isBot( owner->IsBot() ), m_eyePosition( owner->EyePosition() )
{
}

bool TongueTargetScan::IsPotentialTarget( CTerrorPlayer *target ) const
{
	return !target->IsGhost() && !target->IsIncapacitated() && !target->GetPounceAttacker() &&
		!target->GetPummelAttacker() && !target->GetTongueOwner() && !target->IsFallingFromLedge() &&
		!( target->GetFlags() & ( FL_FROZEN | FL_ATCONTROLS ) );
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif

bool TongueTargetScan::IsTargetVisible( CTerrorPlayer *target ) const
{
	ShouldHitFunc_t callback = TerrorGameRules()->IsPlayerControlledZombies() ?
		TongueTraceIgnoresInfectedOrGhosts : TongueTraceIgnoresGhosts;
	if ( target_scan_use_query_cache.GetBool() )
		return IsLineOfSightBetweenTwoEntitiesClear( m_me, EOFFSET_MODE_EYEPOSITION, target,
			EOFFSET_MODE_EYEPOSITION, m_me, COLLISION_GROUP_NONE, 1107320961, callback, 0.1f, true );
	CTraceFilterSimpleListNoGhosts ghostFilter( COLLISION_GROUP_NONE );
	CTraceFilterSimpleListNoInfectedOrGhosts infectedFilter( COLLISION_GROUP_NONE );
	CTraceFilterSimpleListNoGhosts *filter = TerrorGameRules()->IsPlayerControlledZombies() ? &infectedFilter : &ghostFilter;
	filter->AddEntityToIgnore( m_me );
	trace_t trace;
	UTIL_TraceLine( m_me->EyePosition(), target->EyePosition(), 1107320961, filter, &trace );
	return !( trace.fraction < 1.0f ) || trace.m_pEnt == target;
}


// S36EC40 is NOT the generic scan for a human smoker: range is eye-to-center,
// nearest point is taken on the full collision bounds, ties are strict, and
// the cone check precedes an additional visibility test.
bool TongueTargetScan::operator()( CTerrorPlayer *target )
{
	if ( m_isBot ) return TargetScan<CTerrorPlayer>::operator()( target );
	if ( target == m_me || !target->IsAlive() ||
		( target->GetTeamNumber() != TEAM_SURVIVOR && target->GetTeamNumber() != TEAM_L4D1_SURVIVOR ) ||
		!IsPotentialTarget( target ) ) return true;
	if ( ( m_eyePosition - target->WorldSpaceCenter() ).LengthSqr() > m_maxRange * m_maxRange ) return true;
	if ( !m_me->FInViewCone( target->WorldSpaceCenter() ) ) return true;
	Vector point;
	CalcClosestPointOnLine( target->WorldSpaceCenter(), m_eyePosition,
		m_eyePosition + m_viewForward * m_maxRange, point );
	if ( tongue_debug.GetInt() >= 2 )
	{
		NDebugOverlay::Line( m_eyePosition, m_eyePosition + m_viewForward * m_maxRange, 0, 0, 255, true, 0.1f );
		NDebugOverlay::Line( target->WorldSpaceCenter(), point, 0, 255, 0, true, 0.1f );
	}
	Vector nearest;
	target->CollisionProp()->CalcNearestPoint( point, &nearest );
	if ( tongue_debug.GetInt() >= 2 ) NDebugOverlay::Line( point, nearest, 255, 0, 255, true, 0.1f );
	if ( IsTargetVisible( target ) )
	{
		Vector to = nearest - m_eyePosition;
		VectorNormalize( to );
		const float dot = ViewDot( to );
		if ( m_closeDot > dot ) return true;
		if ( tongue_debug.GetInt() >= 1 )
			Msg( "target %s - range: %.1f - dot: %.3f \n", target->GetPlayerName(), to.Length(), dot );
		if ( dot > m_bestDot )
		{
			m_bestDot = dot;
			m_target = target;
		}
	}
	return true;
}

ChargeTargetScan::ChargeTargetScan( CTerrorPlayer *owner, float range ) :
	TargetScan<CTerrorPlayer>( owner, TEAM_SURVIVOR, 0.0f, range ), m_origin( owner->WorldSpaceCenter() )
{
	owner->GetVectors( &m_chargeForward, NULL, NULL );
}
bool ChargeTargetScan::IsPotentialTarget( CTerrorPlayer *target ) const
{
	// Charger deliberately does not reject an already-pummeled victim here.
	return !target->IsGhost() && !target->IsIncapacitated() && !target->GetPounceAttacker() &&
		!target->GetTongueOwner() && !target->IsFallingFromLedge() &&
		!( target->GetFlags() & ( FL_FROZEN | FL_ATCONTROLS ) );
}
bool ChargeTargetScan::IsTargetVisible( CTerrorPlayer *target ) const
{
	if ( target_scan_use_query_cache.GetBool() )
		return IsLineOfSightBetweenTwoEntitiesClear( m_me, EOFFSET_MODE_EYEPOSITION, target,
			EOFFSET_MODE_EYEPOSITION, m_me, COLLISION_GROUP_NONE, 1107320961,
			TongueTraceIgnoresGhosts, 1.0f, true );
	CTraceFilterSimpleListNoGhosts filter( COLLISION_GROUP_NONE );
	filter.AddEntityToIgnore( m_me );
	trace_t trace;
	UTIL_TraceLine( m_me->EyePosition(), target->EyePosition(), 1107320961, &filter, &trace );
	return trace.fraction >= 1.0f || trace.m_pEnt == target;
}
bool ChargeTargetScan::operator()( CTerrorPlayer *target )
{
	if ( target == m_me || !target->IsAlive() ||
		( target->GetTeamNumber() != TEAM_SURVIVOR && target->GetTeamNumber() != TEAM_L4D1_SURVIVOR ) ||
		!IsPotentialTarget( target ) ) return true;
	const Vector center = target->WorldSpaceCenter();
	if ( !( ( m_origin - center ).LengthSqr() <= m_maxRange * m_maxRange ) ||
		!m_me->FInViewCone( center ) || !IsTargetVisible( target ) ) return true;
	if ( CalcDistanceToLineSegment( center, m_origin, m_origin + m_chargeForward * m_maxRange ) <= 32.0f )
	{
		m_target = target;
		m_allTargets.AddToTail( target );
	}
	return true;
}

#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
