//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/entities/world_logic_extras.h"
#include "filters.h"
#include "lights.h"
#include "l4d2/player/terrorplayer.h"
#include "tier0/memdbgon.h"

LINK_ENTITY_TO_CLASS( info_player_logo, CPointEntity );

LINK_ENTITY_TO_CLASS( light_directional, CLight );

LINK_ENTITY_TO_CLASS( filter_activator_model, CFilterModel );

BEGIN_DATADESC( CFilterModel )
	DEFINE_KEYFIELD( m_iFilterModel, FIELD_STRING, "model" ),
END_DATADESC()

LINK_ENTITY_TO_CLASS( filter_activator_context, CFilterContext );

BEGIN_DATADESC( CFilterContext )
	DEFINE_KEYFIELD( m_iFilterContext, FIELD_STRING, "ResponseContext" ),
END_DATADESC()

LINK_ENTITY_TO_CLASS( filter_activator_infected_class, FilterInfectedClass );

BEGIN_DATADESC( FilterInfectedClass )
	DEFINE_KEYFIELD( m_iFilterInfectedClass, FIELD_INTEGER, "filterinfectedclass" ),
END_DATADESC()

LINK_ENTITY_TO_CLASS( filter_melee_damage, FilterMeleeDamage );

BEGIN_DATADESC( FilterMeleeDamage )
	DEFINE_KEYFIELD( m_iDamageType, FIELD_INTEGER, "damagetype" ),
END_DATADESC()

LINK_ENTITY_TO_CLASS( filter_health, FilterHealth );

BEGIN_DATADESC( FilterHealth )
	DEFINE_KEYFIELD( m_bAdrenalineActive, FIELD_BOOLEAN, "adrenalinepresence" ),
	DEFINE_KEYFIELD( m_iHealthMin, FIELD_INTEGER, "healthmin" ),
	DEFINE_KEYFIELD( m_iHealthMax, FIELD_INTEGER, "healthmax" ),
END_DATADESC()

LINK_ENTITY_TO_CLASS( info_particle_target, CInfoParticleTarget );

LINK_ENTITY_TO_CLASS( point_surroundtest, CSurroundTest );

BEGIN_DATADESC( CSurroundTest )
	DEFINE_INPUTFUNC( FIELD_VOID, "FireCorrectOutput", FireCorrectOutput ),
	DEFINE_OUTPUT( m_On2Speakers, "On2Speakers" ),
	DEFINE_OUTPUT( m_On4Speakers, "On4Speakers" ),
	DEFINE_OUTPUT( m_On51Speakers, "On51Speakers" ),
END_DATADESC()


bool CFilterModel::PassesFilterImpl( CBaseEntity *caller, CBaseEntity *entity )
{
		// Main-approved minimal skip of the native NULL-entity dereference.
		return entity && FStrEq( STRING( m_iFilterModel ), STRING( entity->GetModelName() ) );
	}

bool CFilterContext::PassesFilterImpl( CBaseEntity *caller, CBaseEntity *entity )
{
		// No context-expiration cleanup here: native queries the stored entry.
		if ( !entity ) return false; // Main-approved minimal target-only skip.
		const int index = entity->FindContextByName( STRING( m_iFilterContext ) );
		return index != -1 && atoi( entity->GetContextValue( index ) ) > 0;
	}

bool FilterInfectedClass::PassesFilterImpl( CBaseEntity *caller, CBaseEntity *entity )
{
		if ( !entity || !entity->IsPlayer() || !entity->IsAlive() ) return false;
		CTerrorPlayer *player = static_cast< CTerrorPlayer * >( entity );
		if ( player->IsGhost() ) return false;
		return player->GetTeamNumber() == TEAM_INFECTED && player->GetZombieClass() == m_iFilterInfectedClass;
	}

bool FilterMeleeDamage::PassesFilterImpl( CBaseEntity *caller, CBaseEntity *entity )
{ return true; }

bool FilterMeleeDamage::PassesDamageFilterImpl( const CTakeDamageInfo &info )
{
		const int damageType = info.GetDamageType();
		return ( damageType & 0x04200000 ) && ( m_iDamageType <= 0 || ( m_iDamageType & damageType ) );
	}

bool FilterHealth::PassesFilterImpl( CBaseEntity *caller, CBaseEntity *entity )
{
		if ( !entity || !entity->IsPlayer() ) return false;
		CTerrorPlayer *player = static_cast< CTerrorPlayer * >( entity );
		return player->IsAdrenalineActive() == m_bAdrenalineActive &&
			player->GetHealth() >= m_iHealthMin && player->GetHealth() <= m_iHealthMax;
	}

bool FilterHealth::PassesDamageFilterImpl( const CTakeDamageInfo &info )
{
		return PassesFilterImpl( NULL, info.GetAttacker() );
	}

int CInfoParticleTarget::UpdateTransmitState()
{ return SetTransmitState( FL_EDICT_ALWAYS ); }

void CSurroundTest::FireCorrectOutput( inputdata_t &inputdata )
{
		ConVar *speakers = cvar->FindVar( "snd_surround_speakers" );
		if ( !speakers ) return;
		switch ( speakers->GetInt() )
		{
		case 0: case 2: m_On2Speakers.FireOutput( this, this ); break;
		case 4: m_On4Speakers.FireOutput( this, this ); break;
		case 5: m_On51Speakers.FireOutput( this, this ); break;
		}
	}


