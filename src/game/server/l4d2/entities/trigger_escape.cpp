//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "triggers.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/director/terror_director.h"
#include "tier0/memdbgon.h"

class CTriggerEscape : public CBaseTrigger
{
public:
	DECLARE_CLASS( CTriggerEscape, CBaseTrigger );
	DECLARE_DATADESC();
	CTriggerEscape() : m_escapePossible( -1 ) {}
	virtual void Spawn() OVERRIDE;
	virtual void InputEnable( inputdata_t &data ) OVERRIDE;
	virtual void InputDisable( inputdata_t &data ) OVERRIDE;
	virtual bool PassesTriggerFilters( CBaseEntity *other ) OVERRIDE;
private:
	void TeamEscapeThink();
	int m_escapePossible;
	COutputEvent m_OnEscapePossible;
	COutputEvent m_OnEscapeImpossible;
};
LINK_ENTITY_TO_CLASS( trigger_escape, CTriggerEscape );
BEGIN_DATADESC( CTriggerEscape )
	DEFINE_THINKFUNC( TeamEscapeThink ),
	DEFINE_OUTPUT( m_OnEscapePossible, "OnEscapePossible" ),
	DEFINE_OUTPUT( m_OnEscapeImpossible, "OnEscapeImpossible" ),
END_DATADESC()
void CTriggerEscape::Spawn()
{
	BaseClass::Spawn();
	InitTrigger();
	if ( !m_bDisabled )
	{
		SetThink( &CTriggerEscape::TeamEscapeThink );
		SetNextThink( gpGlobals->curtime + 10.0f + float( entindex() % 10 ) * 0.1f );
	}
}
void CTriggerEscape::InputEnable( inputdata_t &data )
{
	bool wasDisabled = m_bDisabled;
	BaseClass::InputEnable( data );
	if ( wasDisabled && !m_bDisabled )
	{
		SetThink( &CTriggerEscape::TeamEscapeThink );
		SetNextThink( gpGlobals->curtime + float( entindex() % 10 ) * 0.1f );
	}
}
void CTriggerEscape::InputDisable( inputdata_t &data )
{
	bool wasDisabled = m_bDisabled;
	BaseClass::InputDisable( data );
	if ( !wasDisabled && m_bDisabled )
	{
		SetThink( NULL );
		SetNextThink( TICK_NEVER_THINK );
		m_escapePossible = -1;
	}
}
bool CTriggerEscape::PassesTriggerFilters( CBaseEntity *other )
{
	return other && other->GetTeamNumber() == TEAM_SURVIVOR && other->IsPlayer() &&
		other->IsAlive() && BaseClass::PassesTriggerFilters( other );
}
void CTriggerEscape::TeamEscapeThink()
{
	if ( m_bDisabled ) return;
	SetNextThink( gpGlobals->curtime + 1.0f );
	if ( !TheDirector->IsRescueVehicleReady() ) return;
	int inside = 0, humansInside = 0, standingHumans = 0;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = static_cast< CTerrorPlayer * >( UTIL_PlayerByIndex( i ) );
		if ( !player || !player->edict() || !player->entindex() || !player->IsPlayer() || player->State_Get() == STATE_PICKINGTEAM ||
			player->GetTeamNumber() != TEAM_SURVIVOR || !player->IsAlive() ) continue;
		if ( IsTouching( player ) )
		{
			++inside;
			if ( !player->IsBot() ) ++humansInside;
		}
		if ( !player->IsIncapacitated() && !player->IsBot() ) ++standingHumans;
	}
	int possible = inside != 0 && ( standingHumans != 0 || humansInside != 0 );
	if ( possible != m_escapePossible )
	{
		if ( possible ) m_OnEscapePossible.FireOutput( NULL, this );
		else m_OnEscapeImpossible.FireOutput( NULL, this );
		m_escapePossible = possible;
	}
}
