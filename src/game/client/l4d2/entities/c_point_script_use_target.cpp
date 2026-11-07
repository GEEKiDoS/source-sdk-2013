//========= Copyright Valve Corporation, All rights reserved. ============//

#include "cbase.h"
#include "l4d2/entities/c_point_script_use_target.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

IMPLEMENT_CLIENTCLASS_DT( C_PointScriptUseTarget, DT_PointScriptUseTarget, CPointScriptUseTarget )
	RecvPropString( RECVINFO( m_sUseString ) ),
	RecvPropString( RECVINFO( m_sUseSubString ) ),
	RecvPropFloat( RECVINFO( m_flPreviousProgressPercent ) ),
	RecvPropBool( RECVINFO( m_bCanShowBuildPanel ) ),
END_RECV_TABLE()

C_PointScriptUseTarget::C_PointScriptUseTarget()
{
	V_memset( m_sUseString, 0, sizeof( m_sUseString ) );
	V_memset( m_sUseSubString, 0, sizeof( m_sUseSubString ) );
	m_flPreviousProgressPercent = 0.0f;
	m_bCanShowBuildPanel = false;
}
