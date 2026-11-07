//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//
//
//  hud_msg.cpp
//
#include "cbase.h"
#include "clientmode.h"
#include "hudelement.h"
#include "KeyValues.h"
#include "vgui_controls/AnimationController.h"
#include "engine/IEngineSound.h"
#include <bitbuf.h>

#ifdef L4D2
#include "l4d2/player/c_terrorplayer.h"
#include "l4d2/clientmode_terror.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

/// USER-DEFINED SERVER MESSAGE HANDLERS

void CHud::MsgFunc_ResetHUD( bf_read &msg )
{
	ResetHUD();
}

#ifdef L4D2
void CHud::MsgFunc_Spawn( bf_read &msg )
{
	C_TerrorPlayer *player = ToTerrorPlayer( C_BasePlayer::GetLocalPlayer() );
	if ( !player )
		return;
	player->ChangeTeam( msg.ReadByte() );
	player->ResetForSpawn();
	engine->ClientCmd( "-attack; -attack2; -duck; -speed; -forward; -moveright; -moveleft; -back; -use\n" );
}

void CHud::MsgFunc_TransitionRestore( bf_read & )
{
	for ( int i = 0; i < m_HudList.Count(); ++i )
		m_HudList[i]->OnTransitionRestore();
}
#endif

void CHud::ResetHUD()
{
	// clear all hud data
	g_pClientMode->GetViewportAnimationController()->CancelAllAnimations();

	for ( int i = 0; i < m_HudList.Size(); i++ )
	{
		m_HudList[i]->Reset();
	}

	g_pClientMode->GetViewportAnimationController()->RunAllAnimationsToCompletion();
#ifndef _XBOX
	// reset sensitivity
	m_flMouseSensitivity = 0;
	m_flMouseSensitivityFactor = 0;
#endif
#if defined( L4D2 )
	static_cast< ClientModeTerrorNormal * >( GetClientModeNormal() )->InitHUD();
#endif
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------

void CHud::MsgFunc_SendAudio( bf_read &msg )
{
	char szString[2048];
	msg.ReadString( szString, sizeof(szString) );
	
	CLocalPlayerFilter filter;
	C_BaseEntity::EmitSound( filter, SOUND_FROM_LOCAL_PLAYER, szString );
}
