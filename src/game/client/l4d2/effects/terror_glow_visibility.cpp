//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "c_basecombatcharacter.h"
#include "l4d2/effects/terror_glow_visibility.h"
#include "igamesystem.h"
#include "util_shared.h"
#include "tier1/utlmap.h"
#include "tier0/memdbgon.h"

#pragma float_control( precise, on, push )

// C:10012280: simple filter first, then no combat characters or weapons,
// and no entities carrying EFL_DONTBLOCKLOS. This is NOT FVisible.
class CTerrorGlowTraceFilter : public CTraceFilterSimple
{
public:
	CTerrorGlowTraceFilter() : CTraceFilterSimple( NULL, COLLISION_GROUP_NONE ) {}
	virtual bool ShouldHitEntity( IHandleEntity *pHandleEntity, int contentsMask ) OVERRIDE
	{
		if ( !CTraceFilterSimple::ShouldHitEntity( pHandleEntity, contentsMask ) )
			return false;
		C_BaseEntity *pEntity = EntityFromEntityHandle( pHandleEntity );
		return pEntity && !pEntity->MyCombatCharacterPointer() &&
			!pEntity->MyCombatWeaponPointer() && pEntity->BlocksLOS();
	}
};

struct TerrorGlowVisibilityKey_t
{
	unsigned int m_nFirst;
	unsigned int m_nSecond;
};
struct TerrorGlowVisibility_t
{
	float m_flTimestamp;
	bool m_bFirstToSecond;
	bool m_bSecondToFirst;
};
static bool TerrorGlow_VisibilityLess( const TerrorGlowVisibilityKey_t &a, const TerrorGlowVisibilityKey_t &b )
{
	// C:10011D50 compares the eight raw handle bytes, not integer handle order.
	return Q_memcmp( &a, &b, sizeof( a ) ) < 0;
}

class CTerrorGlowVisibilityCache : public CAutoGameSystemPerFrame
{
public:
	CTerrorGlowVisibilityCache() : CAutoGameSystemPerFrame( "CCombatCharVisCache" ),
		m_Cache( 0, 0, TerrorGlow_VisibilityLess ) {}

	virtual void LevelShutdownPreEntity() OVERRIDE { m_Cache.Purge(); }
	virtual void Update( float frametime ) OVERRIDE
	{
		// C:10014E10. Once the cache reaches 256 live nodes, sample eight
		// allocated indices and reclaim nodes older than ten seconds.
		if ( m_Cache.Count() < 256 )
			return;
		const int nLast = m_Cache.MaxElement() - 1;
		for ( int i = 0; i < 8; ++i )
		{
			const unsigned short nIndex = (unsigned short)RandomInt( 0, nLast );
			if ( !m_Cache.IsValidIndex( nIndex ) )
				continue;
			const TerrorGlowVisibilityKey_t &key = m_Cache.Key( nIndex );
			if ( key.m_nFirst == INVALID_EHANDLE_INDEX || key.m_nSecond == INVALID_EHANDLE_INDEX ||
				gpGlobals->curtime - m_Cache[nIndex].m_flTimestamp > 10.0f )
				m_Cache.RemoveAt( nIndex );
		}
	}

	bool IsAbleToSee( C_BaseCombatCharacter *pViewer, C_BaseCombatCharacter *pTarget )
	{
		// C:10014F00 orders participants by address before taking serial
		// handles. Direction is separate from the eight-byte map key.
		const bool bReverse = (uintp)pViewer >= (uintp)pTarget;
		TerrorGlowVisibilityKey_t key;
		key.m_nFirst = ( bReverse ? pTarget : pViewer )->GetRefEHandle().ToInt();
		key.m_nSecond = ( bReverse ? pViewer : pTarget )->GetRefEHandle().ToInt();
		unsigned short nIndex = m_Cache.Find( key );
		if ( nIndex == m_Cache.InvalidIndex() && m_Cache.Count() != 0xFFFF )
		{
			TerrorGlowVisibility_t initial;
			initial.m_flTimestamp = gpGlobals->curtime - 0.18000001f;
			initial.m_bFirstToSecond = false;
			initial.m_bSecondToFirst = false;
			nIndex = m_Cache.Insert( key, initial );
		}
		if ( nIndex != m_Cache.InvalidIndex() &&
			gpGlobals->curtime - m_Cache[nIndex].m_flTimestamp <= 0.090000004f )
		{
			return bReverse ? m_Cache[nIndex].m_bSecondToFirst : m_Cache[nIndex].m_bFirstToSecond;
		}
		const bool bVisible = Trace( pViewer->EyePosition(), pTarget->EyePosition() );
		if ( nIndex != m_Cache.InvalidIndex() )
		{
			TerrorGlowVisibility_t &entry = m_Cache[nIndex];
			entry.m_flTimestamp = gpGlobals->curtime;
			entry.m_bFirstToSecond = bVisible;
			entry.m_bSecondToFirst = bVisible;
		}
		return bVisible;
	}

	static bool Trace( const Vector &start, const Vector &end )
	{
		CTerrorGlowTraceFilter filter;
		Ray_t ray;
		ray.Init( start, end );
		trace_t trace;
		enginetrace->TraceRay( ray, 0x02006081, &filter, &trace );
		return trace.fraction == 1.0f;
	}
private:
	CUtlMap< TerrorGlowVisibilityKey_t, TerrorGlowVisibility_t, unsigned short > m_Cache;
};
static CTerrorGlowVisibilityCache s_TerrorGlowVisibility;

bool TerrorGlow_IsAbleToSee( C_BaseCombatCharacter *pViewer, C_BaseEntity *pTarget )
{
	C_BaseCombatCharacter *pCombatTarget = pTarget->MyCombatCharacterPointer();
	if ( pCombatTarget )
		return s_TerrorGlowVisibility.IsAbleToSee( pViewer, pCombatTarget );
	return CTerrorGlowVisibilityCache::Trace( pViewer->EyePosition(), pTarget->WorldSpaceCenter() );
}

#pragma float_control( pop )
