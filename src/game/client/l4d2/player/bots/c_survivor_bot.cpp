//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/bots/c_survivor_bot.h"
#include "l4d2/effects/glow_cvars.h"
#include "voice_status.h"
#include "tier0/memdbgon.h"

// C2D5880: the native factory invokes the real C_TerrorPlayer constructor.
// The inherited C_BaseEntity allocator zeroes both additional integer fields;
// the original has no separate bot-field constructor/reset or prediction copy.
IMPLEMENT_CLIENTCLASS_DT( C_SurvivorBot, DT_SurvivorBot, SurvivorBot )
	RecvPropInt( RECVINFO( m_humanSpectatorUserID ) ),
	RecvPropInt( RECVINFO( m_humanSpectatorEntIndex ) ),
END_RECV_TABLE()

// C2D5870, original primary vtable byte1072: actual concrete bot identity.
bool C_SurvivorBot::IsBot( void ) const
{
	return true;
}

bool C_SurvivorBot::ShouldDrawSpeechVoice()
{
	if ( sv_disable_glow_survivors.GetInt() != 0 )
		return false;
	bool speaking = GetClientVoiceMgr()->IsPlayerSpeaking( entindex() );
	if ( GetHumanSpectatorUserID() > 0 )
	{
		const int humanIndex = GetHumanSpectatorEntIndex();
		C_BasePlayer *human = UTIL_PlayerByIndex( humanIndex );
		if ( human && human->IsLocalPlayer() )
		{
			(void)GetClientVoiceMgr();
			return false;
		}
		if ( humanIndex > 0 )
			speaking = GetClientVoiceMgr()->IsPlayerSpeaking( humanIndex );
	}
	return speaking && !IsLocalPlayer();
}
