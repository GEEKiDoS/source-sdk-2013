//========= Copyright Valve Corporation, All rights reserved. ============//

#include "cbase.h"
#include "l4d2/entities/point_prop_use_target.h"
#include "player.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

LINK_ENTITY_TO_CLASS( point_prop_use_target, CPointPropUseTarget );

IMPLEMENT_SERVERCLASS_ST( CPointPropUseTarget, DT_PointPropUseTarget )
	SendPropEHandle( SENDINFO( m_useActionOwner ) ),
	SendPropEHandle( SENDINFO( m_hGasNozzle ) ),
END_SEND_TABLE()

BEGIN_DATADESC( CPointPropUseTarget )
	DEFINE_KEYFIELD( m_sGasNozzleName, FIELD_STRING, "nozzle" ),
	DEFINE_INPUTFUNC( FIELD_VOID, "Activate", InputActivate ),
	DEFINE_INPUTFUNC( FIELD_VOID, "Deactivate", InputDeactivate ),
	DEFINE_OUTPUT( m_onUseStarted, "OnUseStarted" ),
	DEFINE_OUTPUT( m_onUseCancelled, "OnUseCancelled" ),
	DEFINE_OUTPUT( m_onUseFinished, "OnUseFinished" ),
END_DATADESC()

CPointPropUseTarget::CPointPropUseTarget()
{
	m_useActionOwner = NULL;
	m_hGasNozzle = NULL;
	m_nUseCapabilities = 0;
	m_bActive = false;
	m_sGasNozzleName = NULL_STRING;
}

// Original S:3C8AB0. Neither Deactivate nor another user removes the caps.
void CPointPropUseTarget::Spawn()
{
	BaseClass::Spawn();
	m_nUseCapabilities = 0;
	if ( HasSpawnFlags( 2 ) )
		m_nUseCapabilities = USE_COLA;
	if ( HasSpawnFlags( 1 ) )
		m_nUseCapabilities |= USE_GASCAN;
	if ( !m_nUseCapabilities )
		m_nUseCapabilities = USE_GASCAN;
	m_bActive = true;
	Precache();
	m_hGasNozzle = gEntList.FindEntityByName( NULL, STRING( m_sGasNozzleName ) );
}

int CPointPropUseTarget::ObjectCaps()
{
	return BaseClass::ObjectCaps() | m_nUseCapabilities | FCAP_IMPULSE_USE;
}

void CPointPropUseTarget::InputActivate( inputdata_t &inputdata )
{
	m_bActive = true;
}

void CPointPropUseTarget::InputDeactivate( inputdata_t &inputdata )
{
	m_bActive = false;
}

bool CPointPropUseTarget::IsAvailableForUse() const
{
	return m_bActive && m_useActionOwner.Get() == NULL;
}

void CPointPropUseTarget::SetUseActionOwner( CBaseEntity *pOwner )
{
	m_useActionOwner = pOwner;
}

// S:3C9220's caller is the carried item, not its player. Outputs deliberately
// use the target as both activator and caller, including cancellation/finish.
void CPointPropUseTarget::OnUseStarted( CBaseEntity *pOwner )
{
	SetUseActionOwner( pOwner );
	m_onUseStarted.FireOutput( this, this );
}

void CPointPropUseTarget::OnUseCancelled()
{
	if ( m_useActionOwner.Get() )
		m_useActionOwner = NULL;
	m_onUseCancelled.FireOutput( this, this );
}

void CPointPropUseTarget::OnUseFinished()
{
	if ( m_useActionOwner.Get() )
		m_useActionOwner = NULL;
	m_onUseFinished.FireOutput( this, this );
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
// S:3C8CA0. Availability is intentionally NOT a selection filter: CGasCan
// rejects a busy/inactive selected target, whereas CColaBottles does not.
CPointPropUseTarget *CPointPropUseTarget::FindUseTarget( CBasePlayer *pPlayer, float flRange, float flTolerance, int nCapability )
{
	if ( !pPlayer )
		return NULL;

	Vector vecUseOrigin = pPlayer->EyePosition();
	vecUseOrigin.x = pPlayer->GetAbsOrigin().x;
	vecUseOrigin.y = pPlayer->GetAbsOrigin().y;
	Vector vecForward;
	pPlayer->EyeVectors( &vecForward );
	const float flRangeSquared = flRange * flRange;
	float flBestDot = 0.0f;
	CPointPropUseTarget *pBest = NULL;
	CBaseEntity *pEntity = NULL;
	while ( ( pEntity = gEntList.FindEntityByClassname( pEntity, "point_prop_use_target" ) ) != NULL )
	{
		if ( !( pEntity->ObjectCaps() & nCapability ) )
			continue;

		Vector vecLocalPoint, vecClosest;
		pEntity->CollisionProp()->WorldToCollisionSpace( vecUseOrigin, &vecLocalPoint );
		CalcClosestPointOnAABB( pEntity->CollisionProp()->OBBMins(), pEntity->CollisionProp()->OBBMaxs(), vecLocalPoint, vecClosest );
		if ( vecLocalPoint.DistToSqr( vecClosest ) > flRangeSquared )
			continue;

		Vector vecDirection = pEntity->WorldSpaceCenter() - vecUseOrigin;
		VectorNormalize( vecDirection );
		const float flDot = DotProduct( vecForward, vecDirection );
		if ( flDot <= flBestDot || flDot < flTolerance )
			continue;

		// Original quirk: even an obstructed candidate raises the dot threshold;
		// it does not erase a previously visible, lower-dot winner.
		flBestDot = flDot;
		CTraceFilterSimple filter( pPlayer, COLLISION_GROUP_NONE );
		trace_t tr;
		const unsigned int nMask = CONTENTS_SOLID | CONTENTS_WINDOW | CONTENTS_GRATE | CONTENTS_OPAQUE | CONTENTS_IGNORE_NODRAW_OPAQUE | CONTENTS_MOVEABLE;
		UTIL_TraceLine( pPlayer->EyePosition(), pEntity->WorldSpaceCenter(), nMask, &filter, &tr );
		// S3C8EA3/JBE accepts an unordered trace fraction as well.
		if ( !( tr.fraction < 1.0f ) || tr.m_pEnt == pEntity )
			pBest = static_cast< CPointPropUseTarget * >( pEntity );
	}
	return pBest;
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
