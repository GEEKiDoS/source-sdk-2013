//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/entities/c_prop_health_cabinet.h"

#include "tier0/memdbgon.h"

// Keep the server spelling literal: the DT wrapper expands the shared CPropHealthCabinet alias.
IMPLEMENT_CLIENTCLASS( C_PropHealthCabinet, DT_PropHealthCabinet, CPropHealthCabinet )
BEGIN_RECV_TABLE( C_PropHealthCabinet, DT_PropHealthCabinet )
	RecvPropBool( RECVINFO( m_isUsed ) ),
END_RECV_TABLE()

int C_PropHealthCabinet::ObjectCaps()
{
	// Original C:268EC0, including the lack of a dormant/disabled gate.
	return m_isUsed ? BaseClass::ObjectCaps() : BaseClass::ObjectCaps() | FCAP_IMPULSE_USE | FCAP_USE_IN_RADIUS;
}
