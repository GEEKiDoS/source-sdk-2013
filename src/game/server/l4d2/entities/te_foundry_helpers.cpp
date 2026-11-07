//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "basetempentity.h"
#include "recipientfilter.h"
#include "tier0/memdbgon.h"

// Native D6E30 / D7000. Registration survives even without the Foundry tool.
class CTEFoundryHelpers : public CBaseTempEntity
{
public:
	DECLARE_CLASS( CTEFoundryHelpers, CBaseTempEntity );
	DECLARE_SERVERCLASS();
	CTEFoundryHelpers( const char *name ) : BaseClass( name ) {}
	CNetworkVar( int, m_iEntity );
};
IMPLEMENT_SERVERCLASS_ST( CTEFoundryHelpers, DT_TEFoundryHelpers )
	SendPropInt( SENDINFO( m_iEntity ), 32, 0 ),
END_SEND_TABLE()

static CTEFoundryHelpers g_TEFoundryHelpers( "FoundryHelpers" );
void TE_FoundryHelpers()
{
	g_TEFoundryHelpers.m_iEntity = -1;
	CBroadcastRecipientFilter filter;
	g_TEFoundryHelpers.Create( filter, 0.0f );
}
