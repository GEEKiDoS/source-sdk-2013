//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "tier0/memdbgon.h"

static ConVar sv_spectatoridletime( "sv_spectatoridletime", "3", 0x00004004 );
void CTerrorPlayer::RefreshSpectatorIdleTime()
{
	// S:2FAB10. This is a deadline, separate from CCS last-input activity.
	m_flSpectatorIdleTime = gpGlobals->curtime + sv_spectatoridletime.GetFloat() * 60.0f;
}
static void FireStartAreaExplanation( CTerrorPlayer *player, const char *name )
{
	IGameEvent *event = gameeventmanager->CreateEvent( name );
	if ( event )
	{
		event->SetInt( "userid", player->GetUserID() );
		gameeventmanager->FireEvent( event );
	}
}
void CTerrorPlayer::OnLeftStartArea()
{
	// S:303970, reached only by the non-transition player_left_start_area path.
	static ConVarRef sv_disable_glow_survivors( "sv_disable_glow_survivors" );
	static ConVarRef sv_disable_glow_faritems( "sv_disable_glow_faritems" );
	static ConVarRef sv_rescue_disabled( "sv_rescue_disabled" );
	static ConVarRef z_non_head_damage_factor_multiplier( "z_non_head_damage_factor_multiplier" );
	static ConVarRef z_witch_always_kills( "z_witch_always_kills" );
	if ( sv_disable_glow_survivors.GetBool() )
		FireStartAreaExplanation( this, "explain_survivor_glows_disabled" );
	if ( sv_disable_glow_faritems.GetBool() )
		FireStartAreaExplanation( this, "explain_item_glows_disabled" );
	if ( sv_rescue_disabled.GetBool() )
		FireStartAreaExplanation( this, "explain_rescue_disabled" );
	if ( z_non_head_damage_factor_multiplier.GetFloat() < 1.0f )
		FireStartAreaExplanation( this, "explain_bodyshots_reduced" );
	if ( z_witch_always_kills.GetBool() && TerrorGameRules()->GetDifficultyLevel() != 0 )
		FireStartAreaExplanation( this, "explain_witch_instant_kill" );
}
