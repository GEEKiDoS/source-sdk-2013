//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/entities/brush_roots_client.h"
#include "tier0/memdbgon.h"

IMPLEMENT_CLIENTCLASS_DT( C_BaseTrigger, DT_BaseTrigger, CBaseTrigger )
	RecvPropBool( RECVINFO( m_bClientSidePredicted ) ),
	RecvPropInt( RECVINFO( m_spawnflags ) ),
END_RECV_TABLE()
IMPLEMENT_CLIENTCLASS_DT( C_ScriptBaseButton, DT_ScriptBaseButton, CScriptBaseButton )
END_RECV_TABLE()

