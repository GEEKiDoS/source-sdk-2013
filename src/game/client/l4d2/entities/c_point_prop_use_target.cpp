//========= Copyright Valve Corporation, All rights reserved. ============//

#include "cbase.h"
#include "l4d2/entities/c_point_prop_use_target.h"
#include "c_baseplayer.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

IMPLEMENT_CLIENTCLASS_DT( CPointPropUseTarget, DT_PointPropUseTarget, CPointPropUseTarget )
	RecvPropEHandle( RECVINFO( m_useActionOwner ) ),
	RecvPropEHandle( RECVINFO( m_hGasNozzle ) ),
END_RECV_TABLE()

CPointPropUseTarget::CPointPropUseTarget()
{
	m_useActionOwner = NULL;
	m_hGasNozzle = NULL;
	m_nUseCapabilities = 0;
	m_bActive = false;
}

// C:2F6650. Spawnflags are inherited client entity state, not an extra prop
// on DT_PointPropUseTarget. Active is likewise NOT received from the server.
void CPointPropUseTarget::Spawn()
{
	BaseClass::Spawn();
	m_nUseCapabilities = 0;
	if ( GetSpawnFlags() & 2 )
		m_nUseCapabilities = USE_COLA;
	if ( GetSpawnFlags() & 1 )
		m_nUseCapabilities |= USE_GASCAN;
	if ( !m_nUseCapabilities )
		m_nUseCapabilities = USE_GASCAN;
	m_bActive = true;
}

int CPointPropUseTarget::ObjectCaps()
{
	return BaseClass::ObjectCaps() | m_nUseCapabilities | FCAP_IMPULSE_USE;
}

bool CPointPropUseTarget::IsAvailableForUse() const
{
	return m_bActive && m_useActionOwner.Get() == NULL;
}

void CPointPropUseTarget::SetUseActionOwner( C_BaseEntity *pOwner )
{
	if ( m_useActionOwner.Get() != pOwner )
		m_useActionOwner = pOwner;
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
// C:2F68F0. The native base owns this ordered potentially-useable list;
// removal swaps the last member into the removed slot.
C_BaseEntity *CPointPropUseTarget::FindUseTarget( C_BasePlayer *pPlayer, float flRange, float flTolerance, int nCapability, C_BaseEntity *pSkip )
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
	C_BaseEntity *pBest = NULL;
	const CUtlVector< C_BaseEntity * > &entities = C_BaseEntity::GetUseableEntityList();
	for ( int i = 0; i < entities.Count(); ++i )
	{
		C_BaseEntity *pEntity = entities[i];
		if ( !pEntity || pEntity == pSkip || !( pEntity->ObjectCaps() & nCapability ) )
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

		// Raise the threshold before the trace, even when this candidate is blocked.
		flBestDot = flDot;
		CTraceFilterSimple filter( pPlayer, COLLISION_GROUP_NONE );
		trace_t tr;
		const unsigned int nMask = CONTENTS_SOLID | CONTENTS_WINDOW | CONTENTS_GRATE | CONTENTS_OPAQUE | CONTENTS_IGNORE_NODRAW_OPAQUE | CONTENTS_MOVEABLE;
		UTIL_TraceLine( pPlayer->EyePosition(), pEntity->WorldSpaceCenter(), nMask, &filter, &tr );
		// C2F6B45/JBE accepts an unordered trace fraction as well.
		if ( !( tr.fraction < 1.0f ) || tr.m_pEnt == pEntity )
			pBest = pEntity;
	}
	return pBest;
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
