//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/c_upgrade_item.h"
#include "l4d2/player/c_terrorplayer.h"
#include "tier0/memdbgon.h"

IMPLEMENT_CLIENTCLASS_DT( C_BaseUpgradeItem, DT_BaseUpgradeItem, CBaseUpgradeItem )
	RecvPropInt( RECVINFO( m_iUsedBySurvivorsMask ) ),
END_RECV_TABLE()

int C_BaseUpgradeItem::GetUseType( C_BaseEntity *pUser )
{
	// C:0x249580. Unlike the server's limited-use query, the client tests
	// the received mask directly; no prediction or per-client reset.
	C_TerrorPlayer *pPlayer = ToTerrorPlayer( pUser );
	if ( pPlayer )
	{
		const unsigned int bit = 1u << ( (unsigned int)pPlayer->GetSurvivorCharacter() & 31 );
		if ( ( m_iUsedBySurvivorsMask & bit ) == 0 )
			return BaseClass::GetUseType( pUser );
	}
	return 4;
}
