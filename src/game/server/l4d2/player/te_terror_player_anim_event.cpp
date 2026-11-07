//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "basetempentity.h"
#include "recipientfilter.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/player/terror_playeranimstate.h"
#include "tier0/memdbgon.h"

class CTEPlayerAnimEvent : public CBaseTempEntity
{
public:
	DECLARE_CLASS( CTEPlayerAnimEvent, CBaseTempEntity );
	DECLARE_SERVERCLASS();

	CTEPlayerAnimEvent( const char *pName ) : BaseClass( pName ) {}

	CNetworkHandle( CBasePlayer, m_hPlayer );
	CNetworkVar( int, m_iEvent );
	CNetworkVar( int, m_nData );
};

// Native DT_TEPlayerAnimEvent: handle21, original event7, signed data32.
IMPLEMENT_SERVERCLASS_ST_NOBASE( CTEPlayerAnimEvent, DT_TEPlayerAnimEvent )
	SendPropEHandle( SENDINFO( m_hPlayer ) ),
	SendPropInt( SENDINFO( m_iEvent ), 7, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_nData ), 32 ),
END_SEND_TABLE()

static CTEPlayerAnimEvent g_TEPlayerAnimEvent( "PlayerAnimEvent" );

void TE_TerrorPlayerAnimEvent( CTerrorPlayer *pPlayer, int nOriginalEvent, int nData )
{
	// S244D90 uses EyePosition/PVS, not shot PAS or prediction suppression.
	CPVSFilter filter( pPlayer->EyePosition() );
	if ( nOriginalEvent == 48 || nOriginalEvent == 46 )
		filter.MakeReliable();
	g_TEPlayerAnimEvent.m_hPlayer = pPlayer;
	g_TEPlayerAnimEvent.m_iEvent = nOriginalEvent;
	g_TEPlayerAnimEvent.m_nData = nData;
	g_TEPlayerAnimEvent.Create( filter, 0.0f );
}
