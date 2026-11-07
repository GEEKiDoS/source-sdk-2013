//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/entities/scavenge_progress_display.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "tier0/memdbgon.h"

LINK_ENTITY_TO_CLASS( game_scavenge_progress_display, CScavengeProgressDisplay );

// Native S2C5630 allocates1076: active byte1072 plus three tail-padding bytes.
// S2C5670/S2C56C0 write only that byte; FIELD_INTEGER saves the entire word.
// Evidence: native_scavenge_server_allocation.txt and native_scavenge_server_wire.txt.
static void SendProxy_ScavengeActive( const SendProp *pProp, const void *pStruct,
	const void *pData, DVariant *pOut, int iElement, int objectID )
{
	pOut->m_Int = *static_cast< const int * >( pData ) & 0xff;
}

IMPLEMENT_SERVERCLASS_ST( CScavengeProgressDisplay, DT_ScavengeProgressDisplay )
	SendPropInt( SENDINFO( m_bActive ), 1, SPROP_UNSIGNED, SendProxy_ScavengeActive ),
END_SEND_TABLE()

BEGIN_DATADESC( CScavengeProgressDisplay )
	DEFINE_FIELD( m_bActive, FIELD_INTEGER ),
	DEFINE_KEYFIELD( m_nTotalScavengeItems, FIELD_INTEGER, "Max" ),
	DEFINE_INPUTFUNC( FIELD_VOID, "TurnOn", InputTurnOn ),
	DEFINE_INPUTFUNC( FIELD_VOID, "TurnOff", InputTurnOff ),
	DEFINE_INPUTFUNC( FIELD_INTEGER, "SetTotalItems", InputSetTotalItems ),
END_DATADESC()


void CScavengeProgressDisplay::Spawn()
{
		BaseClass::Spawn();
		if ( TerrorGameRules() ) TerrorGameRules()->SetScavengeItemsGoal( m_nTotalScavengeItems );
		DispatchUpdateTransmitState();
	}

int CScavengeProgressDisplay::UpdateTransmitState()
{ return SetTransmitState( FL_EDICT_ALWAYS ); }

void CScavengeProgressDisplay::InputTurnOn( inputdata_t &data )
{ m_bActive = ( m_bActive.Get() & ~0xff ) | 1; }

void CScavengeProgressDisplay::InputTurnOff( inputdata_t &data )
{ m_bActive = m_bActive.Get() & ~0xff; }

void CScavengeProgressDisplay::InputSetTotalItems( inputdata_t &data )
{
		if ( TerrorGameRules() && !TerrorGameRules()->IsScavengeMode() )
			TerrorGameRules()->SetScavengeItemsGoal( data.value.FieldType() == FIELD_INTEGER ? data.value.Int() : 0 );
		// Native does not change this entity's saved Max key.
	}

