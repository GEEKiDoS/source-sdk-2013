//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "NextBot/NextBotInterface.h"
#include "l4d2/player/bots/survivor_body.h"
#include "l4d2/player/terrorplayer.h"
#include "tier0/memdbgon.h"

// Original server 38AFE0, 5A14D0, 5A1500.
ConVar sb_normal_saccade_speed( "sb_normal_saccade_speed", "350", 0x00004004 );
ConVar sb_combat_saccade_speed( "sb_combat_saccade_speed", "1000", 0x00004004 );

SurvivorBody::SurvivorBody( INextBot *bot ) : PlayerBody( bot )
{
}

void SurvivorBody::Upkeep()
{
	// 38B050: an externally owned view suppresses the actual player-body aim
	// producer. It does not reset its aim target, reply, or expiry timer.
	CBasePlayer *player = static_cast< CBasePlayer * >( GetBot()->GetEntity() );
	CBaseEntity *view = player->GetViewEntity();
	if ( view == player || view == NULL )
		PlayerBody::Upkeep();
}

float SurvivorBody::GetMaxHeadAngularVelocity() const
{
	CTerrorPlayer *player = static_cast< CTerrorPlayer * >( GetBot()->GetEntity() );
	return player->IsInCombat( false ) ? sb_combat_saccade_speed.GetFloat() : sb_normal_saccade_speed.GetFloat();
}
