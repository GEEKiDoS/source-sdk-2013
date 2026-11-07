//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "triggers.h"
#include "team.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/player/terrorplayer.h"
#include "tier0/memdbgon.h"

CTriggerMultiple::CTriggerMultiple()
{
	m_bAllowIncapTouch = false;
	m_bAllowGhostTouch = false;
}

// S:1F7DC0/1FC160/1FD090: team presence is initialized from GetNumPlayers,
// then every eligible, non-PICKINGTEAM player is examined in entity-index order.
namespace
{
	struct CTriggerTeamTouchState
	{
		CTriggerTeamTouchState( const CBaseTrigger *trigger, int team, bool allowIncap, bool allowGhost )
			: m_trigger( trigger ), m_team( team ), m_allowIncap( allowIncap ), m_allowGhost( allowGhost ), m_all( true ), m_count( 0 )
		{
			CTeam *owner = GetGlobalTeam( team );
			if ( owner )
				m_all = owner->GetNumPlayers() != 0;
		}
		void Evaluate()
		{
			for ( int i = 1; i <= gpGlobals->maxClients; ++i )
			{
				CBasePlayer *base = UTIL_PlayerByIndex( i );
				if ( !base || !base->edict() || !base->entindex() || !base->IsPlayer() )
					continue;
				CTerrorPlayer *player = static_cast< CTerrorPlayer * >( base );
				if ( player->State_Get() == STATE_PICKINGTEAM )
					continue;
				if ( !player->IsAlive() || player->GetTeamNumber() != m_team ||
					( !m_allowIncap && player->IsIncapacitated() ) || ( !m_allowGhost && player->IsGhost() ) )
					continue;
				if ( !m_trigger->IsTouching( player ) )
				{
					m_all = false;
					break;
				}
				++m_count;
			}
		}
		const CBaseTrigger *m_trigger;
		int m_team;
		bool m_allowIncap, m_allowGhost, m_all;
		int m_count;
	};
}
bool CTriggerMultiple::CheckEntireTeam( int *count ) const
{
	CTriggerTeamTouchState state( this, m_iEntireTeam, m_bAllowIncapTouch, m_bAllowGhostTouch );
	state.Evaluate();
	*count = state.m_count;
	return state.m_all;
}

