//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/entities/func_extinguisher.h"
#include "l4d2/weapons/inferno.h"
#include "tier0/memdbgon.h"

LINK_ENTITY_TO_CLASS( func_extinguisher, CFuncExtinguisher );

BEGIN_DATADESC( CFuncExtinguisher )
	DEFINE_THINKFUNC( ExtinguishThink ),
END_DATADESC()

// S:0x2033F0. A map's explicit spawnflags key can replace these defaults.
CFuncExtinguisher::CFuncExtinguisher()
{
	AddSpawnFlags( 1 | 2 | 4 );
}

// S:0x203480. The inherited I/O descriptors dispatch the real input overrides.
void CFuncExtinguisher::Spawn()
{
	BaseClass::Spawn();
	SetSolid( SOLID_BSP );
	AddSolidFlags( FSOLID_NOT_SOLID );
	SetMoveType( MOVETYPE_NONE );
	SetModel( STRING( GetModelName() ) );
	AddEffects( EF_NODRAW );
	if ( !m_bDisabled )
	{
		SetThink( &CFuncExtinguisher::ExtinguishThink );
		SetNextThink( gpGlobals->curtime + ( entindex() % 10 ) * 0.1f );
	}
}

// S:0x1FCE60. Only an actual disabled-to-enabled transition arms the think.
void CFuncExtinguisher::InputEnable( inputdata_t &data )
{
	bool wasDisabled = m_bDisabled;
	BaseClass::InputEnable( data );
	if ( wasDisabled && !m_bDisabled )
	{
		SetThink( &CFuncExtinguisher::ExtinguishThink );
		SetNextThink( gpGlobals->curtime );
	}
}

// S:0x1F8820. Repeated Disable still clears the deadline, then the callback.
void CFuncExtinguisher::InputDisable( inputdata_t &data )
{
	BaseClass::InputDisable( data );
	if ( m_bDisabled )
	{
		SetNextThink( TICK_NEVER_THINK );
		SetThink( NULL );
	}
}

// S:0x1F8860. Do not replace this class scan with a damage/visual radius pulse.
void CFuncExtinguisher::ExtinguishClass( const char *pClassname )
{
	CBaseEntity *pEntity = NULL;
	while ( ( pEntity = gEntList.FindEntityByClassname( pEntity, pClassname ) ) != NULL )
	{
		CInferno *pInferno = dynamic_cast< CInferno * >( pEntity );
		if ( pInferno && pInferno->Extinguish( this ) )
		{
			UTIL_Remove( pInferno );
			break;
		}
	}
}

// S:0x1FBCE0. Preserve the original disabled-think and ignored-intersection bugs.
void CFuncExtinguisher::ExtinguishThink()
{
	if ( !m_bDisabled )
	{
		SetNextThink( gpGlobals->curtime + 1.0f );
		RemoveSolidFlags( FSOLID_NOT_SOLID );
		if ( HasSpawnFlags( 1 ) )
			ExtinguishClass( "inferno" );
		if ( HasSpawnFlags( 2 ) )
			ExtinguishClass( "insect_swarm" );
		if ( HasSpawnFlags( 4 ) )
			ExtinguishClass( "fire_cracker_blast" );
		AddSolidFlags( FSOLID_NOT_SOLID );
	}
}
