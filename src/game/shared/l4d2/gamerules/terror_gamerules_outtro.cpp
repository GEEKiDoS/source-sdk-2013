//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/gamerules/terror_gamerules.h"
#ifndef CLIENT_DLL
#include "l4d2/player/terrorplayer.h"
#include "util.h"
#endif
#include "tier0/memdbgon.h"

bool CTerrorGameRules::IsSacrificeEscapee( int playerIndex ) const
{
	// Native x86 SHL masks the count, including actual slot32 aliasing bit0.
	return ( (unsigned int)m_iSacrificeEscapees & ( 1u << ( (unsigned int)playerIndex & 31u ) ) ) != 0;
}
#ifndef CLIENT_DLL
bool CTerrorGameRules::IsSacrificeEscapee( CTerrorPlayer *player ) const
{
	// Native2DBF60 assumes its actual player argument, no invented NULL guard.
	return IsSacrificeEscapee( player->entindex() );
}
void CTerrorGameRules::UpdateSacrificeEscapees()
{
	// Native2E01A0; the ACTUAL trigger_finale::OnEscapeVehicleLeaving caller
	// already invokes this method after Director::OnFinaleVehicleLeaving and
	// before its map output. Do not fabricate a finale event listener.
	m_iSacrificeEscapees = 0;
	Q_memset( m_finaleEscapees, 0, sizeof( m_finaleEscapees ) );
	for ( int index = 1; index <= gpGlobals->maxClients; ++index )
	{
		// Native207410 resolves the real live-edict slot; it does NOT apply the
		// connected-player collector's additional admission. IsPlayer THEN Alive.
		CBaseEntity *entity = UTIL_EntityByIndex( index );
		if ( !entity || !entity->IsPlayer() ) continue;
		CTerrorPlayer *player = static_cast< CTerrorPlayer * >( entity );
		if ( !player->IsAlive() || player->GetTeamNumber() != TEAM_SURVIVOR ) continue;
		// SAME networked13240 handle, native2C7C60 raw resolved getter. No
		// CSurvivorPosition RTTI guard or independent rescue/camera ownership.
		CBaseEntity *position = player->GetSurvivorPosition();
		if ( !position || position == player ) continue;
		unsigned int bit = 1u << ( (unsigned int)index & 31u );
		m_iSacrificeEscapees = (int)( (unsigned int)m_iSacrificeEscapees | bit );
		unsigned int character = (unsigned int)player->GetSurvivorCharacter();
		if ( character <= 3u ) m_finaleEscapees[character] = true;
		DevMsg( "*!* [%u] Sacrifice Escapee %s [%d]<%u> \n", (unsigned int)gpGlobals->framecount, player->GetPlayerName(), index, bit );
	}
}
#endif