void CTriggerMultiple::Spawn() // S:2011F0
{
	BaseClass::Spawn();
	// This map-specific repair is present in the pinned native binary.
	if ( !Q_stricmp( STRING( gpGlobals->mapname ), "c5m2_park" ) && NameMatches( "finale_decon_trigger" ) )
	{
		m_OnEndTouch.DeleteAllElements();
		KeyValue( "OnEntireTeamEndTouch", "ceda_trailer_allinside_branch,SetValueTest,0,0,-1" );
	}
	InitTrigger();
	if ( m_flWait == 0.0f )
		m_flWait = 0.2f;
	m_bEntireTeamTriggered = false;
	SetTouch( &CTriggerMultiple::MultiTouch );
	if ( m_iEntireTeam )
	{
		SetThink( &CTriggerMultiple::TeamTouchThink );
		SetNextThink( ( gpGlobals->curtime + 10.0f ) + ( entindex() % 10 ) * 0.1f );
	}
}
bool CTriggerMultiple::PassesTriggerFilters( CBaseEntity *other ) // S:1FAA70
{
	return BaseClass::PassesTriggerFilters( other ) &&
		( m_bAllowGhostTouch || !other || !other->IsPlayer() || !static_cast< CTerrorPlayer * >( other )->IsGhost() );
}
void CTriggerMultiple::MultiTouch( CBaseEntity *other ) // S:1FD5C0
{
	if ( !PassesTriggerFilters( other ) )
		return;
	if ( m_iEntireTeam )
	{
		if ( !m_bEntireTeamTriggered && other->IsPlayer() && other->GetTeamNumber() == m_iEntireTeam )
		{
			int count;
			if ( CheckEntireTeam( &count ) && count > 0 && TheDirector->GetScriptedMode()->CanTriggerVIP( this ) )
			{
				m_OnEntireTeamStartTouch.FireOutput( other, this );
				m_bEntireTeamTriggered = true;
				ActivateMultiTrigger( other );
				return;
			}
		}
	}
	else
	{
		// Native EndTouch does NOT prevent the final ordinary OnTrigger.
		if ( !m_bAllowIncapTouch && other && other->IsPlayer() && static_cast< CTerrorPlayer * >( other )->IsIncapacitated() )
			EndTouch( other );
		if ( !m_bAllowGhostTouch && other && other->IsPlayer() && static_cast< CTerrorPlayer * >( other )->IsGhost() )
			EndTouch( other );
	}
	ActivateMultiTrigger( other );
}
void CTriggerMultiple::StartTouch( CBaseEntity *other ) // S:1FEAC0
{
	if ( !m_iEntireTeam || other->GetTeamNumber() != m_iEntireTeam )
	{
		BaseClass::StartTouch( other );
		return;
	}
	CTriggerTeamTouchState before( this, m_iEntireTeam, m_bAllowIncapTouch, m_bAllowGhostTouch );
	CTriggerTeamTouchState after( this, m_iEntireTeam, m_bAllowIncapTouch, m_bAllowGhostTouch );
	before.Evaluate();
	BaseClass::StartTouch( other );
	after.Evaluate();
	if ( !before.m_all && after.m_all && TheDirector->GetScriptedMode()->CanTriggerVIP( this ) )
	{
		m_OnEntireTeamStartTouch.FireOutput( other, this );
		m_bEntireTeamTriggered = true;
	}
}
void CTriggerMultiple::EndTouch( CBaseEntity *other ) // S:1FD820
{
	if ( !m_iEntireTeam || other->GetTeamNumber() != m_iEntireTeam )
	{
		BaseClass::EndTouch( other );
		return;
	}
	CTriggerTeamTouchState before( this, m_iEntireTeam, m_bAllowIncapTouch, m_bAllowGhostTouch );
	CTriggerTeamTouchState after( this, m_iEntireTeam, m_bAllowIncapTouch, m_bAllowGhostTouch );
	before.Evaluate();
	BaseClass::EndTouch( other );
	after.Evaluate();
	if ( before.m_all && !after.m_all )
	{
		m_OnEntireTeamEndTouch.FireOutput( other, this );
		m_bEntireTeamTriggered = false;
	}
}
void CTriggerMultiple::TeamTouchThink() // S:1FD730
{
	SetNextThink( gpGlobals->curtime + 1.0f );
	int count;
	bool all = CheckEntireTeam( &count );
	if ( m_bEntireTeamTriggered )
	{
		if ( !all )
		{
			m_OnEntireTeamEndTouch.FireOutput( NULL, this );
			m_bEntireTeamTriggered = false;
		}
	}
	else if ( all && count > 0 && TheDirector->GetScriptedMode()->CanTriggerVIP( this ) )
	{
		m_OnEntireTeamStartTouch.FireOutput( NULL, this );
		m_bEntireTeamTriggered = true;
	}
}
int CTriggerMultiple::DrawDebugTextOverlays() // S:1FA850
{
	int line = BaseClass::DrawDebugTextOverlays();
	if ( ( m_debugOverlays & OVERLAY_TEXT_BIT ) && m_iEntireTeam )
	{
		EntityText( line++, CFmtStr( "Team %d", m_iEntireTeam ), 0.0f );
		EntityText( line++, CFmtStr( "Allow incaps: %d", m_bAllowIncapTouch ), 0.0f );
		EntityText( line++, CFmtStr( "Allow ghosts: %d", m_bAllowGhostTouch ), 0.0f );
		EntityText( line++, m_bEntireTeamTriggered ? "Entire team touching" : "Entire team not touching", 0.0f,
			255, m_bEntireTeamTriggered ? 255 : 0, m_bEntireTeamTriggered ? 255 : 0 );
		FOR_EACH_VEC( m_hTouchingEntities, i )
		{
			CBaseEntity *entity = m_hTouchingEntities[i].Get();
			if ( entity )
				EntityText( line++, CFmtStr( "Touching: %d(%s)\n", entity->entindex(), entity->GetDebugName() ), 0.0f, 0, 255, 64 );
		}
	}
	return line;
}
