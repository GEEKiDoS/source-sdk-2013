//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/entities/visibility_monitor.h"
#include "player.h"
#include "igameevents.h"
#include "igamesystem.h"

#include "l4d2/entities/visibility_monitor_console_server_cvars.inc"
#include "tier0/memdbgon.h"

ConVar vismon_poll_frequency( "vismon_poll_frequency", ".5", 0x00004004 );
ConVar vismon_trace_limit( "vismon_trace_limit", "12", 0x00004004 );

// S:0x09FF30 stores one entity handle/range/observer mask/callback/predicate.
// Keep the original slot mask, including PE32 shift-count aliasing, rather
// than silently treating a replacement connection as a new observer.
struct VisibilityMonitorInfo
{
	VisibilityMonitorInfo() : m_flRangeSqr( 0.0f ), m_nSeenByMask( 0 ), m_pCallback( NULL ), m_pPredicate( NULL ) {}

	EHANDLE m_hEntity;
	float m_flRangeSqr;
	unsigned int m_nSeenByMask;
	VisibilityMonitorCallback_t m_pCallback;
	VisibilityMonitorCallback_t m_pPredicate;
};

class CVisibilityMonitor : public CAutoGameSystemPerFrame
{
public:
	CVisibilityMonitor() : CAutoGameSystemPerFrame( "VisibilityMonitor" ), m_flNextPoll( 0.0f ), m_nNextEntity( 0 ) {}

	virtual void LevelInitPostEntity() OVERRIDE
	{
		// S:0x09F490, equality with the deadline is due.
		m_flNextPoll = gpGlobals->curtime + vismon_poll_frequency.GetFloat();
		m_nNextEntity = 0;
	}

	virtual void LevelShutdownPreEntity() OVERRIDE
	{
		m_Entities.RemoveAll();
		m_nNextEntity = 0;
	}

	void AddEntity( CBaseEntity *pEntity, float flRange, VisibilityMonitorCallback_t pCallback, VisibilityMonitorCallback_t pPredicate )
	{
		if ( FindEntity( pEntity ) != m_Entities.InvalidIndex() )
			return;

		VisibilityMonitorInfo &info = m_Entities[m_Entities.AddToTail()];
		info.m_hEntity = pEntity;
		info.m_flRangeSqr = flRange * flRange;
		info.m_pCallback = pCallback;
		info.m_pPredicate = pPredicate;
	}

	void RemoveEntity( CBaseEntity *pEntity )
	{
		int index = FindEntity( pEntity );
		if ( index != m_Entities.InvalidIndex() )
			m_Entities.Remove( index ); // S:0x09FD00: ordered removal, cursor unchanged.
	}

	virtual void FrameUpdatePostEntityThink() OVERRIDE
	{
		// S:0x09F5C0. One clock and round-robin cursor for every registration.
		if ( m_flNextPoll > gpGlobals->curtime )
			return;
		m_flNextPoll = gpGlobals->curtime + vismon_poll_frequency.GetFloat();

		for ( int i = 0; i < m_Entities.Count(); ++i )
		{
			// Retail advances after FastRemove: a swapped-in invalid entry
			// can survive this pruning pass. Do not turn this into a while.
			if ( !m_Entities[i].m_hEntity.Get() )
				m_Entities.FastRemove( i );
		}
		if ( m_nNextEntity >= m_Entities.Count() )
			m_nNextEntity = 0;

		int nTraces = 0;
		for ( int i = m_nNextEntity; i < m_Entities.Count(); ++i )
		{
			UpdateEntityVisibility( i, nTraces );
			// The trace limit is tested after the ENTIRE entity/player loop,
			// so the last entity may take the poll above twelve traces.
			if ( nTraces >= vismon_trace_limit.GetInt() )
			{
				m_nNextEntity = i + 1;
				return;
			}
		}
		m_nNextEntity = 0;
	}

private:
	int FindEntity( CBaseEntity *pEntity ) const
	{
		FOR_EACH_VEC( m_Entities, i )
		{
			if ( m_Entities[i].m_hEntity.Get() == pEntity )
				return i;
		}
		return m_Entities.InvalidIndex();
	}

	void UpdateEntityVisibility( int index, int &nTraces )
	{
		EHANDLE hEntity = m_Entities[index].m_hEntity;
		CBaseEntity *pEntity = hEntity.Get();
		if ( !pEntity || pEntity->IsMarkedForDeletion() )
			return;

		const int nPlayers = MIN( gpGlobals->maxClients, MAX_PLAYERS );
		for ( int i = 1; i <= nPlayers; ++i )
		{
			const unsigned int nObserverBit = 1u << ( i & 31 );
			CBasePlayer *pObserver = UTIL_PlayerByIndex( i );
			if ( !pObserver || !pObserver->IsAlive() || pObserver->IsBot() )
			{
				m_Entities[index].m_nSeenByMask &= ~nObserverBit;
				continue;
			}
			if ( ( m_Entities[index].m_nSeenByMask & nObserverBit ) != 0 ||
				( pEntity->GetAbsOrigin() - pObserver->GetAbsOrigin() ).LengthSqr() > m_Entities[index].m_flRangeSqr )
			{
				continue;
			}

			++nTraces;
			trace_t trace;
			// S:0x09FAC0: origin distance, then eye to WorldSpaceCenter. No
			// FInViewCone test and no window/grate/opaque mask substitution.
			UTIL_TraceLine( pObserver->EyePosition(), pEntity->WorldSpaceCenter(),
				CONTENTS_SOLID | CONTENTS_MOVEABLE | CONTENTS_MONSTER, pObserver, COLLISION_GROUP_NONE, &trace );
			if ( trace.fraction != 1.0f && trace.m_pEnt != pEntity )
				continue;

			VisibilityMonitorCallback_t pPredicate = m_Entities[index].m_pPredicate;
			if ( pPredicate && !pPredicate( pEntity, pObserver ) )
				continue;

			VisibilityMonitorCallback_t pCallback = m_Entities[index].m_pCallback;
			bool bEmitVisible = !pCallback || pCallback( pEntity, pObserver );
			// Callbacks/events can create entities and grow the vector. Keep
			// no reference into its storage across either callback.
			pEntity = hEntity.Get();
			if ( !pEntity )
				return;
			if ( bEmitVisible )
			{
				IGameEvent *pEvent = gameeventmanager->CreateEvent( "entity_visible" );
				if ( pEvent )
				{
					pEvent->SetInt( "userid", pObserver->GetUserID() );
					pEvent->SetInt( "subject", pEntity->entindex() );
					pEvent->SetString( "classname", pEntity->GetClassname() );
					pEvent->SetString( "entityname", STRING( pEntity->GetEntityName() ) );
					gameeventmanager->FireEvent( pEvent );
				}
			}
			m_Entities[index].m_nSeenByMask |= nObserverBit;
		}
	}

	CUtlVector< VisibilityMonitorInfo > m_Entities;
	float m_flNextPoll;
	int m_nNextEntity;
};

static CVisibilityMonitor s_VisibilityMonitor;

void VisibilityMonitor_AddEntity( CBaseEntity *pEntity, float flRange,
	VisibilityMonitorCallback_t pCallback, VisibilityMonitorCallback_t pPredicate )
{
	s_VisibilityMonitor.AddEntity( pEntity, flRange, pCallback, pPredicate );
}

void VisibilityMonitor_RemoveEntity( CBaseEntity *pEntity )
{
	s_VisibilityMonitor.RemoveEntity( pEntity );
}
