//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
//=============================================================================

#include "cbase.h"

#include "NextBotChasePath.h"
#include "tier1/fmtstr.h"
#if defined( L4D2 )
#include "igamesystem.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


#if defined( L4D2 )
// S4B1AD0 has four process-lifetime entries, keyed by componentwise proximity.
// The SDK has no native gpGlobals->serverCount: the real level-init boundary
// invalidates only area pointers, retaining the native replacement cursor.
class CChasePathLeadCache : public CAutoGameSystem
{
public:
	CChasePathLeadCache() : CAutoGameSystem( "ChasePathLeadCache" ), m_next( 0 )
	{
		ClearAreas();
	}
	virtual void LevelInitPreEntity() OVERRIDE { ClearAreas(); }
	CNavArea *FindArea( Vector *target )
	{
		for ( int i = 0; i < ARRAYSIZE( m_entries ); ++i )
		{
			if ( m_entries[i].area && CloseEnough( m_entries[i].target, *target, 2.0f ) )
			{
				*target = m_entries[i].target;
				return m_entries[i].area;
			}
		}
		CNavArea *area = TheNavMesh->GetNearestNavArea( *target );
		if ( area )
		{
			m_entries[m_next].target = *target;
			m_entries[m_next].area = area;
			m_next = ( m_next + 1 ) & 3;
		}
		return area;
	}
private:
	void ClearAreas()
	{
		for ( int i = 0; i < ARRAYSIZE( m_entries ); ++i )
			m_entries[i].area = NULL;
	}
	struct Entry
	{
		Vector target;
		CNavArea *area;
	};
	Entry m_entries[4];
	int m_next;
};
static CChasePathLeadCache s_chasePathLeadCache;
#endif

//----------------------------------------------------------------------------------------------
/**
 * Try to cutoff our chase subject
 */
#if defined( L4D2 ) && defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
Vector ChasePath::PredictSubjectPosition( INextBot *bot, CBaseEntity *subject ) const
{
	ILocomotion *mover = bot->GetLocomotionInterface();

	const Vector &subjectPos = subject->GetAbsOrigin();

	Vector to = subjectPos - bot->GetPosition();
	to.z = 0.0f;
	float flRangeSq = to.LengthSqr();

	// don't lead if subject is very far away
	float flLeadRadiusSq = GetLeadRadius();
	flLeadRadiusSq *= flLeadRadiusSq;
	if ( flRangeSq > flLeadRadiusSq )
		return subjectPos;

	// Normalize in place
	float range = sqrt( flRangeSq );
	to /= ( range + 0.0001f );	// avoid divide by zero

	// estimate time to reach subject, assuming maximum speed
#if defined( L4D2 )
	float leadTime = static_cast< float >( 0.5 + static_cast< double >( range ) /
		( static_cast< double >( mover->GetRunSpeed() ) + static_cast< double >( 0.0001f ) ) );
#else
	float leadTime = 0.5f + ( range / ( mover->GetRunSpeed() + 0.0001f ) );
#endif
	
	// estimate amount to lead the subject	
	Vector lead = leadTime * subject->GetAbsVelocity();
	lead.z = 0.0f;

	if ( DotProduct( to, lead ) < 0.0f )
	{
		// the subject is moving towards us - only pay attention 
		// to his perpendicular velocity for leading
		Vector2D to2D = to.AsVector2D();
		to2D.NormalizeInPlace();

		Vector2D perp( -to2D.y, to2D.x );

		float enemyGroundSpeed = lead.x * perp.x + lead.y * perp.y;

		lead.x = enemyGroundSpeed * perp.x;
		lead.y = enemyGroundSpeed * perp.y;
	}

	// compute our desired destination
	Vector pathTarget = subjectPos + lead;

	// validate this destination

	// don't lead through walls
	if ( lead.LengthSqr() > 36.0f )
	{
		float fraction;
		if ( !mover->IsPotentiallyTraversable( subjectPos, pathTarget, ILocomotion::IMMEDIATELY, &fraction ) )
		{
			// tried to lead through an unwalkable area - clip to walkable space
			pathTarget = subjectPos + fraction * ( pathTarget - subjectPos );
		}
	}

	// don't lead over cliffs
	CNavArea *leadArea = NULL;

#if defined( L4D2 )
	CBaseCombatCharacter *pBCC = subject->MyCombatCharacterPointer();
	if ( pBCC && CloseEnough( pathTarget, subjectPos, 3.0f ) )
	{
		pathTarget = subjectPos;
		leadArea = pBCC->GetLastKnownArea();
	}
	else
	{
		leadArea = s_chasePathLeadCache.FindArea( &pathTarget );
	}
#elif defined( NEED_GPGLOBALS_SERVERCOUNT_TO_DO_THIS )
	CBaseCombatCharacter *pBCC = subject->MyCombatCharacterPointer();
	if ( pBCC && CloseEnough( pathTarget, subjectPos, 3.0 ) )
	{
		pathTarget = subjectPos;
		leadArea = pBCC->GetLastKnownArea(); // can return null?
	}
	else
	{
		struct CacheEntry_t
		{
			CacheEntry_t() : pArea(NULL) {}
			Vector target;
			CNavArea *pArea;
		};

		static int iServer;
		static CacheEntry_t cache[4];
		static int iNext;
		int i;

		bool bFound = false;
		if ( iServer != gpGlobals->serverCount )
		{
			for ( i = 0; i < ARRAYSIZE(cache); i++ )
			{
				cache[i].pArea = NULL;
			}
			iServer = gpGlobals->serverCount;
		}
		else
		{
			for ( i = 0; i < ARRAYSIZE(cache); i++ )
			{
				if ( cache[i].pArea && CloseEnough( cache[i].target, pathTarget, 2.0 ) )
				{
					pathTarget = cache[i].target;
					leadArea = cache[i].pArea;
					bFound = true;
					break;
				}
			}
		}

		if ( !bFound )
		{
			leadArea = TheNavMesh->GetNearestNavArea( pathTarget );
			if ( leadArea )
			{
				cache[iNext].target = pathTarget;
				cache[iNext].pArea = leadArea;
				iNext = ( iNext + 1 ) % ARRAYSIZE( cache );
			}
		}
	}
#else
	leadArea = TheNavMesh->GetNearestNavArea( pathTarget );
#endif


#if defined( L4D2 )
	if ( !leadArea || static_cast< double >( pathTarget.z ) - mover->GetMaxJumpHeight() >
		static_cast< double >( leadArea->GetZ( pathTarget.x, pathTarget.y ) ) )
#else
	if ( !leadArea || leadArea->GetZ( pathTarget.x, pathTarget.y ) < pathTarget.z - mover->GetMaxJumpHeight() )
#endif
	{
		// would fall off a cliff
		return subjectPos;		
	}
	
	/** This needs more thought - it is preventing bots from using dropdowns
	if ( mover->HasPotentialGap( subjectPos, pathTarget, &fraction ) )
	{
		// tried to lead over a cliff - clip to safe region
		pathTarget = subjectPos + fraction * ( pathTarget - subjectPos );
	}
	*/
	
	return pathTarget;
}
#if defined( L4D2 ) && defined( _MSC_VER )
#pragma float_control( pop )
#endif

// if the victim is a player, poke them so they know they're being chased
void DirectChasePath::NotifyVictim( INextBot *me, CBaseEntity *victim )
{
	CBaseCombatCharacter *pBCCVictim = ToBaseCombatCharacter( victim );
	if ( !pBCCVictim )
		return;
	
	pBCCVictim->OnPursuedBy( me );
}